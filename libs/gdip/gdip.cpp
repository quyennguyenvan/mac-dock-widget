#include "libs/gdip/gdip.h"

void InitGdip() {
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

void CleanupGdip() {
    delete g_fTime; delete g_fDate; delete g_fTitle; delete g_fVal; delete g_fSmall; delete g_fTiny;
    delete g_ff;
    if (g_dib)   DeleteObject(g_dib);
    if (g_memDC) DeleteDC(g_memDC);
    Gdiplus::GdiplusShutdown(g_gdipToken);
}

void EnsureDIB(int w, int h) {
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
