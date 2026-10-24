# Contributing

Calendar Deck targets Windows 10 1903+ and Windows 11 x64 using C++20, Win32, Direct2D and DirectWrite.

## Build and verify

Use CMake 3.24+ with MinGW-w64 GCC/Ninja or Visual Studio 2022 C++ tools. See [validation](docs/VALIDATION.md) for the tested toolchain.

```powershell
.\scripts\build.ps1
.\scripts\package.ps1
```

Build output is `build/release`; executable packages and source archives go to `dist`.

## Guidelines

- Keep the native architecture lightweight and avoid continuous rendering.
- Add focused regressions for dates, recurrence, time zones, cache integrity and migration.
- Use synthetic events and generic identifiers in tests.
- Keep previous user data readable when storage changes.
- Update both READMEs when user-facing behavior changes.
- Explain the behavior, implementation and verification in pull requests.

## Privacy

Do not commit personal ICS files, private URLs, databases, user screenshots, diagnostic dumps or local backups. The ignored input/local-data folders are local-only. Report calendar bugs with a minimal anonymized event and its expected date/time. For UI reports, include Windows version, display scale and Desktop/Floating mode, and review screenshots before posting.

## Distribution

The packaging script uses an explicit source-file allowlist and excludes generated output and private data. Publish binary ZIPs in GitHub Releases rather than source commits. Packaging does not publish anything or change accounts.
