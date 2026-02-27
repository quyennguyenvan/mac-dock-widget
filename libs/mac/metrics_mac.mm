// SysMonitor macOS - System metrics (CPU, memory, disk, network, battery)

#import <Cocoa/Cocoa.h>
#import <IOKit/ps/IOPowerSources.h>
#import <IOKit/ps/IOPSKeys.h>

#include <mach/mach.h>
#include <mach/processor_info.h>
#include <sys/sysctl.h>
#include <sys/mount.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <ifaddrs.h>
#include <arpa/inet.h>

#include "libs/mac/mac_globals.h"
#include "libs/mac/metrics_mac.h"

// ---------------------------------------------------------------------------
// Time helper
// ---------------------------------------------------------------------------
static uint64_t TickMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}

// History for CPU usage calculation (per-core)
static processor_cpu_load_info_t g_prevLoad = nullptr;
static natural_t                 g_prevCount = 0;

// ---------------------------------------------------------------------------
// CPU
// ---------------------------------------------------------------------------
void InitCpu() {
    natural_t numCPUs = 0;
    processor_cpu_load_info_t cpuLoad = nullptr;
    mach_msg_type_number_t cpuMsgCount = 0;
    if (host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO,
            &numCPUs, (processor_info_array_t *)&cpuLoad, &cpuMsgCount) == KERN_SUCCESS) {
        g_numCores = (int)numCPUs;
        g_coreUse.resize(g_numCores, 0.0);
        g_prevLoad = cpuLoad;
        g_prevCount = cpuMsgCount;
    }
}

void UpdateCpu() {
    natural_t numCPUs = 0;
    processor_cpu_load_info_t cpuLoad = nullptr;
    mach_msg_type_number_t cpuMsgCount = 0;
    if (host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO,
            &numCPUs, (processor_info_array_t *)&cpuLoad, &cpuMsgCount) != KERN_SUCCESS)
        return;

    double sum = 0;
    for (int i = 0; i < g_numCores && i < (int)numCPUs; i++) {
        unsigned int *cur  = cpuLoad[i].cpu_ticks;
        unsigned int *prev = g_prevLoad ? g_prevLoad[i].cpu_ticks : nullptr;
        if (prev) {
            unsigned int dUser = cur[CPU_STATE_USER]   - prev[CPU_STATE_USER];
            unsigned int dSys  = cur[CPU_STATE_SYSTEM] - prev[CPU_STATE_SYSTEM];
            unsigned int dNice = cur[CPU_STATE_NICE]   - prev[CPU_STATE_NICE];
            unsigned int dIdle = cur[CPU_STATE_IDLE]   - prev[CPU_STATE_IDLE];
            unsigned int dTotal = dUser + dSys + dNice + dIdle;
            double u = dTotal > 0 ? (double)(dUser + dSys + dNice) / dTotal * 100.0 : 0;
            if (u < 0) u = 0; if (u > 100) u = 100;
            g_coreUse[i] = u;
            sum += u;
        }
    }
    g_totalCpu = g_numCores > 0 ? sum / g_numCores : 0;

    if (g_prevLoad)
        vm_deallocate(mach_task_self(), (vm_address_t)g_prevLoad, g_prevCount * sizeof(int));
    g_prevLoad = cpuLoad;
    g_prevCount = cpuMsgCount;
}

// ---------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------
void UpdateMem() {
    int64_t totalMem = 0;
    size_t len = sizeof(totalMem);
    sysctlbyname("hw.memsize", &totalMem, &len, nullptr, 0);
    g_ramTotalMB = totalMem / (1024 * 1024);

    vm_statistics64_data_t vm;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vm, &count) == KERN_SUCCESS) {
        vm_size_t pageSize = 0;
        host_page_size(mach_host_self(), &pageSize);
        uint64_t usedPages = vm.active_count + vm.wire_count + vm.compressor_page_count;
        g_ramUsedMB = (usedPages * pageSize) / (1024 * 1024);
    }

    struct xsw_usage swap;
    len = sizeof(swap);
    if (sysctlbyname("vm.swapusage", &swap, &len, nullptr, 0) == 0) {
        g_swapTotalMB = swap.xsu_total / (1024 * 1024);
        g_swapUsedMB  = swap.xsu_used / (1024 * 1024);
    }
}

// ---------------------------------------------------------------------------
// Disk
// ---------------------------------------------------------------------------
void UpdateDisk() {
    struct statfs *mounts = nullptr;
    int n = getmntinfo(&mounts, MNT_NOWAIT);
    std::vector<VolInfo> vols;
    for (int i = 0; i < n; i++) {
        std::string fstype = mounts[i].f_fstypename;
        std::string mp = mounts[i].f_mntonname;
        if (fstype != "apfs" && fstype != "hfs") continue;
        if (mp.find("/System/Volumes/") == 0 && mp != "/System/Volumes/Data") continue;
        if (mp == "/dev" || mp == "/private/var/vm") continue;

        double totalGB = (double)mounts[i].f_blocks * mounts[i].f_bsize / (1024.0*1024.0*1024.0);
        double freeGB  = (double)mounts[i].f_bavail * mounts[i].f_bsize / (1024.0*1024.0*1024.0);
        if (totalGB < 0.1) continue;

        VolInfo v;
        if (mp == "/" || mp == "/System/Volumes/Data")
            v.letter = '/';
        else {
            size_t last = mp.rfind('/');
            v.mount = (last != std::string::npos) ? mp.substr(last + 1) : mp;
            v.letter = v.mount.empty() ? '?' : v.mount[0];
        }
        v.totalGB = totalGB;
        v.usedGB  = totalGB - freeGB;
        v.mount   = mp;
        vols.push_back(v);
    }
    g_vols = vols;
}

// ---------------------------------------------------------------------------
// Battery
// ---------------------------------------------------------------------------
void UpdateBattery() {
    g_batteryPct = -1;
    CFTypeRef blob = IOPSCopyPowerSourcesInfo();
    if (!blob) return;
    CFArrayRef list = IOPSCopyPowerSourcesList(blob);
    if (!list) { CFRelease(blob); return; }
    CFIndex count = CFArrayGetCount(list);
    for (CFIndex i = 0; i < count; i++) {
        CFTypeRef ps = CFArrayGetValueAtIndex(list, i);
        CFDictionaryRef desc = IOPSGetPowerSourceDescription(blob, ps);
        if (!desc) continue;
        CFNumberRef cur = (CFNumberRef)CFDictionaryGetValue(desc, CFSTR(kIOPSCurrentCapacityKey));
        CFNumberRef max = (CFNumberRef)CFDictionaryGetValue(desc, CFSTR(kIOPSMaxCapacityKey));
        if (!cur || !max) continue;
        int c = 0, m = 0;
        CFNumberGetValue(cur, kCFNumberIntType, &c);
        CFNumberGetValue(max, kCFNumberIntType, &m);
        if (m > 0) g_batteryPct = (int)lrint((double)c * 100.0 / (double)m);
        break;
    }
    CFRelease(list);
    CFRelease(blob);
}

// ---------------------------------------------------------------------------
// Network + LAN IP
// ---------------------------------------------------------------------------
static void GetNetTotals(uint64_t &in, uint64_t &out) {
    in = out = 0;
    struct ifaddrs *ifap = nullptr;
    if (getifaddrs(&ifap) != 0) return;
    for (struct ifaddrs *ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_LINK) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        if (!ifa->ifa_data) continue;
        struct if_data *ifd = (struct if_data *)ifa->ifa_data;
        in  += ifd->ifi_ibytes;
        out += ifd->ifi_obytes;
    }
    freeifaddrs(ifap);
}

void InitNet() {
    GetNetTotals(g_netPrevIn, g_netPrevOut);
    g_netTick = TickMs();
    g_netInit = true;
}

void UpdateNet() {
    uint64_t ci, co;
    GetNetTotals(ci, co);
    uint64_t now = TickMs();
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
    struct ifaddrs *ifap = nullptr;
    if (getifaddrs(&ifap) != 0) return;
    for (struct ifaddrs *ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        char buf[INET_ADDRSTRLEN];
        struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
        inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf));
        if (strcmp(buf, "127.0.0.1") != 0) {
            g_lanIP = buf;
            freeifaddrs(ifap);
            return;
        }
    }
    freeifaddrs(ifap);
    g_lanIP = "--";
}

