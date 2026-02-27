// SysMonitor - Global state (declarations)
#ifndef SYSMON_GLOBALS_H
#define SYSMON_GLOBALS_H

#include "libs/common/common.h"

// NtQuerySystemInformation types
struct PROC_PERF_INFO {
    LARGE_INTEGER IdleTime;
    LARGE_INTEGER KernelTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER DpcTime;
    LARGE_INTEGER InterruptTime;
    ULONG         InterruptCount;
};
typedef LONG(NTAPI* NtQSI_t)(ULONG, PVOID, ULONG, PULONG);

extern HWND              g_hwnd;
extern HINSTANCE         g_hInst;
extern bool              g_visible;

extern NOTIFYICONDATA    g_nid;
extern HICON             g_trayIcon;

extern ULONG_PTR         g_gdipToken;
extern Gdiplus::FontFamily* g_ff;
extern Gdiplus::Font*    g_fTime;
extern Gdiplus::Font*    g_fDate;
extern Gdiplus::Font*    g_fTitle;
extern Gdiplus::Font*    g_fVal;
extern Gdiplus::Font*    g_fSmall;
extern Gdiplus::Font*    g_fTiny;
extern HDC               g_memDC;
extern HBITMAP           g_dib;
extern void*             g_dibBits;
extern int               g_dibW, g_dibH;

extern NtQSI_t           g_NtQSI;
extern int               g_numCores;
extern std::vector<PROC_PERF_INFO> g_prevCpu;
extern std::vector<double> g_coreUse;
extern double            g_totalCpu;

extern ULONGLONG         g_ramTotalMB, g_ramUsedMB;
extern ULONGLONG         g_swapTotalMB, g_swapUsedMB;

extern double            g_gpuUsagePct;
extern ULONGLONG         g_gpuEngPrev, g_gpuTsPrev;
extern LUID              g_gpuLuid;

extern VolInfo           g_vols[26];
extern int               g_numVols;

extern ULONGLONG         g_netPrevIn, g_netPrevOut;
extern ULONGLONG         g_netTick;
extern double            g_netDown, g_netUp;
extern bool              g_netInit;
extern std::wstring      g_lanIP;

extern std::mutex        g_extMtx;
extern ExtData           g_ext;

extern HANDLE            g_bgThread;
extern HANDLE            g_shutdownEvt;

extern HWND              g_tip;
extern int               g_hovCore, g_hovVol;
extern bool              g_mouseTracking;

#endif // SYSMON_GLOBALS_H
