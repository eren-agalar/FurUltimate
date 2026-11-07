// NVIDIA telemetry through NVML (nvml.dll ships with every NVIDIA display driver). Loaded dynamically;
// no NVIDIA SDK is required at build time. Only documented NVML entry points are used.
#include "Telemetry.h"
#include "../core/Log.h"
#include "../core/Strings.h"

namespace aether {

namespace {
typedef void* nvmlDevice_t;
typedef int nvmlReturn_t;
struct nvmlUtilization_t { unsigned int gpu, memory; };
struct nvmlMemory_t { unsigned long long total, free, used; };
struct nvmlPciInfo_t {
    char busIdLegacy[16];
    unsigned int domain, bus, device, pciDeviceId, pciSubSystemId;
    char busId[32];
};
enum { NVML_TEMPERATURE_GPU = 0, NVML_CLOCK_GRAPHICS = 0, NVML_CLOCK_MEM = 2 };

using FnInit = nvmlReturn_t (*)();
using FnCount = nvmlReturn_t (*)(unsigned int*);
using FnHandle = nvmlReturn_t (*)(unsigned int, nvmlDevice_t*);
using FnPci = nvmlReturn_t (*)(nvmlDevice_t, nvmlPciInfo_t*);
using FnTemp = nvmlReturn_t (*)(nvmlDevice_t, int, unsigned int*);
using FnUtil = nvmlReturn_t (*)(nvmlDevice_t, nvmlUtilization_t*);
using FnClock = nvmlReturn_t (*)(nvmlDevice_t, int, unsigned int*);
using FnUint = nvmlReturn_t (*)(nvmlDevice_t, unsigned int*);
using FnMem = nvmlReturn_t (*)(nvmlDevice_t, nvmlMemory_t*);
using FnStr = nvmlReturn_t (*)(char*, unsigned int);
using FnFan = nvmlReturn_t (*)(nvmlDevice_t, unsigned int*);

class NvmlProvider : public ITelemetryProvider {
public:
    HMODULE lib = nullptr;
    nvmlDevice_t dev = nullptr;
    FnTemp getTemp = nullptr; FnUtil getUtil = nullptr; FnClock getClock = nullptr; FnUint getPower = nullptr;
    FnFan getFan = nullptr; FnMem getMem = nullptr; FnUint getPcieGen = nullptr, getPcieWidth = nullptr;
    using FnShutdown = nvmlReturn_t (*)();
    FnShutdown shutdownFn = nullptr;

    ~NvmlProvider() override {
        if (shutdownFn) shutdownFn();
        if (lib) FreeLibrary(lib);
    }
    const char* name() const override { return "NVIDIA NVML"; }
    TelemetrySource source() const override { return TelemetrySource::NVML; }
    void sample(TelemetrySample& s) override {
        unsigned int v = 0;
        if (getTemp && getTemp(dev, NVML_TEMPERATURE_GPU, &v) == 0) s.gpuTemp = (float)v;
        nvmlUtilization_t u{};
        if (getUtil && getUtil(dev, &u) == 0) { s.gpuLoad = (float)u.gpu; s.memCtrlLoad = (float)u.memory; }
        if (getClock && getClock(dev, NVML_CLOCK_GRAPHICS, &v) == 0) s.coreClockMHz = (float)v;
        if (getClock && getClock(dev, NVML_CLOCK_MEM, &v) == 0) s.memClockMHz = (float)v;
        if (getPower && getPower(dev, &v) == 0) s.powerW = v / 1000.0f;
        if (getFan && getFan(dev, &v) == 0) s.fanPct = (float)v;
        nvmlMemory_t m{};
        if (getMem && getMem(dev, &m) == 0) { s.vramUsedMB = m.used / 1048576.0f; s.vramTotalMB = m.total / 1048576.0f; }
        if (getPcieGen && getPcieGen(dev, &v) == 0) s.pcieGen = (float)v;
        if (getPcieWidth && getPcieWidth(dev, &v) == 0) s.pcieWidth = (float)v;
    }
};

const char* nvArchName(unsigned int a) {
    switch (a) {
        case 2: return "Kepler"; case 3: return "Maxwell"; case 4: return "Pascal"; case 5: return "Volta";
        case 6: return "Turing"; case 7: return "Ampere"; case 8: return "Ada Lovelace"; case 9: return "Hopper";
        case 10: return "Blackwell";
        default: return nullptr;
    }
}
} // namespace

ITelemetryProvider* createNvmlProvider(uint32_t deviceId, uint32_t subSysId, std::string& arch, std::string& driver,
                                       float& powerLimit) {
    HMODULE lib = LoadLibraryW(L"nvml.dll");
    if (!lib) lib = LoadLibraryW(L"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll");
    if (!lib) { LOGW("NVML not found; NVIDIA vendor sensors unavailable"); return nullptr; }
    auto sym = [&](const char* n) { return (void*)GetProcAddress(lib, n); };
    auto init = (FnInit)sym("nvmlInit_v2");
    auto count = (FnCount)sym("nvmlDeviceGetCount_v2");
    auto handle = (FnHandle)sym("nvmlDeviceGetHandleByIndex_v2");
    auto pci = (FnPci)sym("nvmlDeviceGetPciInfo_v3");
    if (!pci) pci = (FnPci)sym("nvmlDeviceGetPciInfo_v2");
    if (!init || !count || !handle || init() != 0) { FreeLibrary(lib); LOGW("nvmlInit failed"); return nullptr; }
    auto* p = new NvmlProvider();
    p->lib = lib;
    p->shutdownFn = (NvmlProvider::FnShutdown)sym("nvmlShutdown");
    unsigned int n = 0;
    count(&n);
    for (unsigned int i = 0; i < n; ++i) {
        nvmlDevice_t d = nullptr;
        if (handle(i, &d) != 0) continue;
        nvmlPciInfo_t info{};
        if (pci && pci(d, &info) == 0) {
            uint32_t devId = info.pciDeviceId >> 16;
            if (devId == deviceId && (subSysId == 0 || info.pciSubSystemId == subSysId)) { p->dev = d; break; }
        }
        if (!p->dev) p->dev = d;   // fall back to the first device
    }
    if (!p->dev) { delete p; return nullptr; }
    p->getTemp = (FnTemp)sym("nvmlDeviceGetTemperature");
    p->getUtil = (FnUtil)sym("nvmlDeviceGetUtilizationRates");
    p->getClock = (FnClock)sym("nvmlDeviceGetClockInfo");
    p->getPower = (FnUint)sym("nvmlDeviceGetPowerUsage");
    p->getFan = (FnFan)sym("nvmlDeviceGetFanSpeed");
    p->getMem = (FnMem)sym("nvmlDeviceGetMemoryInfo");
    p->getPcieGen = (FnUint)sym("nvmlDeviceGetCurrPcieLinkGeneration");
    p->getPcieWidth = (FnUint)sym("nvmlDeviceGetCurrPcieLinkWidth");
    if (auto getArch = (FnUint)sym("nvmlDeviceGetArchitecture")) {
        unsigned int a = 0;
        if (getArch(p->dev, &a) == 0 && nvArchName(a)) arch = nvArchName(a);
    }
    if (auto drv = (FnStr)sym("nvmlSystemGetDriverVersion")) {
        char buf[96] = {};
        if (drv(buf, sizeof(buf)) == 0) driver = buf;
    }
    if (auto lim = (FnUint)sym("nvmlDeviceGetPowerManagementLimit")) {
        unsigned int mw = 0;
        if (lim(p->dev, &mw) == 0 && mw > 0) powerLimit = mw / 1000.0f;
    }
    return p;
}

} // namespace aether
