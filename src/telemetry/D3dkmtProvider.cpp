// Generic Windows telemetry through the WDDM kernel-mode thunks (the same data Task Manager uses).
// Available on Windows 10 2004+ with WDDM 2.7+ drivers; individual fields are 0 when unsupported.
#include "Telemetry.h"
#include "../core/Log.h"

namespace aether {

namespace {
typedef UINT D3DKMT_HANDLE;
struct D3DKMT_OPENADAPTERFROMLUID { LUID AdapterLuid; D3DKMT_HANDLE hAdapter; };
struct D3DKMT_CLOSEADAPTER { D3DKMT_HANDLE hAdapter; };
struct D3DKMT_QUERYADAPTERINFO { D3DKMT_HANDLE hAdapter; UINT Type; VOID* pPrivateDriverData; UINT PrivateDriverDataSize; };
struct D3DKMT_ADAPTER_PERFDATA {
    UINT32 PhysicalAdapterIndex;
    ULONGLONG MemoryFrequency, MaxMemoryFrequency, MaxMemoryFrequencyOC, MemoryBandwidth, PCIEBandwidth;
    ULONG FanRPM, Power, Temperature;
    UCHAR PowerStateOverride;
};
struct D3DKMT_NODE_PERFDATA {
    UINT32 NodeOrdinal, PhysicalAdapterIndex;
    ULONGLONG Frequency, MaxFrequency, MaxFrequencyOC;
    ULONG Voltage, VoltageMax, VoltageMaxOC;
    ULONGLONG MaxTransitionLatency;
};
constexpr UINT KMTQAITYPE_NODEPERFDATA = 61;
constexpr UINT KMTQAITYPE_ADAPTERPERFDATA = 62;

using FnOpen = LONG(APIENTRY*)(D3DKMT_OPENADAPTERFROMLUID*);
using FnClose = LONG(APIENTRY*)(const D3DKMT_CLOSEADAPTER*);
using FnQuery = LONG(APIENTRY*)(const D3DKMT_QUERYADAPTERINFO*);

class D3dkmtProvider : public ITelemetryProvider {
public:
    FnClose closeFn = nullptr;
    FnQuery query = nullptr;
    D3DKMT_HANDLE adapter = 0;
    ~D3dkmtProvider() override {
        if (adapter && closeFn) { D3DKMT_CLOSEADAPTER c{adapter}; closeFn(&c); }
    }
    const char* name() const override { return "Windows D3DKMT"; }
    TelemetrySource source() const override { return TelemetrySource::D3DKMT; }
    void sample(TelemetrySample& s) override {
        D3DKMT_ADAPTER_PERFDATA pd{};
        D3DKMT_QUERYADAPTERINFO q{adapter, KMTQAITYPE_ADAPTERPERFDATA, &pd, sizeof(pd)};
        if (query(&q) == 0) {
            if (pd.Temperature > 0 && pd.Temperature < 1500) s.gpuTemp = pd.Temperature / 10.0f;
            if (pd.FanRPM > 0) s.fanRpm = (float)pd.FanRPM;
            if (pd.Power > 0) s.powerPct = pd.Power / 10.0f;
            if (pd.MemoryFrequency > 0) s.memClockMHz = (float)(pd.MemoryFrequency / 1000000.0);
        }
        D3DKMT_NODE_PERFDATA nd{};
        nd.NodeOrdinal = 0;
        D3DKMT_QUERYADAPTERINFO qn{adapter, KMTQAITYPE_NODEPERFDATA, &nd, sizeof(nd)};
        if (query(&qn) == 0) {
            if (nd.Frequency > 0) s.coreClockMHz = (float)(nd.Frequency / 1000000.0);
            if (nd.Voltage > 0 && nd.Voltage < 3000) s.voltageMv = (float)nd.Voltage;
        }
    }
};
} // namespace

ITelemetryProvider* createD3dkmtProvider(const LUID& luid) {
    HMODULE gdi = GetModuleHandleW(L"gdi32.dll");
    if (!gdi) gdi = LoadLibraryW(L"gdi32.dll");
    auto open = (FnOpen)(void*)GetProcAddress(gdi, "D3DKMTOpenAdapterFromLuid");
    auto close = (FnClose)(void*)GetProcAddress(gdi, "D3DKMTCloseAdapter");
    auto query = (FnQuery)(void*)GetProcAddress(gdi, "D3DKMTQueryAdapterInfo");
    if (!open || !close || !query) return nullptr;
    D3DKMT_OPENADAPTERFROMLUID o{};
    o.AdapterLuid = luid;
    if (open(&o) != 0) { LOGW("D3DKMTOpenAdapterFromLuid failed"); return nullptr; }
    auto* p = new D3dkmtProvider();
    p->closeFn = close; p->query = query; p->adapter = o.hAdapter;
    D3DKMT_ADAPTER_PERFDATA pd{};
    D3DKMT_QUERYADAPTERINFO q{o.hAdapter, KMTQAITYPE_ADAPTERPERFDATA, &pd, sizeof(pd)};
    LONG st = query(&q);
    LOGI("D3DKMT perf data query status 0x%08lX (temp=%lu, fan=%lu)", (unsigned long)st, pd.Temperature, pd.FanRPM);
    return p;
}

} // namespace aether
