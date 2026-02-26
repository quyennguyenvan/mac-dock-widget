@echo off
setlocal EnableDelayedExpansion
echo ============================================
echo  SysMonitor - Build Script
echo ============================================
echo.

:: ---- Try MSVC (Visual Studio) ----
set "VSDIR="
for %%Y in (2022 2019) do (
    for %%E in (Community Professional Enterprise BuildTools) do (
        if exist "C:\Program Files\Microsoft Visual Studio\%%Y\%%E\VC\Tools\MSVC" (
            set "VSDIR=C:\Program Files\Microsoft Visual Studio\%%Y\%%E"
        )
    )
)
if not defined VSDIR goto :try_mingw

:: Find latest MSVC toolset
for /f %%i in ('dir /b /ad "!VSDIR!\VC\Tools\MSVC\" 2^>nul') do set "MSVCVER=%%i"
set "MSVC=!VSDIR!\VC\Tools\MSVC\!MSVCVER!"

:: Find latest Windows SDK
set "WINSDK=C:\Program Files (x86)\Windows Kits\10"
if not exist "!WINSDK!\Include" goto :try_mingw
for /f %%i in ('dir /b /ad "!WINSDK!\Include\" 2^>nul') do set "SDKVER=%%i"

echo [*] Using MSVC !MSVCVER!
echo     SDK !SDKVER!
echo.

set "PATH=!MSVC!\bin\Hostx64\x64;!WINSDK!\bin\!SDKVER!\x64;%PATH%"
set "INCLUDE=!MSVC!\include;!WINSDK!\Include\!SDKVER!\ucrt;!WINSDK!\Include\!SDKVER!\um;!WINSDK!\Include\!SDKVER!\shared;!WINSDK!\Include\!SDKVER!\winrt"
set "LIB=!MSVC!\lib\x64;!WINSDK!\Lib\!SDKVER!\ucrt\x64;!WINSDK!\Lib\!SDKVER!\um\x64"

cl.exe /O2 /EHsc /DUNICODE /D_UNICODE src\main.cpp /Fe:SysMonitor.exe ^
    /link user32.lib gdi32.lib gdiplus.lib shell32.lib iphlpapi.lib winhttp.lib advapi32.lib ole32.lib comctl32.lib ^
    /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF

if !ERRORLEVEL! == 0 (
    echo.
    echo [OK] Build successful!
    echo      Output: SysMonitor.exe
    del main.obj 2>nul
) else (
    echo.
    echo [FAIL] Build failed!
)
goto :end

:try_mingw
where g++ >nul 2>nul
if %ERRORLEVEL% == 0 (
    echo [*] Using MinGW g++...
    g++ -O2 -DUNICODE -D_UNICODE -mwindows src\main.cpp -o SysMonitor.exe ^
        -lgdiplus -liphlpapi -lwinhttp -ladvapi32 -lole32 -lshell32 -lcomctl32
    if !ERRORLEVEL! == 0 (
        echo.
        echo [OK] Build successful!
        echo      Output: SysMonitor.exe
    ) else (
        echo.
        echo [FAIL] Build failed!
    )
    goto :end
)

echo [ERROR] No C++ compiler found!
echo.
echo Options:
echo   1. Install Visual Studio 2022 Community (free)
echo      https://visualstudio.microsoft.com/
echo      Select "Desktop development with C++" workload
echo.
echo   2. Install MSYS2 + MinGW-w64
echo      https://www.msys2.org/
echo      Then: pacman -S mingw-w64-x86_64-gcc

:end
echo.
pause
