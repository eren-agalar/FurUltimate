// AMD telemetry through ADL (atiadlxx.dll ships with AMD Software / Adrenalin drivers), using the
// Overdrive8 PMLog query. Structures follow the public ADL SDK definitions (MIT licensed SDK).
#include "Telemetry.h"
#include "../core/Log.h"
#include "../core/Strings.h"
#include <cstdlib>
#include <cstring>
#include <vector>

namespace aether {

namespace {
constexpr int ADL_MAX_PATH = 256;
struct AdlAdapterInfo {
    int iSize, iAdapterIndex;
    char strUDID[ADL_MAX_PATH];
    int iBusNumber, iDeviceNumber, iFunctionNumber, iVendorID;
    char strAdapterName[ADL_MAX_PATH];
    char strDisplayName[ADL_MAX_PATH];
    int iPresent, iExist;
    char strDriverPath[ADL_MAX_PATH];
    char strDriverPathExt[ADL_MAX_PATH];
    char strPNPString[ADL_MAX_PATH];
    int iOSDisplayIndex;
};
struct AdlSingleSensorData { int supported; int value; };
struct AdlPMLogDataOutput { int size; AdlSingleSensorData sensors[256]; };
struct AdlMemoryInfo2 {
    long long iMemorySize;
    char strMemoryType[ADL_MAX_PATH];
    long long iMemoryBandwidth, iHyperMemorySize, iInvisibleMemorySize, iVisibleMemorySize;
};
enum {
    PMLOG_CLK_GFXCLK = 1, PMLOG_CLK_MEMCLK = 2, PMLOG_TEMPERATURE_EDGE = 8, PMLOG_TEMPERATURE_MEM = 9,
    PMLOG_FAN_RPM = 14, PMLOG_FAN_PERCENTAGE = 15, PMLOG_INFO_ACTIVITY_GFX = 19, PMLOG_INFO_ACTIVITY_MEM = 20,
    PMLOG_GFX_VOLTAGE = 21, PMLOG_ASIC_POWER = 23, PMLOG_TEMPERATURE_HOTSPOT = 27
};

typedef void* ADL_CONTEXT_HANDLE;
typedef void* (__stdcall* ADL_MAIN_MALLOC_CALLBACK)(int);
using FnCreate = int (*)(ADL_MAIN_MALLOC_CALLBACK, int, ADL_CONTEXT_HANDLE*);
using FnDestroy = int (*)(ADL_CONTEXT_HANDLE);
using FnNum = int (*)(ADL_CONTEXT_HANDLE, int*);
using FnInfo = int (*)(ADL_CONTEXT_HANDLE, AdlAdapterInfo*, int);
using FnPMLog = int (*)(ADL_CONTEXT_HANDLE, int, AdlPMLogDataOutput*);
using FnMemInfo = int (*)(ADL_CONTEXT_HANDLE, int, AdlMemoryInfo2*);

void* __stdcall adlMalloc(int size) { return std::malloc(size); }

class AdlProvider : public ITelemetryProvider {
public:
    HMODULE lib = nullptr;
    ADL_CONTEXT_HANDLE ctx = nullptr;
    FnDestroy destroy = nullptr;
    FnPMLog pmlog = nullptr;
    int adapter = -1;
    ~AdlProvider() override {
        if (destroy && ctx) destroy(ctx);
        if (lib) FreeLibrary(lib);
    }
    const char* name() const override { return "AMD ADL (Overdrive8)"; }
    TelemetrySource source() const override { return TelemetrySource::ADL; }
    void sample(TelemetrySample& s) override {
        AdlPMLogDataOutput out{};
        out.size = sizeof(out);
        if (pmlog(ctx, adapter, &out) != 0) return;
        auto get = [&](int idx, float& dst, float scale = 1.0f) {
            if (out.sensors[idx].supported) dst = out.sensors[idx].value * scale;
        };
        get(PMLOG_CLK_GFXCLK, s.coreClockMHz);
        get(PMLOG_CLK_MEMCLK, s.memClockMHz);
        get(PMLOG_TEMPERATURE_EDGE, s.gpuTemp);
        get(PMLOG_TEMPERATURE_MEM, s.memTemp);
        get(PMLOG_TEMPERATURE_HOTSPOT, s.hotspotTemp);
        get(PMLOG_FAN_RPM, s.fanRpm);
        get(PMLOG_FAN_PERCENTAGE, s.fanPct);
        get(PMLOG_INFO_ACTIVITY_GFX, s.gpuLoad);
        get(PMLOG_INFO_ACTIVITY_MEM, s.memCtrlLoad);
        get(PMLOG_GFX_VOLTAGE, s.voltageMv);
        get(PMLOG_ASIC_POWER, s.powerW);
        // Discard implausible readings (sensor not wired on some boards).
        if (has(s.gpuTemp) && (s.gpuTemp <= 0 || s.gpuTemp > 150)) s.gpuTemp = NA;
        if (has(s.hotspotTemp) && (s.hotspotTemp <= 0 || s.hotspotTemp > 150)) s.hotspotTemp = NA;
        if (has(s.memTemp) && (s.memTemp <= 0 || s.memTemp > 150)) s.memTemp = NA;
        if (has(s.powerW) && s.powerW <= 0) s.powerW = NA;
    }
};
} // namespace

ITelemetryProvider* createAdlProvider(uint32_t deviceId, std::string& memType) {
    HMODULE lib = LoadLibraryW(L"atiadlxx.dll");
    if (!lib) { LOGW("ADL not found; AMD vendor sensors unavailable"); return nullptr; }
    auto sym = [&](const char* n) { return (void*)GetProcAddress(lib, n); };
    auto create = (FnCreate)sym("ADL2_Main_Control_Create");
    auto num = (FnNum)sym("ADL2_Adapter_NumberOfAdapters_Get");
    auto info = (FnInfo)sym("ADL2_Adapter_AdapterInfo_Get");
    auto pm = (FnPMLog)sym("ADL2_New_QueryPMLogData_Get");
    if (!create || !num || !info || !pm) { FreeLibrary(lib); LOGW("ADL entry points missing"); return nullptr; }
    auto* p = new AdlProvider();
    p->lib = lib;
    p->destroy = (FnDestroy)sym("ADL2_Main_Control_Destroy");
    p->pmlog = pm;
    if (create(adlMalloc, 1, &p->ctx) != 0) { delete p; return nullptr; }
    int n = 0;
    if (num(p->ctx, &n) != 0 || n <= 0) { delete p; return nullptr; }
    std::vector<AdlAdapterInfo> infos(n);
    for (auto& i : infos) i.iSize = sizeof(AdlAdapterInfo);
    if (info(p->ctx, infos.data(), (int)(sizeof(AdlAdapterInfo) * n)) != 0) { delete p; return nullptr; }
    std::string want = format("DEV_%04X", deviceId);
    for (auto& i : infos) {
        std::string pnp = i.strPNPString;
        for (auto& ch : pnp) ch = (char)toupper((unsigned char)ch);
        if (i.iVendorID == 1002 || i.iVendorID == 0x1002) {
            if (pnp.find(want) != std::string::npos) { p->adapter = i.iAdapterIndex; break; }
            if (p->adapter < 0) p->adapter = i.iAdapterIndex;
        }
    }
    if (p->adapter < 0) { delete p; return nullptr; }
    AdlPMLogDataOutput probe{};
    probe.size = sizeof(probe);
    if (pm(p->ctx, p->adapter, &probe) != 0) { LOGW("ADL PMLog query unsupported on this GPU"); delete p; return nullptr; }
    if (auto mem = (FnMemInfo)sym("ADL2_Adapter_MemoryInfo2_Get")) {
        AdlMemoryInfo2 mi{};
        if (mem(p->ctx, p->adapter, &mi) == 0 && mi.strMemoryType[0]) memType = mi.strMemoryType;
    }
    return p;
}

} // namespace aether
