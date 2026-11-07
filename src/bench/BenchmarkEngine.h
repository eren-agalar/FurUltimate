#pragma once
#include "../stress/StressEngine.h"
#include "../data/Result.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace aether {

enum class BenchPreset { Standard1080 = 0, High1440 = 1, Ultra2160 = 2 };
const char* benchPresetId(BenchPreset p);
void benchPresetResolution(BenchPreset p, uint32_t& w, uint32_t& h);

struct BenchConfig {
    BenchPreset preset = BenchPreset::Standard1080;
    bool quick = false;                 // shortened durations (separate comparison key)
    bool validation = false;            // QA pipeline validation: 480x270, minimal durations, never comparable
    uint32_t vendorId = 0;
    OverlayPalette palette;
    uint32_t testMask = 0x3F;           // bit i = run test i (see benchTests()); 0x3F = all six
    bool fullscreen = false;
    bool overlay = true;
    int stopTempC = 95;
    LUID adapter{};
    std::string gpuName, gpuVendor, driverVersion;
    uint64_t vramBytes = 0;
    HMONITOR monitor = nullptr;
};

struct BenchTestInfo { const char* id; const char* name; const char* description; };
constexpr int kBenchTestCount = 6;      // 5 scored tests + AI performance (separate AI score)
constexpr int kAiTestIndex = 5;
constexpr uint32_t kScoredMask = 0x1F;
const BenchTestInfo* benchTests();

struct BenchStatus {
    EngineState state = EngineState::Idle;
    int testIndex = -1;
    bool warmup = false;
    double testProgress = 0, overallProgress = 0;
    double elapsedSec = 0, totalSec = 0;
    float fps = 0, avgFps = 0, minFps = 0, maxFps = 0, frameMs = 0;
    double liveValue = 0;              // GFLOPS / GB/s for compute tests
    std::string liveUnit;
    double subScores[kBenchTestCount] = {};
    double subValues[kBenchTestCount] = {};
    uint32_t testMask = 0x3F;
    double aiTops = 0, aiTheoreticalTops = 0;
    std::string aiPath;
    std::string message;
};

class BenchmarkEngine {
public:
    ~BenchmarkEngine();
    bool start(const BenchConfig& cfg);
    void stop();
    void join();
    bool busy() const;
    BenchStatus status() const;
    static std::string configKey(const BenchConfig& cfg);
    static std::string aiConfigKey(const std::string& path);
    static bool isFullRun(uint32_t mask) { return (mask & kScoredMask) == kScoredMask; }
    static double plannedDuration(const BenchConfig& cfg);
    std::function<void(const BenchmarkResult&)> onFinished;
private:
    void run(BenchConfig cfg);
    std::thread m_thread;
    mutable std::mutex m_mutex;
    BenchStatus m_status;
    std::atomic<bool> m_stop{false};
};

} // namespace aether
