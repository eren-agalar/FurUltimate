#pragma once
#include "../gpu/GpuDevice.h"
#include <string>
#include <vector>
#include <cstdint>

namespace aether {

// Every string is empty when the value could not be detected; the UI renders it as "N/A".
struct GpuHardware {
    AdapterInfo adapter;
    std::string driverDate, driverProvider, driverVersionInf, pnpId, location;
    int pcieCurGen = -1, pcieCurWidth = -1, pcieMaxGen = -1, pcieMaxWidth = -1;
    std::string memoryType;
    std::string architecture;
    bool architectureInferred = false;     // true when derived from the model name instead of a driver API
    std::string d3d12FeatureLevel, shaderModel;
    bool directMLAvailable = false, vulkanLoader = false, openclLoader = false;
};

struct CpuHardware { std::string brand, vendor; int cores = 0, threads = 0; int baseMHz = 0; };
struct MemoryHardware {
    uint64_t totalBytes = 0, availBytes = 0;
    std::string type;            // DDR4 / DDR5 … from SMBIOS
    int speedMTs = 0;            // configured speed
    int modules = 0;
};
struct DisplayHardware { std::string adapter, device; int width = 0, height = 0, refreshHz = 0, bpp = 0; bool primary = false; };
struct OsHardware { std::string name, version, build; };

struct SystemHardware {
    std::vector<GpuHardware> gpus;
    int primaryGpu = 0;
    CpuHardware cpu;
    MemoryHardware memory;
    std::vector<DisplayHardware> displays;
    OsHardware os;
};

class HardwareDetector {
public:
    static SystemHardware detect();
    static std::string inferArchitecture(const std::string& vendor, const std::string& name);
    static const char* smbiosMemoryType(int code);
};

} // namespace aether
