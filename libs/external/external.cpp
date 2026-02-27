#include "libs/external/external.h"
#include "libs/http/http.h"
#include "libs/json/json.h"
#include "libs/util/util.h"

const wchar_t* WeatherDesc(int c) {
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

static void FetchExternal() {
    std::string ipResp = HttpGet(L"ip-api.com", L"/json", false);
    if (ipResp.empty()) return;

    std::wstring ij = ToWide(ipResp);
    std::wstring ip   = JStr(ij, L"query");
    std::wstring city = JStr(ij, L"city");
    std::wstring cc   = JStr(ij, L"countryCode");
    double lat = JNum(ij, L"lat");
    double lon = JNum(ij, L"lon");

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
        size_t cw = wj.find(L"\"current\"");
        if (cw != std::wstring::npos) {
            std::wstring sub = wj.substr(cw);
            if (sub.find(L"\"temperature_2m\"") != std::wstring::npos) {
                temp  = JNum(sub, L"temperature_2m");
                wcode = JInt(sub, L"weather_code");
            }
        }
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

DWORD WINAPI BgThread(LPVOID) {
    FetchExternal();
    while (WaitForSingleObject(g_shutdownEvt, BG_FETCH_MS) == WAIT_TIMEOUT)
        FetchExternal();
    return 0;
}
