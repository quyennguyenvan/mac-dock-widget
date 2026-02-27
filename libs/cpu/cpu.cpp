#include "libs/cpu/cpu.h"

void InitCpu() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll)
        g_NtQSI = (NtQSI_t)GetProcAddress(ntdll, "NtQuerySystemInformation");

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    g_numCores = (int)si.dwNumberOfProcessors;
    if (g_numCores > 128) g_numCores = 128;

    g_coreUse.resize(g_numCores, 0.0);
    g_prevCpu.resize(g_numCores);

    if (g_NtQSI) {
        ULONG ret = 0;
        g_NtQSI(8, g_prevCpu.data(),
            g_numCores * sizeof(PROC_PERF_INFO), &ret);
    }
}

void UpdateCpu() {
    if (!g_NtQSI) return;
    std::vector<PROC_PERF_INFO> cur(g_numCores);
    ULONG ret = 0;
    if (g_NtQSI(8, cur.data(), g_numCores * sizeof(PROC_PERF_INFO), &ret) != 0)
        return;

    double sum = 0;
    for (int i = 0; i < g_numCores; i++) {
        LONGLONG dI = cur[i].IdleTime.QuadPart   - g_prevCpu[i].IdleTime.QuadPart;
        LONGLONG dK = cur[i].KernelTime.QuadPart - g_prevCpu[i].KernelTime.QuadPart;
        LONGLONG dU = cur[i].UserTime.QuadPart   - g_prevCpu[i].UserTime.QuadPart;
        LONGLONG dT = dK + dU;
        double u = (dT > 0) ? (1.0 - (double)dI / dT) * 100.0 : 0.0;
        if (u < 0) u = 0; if (u > 100) u = 100;
        g_coreUse[i] = u;
        sum += u;
    }
    g_totalCpu = g_numCores > 0 ? sum / g_numCores : 0;
    g_prevCpu = cur;
}
