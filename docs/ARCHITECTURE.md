# Architecture

Calendar Deck is a native C++20 Win32 application. The main UI uses Direct2D and DirectWrite; Settings uses native controls. The distributed app has no embedded browser or JavaScript runtime.

## Modules

| Module | Responsibility |
| --- | --- |
| `src/app.cpp` | Windows, tray, Settings, rendering, background jobs |
| `src/calendar.*` | ICS, time zones, recurrence, week filtering, overlap layout |
| `src/platform.*` | WinHTTP, DPAPI, SQLite, configuration, file import |
| `resources/` | Multi-resolution icon, version information, manifest |
| `tests/core_tests.cpp` | Synthetic regressions with no personal calendar fixtures |

## Data flow

The UI sends the source list, requested local week and generation number to one worker. Pending work is coalesced; obsolete results are ignored. The worker reads cached feeds first, optionally checks remote sources, expands the requested week and returns occurrences through PostMessage. The UI retains the week occurrences and daily display segments.

Imported files are bounded to 10 MiB and validated before saving. A SQLite transaction commits the snapshot and configuration together. Replacement preserves source identity, name, color and visibility. Internal `local:` sources never download from the network and do not depend on the original file after import. Initial file validation/import currently runs on the UI thread, so a large import can pause Settings briefly.

## Network and storage

Downloads accept only HTTPS Google iCal feeds on calendar.google.com:443. Recognized single-calendar src/cid links are converted locally to public iCal. Redirects and automatic Windows credentials are disabled. Conditional requests use validators when available. Failed downloads and invalid feeds do not replace a good cache.

System SQLite and ICU load from System32. Length-prefixed values in the SQLite kv table are protected with user-scoped DPAPI before writing. Records contain settings, raw feeds, HTTP validators, timestamps and sync errors. Feed keys use a stable SHA-256 hash of the source identifier. The original single-calendar settings/cache remain readable for migration.

Normal data is under %LOCALAPPDATA%\\CalendarDeck; portable data is in data beside the EXE. DPAPI still binds portable data to its Windows account. Plaintext exists in memory during processing/rendering. Each thread uses its own connection; the page cache is 256 KiB and the busy timeout is 2.5 seconds.

## Time and layout

Source wall time and its time zone remain separate until conversion to UTC and then the Windows display time zone. All-day dates stay unchanged. Recurrence is evaluated in source civil time to preserve wall-clock schedules across DST. Search periods start at civil midnight before DTSTART's clock is added. COUNT series count from DTSTART; unbounded series skip to relevant periods. Work/occurrence limits bound expansion.

EXDATE excludes occurrences, RDATE adds them, and RECURRENCE-ID replaces moved/cancelled instances. Conflicts use SEQUENCE and modification timestamps. Events crossing midnight split into daily segments. Overlap groups share columns; adjacent endpoints do not overlap.

## Rendering and desktop

The message loop repaints on demand, at minute boundaries and after data changes. It does not run a continuous render loop. Render targets are released when hidden/minimized; one worker waits on a condition variable when idle. WinHTTP uses timeouts, so exit can wait for a blocking request to finish.

Floating mode is a normal taskbar window. Desktop mode reparents into Explorer's WorkerW layer. WorkerW is shell behavior rather than a supported widget API, so Explorer versions and wallpaper tools can affect it. The exposed desktop region is invalidated when the widget hides or changes parent.

See [validation](VALIDATION.md) for evidence and remaining limitations.
