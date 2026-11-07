#include "HardwareInfo.h"
#include "../core/Log.h"
#include "../core/Strings.h"
#include <windows.h>
#include <setupapi.h>
#include <initguid.h>
#include <devpkey.h>
#if defined(_MSC_VER)
#include <intrin.h>
static bool cpuidLeaf(unsigned int leaf, unsigned int* a, unsigned int* b, unsigned int* c, unsigned int* d) {
    int r[4]; __cpuid(r, (int)leaf); *a = r[0]; *b = r[1]; *c = r[2]; *d = r[3]; return true;
}
#define AETHER_POPCNT(x) (int)__popcnt64(x)
#else
#include <cpuid.h>
static bool cpuidLeaf(unsigned int leaf, unsigned int* a, unsigned int* b, unsigned int* c, unsigned int* d) { return __get_cpuid(leaf, a, b, c, d); }
#define AETHER_POPCNT(x) __builtin_popcountll(x)
#endif
#include <d3d12.h>
#include <algorithm>
#include <cstring>

namespace aether {

namespace {
const GUID kDisplayClass = {0x4d36e968, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
const DEVPROPKEY kPciCurSpeed = {{0x3ab22e31, 0x8264, 0x4b4e, {0x9a, 0xf5, 0xa8, 0xd2, 0xd8, 0xe3, 0x3e, 0x62}}, 9};
const DEVPROPKEY kPciCurWidth = {{0x3ab22e31, 0x8264, 0x4b4e, {0x9a, 0xf5, 0xa8, 0xd2, 0xd8, 0xe3, 0x3e, 0x62}}, 10};
const DEVPROPKEY kPciMaxSpeed = {{0x3ab22e31, 0x8264, 0x4b4e, {0x9a, 0xf5, 0xa8, 0xd2, 0xd8, 0xe3, 0x3e, 0x62}}, 11};
const DEVPROPKEY kPciMaxWidth = {{0x3ab22e31, 0x8264, 0x4b4e, {0x9a, 0xf5, 0xa8, 0xd2, 0xd8, 0xe3, 0x3e, 0x62}}, 12};

bool propString(HDEVINFO set, SP_DEVINFO_DATA& d, const DEVPROPKEY& key, std::string& out) {
    DEVPROPTYPE type = 0; DWORD size = 0;
    wchar_t buf[512] = {};
    if (!SetupDiGetDevicePropertyW(set, &d, &key, &type, (PBYTE)buf, sizeof(buf) - 2, &size, 0)) return false;
    if (type != DEVPROP_TYPE_STRING) return false;
    out = narrow(buf);
    return true;
}
bool propUint(HDEVINFO set, SP_DEVINFO_DATA& d, const DEVPROPKEY& key, uint32_t& out) {
    DEVPROPTYPE type = 0; DWORD size = 0; uint32_t v = 0;
    if (!SetupDiGetDevicePropertyW(set, &d, &key, &type, (PBYTE)&v, sizeof(v), &size, 0)) return false;
    if (type != DEVPROP_TYPE_UINT32) return false;
    out = v;
    return true;
}

void fillSetupApi(GpuHardware& g) {
    HDEVINFO set = SetupDiGetClassDevsW(&kDisplayClass, nullptr, nullptr, DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return;
    std::string want = format("VEN_%04X&DEV_%04X", g.adapter.vendorId, g.adapter.deviceId);
    std::string wantSub = format("SUBSYS_%08X", g.adapter.subSysId);
    SP_DEVINFO_DATA d{sizeof(SP_DEVINFO_DATA)};
    int bestScore = -1;
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &d); ++i) {
        wchar_t hw[1024] = {};
        if (!SetupDiGetDeviceRegistryPropertyW(set, &d, SPDRP_HARDWAREID, nullptr, (PBYTE)hw, sizeof(hw) - 4, nullptr)) continue;
        std::string ids;
        for (const wchar_t* p = hw; *p; p += wcslen(p) + 1) ids += narrow(p) + ";";
        for (auto& c : ids) c = (char)toupper((unsigned char)c);
        if (ids.find(want) == std::string::npos) continue;
        int score = ids.find(wantSub) != std::string::npos ? 2 : 1;
        if (score <= bestScore) continue;
        bestScore = score;
        g.pnpId = ids.substr(0, ids.find(';'));
        FILETIME ft{}; DEVPROPTYPE type = 0; DWORD size = 0;
        if (SetupDiGetDevicePropertyW(set, &d, &DEVPKEY_Device_DriverDate, &type, (PBYTE)&ft, sizeof(ft), &size, 0) &&
            type == DEVPROP_TYPE_FILETIME) {
            SYSTEMTIME st; FileTimeToSystemTime(&ft, &st);
            g.driverDate = format("%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
        }
        propString(set, d, DEVPKEY_Device_DriverProvider, g.driverProvider);
        propString(set, d, DEVPKEY_Device_DriverVersion, g.driverVersionInf);
        propString(set, d, DEVPKEY_Device_LocationInfo, g.location);
        uint32_t v = 0;
        if (propUint(set, d, kPciCurSpeed, v)) g.pcieCurGen = (int)v;
        if (propUint(set, d, kPciCurWidth, v)) g.pcieCurWidth = (int)v;
        if (propUint(set, d, kPciMaxSpeed, v)) g.pcieMaxGen = (int)v;
        if (propUint(set, d, kPciMaxWidth, v)) g.pcieMaxWidth = (int)v;
    }
    SetupDiDestroyDeviceInfoList(set);
}

void probeD3D12(GpuHardware& g) {
    HMODULE m = LoadLibraryW(L"d3d12.dll");
    if (!m) return;
    using Fn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    auto create = (Fn)(void*)GetProcAddress(m, "D3D12CreateDevice");
    ComPtr<IDXGIAdapter1> adapter = GpuDeviceManager::findAdapter(g.adapter.luid);
    if (create && adapter) {
        ComPtr<ID3D12Device> dev;
        if (SUCCEEDED(create(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)dev.GetAddressOf()))) {
            D3D_FEATURE_LEVEL levels[] = {(D3D_FEATURE_LEVEL)0xc200, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0,
                                          D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
            D3D12_FEATURE_DATA_FEATURE_LEVELS fl{};
            fl.NumFeatureLevels = 5; fl.pFeatureLevelsRequested = levels;
            if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &fl, sizeof(fl)))) {
                g.d3d12FeatureLevel = fl.MaxSupportedFeatureLevel == (D3D_FEATURE_LEVEL)0xc200 ? "12_2"
                                                                                                 : featureLevelName(fl.MaxSupportedFeatureLevel);
            }
            for (int sm = 0x69; sm >= 0x60; --sm) {
                D3D12_FEATURE_DATA_SHADER_MODEL d{(D3D_SHADER_MODEL)sm};
                if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &d, sizeof(d)))) {
                    g.shaderModel = format("%d.%d", (int)d.HighestShaderModel >> 4, (int)d.HighestShaderModel & 0xF);
                    break;
                }
            }
        }
    }
    FreeLibrary(m);
}

bool systemDllExists(const wchar_t* name) {
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    std::wstring p = std::wstring(sys) + L"\\" + name;
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

void detectCpu(CpuHardware& c) {
    unsigned int a, b, cc, d;
    char vendor[13] = {};
    if (cpuidLeaf(0, &a, &b, &cc, &d)) {
        std::memcpy(vendor, &b, 4); std::memcpy(vendor + 4, &d, 4); std::memcpy(vendor + 8, &cc, 4);
        c.vendor = vendor;
    }
    char brand[49] = {};
    if (cpuidLeaf(0x80000000, &a, &b, &cc, &d) && a >= 0x80000004) {
        for (unsigned int i = 0; i < 3; ++i) {
            cpuidLeaf(0x80000002 + i, &a, &b, &cc, &d);
            std::memcpy(brand + i * 16, &a, 4); std::memcpy(brand + i * 16 + 4, &b, 4);
            std::memcpy(brand + i * 16 + 8, &cc, 4); std::memcpy(brand + i * 16 + 12, &d, 4);
        }
        c.brand = trim(brand);
    }
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
    std::vector<uint8_t> buf(len);
    if (len && GetLogicalProcessorInformationEx(RelationProcessorCore, (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buf.data(), &len)) {
        for (DWORD off = 0; off < len;) {
            auto* info = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(buf.data() + off);
            if (info->Relationship == RelationProcessorCore) {
                ++c.cores;
                for (WORD g = 0; g < info->Processor.GroupCount; ++g)
                    c.threads += AETHER_POPCNT(info->Processor.GroupMask[g].Mask);
            }
            off += info->Size;
        }
    }
    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &k) == ERROR_SUCCESS) {
        DWORD mhz = 0, sz = sizeof(mhz);
        if (RegQueryValueExW(k, L"~MHz", nullptr, nullptr, (LPBYTE)&mhz, &sz) == ERROR_SUCCESS) c.baseMHz = (int)mhz;
        RegCloseKey(k);
    }
}

void detectMemory(MemoryHardware& m) {
    MEMORYSTATUSEX ms{sizeof(ms)};
    if (GlobalMemoryStatusEx(&ms)) { m.totalBytes = ms.ullTotalPhys; m.availBytes = ms.ullAvailPhys; }
    UINT size = GetSystemFirmwareTable(0x52534D42u, 0, nullptr, 0);
    if (size == 0) return;
    std::vector<uint8_t> buf(size);
    if (GetSystemFirmwareTable(0x52534D42u, 0, buf.data(), size) != size || size < 8) return;
    uint32_t tableLen = *(uint32_t*)(buf.data() + 4);
    const uint8_t* p = buf.data() + 8;
    const uint8_t* end = p + std::min<uint32_t>(tableLen, size - 8);
    while (p + 4 <= end) {
        uint8_t type = p[0], len = p[1];
        if (len < 4 || p + len > end) break;
        if (type == 17 && len >= 0x17) {
            uint16_t sizeField = *(uint16_t*)(p + 0x0C);
            if (sizeField != 0 && sizeField != 0xFFFF) {
                ++m.modules;
                if (m.type.empty()) m.type = HardwareDetector::smbiosMemoryType(p[0x12]);
                int speed = *(uint16_t*)(p + 0x15);
                if (len >= 0x22) { int conf = *(uint16_t*)(p + 0x20); if (conf > 0 && conf != 0xFFFF) speed = conf; }
                if (speed > 0 && speed != 0xFFFF) m.speedMTs = std::max(m.speedMTs, speed);
            }
        }
        if (type == 127) break;
        const uint8_t* s = p + len;   // skip string-set (terminated by double NUL)
        while (s + 1 < end && !(s[0] == 0 && s[1] == 0)) ++s;
        p = s + 2;
    }
}

void detectDisplays(std::vector<DisplayHardware>& out) {
    DISPLAY_DEVICEW dd{sizeof(dd)};
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); ++i) {
        if (!(dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) { dd.cb = sizeof(dd); continue; }
        DisplayHardware h;
        h.adapter = narrow(dd.DeviceString);
        h.primary = (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0;
        DEVMODEW dm{}; dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm)) {
            h.width = dm.dmPelsWidth; h.height = dm.dmPelsHeight; h.refreshHz = dm.dmDisplayFrequency; h.bpp = dm.dmBitsPerPel;
        }
        DISPLAY_DEVICEW mon{sizeof(mon)};
        if (EnumDisplayDevicesW(dd.DeviceName, 0, &mon, 0)) h.device = narrow(mon.DeviceString);
        out.push_back(h);
        dd.cb = sizeof(dd);
    }
}

void detectOs(OsHardware& o) {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    auto fn = (RtlGetVersionFn)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    OSVERSIONINFOW v{sizeof(v)};
    if (fn && fn(&v) == 0) {
        o.build = format("%lu", v.dwBuildNumber);
        o.name = v.dwMajorVersion >= 10 ? (v.dwBuildNumber >= 22000 ? "Windows 11" : "Windows 10")
                                        : format("Windows %lu.%lu", v.dwMajorVersion, v.dwMinorVersion);
    }
    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &k) == ERROR_SUCCESS) {
        wchar_t buf[128] = {}; DWORD sz = sizeof(buf) - 2;
        if (RegQueryValueExW(k, L"DisplayVersion", nullptr, nullptr, (LPBYTE)buf, &sz) == ERROR_SUCCESS) o.version = narrow(buf);
        RegCloseKey(k);
    }
}
} // namespace

const char* HardwareDetector::smbiosMemoryType(int code) {
    switch (code) {
        case 0x0F: return "SDRAM"; case 0x12: return "DDR"; case 0x13: return "DDR2"; case 0x18: return "DDR3";
        case 0x1A: return "DDR4"; case 0x1B: return "LPDDR"; case 0x1C: return "LPDDR2"; case 0x1D: return "LPDDR3";
        case 0x1E: return "LPDDR4"; case 0x22: return "DDR5"; case 0x23: return "LPDDR5";
        default: return "";
    }
}

std::string HardwareDetector::inferArchitecture(const std::string& vendor, const std::string& nameIn) {
    std::string n = nameIn;
    for (auto& c : n) c = (char)toupper((unsigned char)c);
    auto has = [&](const char* s) { return n.find(s) != std::string::npos; };
    if (vendor == "NVIDIA") {
        if (has("RTX 50") || has("RTX PRO") || has("B100") || has("B200")) return "Blackwell";
        if (has("RTX 40") || has("RTX 2000 ADA") || has("RTX 4000 ADA") || has("RTX 6000 ADA") || has("L40")) return "Ada Lovelace";
        if (has("RTX 30") || has("RTX A") || has("A100")) return "Ampere";
        if (has("RTX 20") || has("GTX 16") || has("TITAN RTX") || has("QUADRO RTX")) return "Turing";
        if (has("GTX 10") || has("TITAN X")) return "Pascal";
        if (has("GTX 9") || has("GTX 750")) return "Maxwell";
        if (has("GTX 6") || has("GTX 7") || has("GT 7")) return "Kepler";
        if (has("GTX 4") || has("GTX 5")) return "Fermi";
    } else if (vendor == "AMD") {
        int rx = -1;
        size_t pos = n.find("RX ");
        if (pos != std::string::npos) {
            int v = 0, digits = 0;
            for (size_t i = pos + 3; i < n.size() && isdigit((unsigned char)n[i]); ++i) { v = v * 10 + (n[i] - '0'); ++digits; }
            if (digits) rx = v;
        }
        if (rx >= 9000) return "RDNA 4";
        if (rx >= 7000 && rx < 8000) return "RDNA 3";
        if (rx >= 6000 && rx < 7000) return "RDNA 2";
        if (rx >= 5000 && rx < 6000) return "RDNA";
        if (rx >= 400 && rx < 600) return "GCN 4 (Polaris)";
        if (has("VEGA")) return "GCN 5 (Vega)";
        if (has("890M") || has("880M")) return "RDNA 3.5";
        if (has("780M") || has("760M") || has("740M")) return "RDNA 3";
    } else if (vendor == "Intel") {
        if (has("ARC B") || has("ARC(TM) B")) return "Xe2-HPG (Battlemage)";
        if (has("ARC A") || has("ARC(TM) A")) return "Xe-HPG (Alchemist)";
        if (has("ARC(TM) 1") || has("ARC 1")) return "Xe2-LPG";
        if (has("IRIS") && has("XE")) return "Xe-LP";
        if (has("UHD")) return "Gen 9.5 / Gen 11 / Xe-LP";
    }
    return "";
}

SystemHardware HardwareDetector::detect() {
    SystemHardware sys;
    auto adapters = GpuDeviceManager::enumerateAdapters();
    AdapterInfo primary;
    bool havePrimary = GpuDeviceManager::primaryAdapter(primary);
    for (auto& a : adapters) {
        if (a.software) continue;
        GpuHardware g;
        g.adapter = a;
        fillSetupApi(g);
        probeD3D12(g);
        g.architecture = inferArchitecture(a.vendorName(), narrow(a.name));
        g.architectureInferred = !g.architecture.empty();
        g.directMLAvailable = systemDllExists(L"DirectML.dll");
        g.vulkanLoader = systemDllExists(L"vulkan-1.dll");
        g.openclLoader = systemDllExists(L"OpenCL.dll");
        if (havePrimary && a.luid.LowPart == primary.luid.LowPart && a.luid.HighPart == primary.luid.HighPart)
            sys.primaryGpu = (int)sys.gpus.size();
        sys.gpus.push_back(g);
    }
    if (sys.gpus.empty()) {   // only a software adapter (e.g. VM without GPU): keep it so the UI can say so
        for (auto& a : adapters) { GpuHardware g; g.adapter = a; sys.gpus.push_back(g); break; }
    }
    detectCpu(sys.cpu);
    detectMemory(sys.memory);
    detectDisplays(sys.displays);
    detectOs(sys.os);
    LOGI("Hardware detection: %zu GPU(s), CPU '%s', RAM %s", sys.gpus.size(), sys.cpu.brand.c_str(),
         formatBytes(sys.memory.totalBytes).c_str());
    return sys;
}

} // namespace aether
