# Port Killer

Native Win32 port inspector for Windows. Lists every TCP and UDP endpoint on the machine and kills
the process holding one. A single self-contained `PortKiller.exe` — no Python, no installer, no runtime,
no redistributable.

[![ci](https://github.com/SlasshyOverhere/windows-port-killer/actions/workflows/ci.yml/badge.svg)](https://github.com/SlasshyOverhere/windows-port-killer/actions/workflows/ci.yml)
[![release](https://img.shields.io/github/v/release/SlasshyOverhere/windows-port-killer)](https://github.com/SlasshyOverhere/windows-port-killer/releases/latest)
![platform](https://img.shields.io/badge/platform-Windows%2010%2F11%20x64-blue)
![C++](https://img.shields.io/badge/C%2B%2B17-Win32%20%2B%20GDI%2B-informational)
![license](https://img.shields.io/github/license/SlasshyOverhere/windows-port-killer)

## Download

Grab `PortKiller-win-x64.zip` from the [latest release](https://github.com/SlasshyOverhere/windows-port-killer/releases/latest),
verify it against `SHA256SUMS.txt`, unzip anywhere and run `PortKiller.exe`. No admin rights required:
elevation is offered in-app for kills that Windows denies to a standard user.

## Why it is fast

- Socket table is read straight from the IP Helper API (`GetExtendedTcpTable` / `GetExtendedUdpTable`)
  in-process. No `netstat`, no `tasklist`, no subprocess spawn, no console flashing.
- One pass resolves every PID to name, path and working set through `CreateToolhelp32Snapshot` + `OpenProcess`.
- Virtual-mode ListView (`LVS_OWNERDATA`): painting cost depends on visible rows, not table size.
- Typical full refresh: **~12-15 ms** for 250+ endpoints and 340 processes.

## Features

- **All endpoints** — IPv4 + IPv6, TCP and UDP, with real listening state and owning PID.
- **Live search** — one box matching port, address, state, PID, process name or path; instant, no re-scan.
- **Filters** — All / TCP / UDP / Listening-only toggle chips.
- **Sort** — click any column, click again to reverse; the sort survives refresh.
- **Kill** — one PID or the whole process tree, with a confirmation naming what will die.
- **Details panel** — full command path, working set, endpoint counts, elevation state.
- **Copy** — port, PID, `PID 1234 (name)`, `localhost:3000`, or the whole row, from the context menu.
- **Open** — launch the process's port in the browser, or reveal the executable in Explorer.
- **Export** — CSV of the current filtered view.
- **Elevation** — one click re-launches as administrator through UAC when a kill is denied.
- **Auto-refresh** — 2 s timer.
- **Monochrome UI** — pure black/white/gray palette with no hue anywhere: owner-drawn buttons, rows and
  headers painted through GDI+ grayscale anti-aliasing, zebra striping, white selected states.
- **Compact window** — opens at 1040x660 and resizes down to 780x430; DPI-aware (per-monitor v2), custom
  icon, remembered window geometry (geometry saved by older layouts is discarded once).

## Command line

| Flag | Effect |
| --- | --- |
| `--version` | print the version and exit |
| `--selftest` | run the built-in enumeration/format/sort self-check, exit 0 on pass |

## Keyboard

| Key | Action |
| --- | --- |
| `Ctrl+F` / `Ctrl+L` | focus search |
| `Esc` | clear search |
| `F5` | refresh |
| `Ctrl+R` | refresh (with `Shift`: toggle auto-refresh) |
| `Ctrl+K` | kill selected process (`Ctrl+Shift+K`: kill its tree) |
| `Ctrl+D` | details |
| `Ctrl+E` | export CSV |
| `Ctrl+C` | copy selection |
| `Ctrl+A` | select all rows |
| `Enter` | details |
| `Del` | kill selected process |

## Build from source

Requirements: Visual Studio 2022 (Desktop C++ workload) and CMake 3.20+.

```bat
build.bat
```

`build.bat` enters the x64 developer prompt and drives CMake + Ninja; the exe lands at
`build\PortKiller.exe`. Manual equivalent:

```bat
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
```

Verify a build before shipping it:

```bat
build\PortKiller.exe --version
build\PortKiller.exe --selftest
powershell -File tools\check_ui.ps1
```

`tools\check_ui.ps1` launches the app, screenshots it into `tools\ui-shot.png` and asserts the palette is
monochrome (no pixel with more than 20 levels of channel spread), printing a coarse luminance ASCII rendering
of the window so layout regressions are visible without an image viewer.

## Versioning and releases

`version.txt` is the single source of truth. It is read by CMake at configure time, compiled into
`--version`, the status bar and the executable's `VERSIONINFO` resource, so nothing can drift.

- **CI** (`.github/workflows/ci.yml`) — on every push to `main` and every pull request: configure, build,
  run `--selftest`, upload the exe artifact.
- **Release** (`.github/workflows/release.yml`) — either
  - *Actions → release → Run workflow* and pick `patch` / `minor` / `major`: the workflow bumps `version.txt`,
    commits `chore(release): vX.Y.Z`, tags it, pushes, builds, tests and publishes the release, or
  - push a tag yourself: `git tag vX.Y.Z && git push origin vX.Y.Z`.

  Either way the release carries `PortKiller-X.Y.Z-win-x64.zip` (exe + this README + LICENSE) and
  `SHA256SUMS.txt`, with notes generated from the commits since the previous tag.

## Layout

```
CMakeLists.txt        static-CRT WIN32 target, version plumbing
build.bat             one-shot local release build
version.txt           single source of truth for the version
src/ports.h|.cpp      IP Helper enumeration, process map, kill helpers
src/main.cpp          window, GDI+ painting, virtual ListView, filters, actions
src/app.rc|.manifest  icon + VERSIONINFO, comctl32 v6, per-monitor DPI, UTF-8
src/resource.h        resource ids
tools/make_icon.js    regenerates src/app.ico from code (no binary asset by hand)
tools/check_ui.ps1    launch + monochrome/size smoke check
.github/workflows/    ci.yml, release.yml
```

## License

MIT — see [LICENSE](LICENSE).
