// SysMonitor - Lightweight Windows System Monitor Widget
// Single-file C++ application using Win32 API + GDI+ for minimal footprint.
// Features: transparent always-on-top overlay, system tray, auto-start,
//           per-core CPU, RAM/Swap, network speed, public IP, weather.

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
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// ===================================================================
// Constants
// ===================================================================
static const int    WIDGET_H        = 76;
static const int    BAR_PAD         = 12;
static const int    SEC_SEP         = 18;
static const int    SEC_TIME_W      = 115;
static const int    SEC_MEM_W       = 238;
static const int    SEC_IPNET_W     = 190;
static const int    SEC_WX_W        = 105;
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

// ===================================================================
// NtQuerySystemInformation types (locale-independent CPU monitoring)
// ===================================================================
struct PROC_PERF_INFO {
    LARGE_INTEGER IdleTime;
    LARGE_INTEGER KernelTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER DpcTime;
    LARGE_INTEGER InterruptTime;
    ULONG         InterruptCount;
};
typedef LONG(NTAPI* NtQSI_t)(ULONG, PVOID, ULONG, PULONG);

// ===================================================================
// Global state
// ===================================================================
static HWND         g_hwnd          = nullptr;
static HINSTANCE    g_hInst         = nullptr;
static bool         g_visible       = true;

// Tray
static NOTIFYICONDATA g_nid         = {};
static HICON        g_trayIcon      = nullptr;

// GDI+
static ULONG_PTR    g_gdipToken     = 0;
static Gdiplus::FontFamily* g_ff    = nullptr;
static Gdiplus::Font* g_fTime       = nullptr;
static Gdiplus::Font* g_fDate       = nullptr;
static Gdiplus::Font* g_fTitle      = nullptr;
static Gdiplus::Font* g_fVal        = nullptr;
static Gdiplus::Font* g_fSmall      = nullptr;
static Gdiplus::Font* g_fTiny       = nullptr;

// Render buffer
static HDC          g_memDC         = nullptr;
static HBITMAP      g_dib           = nullptr;
static void*        g_dibBits       = nullptr;
static int          g_dibW = 0, g_dibH = 0;

// CPU
static NtQSI_t     g_NtQSI         = nullptr;
static int          g_numCores      = 0;
static std::vector<PROC_PERF_INFO> g_prevCpu;
static std::vector<double> g_coreUse;
static double       g_totalCpu      = 0;

// Memory
static ULONGLONG    g_ramTotalMB = 0, g_ramUsedMB = 0;
static ULONGLONG    g_swapTotalMB = 0, g_swapUsedMB = 0;

// Disk volumes
struct VolInfo { wchar_t letter; double usedGB; double totalGB; };
static VolInfo      g_vols[26];
static int          g_numVols = 0;

// Network speed
static ULONGLONG    g_netPrevIn = 0, g_netPrevOut = 0;
static ULONGLONG    g_netTick = 0;
static double       g_netDown = 0, g_netUp = 0;
static bool         g_netInit = false;
static std::wstring g_lanIP = L"--";

// External data (background thread)
struct ExtData {
    std::wstring ip    = L"Loading...";
    std::wstring city  = L"Loading...";
    std::wstring country;
    double       lat = 0, lon = 0;
    double       temp = 0;
    int          wcode = -1;
    std::wstring wdesc;
    bool         loaded = false;
};
static std::mutex   g_extMtx;
static ExtData      g_ext;

// Background thread
static HANDLE       g_bgThread      = nullptr;
static HANDLE       g_shutdownEvt   = nullptr;

// Tooltip
static HWND         g_tip           = nullptr;
static int          g_hovCore       = -1;
static int          g_hovVol        = -1;
static bool         g_mouseTracking = false;

// ===================================================================
// Utility: UTF-8 → wstring
// ===================================================================
static std::wstring ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

// ===================================================================
// Utility: format helpers
// ===================================================================
static void FmtSpeed(double bps, wchar_t* buf, int len) {
    if (bps < 1024.0)
        swprintf_s(buf, len, L"%.0f B/s", bps);
    else if (bps < 1048576.0)
        swprintf_s(buf, len, L"%.1f KB/s", bps / 1024.0);
    else if (bps < 1073741824.0)
        swprintf_s(buf, len, L"%.1f MB/s", bps / 1048576.0);
    else
        swprintf_s(buf, len, L"%.2f GB/s", bps / 1073741824.0);
}

static void FmtMem(ULONGLONG mb, wchar_t* buf, int len) {
    if (mb >= 1024)
        swprintf_s(buf, len, L"%.1f GB", (double)mb / 1024.0);
    else
        swprintf_s(buf, len, L"%llu MB", mb);
}

// ===================================================================
// Minimal JSON helpers (for known API shapes)
// ===================================================================
static std::wstring JStr(const std::wstring& j, const std::wstring& key) {
    std::wstring k = L"\"" + key + L"\"";
    size_t p = j.find(k);
    if (p == std::wstring::npos) return {};
    p += k.size();
    while (p < j.size() && (j[p] == L' ' || j[p] == L':' || j[p] == L'\t')) p++;
    if (p >= j.size() || j[p] != L'"') return {};
    size_t s = ++p;
    while (p < j.size() && j[p] != L'"') p++;
    return j.substr(s, p - s);
}

static double JNum(const std::wstring& j, const std::wstring& key) {
    std::wstring k = L"\"" + key + L"\"";
    size_t p = j.find(k);
    if (p == std::wstring::npos) return 0;
    p += k.size();
    while (p < j.size() && (j[p] == L' ' || j[p] == L':' || j[p] == L'\t')) p++;
    return _wtof(j.c_str() + p);
}

static int JInt(const std::wstring& j, const std::wstring& key) {
    return (int)JNum(j, key);
}

// ===================================================================
// HTTP GET (supports HTTP and HTTPS)
// ===================================================================
static std::string HttpGet(const wchar_t* host, const wchar_t* path, bool tls) {
    std::string result;
    HINTERNET ses = WinHttpOpen(L"SysMonitor/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    if (!ses) return result;

    INTERNET_PORT port = tls ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    HINTERNET con = WinHttpConnect(ses, host, port, 0);
    if (!con) { WinHttpCloseHandle(ses); return result; }

    DWORD flags = tls ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(con, L"GET", path, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) { WinHttpCloseHandle(con); WinHttpCloseHandle(ses); return result; }

    WinHttpSetTimeouts(req, 10000, 10000, 10000, 10000);

    if (WinHttpSendRequest(req, nullptr, 0, nullptr, 0, 0, 0) &&
        WinHttpReceiveResponse(req, nullptr)) {
        DWORD avail = 0;
        do {
            WinHttpQueryDataAvailable(req, &avail);
            if (avail > 0) {
                std::string buf(avail, 0);
                DWORD rd = 0;
                WinHttpReadData(req, &buf[0], avail, &rd);
                result.append(buf, 0, rd);
            }
        } while (avail > 0);
    }
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return result;
}

// ===================================================================
// Weather code → description
// ===================================================================
static const wchar_t* WeatherDesc(int c) {
    switch (c) {
    case 0:            return L"Clear Sky";
    case 1:            return L"Mainly Clear";
    case 2:            return L"Partly Cloudy";
    case 3:            return L"Overcast";
    case 45: case 48:  return L"Foggy";
    case 51: case 53: case 55: return L"Drizzle";
    case 56: case 57:  return L"Freezing Drizzle";
    case 61: case 63: case 65: return L"Rain";
    case 66: case 67:  return L"Freezing Rain";
    case 71: case 73: case 75: return L"Snow";
    case 77:           return L"Snow Grains";
    case 80: case 81: case 82: return L"Showers";
    case 85: case 86:  return L"Snow Showers";
    case 95:           return L"Thunderstorm";
    case 96: case 99:  return L"Hail Storm";
    default:           return L"Unknown";
    }
}

// ===================================================================
// CPU monitoring (NtQuerySystemInformation - locale-independent)
// ===================================================================
static void InitCpu() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll)
        g_NtQSI = (NtQSI_t)GetProcAddress(ntdll, "NtQuerySystemInformation");

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    g_numCores = (int)si.dwNumberOfProcessors;
    if (g_numCores > 128) g_numCores = 128;

    g_coreUse.resize(g_numCores, 0.0);
    g_prevCpu.resize(g_numCores);

    if (g_NtQSI) {
        ULONG ret = 0;
        g_NtQSI(8, g_prevCpu.data(),
            g_numCores * sizeof(PROC_PERF_INFO), &ret);
    }
}

static void UpdateCpu() {
    if (!g_NtQSI) return;
    std::vector<PROC_PERF_INFO> cur(g_numCores);
    ULONG ret = 0;
    if (g_NtQSI(8, cur.data(), g_numCores * sizeof(PROC_PERF_INFO), &ret) != 0)
        return;

    double sum = 0;
    for (int i = 0; i < g_numCores; i++) {
        LONGLONG dI = cur[i].IdleTime.QuadPart   - g_prevCpu[i].IdleTime.QuadPart;
        LONGLONG dK = cur[i].KernelTime.QuadPart - g_prevCpu[i].KernelTime.QuadPart;
        LONGLONG dU = cur[i].UserTime.QuadPart   - g_prevCpu[i].UserTime.QuadPart;
        LONGLONG dT = dK + dU;
        double u = (dT > 0) ? (1.0 - (double)dI / dT) * 100.0 : 0.0;
        if (u < 0) u = 0; if (u > 100) u = 100;
        g_coreUse[i] = u;
        sum += u;
    }
    g_totalCpu = g_numCores > 0 ? sum / g_numCores : 0;
    g_prevCpu = cur;
}

// ===================================================================
// Memory monitoring
// ===================================================================
static void UpdateMem() {
    MEMORYSTATUSEX ms = {}; ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);
    g_ramTotalMB  = ms.ullTotalPhys / (1024 * 1024);
    g_ramUsedMB   = (ms.ullTotalPhys - ms.ullAvailPhys) / (1024 * 1024);
    g_swapTotalMB = ms.ullTotalPageFile / (1024 * 1024);
    g_swapUsedMB  = (ms.ullTotalPageFile - ms.ullAvailPageFile) / (1024 * 1024);
}

// ===================================================================
// Disk volume monitoring
// ===================================================================
static void UpdateDisk() {
    g_numVols = 0;
    wchar_t drives[128];
    DWORD len = GetLogicalDriveStringsW(127, drives);
    for (wchar_t* d = drives; *d && g_numVols < 26; d += wcslen(d) + 1) {
        if (GetDriveTypeW(d) == DRIVE_FIXED) {
            ULARGE_INTEGER avail, total;
            if (GetDiskFreeSpaceExW(d, &avail, &total, nullptr)) {
                g_vols[g_numVols].letter = d[0];
                g_vols[g_numVols].totalGB = total.QuadPart / (1024.0 * 1024.0 * 1024.0);
                g_vols[g_numVols].usedGB = (total.QuadPart - avail.QuadPart) / (1024.0 * 1024.0 * 1024.0);
                g_numVols++;
            }
        }
    }
}

static void FmtDisk(double gb, wchar_t* buf, int len) {
    if (gb >= 1024.0) swprintf_s(buf, len, L"%.1f TB", gb / 1024.0);
    else              swprintf_s(buf, len, L"%.1f GB", gb);
}

// ===================================================================
// Network speed monitoring
// ===================================================================
static void GetNetTotals(ULONGLONG& in, ULONGLONG& out) {
    in = out = 0;
    DWORD size = 0;
    GetIfTable(nullptr, &size, FALSE);
    if (size == 0) return;
    std::vector<BYTE> buf(size);
    MIB_IFTABLE* tbl = reinterpret_cast<MIB_IFTABLE*>(buf.data());
    if (GetIfTable(tbl, &size, FALSE) != NO_ERROR) return;
    for (DWORD i = 0; i < tbl->dwNumEntries; i++) {
        auto& r = tbl->table[i];
        if (r.dwOperStatus == MIB_IF_OPER_STATUS_OPERATIONAL &&
            r.dwType != MIB_IF_TYPE_LOOPBACK) {
            in  += r.dwInOctets;
            out += r.dwOutOctets;
        }
    }
}

static void InitNet() {
    GetNetTotals(g_netPrevIn, g_netPrevOut);
    g_netTick = GetTickCount64();
    g_netInit = true;
}

static void UpdateNet() {
    ULONGLONG ci, co;
    GetNetTotals(ci, co);
    ULONGLONG now = GetTickCount64();
    double dt = (now - g_netTick) / 1000.0;
    if (dt > 0.05 && g_netInit) {
        g_netDown = (ci >= g_netPrevIn)  ? (ci - g_netPrevIn)  / dt : 0;
        g_netUp   = (co >= g_netPrevOut) ? (co - g_netPrevOut) / dt : 0;
    }
    g_netPrevIn  = ci;
    g_netPrevOut = co;
    g_netTick    = now;
}

static void UpdateLanIP() {
    ULONG size = 0;
    GetAdaptersInfo(nullptr, &size);
    if (size == 0) return;
    std::vector<BYTE> buf(size);
    IP_ADAPTER_INFO* info = reinterpret_cast<IP_ADAPTER_INFO*>(buf.data());
    if (GetAdaptersInfo(info, &size) != ERROR_SUCCESS) return;
    for (IP_ADAPTER_INFO* a = info; a; a = a->Next) {
        std::string ip = a->IpAddressList.IpAddress.String;
        if (!ip.empty() && ip != "0.0.0.0") {
            g_lanIP = std::wstring(ip.begin(), ip.end());
            return;
        }
    }
    g_lanIP = L"--";
}

// ===================================================================
// Background data fetching (IP + geolocation + weather)
// ===================================================================
static void FetchExternal() {
    // ip-api.com returns public IP, city, country, lat, lon (HTTP only for free tier)
    std::string ipResp = HttpGet(L"ip-api.com", L"/json", false);
    if (ipResp.empty()) return;

    std::wstring ij = ToWide(ipResp);
    std::wstring ip   = JStr(ij, L"query");
    std::wstring city = JStr(ij, L"city");
    std::wstring cc   = JStr(ij, L"countryCode");
    double lat = JNum(ij, L"lat");
    double lon = JNum(ij, L"lon");

    // Open-Meteo for weather (HTTPS, no API key needed)
    wchar_t wpath[512];
    swprintf_s(wpath,
        L"/v1/forecast?latitude=%.4f&longitude=%.4f"
        L"&current=temperature_2m,weather_code&current_weather=true",
        lat, lon);
    std::string wResp = HttpGet(L"api.open-meteo.com", wpath, true);

    double temp = 0;
    int wcode = -1;
    if (!wResp.empty()) {
        std::wstring wj = ToWide(wResp);
        // Try new API format first ("current" object)
        size_t cw = wj.find(L"\"current\"");
        if (cw != std::wstring::npos) {
            // Skip past "current_weather" if "current" matched inside it
            std::wstring sub = wj.substr(cw);
            if (sub.find(L"\"temperature_2m\"") != std::wstring::npos) {
                temp  = JNum(sub, L"temperature_2m");
                wcode = JInt(sub, L"weather_code");
            }
        }
        // Fall back to legacy format ("current_weather" object)
        if (wcode < 0) {
            cw = wj.find(L"\"current_weather\"");
            if (cw != std::wstring::npos) {
                std::wstring sub = wj.substr(cw);
                temp  = JNum(sub, L"temperature");
                wcode = JInt(sub, L"weathercode");
            }
        }
    }

    std::lock_guard<std::mutex> lk(g_extMtx);
    g_ext.ip      = ip.empty() ? L"N/A" : ip;
    g_ext.city    = city.empty() ? L"Unknown" : city;
    g_ext.country = cc;
    g_ext.lat     = lat;
    g_ext.lon     = lon;
    g_ext.temp    = temp;
    g_ext.wcode   = wcode;
    g_ext.wdesc   = (wcode >= 0) ? WeatherDesc(wcode) : L"N/A";
    g_ext.loaded  = true;
}

static DWORD WINAPI BgThread(LPVOID) {
    FetchExternal();
    while (WaitForSingleObject(g_shutdownEvt, BG_FETCH_MS) == WAIT_TIMEOUT)
        FetchExternal();
    return 0;
}

// ===================================================================
// Tray icon (created programmatically - cyan circle)
// ===================================================================
static HICON MakeTrayIcon() {
    const int S = 32;
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth  = S;
    bmi.bmiHeader.biHeight = -S;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;

    UINT32* cBits = nullptr;
    HBITMAP cBmp = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, (void**)&cBits, nullptr, 0);
    UINT32* mBits = nullptr;
    HBITMAP mBmp = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, (void**)&mBits, nullptr, 0);

    float cx = S / 2.0f, cy = S / 2.0f, r = S / 2.0f - 1.5f;
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            float d = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy));
            if (d <= r + 0.5f) {
                float a = (d > r - 0.5f) ? (r + 0.5f - d) : 1.0f;
                if (a > 1.f) a = 1.f; if (a < 0.f) a = 0.f;
                BYTE A = (BYTE)(a * 255);
                BYTE R = (BYTE)(60  * a);
                BYTE G = (BYTE)(200 * a);
                BYTE B = (BYTE)(255 * a);
                cBits[y * S + x] = (A << 24) | (R << 16) | (G << 8) | B;
            }
            mBits[y * S + x] = 0;
        }
    }
    ICONINFO ii = {}; ii.fIcon = TRUE; ii.hbmColor = cBmp; ii.hbmMask = mBmp;
    HICON ico = CreateIconIndirect(&ii);
    DeleteObject(cBmp); DeleteObject(mBmp); DeleteDC(dc);
    return ico;
}

// ===================================================================
// System tray management
// ===================================================================
static void AddTray(HWND hw) {
    g_trayIcon = MakeTrayIcon();
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd   = hw;
    g_nid.uID    = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon  = g_trayIcon;
    wcscpy_s(g_nid.szTip, L"SysMonitor - System Widget");
    Shell_NotifyIcon(NIM_ADD, &g_nid);
}

static void RemoveTray() {
    Shell_NotifyIcon(NIM_DELETE, &g_nid);
    if (g_trayIcon) DestroyIcon(g_trayIcon);
}

static void ShowTrayMenu(HWND hw) {
    POINT pt; GetCursorPos(&pt);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | MF_DISABLED, 0, L"SysMonitor v1.0");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, IDM_SHOWHIDE, g_visible ? L"Hide Widget" : L"Show Widget");

    HKEY key;
    bool autoOn = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        autoOn = RegQueryValueExW(key, APP_NAME, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
        RegCloseKey(key);
    }
    AppendMenuW(m, MF_STRING | (autoOn ? MF_CHECKED : 0), IDM_AUTOSTART, L"Start with Windows");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, IDM_EXIT, L"Exit");

    SetForegroundWindow(hw);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hw, nullptr);
    DestroyMenu(m);
}

// ===================================================================
// Auto-start toggle
// ===================================================================
static void ToggleAutoStart() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN, 0, KEY_ALL_ACCESS, &key) != ERROR_SUCCESS)
        return;
    if (RegQueryValueExW(key, APP_NAME, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        RegDeleteValueW(key, APP_NAME);
    } else {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        RegSetValueExW(key, APP_NAME, 0, REG_SZ,
            (BYTE*)path, (DWORD)(wcslen(path) + 1) * sizeof(wchar_t));
    }
    RegCloseKey(key);
}

// ===================================================================
// Position persistence
// ===================================================================
// ===================================================================
// GDI+ init / cleanup
// ===================================================================
static void InitGdip() {
    Gdiplus::GdiplusStartupInput in;
    Gdiplus::GdiplusStartup(&g_gdipToken, &in, nullptr);

    g_ff = new Gdiplus::FontFamily(L"Segoe UI");
    if (!g_ff->IsAvailable()) { delete g_ff; g_ff = new Gdiplus::FontFamily(L"Arial"); }

    g_fTime  = new Gdiplus::Font(g_ff, 20, Gdiplus::FontStyleBold,    Gdiplus::UnitPixel);
    g_fDate  = new Gdiplus::Font(g_ff, 12, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    g_fTitle = new Gdiplus::Font(g_ff, 13, Gdiplus::FontStyleBold,    Gdiplus::UnitPixel);
    g_fVal   = new Gdiplus::Font(g_ff, 13, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    g_fSmall = new Gdiplus::Font(g_ff, 11, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    g_fTiny  = new Gdiplus::Font(g_ff,  8, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
}

static void CleanupGdip() {
    delete g_fTime; delete g_fDate; delete g_fTitle; delete g_fVal; delete g_fSmall; delete g_fTiny;
    delete g_ff;
    if (g_dib)   DeleteObject(g_dib);
    if (g_memDC) DeleteDC(g_memDC);
    Gdiplus::GdiplusShutdown(g_gdipToken);
}

// ===================================================================
// DIB management (persistent render buffer)
// ===================================================================
static void EnsureDIB(int w, int h) {
    if (g_dibW == w && g_dibH == h && g_dib) return;
    if (g_dib) DeleteObject(g_dib);
    if (!g_memDC) g_memDC = CreateCompatibleDC(nullptr);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize     = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth    = w;
    bmi.bmiHeader.biHeight   = -h;
    bmi.bmiHeader.biPlanes   = 1;
    bmi.bmiHeader.biBitCount = 32;
    g_dib = CreateDIBSection(g_memDC, &bmi, DIB_RGB_COLORS, &g_dibBits, nullptr, 0);
    SelectObject(g_memDC, g_dib);
    g_dibW = w; g_dibH = h;
}

// ===================================================================
// Widget dimension calculation (horizontal bar)
// ===================================================================
static int CalcCpuSecW() {
    int blocksW = g_numCores * 10;
    return (blocksW > 110 ? blocksW : 110) + 12;
}

static const int SEC_DISK_COL_W = 95;
static int CalcDiskSecW() {
    int cols = (g_numVols + 1) / 2;
    if (cols < 1) cols = 1;
    return cols * SEC_DISK_COL_W;
}

static int CalcWidth() {
    return BAR_PAD + SEC_TIME_W + SEC_SEP + CalcCpuSecW() + SEC_SEP
         + SEC_MEM_W + SEC_SEP + CalcDiskSecW() + SEC_SEP
         + SEC_IPNET_W + SEC_SEP + SEC_WX_W + BAR_PAD;
}

// ===================================================================
// GDI+ drawing helpers
// ===================================================================
static void FillRoundRect(Gdiplus::Graphics& g, Gdiplus::Brush& br,
                          float x, float y, float w, float h, float r) {
    Gdiplus::GraphicsPath p;
    p.AddArc(x, y, r*2, r*2, 180, 90);
    p.AddArc(x+w-r*2, y, r*2, r*2, 270, 90);
    p.AddArc(x+w-r*2, y+h-r*2, r*2, r*2, 0, 90);
    p.AddArc(x, y+h-r*2, r*2, r*2, 90, 90);
    p.CloseFigure();
    g.FillPath(&br, &p);
}

static void StrokeRoundRect(Gdiplus::Graphics& g, Gdiplus::Pen& pen,
                            float x, float y, float w, float h, float r) {
    Gdiplus::GraphicsPath p;
    p.AddArc(x, y, r*2, r*2, 180, 90);
    p.AddArc(x+w-r*2, y, r*2, r*2, 270, 90);
    p.AddArc(x+w-r*2, y+h-r*2, r*2, r*2, 0, 90);
    p.AddArc(x, y+h-r*2, r*2, r*2, 90, 90);
    p.CloseFigure();
    g.DrawPath(&pen, &p);
}

static void DrawBar(Gdiplus::Graphics& g, float x, float y, float w, float h,
                    double pct, Gdiplus::Color col) {
    Gdiplus::SolidBrush bg(Gdiplus::Color(40, 255, 255, 255));
    FillRoundRect(g, bg, x, y, w, h, h / 2);
    float fw = (float)(w * pct / 100.0);
    if (fw > h) {
        Gdiplus::SolidBrush fb(col);
        FillRoundRect(g, fb, x, y, fw, h, h / 2);
    }
}

static Gdiplus::Color UsageCol(double p) {
    if (p < 50) return Gdiplus::Color(255, 0, 230, 118);
    if (p < 80) return Gdiplus::Color(255, 255, 171, 0);
    return Gdiplus::Color(255, 255, 23, 68);
}

// ===================================================================
// Tooltip (CPU cores + disk volumes)
// ===================================================================
static void InitTip(HWND parent) {
    INITCOMMONCONTROLSEX ic = { sizeof(ic), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&ic);
    g_tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        parent, nullptr, g_hInst, nullptr);
    if (!g_tip) return;
    TOOLINFOW ti = {};
    ti.cbSize   = sizeof(ti);
    ti.uFlags   = TTF_TRACK | TTF_ABSOLUTE;
    ti.hwnd     = parent;
    ti.uId      = 1;
    ti.lpszText = (LPWSTR)L"";
    SendMessageW(g_tip, TTM_ADDTOOL, 0, (LPARAM)&ti);
    SendMessageW(g_tip, TTM_SETMAXTIPWIDTH, 0, 300);
}

static int HitTestCore(int cx, int cy) {
    float cpuX = (float)(BAR_PAD + SEC_TIME_W + 16);
    float blockY = 44.f;
    for (int i = 0; i < g_numCores; i++) {
        float bx = cpuX + i * 10.f;
        if (cx >= (int)bx && cx < (int)(bx + 8) &&
            cy >= (int)blockY && cy < (int)(blockY + 18))
            return i;
    }
    return -1;
}

static int HitTestVol(int cx, int cy) {
    float diskX = (float)(BAR_PAD + SEC_TIME_W + 16 + CalcCpuSecW() + 16
                          + SEC_MEM_W + 16);
    float colW = (float)SEC_DISK_COL_W;
    for (int v = 0; v < g_numVols; v++) {
        int col = v / 2;
        int row = v % 2;
        float rx = diskX + col * colW;
        float ry = (row == 0) ? 9.f : 42.f;
        if (cx >= (int)rx && cx < (int)(rx + colW) &&
            cy >= (int)ry && cy < (int)(ry + 24))
            return v;
    }
    return -1;
}

static void ShowTip(HWND hw, const wchar_t* text) {
    if (!g_tip) return;
    TOOLINFOW ti = {};
    ti.cbSize   = sizeof(ti);
    ti.hwnd     = hw;
    ti.uId      = 1;
    ti.lpszText = (LPWSTR)text;
    SendMessageW(g_tip, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
    POINT pt; GetCursorPos(&pt);
    SendMessageW(g_tip, TTM_TRACKPOSITION, 0, MAKELPARAM(pt.x + 14, pt.y + 14));
    SendMessageW(g_tip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
}

static void HideTip(HWND hw) {
    if (!g_tip) return;
    TOOLINFOW ti = {};
    ti.cbSize = sizeof(ti);
    ti.hwnd   = hw;
    ti.uId    = 1;
    SendMessageW(g_tip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
}

static void UpdateTip(HWND hw) {
    if (!g_tip) return;
    wchar_t buf[256];
    if (g_hovCore >= 0) {
        swprintf_s(buf, L"Core %d: %.1f%%", g_hovCore, g_coreUse[g_hovCore]);
        ShowTip(hw, buf);
    } else if (g_hovVol >= 0 && g_hovVol < g_numVols) {
        wchar_t uB[16], tB[16], fB[16];
        double freeGB = g_vols[g_hovVol].totalGB - g_vols[g_hovVol].usedGB;
        FmtDisk(g_vols[g_hovVol].usedGB, uB, 16);
        FmtDisk(g_vols[g_hovVol].totalGB, tB, 16);
        FmtDisk(freeGB, fB, 16);
        double pct = g_vols[g_hovVol].totalGB > 0
            ? g_vols[g_hovVol].usedGB * 100.0 / g_vols[g_hovVol].totalGB : 0;
        swprintf_s(buf, L"Volume %c:\nUsed: %s / %s (%.1f%%)\nFree: %s",
                   g_vols[g_hovVol].letter, uB, tB, pct, fB);
        ShowTip(hw, buf);
    }
}

// ===================================================================
// Main widget drawing (horizontal bar layout)
// ===================================================================
static void DrawContent(Gdiplus::Graphics& g, int W, int H) {
    using namespace Gdiplus;

    const float R1 = 9.f;         // row 1 top
    const float R2 = 42.f;        // row 2 top
    const float RH = 24.f;        // row height

    SolidBrush white(Color(255, 245, 245, 255));
    SolidBrush dim(Color(210, 210, 215, 235));
    SolidBrush accent(Color(255, 100, 200, 255));
    SolidBrush green(Color(255, 0, 230, 118));
    SolidBrush orange(Color(255, 255, 100, 70));
    Pen sep(Color(40, 255, 255, 255), 1.f);

    StringFormat sfL; sfL.SetAlignment(StringAlignmentNear); sfL.SetFormatFlags(StringFormatFlagsNoWrap);
    StringFormat sfR; sfR.SetAlignment(StringAlignmentFar);  sfR.SetFormatFlags(StringFormatFlagsNoWrap);
    StringFormat sfC; sfC.SetAlignment(StringAlignmentCenter); sfC.SetFormatFlags(StringFormatFlagsNoWrap);

    float x = (float)BAR_PAD;

    // ---- Section 1: Date & Time ----
    {
        float sw = (float)SEC_TIME_W;
        SYSTEMTIME st; GetLocalTime(&st);
        const wchar_t* days[]   = {L"Sun",L"Mon",L"Tue",L"Wed",L"Thu",L"Fri",L"Sat"};
        const wchar_t* months[] = {L"Jan",L"Feb",L"Mar",L"Apr",L"May",L"Jun",
                                   L"Jul",L"Aug",L"Sep",L"Oct",L"Nov",L"Dec"};
        wchar_t dateBuf[64];
        swprintf_s(dateBuf, L"%s, %s %d, %d", days[st.wDayOfWeek],
                   months[st.wMonth-1], st.wDay, st.wYear);
        g.DrawString(dateBuf, -1, g_fDate, RectF(x, R1, sw, RH), &sfC, &dim);

        wchar_t timeBuf[16];
        swprintf_s(timeBuf, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
        g.DrawString(timeBuf, -1, g_fTime, RectF(x, R2 - 2, sw, RH + 4), &sfC, &white);
        x += sw;
    }

    // ---- Vertical separator ----
    x += 8; g.DrawLine(&sep, x, 8.f, x, (float)H - 8.f); x += 8;

    // ---- Section 2: CPU ----
    {
        float sw = (float)CalcCpuSecW();
        wchar_t cpuBuf[32];
        swprintf_s(cpuBuf, L"CPU  %.0f%%", g_totalCpu);
        g.DrawString(cpuBuf, -1, g_fTitle, RectF(x, R1, 70, RH), &sfL, &accent);
        DrawBar(g, x + 70, R1 + 6, sw - 82, 7, g_totalCpu, UsageCol(g_totalCpu));

        for (int i = 0; i < g_numCores; i++) {
            float bx = x + i * 10.f;
            float by = R2 + 2;
            Color uc = UsageCol(g_coreUse[i]);
            BYTE alpha = (BYTE)(80 + g_coreUse[i] * 1.75);
            SolidBrush cb(Color(alpha, uc.GetR(), uc.GetG(), uc.GetB()));
            FillRoundRect(g, cb, bx, by, 8, 18, 2);
        }
        x += sw;
    }

    // ---- Vertical separator ----
    x += 8; g.DrawLine(&sep, x, 8.f, x, (float)H - 8.f); x += 8;

    // ---- Section 3: Memory (wider) ----
    {
        float sw = (float)SEC_MEM_W;
        wchar_t uBuf[32], tBuf[32];

        // RAM - row 1
        FmtMem(g_ramUsedMB, uBuf, 32); FmtMem(g_ramTotalMB, tBuf, 32);
        wchar_t ramV[64]; swprintf_s(ramV, L"%s / %s", uBuf, tBuf);
        g.DrawString(L"RAM", -1, g_fTitle, RectF(x, R1, 38, RH), &sfL, &accent);
        double ramPct = g_ramTotalMB > 0 ? g_ramUsedMB * 100.0 / g_ramTotalMB : 0;
        DrawBar(g, x + 40, R1 + 7, 100, 6, ramPct, Color(255, 100, 180, 255));
        g.DrawString(ramV, -1, g_fSmall, RectF(x + 144, R1 + 1, sw - 144, RH), &sfL, &dim);

        // Swap - row 2
        FmtMem(g_swapUsedMB, uBuf, 32); FmtMem(g_swapTotalMB, tBuf, 32);
        wchar_t swpV[64]; swprintf_s(swpV, L"%s / %s", uBuf, tBuf);
        g.DrawString(L"Swap", -1, g_fTitle, RectF(x, R2, 40, RH), &sfL, &accent);
        double swpPct = g_swapTotalMB > 0 ? g_swapUsedMB * 100.0 / g_swapTotalMB : 0;
        DrawBar(g, x + 42, R2 + 7, 98, 6, swpPct, Color(255, 180, 130, 255));
        g.DrawString(swpV, -1, g_fSmall, RectF(x + 144, R2 + 1, sw - 144, RH), &sfL, &dim);
        x += sw;
    }

    // ---- Vertical separator ----
    x += 8; g.DrawLine(&sep, x, 8.f, x, (float)H - 8.f); x += 8;

    // ---- Section: Disk Volumes ----
    {
        float colW = (float)SEC_DISK_COL_W;
        for (int v = 0; v < g_numVols; v++) {
            int col = v / 2;
            int row = v % 2;
            float cx = x + col * colW;
            float cy = (row == 0) ? R1 : R2;

            wchar_t lbl[4]; swprintf_s(lbl, L"%c:", g_vols[v].letter);
            g.DrawString(lbl, -1, g_fTitle, RectF(cx, cy, 22, RH), &sfL, &accent);

            double pct = g_vols[v].totalGB > 0 ? g_vols[v].usedGB * 100.0 / g_vols[v].totalGB : 0;
            Color bc = pct < 80 ? Color(255, 100, 180, 255) : Color(255, 255, 80, 60);
            DrawBar(g, cx + 24, cy + 7, 35, 6, pct, bc);

            wchar_t pL[8]; swprintf_s(pL, L"%.0f%%", pct);
            SolidBrush pBr(bc);
            g.DrawString(pL, -1, g_fSmall, RectF(cx + 62, cy + 1, 32, RH), &sfL, &pBr);
        }
        x += (float)CalcDiskSecW();
    }

    // ---- Vertical separator ----
    x += 8; g.DrawLine(&sep, x, 8.f, x, (float)H - 8.f); x += 8;

    // ---- Section: IP + Network (combined) ----
    {
        float sw = (float)SEC_IPNET_W;
        wchar_t upS[32], dnS[32];
        FmtSpeed(g_netUp,   upS, 32);
        FmtSpeed(g_netDown, dnS, 32);
        wchar_t upL[48], dnL[48];
        swprintf_s(upL, L"\u2191 %s", upS);
        swprintf_s(dnL, L"\u2193 %s", dnS);

        // Row 1: Public IP (left) + Upload speed (right)
        {
            std::lock_guard<std::mutex> lk(g_extMtx);
            g.DrawString(L"IP", -1, g_fTitle, RectF(x, R1, 18, RH), &sfL, &accent);
            g.DrawString(g_ext.ip.c_str(), -1, g_fSmall, RectF(x + 18, R1 + 1, sw - 100, RH), &sfL, &dim);
        }
        g.DrawString(upL, -1, g_fVal, RectF(x, R1, sw, RH), &sfR, &green);

        // Row 2: LAN IP (left) + Download speed (right)
        g.DrawString(L"LAN", -1, g_fTitle, RectF(x, R2, 36, RH), &sfL, &accent);
        g.DrawString(g_lanIP.c_str(), -1, g_fSmall, RectF(x + 36, R2 + 1, sw - 118, RH), &sfL, &dim);
        g.DrawString(dnL, -1, g_fVal, RectF(x, R2, sw, RH), &sfR, &orange);

        x += sw;
    }

    // ---- Vertical separator ----
    x += 8; g.DrawLine(&sep, x, 8.f, x, (float)H - 8.f); x += 8;

    // ---- Section 5: Location & Weather (rightmost) ----
    {
        std::lock_guard<std::mutex> lk(g_extMtx);
        float wxW = (float)SEC_WX_W;
        wchar_t locL[128];
        if (g_ext.loaded) {
            if (!g_ext.country.empty())
                swprintf_s(locL, L"%s, %s", g_ext.city.c_str(), g_ext.country.c_str());
            else
                swprintf_s(locL, L"%s", g_ext.city.c_str());
        } else {
            wcscpy_s(locL, L"Loading...");
        }
        g.DrawString(locL, -1, g_fTitle, RectF(x, R1, wxW, RH), &sfL, &accent);

        if (g_ext.loaded && g_ext.wcode >= 0) {
            double f = g_ext.temp * 9.0 / 5.0 + 32.0;
            wchar_t wL[128];
            swprintf_s(wL, L"%s %.0f\u00B0C/%.0f\u00B0F",
                       g_ext.wdesc.c_str(), g_ext.temp, f);
            g.DrawString(wL, -1, g_fVal, RectF(x, R2, wxW, RH), &sfL, &white);
        }
    }
}

// ===================================================================
// Render + UpdateLayeredWindow
// ===================================================================
static void Render() {
    if (!g_hwnd || !g_visible) return;
    int W = CalcWidth(), H = WIDGET_H;
    EnsureDIB(W, H);
    memset(g_dibBits, 0, W * H * 4);

    Gdiplus::Graphics g(g_memDC);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

    // Background
    Gdiplus::SolidBrush bg(Gdiplus::Color(200, 15, 15, 30));
    FillRoundRect(g, bg, 0, 0, (float)W, (float)H, 10);

    // Subtle border
    Gdiplus::Pen border(Gdiplus::Color(50, 255, 255, 255), 1.f);
    StrokeRoundRect(g, border, 0.5f, 0.5f, W - 1.f, H - 1.f, 10);

    DrawContent(g, W, H);

    // Composite onto screen
    HDC scr = GetDC(nullptr);
    RECT wr; GetWindowRect(g_hwnd, &wr);
    POINT dst = { wr.left, wr.top };
    SIZE  sz  = { W, H };
    POINT src = { 0, 0 };
    BLENDFUNCTION bf = {}; bf.BlendOp = AC_SRC_OVER;
    bf.SourceConstantAlpha = 255; bf.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(g_hwnd, scr, &dst, &sz, g_memDC, &src, 0, &bf, ULW_ALPHA);
    ReleaseDC(nullptr, scr);
}

// ===================================================================
// Window procedure
// ===================================================================
static LRESULT CALLBACK WndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        SetTimer(hw, TIMER_REFRESH, UPDATE_MS, nullptr);
        return 0;

    case WM_TIMER:
        if (wp == TIMER_REFRESH) {
            UpdateCpu();
            UpdateMem();
            UpdateDisk();
            UpdateNet();
            UpdateLanIP();
            Render();
            if (g_hovCore >= 0 || g_hovVol >= 0) UpdateTip(hw);
        }
        return 0;

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_MOUSEMOVE: {
        if (!g_mouseTracking) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hw, 0 };
            TrackMouseEvent(&tme);
            g_mouseTracking = true;
        }
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        int core = HitTestCore(mx, my);
        int vol  = (core < 0) ? HitTestVol(mx, my) : -1;

        bool changed = (core != g_hovCore) || (vol != g_hovVol);
        g_hovCore = core;
        g_hovVol  = vol;

        if (changed) {
            if (core >= 0 || vol >= 0)
                UpdateTip(hw);
            else
                HideTip(hw);
        } else if (core >= 0 || vol >= 0) {
            UpdateTip(hw);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        g_mouseTracking = false;
        g_hovCore = -1;
        g_hovVol  = -1;
        HideTip(hw);
        return 0;

    case WM_TRAYICON:
        if (LOWORD(lp) == WM_RBUTTONUP)
            ShowTrayMenu(hw);
        else if (LOWORD(lp) == WM_LBUTTONDBLCLK) {
            g_visible = !g_visible;
            ShowWindow(hw, g_visible ? SW_SHOWNOACTIVATE : SW_HIDE);
            if (g_visible) Render();
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_SHOWHIDE:
            g_visible = !g_visible;
            ShowWindow(hw, g_visible ? SW_SHOWNOACTIVATE : SW_HIDE);
            if (g_visible) Render();
            break;
        case IDM_AUTOSTART:
            ToggleAutoStart();
            break;
        case IDM_EXIT:
            PostQuitMessage(0);
            break;
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hw, TIMER_REFRESH);
        RemoveTray();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hw, msg, wp, lp);
}

// ===================================================================
// Prevent multiple instances
// ===================================================================
static HANDLE g_singleMtx = nullptr;
static bool AcquireSingleInstance() {
    g_singleMtx = CreateMutexW(nullptr, TRUE, L"SysMonitor_SingleInstance");
    return g_singleMtx && GetLastError() != ERROR_ALREADY_EXISTS;
}

// ===================================================================
// Entry point
// ===================================================================
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    if (!AcquireSingleInstance()) return 0;

    SetProcessDPIAware();
    g_hInst = hInst;

    // Register window class
    WNDCLASSEXW wc = {}; wc.cbSize = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hInst;
    wc.lpszClassName  = WND_CLASS;
    wc.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    // Init subsystems
    InitGdip();
    InitCpu();
    UpdateMem();
    UpdateDisk();
    InitNet();
    UpdateLanIP();

    // Fixed position: top-right corner with 3px padding
    int scrW = GetSystemMetrics(SM_CXSCREEN);
    int wW   = CalcWidth();
    int wH   = WIDGET_H;
    int posX  = scrW - wW - 3;
    int posY  = 3;

    // Create layered, topmost, tool window (no taskbar entry)
    g_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        WND_CLASS, APP_NAME, WS_POPUP,
        posX, posY, wW, wH,
        nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) return 1;

    AddTray(g_hwnd);
    InitTip(g_hwnd);

    // Start background data thread
    g_shutdownEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_bgThread = CreateThread(nullptr, 0, BgThread, nullptr, 0, nullptr);

    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    Render();

    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup
    SetEvent(g_shutdownEvt);
    WaitForSingleObject(g_bgThread, 5000);
    CloseHandle(g_bgThread);
    CloseHandle(g_shutdownEvt);
    CleanupGdip();
    if (g_singleMtx) CloseHandle(g_singleMtx);
    return 0;
}
