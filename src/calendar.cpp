#include "calendar.h"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <bit>
#include <charconv>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace deck {
using namespace std::chrono;
std::wstring wide(const std::string& s) {
    if(s.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);
    if(!n) throw std::runtime_error("Invalid UTF-8 in calendar");
    std::wstring r(n,0); MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),r.data(),n); return r;
}
std::string utf8(const std::wstring& s) {
    if(s.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);
    std::string r(n,0); WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),r.data(),n,nullptr,nullptr); return r;
}
Stamp civil(int y,unsigned m,unsigned d,int h,int min,int s) {
    year_month_day date{year{y},month{m},day{d}};
    if(!date.ok() || y<1601 || y>9998 || h<0 || h>23 || min<0 || min>59 || s<0 || s>60)
        throw std::runtime_error("Invalid calendar date");
    return duration_cast<seconds>(sys_days{date}.time_since_epoch()).count()+h*3600+min*60+std::min(s,59);
}
Parts parts(Stamp t) {
    auto sd=floor<days>(sys_seconds{seconds{t}}); year_month_day d{sd};
    auto sec=(sys_seconds{seconds{t}}-sd).count();
    return {int(d.year()),unsigned(d.month()),unsigned(d.day()),int(sec/3600),int(sec/60%60),int(sec%60),int((weekday{sd}.iso_encoding()+6)%7)};
}
static SYSTEMTIME systemTime(Stamp t) {
    auto p=parts(t); SYSTEMTIME s{}; s.wYear=WORD(p.y); s.wMonth=WORD(p.m); s.wDay=WORD(p.d);
    s.wHour=WORD(p.hour); s.wMinute=WORD(p.minute); s.wSecond=WORD(p.second); return s;
}
static Stamp stamp(const SYSTEMTIME& s) { return civil(s.wYear,s.wMonth,s.wDay,s.wHour,s.wMinute,s.wSecond); }
Stamp today() { SYSTEMTIME s{}; GetLocalTime(&s); return civil(s.wYear,s.wMonth,s.wDay); }
Stamp monday(Stamp t) { auto p=parts(t); return civil(p.y,p.m,p.d)-p.weekday*daySeconds; }
Stamp monthStart(Stamp t) { auto p=parts(t); return civil(p.y,p.m,1); }
Stamp yearStart(Stamp t) { auto p=parts(t); return civil(p.y,1,1); }
Stamp toLocal(Stamp t) {
    auto u=systemTime(t); SYSTEMTIME l{};
    if(!SystemTimeToTzSpecificLocalTimeEx(nullptr,&u,&l)) throw std::runtime_error("Cannot convert local time");
    return stamp(l);
}
Stamp toUtc(Stamp t,const std::string& zone) {
    if(zone=="UTC" || zone=="Etc/UTC" || zone=="GMT") return t;
    // Thread-local cache avoids enumerating the Windows zone database for every occurrence.
    thread_local std::unordered_map<std::string,DYNAMIC_TIME_ZONE_INFORMATION> zones;
    DYNAMIC_TIME_ZONE_INFORMATION* z=nullptr;
    if(!zone.empty()) {
        auto found=zones.find(zone);
        if(found==zones.end()) {
            static HMODULE icu=LoadLibraryExW(L"icu.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
            using Map=int32_t (__cdecl*)(const wchar_t*,int32_t,wchar_t*,int32_t,int32_t*);
            static auto map=icu?std::bit_cast<Map>(GetProcAddress(icu,"ucal_getWindowsTimeZoneID")):nullptr;
            wchar_t key[128]{}; int32_t error=0; auto id=wide(zone);
            if(map) map(id.c_str(),int32_t(id.size()),key,128,&error);
            if(!*key || error>0) wcsncpy_s(key,id.c_str(),_TRUNCATE); // Also accept Windows IDs.
            DYNAMIC_TIME_ZONE_INFORMATION info{}; bool ok=false;
            for(DWORD i=0;EnumDynamicTimeZoneInformation(i,&info)==ERROR_SUCCESS;++i) {
                if(wcscmp(info.TimeZoneKeyName,key)==0) {ok=true;break;}
            }
            if(!ok) throw std::runtime_error("Unsupported time zone: "+zone);
            found=zones.emplace(zone,info).first;
        }
        z=&found->second;
    }
    auto local=systemTime(t); SYSTEMTIME utc{};
    if(!TzSpecificLocalTimeToSystemTimeEx(z,&local,&utc)) throw std::runtime_error("Cannot convert calendar time zone");
    return stamp(utc);
}
static int number(const std::string& s) {
    int v{};const char* begin=s.data();if(s.size()>1&&s.front()=='+')++begin;
    auto [p,ec]=std::from_chars(begin,s.data()+s.size(),v);
    if(ec!=std::errc{} || p!=s.data()+s.size()) throw std::runtime_error("Invalid calendar number");
    return v;
}
static std::vector<std::string> split(const std::string& s,char c) {
    std::vector<std::string> r; size_t a=0,b;
    do { b=s.find(c,a); r.push_back(s.substr(a,b==s.npos?b:b-a)); a=b+1; } while(b!=s.npos);
    return r;
}
static std::string upper(std::string s) {for(auto& c:s) if(c>='a'&&c<='z') c=char(c-'a'+'A');return s;}
static std::string unescape(const std::string& s) {
    std::string r; for(size_t i=0;i<s.size();++i) {
        if(s[i]=='\\' && i+1<s.size()) {char c=s[++i]; r+=c=='n'||c=='N'?'\n':c;} else r+=s[i];
    } return r;
}
struct Value {Stamp wall{}; std::string zone; bool date{};};
struct Property {std::string key,value; std::map<std::string,std::string> params;};
static Property property(const std::string& line) {
    bool quote=false; size_t colon=0;
    for(;colon<line.size();++colon) {if(line[colon]=='"') quote=!quote;if(line[colon]==':'&&!quote)break;}
    if(colon==line.size()) throw std::runtime_error("Invalid iCalendar property");
    auto fields=split(line.substr(0,colon),';'); Property p{upper(fields[0]),line.substr(colon+1),{}};
    for(size_t i=1;i<fields.size();++i) {auto eq=fields[i].find('=');if(eq!=std::string::npos){auto v=fields[i].substr(eq+1);if(v.size()>=2&&v.front()=='"'&&v.back()=='"')v=v.substr(1,v.size()-2);p.params[upper(fields[i].substr(0,eq))]=v;}}
    return p;
}
static Value value(const Property& p) {
    auto s=p.value; Value v{}; v.date=s.size()==8;
    if(!v.date && s.size()!=15 && s.size()!=16) throw std::runtime_error("Unsupported date format");
    if(s.size()<8 || (!v.date && (s[8]!='T' || (s.size()==16 && s.back()!='Z')))) throw std::runtime_error("Invalid date format");
    v.wall=civil(number(s.substr(0,4)),number(s.substr(4,2)),number(s.substr(6,2)),v.date?0:number(s.substr(9,2)),v.date?0:number(s.substr(11,2)),v.date?0:number(s.substr(13,2)));
    auto tz=p.params.find("TZID");if(tz!=p.params.end())v.zone=tz->second;
    if(!v.date && s.back()=='Z')v.zone="UTC";
    return v;
}
static Stamp absolute(const Value& v) {return v.date?v.wall:toUtc(v.wall,v.zone);}
static Stamp durationValue(std::string s) {
    if(s.empty() || s.front()!='P')throw std::runtime_error("Unsupported event duration");
    Stamp total=0,n=0;bool time=false;
    for(size_t i=1;i<s.size();++i){char c=s[i];if(c=='T'){time=true;continue;}if(c>='0'&&c<='9'){n=n*10+c-'0';if(n>31536000)throw std::runtime_error("Event duration too large");continue;}
        Stamp unit=c=='W'?604800:c=='D'?86400:time&&c=='H'?3600:time&&c=='M'?60:time&&c=='S'?1:0;
        if(!unit)throw std::runtime_error("Invalid event duration");
        total+=n*unit;n=0;
    } if(n || total>366*daySeconds)throw std::runtime_error("Invalid event duration");return total;
}
struct Event {
    std::string uid,title,location,description,rule,status;
    Value start,end,rid; bool hasStart{},hasEnd{},hasRid{},range{}; Stamp duration{-1};
    std::vector<Value> exclude,include; int sequence{}; Stamp modified{};
};
struct Rule {
    std::string freq; int interval{1},count{0},wkst{0}; bool untilSet{}; Value until;
    std::vector<int> months,monthdays,setpos; std::vector<std::pair<int,int>> weekdays;
};
static int weekdayNumber(const std::string& s) {
    static const std::string names="MOTUWETHFRSASU";
    auto p=names.find(s);if(s.size()!=2 || p==names.npos || p%2)throw std::runtime_error("Invalid recurrence weekday");return int(p/2);
}
static Rule rule(const std::string& s) {
    Rule r;
    for(auto& field:split(s,';')) {
        auto eq=field.find('=');if(eq==field.npos)throw std::runtime_error("Invalid recurrence rule");
        auto k=upper(field.substr(0,eq)),v=upper(field.substr(eq+1));
        if(k=="FREQ")r.freq=v;
        else if(k=="INTERVAL")r.interval=number(v);
        else if(k=="COUNT"){r.count=number(v);if(r.count<1)throw std::runtime_error("COUNT must be positive");}
        else if(k=="UNTIL"){r.until=value({"",v,{}});r.untilSet=true;}
        else if(k=="WKST")r.wkst=weekdayNumber(v);
        else if(k=="BYDAY")for(auto& w:split(v,',')){if(w.size()<2)throw std::runtime_error("Invalid BYDAY");int ord=w.size()==2?0:number(w.substr(0,w.size()-2));if(ord< -53||ord>53)throw std::runtime_error("Invalid BYDAY ordinal");r.weekdays.emplace_back(ord,weekdayNumber(w.substr(w.size()-2)));}
        else if(k=="BYMONTH" || k=="BYMONTHDAY" || k=="BYSETPOS") {
            auto& dest=k=="BYMONTH"?r.months:k=="BYMONTHDAY"?r.monthdays:r.setpos;
            for(auto& n:split(v,',')){int x=number(n);int max=k=="BYMONTH"?12:k=="BYMONTHDAY"?31:366;if(!x||x>max||x< -max||(k=="BYMONTH"&&x<0))throw std::runtime_error("Invalid recurrence selector");dest.push_back(x);}
        } else throw std::runtime_error("Unsupported recurrence field: "+k);
    }
    if(r.freq!="DAILY"&&r.freq!="WEEKLY"&&r.freq!="MONTHLY"&&r.freq!="YEARLY")throw std::runtime_error("Unsupported recurrence frequency: "+r.freq);
    if(r.interval<1||r.interval>10000||r.count<0)throw std::runtime_error("Invalid recurrence interval/count");
    for(auto [ord,wd]:r.weekdays){(void)wd;if(ord&&(r.freq=="DAILY"||r.freq=="WEEKLY"))throw std::runtime_error("Ordinal BYDAY requires monthly/yearly recurrence");if(ord&&(ord< -5||ord>5||(r.freq=="YEARLY"&&r.months.empty())))throw std::runtime_error("Unsupported yearly BYDAY ordinal");}
    return r;
}
int monthLength(int y,unsigned m) {return int(unsigned(year_month_day_last{year{y},month_day_last{month{m}}}.day()));}
static bool contains(const std::vector<int>& v,int x) {return std::find(v.begin(),v.end(),x)!=v.end();}
static bool matches(const Rule& r,Stamp date,Stamp start) {
    auto p=parts(date),a=parts(start);int days=int((date-start)/daySeconds),months=(p.y-a.y)*12+int(p.m)-int(a.m);
    if(r.freq=="DAILY"&&days%r.interval)return false;
    if(r.freq=="WEEKLY"){auto w=date-((p.weekday-r.wkst+7)%7)*daySeconds;auto first=start-((a.weekday-r.wkst+7)%7)*daySeconds;if(((w-first)/(7*daySeconds))%r.interval)return false;}
    if(r.freq=="MONTHLY"&&months%r.interval)return false;
    if(r.freq=="YEARLY"&&(p.y-a.y)%r.interval)return false;
    if(!r.months.empty()&&!contains(r.months,int(p.m)))return false;
    int length=monthLength(p.y,p.m);
    if(!r.monthdays.empty()&&!contains(r.monthdays,int(p.d))&&!contains(r.monthdays,int(p.d)-length-1))return false;
    if(!r.weekdays.empty()) {
        bool ok=false;for(auto [ord,wd]:r.weekdays)if(wd==p.weekday && (!ord || (ord>0?(int(p.d)-1)/7+1:-(length-int(p.d))/7-1)==ord))ok=true;
        if(!ok)return false;
    } else if(r.freq=="WEEKLY"&&p.weekday!=a.weekday)return false;
    if((r.freq=="MONTHLY"||r.freq=="YEARLY")&&r.monthdays.empty()&&r.weekdays.empty()&&p.d!=a.d)return false;
    if(r.freq=="YEARLY"&&r.months.empty()&&r.monthdays.empty()&&r.weekdays.empty()&&p.m!=a.m)return false;
    return true;
}
static std::vector<Value> recurrence(const Event& e,Stamp rangeStart,Stamp rangeEnd,size_t& work) {
    std::vector<Value> out{e.start};if(e.rule.empty()){out.insert(out.end(),e.include.begin(),e.include.end());return out;}
    auto r=rule(e.rule);auto p=parts(e.start.wall);Stamp first=civil(p.y,p.m,p.d),clock=e.start.wall-first;
    // Keep one recurrence period at a time, not the entire historical series.
    // Expand complete periods so BYSETPOS is evaluated before trimming to the range.
    Stamp lower=rangeStart-std::max<Stamp>(0,e.end.wall-e.start.wall)-3*daySeconds;
    auto periodStart=[&](Stamp d){
        auto q=parts(d);
        if(r.freq=="YEARLY")return civil(q.y,1,1);
        if(r.freq=="MONTHLY")return civil(q.y,q.m,1);
        Stamp date=civil(q.y,q.m,q.d);
        if(r.freq=="WEEKLY")return date-((q.weekday-r.wkst+7)%7)*daySeconds;
        return date;
    };
    Stamp from=r.count?first:std::max(first,periodStart(lower));
    Stamp limit=periodStart(rangeEnd+2*daySeconds);auto endParts=parts(limit);
    if(r.freq=="YEARLY")limit=civil(endParts.y+1,1,1)-daySeconds;
    else if(r.freq=="MONTHLY")limit=civil(endParts.y,endParts.m,unsigned(monthLength(endParts.y,endParts.m)));
    else if(r.freq=="WEEKLY")limit+=6*daySeconds;
    if(from>limit)return e.include;
    if((limit-from)/daySeconds>250000)throw std::runtime_error("Recurrence expansion exceeds 250000 days");
    out.clear();int count=0;bool done=false;Stamp bucketKey=periodStart(from);std::vector<Value> list;
    auto flush=[&]{
        for(size_t i=0;i<list.size();++i){
            if(!r.setpos.empty()&&!contains(r.setpos,int(i)+1)&&!contains(r.setpos,int(i)-int(list.size())))continue;
            auto v=list[i];
            if(v.wall<e.start.wall)continue;
            if(r.untilSet){auto until=r.until;if(until.zone.empty())until.zone=e.start.zone;if((until.date?v.wall:absolute(v))>(until.date?until.wall:absolute(until)))continue;}
            if(r.count&&count>=r.count){done=true;break;}
            ++count;if(v.wall>=lower)out.push_back(v);
        }
        list.clear();
    };
    for(Stamp d=from;d<=limit&&!done;d+=daySeconds){
        if(++work>5000000)throw std::runtime_error("Calendar recurrence work limit exceeded");
        auto key=periodStart(d);if(key!=bucketKey){flush();bucketKey=key;if(done)break;}
        if(matches(r,d,first))list.push_back({d+clock,e.start.zone,e.start.date});
    }
    if(!done)flush();
    // DTSTART is part of the recurrence set even if selectors are inconsistent.
    if(e.start.wall>=lower && std::none_of(out.begin(),out.end(),[&](const Value& v){return v.wall==e.start.wall;}))out.insert(out.begin(),e.start);
    out.insert(out.end(),e.include.begin(),e.include.end());return out;
}
CalendarResult expandCalendarRange(const std::string& input,Stamp rangeStart,Stamp rangeEnd) {
    if(input.size()>10*1024*1024)throw std::runtime_error("Calendar exceeds 10 MiB");
    std::string data=input;if(data.starts_with("\xef\xbb\xbf"))data.erase(0,3);
    // Validate without allocating a second copy of the whole calendar as UTF-16.
    if(!data.empty()&&!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,data.data(),int(data.size()),nullptr,0))throw std::runtime_error("Invalid UTF-8 in calendar");
    std::istringstream stream(data);std::string line;std::vector<std::string> lines;
    while(std::getline(stream,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();if(!line.empty()&&(line[0]==' '||line[0]=='\t')){if(lines.empty())throw std::runtime_error("Invalid folded line");lines.back()+=line.substr(1);}else lines.push_back(line);}
    if(lines.empty()||upper(lines.front())!="BEGIN:VCALENDAR")throw std::runtime_error("Response is not an iCalendar file");
    CalendarResult result;std::vector<Event> events;Event e;bool active=false,closed=false;int nested=0;
    for(auto& l:lines){if(l.empty())continue;auto p=property(l);
        if(p.key=="END"&&upper(p.value)=="VCALENDAR"){closed=true;continue;}
        if(p.key=="BEGIN"&&upper(p.value)=="VEVENT"){if(active)throw std::runtime_error("Nested VEVENT");active=true;nested=0;e={};continue;}
        if(!active)continue;
        if(p.key=="BEGIN"){++nested;continue;}
        if(p.key=="END"&&nested){--nested;continue;}
        if(p.key=="END"&&upper(p.value)=="VEVENT") {
            active=false;if(e.uid.empty()||(!e.hasStart&&e.status!="CANCELLED"))throw std::runtime_error("VEVENT missing UID/DTSTART");
            if(!e.hasEnd&&e.hasStart){e.end=e.start;e.end.wall+=e.duration>=0?e.duration:e.start.date?daySeconds:0;}
            if(events.size()>=50000)throw std::runtime_error("Calendar exceeds 50000 events");
            events.push_back(std::move(e));continue;
        }
        if(nested)continue;
        if(p.key=="UID")e.uid=p.value;
        else if(p.key=="SUMMARY")e.title=unescape(p.value);
        else if(p.key=="LOCATION")e.location=unescape(p.value);
        else if(p.key=="DESCRIPTION")e.description=unescape(p.value);
        else if(p.key=="STATUS")e.status=upper(p.value);
        else if(p.key=="DTSTART"){e.start=value(p);e.hasStart=true;}
        else if(p.key=="DTEND"){e.end=value(p);e.hasEnd=true;}
        else if(p.key=="DURATION")e.duration=durationValue(p.value);
        else if(p.key=="RRULE")e.rule=upper(p.value);
        else if(p.key=="SEQUENCE")e.sequence=number(p.value);
        else if(p.key=="LAST-MODIFIED"||p.key=="DTSTAMP")e.modified=value(p).wall;
        else if(p.key=="RECURRENCE-ID"){e.rid=value(p);e.hasRid=true;e.range=p.params.contains("RANGE");}
        else if(p.key=="EXDATE"||p.key=="RDATE")for(auto& x:split(p.value,',')){auto q=p;q.value=x;(p.key=="EXDATE"?e.exclude:e.include).push_back(value(q));}
        else if(p.key=="EXRULE") {e.rule="FREQ=UNSUPPORTED";}
    }
    if(active||!closed)throw std::runtime_error("Truncated iCalendar file");
    std::map<std::string,Event> masters;std::map<std::pair<std::string,Stamp>,Event> overrides;
    auto newer=[](const Event& a,const Event& b){return std::pair{a.sequence,a.modified}>=std::pair{b.sequence,b.modified};};
    for(auto& ev:events)try{
        if(ev.hasRid){auto key=std::pair{ev.uid,absolute(ev.rid)};auto it=overrides.find(key);if(it==overrides.end()||newer(ev,it->second))overrides[key]=ev;}
        else {auto it=masters.find(ev.uid);if(it==masters.end()||newer(ev,it->second))masters[ev.uid]=ev;}
    }catch(const std::exception& ex){result.warnings.push_back(ex.what());}
    auto emit=[&](const Event& ev,Value at) {
        if(ev.status=="CANCELLED"||!ev.hasStart)return;
        if(ev.start.date!=ev.end.date)throw std::runtime_error("DTSTART/DTEND type mismatch");
        Stamp duration=ev.end.wall-ev.start.wall;if(duration<0||duration>366*daySeconds)throw std::runtime_error("Event duration is invalid or exceeds 366 days");
        Value end=ev.end;end.wall+=at.wall-ev.start.wall;
        Stamp begin=at.date?at.wall:toLocal(absolute(at));Stamp finish=end.date?end.wall:toLocal(absolute(end));
        if(finish<begin)finish=begin; // Repeated fall-back wall time is rendered as an instantaneous event.
        if(begin<rangeEnd && (finish>rangeStart || (finish==begin&&begin>=rangeStart)))
            result.events.push_back({ev.uid,ev.title.empty()?"(Untitled)":ev.title,ev.location,ev.description,begin,finish,at.date,0,{}});
    };
    size_t work=0;
    for(auto& [uid,ev]:masters)try{
        if(ev.status=="CANCELLED")continue;
        auto instances=recurrence(ev,rangeStart,rangeEnd,work);std::set<Stamp> seen;
        for(auto at:instances){auto key=absolute(at);if(!seen.insert(key).second)continue;
            if(overrides.contains({uid,key}))continue;
            if(std::any_of(ev.exclude.begin(),ev.exclude.end(),[&](const Value& x){return absolute(x)==key;}))continue;
            emit(ev,at);
        }
    }catch(const std::exception& ex){result.warnings.push_back(ex.what());}
    for(auto& [key,ev]:overrides)try{(void)key;if(ev.range)throw std::runtime_error("RECURRENCE-ID RANGE is unsupported; series may be incomplete");emit(ev,ev.start);}catch(const std::exception& ex){result.warnings.push_back(ex.what());}
    std::sort(result.events.begin(),result.events.end(),[](const auto& a,const auto& b){return std::tie(a.start,a.end,a.uid)<std::tie(b.start,b.end,b.uid);});
    std::sort(result.warnings.begin(),result.warnings.end());result.warnings.erase(std::unique(result.warnings.begin(),result.warnings.end()),result.warnings.end());
    if(result.events.size()>50000)throw std::runtime_error("Range exceeds 50000 occurrences");
    return result;
}
CalendarResult expandCalendar(const std::string& input,Stamp week) {
    return expandCalendarRange(input,week,week+7*daySeconds);
}
std::vector<Segment> layoutEvents(const std::vector<Occurrence>& events,Stamp week) {
    std::vector<Segment> out;
    for(int day=0;day<7;++day){Stamp a=week+day*daySeconds,b=a+daySeconds;std::vector<Segment> items;
        for(size_t i=0;i<events.size();++i){auto& e=events[i];if(e.allDay)continue;if(e.start<b&&(e.end>a||(e.start==e.end&&e.start>=a)))items.push_back({i,day,0,1,std::max(a,e.start),std::min(b,std::max(e.end,e.start+60))});}
        std::sort(items.begin(),items.end(),[](auto& x,auto& y){return std::tie(x.start,x.end)<std::tie(y.start,y.end);});
        size_t first=out.size();Stamp clusterEnd=0;std::vector<Stamp> ends;
        auto flush=[&]{for(size_t i=first;i<out.size();++i)out[i].columns=int(ends.size());first=out.size();ends.clear();};
        for(auto s:items){if(!ends.empty()&&s.start>=clusterEnd)flush();if(ends.empty())clusterEnd=s.end;else clusterEnd=std::max(clusterEnd,s.end);
            auto free=std::find_if(ends.begin(),ends.end(),[&](Stamp end){return end<=s.start;});s.column=int(free-ends.begin());if(free==ends.end())ends.push_back(s.end);else *free=s.end;out.push_back(s);
        }flush();
    }return out;
}
}
