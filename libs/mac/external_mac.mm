// SysMonitor macOS - External IP / geolocation / weather
// macOS implementation extracted from src/mac.main.cpp so that
// the main file focuses on UI and scheduling.

#import <Cocoa/Cocoa.h>

#include <string>
#include <mutex>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "libs/mac/mac_globals.h"

// ---------------------------------------------------------------------------
// Minimal JSON helpers (for known API shapes, narrow-string variant)
// ---------------------------------------------------------------------------
static std::string JStr(const std::string& j, const std::string& key) {
    std::string k = "\"" + key + "\"";
    size_t p = j.find(k);
    if (p == std::string::npos) return {};
    p += k.size();
    while (p < j.size() && (j[p] == ' ' || j[p] == ':' || j[p] == '\t')) p++;
    if (p >= j.size() || j[p] != '"') return {};
    size_t s = ++p;
    while (p < j.size() && j[p] != '"') p++;
    return j.substr(s, p - s);
}

static double JNum(const std::string& j, const std::string& key) {
    std::string k = "\"" + key + "\"";
    size_t p = j.find(k);
    if (p == std::string::npos) return 0;
    p += k.size();
    while (p < j.size() && (j[p] == ' ' || j[p] == ':' || j[p] == '\t')) p++;
    return atof(j.c_str() + p);
}

static int JInt(const std::string& j, const std::string& key) {
    return (int)JNum(j, key);
}

// ---------------------------------------------------------------------------
// HTTP GET using NSURLSession (synchronous on background thread)
// ---------------------------------------------------------------------------
static std::string HttpGet(const std::string& urlStr) {
    @autoreleasepool {
        NSString *ns = [NSString stringWithUTF8String:urlStr.c_str()];
        NSURL *url = [NSURL URLWithString:ns];
        if (!url) return {};
        __block NSData *resultData = nil;
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);

        NSURLSessionConfiguration *cfg = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        cfg.timeoutIntervalForRequest = 5;
        cfg.timeoutIntervalForResource = 8;
        NSURLSession *session = [NSURLSession sessionWithConfiguration:cfg];

        NSURLSessionDataTask *task = [session dataTaskWithURL:url
                                            completionHandler:^(NSData *data, NSURLResponse *resp, NSError *err) {
            if (!err && data) resultData = [data copy];
            dispatch_semaphore_signal(sem);
        }];
        [task resume];
        dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 8 * NSEC_PER_SEC));
        [session invalidateAndCancel];

        if (resultData) {
            return std::string((const char *)resultData.bytes, resultData.length);
        }
        return {};
    }
}

// ---------------------------------------------------------------------------
// Weather code → description
// ---------------------------------------------------------------------------
static const char* WeatherDesc(int c) {
    switch (c) {
    case 0:            return "Clear Sky";
    case 1:            return "Mainly Clear";
    case 2:            return "Partly Cloudy";
    case 3:            return "Overcast";
    case 45: case 48:  return "Foggy";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57:  return "Freezing Drizzle";
    case 61: case 63: case 65: return "Rain";
    case 66: case 67:  return "Freezing Rain";
    case 71: case 73: case 75: return "Snow";
    case 77:           return "Snow Grains";
    case 80: case 81: case 82: return "Showers";
    case 85: case 86:  return "Snow Showers";
    case 95:           return "Thunderstorm";
    case 96: case 99:  return "Hail Storm";
    default:           return "Unknown";
    }
}

// ---------------------------------------------------------------------------
// Background data fetching (IP + geolocation + weather)
// ---------------------------------------------------------------------------
static void FetchExternal() {
    // --- Phase 1: IP & geolocation ---
    std::string ip, city, cc;
    double lat = 0, lon = 0;

    std::string ipResp = HttpGet("https://ipwho.is/");
    if (!ipResp.empty()) {
        ip   = JStr(ipResp, "ip");
        city = JStr(ipResp, "city");
        cc   = JStr(ipResp, "country_code");
        lat  = JNum(ipResp, "latitude");
        lon  = JNum(ipResp, "longitude");
    }

    if (ip.empty()) {
        ipResp = HttpGet("https://ipapi.co/json/");
        if (!ipResp.empty()) {
            ip   = JStr(ipResp, "ip");
            city = JStr(ipResp, "city");
            cc   = JStr(ipResp, "country_code");
            lat  = JNum(ipResp, "latitude");
            lon  = JNum(ipResp, "longitude");
        }
    }

    {
        std::lock_guard<std::mutex> lk(g_extMtx);
        if (!ip.empty()) {
            g_ext.ip      = ip;
            g_ext.city    = city.empty() ? "Unknown" : city;
            g_ext.country = cc;
            g_ext.lat     = lat;
            g_ext.lon     = lon;
        }
        if (lat == 0 && lon == 0) {
            lat = g_ext.lat;
            lon = g_ext.lon;
        }
        g_ext.loaded = true;
    }

    // --- Phase 2: Weather (uses cached lat/lon if IP fetch failed) ---
    if (lat == 0 && lon == 0) return;

    char wurl[256];
    std::snprintf(wurl, sizeof(wurl),
        "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,weather_code&current_weather=true",
        lat, lon);
    std::string wResp = HttpGet(wurl);
    if (wResp.empty()) return;

    double temp = 0;
    int wcode = -1;
    bool parsed = false;

    size_t cw = wResp.find("\"current\"");
    if (cw != std::string::npos) {
        std::string sub = wResp.substr(cw);
        if (sub.find("\"temperature_2m\"") != std::string::npos) {
            temp  = JNum(sub, "temperature_2m");
            wcode = JInt(sub, "weather_code");
            parsed = true;
        }
    }
    if (!parsed) {
        cw = wResp.find("\"current_weather\"");
        if (cw != std::string::npos) {
            std::string sub = wResp.substr(cw);
            if (sub.find("\"temperature\"") != std::string::npos) {
                temp  = JNum(sub, "temperature");
                wcode = JInt(sub, "weathercode");
                parsed = true;
            }
        }
    }

    if (parsed) {
        std::lock_guard<std::mutex> lk(g_extMtx);
        g_ext.temp  = temp;
        g_ext.wcode = wcode;
        g_ext.wdesc = WeatherDesc(wcode);
    }
}

void BgThreadFunc() {
    FetchExternal();
    int failures = 0;
    while (!g_shutdown.load()) {
        int waitSec;
        {
            std::lock_guard<std::mutex> lk(g_extMtx);
            bool ipOk = !g_ext.ip.empty() && g_ext.ip != "Loading...";
            bool wxOk = g_ext.wcode >= 0;
            if (ipOk && wxOk) { waitSec = BG_FETCH_SEC; failures = 0; }
            else { failures++; waitSec = std::min(15 * failures, 120); }
        }
        for (int i = 0; i < waitSec * 10 && !g_shutdown.load(); i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!g_shutdown.load()) FetchExternal();
    }
}

