#include "libs/layout/layout.h"

int CalcCpuSecW() {
    int blocksW = g_numCores * 10;
    return (blocksW > 110 ? blocksW : 110) + 12;
}

int CalcDiskSecW() {
    int cols = (g_numVols + 1) / 2;
    if (cols < 1) cols = 1;
    return cols * SEC_DISK_COL_W;
}

int CalcWidth() {
    return BAR_PAD + SEC_TIME_W + SEC_SEP + CalcCpuSecW() + SEC_SEP
         + SEC_MEM_W + SEC_SEP + CalcDiskSecW() + SEC_SEP
         + SEC_IPNET_W + SEC_SEP + SEC_WX_W + BAR_PAD;
}
