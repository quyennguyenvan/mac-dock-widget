# SysMonitor

Lightweight, transparent Windows system monitor widget written in C++ using raw Win32 API and GDI+. Designed for minimal CPU and memory footprint.

## Features

- **Date & Time** — current date and clock with seconds
- **CPU Usage** — total + per-core breakdown with color-coded bars
- **RAM & Swap** — usage bars with exact values
- **Network Speed** — real-time upload/download throughput
- **Public IP** — fetched from ip-api.com
- **Weather** — current temperature and conditions via Open-Meteo (no API key needed)
- **Transparent overlay** — semi-transparent dark background, always on top
- **System tray only** — no taskbar icon; right-click tray icon for menu
- **Auto-start** — optional "Start with Windows" toggle
- **Draggable** — click and drag to reposition; position is saved
- **Single instance** — prevents duplicate processes

## Build

### Option A: Visual Studio / MSVC

1. Open **Developer Command Prompt for VS** (or **x64 Native Tools Command Prompt**)
2. Navigate to the `SysMonitor` folder
3. Run:
   ```
   build.bat
   ```

### Option B: CMake

```bash
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```

### Option C: MinGW-w64

1. Install [MSYS2](https://www.msys2.org/) and `mingw-w64-x86_64-gcc`
2. From MinGW terminal:
   ```
   build.bat
   ```

## Usage

1. Run `SysMonitor.exe`
2. The widget appears on the right side of your screen
3. **Right-click the tray icon** (cyan circle in system tray) for options:
   - Show/Hide Widget
   - Start with Windows (auto-start toggle)
   - Exit
4. **Double-click tray icon** to toggle widget visibility
5. **Click and drag** the widget to reposition it

## System Requirements

- Windows 7 or later
- No additional runtime dependencies

## Architecture

Single-file C++ application (~600 lines) with zero external dependencies beyond the Windows SDK:

| Component | Technology |
|-----------|-----------|
| Window | Win32 API — `WS_EX_LAYERED \| WS_EX_TOPMOST \| WS_EX_TOOLWINDOW` |
| Rendering | GDI+ with `UpdateLayeredWindow` for per-pixel alpha |
| CPU monitoring | `NtQuerySystemInformation` (locale-independent) |
| Memory | `GlobalMemoryStatusEx` |
| Network speed | `GetIfTable2` (IP Helper API) |
| HTTP requests | WinHTTP (background thread, 5-min refresh) |
| Auto-start | Registry `HKCU\...\Run` |
| Config | Registry `HKCU\Software\SysMonitor` |

## APIs Used

- **ip-api.com** — public IP + geolocation (free, no key)
- **Open-Meteo** — weather data (free, no key)
# mac-dock-widget
# mac-dock-widget
