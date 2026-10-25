#include "calendar.h"
#include "platform.h"
#include <windows.h>
#include <iostream>
#include <fstream>
#include <stdexcept>

using namespace deck;
int checks=0;
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
std::string wrap(const std::string& events){return "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"+events+"END:VCALENDAR\r\n";}
std::string event(const std::string& props){return "BEGIN:VEVENT\r\n"+props+"END:VEVENT\r\n";}
void tests(){
    Stamp week=civil(2026,10,19);
    check(monday(civil(2026,10,25))==week,"Monday week boundary");
    check(parts(civil(2024,2,29)).d==29,"Leap day");
    check(monthLength(2024,2)==29,"Leap year February length");
    check(monthLength(2026,2)==28,"Non-leap year February length");
    check(monthLength(2026,10)==31,"October month length");
    check(monthStart(civil(2026,10,25))==civil(2026,10,1),"Month start calculation");
    check(yearStart(civil(2026,10,25))==civil(2026,1,1),"Year start calculation");
    auto repeated=event("UID:a\r\nDTSTART:20261019T090000\r\nDTEND:20261019T100000\r\nRRULE:FREQ=WEEKLY;BYDAY=MO,WE,FR;COUNT=6\r\nEXDATE:20261021T090000\r\nSUMMARY:Tiếng Việt\\, title\r\n folded\r\n");
    auto r=expandCalendar(wrap(repeated),week);
    check(r.events.size()==2,"Weekly recurrence and EXDATE");check(r.events.front().title=="Tiếng Việt, titlefolded","UTF-8 text and folding");
    check(expandCalendarRange(wrap(repeated),civil(2026,10,1),civil(2026,11,1)).events.size()==5,"expandCalendarRange expands monthly occurrences");
    check(expandCalendar(wrap(repeated),week+7*daySeconds).events.size()==3,"COUNT spans weeks");
    check(expandCalendar(wrap(repeated),week+14*daySeconds).events.empty(),"COUNT termination");
    auto canceled=event("UID:a\r\nRECURRENCE-ID:20261023T090000\r\nSTATUS:CANCELLED\r\n");
    check(expandCalendar(wrap(repeated+canceled),week).events.size()==1,"Cancelled exception without DTSTART");
    auto moved=event("UID:a\r\nRECURRENCE-ID:20261021T090000\r\nDTSTART:20261022T150000\r\nDTEND:20261022T160000\r\nSUMMARY:Moved\r\n");
    check(expandCalendar(wrap(repeated+moved),week).events.size()==3,"Moved exception re-included after EXDATE");
    auto all=event("UID:all\r\nDTSTART;VALUE=DATE:20261018\r\nDTEND;VALUE=DATE:20261020\r\nSUMMARY:Trip\r\n");
    auto ar=expandCalendar(wrap(all),week);check(ar.events.size()==1&&ar.events[0].allDay&&ar.events[0].end==civil(2026,10,20),"All-day exclusive end spanning week");
    auto daily=event("UID:daily\r\nDTSTART:20261019T090000Z\r\nDURATION:PT1H\r\nRRULE:FREQ=DAILY;UNTIL=20261021T090000Z\r\n");
    check(expandCalendar(wrap(daily),week).events.size()==3,"UNTIL inclusive");
    auto monthly=event("UID:monthly\r\nDTSTART:20260130T090000\r\nDURATION:PT1H\r\nRRULE:FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=-1\r\n");
    auto mr=expandCalendar(wrap(monthly),civil(2026,10,26));check(mr.events.size()==1&&parts(mr.events[0].start).d==30,"Last weekday monthly");
    auto fifthLast=event("UID:fifth-last\r\nDTSTART:20260126T090000\r\nDURATION:PT1H\r\nRRULE:FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=-5\r\n");
    check(expandCalendar(wrap(fifthLast),civil(2026,10,12)).events.empty(),"BYSETPOS sees complete month before week trim");
    auto oldDaily=event("UID:old-daily\r\nDTSTART:20000101T090000\r\nDURATION:PT1H\r\nRRULE:FREQ=DAILY\r\n");
    check(expandCalendar(wrap(oldDaily),week).events.size()==7,"Decades-old unbounded daily series");
    auto dailyTimes=expandCalendar(wrap(oldDaily),week);
    check(dailyTimes.events.front().start==week+9*3600&&dailyTimes.events.front().end==week+10*3600&&dailyTimes.events.back().start==week+6*daySeconds+9*3600,"Unbounded daily series preserves exact dates and hours");
    auto school=event("UID:school-monday\r\nDTSTART;TZID=Asia/Ho_Chi_Minh:20260824T150000\r\nDTEND;TZID=Asia/Ho_Chi_Minh:20260824T175000\r\nRRULE:FREQ=WEEKLY;UNTIL=20261207T080000Z\r\nEXDATE;TZID=Asia/Ho_Chi_Minh:20260831T150000,20261012T150000\r\nSUMMARY:Afternoon class\r\n");
    school+=event("UID:school-wednesday\r\nDTSTART;TZID=Asia/Ho_Chi_Minh:20260826T070000\r\nDTEND;TZID=Asia/Ho_Chi_Minh:20260826T095000\r\nRRULE:FREQ=WEEKLY;UNTIL=20261209T000000Z\r\nEXDATE;TZID=Asia/Ho_Chi_Minh:20260902T070000,20261014T070000\r\nSUMMARY:Morning class\r\n");
    auto schoolWeek=expandCalendar(wrap(school),week);
    check(schoolWeek.events.size()==2&&schoolWeek.events[0].start==toLocal(civil(2026,10,19,8))&&schoolWeek.events[0].end==toLocal(civil(2026,10,19,10,50)),"Google-style weekly UNTIL preserves Monday 15:00-17:50 Vietnam time");
    check(schoolWeek.events[1].start==toLocal(civil(2026,10,21,0))&&schoolWeek.events[1].end==toLocal(civil(2026,10,21,2,50)),"Google-style weekly UNTIL preserves Wednesday 07:00-09:50 Vietnam time");
    check(expandCalendar(wrap(school),week-7*daySeconds).events.empty(),"Weekly EXDATE remains correct when search bound is rounded");
    auto shortDaily=event("UID:daily-minutes\r\nDTSTART:20260901T000500\r\nDTEND:20260901T001500\r\nRRULE:FREQ=DAILY;UNTIL=20261030T000000\r\n");
    auto minutesWeek=expandCalendar(wrap(shortDaily),week);
    check(minutesWeek.events.size()==7&&minutesWeek.events[0].start==week+5*60&&minutesWeek.events[0].end==week+15*60,"Daily recurrence near midnight retains minutes without shifting a day");
    auto sunday=event("UID:sunday\r\nDTSTART:20260823T233000\r\nDTEND:20260824T013000\r\nRRULE:FREQ=WEEKLY;WKST=SU\r\n");
    auto sundayWeek=expandCalendar(wrap(sunday),week);
    check(sundayWeek.events.size()==2&&sundayWeek.events.front().start==week-daySeconds+23*3600+30*60&&sundayWeek.events.back().start==week+6*daySeconds+23*3600+30*60,"Sunday WKST and overnight weekly events retain both week-edge occurrences");
    auto oldCount=event("UID:old-count\r\nDTSTART:20000101T090000\r\nRRULE:FREQ=DAILY;COUNT=3\r\n");
    check(expandCalendar(wrap(oldCount),week).events.empty(),"Old COUNT series terminates without phantom DTSTART");
    auto ordinal=event("UID:ordinal\r\nDTSTART:20260105T090000\r\nRRULE:FREQ=MONTHLY;BYDAY=+1MO\r\n");
    check(expandCalendar(wrap(ordinal),civil(2026,10,5)).events.size()==1,"Explicit plus ordinal BYDAY");
    auto leap=event("UID:leap\r\nDTSTART:20240229T090000\r\nRRULE:FREQ=YEARLY\r\n");
    check(expandCalendar(wrap(leap),civil(2026,2,23)).events.empty(),"Skip invalid yearly leap-day recurrence");
    check(toUtc(civil(2026,7,1,12),"America/New_York")==civil(2026,7,1,16),"IANA daylight time");
    check(toUtc(civil(2026,1,1,12),"America/New_York")==civil(2026,1,1,17),"IANA winter time");
    check(toUtc(civil(2026,10,19,9),"Asia/Ho_Chi_Minh")==civil(2026,10,19,2),"Vietnam time zone");
    auto dst=event("UID:dst\r\nDTSTART;TZID=America/New_York:20261026T090000\r\nDTEND;TZID=America/New_York:20261026T100000\r\nRRULE:FREQ=WEEKLY;COUNT=2\r\n");
    auto before=expandCalendar(wrap(dst),civil(2026,10,26));auto after=expandCalendar(wrap(dst),civil(2026,11,2));
    check(before.events.size()==1&&before.events[0].start==toLocal(civil(2026,10,26,13)),"Recurring event before DST transition");
    check(after.events.size()==1&&after.events[0].start==toLocal(civil(2026,11,2,14)),"Recurring event stays at source wall time after DST");
    auto zone=event("UID:zone\r\nDTSTART;TZID=No/Such_Zone:20261019T090000\r\nRRULE:FREQ=DAILY\r\n");
    auto zr=expandCalendar(wrap(zone),week);check(zr.events.empty()&&!zr.warnings.empty(),"Unknown zone warns rather than invents time");
    auto unsupported=event("UID:bad-rule\r\nDTSTART:20261019T090000\r\nRRULE:FREQ=HOURLY\r\n");
    check(!expandCalendar(wrap(unsupported),week).warnings.empty(),"Unsupported rule warning");
    auto zeroCount=event("UID:zero-count\r\nDTSTART:20261019T090000\r\nRRULE:FREQ=DAILY;COUNT=0\r\n");
    check(expandCalendar(wrap(zeroCount),week).events.empty()&&!expandCalendar(wrap(zeroCount),week).warnings.empty(),"Invalid COUNT cannot turn into unlimited recurrence");
    auto rdates=event("UID:rdates\r\nDTSTART:20261019T090000\r\nRDATE:20261020T090000,20261022T090000\r\n");
    check(expandCalendar(wrap(rdates),week).events.size()==3,"Explicit RDATE instances");
    auto changed=event("UID:duplicate\r\nDTSTART:20261019T090000\r\nSUMMARY:Old\r\nSEQUENCE:1\r\n")+event("UID:duplicate\r\nDTSTART:20261019T090000\r\nSUMMARY:New\r\nSEQUENCE:2\r\n");
    auto dupe=expandCalendar(wrap(changed),week);check(dupe.events.size()==1&&dupe.events[0].title=="New","Newer SEQUENCE wins");
    bool throws=false;try{expandCalendar("<html>not a calendar</html>",week);}catch(...){throws=true;}check(throws,"Reject HTML response");
    throws=false;try{expandCalendar("BEGIN:VCALENDAR\r\n",week);}catch(...){throws=true;}check(throws,"Reject truncated response");
    std::vector<Occurrence> overlaps={{"a","A","","",week+9*3600,week+11*3600,false},{"b","B","","",week+10*3600,week+12*3600,false},{"c","C","","",week+12*3600,week+13*3600,false},{"d","D","","",week+daySeconds-1800,week+daySeconds+1800,false}};
    auto segments=layoutEvents(overlaps,week);check(segments.size()==5,"Split overnight event");check(segments[0].columns==2&&segments[1].columns==2&&segments[2].columns==1,"Overlap columns and adjacent endpoints");
    check(validFeedUrl("https://calendar.google.com/calendar/ical/id/private-secret/basic.ics"),"Valid private Google URL");
    const std::string publicFeed="https://calendar.google.com/calendar/ical/test%40example.com/public/basic.ics";
    const std::string secretFeed="https://calendar.google.com/calendar/ical/id/private-secret/basic.ics";
    check(normalizeFeedUrl(publicFeed)==publicFeed,"Accept public iCal URL unchanged");
    check(normalizeFeedUrl(secretFeed)==secretFeed,"Preserve secret token and cache identity");
    check(normalizeFeedUrl(" \t"+publicFeed+"\r\n")==publicFeed,"Trim pasted URL whitespace");
    for(const auto& link:std::vector<std::string>{
        "https://calendar.google.com/calendar/embed?src=test%40example.com&ctz=Asia%2FHo_Chi_Minh",
        "https://calendar.google.com/calendar/embed?src=test@example.com",
        "https://CALENDAR.GOOGLE.COM/calendar/embed?src=test%40example.com",
        "https://calendar.google.com/calendar?cid=dGVzdEBleGFtcGxlLmNvbQ==",
        "https://calendar.google.com/calendar/u/0?cid=dGVzdEBleGFtcGxlLmNvbQ",
        "https://calendar.google.com/calendar/u/1/r?cid=dGVzdEBleGFtcGxlLmNvbQ%3D%3D",
        "https://calendar.google.com/calendar/render?cid=dGVzdEBleGFtcGxlLmNvbQ=="}){
        check(normalizeFeedUrl(link)==publicFeed,"Convert Google public view/share URL to expected iCal feed");
    }
    const std::string aliasFeed="https://calendar.google.com/calendar/ical/work%2Bteam%40example.com/public/basic.ics";
    check(normalizeFeedUrl("https://calendar.google.com/calendar/embed?src=work+team%40example.com")==aliasFeed,"Preserve plus in calendar ID");
    check(normalizeFeedUrl("https://calendar.google.com/calendar/embed?src=work%2Bteam%40example.com")==aliasFeed,"Decode encoded plus exactly once");
    check(normalizeFeedUrl("https://calendar.google.com/calendar/embed?src=en.usa%23holiday%40group.v.calendar.google.com")=="https://calendar.google.com/calendar/ical/en.usa%23holiday%40group.v.calendar.google.com/public/basic.ics","Encode public holiday calendar ID safely");
    for(const auto& link:std::vector<std::string>{
        "http://calendar.google.com/calendar/embed?src=test%40example.com",
        "https://calendar.google.com.evil.test/calendar/embed?src=test%40example.com",
        "https://user:password@calendar.google.com/calendar/embed?src=test%40example.com",
        "https://calendar.google.com:444/calendar/embed?src=test%40example.com",
        "https://calendar.google.com/calendar/",
        "https://calendar.google.com/calendar/u/0/r",
        "https://calendar.google.com/calendar/event?src=test%40example.com",
        "https://calendar.google.com/calendar/embed?src=test%40example.com&src=other%40example.com",
        "https://calendar.google.com/calendar/embed?src=test%40example.com&cid=dGVzdEBleGFtcGxlLmNvbQ",
        "https://calendar.google.com/calendar/embed?src=test%40example.com&%73rc=other%40example.com",
        "https://calendar.google.com/calendar/embed?cid=not-base64!",
        "https://calendar.google.com/calendar/embed?cid=dGVzdEBleGFtcGxlLmNvbQ=",
        "https://calendar.google.com/calendar/embed?src=test%40example.com%2F..%2Fother",
        "https://calendar.google.com/calendar/embed?src=test%40example.com%0D%0AHeader",
        "https://calendar.google.com/calendar/embed?src=test%40example.com%00",
        "https://calendar.google.com/calendar/embed?src=test%40example.com%xx",
        "https://calendar.google.com/calendar/embed?src=test%40example.com#fragment",
        "https://calendar.google.com/calendar/embed?src=primary"}){
        check(!normalizeFeedUrl(link),"Reject unsafe, ambiguous or unsupported calendar links");
    }
    check(!normalizeFeedUrl(publicFeed+std::string(1,'\0')+".ics"),"Reject embedded NUL in direct feed URL");
    check(!validFeedUrl("http://calendar.google.com/calendar/ical/a/basic.ics"),"Reject HTTP");
    check(!validFeedUrl("https://calendar.google.com.evil.test/calendar/ical/a/basic.ics"),"Reject deceptive host");
    check(!validFeedUrl("https://user:pass@calendar.google.com/calendar/ical/a/basic.ics"),"Reject URL credentials");
    check(!validFeedUrl("https://calendar.google.com/calendar/ical/a/basic.ics\r\nInjected: header"),"Reject header injection");
    wchar_t tmp[MAX_PATH]{};GetTempPathW(MAX_PATH,tmp);auto path=std::filesystem::path(tmp)/(L"deck-tests-"+std::to_wstring(GetCurrentProcessId())+L".db");
    std::filesystem::remove(path);
    {Store store(path);Settings s;s.url="https://calendar.google.com/calendar/ical/test/private-secret/basic.ics";saveSettings(store,s);auto read=loadSettings(store);check(read.url==s.url&&read.intervalMinutes==15,"Encrypted settings roundtrip");
        Feed f{s.url,wrap(all),"etag","last-modified",123};saveFeed(store,f);auto cached=loadFeed(store,s.url);check(cached.body==f.body&&cached.etag=="etag"&&cached.checked==123,"Offline feed roundtrip");check(loadFeed(store,"different-url").body.empty(),"Cache isolated by URL");}
    {Store store(path);
        auto packLegacy=[](std::string& out,const std::string& value){uint32_t size=uint32_t(value.size());out.append(reinterpret_cast<const char*>(&size),4);out+=value;};
        std::string legacy;const std::string url="https://calendar.google.com/calendar/ical/test/private-secret/basic.ics";
        packLegacy(legacy,url);packLegacy(legacy,"15 0 100 60 1160 760");store.write("settings",legacy);
        std::string legacyFeed;for(auto& value:std::vector<std::string>{url,wrap(all),"legacy-etag","legacy-modified","321"})packLegacy(legacyFeed,value);store.write("feed",legacyFeed);
        check(loadFeed(store,url).etag=="etag","New per-URL cache takes priority over legacy cache");
        const std::string oldOnly="https://calendar.google.com/calendar/ical/old/private-secret/basic.ics";
        legacyFeed.clear();for(auto& value:std::vector<std::string>{oldOnly,wrap(all),"old-etag","modified","321"})packLegacy(legacyFeed,value);store.write("feed",legacyFeed);
        check(loadFeed(store,oldOnly).body==wrap(all),"Legacy feed remains available offline after migration");
        saveSyncStatus(store,url,"Network unavailable");check(loadSyncStatus(store,url)=="Network unavailable"&&loadSyncStatus(store,oldOnly).empty(),"Sync status persisted and isolated");
        auto migrated=loadSettings(store);check(migrated.calendars.size()==1&&migrated.calendars[0].url==url,"Legacy single calendar migration");
        migrated.calendars.push_back({"Work","https://calendar.google.com/calendar/ical/work/private-secret/basic.ics",3,false});migrated.closeToTray=false;migrated.locked=true;
        saveSettings(store,migrated);auto saved=loadSettings(store);check(saved.calendars.size()==2&&saved.calendars[1].color==3&&!saved.calendars[1].visible&&!saved.closeToTray&&saved.locked,"Multi-calendar settings persistence");
        Feed second{saved.calendars[1].url,wrap(repeated),"second","modified",456};saveFeed(store,second);
        check(loadFeed(store,url).body==wrap(all)&&loadFeed(store,second.url).body==wrap(repeated),"Independent caches retain both calendars");
        saved.calendars.clear();saved.url.clear();saveSettings(store,saved);check(loadSettings(store).calendars.empty(),"Removing all calendars does not resurrect legacy calendar");
    }
    auto icsPath=std::filesystem::path(tmp)/(L"deck-import-"+std::to_wstring(GetCurrentProcessId())+L"-l\u1ecbch.ics");
    std::string importedId;
    {
        Store store(path);Settings mixed;mixed.calendars.push_back({"Online",publicFeed,1,true});
        std::string detailed=wrap(event("UID:local\r\nDTSTART:20261019T090000\r\nDTEND:20261019T100000\r\nSUMMARY:Offline class\r\nLOCATION:Room 204\r\nDESCRIPTION:Offline details\\nSecond line\r\n"));
        {std::ofstream file(icsPath,std::ios::binary);file<<"\xEF\xBB\xBF"<<detailed;}
        auto imported=importCalendarFile(store,mixed,icsPath,week);
        check(imported.calendars.size()==2&&imported.calendars[0].url==publicFeed,"Import preserves existing online calendars");
        auto& local=imported.calendars.back();importedId=local.url;
        check(offlineCalendar(local)&&!validFeedUrl(local.url),"Imported source uses internal ID, not a network URL");
        auto feed=loadFeed(store,local.url);auto displayed=expandCalendar(feed.body,week);
        check(displayed.events.size()==1&&displayed.events[0].title=="Offline class"&&displayed.events[0].location=="Room 204"&&displayed.events[0].description=="Offline details\nSecond line","UTF-8 BOM import retains event details");
        check(feed.checked>0&&feed.body==detailed,"Import stores the complete file and import timestamp");
        imported.calendars[1].name="School offline";imported.calendars[1].color=4;imported.calendars[1].visible=false;
        {std::ofstream file(icsPath,std::ios::binary);file<<wrap(repeated);}
        auto replaced=importCalendarFile(store,imported,icsPath,week,1);
        check(replaced.calendars.size()==2&&replaced.calendars[1].url==importedId&&replaced.calendars[1].name=="School offline"&&replaced.calendars[1].color==4&&!replaced.calendars[1].visible,"Replacement keeps source identity and preferences");
        check(expandCalendar(loadFeed(store,importedId).body,week).events.size()==2,"Replacement updates occurrences");
        bool failed=false;
        {std::ofstream file(icsPath,std::ios::binary);file<<"<html>not an ICS file</html>";}
        try{importCalendarFile(store,replaced,icsPath,week,1);}catch(...){failed=true;}
        check(failed&&loadFeed(store,importedId).body==wrap(repeated)&&loadSettings(store).calendars[1].name=="School offline","Invalid replacement preserves previous feed and settings");
        failed=false;{std::ofstream file(icsPath,std::ios::binary);}
        try{importCalendarFile(store,replaced,icsPath,week);}catch(...){failed=true;}
        check(failed&&loadSettings(store).calendars.size()==2,"Empty import does not create a calendar");
        failed=false;{std::ofstream file(icsPath,std::ios::binary);file.seekp(10*1024*1024);file.put('x');}
        try{importCalendarFile(store,replaced,icsPath,week);}catch(...){failed=true;}
        check(failed&&loadFeed(store,importedId).body==wrap(repeated),"Oversized import is rejected without losing saved content");
        failed=false;try{importCalendarFile(store,replaced,icsPath,week,0);}catch(...){failed=true;}
        check(failed,"File replacement cannot overwrite a remote calendar");
        failed=false;try{store.transaction([&]{store.write("rollback-test","not committed");throw std::runtime_error("injected failure");});}catch(...){failed=true;}
        check(failed&&store.read("rollback-test").empty(),"Import transaction rolls back partial writes");
    }
    std::filesystem::remove(icsPath);
    {
        Store reopened(path);auto restored=loadSettings(reopened);
        check(restored.calendars.size()==2&&offlineCalendar(restored.calendars[1])&&loadFeed(reopened,importedId).body==wrap(repeated),"Offline snapshot survives restart after original file is deleted");
    }
    {std::ifstream stream(path,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(stream)),{});check(bytes.find("private-secret")==bytes.npos&&bytes.find("SUMMARY:Trip")==bytes.npos&&bytes.find("School offline")==bytes.npos,"No plaintext URL/events/calendar names in SQLite");}
    std::filesystem::remove(path);
}
int main(int argc,char** argv){try{
    tests();std::cout<<checks<<" checks passed\n";
    if(argc>1&&std::string(argv[1])=="--network"){
        const std::string url="https://calendar.google.com/calendar/ical/en.usa%23holiday%40group.v.calendar.google.com/public/basic.ics";
        std::atomic_bool stop=false;auto feed=downloadFeed(url,{},stop);auto calendar=expandCalendar(feed.body,monday(today()));
        check(!feed.body.empty()&&feed.checked>0,"Download actual public Google iCal feed");
        auto rechecked=downloadFeed(url,feed,stop);check(rechecked.body==feed.body,"Conditional request preserves content");
        std::cout<<"Public Google iCal HTTPS integration passed ("<<feed.body.size()<<" bytes, "<<calendar.warnings.size()<<" warnings)\n";
    }
    return 0;
}catch(const std::exception& ex){std::cerr<<"FAIL after "<<checks<<" checks: "<<ex.what()<<'\n';return 1;}}
