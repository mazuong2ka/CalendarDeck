#include "calendar.h"
#include "platform.h"
#include "resource.h"
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <d2d1.h>
#include <dwrite.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <sstream>
#include <cwchar>
#include <map>

namespace {
using namespace deck;
constexpr UINT dataMessage=WM_APP+1,trayMessage=WM_APP+2;
enum {idPrev=101,idToday,idNext,idSettings,idSync,idDesktop,idFloat,idHide,idExit,idStatus,idLock};
enum {editUrl=201,editInterval,idSave,idCancel,editName,editColor,editVisible,idAdd,idRemove,idList,idOneSync,idGeneral,idCalendars,idAppearance,idAbout,idCloseTray,idShowUrl,idSourceStatus,idImport,idSourceType,idSourceHint};
template<class T> void release(T*& p){if(p){p->Release();p=nullptr;}}
std::wstring text(HWND w){int n=GetWindowTextLengthW(w);std::wstring s(n+1,0);GetWindowTextW(w,s.data(),n+1);s.resize(n);return s;}
std::wstring shortDate(Stamp t){auto p=parts(t);wchar_t b[40];swprintf_s(b,L"%02u/%02u/%04d",p.d,p.m,p.y);return b;}
std::wstring clockText(Stamp t){auto p=parts(t);wchar_t b[20];swprintf_s(b,L"%02d:%02d",p.hour,p.minute);return b;}
std::wstring checkedText(Stamp t){if(!t)return L"Never synced";auto l=toLocal(t);return shortDate(l)+L" "+clockText(l);}

struct Job{Settings settings;Stamp week{};uint64_t generation{};bool refresh{},demo{};std::string only;};
struct Answer{uint64_t generation{};CalendarResult calendar;std::wstring status;bool replace{true};std::map<std::string,std::wstring> states;};
class Worker {
    HWND window_;std::filesystem::path file_;std::mutex mutex_;std::condition_variable cv_;std::optional<Job> job_;
    std::atomic_bool stop_{false};std::thread thread_;
    void run(){
        while(true){Job j;{std::unique_lock lock(mutex_);cv_.wait(lock,[&]{return stop_||job_.has_value();});if(stop_)return;j=std::move(*job_);job_.reset();}
            for(int pass=0;pass<(j.refresh&&!j.demo?2:1);++pass){
            bool fetch=j.refresh&&pass==1;
            auto a=std::make_unique<Answer>();a->generation=j.generation;
            try{
                if(j.demo){a->calendar=expandCalendar(demoCalendar(j.week),j.week);a->status=L"DEMO · sample events · connect your calendar in Settings";}
                else {
                    Store store(file_);int visible=0,errors=0;
                    for(auto& source:j.settings.calendars){
                        if(stop_)return;
                        Feed f;CalendarResult parsed;std::wstring error;
                        try{
                            f=loadFeed(store,source.url);
                            if(offlineCalendar(source)&&f.body.empty())throw std::runtime_error("Offline calendar data is missing; import the .ics file again");
                            if(!offlineCalendar(source))error=wide(loadSyncStatus(store,source.url));
                            if(fetch&&!offlineCalendar(source)&&(j.only.empty()||j.only==source.url)){
                                auto fresh=downloadFeed(source.url,f,stop_);
                                parsed=expandCalendar(fresh.body,j.week);saveFeed(store,fresh);f=std::move(fresh);error.clear();saveSyncStatus(store,source.url,{});
                            }else if(!f.body.empty())parsed=expandCalendar(f.body,j.week);
                        }catch(const std::exception& ex){error=wide(ex.what());try{saveSyncStatus(store,source.url,utf8(error));}catch(...){}
                            if(!f.body.empty())try{parsed=expandCalendar(f.body,j.week);}catch(...){}
                        }
                        if(!error.empty())++errors;
                        a->states[source.url]=offlineCalendar(source)
                            ?(error.empty()?L"Offline file | Imported: ":L"File error: "+error+L" | Imported: ")+checkedText(f.checked)
                            :(error.empty()?L"Last synced: ":L"Offline: "+error+L" | Last synced: ")+checkedText(f.checked);
                        if(source.visible){++visible;for(auto& e:parsed.events){e.color=source.color;e.calendarName=source.name;a->calendar.events.push_back(std::move(e));}}
                        for(auto& warning:parsed.warnings)a->calendar.warnings.push_back(source.name+": "+warning);
                    }
                    a->status=(j.refresh&&pass==0?L"Checking calendars | ":L"")+std::to_wstring(visible)+L" visible calendars | "+std::to_wstring(errors)+L" sync errors | Click for details";
                }
            }catch(const std::exception& ex){a->status=L"Calendar error · "+wide(ex.what());a->replace=false;}
            if(stop_)return;
            if(PostMessageW(window_,dataMessage,0,reinterpret_cast<LPARAM>(a.get())))a.release();
            }
        }
    }
public:
    Worker(HWND w,std::filesystem::path file):window_(w),file_(std::move(file)),thread_([this]{run();}){}
    ~Worker(){stop();}
    void submit(Job j){std::lock_guard lock(mutex_);if(job_&&job_->refresh){j.refresh=true;j.only.clear();}job_=std::move(j);cv_.notify_one();}
    void stop(){stop_=true;cv_.notify_one();if(thread_.joinable())thread_.join();}
};

struct Hit{D2D1_RECT_F rect;int command{};size_t event{};bool isEvent{};int allDay{-1};};
struct App {
    HINSTANCE instance{};HWND window{},settingsWindow{},tooltip{};HFONT font{};UINT taskbarCreated{};
    std::filesystem::path file;Settings settings;std::unique_ptr<Store> store;std::unique_ptr<Worker> worker;
    ID2D1Factory* factory{};IDWriteFactory* writing{};ID2D1HwndRenderTarget* target{};ID2D1SolidColorBrush* brush{};
    IDWriteTextFormat *small{},*normal{},*heading{};
    CalendarResult calendar;std::vector<Segment> segments;std::vector<Hit> hits;
    Stamp week{monday(today())},lastDay{today()},requestedWeek{};std::string requestedUrl;bool requestedDemo{};
    uint64_t generation{};bool demo{},portable{},desktop{},closing{},followToday{true};
    std::wstring status=L"Loading calendar…",tooltipText;int hovered{-1};float scale{1},width{},height{};
    ~App(){if(worker)worker->stop();release(brush);release(target);release(small);release(normal);release(heading);release(writing);release(factory);if(font)DeleteObject(font);if(settingsHeading)DeleteObject(settingsHeading);}
    std::map<std::string,std::wstring> sourceStates;
    int settingsPage{1},selected{-1};
    HFONT settingsHeading{};
    bool editorDirty{},buildingSettings{};
    bool discardEditor(){return !editorDirty||MessageBoxW(settingsWindow,L"Discard your unsaved changes?",L"Unsaved changes",MB_YESNO|MB_ICONQUESTION)==IDYES;}
    void request(bool refresh,std::string only={}){++generation;
        if(week!=requestedWeek||settings.url!=requestedUrl||demo!=requestedDemo){calendar={};segments.clear();hits.clear();hovered=-1;}
        requestedWeek=week;requestedUrl=settings.url;requestedDemo=demo;
        status=refresh?L"Checking Google Calendar…":L"Loading calendar…";worker->submit({settings,week,generation,refresh,demo,std::move(only)});InvalidateRect(window,nullptr,FALSE);}
    void syncTimer(){KillTimer(window,2);if(std::any_of(settings.calendars.begin(),settings.calendars.end(),[](const auto& source){return !offlineCalendar(source);})&&!demo)SetTimer(window,2,UINT(settings.intervalMinutes)*60000,nullptr);}
    void minuteTimer(){SYSTEMTIME now{};GetLocalTime(&now);SetTimer(window,1,60000-now.wSecond*1000-now.wMilliseconds,nullptr);}
    void save(){if(!IsIconic(window)&&!IsZoomed(window)){RECT r{};GetWindowRect(window,&r);settings.x=r.left;settings.y=r.top;settings.width=r.right-r.left;settings.height=r.bottom-r.top;}
        settings.desktop=desktop;try{saveSettings(*store,settings);}catch(const std::exception& ex){status=L"Settings could not be saved · "+wide(ex.what());}}
    void tray(bool add){NOTIFYICONDATAW n{};n.cbSize=sizeof(n);n.hWnd=window;n.uID=1;n.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;n.uCallbackMessage=trayMessage;n.hIcon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(IDI_CALENDAR_DECK),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED));wcscpy_s(n.szTip,L"Calendar Deck · right-click for options");Shell_NotifyIconW(add?NIM_ADD:NIM_DELETE,&n);}
    void invalidate(){if(IsWindowVisible(window)&&!IsIconic(window))InvalidateRect(window,nullptr,FALSE);}
    void hideWindow(){
        HWND host=desktop?GetParent(window):nullptr;RECT area{};GetWindowRect(window,&area);
        if(host)MapWindowPoints(nullptr,host,reinterpret_cast<POINT*>(&area),2);
        ShowWindow(window,SW_HIDE);discardTarget();
        if(host)RedrawWindow(host,&area,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);
    }
    void discardTarget(){release(brush);release(target);}
    bool graphics(){if(target)return true;
        RECT rc{};GetClientRect(window,&rc);UINT dpi=GetDpiForWindow(window);scale=dpi/96.f;
        HRESULT hr=factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),D2D1::HwndRenderTargetProperties(window,D2D1::SizeU(rc.right,rc.bottom),D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS),&target);
        if(FAILED(hr))return false;
        target->SetDpi(float(dpi),float(dpi));
        if(FAILED(target->CreateSolidColorBrush(D2D1::ColorF(0xffffff),&brush))){discardTarget();return false;}return true;}
    void color(UINT32 rgb,float opacity=1){brush->SetColor(D2D1::ColorF(rgb,opacity));}
    void fill(D2D1_RECT_F r,UINT32 c,float radius=0){color(c);if(radius)target->FillRoundedRectangle(D2D1::RoundedRect(r,radius,radius),brush);else target->FillRectangle(r,brush);}
    void label(const std::wstring& s,D2D1_RECT_F r,IDWriteTextFormat* format,UINT32 c=0xe7edf7){color(c);target->DrawText(s.c_str(),UINT32(s.size()),format,r,brush,D2D1_DRAW_TEXT_OPTIONS_CLIP);}
    void line(float x,float y,float ex,float ey,UINT32 c,float thickness=1){color(c);target->DrawLine(D2D1::Point2F(x,y),D2D1::Point2F(ex,ey),brush,thickness);}
    void button(const wchar_t* text,D2D1_RECT_F r,int command,bool active=false){fill(r,active?0x263d55:0x1b2635,6);label(text,{r.left+12,r.top+7,r.right-4,r.bottom},normal,active?0x81c8ff:0xdbe4f0);hits.push_back({r,command,0,false});}
    void draw(){
        if(!graphics())return;
        auto size=target->GetSize();width=size.width;height=size.height;hits.clear();target->BeginDraw();target->Clear(D2D1::ColorF(0x111923));
        label(L"Calendar Deck",{24,17,255,54},heading);
        label(shortDate(week)+L" — "+shortDate(week+6*daySeconds),{24,52,width-425,77},small,0x93a4ba);
        label(std::to_wstring(settings.calendars.size())+L" calendars connected",{24,75,350,91},small,0x93a4ba);
        float right=width-24;
        button(L"‹",{right-378,24,right-338,60},idPrev);button(L"Today",{right-330,24,right-246,60},idToday);button(L"›",{right-238,24,right-198,60},idNext);
        button(L"Sync",{right-184,24,right-112,60},idSync);button(L"Settings",{right-104,24,right,60},idSettings);
        constexpr float left=67,header=92,gridTop=165;float gridBottom=height-55;float cell=(width-left-24)/7,unit=(gridBottom-gridTop)/24;
        auto current=today();static const wchar_t* names[]={L"MON",L"TUE",L"WED",L"THU",L"FRI",L"SAT",L"SUN"};
        for(int day=0;day<7;++day){float x=left+day*cell;bool now=current==week+day*daySeconds;
            if(now)fill({x,header-3,x+cell,gridBottom},0x152333);
            auto p=parts(week+day*daySeconds);label(std::wstring(names[day])+L"   "+std::to_wstring(p.d),{x+9,header,x+cell,header+28},normal,now?0x81c8ff:0xc5d0df);
            std::vector<size_t> all;for(size_t i=0;i<calendar.events.size();++i){auto& e=calendar.events[i];if(e.allDay&&e.start<week+(day+1)*daySeconds&&e.end>week+day*daySeconds)all.push_back(i);}
            if(!all.empty()){D2D1_RECT_F r{x+5,header+32,x+cell-5,gridTop-7};fill(r,0x294238,4);static const UINT32 allColors[]={0x80c8ff,0xb7a5fa,0x8fd5b4,0xeda6be,0xe4cf89};fill({r.left,r.top,r.left+3,r.bottom},allColors[std::clamp(calendar.events[all.front()].color,0,4)]);auto s=wide(calendar.events[all.front()].title);if(all.size()>1)s+=L"  +"+std::to_wstring(all.size()-1);label(s,{r.left+6,r.top+4,r.right-3,r.bottom},small,0xb5ead2);hits.push_back({r,0,all.front(),true,day});}
            line(x,gridTop,x,gridBottom,0x263343);
        }
        for(int hour=0;hour<=24;++hour){float y=gridTop+hour*unit;line(left,y,width-24,y,0x293646);wchar_t b[10];swprintf_s(b,L"%02d:00",hour);label(b,{15,y-7,62,y+14},small,0x8c9cb0);}
        static const UINT32 cards[]={0x243d57,0x343552,0x25453e,0x4a3741,0x414025};
        static const UINT32 accents[]={0x80c8ff,0xb7a5fa,0x8fd5b4,0xeda6be,0xe4cf89};
        for(auto& s:segments){auto& e=calendar.events[s.event];float x=left+s.day*cell;float col=(cell-8)/s.columns;
            float top=gridTop+float(s.start-week-s.day*daySeconds)/3600*unit,bottom=gridTop+float(s.end-week-s.day*daySeconds)/3600*unit;
            D2D1_RECT_F r{x+4+s.column*col,top+1,x+4+(s.column+1)*col-3,std::min(gridBottom,bottom-1)};
            // Short events retain their true vertical interval; tooltip/click exposes the title.
            if(r.bottom<r.top+2)r.bottom=std::min(gridBottom,r.top+2);
            size_t hash=size_t(std::clamp(e.color,0,4));fill(r,cards[hash],3);fill({r.left,r.top,r.left+2,r.bottom},accents[hash]);
            if(r.bottom-r.top>13)label(wide(e.title),{r.left+6,r.top+3,r.right-3,r.bottom},small,0xe9eff7);
            if(r.bottom-r.top>36)label(clockText(e.start)+L" – "+clockText(e.end),{r.left+6,r.top+20,r.right-3,r.bottom},small,0xadbfd2);
            hits.push_back({r,0,s.event,true});
        }
        SYSTEMTIME st{};GetLocalTime(&st);if(current>=week&&current<week+7*daySeconds){int day=int((current-week)/daySeconds);float y=gridTop+(st.wHour+st.wMinute/60.f)*unit,x=left+day*cell;line(x,y,x+cell,y,0xff857b,1.5f);color(0xff857b);target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x,y),3,3),brush);}
        if(calendar.events.empty()&&status.find(L"Loading")==std::wstring::npos&&status.find(L"Checking")==std::wstring::npos){fill({left+28,gridTop+20,width-52,gridTop+64},0x1b2838,6);label(settings.calendars.empty()?L"Your week, at a glance. Add your Google Calendar in Settings.":L"No events in this week.",{left+43,gridTop+32,width-60,gridTop+60},normal,0xa8bdd3);}
        label(desktop?L"DESKTOP":L"FLOATING",{24,height-32,110,height-10},small,0x81c8ff);
        label(status,{118,height-32,width-24,height-9},small,calendar.warnings.empty()?0x93a4ba:0xe4cf89);
        hits.push_back({{118,height-40,width-24,height},idStatus,0,false});
        HRESULT hr=target->EndDraw();if(hr==HRESULT(D2DERR_RECREATE_TARGET))discardTarget();
    }
    int hit(float x,float y){for(int i=int(hits.size())-1;i>=0;--i){auto& r=hits[i].rect;if(x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom)return i;}return -1;}
    std::wstring eventDetails(size_t index){auto& e=calendar.events[index];std::wstring s=wide(e.title)+L"\n"+wide(e.calendarName)+L"\n";
        s+=e.allDay?shortDate(e.start)+L" · All day":shortDate(e.start)+L"  "+clockText(e.start)+L" — "+shortDate(e.end)+L"  "+clockText(e.end);
        if(!e.location.empty())s+=L"\n\n"+wide(e.location);
        if(!e.description.empty())s+=L"\n\n"+wide(e.description).substr(0,8000);
        return s;}
    std::wstring details(const Hit& hit){if(hit.allDay<0)return eventDetails(hit.event);
        Stamp day=week+hit.allDay*daySeconds;std::wstring s=shortDate(day)+L" · All-day events";
        for(size_t i=0;i<calendar.events.size();++i){auto& e=calendar.events[i];if(e.allDay&&e.start<day+daySeconds&&e.end>day){s+=L"\n\n"+eventDetails(i);if(s.size()>50000){s.resize(50000);s+=L"\n…";break;}}}return s;}
    void hover(LPARAM p){float x=GET_X_LPARAM(p)/scale,y=GET_Y_LPARAM(p)/scale;int h=hit(x,y);if(h==hovered)return;hovered=h;
        tooltipText=h>=0&&hits[h].isEvent?details(hits[h]):L"";
        TOOLINFOW ti{};ti.cbSize=sizeof(ti);ti.hwnd=window;ti.uId=1;ti.lpszText=tooltipText.data();SendMessageW(tooltip,TTM_UPDATETIPTEXTW,0,reinterpret_cast<LPARAM>(&ti));}
    static BOOL CALLBACK findDesktop(HWND top,LPARAM param){if(FindWindowExW(top,nullptr,L"SHELLDLL_DefView",nullptr)){auto next=FindWindowExW(nullptr,top,L"WorkerW",nullptr);if(next)*reinterpret_cast<HWND*>(param)=next;}return TRUE;}
    bool mode(bool useDesktop){
        RECT rect{};GetWindowRect(window,&rect);if(!IsIconic(window)&&!IsZoomed(window)){settings.x=rect.left;settings.y=rect.top;settings.width=rect.right-rect.left;settings.height=rect.bottom-rect.top;}
        HWND parent=nullptr;
        if(useDesktop){auto progman=FindWindowW(L"Progman",nullptr);DWORD_PTR result{};if(progman)SendMessageTimeoutW(progman,0x052c,0,0,SMTO_ABORTIFHUNG,1000,&result);EnumWindows(findDesktop,reinterpret_cast<LPARAM>(&parent));if(!parent){status=L"Windows desktop host is unavailable · Floating mode retained";invalidate();return false;}}
        if(IsZoomed(window)||IsIconic(window))ShowWindow(window,SW_RESTORE);
        hideWindow();SetLastError(0);auto old=SetParent(window,parent);if(!old&&GetLastError()){ShowWindow(window,SW_SHOWNOACTIVATE);status=L"Cannot attach to the desktop · mode unchanged";return false;}
        SetWindowLongPtrW(window,GWL_STYLE,useDesktop?WS_CHILD|WS_CLIPCHILDREN|(settings.locked?0:WS_THICKFRAME):WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN);
        SetWindowLongPtrW(window,GWL_EXSTYLE,useDesktop?WS_EX_TOOLWINDOW:WS_EX_APPWINDOW);
        desktop=useDesktop;POINT point{settings.x,settings.y};if(parent)ScreenToClient(parent,&point);
        SetWindowPos(window,useDesktop?HWND_BOTTOM:HWND_NOTOPMOST,point.x,point.y,settings.width,settings.height,SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_SHOWWINDOW);save();invalidate();return true;
    }
    void menu(){HMENU m=CreatePopupMenu();AppendMenuW(m,MF_STRING,idToday,L"Show Calendar Deck");AppendMenuW(m,MF_STRING,idSettings,L"Settings…");AppendMenuW(m,MF_STRING,idSync,L"Sync all calendars");AppendMenuW(m,MF_SEPARATOR,0,nullptr);
        AppendMenuW(m,MF_STRING|(desktop?MF_CHECKED:0),idDesktop,L"Desktop · behind application windows");AppendMenuW(m,MF_STRING|(!desktop?MF_CHECKED:0),idFloat,L"Floating · movable window");AppendMenuW(m,MF_STRING|(settings.locked?MF_CHECKED:0),idLock,L"Lock widget position");AppendMenuW(m,MF_STRING,idHide,L"Hide");AppendMenuW(m,MF_SEPARATOR,0,nullptr);AppendMenuW(m,MF_STRING,idExit,L"Exit");
        POINT p{};GetCursorPos(&p);SetForegroundWindow(window);int cmd=TrackPopupMenu(m,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,window,nullptr);DestroyMenu(m);if(cmd)command(cmd);PostMessageW(window,WM_NULL,0,0);}
    void command(int cmd){switch(cmd){
        case idPrev:week-=7*daySeconds;followToday=false;request(false);break;
        case idNext:week+=7*daySeconds;followToday=false;request(false);break;
        case idToday:ShowWindow(window,IsIconic(window)?SW_RESTORE:SW_SHOW);if(!desktop)SetForegroundWindow(window);week=monday(today());followToday=true;request(false);break;
        case idSync:request(true);break;
        case idDesktop:mode(true);break;
        case idFloat:mode(false);break;
        case idHide:hideWindow();save();break;
        case idLock:settings.locked=!settings.locked;if(desktop)mode(true);save();break;
        case idExit:SendMessageW(window,WM_CLOSE,1,0);break;
        case idSettings:openSettings();break;
        case idStatus:{std::wstring s=status;for(auto& c:settings.calendars)s+=L"\n\n"+wide(c.name)+L": "+sourceStates[c.url];for(auto& w:calendar.warnings)s+=L"\n\n"+wide(w);MessageBoxW(window,s.c_str(),L"Calendar status",MB_OK|MB_ICONINFORMATION);break;}
    }}

    HWND control(const wchar_t* cls,const std::wstring& title,DWORD style,int x,int y,int w,int h,int id){
        float ds=GetDpiForWindow(settingsWindow)/96.f;
        HWND c=CreateWindowExW(wcscmp(cls,L"EDIT")==0?WS_EX_CLIENTEDGE:0,cls,title.c_str(),WS_CHILD|WS_VISIBLE|style,int(x*ds),int(y*ds),int(w*ds),int(h*ds),settingsWindow,reinterpret_cast<HMENU>(INT_PTR(id)),instance,nullptr);
        bool titleLine=wcscmp(cls,L"STATIC")==0&&x==200&&y==24;
        SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(titleLine&&settingsHeading?settingsHeading:font),TRUE);return c;
    }
    void updateSourceStatus(){
        if(!settingsWindow||settingsPage!=1)return;
        std::wstring state=L"Not connected yet";
        if(selected>=0&&selected<int(settings.calendars.size())){auto& c=settings.calendars[selected];auto it=sourceStates.find(c.url);state=it==sourceStates.end()?L"Not checked this session":it->second;}
        SetDlgItemTextW(settingsWindow,idSourceStatus,state.c_str());
    }
    void selectCalendar(){
        buildingSettings=true;
        CalendarSource c;if(selected>=0&&selected<int(settings.calendars.size()))c=settings.calendars[selected];
        SetDlgItemTextW(settingsWindow,editName,wide(c.name).c_str());SetDlgItemTextW(settingsWindow,editUrl,offlineCalendar(c)?L"Imported .ics snapshot":wide(c.url).c_str());
        EnableWindow(GetDlgItem(settingsWindow,editUrl),!offlineCalendar(c));EnableWindow(GetDlgItem(settingsWindow,idShowUrl),!offlineCalendar(c));
        SetDlgItemTextW(settingsWindow,idSourceType,offlineCalendar(c)?L"Source: imported .ics file (offline)":L"iCal URL or public Google Calendar link");
        SetDlgItemTextW(settingsWindow,idSourceHint,offlineCalendar(c)?L"Replace the file below when your schedule changes.":L"Public iCal requires public calendar access.");
        SetDlgItemTextW(settingsWindow,idOneSync,offlineCalendar(c)?L"Replace .ics...":L"Sync now");
        SendDlgItemMessageW(settingsWindow,editColor,CB_SETCURSEL,c.color,0);CheckDlgButton(settingsWindow,editVisible,c.visible?BST_CHECKED:BST_UNCHECKED);
        SendDlgItemMessageW(settingsWindow,editUrl,EM_SETPASSWORDCHAR,offlineCalendar(c)?0:L'*',0);CheckDlgButton(settingsWindow,idShowUrl,BST_UNCHECKED);
        EnableWindow(GetDlgItem(settingsWindow,idRemove),selected>=0);EnableWindow(GetDlgItem(settingsWindow,idOneSync),selected>=0);updateSourceStatus();editorDirty=false;buildingSettings=false;
    }
    void settingsContent(){
        buildingSettings=true;editorDirty=false;
        while(HWND child=GetWindow(settingsWindow,GW_CHILD))DestroyWindow(child);
        control(L"STATIC",L"CALENDAR DECK",0,24,24,160,25,0);
        const wchar_t* pages[]={L"General",L"Calendars",L"Appearance",L"About"};
        for(int i=0;i<4;++i)control(L"BUTTON",pages[i],WS_TABSTOP|BS_PUSHBUTTON,20,78+i*48,150,38,idGeneral+i);
        control(L"STATIC",L"v0.2.4",0,24,532,150,24,0);
        if(settingsPage==1){
            control(L"STATIC",L"My Calendars",0,200,24,580,28,0);
            control(L"STATIC",L"Select a calendar to edit. Save changes before switching calendars.",0,200,57,600,26,0);
            HWND list=control(L"LISTBOX",L"",LBS_NOTIFY|WS_VSCROLL|WS_BORDER|WS_TABSTOP,200,98,230,350,idList);
            for(auto& c:settings.calendars){auto name=(c.visible?L"[On] ":L"[Off] ")+wide(c.name)+(offlineCalendar(c)?L" (file)":L"");SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));}
            SendMessageW(list,LB_SETCURSEL,selected,0);
            control(L"BUTTON",L"+ Add URL Calendar",WS_TABSTOP,200,462,230,34,idAdd);
            control(L"BUTTON",L"Import .ics...",WS_TABSTOP,200,506,230,34,idImport);
            control(L"STATIC",L"Display name",0,454,98,340,23,0);
            HWND name=control(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,454,126,340,30,editName);SendMessageW(name,EM_SETLIMITTEXT,100,0);
            control(L"STATIC",L"iCal URL or public Google Calendar link",0,454,171,340,23,idSourceType);
            HWND url=control(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL|ES_PASSWORD,454,199,340,30,editUrl);SendMessageW(url,EM_SETLIMITTEXT,4096,0);
            control(L"BUTTON",L"Show URL",WS_TABSTOP|BS_AUTOCHECKBOX,454,237,340,24,idShowUrl);
            control(L"STATIC",L"Public iCal requires public calendar access.",0,454,261,340,17,idSourceHint);
            control(L"STATIC",L"Calendar color",0,454,278,340,24,0);
            HWND colors=control(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,454,307,340,180,editColor);
            for(auto color:{L"Blue",L"Purple",L"Green",L"Pink",L"Gold"})SendMessageW(colors,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(color));
            control(L"BUTTON",L"Show events on calendar",WS_TABSTOP|BS_AUTOCHECKBOX,454,350,340,26,editVisible);
            control(L"STATIC",L"",0,454,390,340,64,idSourceStatus);
            control(L"BUTTON",L"Sync now",WS_TABSTOP,454,462,160,34,idOneSync);
            control(L"BUTTON",L"Remove calendar",WS_TABSTOP,634,462,160,34,idRemove);
            control(L"BUTTON",L"Save calendar",WS_TABSTOP|BS_DEFPUSHBUTTON,634,526,160,34,idSave);
            selectCalendar();
        }else if(settingsPage==0){
            control(L"STATIC",L"General",0,200,24,580,30,0);
            control(L"STATIC",L"Check calendars every (5-1440 minutes)",0,200,95,550,25,0);
            control(L"EDIT",std::to_wstring(settings.intervalMinutes),WS_TABSTOP|ES_NUMBER,200,133,140,32,editInterval);
            control(L"BUTTON",L"Close to system tray (keep syncing in the background)",WS_TABSTOP|BS_AUTOCHECKBOX,200,195,590,32,idCloseTray);
            CheckDlgButton(settingsWindow,idCloseTray,settings.closeToTray?BST_CHECKED:BST_UNCHECKED);
            control(L"STATIC",L"Floating mode supports minimize, maximize and restore.\nUse the tray menu to show the calendar or exit completely.",0,200,265,590,60,0);
            control(L"BUTTON",L"Save preferences",WS_TABSTOP,610,526,184,34,idSave);
        }else if(settingsPage==2){
            control(L"STATIC",L"Appearance & desktop",0,200,24,580,30,0);
            control(L"STATIC",L"Dark theme with calendar colors\n\nThe weekly grid always shows 00:00-24:00.\nSelect a color for each calendar in Calendars.",0,200,95,590,110,0);
            control(L"BUTTON",L"Desktop mode",WS_TABSTOP,200,240,220,38,idDesktop);
            control(L"BUTTON",L"Floating mode",WS_TABSTOP,440,240,220,38,idFloat);
            control(L"STATIC",L"Drag the header or edges to position and resize the widget.\nUse the tray menu to lock its position.",0,200,310,590,65,0);
        }else{
            control(L"STATIC",L"Calendar Deck 0.2.4",0,200,24,580,30,0);
            control(L"STATIC",L"Window Controls & Multi-Calendar Settings\n\nLightweight Windows calendar built with Win32 + Direct2D.\nGoogle iCal feeds and offline .ics files, encrypted cache.\n\nGet your URL in Google Calendar:\nSettings > your calendar > Integrate calendar >\nPublic address in iCal format, or Secret address.\nPublic share links with src= or cid= are also accepted.\n\nURLs and cached feeds are protected by Windows DPAPI.",0,200,95,590,330,0);
        }
        buildingSettings=false;InvalidateRect(settingsWindow,nullptr,TRUE);
    }
    void saveEditor(){
        Settings next=settings;bool connectChanged=false;
        if(settingsPage==0){BOOL valid=FALSE;UINT n=GetDlgItemInt(settingsWindow,editInterval,&valid,FALSE);if(!valid||n<5||n>1440)throw std::runtime_error("Enter an interval from 5 to 1440 minutes");next.intervalMinutes=int(n);next.closeToTray=IsDlgButtonChecked(settingsWindow,idCloseTray)==BST_CHECKED;}
        else if(settingsPage==1){
            auto trim=[](std::wstring v){auto first=v.find_first_not_of(L" \r\n\t");if(first==v.npos)return std::wstring{};return v.substr(first,v.find_last_not_of(L" \r\n\t")-first+1);};
            CalendarSource c;if(selected>=0)c=settings.calendars[selected];c.name=utf8(trim(text(GetDlgItem(settingsWindow,editName))));if(!offlineCalendar(c))c.url=utf8(trim(text(GetDlgItem(settingsWindow,editUrl))));c.color=int(SendDlgItemMessageW(settingsWindow,editColor,CB_GETCURSEL,0,0));c.visible=IsDlgButtonChecked(settingsWindow,editVisible)==BST_CHECKED;
            if(c.name.empty())throw std::runtime_error("Enter a display name for this calendar");
            if(!offlineCalendar(c)){
            auto normalized=normalizeFeedUrl(c.url);
            if(!normalized)throw std::runtime_error("Paste a Google Calendar public/secret iCal URL (.ics), or a single-calendar share link containing src= or cid=. A general Calendar page or event link cannot be used. For an embed with multiple calendars, add each calendar separately.");
            c.url=*normalized;
            }
            for(size_t i=0;i<next.calendars.size();++i)if(int(i)!=selected&&next.calendars[i].url==c.url)throw std::runtime_error("This calendar URL is already connected");
            connectChanged=selected<0||next.calendars[selected].url!=c.url;
            if(selected<0){if(next.calendars.size()>=100)throw std::runtime_error("Maximum 100 calendars");next.calendars.push_back(c);}else next.calendars[selected]=c;
        }
        next.url.clear();saveSettings(*store,next);settings=std::move(next);if(settingsPage==1&&selected<0)selected=int(settings.calendars.size())-1;
        if(settingsPage==1){demo=false;}
        calendar={};segments.clear();hits.clear();syncTimer();request(connectChanged,settingsPage==1?settings.calendars[selected].url:std::string{});settingsContent();
    }
    void importFile(int replace=-1){
        wchar_t path[32768]{};OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=settingsWindow;
        dialog.lpstrFilter=L"iCalendar files (*.ics)\0*.ics\0\0";dialog.lpstrFile=path;dialog.nMaxFile=32768;
        dialog.lpstrTitle=replace<0?L"Import offline calendar":L"Replace offline calendar";
        dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_DONTADDTORECENT;
        if(!GetOpenFileNameW(&dialog)){if(CommDlgExtendedError())throw std::runtime_error("Cannot open the calendar file picker");return;}
        SetCursor(LoadCursorW(nullptr,IDC_WAIT));
        try{settings=importCalendarFile(*store,settings,std::filesystem::path(path),week,replace);}
        catch(...){SetCursor(LoadCursorW(nullptr,IDC_ARROW));throw;}
        SetCursor(LoadCursorW(nullptr,IDC_ARROW));
        selected=replace<0?int(settings.calendars.size())-1:replace;demo=false;calendar={};segments.clear();hits.clear();syncTimer();request(false);settingsContent();
    }
    static LRESULT CALLBACK settingsProc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
        auto a=reinterpret_cast<App*>(GetWindowLongPtrW(w,GWLP_USERDATA));
        if(msg==WM_NCCREATE){a=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(a));}
        if(!a)return DefWindowProcW(w,msg,wp,lp);
        if(msg==WM_CTLCOLORSTATIC){auto dc=reinterpret_cast<HDC>(wp);SetTextColor(dc,RGB(34,48,67));SetBkMode(dc,TRANSPARENT);return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));}
        if(msg==WM_COMMAND)try{
            int id=LOWORD(wp);
            if(id>=idGeneral&&id<=idAbout){if(!a->discardEditor())return 0;a->settingsPage=id-idGeneral;a->settingsContent();return 0;}
            if(id==idList&&HIWORD(wp)==LBN_SELCHANGE){if(!a->discardEditor()){SendDlgItemMessageW(w,idList,LB_SETCURSEL,a->selected,0);return 0;}a->selected=int(SendDlgItemMessageW(w,idList,LB_GETCURSEL,0,0));a->selectCalendar();}
            if(!a->buildingSettings&&((HIWORD(wp)==EN_CHANGE&&(id==editName||id==editUrl||id==editInterval))||(id==editColor&&HIWORD(wp)==CBN_SELCHANGE)||id==editVisible||id==idCloseTray))a->editorDirty=true;
            if(id==idShowUrl){SendDlgItemMessageW(w,editUrl,EM_SETPASSWORDCHAR,IsDlgButtonChecked(w,idShowUrl)==BST_CHECKED?0:L'*',0);InvalidateRect(GetDlgItem(w,editUrl),nullptr,TRUE);}
            if(id==idAdd){if(!a->discardEditor())return 0;a->selected=-1;SendDlgItemMessageW(w,idList,LB_SETCURSEL,-1,0);a->selectCalendar();SetFocus(GetDlgItem(w,editName));}
            if(id==idSave)a->saveEditor();
            if(id==idImport){if(!a->discardEditor())return 0;a->importFile();return 0;}
            if(id==idOneSync&&a->selected>=0){if(offlineCalendar(a->settings.calendars[a->selected])){if(!a->discardEditor())return 0;a->importFile(a->selected);return 0;}if(a->editorDirty){MessageBoxW(w,L"Save your changes before syncing.",L"Unsaved changes",MB_OK);return 0;}a->request(true,a->settings.calendars[a->selected].url);SetDlgItemTextW(w,idSourceStatus,L"Syncing...");}
            if(id==idRemove&&a->selected>=0&&MessageBoxW(w,L"Remove this calendar from Calendar Deck? Google Calendar events will remain unchanged.",L"Remove calendar",MB_YESNO|MB_ICONQUESTION)==IDYES){auto next=a->settings;next.calendars.erase(next.calendars.begin()+a->selected);next.url.clear();saveSettings(*a->store,next);a->settings=std::move(next);a->selected=-1;a->calendar={};a->segments.clear();a->hits.clear();a->syncTimer();a->request(false);a->settingsContent();}
            if(id==idDesktop||id==idFloat)a->command(id);
            return 0;
        }catch(const std::exception& ex){MessageBoxW(w,wide(ex.what()).c_str(),L"Check settings",MB_OK|MB_ICONWARNING);return 0;}
        if(msg==WM_CLOSE){if(a->discardEditor())DestroyWindow(w);return 0;}if(msg==WM_DESTROY){a->settingsWindow=nullptr;return 0;}
        return DefWindowProcW(w,msg,wp,lp);
    }
    void openSettings(){
        if(settingsWindow){SetForegroundWindow(settingsWindow);return;}
        WNDCLASSW c{};c.lpfnWndProc=settingsProc;c.hInstance=instance;c.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(IDI_CALENDAR_DECK));c.hCursor=LoadCursorW(nullptr,IDC_ARROW);c.hbrBackground=static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));c.lpszClassName=L"CalendarDeckSettings";RegisterClassW(&c);
        UINT dpi=GetDpiForWindow(window);float ds=dpi/96.f;
        if(settingsHeading){DeleteObject(settingsHeading);}
        settingsHeading=CreateFontW(-int(22*ds),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        RECT r{0,0,LONG(824*ds),LONG(584*ds)};AdjustWindowRectExForDpi(&r,WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_DLGMODALFRAME,dpi);
        settingsWindow=CreateWindowExW(WS_EX_DLGMODALFRAME,c.lpszClassName,L"Calendar Deck - Settings",WS_CAPTION|WS_SYSMENU,CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,nullptr,nullptr,instance,this);
        selected=settings.calendars.empty()?-1:0;settingsContent();ShowWindow(settingsWindow,SW_SHOW);SetForegroundWindow(settingsWindow);
    }
    static LRESULT CALLBACK windowProc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
        auto a=reinterpret_cast<App*>(GetWindowLongPtrW(w,GWLP_USERDATA));
        if(msg==WM_NCCREATE){a=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);a->window=w;SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(a));}
        if(!a)return DefWindowProcW(w,msg,wp,lp);
        if(msg==a->taskbarCreated&&a->taskbarCreated){a->tray(true);if(a->desktop)a->mode(true);return 0;}
        switch(msg){
            case WM_SYSCOMMAND:if(a->desktop&&((wp&0xfff0)==SC_MAXIMIZE||(wp&0xfff0)==SC_MINIMIZE))return 0;break;
            case WM_EXITSIZEMOVE:a->save();return 0;
            case WM_ERASEBKGND:return 1;
            case WM_PAINT:{PAINTSTRUCT p{};BeginPaint(w,&p);a->draw();EndPaint(w,&p);return 0;}
            case WM_SIZE:if(wp==SIZE_MINIMIZED){a->discardTarget();}else{if(a->target)a->target->Resize(D2D1::SizeU(LOWORD(lp),HIWORD(lp)));a->invalidate();}if(a->tooltip){RECT r{};GetClientRect(w,&r);TOOLINFOW ti{};ti.cbSize=sizeof(ti);ti.hwnd=w;ti.uId=1;ti.rect=r;SendMessageW(a->tooltip,TTM_NEWTOOLRECTW,0,reinterpret_cast<LPARAM>(&ti));}return 0;
            case WM_DPICHANGED:{a->discardTarget();auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(w,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);return 0;}
            case WM_GETMINMAXINFO:{auto m=reinterpret_cast<MINMAXINFO*>(lp);float s=GetDpiForWindow(w)/96.f;m->ptMinTrackSize={LONG(860*s),LONG(600*s)};return 0;}
            case WM_NCHITTEST:{LRESULT r=DefWindowProcW(w,msg,wp,lp);if(a->settings.locked&&a->desktop)return HTCLIENT;if(r!=HTCLIENT)return r;POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(w,&p);if(p.y/a->scale<82&&a->hit(p.x/a->scale,p.y/a->scale)<0)return HTCAPTION;return r;}
            case WM_MOUSEMOVE:a->hover(lp);return 0;
            case WM_LBUTTONUP:{int h=a->hit(GET_X_LPARAM(lp)/a->scale,GET_Y_LPARAM(lp)/a->scale);if(h>=0){auto item=a->hits[h];if(item.isEvent)MessageBoxW(w,a->details(item).c_str(),L"Event details",MB_OK|MB_ICONINFORMATION);else a->command(item.command);}return 0;}
            case WM_CONTEXTMENU:a->menu();return 0;
            case WM_COMMAND:a->command(LOWORD(wp));return 0;
            case WM_KEYDOWN:if(wp==VK_LEFT)a->command(idPrev);else if(wp==VK_RIGHT)a->command(idNext);else if(wp==VK_HOME)a->command(idToday);else if(wp==VK_F5)a->command(idSync);else if(wp==VK_ESCAPE)a->command(idHide);else if(wp=='S'&&(GetKeyState(VK_CONTROL)&0x8000))a->command(idSettings);return 0;
            case WM_TIMER:if(wp==2)a->request(true);else{
                a->minuteTimer();
                auto current=today();if(current!=a->lastDay){a->lastDay=current;if(a->followToday)a->week=monday(current);a->request(false);}a->invalidate();}return 0;
            case WM_TIMECHANGE:case WM_SETTINGCHANGE:a->minuteTimer();if(a->worker)a->request(false);return DefWindowProcW(w,msg,wp,lp);
            case dataMessage:{std::unique_ptr<Answer> answer(reinterpret_cast<Answer*>(lp));if(answer->generation!=a->generation)return 0;if(answer->replace){a->calendar=std::move(answer->calendar);a->hits.clear();a->segments=layoutEvents(a->calendar.events,a->week);}a->sourceStates=std::move(answer->states);a->updateSourceStatus();a->status=std::move(answer->status);a->hovered=-1;a->invalidate();return 0;}
            case trayMessage:if(lp==WM_RBUTTONUP||lp==WM_CONTEXTMENU)a->menu();else if(lp==WM_LBUTTONDBLCLK)a->command(idToday);return 0;
            case WM_CLOSE:if(!wp&&a->settings.closeToTray){a->command(idHide);return 0;}if(a->closing)return 0;a->closing=true;a->hideWindow();if(a->settingsWindow)DestroyWindow(a->settingsWindow);a->save();if(a->worker)a->worker->stop();a->tray(false);DestroyWindow(w);return 0;
            case WM_DESTROY:{MSG pending{};while(PeekMessageW(&pending,w,dataMessage,dataMessage,PM_REMOVE))delete reinterpret_cast<Answer*>(pending.lParam);PostQuitMessage(0);return 0;}
        }return DefWindowProcW(w,msg,wp,lp);
    }
    int run(HINSTANCE inst,const std::wstring& args){
        instance=inst;portable=args.find(L"--portable")!=args.npos;demo=args.find(L"--demo")!=args.npos;
        file=dataDirectory(portable)/L"calendar-deck.db";store=std::make_unique<Store>(file);settings=loadSettings(*store);bool initialDesktop=settings.desktop;
        if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,&factory))||FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(&writing))))throw std::runtime_error("Direct2D/DirectWrite initialization failed");
        auto format=[&](float size,DWRITE_FONT_WEIGHT weight,IDWriteTextFormat** out){if(FAILED(writing->CreateTextFormat(L"Segoe UI",nullptr,weight,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"en-us",out)))throw std::runtime_error("Cannot create text format");(*out)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);};format(11,DWRITE_FONT_WEIGHT_NORMAL,&small);format(13,DWRITE_FONT_WEIGHT_NORMAL,&normal);format(24,DWRITE_FONT_WEIGHT_SEMI_BOLD,&heading);
        WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.lpfnWndProc=windowProc;wc.hInstance=inst;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(inst,MAKEINTRESOURCEW(IDI_CALENDAR_DECK));wc.hIconSm=static_cast<HICON>(LoadImageW(inst,MAKEINTRESOURCEW(IDI_CALENDAR_DECK),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED));wc.lpszClassName=L"CalendarDeckMain";RegisterClassExW(&wc);
        RECT saved{settings.x,settings.y,settings.x+settings.width,settings.y+settings.height};MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);GetMonitorInfoW(MonitorFromRect(&saved,MONITOR_DEFAULTTONEAREST),&monitor);auto work=monitor.rcWork;settings.width=std::min(settings.width,int(work.right-work.left));settings.height=std::min(settings.height,int(work.bottom-work.top));settings.x=std::clamp(settings.x,int(work.left),int(work.right-settings.width));settings.y=std::clamp(settings.y,int(work.top),int(work.bottom-settings.height));
        window=CreateWindowExW(WS_EX_APPWINDOW,wc.lpszClassName,L"Calendar Deck",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,settings.x,settings.y,settings.width,settings.height,nullptr,nullptr,inst,this);if(!window)throw std::runtime_error("Cannot create calendar window");
        scale=GetDpiForWindow(window)/96.f;font=CreateFontW(-int(14*scale),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,window,nullptr,instance,nullptr);
        TOOLINFOW ti{};ti.cbSize=sizeof(ti);ti.uFlags=TTF_SUBCLASS;ti.hwnd=window;ti.uId=1;GetClientRect(window,&ti.rect);ti.lpszText=const_cast<wchar_t*>(L"");SendMessageW(tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&ti));SendMessageW(tooltip,TTM_SETMAXTIPWIDTH,0,420);SendMessageW(tooltip,TTM_SETDELAYTIME,TTDT_INITIAL,500);
        taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");tray(true);worker=std::make_unique<Worker>(window,file);
        minuteTimer();syncTimer();ShowWindow(window,SW_SHOWNOACTIVATE);if(initialDesktop)mode(true);
        request(!settings.calendars.empty()&&!demo);
        MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(settingsWindow&&IsDialogMessageW(settingsWindow,&msg))continue;TranslateMessage(&msg);DispatchMessageW(&msg);}return int(msg.wParam);
    }
};
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR args,int){
    // One instance per data directory. A second launch restores the existing widget.
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);int result=1;
    HANDLE mutex=nullptr;
    try{
        bool portable=std::wstring(args).find(L"--portable")!=std::wstring::npos;auto key=L"Local\\CalendarDeck-"+dataDirectory(portable).wstring();std::replace(key.begin()+6,key.end(),L'\\',L'_');
        mutex=CreateMutexW(nullptr,FALSE,key.c_str());if(!mutex)throw std::runtime_error("Cannot create instance lock");
        if(GetLastError()==ERROR_ALREADY_EXISTS){if(auto w=FindWindowW(L"CalendarDeckMain",L"Calendar Deck")){ShowWindow(w,IsIconic(w)?SW_RESTORE:SW_SHOW);SetForegroundWindow(w);}CloseHandle(mutex);CoUninitialize();return 0;}
        App app;result=app.run(instance,args);
    }catch(const std::exception& ex){MessageBoxW(nullptr,wide(ex.what()).c_str(),L"Calendar Deck could not start",MB_OK|MB_ICONERROR);}
    CoUninitialize();if(mutex)CloseHandle(mutex);return result;
}
