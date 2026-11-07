// GPU engine utilization and dedicated memory usage via Performance Data Helper counters
// ("GPU Engine" / "GPU Adapter Memory"), added with English names so they work on any Windows locale.
#include "Telemetry.h"
#include "../core/Log.h"
#include "../core/Strings.h"
#include <pdh.h>
#include <pdhmsg.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <map>
#include <cwctype>
#include <algorithm>
#include <vector>

namespace aether {

namespace {
std::wstring lower(std::wstring s) { for (auto& c : s) c = (wchar_t)towlower(c); return s; }

class PdhProvider : public ITelemetryProvider {
public:
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER engine = nullptr, memory = nullptr;
    std::wstring luidTag;
    uint64_t vramBytes = 0;
    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3;
    bool primed = false;

    ~PdhProvider() override { if (query) PdhCloseQuery(query); }
    const char* name() const override { return "Windows PDH"; }
    TelemetrySource source() const override { return TelemetrySource::PDH; }

    bool readArray(PDH_HCOUNTER c, std::vector<uint8_t>& buf, DWORD& count) {
        DWORD size = 0; count = 0;
        PDH_STATUS st = PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count, nullptr);
        if (st != (PDH_STATUS)PDH_MORE_DATA || size == 0) return false;
        buf.resize(size);
        return PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count,
                                            (PDH_FMT_COUNTERVALUE_ITEM_W*)buf.data()) == ERROR_SUCCESS;
    }

    void sample(TelemetrySample& s) override {
        if (PdhCollectQueryData(query) != ERROR_SUCCESS) return;
        if (!primed) { primed = true; return; }   // rate counters need two collections
        std::vector<uint8_t> buf;
        DWORD count = 0;
        if (engine && readArray(engine, buf, count)) {
            auto* items = (PDH_FMT_COUNTERVALUE_ITEM_W*)buf.data();
            std::map<std::wstring, double> perEngine;   // key: engine instance without pid
            for (DWORD i = 0; i < count; ++i) {
                std::wstring n = lower(items[i].szName);
                if (n.find(luidTag) == std::wstring::npos || items[i].FmtValue.CStatus != ERROR_SUCCESS) continue;
                size_t pos = n.find(L"_luid_");
                std::wstring key = pos == std::wstring::npos ? n : n.substr(pos);
                perEngine[key] += items[i].FmtValue.doubleValue;
            }
            double best = 0;
            for (auto& kv : perEngine) best = std::max(best, kv.second);
            if (!perEngine.empty()) s.gpuLoad = (float)std::min(100.0, best);
        }
        if (memory && readArray(memory, buf, count)) {
            auto* items = (PDH_FMT_COUNTERVALUE_ITEM_W*)buf.data();
            for (DWORD i = 0; i < count; ++i) {
                std::wstring n = lower(items[i].szName);
                if (n.find(luidTag) != std::wstring::npos && items[i].FmtValue.CStatus == ERROR_SUCCESS) {
                    s.vramUsedMB = (float)(items[i].FmtValue.doubleValue / 1048576.0);
                    break;
                }
            }
        }
        if (vramBytes > 0) s.vramTotalMB = (float)(vramBytes / 1048576.0);
        if (!has(s.vramUsedMB) && adapter3) {   // fallback: this process' local segment usage
            DXGI_QUERY_VIDEO_MEMORY_INFO vi{};
            if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vi)))
                s.vramUsedMB = (float)(vi.CurrentUsage / 1048576.0);
        }
    }
};
} // namespace

ITelemetryProvider* createPdhProvider(const LUID& luid, uint64_t vramBytes) {
    auto* p = new PdhProvider();
    p->vramBytes = vramBytes;
    p->luidTag = lower(wformat(L"luid_0x%08X_0x%08X", (unsigned)luid.HighPart, (unsigned)luid.LowPart));
    if (PdhOpenQueryW(nullptr, 0, &p->query) != ERROR_SUCCESS) { delete p; return nullptr; }
    if (PdhAddEnglishCounterW(p->query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &p->engine) != ERROR_SUCCESS) {
        p->engine = nullptr;
        LOGW("PDH GPU Engine counter unavailable");
    }
    if (PdhAddEnglishCounterW(p->query, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &p->memory) != ERROR_SUCCESS) {
        p->memory = nullptr;
        LOGW("PDH GPU Adapter Memory counter unavailable");
    }
    Microsoft::WRL::ComPtr<IDXGIFactory1> f;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)f.GetAddressOf()))) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> a;
        for (UINT i = 0; f->EnumAdapters1(i, a.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 d{};
            a->GetDesc1(&d);
            if (d.AdapterLuid.LowPart == luid.LowPart && d.AdapterLuid.HighPart == luid.HighPart) { a.As(&p->adapter3); break; }
        }
    }
    PdhCollectQueryData(p->query);
    return p;
}

} // namespace aether
