#include "Telemetry.h"
#include "../core/Log.h"

namespace aether {

static const size_t kRingCapacity = 14400;   // 2 h at 500 ms, 1 h at 250 ms

const char* sourceName(TelemetrySource s) {
    switch (s) {
        case TelemetrySource::NVML: return "NVIDIA NVML";
        case TelemetrySource::ADL: return "AMD ADL";
        case TelemetrySource::D3DKMT: return "Windows D3DKMT";
        case TelemetrySource::PDH: return "Windows PDH";
        case TelemetrySource::DXGI: return "DXGI";
        case TelemetrySource::Workload: return "FurUltimate";
        default: return "N/A";
    }
}

TelemetryHub& TelemetryHub::instance() { static TelemetryHub hub; return hub; }

void TelemetryHub::start(const LUID& luid, uint32_t vendorId, uint32_t deviceId, uint32_t subSysId, uint64_t vramBytes) {
    if (m_running) return;
    QueryPerformanceFrequency(&m_freq);
    QueryPerformanceCounter(&m_t0);
    m_ring.assign(kRingCapacity, TelemetrySample{});
    m_ringHead = m_ringCount = 0;
    // Order defines priority: earlier providers win for a metric (vendor APIs are most accurate).
    if (vendorId == 0x10DE) {
        if (auto* p = createNvmlProvider(deviceId, subSysId, m_arch, m_vendorDriver, m_powerLimit)) m_providers.push_back(p);
    } else if (vendorId == 0x1002) {
        if (auto* p = createAdlProvider(deviceId, m_memType)) m_providers.push_back(p);
    }
    if (auto* p = createD3dkmtProvider(luid)) m_providers.push_back(p);
    if (auto* p = createPdhProvider(luid, vramBytes)) m_providers.push_back(p);
    for (auto* p : m_providers) LOGI("Telemetry provider active: %s", p->name());
    m_running = true;
    m_thread = std::thread([this] { run(); });
}

void TelemetryHub::stop() {
    if (!m_running) return;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        m_running = false;
    }
    m_cv.notify_all();
    if (m_thread.joinable()) m_thread.join();
    for (auto* p : m_providers) delete p;
    m_providers.clear();
}

static void track(TelemetrySource& slot, float before, float after, TelemetrySource src) {
    if (slot == TelemetrySource::None && !has(before) && has(after)) slot = src;
}

void TelemetryHub::run() {
    while (m_running) {
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        TelemetrySample s;
        s.t = double(now.QuadPart - m_t0.QuadPart) / double(m_freq.QuadPart);
        SensorSources src;
        for (auto* p : m_providers) {
            TelemetrySample before = s;
            // Providers fill only empty fields so higher-priority sources are kept.
            TelemetrySample tmp;
            p->sample(tmp);
            auto merge = [](float& dst, float v) { if (!has(dst) && has(v)) dst = v; };
            merge(s.gpuTemp, tmp.gpuTemp); merge(s.hotspotTemp, tmp.hotspotTemp); merge(s.memTemp, tmp.memTemp);
            merge(s.gpuLoad, tmp.gpuLoad); merge(s.memCtrlLoad, tmp.memCtrlLoad);
            merge(s.vramUsedMB, tmp.vramUsedMB); merge(s.vramTotalMB, tmp.vramTotalMB);
            merge(s.coreClockMHz, tmp.coreClockMHz); merge(s.memClockMHz, tmp.memClockMHz);
            merge(s.powerW, tmp.powerW); merge(s.powerPct, tmp.powerPct);
            merge(s.fanRpm, tmp.fanRpm); merge(s.fanPct, tmp.fanPct); merge(s.voltageMv, tmp.voltageMv);
            merge(s.pcieGen, tmp.pcieGen); merge(s.pcieWidth, tmp.pcieWidth);
            TelemetrySource ps = p->source();
            track(src.gpuTemp, before.gpuTemp, s.gpuTemp, ps);
            track(src.hotspot, before.hotspotTemp, s.hotspotTemp, ps);
            track(src.memTemp, before.memTemp, s.memTemp, ps);
            track(src.load, before.gpuLoad, s.gpuLoad, ps);
            track(src.vram, before.vramUsedMB, s.vramUsedMB, ps);
            track(src.coreClock, before.coreClockMHz, s.coreClockMHz, ps);
            track(src.memClock, before.memClockMHz, s.memClockMHz, ps);
            track(src.power, before.powerW, s.powerW, ps);
            if (src.power == TelemetrySource::None) track(src.power, before.powerPct, s.powerPct, ps);
            track(src.fan, before.fanRpm, s.fanRpm, ps);
            if (src.fan == TelemetrySource::None) track(src.fan, before.fanPct, s.fanPct, ps);
            track(src.voltage, before.voltageMv, s.voltageMv, ps);
            track(src.pcie, before.pcieGen, s.pcieGen, ps);
        }
        // Frame statistics are only valid while a workload is publishing them (stale after 2 s).
        LARGE_INTEGER n2; QueryPerformanceCounter(&n2);
        int64_t stamp = m_frameStamp.load();
        if (stamp != 0 && double(n2.QuadPart - stamp) / double(m_freq.QuadPart) < 2.0) {
            s.fps = m_fps.load();
            s.frameTimeMs = m_frameMs.load();
        }
        {
            std::lock_guard<std::mutex> l(m_mutex);
            m_latest = s;
            m_sources = src;
            m_ring[m_ringHead] = s;
            m_ringHead = (m_ringHead + 1) % kRingCapacity;
            if (m_ringCount < kRingCapacity) ++m_ringCount;
        }
        std::unique_lock<std::mutex> lk(m_mutex);
        m_cv.wait_for(lk, std::chrono::milliseconds(m_intervalMs.load()), [this] { return !m_running.load(); });
    }
}

TelemetrySample TelemetryHub::latest() const { std::lock_guard<std::mutex> l(m_mutex); return m_latest; }
SensorSources TelemetryHub::sources() const { std::lock_guard<std::mutex> l(m_mutex); return m_sources; }

void TelemetryHub::history(double window, std::vector<TelemetrySample>& out) const {
    std::lock_guard<std::mutex> l(m_mutex);
    out.clear();
    if (m_ringCount == 0) return;
    double newest = m_latest.t;
    size_t start = (m_ringHead + kRingCapacity - m_ringCount) % kRingCapacity;
    for (size_t i = 0; i < m_ringCount; ++i) {
        const auto& s = m_ring[(start + i) % kRingCapacity];
        if (newest - s.t <= window) out.push_back(s);
    }
}

std::vector<std::string> TelemetryHub::providerNames() const {
    std::vector<std::string> v;
    for (auto* p : m_providers) v.push_back(p->name());
    return v;
}

void TelemetryHub::publishFrameStats(float fps, float ms) {
    m_fps = fps; m_frameMs = ms;
    LARGE_INTEGER n; QueryPerformanceCounter(&n);
    m_frameStamp = n.QuadPart;
}
void TelemetryHub::clearFrameStats() { m_frameStamp = 0; m_fps = NA; m_frameMs = NA; }

std::string TelemetryHub::vendorArchitecture() const { return m_arch; }
std::string TelemetryHub::vendorMemoryType() const { return m_memType; }
std::string TelemetryHub::vendorDriverVersion() const { return m_vendorDriver; }
float TelemetryHub::powerLimitW() const { return m_powerLimit; }

} // namespace aether
