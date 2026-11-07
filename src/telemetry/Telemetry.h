#pragma once
#include <windows.h>
#include <atomic>
#include <cmath>
#include <limits>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <condition_variable>

namespace aether {

constexpr float NA = std::numeric_limits<float>::quiet_NaN();
inline bool has(float v) { return !std::isnan(v); }

// One telemetry sample. Every field is NaN when the sensor is not available ("N/A" in the UI).
struct TelemetrySample {
    double t = 0;               // seconds since telemetry start
    float gpuTemp = NA;         // °C
    float hotspotTemp = NA;     // °C
    float memTemp = NA;         // °C
    float gpuLoad = NA;         // %
    float memCtrlLoad = NA;     // % (memory controller activity)
    float vramUsedMB = NA;
    float vramTotalMB = NA;
    float coreClockMHz = NA;
    float memClockMHz = NA;
    float powerW = NA;          // board / ASIC power in watts
    float powerPct = NA;        // % of power limit (Windows perf counters)
    float fanRpm = NA;
    float fanPct = NA;
    float voltageMv = NA;
    float pcieGen = NA, pcieWidth = NA;
    float fps = NA;             // from the active FurUltimate workload
    float frameTimeMs = NA;
};

enum class TelemetrySource { None, NVML, ADL, D3DKMT, PDH, DXGI, Workload };
const char* sourceName(TelemetrySource s);

struct SensorSources {
    TelemetrySource gpuTemp = TelemetrySource::None, hotspot = TelemetrySource::None, memTemp = TelemetrySource::None,
                    load = TelemetrySource::None, vram = TelemetrySource::None, coreClock = TelemetrySource::None,
                    memClock = TelemetrySource::None, power = TelemetrySource::None, fan = TelemetrySource::None,
                    voltage = TelemetrySource::None, pcie = TelemetrySource::None;
};

class ITelemetryProvider {
public:
    virtual ~ITelemetryProvider() = default;
    virtual const char* name() const = 0;
    virtual TelemetrySource source() const = 0;
    // Fills only the fields this provider can measure; leaves others untouched.
    virtual void sample(TelemetrySample& s) = 0;
};

// Central telemetry service: background sampling thread + history ring buffer.
class TelemetryHub {
public:
    static TelemetryHub& instance();
    void start(const LUID& adapterLuid, uint32_t vendorId, uint32_t deviceId, uint32_t subSysId, uint64_t vramBytes);
    void stop();
    bool running() const { return m_running; }
    void setInterval(int ms) { m_intervalMs = ms; }

    TelemetrySample latest() const;
    // Copies samples newer than `sinceSeconds` (relative to latest) into `out`.
    void history(double windowSeconds, std::vector<TelemetrySample>& out) const;
    SensorSources sources() const;
    std::vector<std::string> providerNames() const;

    // Workload frame statistics (published by the stress / benchmark engines).
    void publishFrameStats(float fps, float frameTimeMs);
    void clearFrameStats();

    // Vendor extras discovered by providers (architecture, memory type …), empty if unknown.
    std::string vendorArchitecture() const;
    std::string vendorMemoryType() const;
    std::string vendorDriverVersion() const;
    float powerLimitW() const;

private:
    TelemetryHub() = default;
    void run();
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<int> m_intervalMs{500};
    std::vector<ITelemetryProvider*> m_providers;
    std::vector<TelemetrySample> m_ring;
    size_t m_ringHead = 0, m_ringCount = 0;
    TelemetrySample m_latest;
    SensorSources m_sources;
    std::atomic<float> m_fps{NA}, m_frameMs{NA};
    std::atomic<int64_t> m_frameStamp{0};
    LARGE_INTEGER m_t0{}, m_freq{};
    std::string m_arch, m_memType, m_vendorDriver;
    float m_powerLimit = NA;
    friend class NvmlProvider;
    friend class AdlProvider;
};

// Factory functions (implemented per provider).
ITelemetryProvider* createNvmlProvider(uint32_t deviceId, uint32_t subSysId, std::string& arch, std::string& driver, float& powerLimit);
ITelemetryProvider* createAdlProvider(uint32_t deviceId, std::string& memType);
ITelemetryProvider* createD3dkmtProvider(const LUID& luid);
ITelemetryProvider* createPdhProvider(const LUID& luid, uint64_t vramBytes);

} // namespace aether
