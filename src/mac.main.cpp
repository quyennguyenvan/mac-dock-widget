// SysMonitor - Lightweight macOS System Monitor Widget
// Single-file Objective-C++ application using Cocoa + Core Graphics.
// Features: transparent always-on-top overlay, menu bar icon, auto-start,
//           per-core CPU, RAM/Swap, disk volumes, network speed, public IP, weather.
// Compile with: clang++ -x objective-c++ -std=c++17 -O2 -framework Cocoa mac.main.cpp -o SysMonitor

#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "libs/mac/mac_globals.h"
#include "libs/mac/metrics_mac.h"
#include "libs/mac/external_mac.h"

// ===================================================================
// Constants (matching Windows layout)
// ===================================================================
static const int    WIDGET_H        = 76;
static const int    BAR_PAD         = 12;
static const int    SEC_SEP         = 18;
static const int    SEC_TIME_W      = 115;
static const int    SEC_MEM_W       = 265;
static const int    SEC_IPNET_W     = 215;
static const int    SEC_WX_W        = 105;
static const CGFloat UPDATE_SEC     = 1.0;

// Widget panel (simulated macOS medium widget)
static const int    WPANEL_W        = 390;
static const int    WPANEL_H        = 170;
static const int    WPANEL_PAD      = 16;
static const CGFloat WPANEL_RADIUS  = 22;

// ===================================================================
// Global state
// ===================================================================

// GDI-like font sizes
static const CGFloat kFTime  = 20;
static const CGFloat kFDate  = 12;
static const CGFloat kFTitle = 13;
static const CGFloat kFVal   = 13;
static const CGFloat kFSmall = 11;

// Tooltip state
static int g_hovCore = -1;
static int g_hovVol  = -1;
static NSWindow    *g_tipWin   = nil;
static NSTextField *g_tipField = nil;

// Window-behind detection for adaptive transparency
static std::atomic<bool> g_windowBehind{false};

// Widget panel docked state
static bool g_widgetDocked = false;
static std::atomic<bool> g_wpanelBehind{false};

// Status item mode (icon vs text stats in macOS top bar)
static bool g_statusTextMode = false;

// ===================================================================
// Utility: format helpers
// ===================================================================
static std::string FmtSpeed(double bps) {
    char buf[32];
    if (bps < 1024.0)          snprintf(buf, 32, "%.0f B/s", bps);
    else if (bps < 1048576.0)  snprintf(buf, 32, "%.1f KB/s", bps / 1024.0);
    else if (bps < 1073741824.0) snprintf(buf, 32, "%.1f MB/s", bps / 1048576.0);
    else                       snprintf(buf, 32, "%.2f GB/s", bps / 1073741824.0);
    return buf;
}

static std::string FmtMem(uint64_t mb) {
    char buf[32];
    if (mb >= 1024) snprintf(buf, 32, "%.1f GB", (double)mb / 1024.0);
    else            snprintf(buf, 32, "%llu MB", (unsigned long long)mb);
    return buf;
}

static std::string FmtDisk(double gb) {
    char buf[32];
    if (gb >= 1024.0) snprintf(buf, 32, "%.1f TB", gb / 1024.0);
    else              snprintf(buf, 32, "%.1f GB", gb);
    return buf;
}

// ===================================================================
// Detect if any window overlaps behind the widget
// ===================================================================
static void UpdateWindowBehind(NSWindow *myWindow, std::atomic<bool> *target = &g_windowBehind) {
    if (!myWindow.isVisible) return;
    CGWindowID myWinID = (CGWindowID)[myWindow windowNumber];
    NSRect myFrame = myWindow.frame;

    CGFloat mainH = [NSScreen screens][0].frame.size.height;
    CGRect myBounds = CGRectMake(myFrame.origin.x,
        mainH - myFrame.origin.y - myFrame.size.height,
        myFrame.size.width, myFrame.size.height);

    CFArrayRef windowList = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
        kCGNullWindowID);
    if (!windowList) { target->store(false); return; }

    bool found = false, passedSelf = false;
    CFIndex count = CFArrayGetCount(windowList);

    for (CFIndex i = 0; i < count; i++) {
        CFDictionaryRef info = (CFDictionaryRef)CFArrayGetValueAtIndex(windowList, i);
        CFNumberRef numRef = (CFNumberRef)CFDictionaryGetValue(info, kCGWindowNumber);
        if (!numRef) continue;
        CGWindowID winID;
        CFNumberGetValue(numRef, kCFNumberIntType, &winID);
        if (winID == myWinID) { passedSelf = true; continue; }
        if (!passedSelf) continue;

        CFNumberRef layerRef = (CFNumberRef)CFDictionaryGetValue(info, kCGWindowLayer);
        if (layerRef) {
            int layer = 0;
            CFNumberGetValue(layerRef, kCFNumberIntType, &layer);
            if (layer < 0) continue;
        }

        CFDictionaryRef boundsDict = (CFDictionaryRef)CFDictionaryGetValue(info, kCGWindowBounds);
        if (!boundsDict) continue;
        CGRect bounds;
        if (!CGRectMakeWithDictionaryRepresentation(boundsDict, &bounds)) continue;

        if (CGRectIntersectsRect(myBounds, bounds)) {
            found = true;
            break;
        }
    }

    CFRelease(windowList);
    target->store(found);
}

// ===================================================================
// Widget dimension calculation
// ===================================================================
static int CalcCpuSecW() {
    int blocksW = g_numCores * 10;
    return (blocksW > 110 ? blocksW : 110) + 12;
}

static const int SEC_DISK_COL_W = 95;
static int CalcDiskSecW() {
    int n = (int)g_vols.size();
    int cols = (n + 1) / 2;
    if (cols < 1) cols = 1;
    return cols * SEC_DISK_COL_W;
}

static int CalcWidth() {
    return BAR_PAD + SEC_TIME_W + SEC_SEP + CalcCpuSecW() + SEC_SEP
         + SEC_MEM_W + SEC_SEP + CalcDiskSecW() + SEC_SEP
         + SEC_IPNET_W + SEC_SEP + SEC_WX_W + BAR_PAD;
}

// ===================================================================
// Color helpers
// ===================================================================
static NSColor* RGBA(int r, int g, int b, int a = 255) {
    return [NSColor colorWithCalibratedRed:r/255.0 green:g/255.0 blue:b/255.0 alpha:a/255.0];
}

static NSColor* UsageCol(double p) {
    if (p < 50) return RGBA(0, 230, 118);
    if (p < 80) return RGBA(255, 171, 0);
    return RGBA(255, 23, 68);
}

// ===================================================================
// Drawing helpers
// ===================================================================
static void FillRoundRect(CGContextRef ctx, CGFloat x, CGFloat y, CGFloat w, CGFloat h,
                          CGFloat r, NSColor *color) {
    CGRect rc = CGRectMake(x, y, w, h);
    CGPathRef path = CGPathCreateWithRoundedRect(rc, r, r, nullptr);
    CGFloat comps[4];
    [[color colorUsingColorSpace:[NSColorSpace sRGBColorSpace]] getComponents:comps];
    CGContextSetRGBFillColor(ctx, comps[0], comps[1], comps[2], comps[3]);
    CGContextAddPath(ctx, path);
    CGContextFillPath(ctx);
    CGPathRelease(path);
}

static void DrawBar(CGContextRef ctx, CGFloat x, CGFloat y, CGFloat w, CGFloat h,
                    double pct, NSColor *color) {
    FillRoundRect(ctx, x, y, w, h, h/2, RGBA(255, 255, 255, 40));
    CGFloat fw = (CGFloat)(w * pct / 100.0);
    if (fw > h) FillRoundRect(ctx, x, y, fw, h, h/2, color);
}

static void DrawText(NSString *text, CGFloat x, CGFloat y, CGFloat w, CGFloat h,
                     NSFont *font, NSColor *color, NSTextAlignment align) {
    NSMutableParagraphStyle *ps = [[NSMutableParagraphStyle alloc] init];
    ps.alignment = align;
    ps.lineBreakMode = NSLineBreakByClipping;
    NSDictionary *attrs = @{
        NSFontAttributeName: font,
        NSForegroundColorAttributeName: color,
        NSParagraphStyleAttributeName: ps
    };
    [text drawInRect:NSMakeRect(x, y, w, h) withAttributes:attrs];
}

static void DrawCircleGauge(CGContextRef ctx, CGFloat cx, CGFloat cy, CGFloat radius,
                            CGFloat lineW, double pct, NSColor *color,
                            NSString *label, NSFont *font, NSColor *textColor) {
    CGFloat startAngle = M_PI_2;
    CGFloat fullSweep  = -2.0 * M_PI;
    CGFloat fgSweep    = fullSweep * pct / 100.0;

    // Background ring
    CGContextSetLineWidth(ctx, lineW);
    CGFloat bgC[4] = {1, 1, 1, 0.12};
    CGContextSetRGBStrokeColor(ctx, bgC[0], bgC[1], bgC[2], bgC[3]);
    CGContextAddArc(ctx, cx, cy, radius, startAngle, startAngle + fullSweep, 1);
    CGContextStrokePath(ctx);

    // Foreground arc
    if (pct > 0.5) {
        CGFloat comps[4];
        [[color colorUsingColorSpace:[NSColorSpace sRGBColorSpace]] getComponents:comps];
        CGContextSetRGBStrokeColor(ctx, comps[0], comps[1], comps[2], comps[3]);
        CGContextSetLineCap(ctx, kCGLineCapRound);
        CGContextAddArc(ctx, cx, cy, radius, startAngle, startAngle + fgSweep, 1);
        CGContextStrokePath(ctx);
        CGContextSetLineCap(ctx, kCGLineCapButt);
    }

    // Centered label
    NSDictionary *attrs = @{NSFontAttributeName: font, NSForegroundColorAttributeName: textColor};
    NSSize sz = [label sizeWithAttributes:attrs];
    [label drawAtPoint:NSMakePoint(cx - sz.width / 2, cy - sz.height / 2) withAttributes:attrs];
}

// ===================================================================
// Hit-testing for tooltips
// ===================================================================
static int HitTestCore(CGFloat mx, CGFloat my) {
    CGFloat cpuX = BAR_PAD + SEC_TIME_W + 16;
    CGFloat blockY = 42;
    for (int i = 0; i < g_numCores; i++) {
        CGFloat bx = cpuX + i * 10.0;
        if (mx >= bx && mx < bx + 10 && my >= blockY && my < blockY + 20)
            return i;
    }
    return -1;
}

static int HitTestVol(CGFloat mx, CGFloat my) {
    CGFloat diskX = BAR_PAD + SEC_TIME_W + 16 + CalcCpuSecW() + 16 + SEC_MEM_W + 16;
    CGFloat colW = SEC_DISK_COL_W;
    int n = (int)g_vols.size();
    for (int v = 0; v < n; v++) {
        int col = v / 2;
        int row = v % 2;
        CGFloat rx = diskX + col * colW;
        CGFloat ry = (row == 0) ? 9 : 42;
        if (mx >= rx && mx < rx + colW && my >= ry && my < ry + 24)
            return v;
    }
    return -1;
}

// Hit-test per-core bars in the dock widget panel
static int HitTestCorePanel(CGFloat mx, CGFloat my, CGFloat viewW, CGFloat viewH) {
    (void)viewW; // currently unused but kept for symmetry/possible future layout tweaks
    CGFloat pad = WPANEL_PAD;
    CGFloat leftW = 135;
    int maxVisual = (int)((leftW - 4) / 8);
    int maxCores = g_numCores;
    if (maxCores > maxVisual) maxCores = maxVisual;
    CGFloat coreY = viewH - pad - 22;
    CGFloat coreX = pad;
    CGFloat barW = 6;
    CGFloat barH = 18;
    for (int i = 0; i < maxCores; i++) {
        CGFloat bx = coreX + i * 8.0;
        if (mx >= bx && mx < bx + barW && my >= coreY && my < coreY + barH)
            return i;
    }
    return -1;
}

// ===================================================================
// Custom tooltip window (NSView.toolTip unreliable on borderless windows)
// ===================================================================
static void ShowTip(NSString *text, NSPoint screenPt) {
    if (!g_tipWin) {
        g_tipWin = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 10, 10)
            styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:YES];
        g_tipWin.backgroundColor = [NSColor colorWithCalibratedRed:0.08 green:0.08 blue:0.14 alpha:0.95];
        g_tipWin.opaque = NO;
        g_tipWin.level = NSScreenSaverWindowLevel;
        g_tipWin.ignoresMouseEvents = YES;
        g_tipWin.hasShadow = YES;

        g_tipField = [[NSTextField alloc] initWithFrame:NSMakeRect(8, 6, 200, 20)];
        g_tipField.bezeled = NO;
        g_tipField.drawsBackground = NO;
        g_tipField.editable = NO;
        g_tipField.selectable = NO;
        g_tipField.textColor = [NSColor colorWithCalibratedRed:0.94 green:0.94 blue:1.0 alpha:1.0];
        g_tipField.font = [NSFont systemFontOfSize:11];
        g_tipField.lineBreakMode = NSLineBreakByClipping;
        g_tipField.maximumNumberOfLines = 0;
        [g_tipWin.contentView addSubview:g_tipField];
    }

    g_tipField.stringValue = text;
    g_tipField.preferredMaxLayoutWidth = 250;
    [g_tipField sizeToFit];
    NSSize fs = g_tipField.frame.size;
    CGFloat winW = fs.width + 16, winH = fs.height + 12;
    g_tipField.frame = NSMakeRect(8, 6, fs.width, fs.height);
    [g_tipWin setContentSize:NSMakeSize(winW, winH)];
    [g_tipWin setFrameOrigin:NSMakePoint(screenPt.x + 14, screenPt.y - winH - 4)];
    [g_tipWin orderFront:nil];
}

static void HideTip() {
    [g_tipWin orderOut:nil];
}

// ===================================================================
// MonitorView - custom NSView
// ===================================================================
@interface MonitorView : NSView
@property (nonatomic) NSTrackingArea *trackArea;
@end

@implementation MonitorView

- (BOOL)isFlipped { return YES; }

- (void)updateTrackingAreas {
    if (self.trackArea) [self removeTrackingArea:self.trackArea];
    self.trackArea = [[NSTrackingArea alloc]
        initWithRect:self.bounds
        options:(NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved | NSTrackingActiveAlways)
        owner:self userInfo:nil];
    [self addTrackingArea:self.trackArea];
    [super updateTrackingAreas];
}

- (void)mouseMoved:(NSEvent *)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    int core = HitTestCore(p.x, p.y);
    int vol  = (core < 0) ? HitTestVol(p.x, p.y) : -1;
    g_hovCore = core;
    g_hovVol  = vol;

    if (core >= 0 && core < (int)g_coreUse.size()) {
        char buf[64];
        snprintf(buf, 64, "Core %d: %.1f%% usage", core, g_coreUse[core]);
        ShowTip([NSString stringWithUTF8String:buf], [NSEvent mouseLocation]);
    } else if (vol >= 0 && vol < (int)g_vols.size()) {
        double pct = g_vols[vol].totalGB > 0 ?
            g_vols[vol].usedGB * 100.0 / g_vols[vol].totalGB : 0;
        double freeGB = g_vols[vol].totalGB - g_vols[vol].usedGB;
        char buf[256];
        snprintf(buf, 256, "Volume: %s\nUsed: %s / %s (%.1f%%)\nFree: %s",
                 g_vols[vol].mount.c_str(),
                 FmtDisk(g_vols[vol].usedGB).c_str(),
                 FmtDisk(g_vols[vol].totalGB).c_str(), pct,
                 FmtDisk(freeGB).c_str());
        ShowTip([NSString stringWithUTF8String:buf], [NSEvent mouseLocation]);
    } else {
        HideTip();
    }
}

- (void)mouseExited:(NSEvent *)event {
    g_hovCore = -1;
    g_hovVol  = -1;
    HideTip();
}

- (void)drawRect:(NSRect)dirtyRect {
    CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
    int W = (int)self.bounds.size.width;
    int H = (int)self.bounds.size.height;

    // Clear
    CGContextClearRect(ctx, self.bounds);

    // Background rounded rect — 90% transparent when a window is behind
    bool behind = g_windowBehind.load();
    FillRoundRect(ctx, 0, 0, W, H, 10, RGBA(15, 15, 30, behind ? 25 : 200));

    // Border
    CGRect brc = CGRectMake(0.5, 0.5, W - 1, H - 1);
    CGPathRef bp = CGPathCreateWithRoundedRect(brc, 10, 10, nullptr);
    CGContextSetRGBStrokeColor(ctx, 1, 1, 1, behind ? 0.05 : 0.2);
    CGContextSetLineWidth(ctx, 1);
    CGContextAddPath(ctx, bp);
    CGContextStrokePath(ctx);
    CGPathRelease(bp);

    // Fonts
    NSFont *fTime  = [NSFont monospacedDigitSystemFontOfSize:kFTime weight:NSFontWeightBold];
    NSFont *fDate  = [NSFont systemFontOfSize:kFDate];
    NSFont *fTitle = [NSFont systemFontOfSize:kFTitle weight:NSFontWeightSemibold];
    NSFont *fVal   = [NSFont monospacedDigitSystemFontOfSize:kFVal weight:NSFontWeightRegular];
    NSFont *fSmall = [NSFont monospacedDigitSystemFontOfSize:kFSmall weight:NSFontWeightRegular];

    NSColor *white  = RGBA(245, 245, 255);
    NSColor *dim    = RGBA(210, 215, 235);
    NSColor *accent = RGBA(100, 200, 255);
    NSColor *green  = RGBA(0, 230, 118);
    NSColor *orange = RGBA(255, 100, 70);

    CGFloat R1 = 9, R2 = 42, RH = 24;
    CGFloat x = BAR_PAD;

    // ---- Section 1: Date & Time ----
    {
        CGFloat sw = SEC_TIME_W;
        NSDateFormatter *df = [[NSDateFormatter alloc] init];
        df.dateFormat = @"EEE, MMM d, yyyy";
        NSString *dateStr = [df stringFromDate:[NSDate date]];
        DrawText(dateStr, x, R1, sw, RH, fDate, dim, NSTextAlignmentCenter);

        df.dateFormat = @"HH:mm:ss";
        NSString *timeStr = [df stringFromDate:[NSDate date]];
        DrawText(timeStr, x, R2 - 2, sw, RH + 4, fTime, white, NSTextAlignmentCenter);
        x += sw;
    }

    // Separator
    CGContextSetRGBStrokeColor(ctx, 1, 1, 1, 0.16);
    x += 8; CGContextMoveToPoint(ctx, x, 8); CGContextAddLineToPoint(ctx, x, H-8);
    CGContextStrokePath(ctx); x += 8;

    // ---- Section 2: CPU ----
    {
        CGFloat sw = CalcCpuSecW();
        char cpuBuf[32]; snprintf(cpuBuf, 32, "CPU  %.0f%%", g_totalCpu);
        DrawText([NSString stringWithUTF8String:cpuBuf], x, R1, 70, RH, fTitle, accent, NSTextAlignmentLeft);
        DrawBar(ctx, x + 70, R1 + 6, sw - 82, 7, g_totalCpu, UsageCol(g_totalCpu));

        CGFloat barH = 20;
        CGFloat barY = R2;
        NSFont *fIdx = [NSFont monospacedDigitSystemFontOfSize:7 weight:NSFontWeightRegular];
        for (int i = 0; i < g_numCores; i++) {
            CGFloat bx = x + i * 10.0;
            FillRoundRect(ctx, bx, barY, 8, barH, 2, RGBA(255, 255, 255, 25));
            CGFloat fillH = barH * g_coreUse[i] / 100.0;
            if (fillH >= 2)
                FillRoundRect(ctx, bx, barY + barH - fillH, 8, fillH, 2, UsageCol(g_coreUse[i]));
            if (g_numCores <= 16 || i % 2 == 0) {
                char idx[4]; snprintf(idx, 4, "%d", i);
                DrawText([NSString stringWithUTF8String:idx], bx - 1, barY + barH + 1, 10, 9,
                         fIdx, RGBA(180, 180, 200), NSTextAlignmentCenter);
            }
        }
        x += sw;
    }

    // Separator
    x += 8; CGContextMoveToPoint(ctx, x, 8); CGContextAddLineToPoint(ctx, x, H-8);
    CGContextStrokePath(ctx); x += 8;

    // ---- Section 3: Memory ----
    {
        CGFloat sw = SEC_MEM_W;
        std::string ramU = FmtMem(g_ramUsedMB), ramT = FmtMem(g_ramTotalMB);
        char ramV[64]; snprintf(ramV, 64, "%s / %s", ramU.c_str(), ramT.c_str());
        DrawText(@"RAM", x, R1, 38, RH, fTitle, accent, NSTextAlignmentLeft);
        double ramPct = g_ramTotalMB > 0 ? (double)g_ramUsedMB * 100.0 / g_ramTotalMB : 0;
        DrawBar(ctx, x + 40, R1 + 7, 100, 6, ramPct, RGBA(100, 180, 255));
        DrawText([NSString stringWithUTF8String:ramV], x + 144, R1 + 1, sw - 144, RH, fSmall, dim, NSTextAlignmentLeft);

        std::string swpU = FmtMem(g_swapUsedMB), swpT = FmtMem(g_swapTotalMB);
        char swpV[64]; snprintf(swpV, 64, "%s / %s", swpU.c_str(), swpT.c_str());
        DrawText(@"Swap", x, R2, 40, RH, fTitle, accent, NSTextAlignmentLeft);
        double swpPct = g_swapTotalMB > 0 ? (double)g_swapUsedMB * 100.0 / g_swapTotalMB : 0;
        DrawBar(ctx, x + 42, R2 + 7, 98, 6, swpPct, RGBA(180, 130, 255));
        DrawText([NSString stringWithUTF8String:swpV], x + 144, R2 + 1, sw - 144, RH, fSmall, dim, NSTextAlignmentLeft);
        x += sw;
    }

    // Separator
    x += 8; CGContextMoveToPoint(ctx, x, 8); CGContextAddLineToPoint(ctx, x, H-8);
    CGContextStrokePath(ctx); x += 8;

    // ---- Section: Disk Volumes ----
    {
        CGFloat colW = SEC_DISK_COL_W;
        int n = (int)g_vols.size();
        for (int v = 0; v < n; v++) {
            int col = v / 2;
            int row = v % 2;
            CGFloat cx = x + col * colW;
            CGFloat cy = (row == 0) ? R1 : R2;

            std::string lbl = (g_vols[v].letter == '/') ? "/:" : std::string(1, g_vols[v].letter) + ":";
            DrawText([NSString stringWithUTF8String:lbl.c_str()], cx, cy, 22, RH, fTitle, accent, NSTextAlignmentLeft);

            double pct = g_vols[v].totalGB > 0 ? g_vols[v].usedGB * 100.0 / g_vols[v].totalGB : 0;
            NSColor *bc = pct < 80 ? RGBA(100, 180, 255) : RGBA(255, 80, 60);
            DrawBar(ctx, cx + 24, cy + 7, 35, 6, pct, bc);

            char pL[8]; snprintf(pL, 8, "%.0f%%", pct);
            DrawText([NSString stringWithUTF8String:pL], cx + 62, cy + 1, 32, RH, fSmall, bc, NSTextAlignmentLeft);
        }
        x += CalcDiskSecW();
    }

    // Separator
    x += 8; CGContextMoveToPoint(ctx, x, 8); CGContextAddLineToPoint(ctx, x, H-8);
    CGContextStrokePath(ctx); x += 8;

    // ---- Section: IP + Network ----
    {
        CGFloat sw = SEC_IPNET_W;
        std::string upS = "\xe2\x86\x91 " + FmtSpeed(g_netUp);   // ↑
        std::string dnS = "\xe2\x86\x93 " + FmtSpeed(g_netDown); // ↓

        {
            std::lock_guard<std::mutex> lk(g_extMtx);
            DrawText(@"IP", x, R1, 18, RH, fTitle, accent, NSTextAlignmentLeft);
            DrawText([NSString stringWithUTF8String:g_ext.ip.c_str()], x + 18, R1 + 1, sw - 100, RH, fSmall, dim, NSTextAlignmentLeft);
        }
        DrawText([NSString stringWithUTF8String:upS.c_str()], x, R1, sw, RH, fVal, green, NSTextAlignmentRight);

        DrawText(@"LAN", x, R2, 36, RH, fTitle, accent, NSTextAlignmentLeft);
        DrawText([NSString stringWithUTF8String:g_lanIP.c_str()], x + 36, R2 + 1, sw - 118, RH, fSmall, dim, NSTextAlignmentLeft);
        DrawText([NSString stringWithUTF8String:dnS.c_str()], x, R2, sw, RH, fVal, orange, NSTextAlignmentRight);
        x += sw;
    }

    // Separator
    x += 8; CGContextMoveToPoint(ctx, x, 8); CGContextAddLineToPoint(ctx, x, H-8);
    CGContextStrokePath(ctx); x += 8;

    // ---- Section: Location & Weather ----
    {
        std::lock_guard<std::mutex> lk(g_extMtx);
        CGFloat wxW = SEC_WX_W;
        std::string loc;
        if (g_ext.loaded) {
            loc = g_ext.city;
            if (!g_ext.country.empty()) loc += ", " + g_ext.country;
        } else {
            loc = "Loading...";
        }
        DrawText([NSString stringWithUTF8String:loc.c_str()], x, R1, wxW, RH, fTitle, accent, NSTextAlignmentLeft);

        if (g_ext.loaded && g_ext.wcode >= 0) {
            double f = g_ext.temp * 9.0 / 5.0 + 32.0;
            char wL[128];
            snprintf(wL, 128, "%s %.0f\u00B0C/%.0f\u00B0F", g_ext.wdesc.c_str(), g_ext.temp, f);
            DrawText([NSString stringWithUTF8String:wL], x, R2, wxW, RH, fVal, white, NSTextAlignmentLeft);
        }
    }
}

@end

// ===================================================================
// WidgetPanelView - macOS medium widget style
// ===================================================================
@interface WidgetPanelView : NSView
@end

@implementation WidgetPanelView

- (BOOL)isFlipped { return YES; }

- (void)drawRect:(NSRect)dirtyRect {
    CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
    CGFloat W = self.bounds.size.width;
    CGFloat H = self.bounds.size.height;

    CGContextClearRect(ctx, self.bounds);

    // Widget background — 90% transparent when a window is behind
    bool wpBehind = g_wpanelBehind.load();
    FillRoundRect(ctx, 0, 0, W, H, WPANEL_RADIUS, RGBA(28, 28, 32, wpBehind ? 25 : 220));
    CGRect brc = CGRectMake(0.5, 0.5, W - 1, H - 1);
    CGPathRef bp = CGPathCreateWithRoundedRect(brc, WPANEL_RADIUS, WPANEL_RADIUS, nullptr);
    CGContextSetRGBStrokeColor(ctx, 1, 1, 1, wpBehind ? 0.03 : 0.1);
    CGContextSetLineWidth(ctx, 0.5);
    CGContextAddPath(ctx, bp);
    CGContextStrokePath(ctx);
    CGPathRelease(bp);

    // Fonts
    NSFont *fLarge = [NSFont monospacedDigitSystemFontOfSize:22 weight:NSFontWeightBold];
    NSFont *fMed   = [NSFont systemFontOfSize:13 weight:NSFontWeightSemibold];
    NSFont *fVal   = [NSFont monospacedDigitSystemFontOfSize:12 weight:NSFontWeightRegular];
    NSFont *fSmall = [NSFont monospacedDigitSystemFontOfSize:10 weight:NSFontWeightRegular];
    NSFont *fGauge = [NSFont monospacedDigitSystemFontOfSize:13 weight:NSFontWeightBold];
    NSFont *fGaugeS = [NSFont monospacedDigitSystemFontOfSize:9 weight:NSFontWeightBold];
    NSFont *fLabel = [NSFont systemFontOfSize:8 weight:NSFontWeightMedium];

    NSColor *white  = RGBA(245, 245, 255);
    NSColor *dim    = RGBA(200, 205, 220);
    NSColor *accent = RGBA(100, 200, 255);
    NSColor *green  = RGBA(0, 230, 118);
    NSColor *orange = RGBA(255, 100, 70);

    CGFloat pad = WPANEL_PAD;
    CGFloat sepX;

    // ===== LEFT SECTION: CPU gauge + RAM/Swap gauges + core bars =====
    CGFloat leftW = 135;
    {
        // CPU circle gauge (large)
        CGFloat cpuR = 30;
        CGFloat cpuCX = pad + cpuR + 4;
        CGFloat cpuCY = pad + cpuR + 12;
        char cpuLbl[8]; snprintf(cpuLbl, 8, "%.0f%%", g_totalCpu);
        DrawCircleGauge(ctx, cpuCX, cpuCY, cpuR, 5.0, g_totalCpu,
                        UsageCol(g_totalCpu),
                        [NSString stringWithUTF8String:cpuLbl], fGauge, white);
        DrawText(@"CPU", pad, cpuCY + cpuR + 2, cpuR * 2 + 8, 12, fLabel, dim, NSTextAlignmentCenter);

        // RAM circle gauge (small)
        double ramPct = g_ramTotalMB > 0 ? (double)g_ramUsedMB * 100.0 / g_ramTotalMB : 0;
        CGFloat smR = 17;
        CGFloat ramCX = pad + cpuR * 2 + 20 + smR;
        CGFloat ramCY = pad + smR + 2;
        char ramLbl[8]; snprintf(ramLbl, 8, "%.0f%%", ramPct);
        DrawCircleGauge(ctx, ramCX, ramCY, smR, 3.5, ramPct,
                        RGBA(100, 180, 255),
                        [NSString stringWithUTF8String:ramLbl], fGaugeS, white);
        DrawText(@"RAM", ramCX - smR - 2, ramCY + smR + 2, smR * 2 + 4, 12, fLabel, dim, NSTextAlignmentCenter);

        // Swap circle gauge (small)
        double swpPct = g_swapTotalMB > 0 ? (double)g_swapUsedMB * 100.0 / g_swapTotalMB : 0;
        CGFloat swpCY = ramCY + smR * 2 + 22;
        char swpLbl[8]; snprintf(swpLbl, 8, "%.0f%%", swpPct);
        DrawCircleGauge(ctx, ramCX, swpCY, smR, 3.5, swpPct,
                        RGBA(180, 130, 255),
                        [NSString stringWithUTF8String:swpLbl], fGaugeS, white);
        DrawText(@"Swap", ramCX - smR - 2, swpCY + smR + 2, smR * 2 + 4, 12, fLabel, dim, NSTextAlignmentCenter);

        // Per-core bars and disk bar on the same row
        int maxCores = std::min(g_numCores, (int)((leftW - 4) / 8));
        CGFloat coreY = H - pad - 22;
        CGFloat coreX = pad;
        for (int i = 0; i < maxCores; i++) {
            CGFloat bx = coreX + i * 8.0;
            FillRoundRect(ctx, bx, coreY, 6, 18, 2, RGBA(255, 255, 255, 20));
            CGFloat fillH = 18.0 * g_coreUse[i] / 100.0;
            if (fillH >= 2)
                FillRoundRect(ctx, bx, coreY + 18 - fillH, 6, fillH, 2, UsageCol(g_coreUse[i]));
        }
    }

    // Separator
    sepX = pad + leftW;
    CGContextSetRGBStrokeColor(ctx, 1, 1, 1, 0.12);
    CGContextSetLineWidth(ctx, 0.5);
    CGContextMoveToPoint(ctx, sepX, pad + 4);
    CGContextAddLineToPoint(ctx, sepX, H - pad - 4);
    CGContextStrokePath(ctx);

    // Disk usage bar (same row as CPU core bars) — primary volume only (e.g. Data), not sum of all
    double diskUsed = 0, diskTotal = 0;
    for (const auto& v : g_vols) {
        if (v.letter == '/' || v.mount == "/" || v.mount == "/System/Volumes/Data") {
            diskUsed = v.usedGB;
            diskTotal = v.totalGB;
            break;
        }
    }
    if (diskTotal == 0 && !g_vols.empty()) {
        diskUsed = g_vols[0].usedGB;
        diskTotal = g_vols[0].totalGB;
    }
    double diskPct = (diskTotal > 0) ? (diskUsed * 100.0 / diskTotal) : 0;
    double diskFreeGB = diskTotal - diskUsed;
    CGFloat coreY = H - pad - 22;
    CGFloat diskBarH = 12;
    CGFloat diskBarY = coreY + (18 - diskBarH) / 2.0;
    CGFloat diskBarX = sepX + 8;
    CGFloat diskBarW = W - diskBarX - pad - 2;
    FillRoundRect(ctx, diskBarX, diskBarY, diskBarW, diskBarH, diskBarH/2, RGBA(255, 255, 255, 25));
    if (diskPct > 0) {
        CGFloat fillW = diskBarW * diskPct / 100.0;
        if (fillW >= diskBarH)
            FillRoundRect(ctx, diskBarX, diskBarY, fillW, diskBarH, diskBarH/2, UsageCol(diskPct));
    }
    char diskPctStr[80];
    snprintf(diskPctStr, 80, "%.0f%% - %.1f / %.1f GB", diskPct, diskFreeGB, diskTotal);
    for (char *p = diskPctStr; *p; p++) if (*p == '.') *p = ',';
    DrawText([NSString stringWithUTF8String:diskPctStr], diskBarX, diskBarY, diskBarW, diskBarH, fSmall, white, NSTextAlignmentCenter);

    // ===== MIDDLE SECTION: Date/Time + IP/Network =====
    CGFloat midX = sepX + 10;
    CGFloat midW = 135;
    {
        // Date
        NSDateFormatter *df = [[NSDateFormatter alloc] init];
        df.dateFormat = @"EEE, MMM d, yyyy";
        NSString *dateStr = [df stringFromDate:[NSDate date]];
        DrawText(dateStr, midX, pad, midW, 16, fVal, dim, NSTextAlignmentCenter);

        // Time
        df.dateFormat = @"HH:mm:ss";
        NSString *timeStr = [df stringFromDate:[NSDate date]];
        DrawText(timeStr, midX, pad + 18, midW, 30, fLarge, white, NSTextAlignmentCenter);

        // Public IP then LAN IP (stacked), then throughput alongside
        CGFloat netY = pad + 58;
        {
            std::lock_guard<std::mutex> lk(g_extMtx);
            DrawText([NSString stringWithUTF8String:g_ext.ip.c_str()],
                     midX, netY + 1, midW, 16, fSmall, dim, NSTextAlignmentLeft);
        }
        DrawText([NSString stringWithUTF8String:g_lanIP.c_str()],
                 midX, netY + 18, midW, 16, fSmall, dim, NSTextAlignmentLeft);

        std::string upS = "\xe2\x86\x91 " + FmtSpeed(g_netUp);
        std::string dnS = "\xe2\x86\x93 " + FmtSpeed(g_netDown);
        NSString *upNS = [NSString stringWithUTF8String:(upS + " - ").c_str()];
        NSDictionary *tattrs = @{NSFontAttributeName: fVal};
        CGFloat upW = [upNS sizeWithAttributes:tattrs].width;
        DrawText([NSString stringWithUTF8String:upS.c_str()],
                 midX, netY + 36, midW, 16, fVal, green, NSTextAlignmentLeft);
        DrawText([NSString stringWithUTF8String:dnS.c_str()],
                 midX + upW, netY + 36, midW - upW, 16, fVal, RGBA(255, 70, 70), NSTextAlignmentLeft);
    }

    // Separator
    sepX = midX + midW;
    CGContextSetRGBStrokeColor(ctx, 1, 1, 1, 0.12);
    CGContextMoveToPoint(ctx, sepX, pad + 4);
    CGContextAddLineToPoint(ctx, sepX, H - pad - 4);
    CGContextStrokePath(ctx);

    // ===== RIGHT SECTION: Location + Weather =====
    CGFloat rtX = sepX + 3;
    CGFloat rtW = W - rtX - 3;
    {
        NSFont *fLocTitle = [NSFont systemFontOfSize:11 weight:NSFontWeightSemibold];
        NSFont *fWxDesc   = [NSFont systemFontOfSize:10 weight:NSFontWeightRegular];
        NSFont *fTempBig  = [NSFont monospacedDigitSystemFontOfSize:18 weight:NSFontWeightBold];
        NSFont *fTempSm   = [NSFont monospacedDigitSystemFontOfSize:11 weight:NSFontWeightRegular];

        std::lock_guard<std::mutex> lk(g_extMtx);
        std::string loc;
        if (g_ext.loaded) {
            loc = g_ext.city;
            if (!g_ext.country.empty()) loc += ", " + g_ext.country;
        } else {
            loc = "Loading...";
        }
        DrawText([NSString stringWithUTF8String:loc.c_str()],
                 rtX, pad, rtW, 16, fLocTitle, accent, NSTextAlignmentCenter);

        if (g_ext.loaded && g_ext.wcode >= 0) {
            DrawText([NSString stringWithUTF8String:g_ext.wdesc.c_str()],
                     rtX, pad + 34, rtW, 14, fWxDesc, white, NSTextAlignmentCenter);

            char tempC[16]; snprintf(tempC, 16, "%.0f\u00B0C", g_ext.temp);
            DrawText([NSString stringWithUTF8String:tempC],
                     rtX, pad + 56, rtW, 22, fTempBig, white, NSTextAlignmentCenter);

            double f = g_ext.temp * 9.0 / 5.0 + 32.0;
            char tempF[16]; snprintf(tempF, 16, "%.0f\u00B0F", f);
            DrawText([NSString stringWithUTF8String:tempF],
                     rtX, pad + 82, rtW, 16, fTempSm, dim, NSTextAlignmentCenter);
        } else {
            DrawText(@"--", rtX, pad + 56, rtW, 22, fTempBig, dim, NSTextAlignmentCenter);
        }
    }
}

@end

// ===================================================================
// Auto-start (Launch Agent)
// ===================================================================
static NSString* LaunchAgentPath() {
    return [NSHomeDirectory() stringByAppendingPathComponent:@"Library/LaunchAgents/com.sysmonitor.widget.plist"];
}

static bool IsAutoStartEnabled() {
    return [[NSFileManager defaultManager] fileExistsAtPath:LaunchAgentPath()];
}

static void ToggleAutoStart() {
    NSString *path = LaunchAgentPath();
    if (IsAutoStartEnabled()) {
        [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
    } else {
        NSString *exe = [[NSBundle mainBundle] executablePath];
        NSDictionary *plist = @{
            @"Label": @"com.sysmonitor.widget",
            @"ProgramArguments": @[exe],
            @"RunAtLoad": @YES,
            @"KeepAlive": @NO
        };
        [plist writeToFile:path atomically:YES];
    }
}

// ===================================================================
// App Delegate
// ===================================================================
@interface AppDelegate : NSObject <NSApplicationDelegate>
@property (nonatomic, strong) NSWindow *window;
@property (nonatomic, strong) MonitorView *monitorView;
@property (nonatomic, strong) NSStatusItem *statusItem;
@property (nonatomic, strong) NSTimer *refreshTimer;
@property (nonatomic, strong) NSWindow *widgetPanel;
@property (nonatomic, strong) WidgetPanelView *widgetPanelView;
@end

@implementation AppDelegate

- (void)repositionWidget {
    int wW = CalcWidth();
    int wH = WIDGET_H;
    NSScreen *screen = nil;
    for (NSScreen *s in [NSScreen screens]) {
        NSDictionary *desc = [s deviceDescription];
        CGDirectDisplayID dispID = [[desc objectForKey:@"NSScreenNumber"] unsignedIntValue];
        if (!CGDisplayIsBuiltin(dispID)) { screen = s; break; }
    }
    if (!screen) screen = [NSScreen mainScreen];
    NSRect vis = screen.visibleFrame;
    CGFloat posX = vis.origin.x + vis.size.width - wW - 3;
    CGFloat posY = vis.origin.y + vis.size.height - wH - 3;
    [self.window setFrame:NSMakeRect(posX, posY, wW, wH) display:YES];
}

- (void)screenDidChange:(NSNotification *)note {
    [self repositionWidget];
    [self repositionWidgetPanel];
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

    int wW = CalcWidth();
    int wH = WIDGET_H;
    NSRect frame = NSMakeRect(0, 0, wW, wH);
    self.window = [[NSWindow alloc] initWithContentRect:frame
        styleMask:NSWindowStyleMaskBorderless
        backing:NSBackingStoreBuffered defer:NO];
    self.window.backgroundColor = [NSColor clearColor];
    self.window.opaque = NO;
    self.window.hasShadow = NO;
    self.window.level = NSFloatingWindowLevel;
    self.window.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces
        | NSWindowCollectionBehaviorStationary
        | NSWindowCollectionBehaviorIgnoresCycle;
    self.window.movable = NO;
    self.window.ignoresMouseEvents = YES;

    self.monitorView = [[MonitorView alloc] initWithFrame:NSMakeRect(0, 0, wW, wH)];
    self.window.contentView = self.monitorView;
    [self repositionWidget];
    [self.window orderFrontRegardless];

    [[NSNotificationCenter defaultCenter] addObserver:self
        selector:@selector(screenDidChange:)
        name:NSApplicationDidChangeScreenParametersNotification object:nil];

    // Status bar icon
    self.statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    self.statusItem.button.title = @"📊";
    NSMenu *menu = [[NSMenu alloc] init];
    NSMenuItem *toggleItem = [[NSMenuItem alloc] initWithTitle:@"Hide Widget" action:@selector(toggleWidget:) keyEquivalent:@"h"];
    [menu addItem:toggleItem];
    NSMenuItem *dockItem = [[NSMenuItem alloc] initWithTitle:@"Dock as Widget" action:@selector(toggleDockWidget:) keyEquivalent:@"d"];
    [menu addItem:dockItem];
    NSMenuItem *statusTextItem = [[NSMenuItem alloc] initWithTitle:@"Run in Top Bar (text)" action:@selector(toggleStatusText:) keyEquivalent:@""];
    statusTextItem.state = g_statusTextMode ? NSControlStateValueOn : NSControlStateValueOff;
    [menu addItem:statusTextItem];
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *autoItem = [[NSMenuItem alloc] initWithTitle:@"Auto Start" action:@selector(toggleAutoStart:) keyEquivalent:@""];
    autoItem.state = IsAutoStartEnabled() ? NSControlStateValueOn : NSControlStateValueOff;
    [menu addItem:autoItem];
    [menu addItem:[NSMenuItem separatorItem]];
    [menu addItemWithTitle:@"Quit" action:@selector(quit:) keyEquivalent:@"q"];
    self.statusItem.menu = menu;

    // Start refresh timer
    self.refreshTimer = [NSTimer scheduledTimerWithTimeInterval:UPDATE_SEC
        target:self selector:@selector(tick:) userInfo:nil repeats:YES];
    [[NSRunLoop currentRunLoop] addTimer:self.refreshTimer forMode:NSRunLoopCommonModes];

    // Global mouse-move monitor for hover tooltips (window is click-through)
    __weak AppDelegate *weakSelf = self;
    void (^tipHandler)(NSEvent *) = ^(NSEvent *event) {
        AppDelegate *ss = weakSelf;
        NSPoint sp = [NSEvent mouseLocation];
        if (!ss) { HideTip(); return; }

        // Prefer main HUD widget if visible
        if (ss.window && ss.window.isVisible) {
            NSRect wf = ss.window.frame;
            if (!NSPointInRect(sp, wf)) {
                if (g_hovCore >= 0 || g_hovVol >= 0) {
                    g_hovCore = -1; g_hovVol = -1;
                    HideTip();
                }
                return;
            }

            CGFloat lx = sp.x - wf.origin.x;
            CGFloat ly = wf.size.height - (sp.y - wf.origin.y);
            int core = HitTestCore(lx, ly);
            int vol  = (core < 0) ? HitTestVol(lx, ly) : -1;
            g_hovCore = core;
            g_hovVol  = vol;

            if (core >= 0 && core < (int)g_coreUse.size()) {
                char buf[64];
                snprintf(buf, 64, "Core %d: %.1f%% usage", core, g_coreUse[core]);
                ShowTip([NSString stringWithUTF8String:buf], sp);
            } else if (vol >= 0 && vol < (int)g_vols.size()) {
                double pct = g_vols[vol].totalGB > 0 ?
                    g_vols[vol].usedGB * 100.0 / g_vols[vol].totalGB : 0;
                double freeGB = g_vols[vol].totalGB - g_vols[vol].usedGB;
                char buf[256];
                snprintf(buf, 256, "Volume: %s\nUsed: %s / %s (%.1f%%)\nFree: %s",
                         g_vols[vol].mount.c_str(),
                         FmtDisk(g_vols[vol].usedGB).c_str(),
                         FmtDisk(g_vols[vol].totalGB).c_str(), pct,
                         FmtDisk(freeGB).c_str());
                ShowTip([NSString stringWithUTF8String:buf], sp);
            } else {
                HideTip();
            }
            return;
        }

        // If dock widget panel is visible instead, show per-core tooltip there
        if (ss.widgetPanel && ss.widgetPanel.isVisible) {
            NSRect pf = ss.widgetPanel.frame;
            if (!NSPointInRect(sp, pf)) {
                if (g_hovCore >= 0) {
                    g_hovCore = -1;
                    HideTip();
                }
                return;
            }

            CGFloat lx = sp.x - pf.origin.x;
            CGFloat ly = pf.size.height - (sp.y - pf.origin.y);
            int core = HitTestCorePanel(lx, ly, pf.size.width, pf.size.height);
            g_hovCore = core;
            g_hovVol  = -1;

            if (core >= 0 && core < (int)g_coreUse.size()) {
                char buf[64];
                snprintf(buf, 64, "Core %d: %.1f%% usage", core, g_coreUse[core]);
                ShowTip([NSString stringWithUTF8String:buf], sp);
            } else {
                HideTip();
            }
            return;
        }

        // Neither widget is visible; just clear any existing tooltip
        if (g_hovCore >= 0 || g_hovVol >= 0) {
            g_hovCore = -1; g_hovVol = -1;
            HideTip();
        }
    };
    [NSEvent addGlobalMonitorForEventsMatchingMask:NSEventMaskMouseMoved handler:tipHandler];
    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskMouseMoved handler:^(NSEvent *ev) {
        tipHandler(ev); return ev;
    }];
}

- (void)tick:(NSTimer *)timer {
    UpdateCpu();
    UpdateMem();
    UpdateDisk();
    UpdateNet();
    UpdateLanIP();
    UpdateBattery();
    UpdateWindowBehind(self.window);
    [self.monitorView setNeedsDisplay:YES];
    if (self.widgetPanel.isVisible) {
        UpdateWindowBehind(self.widgetPanel, &g_wpanelBehind);
        [self.widgetPanelView setNeedsDisplay:YES];
    }
    // Update status item text in top bar if enabled
    if (g_statusTextMode && self.statusItem) {
        double ramPct = g_ramTotalMB > 0 ? (double)g_ramUsedMB * 100.0 / g_ramTotalMB : 0;
        // Primary disk (same logic as dock widget)
        double diskUsed = 0, diskTotal = 0;
        for (const auto& v : g_vols) {
            if (v.letter == '/' || v.mount == "/" || v.mount == "/System/Volumes/Data") {
                diskUsed = v.usedGB;
                diskTotal = v.totalGB;
                break;
            }
        }
        if (diskTotal == 0 && !g_vols.empty()) {
            diskUsed = g_vols[0].usedGB;
            diskTotal = g_vols[0].totalGB;
        }
        double diskPct = (diskTotal > 0) ? (diskUsed * 100.0 / diskTotal) : 0;

        std::string upS = FmtSpeed(g_netUp);
        std::string dnS = FmtSpeed(g_netDown);
        int bat = g_batteryPct;
        char buf[160];
        snprintf(buf, sizeof(buf),
                 "CPU %.0f%%  GPU 0%%  RAM %.0f%%  SSD %.0f%%  BAT %s%d%%  \xE2\x86\x91 %s  \xE2\x86\x93 %s",
                 g_totalCpu,
                 ramPct,
                 diskPct,
                 (bat >= 0 ? "" : "- "),
                 (bat >= 0 ? bat : 1),
                 upS.c_str(),
                 dnS.c_str());
        self.statusItem.button.title = [NSString stringWithUTF8String:buf];
    } else if (self.statusItem && !g_statusTextMode) {
        self.statusItem.button.title = @"📊";
    }
}

- (void)disableStatusTextModeIfNeeded {
    if (!g_statusTextMode) return;
    g_statusTextMode = false;
    if (self.statusItem) {
        self.statusItem.button.title = @"📊";
        NSMenuItem *txtItem = [self.statusItem.menu itemWithTitle:@"Run in Top Bar (text)"];
        if (txtItem) {
            txtItem.state = NSControlStateValueOff;
        }
    }
}

- (void)toggleWidget:(id)sender {
    NSMenuItem *item = (NSMenuItem *)sender;
    if (self.window.isVisible) {
        [self.window orderOut:nil];
        HideTip();
        item.title = @"Show Widget";
    } else if (!g_widgetDocked) {
        [self disableStatusTextModeIfNeeded];
        [self.window orderFrontRegardless];
        item.title = @"Hide Widget";
    }
}

- (void)toggleStatusText:(id)sender {
    g_statusTextMode = !g_statusTextMode;
    NSMenuItem *item = (NSMenuItem *)sender;
    item.state = g_statusTextMode ? NSControlStateValueOn : NSControlStateValueOff;

    // When running in top bar only, hide all on-screen widgets (top HUD + dock widget)
    if (g_statusTextMode) {
        if (self.window && self.window.isVisible) {
            [self.window orderOut:nil];
            HideTip();
        }
        if (self.widgetPanel && self.widgetPanel.isVisible) {
            [self.widgetPanel orderOut:nil];
            g_widgetDocked = false;
        }
    }

    // Force immediate update of the status item title
    if (self.statusItem) {
        if (!g_statusTextMode) {
            self.statusItem.button.title = @"📊";
        } else {
            double ramPct = g_ramTotalMB > 0 ? (double)g_ramUsedMB * 100.0 / g_ramTotalMB : 0;
            double diskUsed = 0, diskTotal = 0;
            for (const auto& v : g_vols) {
                if (v.letter == '/' || v.mount == "/" || v.mount == "/System/Volumes/Data") {
                    diskUsed = v.usedGB;
                    diskTotal = v.totalGB;
                    break;
                }
            }
            if (diskTotal == 0 && !g_vols.empty()) {
                diskUsed = g_vols[0].usedGB;
                diskTotal = g_vols[0].totalGB;
            }
            double diskPct = (diskTotal > 0) ? (diskUsed * 100.0 / diskTotal) : 0;
            std::string upS = FmtSpeed(g_netUp);
            std::string dnS = FmtSpeed(g_netDown);
            int bat = g_batteryPct;
            char buf[160];
            snprintf(buf, sizeof(buf),
                     "CPU %.0f%%  GPU 0%%  RAM %.0f%%  SSD %.0f%%  BAT %s%d%%  \xE2\x86\x91 %s  \xE2\x86\x93 %s",
                     g_totalCpu,
                     ramPct,
                     diskPct,
                     (bat >= 0 ? "" : "- "),
                     (bat >= 0 ? bat : 1),
                     upS.c_str(),
                     dnS.c_str());
            self.statusItem.button.title = [NSString stringWithUTF8String:buf];
        }
    }
}

- (void)repositionWidgetPanel {
    if (!self.widgetPanel) return;
    NSScreen *screen = nil;
    for (NSScreen *s in [NSScreen screens]) {
        NSDictionary *desc = [s deviceDescription];
        CGDirectDisplayID dispID = [[desc objectForKey:@"NSScreenNumber"] unsignedIntValue];
        if (!CGDisplayIsBuiltin(dispID)) { screen = s; break; }
    }
    if (!screen) screen = [NSScreen mainScreen];
    NSRect vis = screen.visibleFrame;
    CGFloat posX = vis.origin.x + vis.size.width - WPANEL_W - 2;
    CGFloat posY = vis.origin.y + vis.size.height - WPANEL_H - 2;
    [self.widgetPanel setFrame:NSMakeRect(posX, posY, WPANEL_W, WPANEL_H) display:YES];
}

- (void)toggleDockWidget:(id)sender {
    NSMenuItem *item = (NSMenuItem *)sender;

    if (!self.widgetPanel) {
        NSRect frame = NSMakeRect(0, 0, WPANEL_W, WPANEL_H);
        self.widgetPanel = [[NSWindow alloc] initWithContentRect:frame
            styleMask:NSWindowStyleMaskBorderless
            backing:NSBackingStoreBuffered defer:NO];
        self.widgetPanel.backgroundColor = [NSColor clearColor];
        self.widgetPanel.opaque = NO;
        self.widgetPanel.hasShadow = YES;
        self.widgetPanel.level = NSFloatingWindowLevel;
        self.widgetPanel.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces
            | NSWindowCollectionBehaviorStationary
            | NSWindowCollectionBehaviorIgnoresCycle;
        self.widgetPanel.movable = NO;
        self.widgetPanel.ignoresMouseEvents = YES;

        self.widgetPanelView = [[WidgetPanelView alloc] initWithFrame:NSMakeRect(0, 0, WPANEL_W, WPANEL_H)];
        self.widgetPanel.contentView = self.widgetPanelView;
        [self repositionWidgetPanel];
    }

    if (self.widgetPanel.isVisible) {
        [self.widgetPanel orderOut:nil];
        [self.window orderFrontRegardless];
        g_widgetDocked = false;
        item.title = @"Dock as Widget";
    } else {
        [self disableStatusTextModeIfNeeded];
        [self.window orderOut:nil];
        HideTip();
        [self repositionWidgetPanel];
        [self.widgetPanel orderFrontRegardless];
        g_widgetDocked = true;
        item.title = @"Undock Widget";
    }
}

- (void)toggleAutoStart:(id)sender {
    ToggleAutoStart();
    NSMenuItem *item = (NSMenuItem *)sender;
    item.state = IsAutoStartEnabled() ? NSControlStateValueOn : NSControlStateValueOff;
}

- (void)quit:(id)sender {
    g_shutdown.store(true);
    [NSApp terminate:nil];
}

- (void)applicationWillTerminate:(NSNotification *)notification {
    g_shutdown.store(true);
}

@end

// ===================================================================
// Entry point
// ===================================================================
int main(int argc, const char *argv[]) {
    @autoreleasepool {
        InitCpu();
        UpdateMem();
        UpdateDisk();
        InitNet();
        UpdateLanIP();

        std::thread bgThread(BgThreadFunc);
        bgThread.detach();

        NSApplication *app = [NSApplication sharedApplication];
        AppDelegate *delegate = [[AppDelegate alloc] init];
        app.delegate = delegate;
        [app run];

        g_shutdown.store(true);
    }
    return 0;
}
