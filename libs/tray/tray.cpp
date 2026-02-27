#include "libs/tray/tray.h"
#include <cmath>

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

void AddTray(HWND hw) {
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

void RemoveTray() {
    Shell_NotifyIcon(NIM_DELETE, &g_nid);
    if (g_trayIcon) DestroyIcon(g_trayIcon);
}

void ShowTrayMenu(HWND hw) {
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

void ToggleAutoStart() {
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
