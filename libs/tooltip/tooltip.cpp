#include "libs/tooltip/tooltip.h"
#include "libs/util/util.h"
#include "libs/layout/layout.h"

void InitTip(HWND parent) {
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

int HitTestCore(int cx, int cy) {
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

int HitTestVol(int cx, int cy) {
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

void ShowTip(HWND hw, const wchar_t* text) {
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

void HideTip(HWND hw) {
    if (!g_tip) return;
    TOOLINFOW ti = {};
    ti.cbSize = sizeof(ti);
    ti.hwnd   = hw;
    ti.uId    = 1;
    SendMessageW(g_tip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
}

void UpdateTip(HWND hw) {
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
