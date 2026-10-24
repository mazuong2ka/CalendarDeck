# Calendar Deck v0.2 validation - 2026-10-24

## 0.2.4 icon, documentation and packaging

A fresh Release build in `build/release` passed CTest; **96 core checks passed**. The executable now embeds the blue calendar/36 icon, with nine sizes (16, 20, 24, 32, 40, 48, 64, 128, 256). Resource inspection confirmed icon group 101 and all nine entries. Window, Settings and tray initialization use the application resource. File/product version metadata is 0.2.4.

The runnable distribution is `dist/CalendarDeck/CalendarDeck.exe`. Both English/Vietnamese READMEs and their local links were checked in the project and distribution. Source and binary ZIPs passed CRC checks; the source allowlist excludes databases, personal ICS files, executables, shortcuts, build output and local tooling. SHA-256 checksums accompany the archives.

Desktop shortcut creation was blocked by the session's Desktop write permissions. A working shortcut was created in the project root instead and verified to point to the packaged EXE and embedded icon. The included script can create it on the user's Desktop when run with normal user permissions.

Old build and diagnostic/transfer folders were archived under ignored `local-data/previous-builds` rather than permanently deleted because recursive deletion required unavailable approval. Their portable data directories were preserved. Original user files remain in ignored `input`; normal AppData storage was not modified. Full visual verification of the new icon in Explorer/taskbar is still a manual check; binary resource verification passed.

## 0.2.3 recurrence date/time correction

Reproduced against the user's Google exports: a weekly Monday 15:00-17:50 event appeared on Tuesday 12:10-15:00. The unbounded recurrence search subtracts the event duration from its week look-back bound. Daily/weekly periodStart previously retained the resulting non-midnight clock, which was then added to DTSTART's clock and shifted event dates and times. Period starts are now normalized to civil midnight; source timezone, UNTIL and EXDATE semantics are preserved.

Release build/CTest passed in `out-fix`; **96 checks passed**. New synthetic regressions cover exact daily timestamps, Google-style weekly UNTIL plus EXDATE, morning and afternoon classes with fractional-hour durations, near-midnight daily events, Sunday WKST and overnight events at both week boundaries. Personal calendar IDs or descriptions are not included in the regression tests.

A separate local audit compared all five supplied `.ics` files over 104 weeks (2025-01-06 through 2026-12-28) against Python dateutil recurrence expansion with independent timezone/exclusion/override handling: **487 event/week records matched** in dates, start/end times and all-day flags. The current SV261 week now contains six correctly timed class occurrences. The user's original input files were read without modification. Existing cached raw feeds will be recalculated on startup with no data migration or reimport.

## 0.2.2 offline .ics import

Release build/CTest passed in `out`; the test executable reports **90 checks passed**. Imports and replacements use an atomic SQLite transaction and DPAPI-protected feed/settings records. The original file is not modified or required after import. URL sources retain their previous format; internal `local:` source IDs identify offline snapshots. Offline sources are excluded from network download and periodic-sync scheduling.

New tests cover an actual file with a Unicode path and UTF-8 BOM, retaining title/location/description, coexistence with URL sources, replacement without changing ID/name/color/visibility, invalid/empty/oversized-file rejection, preventing file replacement of URL sources, transaction rollback, and reopening the database after deleting the source file. These tests use synthetic timetable data. Full GUI interaction with the file picker and a real BKalendar export remain manual checks.

Manual check: import an `.ics` file, inspect an event, restart offline, replace the file, and verify that events update while display preferences remain. Disable or remove the old failing URL source if an offline snapshot is intended to replace it.

## 0.2.1 public URL update

The URL field now accepts public and secret iCal feeds and converts single-calendar Google view/share links containing `src` or base64 `cid` to public iCal before saving. Conversion runs locally and does not change sharing permissions. Existing direct iCal URLs remain byte-for-byte unchanged to preserve private tokens and cache keys. The earlier desktop repaint fix is included.

Release build and CTest passed in `out-fix`; the test executable reports **78 checks passed**. Regression coverage includes public feed acceptance, unchanged private URLs, encoded query values, account-specific Google links, base64 calendar IDs, plus signs, holiday IDs, and rejection of deceptive hosts, credentials, non-HTTPS schemes, invalid IDs, control characters, event links and multi-calendar ambiguity.

The optional public Google feed integration was attempted again and failed to connect with WinHTTP 12029. This does not validate real Google downloads or access permissions. This patch does not claim visual QA of the updated labels or a live end-to-end sync.

Release x64 built with CMake, Ninja and MinGW-w64 GCC 13.2. CTest passed; deck_tests reports 46 checks.

Coverage includes existing recurrence/time-zone/overlap tests, DPAPI encryption, single-calendar migration, independent caches, removal of all calendars, legacy offline cache fallback, new-cache priority, calendar visibility/color preferences and persistent per-source sync status.

Launch smoke tests passed. A short portable demo sample measured 50,192,384-50,208,768 bytes working set (47.87-47.88 MiB), 29,401,088-29,433,856 bytes private memory (28.04-28.07 MiB), with CPU time unchanged over approximately five seconds. A separate normal-mode process measured about 49.41 MiB working set and 24.85 MiB private memory, also with no CPU-time increase during that sample. These are brief idle samples, not peak or long-term measurements, and do not measure real multi-feed synchronization.

UI Automation confirmed that Floating mode exposes Minimize, Maximize and Close buttons. Screenshot capture failed with FrameArrived timeout; subsequent input inspection returned a stale-element error. Visual QA and full interactive Settings/window/tray verification remain incomplete. Desktop attachment, lock/resize, Explorer restart, mixed DPI and multi-monitor behavior require manual validation.

Google HTTPS integration was not confirmed in this environment. Earlier public-feed requests failed to connect. The tests use synthetic feeds and do not validate a real private Google calendar or HTTP 304 end-to-end.

Manual follow-up: connect two calendars, assign colors, test overlapping events, hide/show a calendar, sync one/all, restart offline, verify last-check/error state, cancel removal, confirm removal, minimize/restore/maximize, close to tray, reopen and Exit. Try Desktop mode and lock/unlock, then return to Floating.
