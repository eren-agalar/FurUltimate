#include "GpuDevice.h"
#include "../core/Log.h"
#include "../core/Strings.h"
#include <d3d12.h>
#include <algorithm>

namespace aether {

const char* vendorNameFromId(uint32_t v) {
    switch (v) {
        case 0x10DE: return "NVIDIA";
        case 0x1002: case 0x1022: return "AMD";
        case 0x8086: return "Intel";
        case 0x1414: return "Microsoft";
        case 0x5143: return "Qualcomm";
        case 0x13B5: return "ARM";
        case 0x1AE0: return "Google";
        case 0x15AD: return "VMware";
        case 0x80EE: return "Oracle";
        default: return "Bilinmeyen";
    }
}

std::string AdapterInfo::vendorName() const { return vendorNameFromId(vendorId); }

const char* featureLevelName(D3D_FEATURE_LEVEL fl) {
    switch (fl) {
        case D3D_FEATURE_LEVEL_12_1: return "12_1";
        case D3D_FEATURE_LEVEL_12_0: return "12_0";
        case D3D_FEATURE_LEVEL_11_1: return "11_1";
        case D3D_FEATURE_LEVEL_11_0: return "11_0";
        case D3D_FEATURE_LEVEL_10_1: return "10_1";
        case D3D_FEATURE_LEVEL_10_0: return "10_0";
        default: return "N/A";
    }
}

static D3D_FEATURE_LEVEL probeFeatureLevel(IDXGIAdapter1* adapter) {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1,
                                        D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got{};
    for (int start = 0; start < 3; ++start) {   // older runtimes reject 12_x / 11_1 with E_INVALIDARG
        HRESULT hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, levels + start * 2,
                                       (UINT)(std::size(levels) - start * 2), D3D11_SDK_VERSION, nullptr, &got, nullptr);
        if (SUCCEEDED(hr)) return got;
        if (hr != E_INVALIDARG) break;
    }
    return (D3D_FEATURE_LEVEL)0;
}

static bool probeD3D12(IDXGIAdapter1* adapter) {
    HMODULE m = LoadLibraryW(L"d3d12.dll");
    if (!m) return false;
    using Fn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    auto fn = (Fn)(void*)GetProcAddress(m, "D3D12CreateDevice");
    bool ok = fn && SUCCEEDED(fn(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr));
    FreeLibrary(m);
    return ok;
}

std::vector<AdapterInfo> GpuDeviceManager::enumerateAdapters() {
    std::vector<AdapterInfo> list;
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)factory.GetAddressOf()))) {
        LOGE("CreateDXGIFactory1 failed");
        return list;
    }
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 d{};
        if (FAILED(adapter->GetDesc1(&d))) continue;
        AdapterInfo a;
        a.name = d.Description;
        a.vendorId = d.VendorId; a.deviceId = d.DeviceId; a.subSysId = d.SubSysId; a.revision = d.Revision;
        a.luid = d.AdapterLuid;
        a.dedicatedVideoMemory = d.DedicatedVideoMemory;
        a.dedicatedSystemMemory = d.DedicatedSystemMemory;
        a.sharedSystemMemory = d.SharedSystemMemory;
        a.software = (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 || (d.VendorId == 0x1414 && d.DeviceId == 0x8c);
        LARGE_INTEGER umd{};
        if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd))) {
            a.driverVersion = format("%u.%u.%u.%u", HIWORD(umd.HighPart), LOWORD(umd.HighPart), HIWORD(umd.LowPart),
                                     LOWORD(umd.LowPart));
        }
        a.maxFeatureLevel = probeFeatureLevel(adapter.Get());
        a.d3d12Capable = !a.software && probeD3D12(adapter.Get());
        list.push_back(a);
    }
    return list;
}

bool GpuDeviceManager::primaryAdapter(AdapterInfo& out) {
    auto list = enumerateAdapters();
    // Prefer DXGI 1.6 high-performance ordering when available.
    ComPtr<IDXGIFactory6> f6;
    ComPtr<IDXGIFactory1> f1;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)f1.GetAddressOf())) &&
        SUCCEEDED(f1.As(&f6))) {
        ComPtr<IDXGIAdapter1> a;
        for (UINT i = 0; SUCCEEDED(f6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                                  __uuidof(IDXGIAdapter1), (void**)a.ReleaseAndGetAddressOf()));
             ++i) {
            DXGI_ADAPTER_DESC1 d{};
            a->GetDesc1(&d);
            for (auto& info : list) {
                if (info.luid.LowPart == d.AdapterLuid.LowPart && info.luid.HighPart == d.AdapterLuid.HighPart &&
                    !info.software) {
                    out = info;
                    return true;
                }
            }
        }
    }
    const AdapterInfo* best = nullptr;
    for (auto& a : list)
        if (!a.software && (!best || a.dedicatedVideoMemory > best->dedicatedVideoMemory)) best = &a;
    if (!best && !list.empty()) best = &list.front();
    if (!best) return false;
    out = *best;
    return true;
}

ComPtr<IDXGIAdapter1> GpuDeviceManager::findAdapter(const LUID& luid) {
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)factory.GetAddressOf()))) return nullptr;
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 d{};
        adapter->GetDesc1(&d);
        if (d.AdapterLuid.LowPart == luid.LowPart && d.AdapterLuid.HighPart == luid.HighPart) return adapter;
    }
    return nullptr;
}

HRESULT GpuDeviceManager::createDevice(const LUID& luid, ComPtr<ID3D11Device>& device,
                                       ComPtr<ID3D11DeviceContext>& context, D3D_FEATURE_LEVEL& featureLevel) {
    ComPtr<IDXGIAdapter1> adapter = findAdapter(luid);
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(adapter.Get(), adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   flags, levels, 2, D3D11_SDK_VERSION, device.ReleaseAndGetAddressOf(), &featureLevel,
                                   context.ReleaseAndGetAddressOf());
    if (hr == E_INVALIDARG) {
        hr = D3D11CreateDevice(adapter.Get(), adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr,
                               flags, levels + 1, 1, D3D11_SDK_VERSION, device.ReleaseAndGetAddressOf(), &featureLevel,
                               context.ReleaseAndGetAddressOf());
    }
    if (FAILED(hr)) LOGE("D3D11CreateDevice failed: %s", hresultText(hr).c_str());
    return hr;
}

bool GpuDeviceManager::tearingSupported() {
    ComPtr<IDXGIFactory1> f1;
    ComPtr<IDXGIFactory5> f5;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)f1.GetAddressOf())) || FAILED(f1.As(&f5)))
        return false;
    BOOL allow = FALSE;
    if (FAILED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow)))) return false;
    return allow == TRUE;
}

std::string GpuDeviceManager::hresultText(HRESULT hr) {
    switch (hr) {
        case DXGI_ERROR_DEVICE_REMOVED: return "DXGI_ERROR_DEVICE_REMOVED";
        case DXGI_ERROR_DEVICE_HUNG: return "DXGI_ERROR_DEVICE_HUNG";
        case DXGI_ERROR_DEVICE_RESET: return "DXGI_ERROR_DEVICE_RESET";
        case DXGI_ERROR_DRIVER_INTERNAL_ERROR: return "DXGI_ERROR_DRIVER_INTERNAL_ERROR";
        case DXGI_ERROR_INVALID_CALL: return "DXGI_ERROR_INVALID_CALL";
        case E_OUTOFMEMORY: return "E_OUTOFMEMORY";
        case E_INVALIDARG: return "E_INVALIDARG";
        default: return format("HRESULT 0x%08lX", (unsigned long)hr);
    }
}

} // namespace aether
