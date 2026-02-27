#include "libs/disk/disk.h"

void UpdateDisk() {
    g_numVols = 0;
    wchar_t drives[128];
    DWORD len = GetLogicalDriveStringsW(127, drives);
    for (wchar_t* d = drives; *d && g_numVols < 26; d += wcslen(d) + 1) {
        if (GetDriveTypeW(d) == DRIVE_FIXED) {
            ULARGE_INTEGER avail, total;
            if (GetDiskFreeSpaceExW(d, &avail, &total, nullptr)) {
                g_vols[g_numVols].letter = d[0];
                g_vols[g_numVols].totalGB = total.QuadPart / (1024.0 * 1024.0 * 1024.0);
                g_vols[g_numVols].usedGB = (total.QuadPart - avail.QuadPart) / (1024.0 * 1024.0 * 1024.0);
                g_numVols++;
            }
        }
    }
}
