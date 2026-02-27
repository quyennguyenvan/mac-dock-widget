#include "libs/mem/mem.h"

void UpdateMem() {
    MEMORYSTATUSEX ms = {}; ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);
    g_ramTotalMB  = ms.ullTotalPhys / (1024 * 1024);
    g_ramUsedMB   = (ms.ullTotalPhys - ms.ullAvailPhys) / (1024 * 1024);
    g_swapTotalMB = ms.ullTotalPageFile / (1024 * 1024);
    g_swapUsedMB  = (ms.ullTotalPageFile - ms.ullAvailPageFile) / (1024 * 1024);
}
