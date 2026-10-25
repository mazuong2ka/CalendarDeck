<div align="center">
  <img src="resources/calendar-deck.png" width="140" alt="Calendar Deck blue calendar icon with the number 36">

  # 📅 Calendar Deck

  ### **Your week. Your month. Your year. All on your desktop.**

  A blazing-fast, native Windows calendar widget — powered by C++20, Direct2D & zero bloat.
  Sync Google Calendar feeds, import offline ICS files, and see everything at a glance.

  **English** · [Tiếng Việt](README.vi.md)

  ![Windows 10 / 11](https://img.shields.io/badge/Windows-10%20%2F%2011-0078D4?style=for-the-badge&logo=windows&logoColor=white)
  ![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)
  ![Version](https://img.shields.io/badge/version-0.2.4-2563EB?style=for-the-badge)
  ![License](https://img.shields.io/badge/license-Personal-green?style=for-the-badge)

  ---

  > 🚀 **Big UI Update** — Completely redesigned with modern card-based layout, segmented view switcher, and a gorgeous dark theme. Three view modes, five event colors, and a sleek bottom dock. This isn't your grandfather's calendar app.

</div>

---

## ✨ What's New in v0.2.4

| 🎯 Feature | Description |
|---|---|
| 🖼️ **Fully Redesigned UI** | Modern dark theme with rounded cards, pill-shaped view switcher, hover effects, and smooth Direct2D rendering |
| 📆 **3 View Modes** | Seamlessly switch between **Week**, **Month**, and **Year** views with `Ctrl+1/2/3` |
| 🎨 **5 Color-Coded Calendars** | Ocean Blue · Violet · Emerald · Rose · Amber — each with unique card backgrounds and accent colors |
| 🔄 **Smart Sync Engine** | Auto-sync Google Calendar feeds (5 min – 24 hr intervals), offline cache, conditional requests |
| 📁 **Offline ICS Import** | Import `.ics` files with one click. Replace when your schedule changes — settings preserved |
| 🖥️ **Desktop Widget Mode** | Pin your calendar behind all windows. Lock position, resize freely |
| 🌐 **Bilingual Interface** | Full Vietnamese 🇻🇳 & English 🇬🇧 support — switchable in Settings |
| ⚡ **~48 MB Memory** | Native Win32. No Electron. No browser engine. No runtime. Just pure performance |

---

## 🖥️ The Three Views

### 📋 Week View
> Your command center. Full **00:00–24:00** timeline grid with all 7 days, Monday through Sunday.

- **Curved event cards** with color-coded left accent bars
- **Live time marker** — red line with current time badge, moving in real-time
- **Today highlight** — subtle blue background column
- **All-day event strip** — compact chips above the grid
- **Overlap columns** — multiple concurrent events display side-by-side
- **Hover effects** — cards glow white on hover for instant visual feedback

### 📊 Month View
> The big picture. A 6-week grid showing your entire month with event chips.

- **Daily event chips** with color dots and time labels
- **Overflow indicator** — `+N more` badge when a day is packed
- **Click any day** to jump directly to that week
- **Today badge** — distinctive blue circle on the current date
- **Cross-month days** shown in muted style for context

### 🗓️ Year View
> 12 mini-month cards at a glance. Spot busy periods instantly.

- **Event count badges** on each month card
- **Dot indicators** on days with events
- **Click any month** to jump to Month view
- **Hover cards** glow with accent borders

---

## 🚀 Quick Start

### 1. Download & Run

Extract `CalendarDeck-v0.2.4-windows-x64.zip` → open **CalendarDeck.exe**. That's it.

> [!NOTE]
> No installer, no admin rights, no dependencies. Runs on **Windows 10 (1903+)** or **Windows 11**, x64.

### 2. Add Your Calendar

Open **Settings → Calendars**, then pick your method:

| 🌐 Add URL Calendar | 📁 Import .ics File |
|---|---|
| Paste a Google Calendar iCal link | Select a local `.ics` file |
| Auto-syncs on your chosen interval | Instant offline snapshot |
| Offline cache when network is down | No Google sign-in needed |
| Works with public & private feeds | Use **Replace .ics** to update |

### 3. Create a Desktop Shortcut

```powershell
.\create-desktop-shortcut.ps1
```

The blue **36** icon is embedded in the EXE — no external icon file needed.

---

## 🔗 Connect Google Calendar

1. Open [Google Calendar](https://calendar.google.com) on desktop
2. **Settings → select calendar → Integrate calendar**
3. Copy the **Secret address in iCal format** (or Public address)
4. In Calendar Deck: **Add URL Calendar** → paste URL → pick a name & color → **Save**

> [!IMPORTANT]
> **Keep your Secret URL private.** Never share it in screenshots, issues, or public repos.

### Sync Behavior

| Action | What it does |
|---|---|
| **Sync now** | Checks the selected URL calendar |
| **Sync** button / `F5` | Checks all URL sources |
| **Auto interval** | Default **15 min** (configurable: 5–1440 min) |
| **Offline resilience** | Last valid cache stays visible when sync fails |

> [!NOTE]
> Google may delay iCal feed updates. Calendar Deck performs read-only sync — it does not modify your Google Calendar.

---

## 📁 Import an ICS File

1. Get your `.ics` file (from Google Export, school timetable, etc.)
2. **Settings → Calendars → Import .ics...**
3. The file is imported and saved immediately, marked **(file)** in the list
4. Customize name, color, visibility → **Save calendar**
5. Click any event for full details: title, time, location, description, source calendar

### When Your Schedule Changes

Select the imported calendar → **Replace .ics...** → pick the new file.
Events update instantly. **Name, color, and visibility are preserved.**

> [!TIP]
> Always use **Replace** instead of importing as a new calendar to avoid duplicates.

### Accepted Formats
- UTF-8 `.ics` files (including UTF-8 BOM)
- Maximum **10 MiB** per file
- ZIP archives must be extracted first

---

## ⌨️ Keyboard Shortcuts

| Action | Shortcut |
|---|---|
| Week view | `Ctrl+1` |
| Month view | `Ctrl+2` |
| Year view | `Ctrl+3` |
| Previous / Next period | `←` / `→` |
| Jump to today | `Home` |
| Open Settings | `Ctrl+S` |
| Sync all calendars | `F5` |
| Hide widget | `Esc` |

---

## 🖥️ Desktop & Floating Modes

| Mode | Behavior |
|---|---|
| **Floating** | Normal window with taskbar, minimize/maximize, drag & resize |
| **Desktop** | Pinned behind all windows on your desktop. Drag header/edges when unlocked |

> Right-click the calendar or tray icon to switch mode or toggle **Lock widget position**.

### Close, Hide & Exit

In **Settings → General**, choose whether the Close button:
- **Hides to system tray** (default) — double-click tray icon to restore
- **Exits completely** — use **Exit** in the tray menu for a full stop

---

## 💾 Data & Storage

| Mode | Database Location |
|---|---|
| Normal | `%LOCALAPPDATA%\CalendarDeck\calendar-deck.db` |
| Portable (`--portable`) | `data\calendar-deck.db` beside the EXE |

- All data encrypted with **Windows DPAPI** before SQLite storage
- Portable data is tied to the current Windows account
- Keep original ICS files or source URLs for cross-account migration

### Updating

1. **Exit** from the tray
2. Replace application files
3. Reopen — your data is preserved in AppData (or `data/` for portable)

### Demo Mode

```powershell
.\CalendarDeck.exe --portable --demo
```

> Use a separate folder for demo mode. Only one instance per data directory.

---

## 🔧 Troubleshooting

| Symptom | Solution |
|---|---|
| Events show "Busy" or no names | Check source permissions — free/busy feeds don't include details |
| Public feed unavailable | Verify public access without sign-in; check org iCal export policy |
| No Secret address option | Work/school account restriction — try full ICS export |
| No events after import | Check viewed week, calendar visibility, and file date range |
| Duplicate events | Use **Replace .ics** instead of importing again |
| Wrong times | Check Windows time zone; use version 0.2.3+ |
| Close doesn't stop app | Disable close-to-tray, or use tray **Exit** |
| Desktop mode unavailable | Explorer/WorkerW or wallpaper tools may interfere — use Floating |

---

## 🏗️ Build from Source

**Requirements:** CMake 3.24+ and MinGW-w64 GCC with Ninja, or Visual Studio 2022 C++ tools.

```powershell
.\scripts\build.ps1
```

Or manually:

```powershell
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release
ctest --test-dir build/release --output-on-failure
```

### Package for Distribution

```powershell
.\scripts\package.ps1
```

| Output | Purpose |
|---|---|
| `dist/CalendarDeck/` | Runnable app with instructions |
| `dist/CalendarDeck-v0.2.4-windows-x64.zip` | Binary package for Releases |
| `dist/CalendarDeck-v0.2.4-source.zip` | Curated source archive |
| `dist/SHA256SUMS.txt` | Checksums for verification |

---

## 📂 Project Layout

```
CalendarDeck/
├── src/           # App core — UI, calendar engine, storage
├── resources/     # Icons (9 sizes), manifest, version info
├── scripts/       # Build, package & shortcut automation
├── tests/         # 96 synthetic regression checks
├── docs/          # Architecture & validation evidence
├── build/         # Generated output (git-ignored)
└── dist/          # Distribution packages (git-ignored)
```

## 🔬 Performance & Verification

- **Event-driven** Win32 message loop — no continuous 60 FPS render
- **~48–49 MiB** working set (brief demo measurement)
- **96 core checks** passing in CTest
- Recurrence validated against **5 Google exports × 104 weeks** (487 records matched)
- Supports: DAILY/WEEKLY/MONTHLY/YEARLY, INTERVAL, COUNT, UNTIL, WKST, BYDAY, BYMONTHDAY, BYMONTH, BYSETPOS, EXDATE, RDATE, RECURRENCE-ID overrides

---

## 📚 Contributing & References

Read [CONTRIBUTING.md](CONTRIBUTING.md) and the [Architecture Notes](docs/ARCHITECTURE.md).

**References:**
[Google iCal Sync](https://support.google.com/calendar/answer/37648) ·
[Google Export](https://support.google.com/calendar/answer/37111) ·
[Public Calendars](https://support.google.com/calendar/answer/37083) ·
[RFC 5545](https://www.rfc-editor.org/rfc/rfc5545)

---

<div align="center">
  <sub>Built with ❤️ using C++20 · Win32 · Direct2D · DirectWrite</sub>
  <br>
  <sub>No Electron. No bloat. Just your calendar.</sub>
</div>
