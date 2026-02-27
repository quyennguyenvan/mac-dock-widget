#include "libs/draw/draw.h"
#include "libs/util/util.h"
#include "libs/layout/layout.h"
#include "libs/gdip/gdip.h"

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

void DrawContent(Gdiplus::Graphics& g, int W, int H) {
    using namespace Gdiplus;

    const float R1 = 6.f;
    const float RH = 18.f;
    const float ROW_GAP = 1.f;
    const float R2 = R1 + RH + ROW_GAP;
    const float R3 = R2 + RH + ROW_GAP;

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

    x += 8; g.DrawLine(&sep, x, 6.f, x, (float)H - 6.f); x += 8;

    {
        float sw = (float)CalcCpuSecW();
        wchar_t cpuBuf[32];
        swprintf_s(cpuBuf, L"CPU  %.0f%%", g_totalCpu);
        g.DrawString(cpuBuf, -1, g_fTitle, RectF(x, R1, 70, RH), &sfL, &accent);
        DrawBar(g, x + 70, R1 + 6, sw - 82, 7, g_totalCpu, UsageCol(g_totalCpu));

        for (int i = 0; i < g_numCores; i++) {
            float bx = x + i * 10.f;
            float by = (float)H - 6.f - 18.f;
            Color uc = UsageCol(g_coreUse[i]);
            BYTE alpha = (BYTE)(80 + g_coreUse[i] * 1.75);
            SolidBrush cb(Color(alpha, uc.GetR(), uc.GetG(), uc.GetB()));
            FillRoundRect(g, cb, bx, by, 8, 18, 2);
        }
        x += sw;
    }

    x += 8; g.DrawLine(&sep, x, 6.f, x, (float)H - 6.f); x += 8;

    {
        float sw = (float)SEC_MEM_W;
        wchar_t uBuf[32], tBuf[32];

        FmtMem(g_ramUsedMB, uBuf, 32); FmtMem(g_ramTotalMB, tBuf, 32);
        wchar_t ramV[64]; swprintf_s(ramV, L"%s / %s", uBuf, tBuf);
        g.DrawString(L"RAM", -1, g_fTitle, RectF(x, R1, 38, RH), &sfL, &accent);
        double ramPct = g_ramTotalMB > 0 ? g_ramUsedMB * 100.0 / g_ramTotalMB : 0;
        DrawBar(g, x + 40, R1 + 7, 100, 6, ramPct, Color(255, 100, 180, 255));
        g.DrawString(ramV, -1, g_fSmall, RectF(x + 144, R1 + 1, sw - 144, RH), &sfL, &dim);

        FmtMem(g_swapUsedMB, uBuf, 32); FmtMem(g_swapTotalMB, tBuf, 32);
        wchar_t swpV[64]; swprintf_s(swpV, L"%s / %s", uBuf, tBuf);
        g.DrawString(L"Swap", -1, g_fTitle, RectF(x, R2, 40, RH), &sfL, &accent);
        double swpPct = g_swapTotalMB > 0 ? g_swapUsedMB * 100.0 / g_swapTotalMB : 0;
        DrawBar(g, x + 42, R2 + 7, 98, 6, swpPct, Color(255, 180, 130, 255));
        g.DrawString(swpV, -1, g_fSmall, RectF(x + 144, R2 + 1, sw - 144, RH), &sfL, &dim);

        wchar_t gpuV[32];
        swprintf_s(gpuV, L"%.0f%%", g_gpuUsagePct);
        g.DrawString(L"GPU", -1, g_fTitle, RectF(x, R3, 40, RH), &sfL, &accent);
        DrawBar(g, x + 42, R3 + 7, 98, 6, g_gpuUsagePct, UsageCol(g_gpuUsagePct));
        g.DrawString(gpuV, -1, g_fSmall, RectF(x + 144, R3 + 1, sw - 144, RH), &sfL, &dim);
        x += sw;
    }

    x += 8; g.DrawLine(&sep, x, 6.f, x, (float)H - 6.f); x += 8;

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

    x += 8; g.DrawLine(&sep, x, 6.f, x, (float)H - 6.f); x += 8;

    {
        float sw = (float)SEC_IPNET_W;
        wchar_t upS[32], dnS[32];
        FmtSpeed(g_netUp,   upS, 32);
        FmtSpeed(g_netDown, dnS, 32);
        wchar_t upL[48], dnL[48];
        swprintf_s(upL, L"\u2191 %s", upS);
        swprintf_s(dnL, L"\u2193 %s", dnS);

        {
            std::lock_guard<std::mutex> lk(g_extMtx);
            g.DrawString(L"IP", -1, g_fTitle, RectF(x, R1, 18, RH), &sfL, &accent);
            g.DrawString(g_ext.ip.c_str(), -1, g_fSmall, RectF(x + 18, R1 + 1, sw - 100, RH), &sfL, &dim);
        }
        g.DrawString(upL, -1, g_fVal, RectF(x, R1, sw, RH), &sfR, &green);

        g.DrawString(L"LAN", -1, g_fTitle, RectF(x, R2, 36, RH), &sfL, &accent);
        g.DrawString(g_lanIP.c_str(), -1, g_fSmall, RectF(x + 36, R2 + 1, sw - 118, RH), &sfL, &dim);
        g.DrawString(dnL, -1, g_fVal, RectF(x, R2, sw, RH), &sfR, &orange);

        x += sw;
    }

    x += 8; g.DrawLine(&sep, x, 6.f, x, (float)H - 6.f); x += 8;

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

void Render() {
    if (!g_hwnd || !g_visible) return;
    int W = CalcWidth(), H = WIDGET_H;
    EnsureDIB(W, H);
    memset(g_dibBits, 0, W * H * 4);

    Gdiplus::Graphics g(g_memDC);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

    Gdiplus::SolidBrush bg(Gdiplus::Color(200, 15, 15, 30));
    FillRoundRect(g, bg, 0, 0, (float)W, (float)H, 10);

    Gdiplus::Pen border(Gdiplus::Color(50, 255, 255, 255), 1.f);
    StrokeRoundRect(g, border, 0.5f, 0.5f, W - 1.f, H - 1.f, 10);

    DrawContent(g, W, H);

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
