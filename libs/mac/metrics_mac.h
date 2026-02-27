// SysMonitor macOS - System metrics (CPU, memory, disk, network, battery)
#ifndef SYSMON_MAC_METRICS_H
#define SYSMON_MAC_METRICS_H

#include "libs/mac/mac_globals.h"

// CPU
void InitCpu();
void UpdateCpu();

// Memory
void UpdateMem();

// Disk volumes
void UpdateDisk();

// Battery
void UpdateBattery();

// Network throughput + LAN IP
void InitNet();
void UpdateNet();
void UpdateLanIP();

#endif // SYSMON_MAC_METRICS_H

