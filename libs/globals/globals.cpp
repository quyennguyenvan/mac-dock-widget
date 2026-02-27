// SysMonitor - Global state (definitions)
#include "libs/globals/globals.h"

HWND              g_hwnd          = nullptr;
HINSTANCE         g_hInst         = nullptr;
bool              g_visible       = true;

NOTIFYICONDATA    g_nid           = {};
HICON             g_trayIcon      = nullptr;

ULONG_PTR         g_gdipToken     = 0;
Gdiplus::FontFamily* g_ff         = nullptr;
Gdiplus::Font*    g_fTime         = nullptr;
Gdiplus::Font*    g_fDate         = nullptr;
Gdiplus::Font*    g_fTitle        = nullptr;
Gdiplus::Font*    g_fVal          = nullptr;
Gdiplus::Font*    g_fSmall        = nullptr;
Gdiplus::Font*    g_fTiny         = nullptr;
HDC               g_memDC         = nullptr;
HBITMAP           g_dib           = nullptr;
void*             g_dibBits       = nullptr;
int               g_dibW = 0, g_dibH = 0;

NtQSI_t           g_NtQSI         = nullptr;
int               g_numCores      = 0;
std::vector<PROC_PERF_INFO> g_prevCpu;
std::vector<double> g_coreUse;
double            g_totalCpu      = 0;

ULONGLONG         g_ramTotalMB = 0, g_ramUsedMB = 0;
ULONGLONG         g_swapTotalMB = 0, g_swapUsedMB = 0;

double            g_gpuUsagePct   = 0.0;
ULONGLONG         g_gpuEngPrev    = 0;
ULONGLONG         g_gpuTsPrev     = 0;
LUID              g_gpuLuid       = {};

VolInfo           g_vols[26];
int               g_numVols       = 0;

ULONGLONG         g_netPrevIn = 0, g_netPrevOut = 0;
ULONGLONG         g_netTick = 0;
double            g_netDown = 0, g_netUp = 0;
bool              g_netInit = false;
std::wstring      g_lanIP = L"--";

std::mutex        g_extMtx;
ExtData           g_ext;

HANDLE            g_bgThread      = nullptr;
HANDLE            g_shutdownEvt   = nullptr;

HWND              g_tip           = nullptr;
int               g_hovCore       = -1;
int               g_hovVol         = -1;
bool              g_mouseTracking  = false;
