#include "libs/net/net.h"

static void GetNetTotals(ULONGLONG& in, ULONGLONG& out) {
    in = out = 0;
    DWORD size = 0;
    GetIfTable(nullptr, &size, FALSE);
    if (size == 0) return;
    std::vector<BYTE> buf(size);
    MIB_IFTABLE* tbl = reinterpret_cast<MIB_IFTABLE*>(buf.data());
    if (GetIfTable(tbl, &size, FALSE) != NO_ERROR) return;
    for (DWORD i = 0; i < tbl->dwNumEntries; i++) {
        auto& r = tbl->table[i];
        if (r.dwOperStatus == MIB_IF_OPER_STATUS_OPERATIONAL &&
            r.dwType != MIB_IF_TYPE_LOOPBACK) {
            in  += r.dwInOctets;
            out += r.dwOutOctets;
        }
    }
}

void InitNet() {
    GetNetTotals(g_netPrevIn, g_netPrevOut);
    g_netTick = GetTickCount64();
    g_netInit = true;
}

void UpdateNet() {
    ULONGLONG ci, co;
    GetNetTotals(ci, co);
    ULONGLONG now = GetTickCount64();
    double dt = (now - g_netTick) / 1000.0;
    if (dt > 0.05 && g_netInit) {
        g_netDown = (ci >= g_netPrevIn)  ? (ci - g_netPrevIn)  / dt : 0;
        g_netUp   = (co >= g_netPrevOut) ? (co - g_netPrevOut) / dt : 0;
    }
    g_netPrevIn  = ci;
    g_netPrevOut = co;
    g_netTick    = now;
}

void UpdateLanIP() {
    ULONG size = 0;
    GetAdaptersInfo(nullptr, &size);
    if (size == 0) return;
    std::vector<BYTE> buf(size);
    IP_ADAPTER_INFO* info = reinterpret_cast<IP_ADAPTER_INFO*>(buf.data());
    if (GetAdaptersInfo(info, &size) != ERROR_SUCCESS) return;
    for (IP_ADAPTER_INFO* a = info; a; a = a->Next) {
        std::string ip = a->IpAddressList.IpAddress.String;
        if (!ip.empty() && ip != "0.0.0.0") {
            g_lanIP = std::wstring(ip.begin(), ip.end());
            return;
        }
    }
    g_lanIP = L"--";
}
