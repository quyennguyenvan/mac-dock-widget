// SysMonitor - Common definitions, includes, constants
#ifndef SYSMON_COMMON_H
#define SYSMON_COMMON_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x06000000
#endif

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <iphlpapi.h>
#include <winhttp.h>
#include <commctrl.h>
#include <dxgi1_4.h>
#include <string>
#include <vector>
#include <mutex>
#include <cstdio>
#include <cmath>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static const int    WIDGET_H        = 68;
static const int    BAR_PAD         = 12;
static const int    SEC_SEP         = 18;
static const int    SEC_TIME_W      = 115;
static const int    SEC_MEM_W       = 238;
static const int    SEC_IPNET_W     = 190;
static const int    SEC_WX_W        = 105;
static const int    SEC_DISK_COL_W  = 95;
static const int    UPDATE_MS       = 1000;
static const int    BG_FETCH_MS     = 300000;   // 5 min
static const UINT   TIMER_REFRESH   = 1;
static const UINT   WM_TRAYICON     = WM_USER + 100;
static const UINT   IDM_SHOWHIDE    = 2001;
static const UINT   IDM_AUTOSTART   = 2002;
static const UINT   IDM_EXIT        = 2003;

static const wchar_t* APP_NAME      = L"SysMonitor";
static const wchar_t* WND_CLASS     = L"SysMonitorWidgetClass";
static const wchar_t* REG_RUN       = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* REG_APP       = L"Software\\SysMonitor";

// ---------------------------------------------------------------------------
// Shared data types
// ---------------------------------------------------------------------------
struct VolInfo {
    wchar_t letter;
    double  usedGB;
    double  totalGB;
};

struct ExtData {
    std::wstring ip     = L"Loading...";
    std::wstring city   = L"Loading...";
    std::wstring country;
    double       lat    = 0, lon = 0;
    double       temp   = 0;
    int          wcode  = -1;
    std::wstring wdesc;
    bool         loaded = false;
};

#endif // SYSMON_COMMON_H
