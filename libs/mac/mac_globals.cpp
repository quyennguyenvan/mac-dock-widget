// SysMonitor macOS - Global metrics/state definitions
#include "libs/mac/mac_globals.h"

// CPU
int                 g_numCores  = 0;
std::vector<double> g_coreUse;
double              g_totalCpu  = 0.0;

// Memory
std::uint64_t g_ramTotalMB  = 0;
std::uint64_t g_ramUsedMB   = 0;
std::uint64_t g_swapTotalMB = 0;
std::uint64_t g_swapUsedMB  = 0;

// Disk
std::vector<VolInfo> g_vols;

// Network
std::uint64_t g_netPrevIn  = 0;
std::uint64_t g_netPrevOut = 0;
std::uint64_t g_netTick    = 0;
double        g_netDown    = 0.0;
double        g_netUp      = 0.0;
bool          g_netInit    = false;
std::string   g_lanIP      = "--";

// External data
std::mutex g_extMtx;
ExtData    g_ext{
    "Loading...",   // ip
    "Loading...",   // city
    "",             // country
    0.0, 0.0,       // lat, lon
    0.0,            // temp
    -1,             // wcode
    "",             // wdesc
    false           // loaded
};

// Shutdown flag for background thread
std::atomic<bool> g_shutdown{false};

// Battery percentage
int g_batteryPct = -1;

