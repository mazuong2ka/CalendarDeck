#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace deck {
using Stamp = int64_t; // Seconds on a civil timeline, or UTC, as documented per field.
constexpr Stamp daySeconds = 86400;
Stamp civil(int y, unsigned m, unsigned d, int h=0, int min=0, int s=0);
struct Parts { int y; unsigned m, d; int hour, minute, second, weekday; }; // Monday = 0
Parts parts(Stamp value);
Stamp today();
Stamp monday(Stamp date);
int monthLength(int y, unsigned m);
Stamp monthStart(Stamp date);
Stamp yearStart(Stamp date);
Stamp toLocal(Stamp utc);
Stamp toUtc(Stamp wall, const std::string& zone); // Empty zone = Windows local.
std::wstring wide(const std::string& utf8);
std::string utf8(const std::wstring& text);

struct Occurrence {
    std::string uid, title, location, description;
    Stamp start{}, end{}; // Local civil times for display. All-day end is exclusive.
    bool allDay{};
    int color{};
    std::string calendarName;
};
struct CalendarResult {
    std::vector<Occurrence> events;
    std::vector<std::string> warnings;
};
// Expands occurrences intersecting the requested local range or week.
// Malformed input throws; unsupported rules/timezones produce visible warnings.
CalendarResult expandCalendarRange(const std::string& ics, Stamp rangeStart, Stamp rangeEnd);
CalendarResult expandCalendar(const std::string& ics, Stamp week);
struct Segment { size_t event; int day, column{}, columns{1}; Stamp start, end; };
std::vector<Segment> layoutEvents(const std::vector<Occurrence>& events, Stamp week);
}
