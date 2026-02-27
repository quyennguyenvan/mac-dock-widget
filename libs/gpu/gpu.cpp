#include "libs/gpu/gpu.h"

typedef struct _D3DKMT_OPENADAPTERFROMLUID {
    LUID  AdapterLuid;
    UINT  hAdapter;
} D3DKMT_OPENADAPTERFROMLUID;

typedef struct _D3DKMT_CLOSEADAPTER {
    UINT hAdapter;
} D3DKMT_CLOSEADAPTER;

#pragma pack(push, 1)
typedef struct _D3DKMT_QUERYSTATISTICS {
    UINT  Type;
    LUID  AdapterLuid;
    ULONG Padding0;
    ULONG ProcessHandle;
    ULONG Padding1;
    union {
        ULONG QueryNode_NodeId;
        struct { ULONG x1; ULONG x2; } QuerySegment;
        BYTE  Pad[32];
    } Input;
    BYTE  Result[1024];
} D3DKMT_QUERYSTATISTICS;
#pragma pack(pop)

typedef LONG(APIENTRY* PFN_D3DKMTOpenAdapterFromLuid)(D3DKMT_OPENADAPTERFROMLUID*);
typedef LONG(APIENTRY* PFN_D3DKMTCloseAdapter)(D3DKMT_CLOSEADAPTER*);
typedef LONG(APIENTRY* PFN_D3DKMTQueryStatistics)(D3DKMT_QUERYSTATISTICS*);

static PFN_D3DKMTOpenAdapterFromLuid pfnOpenAdapter  = nullptr;
static PFN_D3DKMTCloseAdapter        pfnCloseAdapter = nullptr;
static PFN_D3DKMTQueryStatistics     pfnQueryStats   = nullptr;
static bool g_gpuD3dKmtInit = false;
static UINT g_gpuNodeCount  = 0;
static double g_qpcInvFreq = 0.0;

void InitGpuD3dKmt() {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_qpcInvFreq = 1.0 / (double)f.QuadPart;

    if (g_gpuD3dKmtInit) return;
    g_gpuD3dKmtInit = true;

    HMODULE hGdi = GetModuleHandleW(L"gdi32.dll");
    if (!hGdi) hGdi = LoadLibraryW(L"gdi32.dll");
    if (!hGdi) return;

    pfnOpenAdapter  = (PFN_D3DKMTOpenAdapterFromLuid)GetProcAddress(hGdi, "D3DKMTOpenAdapterFromLuid");
    pfnCloseAdapter = (PFN_D3DKMTCloseAdapter)GetProcAddress(hGdi, "D3DKMTCloseAdapter");
    pfnQueryStats   = (PFN_D3DKMTQueryStatistics)GetProcAddress(hGdi, "D3DKMTQueryStatistics");

    if (!pfnOpenAdapter || !pfnCloseAdapter || !pfnQueryStats) {
        pfnQueryStats = nullptr;
        return;
    }

    IDXGIFactory1* pFactory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory)))
        return;

    SIZE_T bestVram = 0;
    for (UINT i = 0;; ++i) {
        IDXGIAdapter1* pAdapter = nullptr;
        if (pFactory->EnumAdapters1(i, &pAdapter) == DXGI_ERROR_NOT_FOUND)
            break;
        DXGI_ADAPTER_DESC1 desc = {};
        if (SUCCEEDED(pAdapter->GetDesc1(&desc))) {
            if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                desc.DedicatedVideoMemory > bestVram) {
                bestVram   = desc.DedicatedVideoMemory;
                g_gpuLuid  = desc.AdapterLuid;
            }
        }
        pAdapter->Release();
    }
    pFactory->Release();

    if (bestVram == 0) { pfnQueryStats = nullptr; return; }

    D3DKMT_OPENADAPTERFROMLUID openArg = {};
    openArg.AdapterLuid = g_gpuLuid;
    if (pfnOpenAdapter(&openArg) != 0) { pfnQueryStats = nullptr; return; }

    for (UINT n = 0; n < 64; n++) {
        D3DKMT_QUERYSTATISTICS qs = {};
        qs.Type        = 3;
        qs.AdapterLuid = g_gpuLuid;
        qs.Input.QueryNode_NodeId = n;
        if (pfnQueryStats(&qs) != 0) break;

        ULONGLONG v = *(ULONGLONG*)(qs.Result + 32);
        if (v != 0) g_gpuNodeCount = n + 1;
    }

    D3DKMT_CLOSEADAPTER closeArg = {};
    closeArg.hAdapter = openArg.hAdapter;
    pfnCloseAdapter(&closeArg);

    if (g_gpuNodeCount == 0) pfnQueryStats = nullptr;
}

void UpdateGpu() {
    if (!pfnQueryStats || g_gpuNodeCount == 0) {
        g_gpuUsagePct = 0.0;
        return;
    }

    D3DKMT_OPENADAPTERFROMLUID openArg = {};
    openArg.AdapterLuid = g_gpuLuid;
    if (pfnOpenAdapter(&openArg) != 0) return;

    ULONGLONG totalRunning = 0;
    for (UINT n = 0; n < g_gpuNodeCount; n++) {
        D3DKMT_QUERYSTATISTICS qs = {};
        qs.Type        = 3;
        qs.AdapterLuid = g_gpuLuid;
        qs.Input.QueryNode_NodeId = n;
        if (pfnQueryStats(&qs) == 0) {
            ULONGLONG runningTime = *(ULONGLONG*)(qs.Result + 0);
            totalRunning += runningTime;
        }
    }

    D3DKMT_CLOSEADAPTER closeArg = {};
    closeArg.hAdapter = openArg.hAdapter;
    pfnCloseAdapter(&closeArg);

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    ULONGLONG tsNow = (ULONGLONG)now.QuadPart;

    if (g_gpuTsPrev > 0 && tsNow > g_gpuTsPrev) {
        double deltaSec = (double)(tsNow - g_gpuTsPrev) * g_qpcInvFreq;
        double deltaEng = (double)(totalRunning - g_gpuEngPrev) / 10000000.0;
        double pct = (deltaSec > 0.0) ? (deltaEng / deltaSec) * 100.0 : 0.0;
        if (pct < 0.0) pct = 0.0;
        if (pct > 100.0) pct = 100.0;
        g_gpuUsagePct = pct;
    }

    g_gpuEngPrev = totalRunning;
    g_gpuTsPrev  = tsNow;
}
