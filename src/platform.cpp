#include "platform.h"
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <shlobj.h>
#include <chrono>
#include <algorithm>
#include <bit>
#include <fstream>
#include <sstream>
#include <memory>
#include <cstring>

namespace deck {
namespace {
// Stable public SQLite C ABI, loaded from Windows' serviced system SQLite.
struct sqlite3; struct sqlite3_stmt;
struct Sqlite {
    HMODULE module{};
    int (__cdecl *open)(const char*,sqlite3**,int,const char*){};
    int (__cdecl *close)(sqlite3*){};
    int (__cdecl *exec)(sqlite3*,const char*,void*,void*,char**){};
    int (__cdecl *prepare)(sqlite3*,const char*,int,sqlite3_stmt**,const char**){};
    int (__cdecl *bindText)(sqlite3_stmt*,int,const char*,int,void(__cdecl*)(void*)){};
    int (__cdecl *bindBlob)(sqlite3_stmt*,int,const void*,int,void(__cdecl*)(void*)){};
    int (__cdecl *step)(sqlite3_stmt*){};
    const void* (__cdecl *blob)(sqlite3_stmt*,int){};
    int (__cdecl *bytes)(sqlite3_stmt*,int){};
    int (__cdecl *finalize)(sqlite3_stmt*){};
    Sqlite() {
        module=LoadLibraryExW(L"winsqlite3.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!module)throw std::runtime_error("Windows SQLite is unavailable (Windows 10 required)");
#define LOAD(member,name) member=std::bit_cast<decltype(member)>(GetProcAddress(module,name));if(!member)throw std::runtime_error("Windows SQLite API unavailable")
        LOAD(open,"sqlite3_open_v2");LOAD(close,"sqlite3_close");LOAD(exec,"sqlite3_exec");LOAD(prepare,"sqlite3_prepare_v2");
        LOAD(bindText,"sqlite3_bind_text");LOAD(bindBlob,"sqlite3_bind_blob");LOAD(step,"sqlite3_step");LOAD(blob,"sqlite3_column_blob");LOAD(bytes,"sqlite3_column_bytes");LOAD(finalize,"sqlite3_finalize");
#undef LOAD
    }
    ~Sqlite(){if(module)FreeLibrary(module);}
};
Sqlite& sql(){static Sqlite api;return api;}
struct Statement {
    sqlite3_stmt* p{};
    Statement(void* db,const char* query){if(sql().prepare(static_cast<sqlite3*>(db),query,-1,&p,nullptr))throw std::runtime_error("Cannot prepare cache query");}
    ~Statement(){if(p)sql().finalize(p);}
};
std::string protect(const std::string& value,bool encrypt) {
    DATA_BLOB in{DWORD(value.size()),reinterpret_cast<BYTE*>(const_cast<char*>(value.data()))},out{};
    BOOL ok=encrypt?CryptProtectData(&in,L"Calendar Deck",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out)
        :CryptUnprotectData(&in,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out);
    if(!ok)throw std::runtime_error("Cannot access encrypted data for this Windows account");
    std::string result(reinterpret_cast<char*>(out.pbData),out.cbData);SecureZeroMemory(out.pbData,out.cbData);LocalFree(out.pbData);return result;
}
void pack(std::string& out,const std::string& value){uint32_t n=uint32_t(value.size());out.append(reinterpret_cast<const char*>(&n),4);out+=value;}
std::string unpack(const std::string& s,size_t& at){uint32_t n{};if(at+4>s.size())throw std::runtime_error("Invalid cache record");std::memcpy(&n,s.data()+at,4);at+=4;if(n>s.size()-at)throw std::runtime_error("Invalid cache record");auto r=s.substr(at,n);at+=n;return r;}
struct Internet {
    HINTERNET h{};
    explicit Internet(HINTERNET v):h(v){if(!h)throw std::runtime_error("Cannot open HTTPS connection");}
    ~Internet(){if(h)WinHttpCloseHandle(h);}
    operator HINTERNET() const{return h;}
};
struct Url {std::wstring host,path;};
int hexDigit(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
std::optional<std::string> decodeQueryValue(const std::string& value){
    std::string out;
    for(size_t i=0;i<value.size();++i){
        if(value[i]!='%'){out+=value[i];continue;}
        if(i+2>=value.size())return {};
        int hi=hexDigit(value[i+1]),lo=hexDigit(value[i+2]);if(hi<0||lo<0)return {};
        out+=char(hi*16+lo);i+=2;
    }
    // Keep literal '+' intact: calendar IDs and base64 cid values can contain it.
    return out;
}
bool calendarId(const std::string& value){
    auto at=value.find('@');if(at==0||at==value.npos||at+1==value.size()||value.find('@',at+1)!=value.npos)return false;
    return value.size()<=1024&&std::all_of(value.begin(),value.end(),[](unsigned char c){
        return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='@'||c=='.'||c=='_'||c=='-'||c=='+'||c=='#';
    });
}
std::optional<std::string> decodeCalendarId(std::string value){
    if(value.empty()||value.size()>2048)return {};
    std::replace(value.begin(),value.end(),'-','+');std::replace(value.begin(),value.end(),'_','/');
    auto pad=value.find('=');
    if(pad!=value.npos&&(value.size()-pad>2||value.find_first_not_of('=',pad)!=value.npos))return {};
    auto data=pad==value.npos?value.size():pad;
    for(size_t i=0;i<data;++i){unsigned char c=value[i];if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='+'||c=='/'))return {};}
    if(value.size()%4==1||(pad!=value.npos&&value.size()%4!=0))return {};
    while(value.size()%4)value+='=';
    DWORD size=0;if(!CryptStringToBinaryA(value.c_str(),DWORD(value.size()),CRYPT_STRING_BASE64,nullptr,&size,nullptr,nullptr))return {};
    std::string id(size,0);if(!CryptStringToBinaryA(value.c_str(),DWORD(value.size()),CRYPT_STRING_BASE64,reinterpret_cast<BYTE*>(id.data()),&size,nullptr,nullptr))return {};
    id.resize(size);if(!calendarId(id))return {};return id;
}
bool sharePath(const std::wstring& path){
    if(path==L"/calendar"||path==L"/calendar/"||path==L"/calendar/embed"||path==L"/calendar/render")return true;
    const std::wstring prefix=L"/calendar/u/";if(!path.starts_with(prefix))return false;
    auto rest=path.substr(prefix.size());auto slash=rest.find('/');
    auto account=rest.substr(0,slash);
    if(account.empty()||!std::all_of(account.begin(),account.end(),[](wchar_t c){return c>=L'0'&&c<=L'9';}))return false;
    return slash==rest.npos||rest.substr(slash)==L"/"||rest.substr(slash)==L"/r"||rest.substr(slash)==L"/r/";
}
std::optional<Url> parseUrl(const std::string& raw) {
    try {
        if(raw.size()>4096||raw.find_first_of("\r\n\t #")!=raw.npos||std::any_of(raw.begin(),raw.end(),[](unsigned char c){return c<32||c==127;}))return {};
        auto w=wide(raw);URL_COMPONENTS c{};c.dwStructSize=sizeof(c);c.dwSchemeLength=c.dwHostNameLength=c.dwUrlPathLength=c.dwExtraInfoLength=c.dwUserNameLength=c.dwPasswordLength=DWORD(-1);
        if(!WinHttpCrackUrl(w.c_str(),DWORD(w.size()),0,&c)||c.nScheme!=INTERNET_SCHEME_HTTPS||c.nPort!=443||c.dwUserNameLength||c.dwPasswordLength||c.dwExtraInfoLength)return {};
        std::wstring host(c.lpszHostName,c.dwHostNameLength),path(c.lpszUrlPath,c.dwUrlPathLength);
        if(_wcsicmp(host.c_str(),L"calendar.google.com")||!path.starts_with(L"/calendar/ical/")||!path.ends_with(L".ics"))return {};
        return Url{host,path};
    }catch(...){return {};}
}
std::string header(HINTERNET request,DWORD what) {
    DWORD size=0;WinHttpQueryHeaders(request,what,WINHTTP_HEADER_NAME_BY_INDEX,nullptr,&size,WINHTTP_NO_HEADER_INDEX);
    if(!size||size>8192)return {};
    std::wstring text(size/sizeof(wchar_t),0);
    if(!WinHttpQueryHeaders(request,what,WINHTTP_HEADER_NAME_BY_INDEX,text.data(),&size,WINHTTP_NO_HEADER_INDEX))return {};
    text.resize(size/sizeof(wchar_t));while(!text.empty()&&!text.back())text.pop_back();return utf8(text);
}
}
Store::Store(const std::filesystem::path& file) {
    auto parent=file.parent_path();if(!parent.empty())std::filesystem::create_directories(parent);
    sqlite3* db=nullptr;auto path=file.u8string();
    if(sql().open(reinterpret_cast<const char*>(path.c_str()),&db,2|4|0x10000,nullptr)){if(db)sql().close(db);throw std::runtime_error("Cannot open SQLite cache");}
    db_=db;
    if(sql().exec(db,"PRAGMA busy_timeout=2500; PRAGMA cache_size=-256; PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL; PRAGMA secure_delete=ON; CREATE TABLE IF NOT EXISTS kv(k TEXT PRIMARY KEY, v BLOB NOT NULL);",nullptr,nullptr,nullptr)){
        sql().close(db);db_=nullptr;throw std::runtime_error("Cannot initialize SQLite cache");
    }
}
Store::~Store(){if(db_)sql().close(static_cast<sqlite3*>(db_));}
std::string Store::read(const char* key) {
    Statement s(db_,"SELECT v FROM kv WHERE k=?");if(sql().bindText(s.p,1,key,-1,nullptr))throw std::runtime_error("Cannot read cache key");
    int rc=sql().step(s.p);if(rc==101)return {};if(rc!=100)throw std::runtime_error("Cannot read SQLite cache");
    int n=sql().bytes(s.p,0);auto p=static_cast<const char*>(sql().blob(s.p,0));if(!p||n<=0)return {};return protect(std::string(p,n),false);
}
void Store::write(const char* key,const std::string& value) {
    auto encrypted=protect(value,true);Statement s(db_,"INSERT OR REPLACE INTO kv(k,v) VALUES(?,?)");
    if(sql().bindText(s.p,1,key,-1,nullptr)||sql().bindBlob(s.p,2,encrypted.data(),int(encrypted.size()),nullptr)||sql().step(s.p)!=101)throw std::runtime_error("Cannot save SQLite cache");
}
void Store::transaction(const std::function<void()>& operation){
    auto db=static_cast<sqlite3*>(db_);
    if(sql().exec(db,"BEGIN IMMEDIATE",nullptr,nullptr,nullptr))throw std::runtime_error("Cannot start calendar import");
    try{operation();if(sql().exec(db,"COMMIT",nullptr,nullptr,nullptr))throw std::runtime_error("Cannot commit calendar import");}
    catch(...){sql().exec(db,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
}
Settings loadSettings(Store& store){Settings s;auto raw=store.read("settings");if(raw.empty())return s;size_t at=0;s.url=unpack(raw,at);std::istringstream in(unpack(raw,at));int desktop=0;
    if(!(in>>s.intervalMinutes>>desktop>>s.x>>s.y>>s.width>>s.height))throw std::runtime_error("Invalid settings record");
    s.desktop=desktop!=0;s.intervalMinutes=std::clamp(s.intervalMinutes,5,1440);s.width=std::clamp(s.width,640,7680);s.height=std::clamp(s.height,480,4320);
    if(at<raw.size()){
        std::istringstream flags(unpack(raw,at));size_t count=0;if(!(flags>>s.closeToTray>>s.locked>>count))throw std::runtime_error("Invalid settings flags");
        if(count>100)throw std::runtime_error("Too many calendars");
        for(size_t i=0;i<count;++i){CalendarSource c;c.name=unpack(raw,at);c.url=unpack(raw,at);std::istringstream values(unpack(raw,at));if(!(values>>c.color>>c.visible))throw std::runtime_error("Invalid calendar settings");c.color=std::clamp(c.color,0,4);s.calendars.push_back(c);}
    }else if(!s.url.empty())s.calendars.push_back({"My Calendar",s.url,0,true});
    return s;}
void saveSettings(Store& store,const Settings& s){std::string raw;pack(raw,s.url);std::ostringstream out;out<<s.intervalMinutes<<' '<<int(s.desktop)<<' '<<s.x<<' '<<s.y<<' '<<s.width<<' '<<s.height;pack(raw,out.str());std::ostringstream flags;flags<<s.closeToTray<<' '<<s.locked<<' '<<s.calendars.size();pack(raw,flags.str());
    for(auto& c:s.calendars){pack(raw,c.name);pack(raw,c.url);pack(raw,std::to_string(c.color)+" "+std::to_string(c.visible));}store.write("settings",raw);}
std::string feedKey(const std::string& url){
    // Stable key; private URLs themselves never become SQLite keys.
    HCRYPTPROV provider{};HCRYPTHASH hash{};BYTE bytes[32]{};DWORD size=32;
    if(!CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT))throw std::runtime_error("Cannot initialize cache key");
    bool ok=CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)&&CryptHashData(hash,reinterpret_cast<const BYTE*>(url.data()),DWORD(url.size()),0)&&CryptGetHashParam(hash,HP_HASHVAL,bytes,&size,0);
    if(hash){CryptDestroyHash(hash);}
    CryptReleaseContext(provider,0);if(!ok)throw std::runtime_error("Cannot hash cache key");
    std::string key="feed-";const char* hex="0123456789abcdef";for(auto b:bytes){key+=hex[b>>4];key+=hex[b&15];}return key;
}
Feed loadFeed(Store& store,const std::string& url){Feed f;auto raw=store.read(feedKey(url).c_str());if(raw.empty())raw=store.read("feed");if(raw.empty())return f;size_t at=0;f.url=unpack(raw,at);if(f.url!=url)return {};f.body=unpack(raw,at);f.etag=unpack(raw,at);f.modified=unpack(raw,at);auto checked=unpack(raw,at);try{f.checked=std::stoll(checked);}catch(...){f.checked=0;}return f;}
void saveFeed(Store& store,const Feed& f){std::string raw;pack(raw,f.url);pack(raw,f.body);pack(raw,f.etag);pack(raw,f.modified);pack(raw,std::to_string(f.checked));store.write(feedKey(f.url).c_str(),raw);}
bool offlineCalendar(const CalendarSource& source){return source.url.starts_with("local:");}
Settings importCalendarFile(Store& store,const Settings& settings,const std::filesystem::path& file,Stamp week,int replaceIndex){
    auto extension=file.extension().wstring();
    if(_wcsicmp(extension.c_str(),L".ics"))throw std::runtime_error("Choose an .ics calendar file. Extract Google Calendar ZIP exports first.");
    if(replaceIndex<-1||replaceIndex>=int(settings.calendars.size())||(replaceIndex>=0&&!offlineCalendar(settings.calendars[replaceIndex])))throw std::runtime_error("Select an imported calendar to replace");
    if(replaceIndex<0&&settings.calendars.size()>=100)throw std::runtime_error("Maximum 100 calendars");
    std::ifstream input(file,std::ios::binary);if(!input)throw std::runtime_error("Cannot open the selected calendar file");
    constexpr size_t limit=10*1024*1024;
    std::string body;char buffer[16384];
    while(input){input.read(buffer,sizeof(buffer));auto got=input.gcount();if(body.size()+size_t(got)>limit)throw std::runtime_error("Calendar file exceeds 10 MiB limit");body.append(buffer,size_t(got));}
    if(!input.eof())throw std::runtime_error("Cannot read the selected calendar file");
    if(body.starts_with("\xEF\xBB\xBF"))body.erase(0,3);
    if(body.empty())throw std::runtime_error("The selected calendar file is empty");
    expandCalendar(body,week); // Validate before writing either feed or settings.
    Settings next=settings;CalendarSource source;
    if(replaceIndex>=0)source=next.calendars[replaceIndex];
    else{
        GUID guid{};wchar_t id[40]{};
        if(FAILED(CoCreateGuid(&guid))||!StringFromGUID2(guid,id,40))throw std::runtime_error("Cannot create an offline calendar ID");
        source.url="local:"+utf8(id);source.name=utf8(file.stem().wstring());source.color=int(next.calendars.size()%5);
        if(source.name.empty())source.name="Imported calendar";
        next.calendars.push_back(source);
    }
    next.url.clear();
    Feed feed{source.url,std::move(body),{}, {},std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()};
    store.transaction([&]{saveFeed(store,feed);saveSettings(store,next);});
    return next;
}
std::string loadSyncStatus(Store& store,const std::string& url){return store.read(("status-"+feedKey(url)).c_str());}
void saveSyncStatus(Store& store,const std::string& url,const std::string& status){store.write(("status-"+feedKey(url)).c_str(),status);}
bool validFeedUrl(const std::string& url){return parseUrl(url).has_value();}
std::optional<std::string> normalizeFeedUrl(const std::string& input){
    auto first=input.find_first_not_of(" \r\n\t");if(first==input.npos)return {};
    auto raw=input.substr(first,input.find_last_not_of(" \r\n\t")-first+1);
    if(validFeedUrl(raw))return raw; // Preserve private tokens and existing cache identity.
    if(raw.size()>4096||raw.find_first_of("\r\n\t #")!=raw.npos||raw.find('\0')!=raw.npos)return {};
    try{
        auto w=wide(raw);URL_COMPONENTS c{};c.dwStructSize=sizeof(c);
        c.dwSchemeLength=c.dwHostNameLength=c.dwUrlPathLength=c.dwExtraInfoLength=c.dwUserNameLength=c.dwPasswordLength=DWORD(-1);
        if(!WinHttpCrackUrl(w.c_str(),DWORD(w.size()),0,&c)||c.nScheme!=INTERNET_SCHEME_HTTPS||c.nPort!=443||c.dwUserNameLength||c.dwPasswordLength)return {};
        std::wstring host(c.lpszHostName,c.dwHostNameLength),path(c.lpszUrlPath,c.dwUrlPathLength);
        if(_wcsicmp(host.c_str(),L"calendar.google.com")||!sharePath(path)||!c.dwExtraInfoLength)return {};
        auto query=utf8(std::wstring(c.lpszExtraInfo,c.dwExtraInfoLength));if(query.front()!='?')return {};
        std::optional<std::string> id;
        for(size_t start=1;start<=query.size();){
            auto end=query.find('&',start);if(end==query.npos)end=query.size();auto part=query.substr(start,end-start);auto equal=part.find('=');
            auto key=decodeQueryValue(part.substr(0,equal));if(!key)return {};
            if(*key=="src"||*key=="cid"){
                if(id||equal==part.npos)return {}; // Multiple calendar IDs require separate sources.
                auto value=decodeQueryValue(part.substr(equal+1));if(!value)return {};
                id=*key=="cid"?decodeCalendarId(*value):value;if(!id||!calendarId(*id))return {};
            }
            if(end==query.size())break;
            start=end+1;
        }
        if(!id)return {};
        std::string encoded;const char* hex="0123456789ABCDEF";
        for(unsigned char ch:*id){if((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='.'||ch=='_'||ch=='-')encoded+=char(ch);else{encoded+='%';encoded+=hex[ch>>4];encoded+=hex[ch&15];}}
        auto feed="https://calendar.google.com/calendar/ical/"+encoded+"/public/basic.ics";
        if(!validFeedUrl(feed))return {};
        return feed;
    }catch(...){return {};}
}
Feed downloadFeed(const std::string& url,const Feed& previous,const std::atomic_bool& stop) {
    auto target=parseUrl(url);if(!target)throw std::runtime_error("Enter a Google Calendar HTTPS iCal URL");
    Internet session(WinHttpOpen(L"CalendarDeck/0.2",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    WinHttpSetTimeouts(session,10000,10000,10000,10000);
    Internet connection(WinHttpConnect(session,target->host.c_str(),443,0));
    Internet request(WinHttpOpenRequest(connection,L"GET",target->path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
    DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER,autologon=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    if(!WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect)) || !WinHttpSetOption(request,WINHTTP_OPTION_AUTOLOGON_POLICY,&autologon,sizeof(autologon)))throw std::runtime_error("Cannot configure secure HTTPS request");
    std::wstring headers=L"Accept: text/calendar\r\n";
    auto add=[&](const wchar_t* name,const std::string& v){if(!v.empty()&&v.size()<4096&&v.find_first_of("\r\n")==v.npos)headers+=std::wstring(name)+wide(v)+L"\r\n";};
    if(previous.url==url&&!previous.body.empty()){add(L"If-None-Match: ",previous.etag);add(L"If-Modified-Since: ",previous.modified);}
    if(stop)throw std::runtime_error("Stopped");
    if(!WinHttpSendRequest(request,headers.c_str(),DWORD(headers.size()),WINHTTP_NO_REQUEST_DATA,0,0,0)||!WinHttpReceiveResponse(request,nullptr))throw std::runtime_error("Google Calendar is unreachable (WinHTTP "+std::to_string(GetLastError())+"); cached calendar retained");
    DWORD status=0,size=sizeof(status);
    if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&size,nullptr))throw std::runtime_error("Invalid HTTPS response");
    Feed result=previous;
    if(status==304 && previous.url==url&&!previous.body.empty()){}
    else if(status==200){
        result={};result.url=url;result.etag=header(request,WINHTTP_QUERY_ETAG);result.modified=header(request,WINHTTP_QUERY_LAST_MODIFIED);
        char buffer[16384];DWORD got=0;auto started=std::chrono::steady_clock::now();
        do{if(stop)throw std::runtime_error("Stopped");if(std::chrono::steady_clock::now()-started>std::chrono::seconds(60))throw std::runtime_error("Calendar download timed out");
            if(!WinHttpReadData(request,buffer,sizeof(buffer),&got))throw std::runtime_error("Calendar download failed; cached calendar retained");
            if(result.body.size()+got>10*1024*1024)throw std::runtime_error("Calendar exceeds 10 MiB limit");
            result.body.append(buffer,got);
        }while(got);
    } else if(status==401||status==403||status==404){
        if(target->path.ends_with(L"/public/basic.ics"))throw std::runtime_error("Public calendar feed is unavailable. Check the link and whether the owner or organization allows public iCal access; a browser-only shared calendar may require sign-in.");
        throw std::runtime_error("Calendar feed was rejected; check the iCal URL and its access permissions");
    }
    else throw std::runtime_error("Google Calendar returned HTTP "+std::to_string(status)+"; cached calendar retained");
    result.checked=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();return result;
}
std::filesystem::path dataDirectory(bool portable){wchar_t path[32768]{};
    if(portable){auto n=GetModuleFileNameW(nullptr,path,32768);if(!n||n>=32768)throw std::runtime_error("Cannot locate executable");return std::filesystem::path(path).parent_path()/L"data";}
    PWSTR local=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local)))throw std::runtime_error("Cannot locate application data");
    std::filesystem::path result=std::filesystem::path(local)/L"CalendarDeck";CoTaskMemFree(local);return result;
}
std::string demoCalendar(Stamp week){auto date=[](Stamp t){auto p=parts(t);char b[32];snprintf(b,sizeof(b),"%04d%02u%02uT%02d%02d%02d",p.y,p.m,p.d,p.hour,p.minute,p.second);return std::string(b);};
    auto base=monday(week);
    std::string s="BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Calendar Deck//Demo//EN\r\n";
    auto event=[&](const char* id,const char* title,int day,int hour,int minute,int length,const char* rule=""){
        Stamp t=base+day*daySeconds+hour*3600+minute*60;
        s+="BEGIN:VEVENT\r\nUID:"+std::string(id)+"\r\nDTSTART:"+date(t)+"\r\nDTEND:"+date(t+length*60)+"\r\nSUMMARY:"+title+"\r\n";
        if(rule&&*rule)s+="RRULE:"+std::string(rule)+"\r\n";
        s+="END:VEVENT\r\n";
    };
    event("demo-focus","Deep work",0,9,0,120,"FREQ=WEEKLY;INTERVAL=1");
    event("demo-review","Project review",1,14,0,90,"FREQ=WEEKLY;INTERVAL=1");
    event("demo-build","Build something",2,10,0,120,"FREQ=WEEKLY;INTERVAL=1");
    event("demo-team","Team catch-up",2,10,30,45,"FREQ=WEEKLY;INTERVAL=1");
    event("demo-reading","Reading & Research",3,20,0,60,"FREQ=WEEKLY;INTERVAL=1");
    event("demo-wrap","Weekly review",4,16,0,60,"FREQ=WEEKLY;INTERVAL=1");
    event("demo-outside","Outdoor activities",5,8,0,120,"FREQ=WEEKLY;INTERVAL=1");
    auto p=parts(base);char b[20];snprintf(b,sizeof(b),"%04d%02u%02u",p.y,p.m,p.d);
    s+="BEGIN:VEVENT\r\nUID:demo-all\r\nDTSTART;VALUE=DATE:"+std::string(b)+"\r\nSUMMARY:Weekly Goal Kickoff\r\nRRULE:FREQ=WEEKLY;INTERVAL=1\r\nEND:VEVENT\r\n";
    s+="END:VCALENDAR\r\n";
    return s;
}
}
