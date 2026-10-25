<div align="center">
  <img src="resources/calendar-deck.png" width="120" alt="Calendar Deck blue calendar icon with the number 36">
  <h1>Calendar Deck</h1>
  <p><strong>Your week, on your desktop.</strong></p>
  <p>A lightweight Windows calendar for Google iCal feeds and offline ICS files.</p>
  <p><strong>English</strong> · <a href="README.vi.md">Tiếng Việt</a></p>
  <p>
    <img alt="Windows 10 and 11" src="https://img.shields.io/badge/Windows-10%20%2F%2011-0078D4">
    <img alt="C++20" src="https://img.shields.io/badge/C%2B%2B-20-00599C">
    <img alt="Version 0.2.4" src="https://img.shields.io/badge/version-0.2.4-2563EB">
  </p>
</div>

---

## A clear view of the whole week

Calendar Deck puts a Monday–Sunday calendar with a full **00:00–24:00** grid on Windows. Keep it behind your application windows in **Desktop mode**, or use **Floating mode** when you want a normal movable window.

Built with **C++20, Win32, Direct2D and DirectWrite**. No Electron, embedded browser, Python or Node.js is required to run the packaged app.

| Feature | What you get |
| --- | --- |
| 3 View Modes | Seamlessly toggle between Week, Month, and Year views |
| Weekly view | All seven days, current-day highlight and live time marker with modern curved cards |
| Monthly view | 6-week full month grid with daily event chips and direct navigation |
| Yearly view | 12 mini-month grid with event indicators and quick jump |
| Multiple calendars | Separate names, five colors, visibility and source status |
| Online sources | Read-only Google iCal feeds with automatic checks and offline cache |
| Offline imports | Import an ICS file; replace it when the timetable changes |
| Event layout | Supported recurring events, all-day events, overnight events and overlap columns |
| Windows controls | Minimize, maximize/restore, tray menu and configurable close behavior |
| Desktop widget | Move/resize when unlocked; lock its position from the tray menu |
| Local storage | URLs, feeds and configuration protected with Windows DPAPI |

## Quick start

### 1. Get the app

For a packaged build, extract **CalendarDeck-v0.2.4-windows-x64.zip** to a folder you will keep, then open **CalendarDeck.exe**. No administrator rights or installer are required.

The package contains the EXE, English/Vietnamese instructions and a shortcut script. If you are building from source, use the [build instructions](#build-from-source).

**Requirements:** Windows 10 version 1903 or later, or Windows 11, x64. The app uses Windows system SQLite and ICU. Internet access is needed for URL sources; file imports work entirely offline.

### 2. Choose how to add your calendar

Open **Settings → Calendars**.

| Add URL Calendar | Import .ics |
| --- | --- |
| Use a Google Calendar feed or supported share link | Select a local iCalendar file |
| Automatically checks for updates | Keeps an offline snapshot |
| Needs access to the remote feed for new data | Needs no Google sign-in or network |
| Last good cache remains available offline | Use **Replace .ics** when your schedule changes |

### 3. Create a desktop shortcut

From the extracted package, run:

```powershell
.\create-desktop-shortcut.ps1
```

From the source project after packaging:

```powershell
.\scripts\create-desktop-shortcut.ps1
```

Alternatively, right-click CalendarDeck.exe and create a shortcut with Windows. The blue **36** icon is embedded in the executable; the app does not depend on an external icon file. If you move the EXE later, recreate the shortcut.

## Connect Google Calendar by URL

1. Open Google Calendar on a computer.
2. Go to **Settings → select your calendar → Integrate calendar**.
3. Copy **Secret address in iCal format**, or **Public address in iCal format** for an accessible public calendar.
4. In Calendar Deck, choose **Add URL Calendar**.
5. Enter a display name, paste the URL, choose a color and visibility, and click **Save calendar**.

Direct HTTPS iCal links ending in `.ics` are accepted. Single-calendar Google share/view links containing `src=` or base64 `cid=` are converted locally to their public iCal URL. General Calendar home pages, event links, iframe HTML and embeds with multiple calendar IDs cannot be used as a single source.

- **Sync now** checks the selected URL calendar.
- **Sync** in the main window, or **Sync all calendars** in the tray menu, checks all URL sources.
- The default interval is **15 minutes**; change it in **General** from **5 to 1440 minutes**.
- The last good feed remains visible if a later request fails.
- Upstream iCal changes can be delayed by Google; this is not instant two-way sync.

**Private URL:** keep it secret. Do not paste it into an issue, screenshot or public repository.

**Public URL:** only works when the calendar owner and organization allow public feed access. A link that works in your signed-in browser may still be unavailable to the app. If the source exposes only free/busy, event names and descriptions are not present for Calendar Deck to display. The app does not change Google sharing permissions.

## Import an ICS file for offline viewing

1. Obtain a `.ics` file that contains your schedule.
2. Open **Settings → Calendars → Import .ics...**.
3. Select the file. It is imported and saved immediately, and marked **(file)** in the list.
4. Edit its display name, color or visibility if desired, then click **Save calendar**.
5. Click an event in the weekly view for its full title, source calendar, time, location and description.

The snapshot is copied into encrypted local storage. Moving or deleting the original file does not remove the imported calendar. The app displays the time of the last import and never attempts network sync for file sources.

### When the timetable changes

Select the imported calendar and choose **Replace .ics...**. Pick the updated file. Its events replace the old snapshot while the calendar's **name, color and visibility are preserved**. Failed or invalid replacements keep the previous snapshot.

To avoid duplicates, replace the existing file calendar rather than importing each revision as another new calendar.

### Exporting from Google Calendar

On desktop, use **Settings → Import & export → Export** when your account allows it. Extract the downloaded ZIP and choose the relevant `.ics` file inside. School/work administrators may restrict exporting. You can also use a valid ICS file obtained directly from your timetable provider.

Imports accept UTF-8 ICS files, including a UTF-8 BOM, up to **10 MiB per file**. ZIP files must be extracted first. Importing a file that already omits event details cannot reconstruct those details.

## Everyday use

| Action | How |
| --- | --- |
| Switch Week / Month / Year | Pill switcher on toolbar, context menu, or Ctrl+1 / Ctrl+2 / Ctrl+3 |
| Previous / next period | Arrow buttons ‹ / › or Left / Right keys |
| Current period | **Today** button or Home key |
| Read event details | Click an event; hover for a tooltip |
| See all-day events | Click the all-day strip for that day |
| Inspect source errors/warnings | Click the status bar at the bottom |
| Change a calendar's appearance | Settings → Calendars → choose it → Save calendar |
| Hide a calendar without removing it | Turn off **Show events on calendar**, then save |
| Remove a source | **Remove calendar**, then confirm; Google events remain unchanged |
| Open Settings | Toolbar button or Ctrl+S |
| Check URL sources | Sync button or F5 |
| Hide the widget | Esc or the tray menu |

**Save before switching:** Settings warns before discarding unsaved edits when you change pages or calendars.

### Floating and Desktop modes

**Floating** is a normal window with taskbar entry, minimize, maximize/restore and close controls. Drag its title area or resize its edges.

**Desktop** attaches the widget to Explorer behind application windows. When unlocked, drag its header or edges to move/resize it. Maximizing is disabled. Right-click the calendar or tray icon to switch mode or toggle **Lock widget position**. Wallpaper tools and Explorer behavior can affect Desktop mode.

### Close, hide and exit

In **Settings → General**, choose whether the Close button hides to the system tray or exits. Double-click the tray icon to show the current week. Choose **Exit** in the tray menu to fully stop the app. A network request in progress may take a short time to finish.

## Updates and local data

| Mode | Location |
| --- | --- |
| Normal | `%LOCALAPPDATA%\CalendarDeck\calendar-deck.db` |
| Portable (`--portable`) | `data\calendar-deck.db` beside the EXE |

Configuration, raw feeds, sync errors and imports use user-scoped Windows DPAPI encryption before SQLite storage. Portable mode is still tied to the Windows account and is not a cross-account backup format. Keep original ICS files or source URLs if you need to move to another Windows account.

For an update: **Exit from the tray**, replace the application files, and reopen it. Normal-mode user data remains in AppData. Preserve the `data` directory when updating a portable installation. Older single-calendar data is read automatically.

To try sample events without connecting a calendar:

```powershell
.\CalendarDeck.exe --portable --demo
```

Use a separate extracted folder for demos. Only one instance is allowed per data directory; exit that instance before changing demo/live mode.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Calendar shows Busy or lacks names | Inspect the source's permissions or original ICS content; free/busy data does not contain the missing details |
| Public feed is unavailable | Check whether it is publicly accessible without signing in and whether the organization allows iCal export |
| No Secret address is offered | Work/school policy or your rights may restrict it; use a permitted full ICS export if available |
| Nothing appears after import | Check the selected week, calendar visibility and event dates; old semester files may have no events this week |
| Events appear duplicated | Hide/remove the old source, or use Replace .ics instead of importing another copy |
| Times look wrong | Check the Windows time zone, source event details and that you are running version 0.2.3 or newer |
| Closing does not stop the app | Close-to-tray may be enabled; use the tray's Exit command |
| Desktop still shows an old image | Exit, refresh the desktop, and use the current build; the app does not alter the wallpaper image |
| Desktop mode is unavailable | Use Floating mode; Explorer/WorkerW or wallpaper software may prevent attachment |
| Build cannot overwrite the EXE | Exit the instance using that build directory, then rebuild |

## Build from source

Tools: **CMake 3.24+** and either **MinGW-w64 GCC with Ninja**, or **Visual Studio 2022 C++ tools**. Python is not required for the application build.

```powershell
.\scripts\build.ps1
```

This configures a Release build in `build/release`, compiles and runs CTest. For an explicit MinGW/Ninja build:

```powershell
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release
ctest --test-dir build/release --output-on-failure
```

The MSVC route is configured but has not been validated on this machine:

```powershell
cmake -S . -B build/msvc -G "Visual Studio 17 2022" -A x64
cmake --build build/msvc --config Release
ctest --test-dir build/msvc -C Release --output-on-failure
```

### Package for distribution

```powershell
.\scripts\package.ps1
```

| Output | Purpose |
| --- | --- |
| `dist/CalendarDeck/` | Runnable app, instructions and shortcut script |
| `dist/CalendarDeck-v0.2.4-windows-x64.zip` | Binary package for GitHub Releases |
| `dist/CalendarDeck-v0.2.4-source.zip` | Allowlisted source files for sharing/upload |
| `dist/SHA256SUMS.txt` | Checksums of the two ZIPs |

The source package excludes builds, binaries, private calendars, caches, local backups and Codex tooling. The repository's ignore rules do the same for normal Git use. Put binary ZIPs in **Releases**, not source commits. Review `git status` before committing.

## Project layout

| Folder | Contents |
| --- | --- |
| `src/` | Application and calendar/storage modules |
| `resources/` | PNG master, multi-size ICO, manifest and version resources |
| `scripts/` | Build, package and shortcut tools |
| `tests/` | Synthetic regression tests |
| `docs/` | Architecture and validation evidence |
| `build/`, `dist/` | Generated output, ignored by Git |
| `input/`, `local-data/` | Local personal files/backups, ignored by Git |

## Scope, performance and verification

The app uses an event-driven Win32 message loop and one background worker; it does not repaint continuously at 60 FPS. A short demo measurement was about **48–49 MiB working set**. That is a brief sample, not a RAM guarantee for every feed, GPU or driver.

The core suite currently contains **96 checks**. The recurrence-time fix was also checked against five Google exports across 104 weeks, with 487 event/week records matching an independent expansion. See [validation evidence and open checks](docs/VALIDATION.md).

Supported recurrence includes DAILY/WEEKLY/MONTHLY/YEARLY, INTERVAL, COUNT, UNTIL, WKST, BYDAY, BYMONTHDAY, BYMONTH, BYSETPOS, EXDATE, RDATE and RECURRENCE-ID overrides. This is not a complete RFC 5545 implementation: custom VTIMEZONE definitions, hourly recurrence, RDATE periods and RECURRENCE-ID RANGE remain unsupported. Limits include 100 sources, 10 MiB/feed, 50,000 VEVENTs/feed and 10,000 occurrences/source/week.

There is no Google OAuth sign-in, event creation/editing, reminder system, light theme, opacity setting or interface-language switch in this release. The main grid does not expose individual events through UI Automation. Live Google feed integration and full Desktop/DPI behavior still need broader manual validation.

## Contributing and references

Read [CONTRIBUTING.md](CONTRIBUTING.md) and the [architecture notes](docs/ARCHITECTURE.md).

Reference documentation: [Google iCal sync](https://support.google.com/calendar/answer/37648), [Google export](https://support.google.com/calendar/answer/37111), [public calendars](https://support.google.com/calendar/answer/37083) and [RFC 5545](https://www.rfc-editor.org/rfc/rfc5545).
