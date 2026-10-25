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
enum {
    idPrev=101,idToday,idNext,idSettings,idSync,idDesktop,idFloat,idHide,idExit,idStatus,idLock,
    idViewWeek,idViewMonth,idViewYear,idJumpDate
};
enum {
    editUrl=201,editInterval,idSave,idCancel,editName,editColor,editVisible,idAdd,idRemove,
    idList,idOneSync,idGeneral,idCalendars,idAppearance,idAbout,idCloseTray,idShowUrl,
    idSourceStatus,idImport,idSourceType,idSourceHint,editLang,idAutoStart
};
enum class ViewMode { Week = 0, Month, Year };
enum class Lang { VI = 0, EN = 1 };

inline const wchar_t* tr(Lang lang, const wchar_t* vi, const wchar_t* en) {
    return (lang == Lang::EN) ? en : vi;
}
inline const char* tr(Lang lang, const char* vi, const char* en) {
    return (lang == Lang::EN) ? en : vi;
}

template<class T> void release(T*& p){if(p){p->Release();p=nullptr;}}
std::wstring text(HWND w){int n=GetWindowTextLengthW(w);std::wstring s(n+1,0);GetWindowTextW(w,s.data(),n+1);s.resize(n);return s;}
std::wstring shortDate(Stamp t){auto p=parts(t);wchar_t b[40];swprintf_s(b,L"%02u/%02u/%04d",p.d,p.m,p.y);return b;}
std::wstring clockText(Stamp t){auto p=parts(t);wchar_t b[20];swprintf_s(b,L"%02d:%02d",p.hour,p.minute);return b;}
std::wstring checkedText(Stamp t, Lang lang){
    if(!t)return tr(lang, L"Chưa đồng bộ", L"Never synced");
    auto l=toLocal(t);return shortDate(l)+L" "+clockText(l);
}

static const wchar_t* vnDayNamesShort[] = { L"T2", L"T3", L"T4", L"T5", L"T6", L"T7", L"CN" };
static const wchar_t* vnDayNamesFull[]  = { L"Thứ 2", L"Thứ 3", L"Thứ 4", L"Thứ 5", L"Thứ 6", L"Thứ 7", L"Chủ nhật" };
static const wchar_t* enDayNamesShort[] = { L"MON", L"TUE", L"WED", L"THU", L"FRI", L"SAT", L"SUN" };
static const wchar_t* enDayNamesFull[]  = { L"Monday", L"Tuesday", L"Wednesday", L"Thursday", L"Friday", L"Saturday", L"Sunday" };

static const wchar_t* vnMonthNames[]    = { L"Tháng 1", L"Tháng 2", L"Tháng 3", L"Tháng 4", L"Tháng 5", L"Tháng 6",
                                            L"Tháng 7", L"Tháng 8", L"Tháng 9", L"Tháng 10", L"Tháng 11", L"Tháng 12" };
static const wchar_t* enMonthNames[]    = { L"January", L"February", L"March", L"April", L"May", L"June",
                                            L"July", L"August", L"September", L"October", L"November", L"December" };

bool isAutoStartEnabled(){
    HKEY hKey=nullptr;
    if(RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS){
        DWORD type=0, size=0;
        LONG res = RegQueryValueExW(hKey, L"CalendarDeck", nullptr, &type, nullptr, &size);
        RegCloseKey(hKey);
        return (res == ERROR_SUCCESS);
    }
    return false;
}

void setAutoStartEnabled(bool enable, bool portable){
    HKEY hKey=nullptr;
    if(RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS){
        if(enable){
            wchar_t exePath[MAX_PATH]{};
            GetModuleFileNameW(nullptr, exePath, MAX_PATH);
            std::wstring cmd = L"\"" + std::wstring(exePath) + L"\"";
            if(portable) cmd += L" --portable";
            RegSetValueExW(hKey, L"CalendarDeck", 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()), DWORD((cmd.size() + 1) * sizeof(wchar_t)));
        }else{
            RegDeleteValueW(hKey, L"CalendarDeck");
        }
        RegCloseKey(hKey);
    }
}

struct Job{Settings settings;Stamp rangeStart{},rangeEnd{};Stamp week{};Lang lang{Lang::VI};uint64_t generation{};bool refresh{},demo{};std::string only;};
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
                if(j.demo){
                    a->calendar=expandCalendarRange(demoCalendar(j.rangeStart),j.rangeStart,j.rangeEnd);
                    a->status=tr(j.lang, L"DEMO · sự kiện mẫu · kết nối lịch của bạn trong Cài đặt", L"DEMO · sample events · connect your calendar in Settings");
                }else {
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
                                parsed=expandCalendarRange(fresh.body,j.rangeStart,j.rangeEnd);saveFeed(store,fresh);f=std::move(fresh);error.clear();saveSyncStatus(store,source.url,{});
                            }else if(!f.body.empty())parsed=expandCalendarRange(f.body,j.rangeStart,j.rangeEnd);
                        }catch(const std::exception& ex){error=wide(ex.what());try{saveSyncStatus(store,source.url,utf8(error));}catch(...){}
                            if(!f.body.empty())try{parsed=expandCalendarRange(f.body,j.rangeStart,j.rangeEnd);}catch(...){}
                        }
                        if(!error.empty())++errors;
                        a->states[source.url]=offlineCalendar(source)
                            ?(error.empty()?tr(j.lang,L"Tệp offline | Đã nhập: ",L"Offline file | Imported: "):tr(j.lang,L"Lỗi tệp: ",L"File error: ")+error+L" | ")+checkedText(f.checked, j.lang)
                            :(error.empty()?tr(j.lang,L"Đồng bộ gần nhất: ",L"Last synced: "):tr(j.lang,L"Ngoại tuyến: ",L"Offline: ")+error+L" | ")+checkedText(f.checked, j.lang);
                        if(source.visible){++visible;for(auto& e:parsed.events){e.color=source.color;e.calendarName=source.name;a->calendar.events.push_back(std::move(e));}}
                        for(auto& warning:parsed.warnings)a->calendar.warnings.push_back(source.name+": "+warning);
                    }
                    a->status=(j.refresh&&pass==0?(j.lang==Lang::VI?L"Đang kiểm tra lịch | ":L"Checking calendars | "):L"")+
                              std::to_wstring(visible)+tr(j.lang,L" lịch hiển thị | ",L" visible calendars | ")+
                              std::to_wstring(errors)+tr(j.lang,L" lỗi đồng bộ | Bấm để xem chi tiết",L" sync errors | Click for details");
                }
            }catch(const std::exception& ex){
                a->status=tr(j.lang, L"Lỗi lịch · ", L"Calendar error · ")+wide(ex.what());a->replace=false;
            }
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

struct Hit {
    D2D1_RECT_F rect;
    int command{};
    size_t event{};
    bool isEvent{};
    int allDay{-1};
    Stamp targetDate{};
    ViewMode targetMode{ViewMode::Week};
    std::wstring tooltip;
};

std::map<Stamp,std::vector<size_t>> indexEventsByDay(const std::vector<Occurrence>& events,Stamp rangeStart,Stamp rangeEnd) {
    std::map<Stamp,std::vector<size_t>> dayMap;
    for(size_t i=0;i<events.size();++i){
        auto& e=events[i];
        Stamp s=std::max(rangeStart,(e.start/daySeconds)*daySeconds);
        Stamp finish=(e.end>e.start)?((e.end-1)/daySeconds)*daySeconds:s;
        finish=std::min(rangeEnd-daySeconds,finish);
        for(Stamp d=s;d<=finish;d+=daySeconds){
            dayMap[d].push_back(i);
        }
    }
    return dayMap;
}

struct App {
    HINSTANCE instance{};HWND window{},settingsWindow{},tooltip{};HFONT font{};UINT taskbarCreated{};
    std::filesystem::path file;Settings settings;std::unique_ptr<Store> store;std::unique_ptr<Worker> worker;
    ID2D1Factory* factory{};IDWriteFactory* writing{};ID2D1HwndRenderTarget* target{};ID2D1SolidColorBrush* brush{};
    IDWriteTextFormat *small{},*smallBold{},*normal{},*normalBold{},*heading{},*title{};
    IDWriteTextFormat *centerSmall{},*centerBold{},*centerMini{};
    CalendarResult calendar;std::vector<Segment> segments;std::vector<Hit> hits;

    ViewMode viewMode{ViewMode::Week};
    Lang language{Lang::VI};
    Stamp currentDate{today()};
    Stamp week{monday(today())};
    Stamp month{monthStart(today())};
    Stamp year{yearStart(today())};
    Stamp lastDay{today()};
    Stamp requestedStart{},requestedEnd{};
    std::string requestedUrl;bool requestedDemo{};
    uint64_t generation{};bool demo{},portable{},desktop{},closing{},followToday{true};
    std::wstring status=L"Loading calendar…",tooltipText;
    int hovered{-1};float scale{1},width{},height{};
    float mouseX{-1},mouseY{-1};

    ~App(){
        if(worker)worker->stop();
        release(brush);release(target);
        release(small);release(smallBold);release(normal);release(normalBold);release(heading);release(title);
        release(centerSmall);release(centerBold);release(centerMini);
        release(writing);release(factory);
        if(font) { DeleteObject(font); }
        if(settingsHeading) { DeleteObject(settingsHeading); }
    }

    std::map<std::string,std::wstring> sourceStates;
    int settingsPage{1},selected{-1};
    HFONT settingsHeading{};
    bool editorDirty{},buildingSettings{};
    bool discardEditor(){return !editorDirty||MessageBoxW(settingsWindow,tr(language, L"Hủy các thay đổi chưa lưu?", L"Discard your unsaved changes?"),tr(language, L"Thay đổi chưa lưu", L"Unsaved changes"),MB_YESNO|MB_ICONQUESTION)==IDYES;}

    std::pair<Stamp,Stamp> getActiveRange() const {
        if(viewMode==ViewMode::Week){
            return {week,week+7*daySeconds};
        }else if(viewMode==ViewMode::Month){
            Stamp gridStart=monday(month);
            return {gridStart,gridStart+42*daySeconds};
        }else{
            auto yp=parts(year);
            return {civil(yp.y,1,1),civil(yp.y+1,1,1)};
        }
    }

    void request(bool refresh,std::string only={}){
        ++generation;
        auto [rStart,rEnd]=getActiveRange();
        if(rStart!=requestedStart||rEnd!=requestedEnd||settings.url!=requestedUrl||demo!=requestedDemo){
            calendar={};segments.clear();hits.clear();hovered=-1;
        }
        requestedStart=rStart;requestedEnd=rEnd;requestedUrl=settings.url;requestedDemo=demo;
        status=refresh?tr(language, L"Đang cập nhật lịch…", L"Checking calendars…"):tr(language, L"Đang tải lịch…", L"Loading calendar…");
        worker->submit({settings,rStart,rEnd,week,language,generation,refresh,demo,std::move(only)});
        InvalidateRect(window,nullptr,FALSE);
    }

    void syncTimer(){KillTimer(window,2);if(std::any_of(settings.calendars.begin(),settings.calendars.end(),[](const auto& source){return !offlineCalendar(source);})&&!demo)SetTimer(window,2,UINT(settings.intervalMinutes)*60000,nullptr);}
    void minuteTimer(){SYSTEMTIME now{};GetLocalTime(&now);SetTimer(window,1,60000-now.wSecond*1000-now.wMilliseconds,nullptr);}
    void save(){
        if(!IsIconic(window)&&!IsZoomed(window)){
            RECT r{};GetWindowRect(window,&r);
            settings.x=r.left;settings.y=r.top;settings.width=r.right-r.left;settings.height=r.bottom-r.top;
        }
        settings.desktop=desktop;
        try{saveSettings(*store,settings);}catch(const std::exception& ex){status=tr(language, L"Không thể lưu cấu hình · ", L"Settings could not be saved · ")+wide(ex.what());}
    }

    void tray(bool add){
        NOTIFYICONDATAW n{};n.cbSize=sizeof(n);n.hWnd=window;n.uID=1;n.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;n.uCallbackMessage=trayMessage;
        n.hIcon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(IDI_CALENDAR_DECK),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED));
        wcscpy_s(n.szTip,tr(language, L"Calendar Deck · nhấp chuột phải để xem tùy chọn", L"Calendar Deck · right-click for options"));
        Shell_NotifyIconW(add?NIM_ADD:NIM_DELETE,&n);
    }
    void invalidate(){if(IsWindowVisible(window)&&!IsIconic(window))InvalidateRect(window,nullptr,FALSE);}
    
    static void refreshDesktopArea(const RECT* pArea = nullptr){
        RedrawWindow(nullptr, pArea, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        EnumWindows([](HWND top, LPARAM lp) -> BOOL {
            wchar_t cls[64]{};
            GetClassNameW(top, cls, 64);
            if (wcscmp(cls, L"WorkerW") == 0 || wcscmp(cls, L"Progman") == 0) {
                auto area = reinterpret_cast<const RECT*>(lp);
                if (area) {
                    RECT localArea = *area;
                    MapWindowPoints(nullptr, top, reinterpret_cast<POINT*>(&localArea), 2);
                    RedrawWindow(top, &localArea, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
                } else {
                    RedrawWindow(top, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
                }
                HWND defView = FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr);
                if (defView) {
                    if (area) {
                        RECT dArea = *area;
                        MapWindowPoints(nullptr, defView, reinterpret_cast<POINT*>(&dArea), 2);
                        RedrawWindow(defView, &dArea, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
                    } else {
                        RedrawWindow(defView, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
                    }
                }
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(pArea));
        SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, nullptr, SPIF_SENDWININICHANGE);
    }

    void hideWindow(){
        RECT screenArea{};
        GetWindowRect(window, &screenArea);
        bool wasDesktop = desktop || (GetParent(window) != nullptr);
        ShowWindow(window, SW_HIDE);
        discardTarget();
        if (wasDesktop) {
            refreshDesktopArea(&screenArea);
        }
    }
    void discardTarget(){release(brush);release(target);}
    bool graphics(){
        if(target)return true;
        RECT rc{};GetClientRect(window,&rc);UINT dpi=GetDpiForWindow(window);scale=dpi/96.f;
        HRESULT hr=factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),D2D1::HwndRenderTargetProperties(window,D2D1::SizeU(rc.right,rc.bottom),D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS),&target);
        if(FAILED(hr))return false;
        target->SetDpi(float(dpi),float(dpi));
        if(FAILED(target->CreateSolidColorBrush(D2D1::ColorF(0xffffff),&brush))){discardTarget();return false;}
        return true;
    }

    void color(UINT32 rgb,float opacity=1.0f){brush->SetColor(D2D1::ColorF(rgb,opacity));}
    void fill(D2D1_RECT_F r,UINT32 c,float radius=0,float opacity=1.0f){
        color(c,opacity);
        if(radius>0)target->FillRoundedRectangle(D2D1::RoundedRect(r,radius,radius),brush);
        else target->FillRectangle(r,brush);
    }
    void stroke(D2D1_RECT_F r,UINT32 c,float thickness=1.0f,float radius=0,float opacity=1.0f){
        color(c,opacity);
        if(radius>0)target->DrawRoundedRectangle(D2D1::RoundedRect(r,radius,radius),brush,thickness);
        else target->DrawRectangle(r,brush,thickness);
    }
    void circle(float cx,float cy,float radius,UINT32 fillColor,float opacity=1.0f){
        color(fillColor,opacity);
        target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx,cy),radius,radius),brush);
    }
    void line(float x,float y,float ex,float ey,UINT32 c,float thickness=1.0f,float opacity=1.0f){
        color(c,opacity);
        target->DrawLine(D2D1::Point2F(x,y),D2D1::Point2F(ex,ey),brush,thickness);
    }
    void label(const std::wstring& s,D2D1_RECT_F r,IDWriteTextFormat* format,UINT32 c=0xe7edf7,float opacity=1.0f){
        color(c,opacity);
        target->DrawText(s.c_str(),UINT32(s.size()),format,r,brush,D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    void button(const wchar_t* btnText,D2D1_RECT_F r,int command,bool active=false,float radius=14.0f,const wchar_t* tip=L""){
        bool isHover=(mouseX>=r.left&&mouseX<=r.right&&mouseY>=r.top&&mouseY<=r.bottom);
        UINT32 bg=active?0x2563eb:(isHover?0x223247:0x162232);
        UINT32 border=active?0x60a5fa:(isHover?0x3b82f6:0x22354c);
        UINT32 fg=active?0xffffff:(isHover?0xffffff:0xdbe4f0);
        fill(r,bg,radius);
        stroke(r,border,1.0f,radius);
        label(btnText,r,centerBold,fg);
        hits.push_back({r,command,0,false,-1,0,ViewMode::Week,tip});
    }

    void draw(){
        if(!graphics())return;
        auto size=target->GetSize();width=size.width;height=size.height;hits.clear();
        target->BeginDraw();
        target->Clear(D2D1::ColorF(0x0c111a));

        // 1. Top Bar Container
        D2D1_RECT_F topBar={16,12,width-16,68};
        fill(topBar,0x121924,16);
        stroke(topBar,0x1f2b3e,1.0f,16);

        // App Title
        label(L"Calendar Deck",{32,18,220,42},heading,0xffffff);

        // Subtitle / Active period
        std::wstring periodStr;
        if(viewMode==ViewMode::Week){
            periodStr=shortDate(week)+L" — "+shortDate(week+6*daySeconds);
        }else if(viewMode==ViewMode::Month){
            auto mp=parts(month);
            periodStr=(language==Lang::EN?enMonthNames[mp.m-1]:vnMonthNames[mp.m-1])+(language==Lang::EN?L" "+std::to_wstring(mp.y):L", "+std::to_wstring(mp.y));
        }else{
            auto yp=parts(year);
            periodStr=tr(language, L"Năm ", L"Year ")+std::to_wstring(yp.y);
        }
        std::wstring subStr=periodStr+L"  ·  "+std::to_wstring(settings.calendars.size())+L" "+tr(language, L"lịch kết nối", L"calendars connected");
        label(subStr,{32,44,380,62},small,0x94a3b8);

        // Center: Segmented View Switcher Pill [ Tuần | Tháng | Năm ]
        float segW=252.0f;
        float segX=(width-segW)/2.0f;
        D2D1_RECT_F segRect={segX,20,segX+segW,58};
        fill(segRect,0x0f1520,18);
        stroke(segRect,0x223247,1.0f,18);

        struct SegItem { const wchar_t* name; ViewMode mode; int cmd; };
        SegItem segs[3]={
            {tr(language, L"Tuần", L"Week"),ViewMode::Week,idViewWeek},
            {tr(language, L"Tháng", L"Month"),ViewMode::Month,idViewMonth},
            {tr(language, L"Năm", L"Year"),ViewMode::Year,idViewYear}
        };
        float itemW=(segW-8.0f)/3.0f;
        for(int i=0;i<3;++i){
            D2D1_RECT_F itR={segX+4+i*itemW,23,segX+4+(i+1)*itemW-2,55};
            bool isAct=(viewMode==segs[i].mode);
            bool isHov=(mouseX>=itR.left&&mouseX<=itR.right&&mouseY>=itR.top&&mouseY<=itR.bottom);
            if(isAct){
                fill(itR,0x2563eb,14);
                stroke(itR,0x60a5fa,1.0f,14);
                label(segs[i].name,itR,centerBold,0xffffff);
            }else{
                if(isHov)fill(itR,0x1b283b,14);
                label(segs[i].name,itR,centerSmall,isHov?0xffffff:0x94a3b8);
            }
            hits.push_back({itR,segs[i].cmd,0,false,-1,0,segs[i].mode,L""});
        }

        // Right: Navigation and Action Buttons
        float rX=width-28;
        button(tr(language, L"Cài đặt", L"Settings"),{rX-76,23,rX,57},idSettings,false,14.0f,tr(language, L"Cài đặt lịch và nguồn dữ liệu", L"Configure calendar sources"));
        rX-=84;
        button(tr(language, L"Đồng bộ", L"Sync"),{rX-80,23,rX,57},idSync,false,14.0f,tr(language, L"Đồng bộ các lịch kết nối", L"Sync connected calendars"));
        rX-=88;
        button(L"›",{rX-38,23,rX,57},idNext,false,14.0f,tr(language, L"Tiếp theo", L"Next period"));
        rX-=44;
        button(tr(language, L"Hôm nay", L"Today"),{rX-78,23,rX,57},idToday,false,14.0f,tr(language, L"Quay về hôm nay", L"Jump to today"));
        rX-=84;
        button(L"‹",{rX-38,23,rX,57},idPrev,false,14.0f,tr(language, L"Trước đó", L"Previous period"));

        // 2. Main Content Card Container
        D2D1_RECT_F contentRect={16,78,width-16,height-48};
        fill(contentRect,0x101722,16);
        stroke(contentRect,0x1c2839,1.0f,16);

        static const UINT32 cards[]={0x1a3350,0x2e234e,0x143b2f,0x421d33,0x3e3215};
        static const UINT32 accents[]={0x38bdf8,0xa78bfa,0x34d399,0xf472b6,0xfbbf24};

        if(viewMode==ViewMode::Week){
            float leftMargin=62.0f;
            float dayW=(contentRect.right-contentRect.left-leftMargin-16.0f)/7.0f;
            float headTop=contentRect.top+10.0f;
            float headBottom=headTop+42.0f;

            bool hasAllDay=false;
            for(auto& e:calendar.events){if(e.allDay&&e.start<week+7*daySeconds&&e.end>week){hasAllDay=true;break;}}

            float allDayBottom=hasAllDay?headBottom+38.0f:headBottom;
            float gridTop=allDayBottom+8.0f;
            float gridBottom=contentRect.bottom-12.0f;
            float unit=(gridBottom-gridTop)/24.0f;
            auto current=today();

            for(int day=0;day<7;++day){
                float x=contentRect.left+leftMargin+day*dayW;
                Stamp dStamp=week+day*daySeconds;
                bool isToday=(dStamp==current);
                auto p=parts(dStamp);
                const wchar_t* dName = language==Lang::EN?enDayNamesShort[day]:vnDayNamesShort[day];

                D2D1_RECT_F hRect={x+2,headTop,x+dayW-2,headBottom};
                if(isToday){
                    fill(hRect,0x2563eb,12);
                    stroke(hRect,0x60a5fa,1.2f,12);
                    label(dName,{hRect.left,hRect.top+3,hRect.right,hRect.top+18},centerMini,0xbfdbfe);
                    label(std::to_wstring(p.d),{hRect.left,hRect.top+17,hRect.right,hRect.bottom-3},centerBold,0xffffff);
                }else{
                    fill(hRect,0x141e2b,12);
                    stroke(hRect,0x1f2e42,1.0f,12);
                    label(dName,{hRect.left,hRect.top+3,hRect.right,hRect.top+18},centerMini,0x94a3b8);
                    label(std::to_wstring(p.d),{hRect.left,hRect.top+17,hRect.right,hRect.bottom-3},centerBold,0xe2e8f0);
                }

                if(hasAllDay){
                    std::vector<size_t> all;
                    for(size_t i=0;i<calendar.events.size();++i){
                        auto& e=calendar.events[i];
                        if(e.allDay&&e.start<week+(day+1)*daySeconds&&e.end>week+day*daySeconds)all.push_back(i);
                    }
                    if(!all.empty()){
                        D2D1_RECT_F ar={x+2,headBottom+5,x+dayW-2,headBottom+33};
                        fill(ar,0x172c23,8);
                        stroke(ar,0x34d399,1.0f,8);
                        size_t hash=size_t(std::clamp(calendar.events[all.front()].color,0,4));
                        fill({ar.left+2,ar.top+3,ar.left+5,ar.bottom-3},accents[hash],2);
                        auto s=wide(calendar.events[all.front()].title);
                        if(all.size()>1)s+=L"  +"+std::to_wstring(all.size()-1);
                        label(s,{ar.left+8,ar.top+4,ar.right-4,ar.bottom},small,0x6ee7b7);
                        hits.push_back({ar,0,all.front(),true,day,0,ViewMode::Week,L""});
                    }
                }

                if(isToday){
                    fill({x+1,gridTop,x+dayW-1,gridBottom},0x18283c,8,0.45f);
                }
                line(x,gridTop,x,gridBottom,0x162232);
            }

            for(int hour=0;hour<=24;++hour){
                float y=gridTop+hour*unit;
                line(contentRect.left+leftMargin,y,contentRect.right-12,y,0x162232);
                wchar_t b[10];swprintf_s(b,L"%02d:00",hour);
                label(b,{contentRect.left+8,y-7,contentRect.left+leftMargin-4,y+14},small,0x64748b);
            }

            for(auto& s:segments){
                auto& e=calendar.events[s.event];
                float x=contentRect.left+leftMargin+s.day*dayW;
                float col=(dayW-8)/s.columns;
                float top=gridTop+float(s.start-week-s.day*daySeconds)/3600.f*unit;
                float bottom=gridTop+float(s.end-week-s.day*daySeconds)/3600.f*unit;
                D2D1_RECT_F r={x+4+s.column*col,top+1,x+4+(s.column+1)*col-3,std::min(gridBottom,bottom-1)};
                if(r.bottom<r.top+4)r.bottom=std::min(gridBottom,r.top+4);

                size_t hash=size_t(std::clamp(e.color,0,4));
                bool isCardHover=(mouseX>=r.left&&mouseX<=r.right&&mouseY>=r.top&&mouseY<=r.bottom);
                fill(r,cards[hash],8);
                stroke(r,isCardHover?0xffffff:accents[hash],isCardHover?1.5f:1.0f,8);
                fill({r.left+2,r.top+4,r.left+5,r.bottom-4},accents[hash],2);

                if(r.bottom-r.top>14)label(wide(e.title),{r.left+8,r.top+3,r.right-4,r.bottom},smallBold,0xffffff);
                if(r.bottom-r.top>32)label(clockText(e.start)+L" – "+clockText(e.end),{r.left+8,r.top+18,r.right-4,r.bottom},small,0xbfdbfe);
                hits.push_back({r,0,s.event,true,-1,0,ViewMode::Week,L""});
            }

            SYSTEMTIME st{};GetLocalTime(&st);
            if(current>=week&&current<week+7*daySeconds){
                int day=int((current-week)/daySeconds);
                float y=gridTop+(st.wHour+st.wMinute/60.f)*unit;
                float x=contentRect.left+leftMargin+day*dayW;
                line(x,y,x+dayW,y,0xf43f5e,2.0f);
                circle(x,y,4.0f,0xf43f5e);
                wchar_t timeBuf[12];swprintf_s(timeBuf,L"%02d:%02d",st.wHour,st.wMinute);
                D2D1_RECT_F timeBadge={x-36,y-9,x-4,y+9};
                fill(timeBadge,0xf43f5e,6);
                label(timeBuf,timeBadge,centerMini,0xffffff);
            }

            if(calendar.events.empty()&&status.find(L"Loading")==std::wstring::npos&&status.find(L"Checking")==std::wstring::npos&&status.find(L"Đang")==std::wstring::npos){
                D2D1_RECT_F emptyCard={contentRect.left+leftMargin+28,gridTop+24,contentRect.right-32,gridTop+72};
                fill(emptyCard,0x162232,10);
                stroke(emptyCard,0x24354a,1.0f,10);
                label(settings.calendars.empty()?tr(language, L"Tuần làm việc của bạn. Thêm Google Calendar hoặc tệp .ics trong Cài đặt.", L"Your week, at a glance. Add your Google Calendar or .ics file in Settings."):tr(language, L"Không có sự kiện nào trong tuần này.", L"No events scheduled for this week."),
                      {emptyCard.left+20,emptyCard.top+14,emptyCard.right-20,emptyCard.bottom},normalBold,0xa8bdd3);
            }
        }else if(viewMode==ViewMode::Month){
            float mLeft=contentRect.left+16.0f;
            float mRight=contentRect.right-16.0f;
            float mColW=(mRight-mLeft)/7.0f;
            float headTop=contentRect.top+10.0f;
            float headBottom=headTop+28.0f;

            for(int wd=0;wd<7;++wd){
                D2D1_RECT_F wR={mLeft+wd*mColW,headTop,mLeft+(wd+1)*mColW,headBottom};
                label(language==Lang::EN?enDayNamesFull[wd]:vnDayNamesFull[wd],wR,centerMini,0x94a3b8);
            }

            float mTop=headBottom+6.0f;
            float mBottom=contentRect.bottom-12.0f;
            float mRowH=(mBottom-mTop)/6.0f;

            Stamp gridStart=monday(month);
            auto dayEvents=indexEventsByDay(calendar.events,gridStart,gridStart+42*daySeconds);
            auto mp=parts(month);
            auto current=today();

            for(int row=0;row<6;++row){
                for(int col=0;col<7;++col){
                    int idx=row*7+col;
                    Stamp cellStamp=gridStart+idx*daySeconds;
                    auto cp=parts(cellStamp);
                    bool inMonth=(int(cp.m)==int(mp.m)&&cp.y==mp.y);
                    bool isToday=(cellStamp==current);

                    D2D1_RECT_F cellRect={mLeft+col*mColW+2,mTop+row*mRowH+2,mLeft+(col+1)*mColW-2,mTop+(row+1)*mRowH-2};
                    bool isCellHover=(mouseX>=cellRect.left&&mouseX<=cellRect.right&&mouseY>=cellRect.top&&mouseY<=cellRect.bottom);

                    UINT32 bg=isToday?0x18273b:(inMonth?(isCellHover?0x182333:0x131c28):0x0c121b);
                    UINT32 border=isToday?0x3b82f6:(isCellHover?0x38bdf8:(inMonth?0x1f2e42:0x151f2b));
                    fill(cellRect,bg,10);
                    stroke(cellRect,border,isToday?1.5f:1.0f,10);

                    if(isToday){
                        D2D1_RECT_F dBadge={cellRect.left+5,cellRect.top+5,cellRect.left+27,cellRect.top+23};
                        fill(dBadge,0x2563eb,9);
                        label(std::to_wstring(cp.d),dBadge,centerBold,0xffffff);
                    }else{
                        label(std::to_wstring(cp.d),{cellRect.left+8,cellRect.top+5,cellRect.left+35,cellRect.top+23},inMonth?normalBold:normal,inMonth?0xe2e8f0:0x475569);
                    }

                    auto it=dayEvents.find(cellStamp);
                    if(it!=dayEvents.end()&&!it->second.empty()){
                        auto& evList=it->second;
                        int maxChips=std::max(1,int((mRowH-30)/20));
                        int showCount=std::min(int(evList.size()),maxChips);
                        bool hasMore=int(evList.size())>maxChips;
                        if(hasMore)showCount=maxChips-1;

                        for(int k=0;k<showCount;++k){
                            auto& ev=calendar.events[evList[k]];
                            float chipTop=cellRect.top+26+k*20;
                            D2D1_RECT_F chipRect={cellRect.left+5,chipTop,cellRect.right-5,chipTop+18};
                            size_t hash=size_t(std::clamp(ev.color,0,4));
                            bool isChipHover=(mouseX>=chipRect.left&&mouseX<=chipRect.right&&mouseY>=chipRect.top&&mouseY<=chipRect.bottom);

                            fill(chipRect,cards[hash],5);
                            stroke(chipRect,isChipHover?0xffffff:accents[hash],0.8f,5);
                            circle(chipRect.left+7,chipTop+9,2.5f,accents[hash]);

                            std::wstring evTitle=(ev.allDay?L"":clockText(ev.start)+L" ")+wide(ev.title);
                            label(evTitle,{chipRect.left+13,chipRect.top+1,chipRect.right-4,chipRect.bottom},small,0xf1f5f9);
                            hits.push_back({chipRect,0,evList[k],true,-1,0,ViewMode::Week,L""});
                        }

                        if(hasMore){
                            float moreTop=cellRect.top+26+showCount*20;
                            D2D1_RECT_F moreRect={cellRect.left+5,moreTop,cellRect.right-5,moreTop+18};
                            int moreN=int(evList.size())-showCount;
                            fill(moreRect,0x1e2a3c,5);
                            stroke(moreRect,0x2c3e55,0.8f,5);
                            std::wstring moreStr=L"+"+std::to_wstring(moreN)+L" "+tr(language, L"sự kiện", L"more");
                            label(moreStr,{moreRect.left+8,moreRect.top+1,moreRect.right-4,moreRect.bottom},centerMini,0x93c5fd);
                            hits.push_back({moreRect,idJumpDate,0,false,-1,cellStamp,ViewMode::Week,tr(language, L"Xem tuần này", L"View this week")});
                        }
                    }

                    hits.push_back({cellRect,idJumpDate,0,false,-1,cellStamp,ViewMode::Week,tr(language, L"Bấm để xem tuần", L"Click to view week")});
                }
            }
        }else{
            // ViewMode::Year
            float yLeft=contentRect.left+16.0f;
            float yRight=contentRect.right-16.0f;
            float yTop=contentRect.top+12.0f;
            float yBottom=contentRect.bottom-12.0f;
            float gap=12.0f;
            float cardW=(yRight-yLeft-3*gap)/4.0f;
            float cardH=(yBottom-yTop-2*gap)/3.0f;

            auto yp=parts(year);
            auto yearEvents=indexEventsByDay(calendar.events,civil(yp.y,1,1),civil(yp.y+1,1,1));
            auto current=today();

            for(int m=1;m<=12;++m){
                int col=(m-1)%4;
                int row=(m-1)/4;
                D2D1_RECT_F cardRect={yLeft+col*(cardW+gap),yTop+row*(cardH+gap),yLeft+col*(cardW+gap)+cardW,yTop+row*(cardH+gap)+cardH};
                bool isCardHover=(mouseX>=cardRect.left&&mouseX<=cardRect.right&&mouseY>=cardRect.top&&mouseY<=cardRect.bottom);

                fill(cardRect,isCardHover?0x162232:0x121a26,14);
                stroke(cardRect,isCardHover?0x38bdf8:0x1f2e42,isCardHover?1.5f:1.0f,14);

                label(language==Lang::EN?enMonthNames[m-1]:vnMonthNames[m-1],{cardRect.left+12,cardRect.top+7,cardRect.left+120,cardRect.top+27},normalBold,0xffffff);

                int mCount=0;
                for(int d=1;d<=monthLength(yp.y,m);++d){
                    auto it=yearEvents.find(civil(yp.y,m,d));
                    if(it!=yearEvents.end())mCount+=int(it->second.size());
                }
                if(mCount>0){
                    D2D1_RECT_F mBadge={cardRect.right-76,cardRect.top+7,cardRect.right-10,cardRect.top+24};
                    fill(mBadge,0x1e3a5f,8);
                    label(std::to_wstring(mCount)+L" "+tr(language, L"sự kiện", L"events"),mBadge,centerMini,0x93c5fd);
                }

                float miniColW=(cardW-16.0f)/7.0f;
                float miniHeadTop=cardRect.top+30.0f;
                for(int wd=0;wd<7;++wd){
                    label(language==Lang::EN?enDayNamesShort[wd]:vnDayNamesShort[wd],{cardRect.left+8+wd*miniColW,miniHeadTop,cardRect.left+8+(wd+1)*miniColW,miniHeadTop+14},centerMini,0x64748b);
                }

                int mDays=monthLength(yp.y,m);
                int startWd=parts(civil(yp.y,m,1)).weekday;
                float miniGridTop=miniHeadTop+16.0f;
                float miniRowH=(cardH-52.0f)/6.0f;

                for(int d=1;d<=mDays;++d){
                    int cellIdx=startWd+d-1;
                    int cCol=cellIdx%7;
                    int cRow=cellIdx/7;
                    Stamp dStamp=civil(yp.y,m,d);
                    bool isToday=(dStamp==current);
                    auto it=yearEvents.find(dStamp);
                    bool hasEvents=(it!=yearEvents.end()&&!it->second.empty());

                    D2D1_RECT_F dRect={cardRect.left+8+cCol*miniColW,miniGridTop+cRow*miniRowH,cardRect.left+8+(cCol+1)*miniColW,miniGridTop+(cRow+1)*miniRowH};

                    if(isToday){
                        fill(dRect,0x2563eb,6);
                        label(std::to_wstring(d),dRect,centerMini,0xffffff);
                    }else if(hasEvents){
                        label(std::to_wstring(d),{dRect.left,dRect.top,dRect.right,dRect.bottom-4},centerMini,0x38bdf8);
                        circle((dRect.left+dRect.right)/2.0f,dRect.bottom-2.0f,1.8f,0x38bdf8);
                    }else{
                        label(std::to_wstring(d),dRect,centerMini,0x94a3b8);
                    }
                }

                hits.push_back({cardRect,idJumpDate,0,false,-1,civil(yp.y,m,1),ViewMode::Month,tr(language, L"Xem tháng này", L"View this month")});
            }
        }

        // 3. Bottom Status Dock Bar
        D2D1_RECT_F bottomBar={16,height-42,width-16,height-12};
        fill(bottomBar,0x121924,15);
        stroke(bottomBar,0x1f2b3e,1.0f,15);

        D2D1_RECT_F modeBadge={bottomBar.left+8,bottomBar.top+4,bottomBar.left+95,bottomBar.bottom-4};
        fill(modeBadge,0x182436,8);
        label(desktop?L"DESKTOP":L"FLOATING",modeBadge,centerMini,0x38bdf8);

        UINT32 statusColor=calendar.warnings.empty()?0x10b981:0xf59e0b;
        circle(bottomBar.left+112,(bottomBar.top+bottomBar.bottom)/2.0f,3.5f,statusColor);

        label(status,{bottomBar.left+124,bottomBar.top+4,width-280,bottomBar.bottom},small,calendar.warnings.empty()?0x94a3b8:0xf59e0b);
        hits.push_back({{bottomBar.left+100,bottomBar.top,width-280,bottomBar.bottom},idStatus,0,false,-1,0,ViewMode::Week,tr(language, L"Xem trạng thái chi tiết", L"View status details")});

        label(tr(language, L"Ctrl+1: Tuần  ·  Ctrl+2: Tháng  ·  Ctrl+3: Năm", L"Ctrl+1: Week  ·  Ctrl+2: Month  ·  Ctrl+3: Year"),
              {width-280,bottomBar.top+4,width-28,bottomBar.bottom},centerMini,0x64748b);

        HRESULT hr=target->EndDraw();
        if(hr==HRESULT(D2DERR_RECREATE_TARGET))discardTarget();
    }

    int hit(float x,float y){
        for(int i=int(hits.size())-1;i>=0;--i){
            auto& r=hits[i].rect;
            if(x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom)return i;
        }
        return -1;
    }

    std::wstring eventDetails(size_t index){
        auto& e=calendar.events[index];
        std::wstring s=wide(e.title)+L"\n"+wide(e.calendarName)+L"\n";
        s+=e.allDay?shortDate(e.start)+L" · "+tr(language, L"Cả ngày", L"All day"):shortDate(e.start)+L"  "+clockText(e.start)+L" — "+shortDate(e.end)+L"  "+clockText(e.end);
        if(!e.location.empty())s+=L"\n\n"+wide(e.location);
        if(!e.description.empty())s+=L"\n\n"+wide(e.description).substr(0,8000);
        return s;
    }

    std::wstring details(const Hit& hit){
        if(hit.allDay<0)return eventDetails(hit.event);
        Stamp day=week+hit.allDay*daySeconds;
        std::wstring s=shortDate(day)+L" · "+tr(language, L"Sự kiện cả ngày", L"All-day events");
        for(size_t i=0;i<calendar.events.size();++i){
            auto& e=calendar.events[i];
            if(e.allDay&&e.start<day+daySeconds&&e.end>day){
                s+=L"\n\n"+eventDetails(i);
                if(s.size()>50000){s.resize(50000);s+=L"\n…";break;}
            }
        }
        return s;
    }

    void hover(LPARAM p){
        float x=GET_X_LPARAM(p)/scale,y=GET_Y_LPARAM(p)/scale;
        mouseX=x;mouseY=y;
        int h=hit(x,y);
        if(h!=hovered){
            hovered=h;
            if(h>=0&&hits[h].isEvent){
                tooltipText=details(hits[h]);
            }else if(h>=0&&!hits[h].tooltip.empty()){
                tooltipText=hits[h].tooltip;
            }else{
                tooltipText=L"";
            }
            TOOLINFOW ti{};ti.cbSize=sizeof(ti);ti.hwnd=window;ti.uId=1;ti.lpszText=tooltipText.data();
            SendMessageW(tooltip,TTM_UPDATETIPTEXTW,0,reinterpret_cast<LPARAM>(&ti));
            invalidate();
        }
    }

    static BOOL CALLBACK findDesktop(HWND top,LPARAM param){
        if(FindWindowExW(top,nullptr,L"SHELLDLL_DefView",nullptr)){
            auto next=FindWindowExW(nullptr,top,L"WorkerW",nullptr);
            if(next)*reinterpret_cast<HWND*>(param)=next;
        }
        return TRUE;
    }

    bool mode(bool useDesktop){
        RECT rect{};GetWindowRect(window,&rect);
        if(!IsIconic(window)&&!IsZoomed(window)){
            settings.x=rect.left;settings.y=rect.top;settings.width=rect.right-rect.left;settings.height=rect.bottom-rect.top;
        }
        HWND parent=nullptr;
        if(useDesktop){
            auto progman=FindWindowW(L"Progman",nullptr);
            DWORD_PTR result{};
            if(progman)SendMessageTimeoutW(progman,0x052c,0,0,SMTO_ABORTIFHUNG,1000,&result);
            EnumWindows(findDesktop,reinterpret_cast<LPARAM>(&parent));
            if(!parent){status=tr(language, L"Không thể kết nối máy chủ desktop Windows · Giữ chế độ cửa sổ nổi", L"Windows desktop host is unavailable · Floating mode retained");invalidate();return false;}
        }
        if(IsZoomed(window)||IsIconic(window))ShowWindow(window,SW_RESTORE);
        bool wasDesktop = desktop || (GetParent(window) != nullptr);
        RECT oldScreenArea = rect;
        hideWindow();SetLastError(0);
        auto old=SetParent(window,parent);
        if(!old&&GetLastError()){ShowWindow(window,SW_SHOWNOACTIVATE);status=tr(language, L"Không thể gắn vào desktop · Không đổi chế độ", L"Cannot attach to the desktop · mode unchanged");return false;}
        SetWindowLongPtrW(window,GWL_STYLE,useDesktop?WS_CHILD|WS_CLIPCHILDREN|(settings.locked?0:WS_THICKFRAME):WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN);
        SetWindowLongPtrW(window,GWL_EXSTYLE,useDesktop?WS_EX_TOOLWINDOW:WS_EX_APPWINDOW);
        desktop=useDesktop;POINT point{settings.x,settings.y};if(parent)ScreenToClient(parent,&point);
        SetWindowPos(window,useDesktop?HWND_BOTTOM:HWND_NOTOPMOST,point.x,point.y,settings.width,settings.height,SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_SHOWWINDOW);
        if(wasDesktop && !useDesktop){
            refreshDesktopArea(&oldScreenArea);
        }
        save();invalidate();return true;
    }

    void menu(){
        HMENU m=CreatePopupMenu();
        AppendMenuW(m,MF_STRING,idToday,tr(language, L"Mở Calendar Deck", L"Show Calendar Deck"));
        AppendMenuW(m,MF_SEPARATOR,0,nullptr);
        AppendMenuW(m,MF_STRING|(viewMode==ViewMode::Week?MF_CHECKED:0),idViewWeek,tr(language, L"Chế độ Tuần (Week view)", L"Week view"));
        AppendMenuW(m,MF_STRING|(viewMode==ViewMode::Month?MF_CHECKED:0),idViewMonth,tr(language, L"Chế độ Tháng (Month view)", L"Month view"));
        AppendMenuW(m,MF_STRING|(viewMode==ViewMode::Year?MF_CHECKED:0),idViewYear,tr(language, L"Chế độ Năm (Year view)", L"Year view"));
        AppendMenuW(m,MF_SEPARATOR,0,nullptr);
        AppendMenuW(m,MF_STRING,idSettings,tr(language, L"Cài đặt…", L"Settings…"));
        AppendMenuW(m,MF_STRING,idSync,tr(language, L"Đồng bộ tất cả lịch", L"Sync all calendars"));
        AppendMenuW(m,MF_SEPARATOR,0,nullptr);
        AppendMenuW(m,MF_STRING|(desktop?MF_CHECKED:0),idDesktop,tr(language, L"Desktop · Nền màn hình", L"Desktop · behind application windows"));
        AppendMenuW(m,MF_STRING|(!desktop?MF_CHECKED:0),idFloat,tr(language, L"Floating · Cửa sổ nổi", L"Floating · movable window"));
        AppendMenuW(m,MF_STRING|(settings.locked?MF_CHECKED:0),idLock,tr(language, L"Khóa vị trí widget", L"Lock widget position"));
        AppendMenuW(m,MF_STRING,idHide,tr(language, L"Ẩn widget", L"Hide"));
        AppendMenuW(m,MF_SEPARATOR,0,nullptr);
        AppendMenuW(m,MF_STRING,idExit,tr(language, L"Thoát", L"Exit"));
        POINT p{};GetCursorPos(&p);SetForegroundWindow(window);
        int cmd=TrackPopupMenu(m,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,window,nullptr);
        DestroyMenu(m);if(cmd)command(cmd);PostMessageW(window,WM_NULL,0,0);
    }

    void command(int cmd){
        switch(cmd){
            case idPrev:
                if(viewMode==ViewMode::Week){
                    week-=7*daySeconds;currentDate=week;
                }else if(viewMode==ViewMode::Month){
                    auto p=parts(month);int y=p.y,m=int(p.m)-1;if(m<1){m=12;y--;}
                    month=civil(y,m,1);currentDate=month;week=monday(month);
                }else{
                    auto p=parts(year);year=civil(p.y-1,1,1);currentDate=year;week=monday(year);
                }
                followToday=false;request(false);break;

            case idNext:
                if(viewMode==ViewMode::Week){
                    week+=7*daySeconds;currentDate=week;
                }else if(viewMode==ViewMode::Month){
                    auto p=parts(month);int y=p.y,m=int(p.m)+1;if(m>12){m=1;y++;}
                    month=civil(y,m,1);currentDate=month;week=monday(month);
                }else{
                    auto p=parts(year);year=civil(p.y+1,1,1);currentDate=year;week=monday(year);
                }
                followToday=false;request(false);break;

            case idToday:
                ShowWindow(window,IsIconic(window)?SW_RESTORE:SW_SHOW);
                if(!desktop)SetForegroundWindow(window);
                currentDate=today();week=monday(currentDate);month=monthStart(currentDate);year=yearStart(currentDate);
                followToday=true;request(false);break;

            case idViewWeek:
                if(viewMode!=ViewMode::Week){
                    viewMode=ViewMode::Week;week=monday(currentDate);request(false);
                }
                break;

            case idViewMonth:
                if(viewMode!=ViewMode::Month){
                    viewMode=ViewMode::Month;month=monthStart(currentDate);request(false);
                }
                break;

            case idViewYear:
                if(viewMode!=ViewMode::Year){
                    viewMode=ViewMode::Year;year=yearStart(currentDate);request(false);
                }
                break;

            case idSync:request(true);break;
            case idDesktop:mode(true);break;
            case idFloat:mode(false);break;
            case idHide:hideWindow();save();break;
            case idLock:settings.locked=!settings.locked;if(desktop)mode(true);save();break;
            case idExit:SendMessageW(window,WM_CLOSE,1,0);break;
            case idSettings:openSettings();break;
            case idStatus:{
                std::wstring s=status;
                for(auto& c:settings.calendars)s+=L"\n\n"+wide(c.name)+L": "+sourceStates[c.url];
                for(auto& w:calendar.warnings)s+=L"\n\n"+wide(w);
                MessageBoxW(window,s.c_str(),tr(language, L"Trạng thái lịch", L"Calendar status"),MB_OK|MB_ICONINFORMATION);break;
            }
        }
    }

    HWND control(const wchar_t* cls,const std::wstring& title,DWORD style,int x,int y,int w,int h,int id){
        float ds=GetDpiForWindow(settingsWindow)/96.f;
        HWND c=CreateWindowExW(wcscmp(cls,L"EDIT")==0?WS_EX_CLIENTEDGE:0,cls,title.c_str(),WS_CHILD|WS_VISIBLE|style,int(x*ds),int(y*ds),int(w*ds),int(h*ds),settingsWindow,reinterpret_cast<HMENU>(INT_PTR(id)),instance,nullptr);
        bool titleLine=wcscmp(cls,L"STATIC")==0&&x==200&&y<=24;
        SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(titleLine&&settingsHeading?settingsHeading:font),TRUE);return c;
    }
    void updateSourceStatus(){
        if(!settingsWindow||settingsPage!=1)return;
        std::wstring state=tr(language, L"Chưa kết nối", L"Not connected yet");
        if(selected>=0&&selected<int(settings.calendars.size())){auto& c=settings.calendars[selected];auto it=sourceStates.find(c.url);state=it==sourceStates.end()?tr(language, L"Chưa kiểm tra phiên này", L"Not checked this session"):it->second;}
        SetDlgItemTextW(settingsWindow,idSourceStatus,state.c_str());
    }
    void selectCalendar(){
        buildingSettings=true;
        CalendarSource c;if(selected>=0&&selected<int(settings.calendars.size()))c=settings.calendars[selected];
        SetDlgItemTextW(settingsWindow,editName,wide(c.name).c_str());
        SetDlgItemTextW(settingsWindow,editUrl,offlineCalendar(c)?tr(language, L"Tệp .ics ngoại tuyến", L"Imported .ics snapshot"):wide(c.url).c_str());
        EnableWindow(GetDlgItem(settingsWindow,editUrl),!offlineCalendar(c));EnableWindow(GetDlgItem(settingsWindow,idShowUrl),!offlineCalendar(c));
        SetDlgItemTextW(settingsWindow,idSourceType,offlineCalendar(c)?tr(language, L"Nguồn: tệp .ics đã nhập (offline)", L"Source: imported .ics file (offline)"):tr(language, L"Đường dẫn iCal URL hoặc liên kết chia sẻ", L"iCal URL or public Google Calendar link"));
        SetDlgItemTextW(settingsWindow,idSourceHint,offlineCalendar(c)?tr(language, L"Thay thế tệp bên dưới khi lịch trình thay đổi.", L"Replace the file below when your schedule changes."):tr(language, L"Lịch công khai cần quyền chia sẻ công khai.", L"Public iCal requires public calendar access."));
        SetDlgItemTextW(settingsWindow,idOneSync,offlineCalendar(c)?tr(language, L"Thay thế .ics...", L"Replace .ics..."):tr(language, L"Đồng bộ ngay", L"Sync now"));
        SendDlgItemMessageW(settingsWindow,editColor,CB_SETCURSEL,c.color,0);CheckDlgButton(settingsWindow,editVisible,c.visible?BST_CHECKED:BST_UNCHECKED);
        SendDlgItemMessageW(settingsWindow,editUrl,EM_SETPASSWORDCHAR,offlineCalendar(c)?0:L'*',0);CheckDlgButton(settingsWindow,idShowUrl,BST_UNCHECKED);
        EnableWindow(GetDlgItem(settingsWindow,idRemove),selected>=0);EnableWindow(GetDlgItem(settingsWindow,idOneSync),selected>=0);updateSourceStatus();editorDirty=false;buildingSettings=false;
    }
    void settingsContent(){
        buildingSettings=true;editorDirty=false;
        while(HWND child=GetWindow(settingsWindow,GW_CHILD))DestroyWindow(child);
        
        // Sidebar header brand
        control(L"STATIC",L"CALENDAR DECK",0,20,24,145,25,0);
        
        // Sidebar tab buttons (Owner-drawn for beautiful rounded pills)
        const wchar_t* pagesVI[]={L"Chung",L"Lịch",L"Giao diện",L"Giới thiệu"};
        const wchar_t* pagesEN[]={L"General",L"Calendars",L"Appearance",L"About"};
        for(int i=0;i<4;++i){
            control(L"BUTTON",language==Lang::EN?pagesEN[i]:pagesVI[i],WS_TABSTOP|BS_OWNERDRAW,15,74+i*46,145,38,idGeneral+i);
        }
        control(L"STATIC",L"v0.2.4",0,20,535,145,24,0);

        if(settingsPage==1){
            // Calendars Tab
            control(L"STATIC",tr(language, L"Quản lý lịch", L"My Calendars"),0,200,20,580,28,0);
            control(L"STATIC",tr(language, L"Chọn một lịch để chỉnh sửa. Nhớ lưu trước khi chuyển sang lịch khác.", L"Select a calendar to edit. Save changes before switching."),0,200,52,590,24,0);
            
            HWND list=control(L"LISTBOX",L"",LBS_NOTIFY|WS_VSCROLL|WS_BORDER|WS_TABSTOP,200,90,230,345,idList);
            for(auto& c:settings.calendars){
                auto onText=language==Lang::VI?L"[Bật] ":L"[On] ";
                auto offText=language==Lang::VI?L"[Tắt] ":L"[Off] ";
                auto fileText=language==Lang::VI?L" (tệp)":L" (file)";
                auto name=(c.visible?onText:offText)+wide(c.name)+(offlineCalendar(c)?fileText:L"");
                SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));
            }
            SendMessageW(list,LB_SETCURSEL,selected,0);
            
            control(L"BUTTON",tr(language, L"+ Thêm lịch URL", L"+ Add URL Calendar"),WS_TABSTOP|BS_OWNERDRAW,200,448,230,34,idAdd);
            control(L"BUTTON",tr(language, L"Nhập file .ics...", L"Import .ics..."),WS_TABSTOP|BS_OWNERDRAW,200,490,230,34,idImport);
            
            control(L"STATIC",tr(language, L"Tên hiển thị", L"Display name"),0,455,90,335,20,0);
            HWND name=control(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,455,114,335,30,editName);SendMessageW(name,EM_SETLIMITTEXT,100,0);
            
            control(L"STATIC",tr(language, L"Đường dẫn iCal URL hoặc liên kết Google", L"iCal URL or public Google link"),0,455,154,335,20,idSourceType);
            HWND url=control(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL|ES_PASSWORD,455,178,335,30,editUrl);SendMessageW(url,EM_SETLIMITTEXT,4096,0);
            
            control(L"BUTTON",tr(language, L"Hiện đường dẫn", L"Show URL"),WS_TABSTOP|BS_AUTOCHECKBOX,455,214,335,22,idShowUrl);
            control(L"STATIC",tr(language, L"Lịch công khai cần quyền chia sẻ công khai.", L"Public iCal requires public calendar access."),0,455,238,335,18,idSourceHint);
            
            control(L"STATIC",tr(language, L"Màu sắc hiển thị", L"Calendar color"),0,455,260,335,20,0);
            HWND colors=control(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,455,284,335,180,editColor);
            if(language==Lang::VI){
                for(auto color:{L"Xanh dương",L"Tím",L"Xanh lá",L"Hồng",L"Vàng kim"})SendMessageW(colors,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(color));
            }else{
                for(auto color:{L"Blue",L"Purple",L"Green",L"Pink",L"Gold"})SendMessageW(colors,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(color));
            }
            
            control(L"BUTTON",tr(language, L"Hiển thị sự kiện trên lịch", L"Show events on calendar"),WS_TABSTOP|BS_AUTOCHECKBOX,455,324,335,24,editVisible);
            control(L"STATIC",L"",0,455,355,335,48,idSourceStatus);
            
            control(L"BUTTON",tr(language, L"Đồng bộ ngay", L"Sync now"),WS_TABSTOP|BS_OWNERDRAW,455,415,160,34,idOneSync);
            control(L"BUTTON",tr(language, L"Xóa lịch", L"Remove"),WS_TABSTOP|BS_OWNERDRAW,630,415,160,34,idRemove);
            control(L"BUTTON",tr(language, L"Lưu lịch", L"Save calendar"),WS_TABSTOP|BS_OWNERDRAW,455,470,335,38,idSave);
            selectCalendar();
        }else if(settingsPage==0){
            // General Tab
            control(L"STATIC",tr(language, L"Cài đặt chung", L"General Settings"),0,200,20,580,28,0);
            control(L"STATIC",tr(language, L"Cấu hình khởi động, chu kỳ làm mới và ngôn ngữ hiển thị.", L"Configure automatic startup, refresh interval, and display language."),0,200,52,590,24,0);

            control(L"BUTTON",tr(language, L"Khởi động cùng Windows khi mở máy", L"Start automatically with Windows"),WS_TABSTOP|BS_AUTOCHECKBOX,200,95,580,26,idAutoStart);
            CheckDlgButton(settingsWindow,idAutoStart,isAutoStartEnabled()?BST_CHECKED:BST_UNCHECKED);

            control(L"BUTTON",tr(language, L"Thu nhỏ xuống khay hệ thống khi đóng (tiếp tục chạy ngầm)", L"Close to system tray (keep syncing in the background)"),WS_TABSTOP|BS_AUTOCHECKBOX,200,130,580,26,idCloseTray);
            CheckDlgButton(settingsWindow,idCloseTray,settings.closeToTray?BST_CHECKED:BST_UNCHECKED);

            control(L"STATIC",tr(language, L"Tự động kiểm tra lịch mỗi (5 - 1440 phút):", L"Check calendars every (5 - 1440 minutes):"),0,200,172,550,22,0);
            control(L"EDIT",std::to_wstring(settings.intervalMinutes),WS_TABSTOP|ES_NUMBER,200,198,120,30,editInterval);

            control(L"STATIC",tr(language, L"Ngôn ngữ hiển thị (Display Language):", L"Display Language:"),0,200,245,550,22,0);
            HWND langBox=control(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,200,272,240,140,editLang);
            SendMessageW(langBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Tiếng Việt (Vietnamese)"));
            SendMessageW(langBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"English"));
            SendMessageW(langBox,CB_SETCURSEL,language==Lang::EN?1:0,0);

            control(L"STATIC",tr(language,
                L"Phím tắt hữu ích:\n- Ctrl+1: Chế độ Tuần  ·  Ctrl+2: Chế độ Tháng  ·  Ctrl+3: Chế độ Năm\n- F5: Đồng bộ ngay  ·  Home: Về tuần hiện tại  ·  Esc: Ẩn widget\n- Nhấp chuột phải vào ứng dụng hoặc khay để đổi Desktop / Floating.",
                L"Useful shortcuts:\n- Ctrl+1: Week view  ·  Ctrl+2: Month view  ·  Ctrl+3: Year view\n- F5: Sync now  ·  Home: Jump to today  ·  Esc: Hide widget\n- Right-click widget or tray icon to toggle Desktop / Floating."),
                0,200,335,590,75,0);

            control(L"BUTTON",tr(language, L"Lưu thiết lập", L"Save preferences"),WS_TABSTOP|BS_OWNERDRAW,600,485,190,38,idSave);
        }else if(settingsPage==2){
            // Appearance Tab
            control(L"STATIC",tr(language, L"Giao diện & Chế độ Desktop", L"Appearance & Desktop"),0,200,20,580,28,0);
            control(L"STATIC",tr(language,
                L"Giao diện tối bo cong mềm mại với 5 bảng màu lịch nổi bật.\n\nỨng dụng hỗ trợ xem Tuần (lưới 24h), Tháng (toàn cảnh các tuần) và Năm (12 tháng thu nhỏ).\nBạn có thể đổi màu sắc cho từng lịch trong tab Lịch.",
                L"Modern curved dark theme with 5 distinct calendar colors.\n\nSupports Week view (24h grid), Month view (full weeks), and Year view (12 mini-months).\nSelect a custom color for each calendar in the Calendars tab."),
                0,200,56,590,80,0);

            control(L"BUTTON",tr(language, L"Chế độ Desktop (Nền màn hình)", L"Desktop mode"),WS_TABSTOP|BS_OWNERDRAW,200,160,280,42,idDesktop);
            control(L"BUTTON",tr(language, L"Chế độ Cửa sổ nổi (Floating)", L"Floating mode"),WS_TABSTOP|BS_OWNERDRAW,500,160,280,42,idFloat);

            control(L"STATIC",tr(language,
                L"Hướng dẫn:\n- Kéo thanh tiêu đề hoặc các cạnh để định vị và thay đổi kích thước widget.\n- Nhấp chuột phải vào ứng dụng hoặc icon ở khay hệ thống để khóa vị trí hoặc đổi chế độ.\n- Phím tắt: Ctrl+1 (Tuần), Ctrl+2 (Tháng), Ctrl+3 (Năm), F5 (Đồng bộ).",
                L"Instructions:\n- Drag the header or edges to position and resize the widget.\n- Right-click the widget or tray icon to lock position or switch mode.\n- Shortcuts: Ctrl+1 (Week), Ctrl+2 (Month), Ctrl+3 (Year), F5 (Sync)."),
                0,200,230,590,120,0);
        }else{
            // About Tab
            control(L"STATIC",L"Calendar Deck 0.2.4",0,200,20,580,28,0);
            control(L"STATIC",tr(language,
                L"Lịch Desktop gọn nhẹ cho Windows xây dựng bằng C++20, Win32 & Direct2D.\n"
                L"Hỗ trợ đồng bộ Google Calendar iCal và tệp .ics offline, mã hóa bảo mật DPAPI.\n\n"
                L"Cách lấy URL trong Google Calendar:\n"
                L"Cài đặt > Chọn lịch của bạn > Tích hợp lịch >\n"
                L"Địa chỉ công khai ở định dạng iCal, hoặc Địa chỉ bí mật.\n"
                L"Các liên kết chia sẻ công khai có chứa src= hoặc cid= cũng được hỗ trợ.\n\n"
                L"Tất cả URL và dữ liệu lưu trong máy đều được bảo vệ bởi Windows DPAPI.",

                L"Lightweight Windows calendar built with C++20, Win32 + Direct2D.\n"
                L"Google iCal feeds and offline .ics files, encrypted with Windows DPAPI.\n\n"
                L"How to get your URL in Google Calendar:\n"
                L"Settings > your calendar > Integrate calendar >\n"
                L"Public address in iCal format, or Secret address.\n"
                L"Public share links with src= or cid= are also accepted.\n\n"
                L"URLs and cached feeds are securely protected by Windows DPAPI."),
                0,200,56,590,300,0);
        }
        buildingSettings=false;InvalidateRect(settingsWindow,nullptr,TRUE);
    }

    void saveEditor(){
        Settings next=settings;bool connectChanged=false;
        if(settingsPage==0){
            BOOL valid=FALSE;UINT n=GetDlgItemInt(settingsWindow,editInterval,&valid,FALSE);
            if(!valid||n<5||n>1440)throw std::runtime_error(tr(language, "Nhập khoảng thời gian từ 5 đến 1440 phút", "Enter an interval from 5 to 1440 minutes"));
            next.intervalMinutes=int(n);
            next.closeToTray=IsDlgButtonChecked(settingsWindow,idCloseTray)==BST_CHECKED;

            bool autoStart=IsDlgButtonChecked(settingsWindow,idAutoStart)==BST_CHECKED;
            setAutoStartEnabled(autoStart,portable);

            int langSel=int(SendDlgItemMessageW(settingsWindow,editLang,CB_GETCURSEL,0,0));
            Lang newLang=(langSel==1)?Lang::EN:Lang::VI;
            if(newLang!=language){
                language=newLang;
                try{store->write("language",language==Lang::EN?"en":"vi");}catch(...){}
                invalidate();
            }
        }else if(settingsPage==1){
            auto trim=[](std::wstring v){auto first=v.find_first_not_of(L" \r\n\t");if(first==v.npos)return std::wstring{};return v.substr(first,v.find_last_not_of(L" \r\n\t")-first+1);};
            CalendarSource c;if(selected>=0)c=settings.calendars[selected];c.name=utf8(trim(text(GetDlgItem(settingsWindow,editName))));if(!offlineCalendar(c))c.url=utf8(trim(text(GetDlgItem(settingsWindow,editUrl))));c.color=int(SendDlgItemMessageW(settingsWindow,editColor,CB_GETCURSEL,0,0));c.visible=IsDlgButtonChecked(settingsWindow,editVisible)==BST_CHECKED;
            if(c.name.empty())throw std::runtime_error(tr(language, "Nhập tên hiển thị cho lịch này", "Enter a display name for this calendar"));
            if(!offlineCalendar(c)){
                auto normalized=normalizeFeedUrl(c.url);
                if(!normalized)throw std::runtime_error(tr(language, "Dán URL iCal Google Calendar công khai/bí mật (.ics), hoặc link chia sẻ chứa src= hoặc cid=.", "Paste a Google Calendar public/secret iCal URL (.ics), or a single-calendar share link containing src= or cid=."));
                c.url=*normalized;
            }
            for(size_t i=0;i<next.calendars.size();++i)if(int(i)!=selected&&next.calendars[i].url==c.url)throw std::runtime_error(tr(language, "URL lịch này đã được kết nối trước đó", "This calendar URL is already connected"));
            connectChanged=selected<0||next.calendars[selected].url!=c.url;
            if(selected<0){if(next.calendars.size()>=100)throw std::runtime_error(tr(language, "Tối đa 100 lịch", "Maximum 100 calendars"));next.calendars.push_back(c);}else next.calendars[selected]=c;
        }
        next.url.clear();saveSettings(*store,next);settings=std::move(next);if(settingsPage==1&&selected<0)selected=int(settings.calendars.size())-1;
        if(settingsPage==1){demo=false;}
        calendar={};segments.clear();hits.clear();syncTimer();request(connectChanged,settingsPage==1?settings.calendars[selected].url:std::string{});settingsContent();
    }

    void importFile(int replace=-1){
        wchar_t path[32768]{};OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=settingsWindow;
        dialog.lpstrFilter=tr(language, L"Tệp iCalendar (*.ics)\0*.ics\0\0", L"iCalendar files (*.ics)\0*.ics\0\0");dialog.lpstrFile=path;dialog.nMaxFile=32768;
        dialog.lpstrTitle=replace<0?tr(language, L"Nhập lịch offline", L"Import offline calendar"):tr(language, L"Thay thế lịch offline", L"Replace offline calendar");
        dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_DONTADDTORECENT;
        if(!GetOpenFileNameW(&dialog)){if(CommDlgExtendedError())throw std::runtime_error(tr(language, "Không thể mở hộp thoại chọn tệp", "Cannot open the calendar file picker"));return;}
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

        switch(msg){
            case WM_ERASEBKGND:{
                HDC dc=reinterpret_cast<HDC>(wp);
                RECT r{};GetClientRect(w,&r);
                float ds=GetDpiForWindow(w)/96.f;
                int sidebarW=int(175*ds);

                // Sidebar background
                RECT sRect={0,0,sidebarW,r.bottom};
                HBRUSH sBr=CreateSolidBrush(RGB(12,17,26));
                FillRect(dc,&sRect,sBr);
                DeleteObject(sBr);

                // Sidebar divider line
                HPEN pen=CreatePen(PS_SOLID,1,RGB(32,45,64));
                HPEN oldPen=(HPEN)SelectObject(dc,pen);
                MoveToEx(dc,sidebarW,0,nullptr);
                LineTo(dc,sidebarW,r.bottom);
                SelectObject(dc,oldPen);
                DeleteObject(pen);

                // Main content area background
                RECT cRect={sidebarW+1,0,r.right,r.bottom};
                HBRUSH cBr=CreateSolidBrush(RGB(17,24,36));
                FillRect(dc,&cRect,cBr);
                DeleteObject(cBr);
                return 1;
            }
            case WM_CTLCOLORSTATIC:{
                HDC dc=reinterpret_cast<HDC>(wp);
                HWND c=reinterpret_cast<HWND>(lp);
                RECT cr{};GetWindowRect(c,&cr);
                POINT pt{cr.left,cr.top};ScreenToClient(w,&pt);
                float ds=GetDpiForWindow(w)/96.f;
                SetBkMode(dc,TRANSPARENT);
                if(pt.x<int(175*ds)){
                    SetTextColor(dc,RGB(56,189,248)); // Cyan brand
                    static HBRUSH sBr=CreateSolidBrush(RGB(12,17,26));
                    return reinterpret_cast<LRESULT>(sBr);
                }
                if(pt.y<int(45*ds)){
                    SetTextColor(dc,RGB(255,255,255)); // Header title
                }else{
                    SetTextColor(dc,RGB(226,232,240)); // Normal text
                }
                static HBRUSH cBr=CreateSolidBrush(RGB(17,24,36));
                return reinterpret_cast<LRESULT>(cBr);
            }
            case WM_CTLCOLOREDIT:{
                HDC dc=reinterpret_cast<HDC>(wp);
                SetTextColor(dc,RGB(241,245,249));
                SetBkColor(dc,RGB(22,34,50));
                static HBRUSH eBr=CreateSolidBrush(RGB(22,34,50));
                return reinterpret_cast<LRESULT>(eBr);
            }
            case WM_CTLCOLORLISTBOX:{
                HDC dc=reinterpret_cast<HDC>(wp);
                SetTextColor(dc,RGB(241,245,249));
                SetBkColor(dc,RGB(22,34,50));
                static HBRUSH lBr=CreateSolidBrush(RGB(22,34,50));
                return reinterpret_cast<LRESULT>(lBr);
            }
            case WM_CTLCOLORBTN:{
                HDC dc=reinterpret_cast<HDC>(wp);
                SetTextColor(dc,RGB(241,245,249));
                SetBkMode(dc,TRANSPARENT);
                static HBRUSH bBr=CreateSolidBrush(RGB(17,24,36));
                return reinterpret_cast<LRESULT>(bBr);
            }
            case WM_DRAWITEM:{
                auto dis=reinterpret_cast<LPDRAWITEMSTRUCT>(lp);
                if(dis->CtlType==ODT_BUTTON){
                    HDC dc=dis->hDC;
                    RECT r=dis->rcItem;
                    int id=int(dis->CtlID);
                    bool isPressed=(dis->itemState&ODS_SELECTED);
                    bool isSidebar=(id>=idGeneral&&id<=idAbout);
                    bool isPrimary=(id==idSave);
                    bool isDanger=(id==idRemove);
                    bool isActiveTab=isSidebar&&(a->settingsPage==id-idGeneral);
                    bool isModeActive=(id==idDesktop&&a->desktop)||(id==idFloat&&!a->desktop);

                    COLORREF bg,border,text;
                    if(isActiveTab||isPrimary||isModeActive){
                        bg=isPressed?RGB(29,78,216):RGB(37,99,235);
                        border=RGB(96,165,250);
                        text=RGB(255,255,255);
                    }else if(isDanger){
                        bg=isPressed?RGB(80,20,28):RGB(50,20,28);
                        border=RGB(150,40,50);
                        text=RGB(252,165,165);
                    }else{
                        bg=isPressed?RGB(35,50,70):RGB(22,34,50);
                        border=RGB(45,65,90);
                        text=RGB(226,232,240);
                    }

                    HBRUSH br=CreateSolidBrush(bg);
                    HPEN pen=CreatePen(PS_SOLID,1,border);
                    HBRUSH oldBr=(HBRUSH)SelectObject(dc,br);
                    HPEN oldPen=(HPEN)SelectObject(dc,pen);

                    RoundRect(dc,r.left,r.top,r.right,r.bottom,10,10);

                    SelectObject(dc,oldBr);
                    SelectObject(dc,oldPen);
                    DeleteObject(br);
                    DeleteObject(pen);

                    wchar_t btnText[128]{};
                    GetWindowTextW(dis->hwndItem,btnText,128);
                    SetBkMode(dc,TRANSPARENT);
                    SetTextColor(dc,text);
                    HFONT oldFont=(HFONT)SelectObject(dc,a->font);
                    DrawTextW(dc,btnText,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
                    SelectObject(dc,oldFont);
                    return TRUE;
                }
                break;
            }
            case WM_COMMAND:try{
                int id=LOWORD(wp);
                if(id>=idGeneral&&id<=idAbout){if(!a->discardEditor())return 0;a->settingsPage=id-idGeneral;a->settingsContent();return 0;}
                if(id==idList&&HIWORD(wp)==LBN_SELCHANGE){if(!a->discardEditor()){SendDlgItemMessageW(w,idList,LB_SETCURSEL,a->selected,0);return 0;}a->selected=int(SendDlgItemMessageW(w,idList,LB_GETCURSEL,0,0));a->selectCalendar();}
                if(!a->buildingSettings&&((HIWORD(wp)==EN_CHANGE&&(id==editName||id==editUrl||id==editInterval))||(id==editColor&&HIWORD(wp)==CBN_SELCHANGE)||(id==editLang&&HIWORD(wp)==CBN_SELCHANGE)||id==editVisible||id==idCloseTray||id==idAutoStart))a->editorDirty=true;
                if(id==idShowUrl){SendDlgItemMessageW(w,editUrl,EM_SETPASSWORDCHAR,IsDlgButtonChecked(w,idShowUrl)==BST_CHECKED?0:L'*',0);InvalidateRect(GetDlgItem(w,editUrl),nullptr,TRUE);}
                if(id==idAdd){if(!a->discardEditor())return 0;a->selected=-1;SendDlgItemMessageW(w,idList,LB_SETCURSEL,-1,0);a->selectCalendar();SetFocus(GetDlgItem(w,editName));}
                if(id==idSave)a->saveEditor();
                if(id==idImport){if(!a->discardEditor())return 0;a->importFile();return 0;}
                if(id==idOneSync&&a->selected>=0){if(offlineCalendar(a->settings.calendars[a->selected])){if(!a->discardEditor())return 0;a->importFile(a->selected);return 0;}if(a->editorDirty){MessageBoxW(w,tr(a->language, L"Hãy lưu thay đổi trước khi đồng bộ.", L"Save your changes before syncing."),tr(a->language, L"Thay đổi chưa lưu", L"Unsaved changes"),MB_OK);return 0;}a->request(true,a->settings.calendars[a->selected].url);SetDlgItemTextW(w,idSourceStatus,tr(a->language, L"Đang đồng bộ...", L"Syncing..."));}
                if(id==idRemove&&a->selected>=0&&MessageBoxW(w,tr(a->language, L"Xóa lịch này khỏi Calendar Deck? Các sự kiện trên Google Calendar sẽ không bị ảnh hưởng.", L"Remove this calendar from Calendar Deck? Google Calendar events will remain unchanged."),tr(a->language, L"Xóa lịch", L"Remove calendar"),MB_YESNO|MB_ICONQUESTION)==IDYES){auto next=a->settings;next.calendars.erase(next.calendars.begin()+a->selected);next.url.clear();saveSettings(*a->store,next);a->settings=std::move(next);a->selected=-1;a->calendar={};a->segments.clear();a->hits.clear();a->syncTimer();a->request(false);a->settingsContent();}
                if(id==idDesktop||id==idFloat){a->command(id);InvalidateRect(w,nullptr,TRUE);}
                return 0;
            }catch(const std::exception& ex){MessageBoxW(w,wide(ex.what()).c_str(),tr(a->language, L"Kiểm tra cấu hình", L"Check settings"),MB_OK|MB_ICONWARNING);return 0;}
            case WM_CLOSE:if(a->discardEditor())DestroyWindow(w);return 0;
            case WM_DESTROY:a->settingsWindow=nullptr;return 0;
        }
        return DefWindowProcW(w,msg,wp,lp);
    }

    void openSettings(){
        if(settingsWindow){SetForegroundWindow(settingsWindow);return;}
        WNDCLASSW c{};c.lpfnWndProc=settingsProc;c.hInstance=instance;c.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(IDI_CALENDAR_DECK));c.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        static HBRUSH bgBr=CreateSolidBrush(RGB(17,24,36));
        c.hbrBackground=bgBr;c.lpszClassName=L"CalendarDeckSettings";RegisterClassW(&c);
        UINT dpi=GetDpiForWindow(window);float ds=dpi/96.f;
        if(settingsHeading){DeleteObject(settingsHeading);}
        settingsHeading=CreateFontW(-int(20*ds),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        RECT r{0,0,LONG(840*ds),LONG(590*ds)};AdjustWindowRectExForDpi(&r,WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_DLGMODALFRAME,dpi);
        settingsWindow=CreateWindowExW(WS_EX_DLGMODALFRAME,c.lpszClassName,tr(language, L"Calendar Deck - Cài đặt", L"Calendar Deck - Settings"),WS_CAPTION|WS_SYSMENU,CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,nullptr,nullptr,instance,this);
        selected=settings.calendars.empty()?-1:0;settingsContent();ShowWindow(settingsWindow,SW_SHOW);SetForegroundWindow(settingsWindow);
    }

    static LRESULT CALLBACK windowProc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
        auto a=reinterpret_cast<App*>(GetWindowLongPtrW(w,GWLP_USERDATA));
        if(msg==WM_NCCREATE){a=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);a->window=w;SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(a));}
        if(!a)return DefWindowProcW(w,msg,wp,lp);
        if(msg==a->taskbarCreated&&a->taskbarCreated){a->tray(true);if(a->desktop)a->mode(true);return 0;}
        switch(msg){
            case WM_SYSCOMMAND:if(a->desktop&&((wp&0xfff0)==SC_MAXIMIZE||(wp&0xfff0)==SC_MINIMIZE))return 0;break;
            case WM_EXITSIZEMOVE:a->save();if(a->desktop){RECT r{};GetWindowRect(w,&r);refreshDesktopArea(&r);}return 0;
            case WM_ERASEBKGND:return 1;
            case WM_PAINT:{PAINTSTRUCT p{};BeginPaint(w,&p);a->draw();EndPaint(w,&p);return 0;}
            case WM_SIZE:if(wp==SIZE_MINIMIZED){a->discardTarget();}else{if(a->target)a->target->Resize(D2D1::SizeU(LOWORD(lp),HIWORD(lp)));a->invalidate();}if(a->tooltip){RECT r{};GetClientRect(w,&r);TOOLINFOW ti{};ti.cbSize=sizeof(ti);ti.hwnd=w;ti.uId=1;ti.rect=r;SendMessageW(a->tooltip,TTM_NEWTOOLRECTW,0,reinterpret_cast<LPARAM>(&ti));}return 0;
            case WM_DPICHANGED:{a->discardTarget();auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(w,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);return 0;}
            case WM_GETMINMAXINFO:{auto m=reinterpret_cast<MINMAXINFO*>(lp);float s=GetDpiForWindow(w)/96.f;m->ptMinTrackSize={LONG(860*s),LONG(600*s)};return 0;}
            case WM_NCHITTEST:{LRESULT r=DefWindowProcW(w,msg,wp,lp);if(a->settings.locked&&a->desktop)return HTCLIENT;if(r!=HTCLIENT)return r;POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(w,&p);if(p.y/a->scale<78&&a->hit(p.x/a->scale,p.y/a->scale)<0)return HTCAPTION;return r;}
            case WM_MOUSEMOVE:a->hover(lp);return 0;
            case WM_MOUSELEAVE:a->mouseX=-1;a->mouseY=-1;a->hovered=-1;a->invalidate();return 0;
            case WM_LBUTTONUP:{
                int h=a->hit(GET_X_LPARAM(lp)/a->scale,GET_Y_LPARAM(lp)/a->scale);
                if(h>=0){
                    auto item=a->hits[h];
                    if(item.isEvent){
                        MessageBoxW(w,a->details(item).c_str(),tr(a->language, L"Chi tiết sự kiện", L"Event details"),MB_OK|MB_ICONINFORMATION);
                    }else if(item.command==idJumpDate){
                        a->currentDate=item.targetDate;
                        a->week=monday(item.targetDate);
                        a->month=monthStart(item.targetDate);
                        a->year=yearStart(item.targetDate);
                        a->viewMode=item.targetMode;
                        a->request(false);
                    }else{
                        a->command(item.command);
                    }
                }
                return 0;
            }
            case WM_CONTEXTMENU:a->menu();return 0;
            case WM_COMMAND:a->command(LOWORD(wp));return 0;
            case WM_KEYDOWN:
                if(wp==VK_LEFT)a->command(idPrev);
                else if(wp==VK_RIGHT)a->command(idNext);
                else if(wp==VK_HOME)a->command(idToday);
                else if(wp==VK_F5)a->command(idSync);
                else if(wp==VK_ESCAPE)a->command(idHide);
                else if(wp=='S'&&(GetKeyState(VK_CONTROL)&0x8000))a->command(idSettings);
                else if((wp=='1'||wp=='W')&&(GetKeyState(VK_CONTROL)&0x8000))a->command(idViewWeek);
                else if((wp=='2'||wp=='M')&&(GetKeyState(VK_CONTROL)&0x8000))a->command(idViewMonth);
                else if((wp=='3'||wp=='Y')&&(GetKeyState(VK_CONTROL)&0x8000))a->command(idViewYear);
                return 0;
            case WM_TIMER:
                if(wp==2)a->request(true);
                else{
                    a->minuteTimer();
                    auto current=today();
                    if(current!=a->lastDay){
                        a->lastDay=current;
                        if(a->followToday){
                            a->currentDate=current;
                            a->week=monday(current);
                            a->month=monthStart(current);
                            a->year=yearStart(current);
                        }
                        a->request(false);
                    }
                    a->invalidate();
                }
                return 0;
            case WM_TIMECHANGE:case WM_SETTINGCHANGE:a->minuteTimer();if(a->worker)a->request(false);return DefWindowProcW(w,msg,wp,lp);
            case dataMessage:{
                std::unique_ptr<Answer> answer(reinterpret_cast<Answer*>(lp));
                if(answer->generation!=a->generation)return 0;
                if(answer->replace){
                    a->calendar=std::move(answer->calendar);
                    a->hits.clear();
                    a->segments=layoutEvents(a->calendar.events,a->week);
                }
                a->sourceStates=std::move(answer->states);
                a->updateSourceStatus();
                a->status=std::move(answer->status);
                a->hovered=-1;
                a->invalidate();
                return 0;
            }
            case trayMessage:if(lp==WM_RBUTTONUP||lp==WM_CONTEXTMENU)a->menu();else if(lp==WM_LBUTTONDBLCLK)a->command(idToday);return 0;
            case WM_CLOSE:if(!wp&&a->settings.closeToTray){a->command(idHide);return 0;}if(a->closing)return 0;a->closing=true;a->hideWindow();if(a->settingsWindow)DestroyWindow(a->settingsWindow);a->save();if(a->worker)a->worker->stop();a->tray(false);DestroyWindow(w);return 0;
            case WM_DESTROY:{MSG pending{};while(PeekMessageW(&pending,w,dataMessage,dataMessage,PM_REMOVE))delete reinterpret_cast<Answer*>(pending.lParam);PostQuitMessage(0);return 0;}
        }
        return DefWindowProcW(w,msg,wp,lp);
    }

    int run(HINSTANCE inst,const std::wstring& args){
        instance=inst;portable=args.find(L"--portable")!=args.npos;demo=args.find(L"--demo")!=args.npos;
        file=dataDirectory(portable)/L"calendar-deck.db";store=std::make_unique<Store>(file);settings=loadSettings(*store);bool initialDesktop=settings.desktop;

        // Load saved language
        std::string savedLang=store->read("language");
        if(savedLang=="en")language=Lang::EN;
        else language=Lang::VI;

        if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,&factory))||FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(&writing))))throw std::runtime_error("Direct2D/DirectWrite initialization failed");

        auto makeFormat=[&](float fsize,DWRITE_FONT_WEIGHT weight,IDWriteTextFormat** out,DWRITE_TEXT_ALIGNMENT align=DWRITE_TEXT_ALIGNMENT_LEADING,DWRITE_PARAGRAPH_ALIGNMENT valign=DWRITE_PARAGRAPH_ALIGNMENT_NEAR){
            if(FAILED(writing->CreateTextFormat(L"Segoe UI",nullptr,weight,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,fsize,L"en-us",out)))
                throw std::runtime_error("Cannot create text format");
            (*out)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            (*out)->SetTextAlignment(align);
            (*out)->SetParagraphAlignment(valign);
        };
        makeFormat(11.0f,DWRITE_FONT_WEIGHT_NORMAL,&small);
        makeFormat(11.0f,DWRITE_FONT_WEIGHT_SEMI_BOLD,&smallBold);
        makeFormat(13.0f,DWRITE_FONT_WEIGHT_NORMAL,&normal);
        makeFormat(13.0f,DWRITE_FONT_WEIGHT_SEMI_BOLD,&normalBold);
        makeFormat(16.0f,DWRITE_FONT_WEIGHT_SEMI_BOLD,&title);
        makeFormat(22.0f,DWRITE_FONT_WEIGHT_BOLD,&heading);
        makeFormat(11.0f,DWRITE_FONT_WEIGHT_NORMAL,&centerSmall,DWRITE_TEXT_ALIGNMENT_CENTER,DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        makeFormat(12.0f,DWRITE_FONT_WEIGHT_SEMI_BOLD,&centerBold,DWRITE_TEXT_ALIGNMENT_CENTER,DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        makeFormat(9.5f,DWRITE_FONT_WEIGHT_SEMI_BOLD,&centerMini,DWRITE_TEXT_ALIGNMENT_CENTER,DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

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