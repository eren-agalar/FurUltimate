#pragma once
#include "../render/SceneRenderer.h"
#include "../render/RenderSurface.h"
#include "../gpu/GpuDevice.h"
#include "../data/Result.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace aether {

enum class StressPreset { Eco = 0, Balanced = 1, Extreme = 2, Custom = 3 };
const char* stressPresetName(StressPreset p);
SceneQuality stressPresetQuality(StressPreset p, uint64_t vramBytes);

struct StressConfig {
    StressPreset preset = StressPreset::Balanced;
    SceneQuality quality;                 // used for Custom (and filled for others)
    uint32_t width = 1920, height = 1080; // render resolution
    float renderScale = 1.0f;             // render quality multiplier
    bool fullscreen = false;
    int durationSec = 600;                // 0 = continuous (until stopped)
    bool vsync = false;                   // Eco preset caps to display refresh
    int warnTempC = 85, stopTempC = 95;
    bool tdrGuard = true;
    bool overlay = true, overlayFrameTime = true;
    OverlayPalette palette;
    int tempUnit = 0;                     // 0 °C, 1 °F
    LUID adapter{};
    std::string gpuName, gpuVendor, driverVersion;
    uint64_t vramBytes = 0;
    HMONITOR monitor = nullptr;
};

enum class EngineState { Idle, Starting, Running, Paused, Stopping, Finished, Failed };

struct StressStatus {
    EngineState state = EngineState::Idle;
    double elapsedSec = 0, durationSec = 0;
    float fps = 0, frameMs = 0, gpuMs = -1, avgFps = 0, minFps = 0, maxFps = 0;
    uint64_t frames = 0;
    bool tempWarning = false, noTempSensor = false;
    int tdrReductions = 0;
    uint64_t allocatedBytes = 0;
    uint32_t renderW = 0, renderH = 0;
    std::string message;                  // user-facing status / stop reason (Turkish source string)
};

class StressEngine {
public:
    ~StressEngine();
    bool start(const StressConfig& cfg);
    void pause();
    void resume();
    void stop();                       // immediate manual termination (asynchronous)
    void join();
    StressStatus status() const;
    bool busy() const;
    // Invoked on the worker thread when a run ends (result is filled for completed/aborted runs).
    std::function<void(const BenchmarkResult&)> onFinished;
private:
    void run(StressConfig cfg);
    void setState(EngineState s, const std::string& msg = {});
    std::thread m_thread;
    mutable std::mutex m_mutex;
    StressStatus m_status;
    std::atomic<bool> m_stop{false}, m_pause{false};
};

} // namespace aether
