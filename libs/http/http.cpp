#include "libs/http/http.h"

std::string HttpGet(const wchar_t* host, const wchar_t* path, bool tls) {
    std::string result;
    HINTERNET ses = WinHttpOpen(L"SysMonitor/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    if (!ses) return result;

    INTERNET_PORT port = tls ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    HINTERNET con = WinHttpConnect(ses, host, port, 0);
    if (!con) { WinHttpCloseHandle(ses); return result; }

    DWORD flags = tls ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(con, L"GET", path, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) { WinHttpCloseHandle(con); WinHttpCloseHandle(ses); return result; }

    WinHttpSetTimeouts(req, 10000, 10000, 10000, 10000);

    if (WinHttpSendRequest(req, nullptr, 0, nullptr, 0, 0, 0) &&
        WinHttpReceiveResponse(req, nullptr)) {
        DWORD avail = 0;
        do {
            WinHttpQueryDataAvailable(req, &avail);
            if (avail > 0) {
                std::string buf(avail, 0);
                DWORD rd = 0;
                WinHttpReadData(req, &buf[0], avail, &rd);
                result.append(buf, 0, rd);
            }
        } while (avail > 0);
    }
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return result;
}
