#ifndef SYSMON_GDIP_H
#define SYSMON_GDIP_H

#include "libs/globals/globals.h"

void InitGdip();
void CleanupGdip();
void EnsureDIB(int w, int h);

#endif
