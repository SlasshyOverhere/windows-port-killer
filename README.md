# Windows Port Killer

Native Win32 port inspector. Shows every TCP/UDP endpoint on the machine and kills the process
holding one. Single self-contained `.exe`, no Python, no runtime, no installer.

![Windows](https://img.shields.io/badge/platform-Windows%2010%2F11-blue)
![C++](https://img.shields.io/badge/C%2B%2B17-Win32%20%2B%20GDI%2B-informational)
![License](https://img.shields.io/badge/license-MIT-orange)

## Why it is fast

- Reads the socket table straight from the IP Helper API (`GetExtendedTcpTable` / `GetExtendedUdpTable`)
  in-process. No `netstat`, no `tasklist`, no subprocess spawn, no console flashing.
- One pass resolves every PID to name/path/working-set through `CreateToolhelp32Snapshot` + `OpenProcess`.
- Virtual-mode ListView (`LVS_OWNERDATA`): painting cost depends on visible rows, not table size.
- Typical full refresh: **~15 ms** for 250+ endpoints and 340 processes.

## Features

- **All endpoints** — IPv4 + IPv6 TCP and UDP, with real listening state and owning PID.
- **Live search** — one box matching port, address, state, PID, process name or path; instant, no re-scan.
- **Protocol filter** — All / TCP / UDP / Listening-only toggle chips.
- **Sort** — click any column, click again to reverse; sort survives refresh.
- **Kill** — kills a single PID or the whole process tree, with a confirmation showing what dies.
- **Details panel** — full command path, working set, endpoint counts, elevation state.
- **Copy** — port, PID, `PID 1234 (name)`, `localhost:3000`, or the whole row, from the right-click menu.
- **Open** — launch the process's port in the browser, or jump to the executable in Explorer.
- **Export** — CSV of the current filtered view.
- **Elevation** — one click (`Ctrl+E`) re-launches as administrator via UAC when a kill is denied.
- **Auto-refresh** — 2 s timer, toggled with `Ctrl+R`.
- **Monochrome UI** — pure black/white/gray palette, no hue anywhere: owner-drawn buttons, rows and
  headers painted with GDI+ grayscale anti-aliasing, zebra striping, white selected states.
- **Compact window** — opens at 1040x660 and resizes down to 780x430; DPI-aware (per-monitor v2),
  custom icon, remembered geometry (stale geometry from older layouts is discarded once).

## Keyboard

| Key | Action |
| --- | --- |
| `Ctrl+F` / `Ctrl+L` | focus search |
| `Esc` | clear search |
| `F5` | refresh |
| `Ctrl+R` | toggle auto-refresh |
| `Ctrl+K` / `Del` | kill selection (tree: `Ctrl+Shift+K`) |
| `Ctrl+C` | copy port |
| `Ctrl+D` | details |
| `Ctrl+E` | elevate |
| `Ctrl+A` | select all |
| `Enter` / double-click | details |

## Build

Needs Visual Studio 2022 (Build Tools are enough) with the C++ workload and a Windows 10/11 SDK.

```bat
build.bat
```

or manually:

```bat
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -S . -B build
cmake --build build
```

Output: `build\PortKiller.exe`. Static CRT, no manifest or VC++ redistributable required.

## Verify

```bat
build\PortKiller.exe --selftest
```

Runs the enumeration and comparator against live OS data and prints
`RESULT PASS` / `RESULT FAIL` plus counts and scan time (exit code 0/1). Useful in CI or after changes.

## Layout

```
src/ports.{h,cpp}   IP Helper enumeration, toolhelp process map, RFC 5952 IPv6 formatting,
                    kill / child-tree walk / privilege helpers
src/main.cpp        window, painting (GDI+), virtual ListView, filtering, sorting, actions
src/app.rc          icon, version info
src/app.manifest    comctl32 v6, per-monitor v2 DPI, UTF-8 code page
tools/make_icon.js  regenerates src/app.ico and the preview PNG
```

Settings persist under `HKCU\Software\PortKiller`.

`port_killer.py` is the original Python/tkinter version, kept for reference only.
