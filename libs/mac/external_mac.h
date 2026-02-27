// SysMonitor macOS - External IP / weather background worker
#ifndef SYSMON_MAC_EXTERNAL_H
#define SYSMON_MAC_EXTERNAL_H

#include "libs/mac/mac_globals.h"

// Background worker entry that periodically refreshes g_ext.
// Intended to be run on a detached std::thread.
void BgThreadFunc();

#endif // SYSMON_MAC_EXTERNAL_H

