// SysMonitor - Lightweight Windows System Monitor Widget
// Entry point and window procedure. Feature code lives under libs/.

#include "libs/common/common.h"
#include "libs/globals/globals.h"
#include "libs/cpu/cpu.h"
#include "libs/mem/mem.h"
#include "libs/gpu/gpu.h"
#include "libs/disk/disk.h"
#include "libs/net/net.h"
#include "libs/external/external.h"
#include "libs/tray/tray.h"
#include "libs/gdip/gdip.h"
#include "libs/layout/layout.h"
#include "libs/draw/draw.h"
#include "libs/tooltip/tooltip.h"

static HANDLE g_singleMtx = nullptr;
static bool AcquireSingleInstance() {
    g_singleMtx = CreateMutexW(nullptr, TRUE, L"SysMonitor_SingleInstance");
    return g_singleMtx && GetLastError() != ERROR_ALREADY_EXISTS;
}

static LRESULT CALLBACK WndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        SetTimer(hw, TIMER_REFRESH, UPDATE_MS, nullptr);
        return 0;

    case WM_TIMER:
        if (wp == TIMER_REFRESH) {
            UpdateCpu();
            UpdateMem();
            UpdateGpu();
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

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    if (!AcquireSingleInstance()) return 0;

    SetProcessDPIAware();
    g_hInst = hInst;

    WNDCLASSEXW wc = {}; wc.cbSize = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName  = WND_CLASS;
    wc.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    InitGdip();
    InitCpu();
    UpdateMem();
    InitGpuD3dKmt();
    UpdateGpu();
    UpdateDisk();
    InitNet();
    UpdateLanIP();

    int scrW = GetSystemMetrics(SM_CXSCREEN);
    int wW   = CalcWidth();
    int wH   = WIDGET_H;
    int posX  = scrW - wW - 3;
    int posY  = 3;

    g_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        WND_CLASS, APP_NAME, WS_POPUP,
        posX, posY, wW, wH,
        nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) return 1;

    AddTray(g_hwnd);
    InitTip(g_hwnd);

    g_shutdownEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_bgThread = CreateThread(nullptr, 0, BgThread, nullptr, 0, nullptr);

    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    Render();

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    SetEvent(g_shutdownEvt);
    WaitForSingleObject(g_bgThread, 5000);
    CloseHandle(g_bgThread);
    CloseHandle(g_shutdownEvt);
    CleanupGdip();
    if (g_singleMtx) CloseHandle(g_singleMtx);
    return 0;
}
