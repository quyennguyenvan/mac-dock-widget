// SysMonitor macOS - Shared global metrics/state for mac build
#ifndef SYSMON_MAC_GLOBALS_H
#define SYSMON_MAC_GLOBALS_H

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>

// Shared constants
static const int BG_FETCH_SEC = 300; // 5 minutes

// Disk volume info for macOS
struct VolInfo {
    char        letter;
    double      usedGB;
    double      totalGB;
    std::string mount;
};

// External IP / location / weather data
struct ExtData {
    std::string ip;
    std::string city;
    std::string country;
    double      lat;
    double      lon;
    double      temp;
    int         wcode;
    std::string wdesc;
    bool        loaded;
};

// CPU
extern int                 g_numCores;
extern std::vector<double> g_coreUse;
extern double              g_totalCpu;

// Memory
extern std::uint64_t g_ramTotalMB;
extern std::uint64_t g_ramUsedMB;
extern std::uint64_t g_swapTotalMB;
extern std::uint64_t g_swapUsedMB;

// Disk
extern std::vector<VolInfo> g_vols;

// Network throughput + LAN IP
extern std::uint64_t g_netPrevIn;
extern std::uint64_t g_netPrevOut;
extern std::uint64_t g_netTick;
extern double        g_netDown;
extern double        g_netUp;
extern bool          g_netInit;
extern std::string   g_lanIP;

// External data (public IP, weather)
extern std::mutex   g_extMtx;
extern ExtData      g_ext;
extern std::atomic<bool> g_shutdown;

// Battery percentage (for top-bar text mode)
extern int g_batteryPct;

#endif // SYSMON_MAC_GLOBALS_H

