#pragma once
#include "calendar.h"
#include <atomic>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <functional>

namespace deck {
class Store {
public:
    explicit Store(const std::filesystem::path& file);
    ~Store();
    Store(const Store&)=delete;
    Store& operator=(const Store&)=delete;
    std::string read(const char* key);
    void write(const char* key,const std::string& value);
    void transaction(const std::function<void()>& operation);
private:
    void* db_{};
};
struct CalendarSource {
    std::string name, url;
    int color{}; bool visible{true};
};
struct Settings {
    std::vector<CalendarSource> calendars;
    bool closeToTray{true}, locked{false};
    std::string url;
    int intervalMinutes{15};
    bool desktop{false};
    int x{100},y{60},width{1160},height{760}; // Physical pixels; restored within monitor work area.
};
Settings loadSettings(Store& store);
void saveSettings(Store& store,const Settings& s);
struct Feed {std::string url,body,etag,modified; Stamp checked{};};
Feed loadFeed(Store& store,const std::string& url);
void saveFeed(Store& store,const Feed& feed);
bool offlineCalendar(const CalendarSource& source);
// Imports a bounded UTF-8 .ics snapshot into encrypted storage. Replacement keeps
// the source ID/name/color/visibility. Failed reads/parses leave settings intact.
Settings importCalendarFile(Store& store,const Settings& settings,const std::filesystem::path& file,Stamp week,int replaceIndex=-1);
std::string loadSyncStatus(Store& store,const std::string& url);
void saveSyncStatus(Store& store,const std::string& url,const std::string& status);
bool validFeedUrl(const std::string& url);
// Preserves direct iCal URLs; converts a single-calendar Google src/cid share URL
// to its public iCal feed without accessing the network. Does not change sharing.
std::optional<std::string> normalizeFeedUrl(const std::string& input);
// Only HTTPS Google Calendar iCal URLs; no redirects or automatic Windows credentials.
// A successful 304 keeps the previous body. Exceptions never contain the private URL.
Feed downloadFeed(const std::string& url,const Feed& previous,const std::atomic_bool& stop);
std::filesystem::path dataDirectory(bool portable);
std::string demoCalendar(Stamp week);
}
