#pragma once
#include <d3d11_1.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <cstdint>

namespace aether {

using Microsoft::WRL::ComPtr;

struct AdapterInfo {
    std::wstring name;
    uint32_t vendorId = 0, deviceId = 0, subSysId = 0, revision = 0;
    LUID luid{};
    uint64_t dedicatedVideoMemory = 0;
    uint64_t dedicatedSystemMemory = 0;
    uint64_t sharedSystemMemory = 0;
    bool software = false;
    std::string driverVersion;     // "31.0.15.5222" from UMD version, empty if unknown
    D3D_FEATURE_LEVEL maxFeatureLevel = (D3D_FEATURE_LEVEL)0;
    bool d3d12Capable = false;
    std::string vendorName() const;
};

const char* vendorNameFromId(uint32_t vendorId);
const char* featureLevelName(D3D_FEATURE_LEVEL fl);

// Adapter discovery and D3D11 device creation. Stateless helpers, thread-safe.
class GpuDeviceManager {
public:
    static std::vector<AdapterInfo> enumerateAdapters();
    // High-performance hardware adapter (DXGI 1.6 preference), or best fallback.
    static bool primaryAdapter(AdapterInfo& out);
    static ComPtr<IDXGIAdapter1> findAdapter(const LUID& luid);
    // Creates a device on the adapter with the given LUID; falls back to default hardware adapter.
    static HRESULT createDevice(const LUID& luid, ComPtr<ID3D11Device>& device, ComPtr<ID3D11DeviceContext>& context,
                                D3D_FEATURE_LEVEL& featureLevel);
    static bool tearingSupported();
    static std::string hresultText(HRESULT hr);
};

} // namespace aether
