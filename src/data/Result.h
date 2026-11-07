#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cmath>
#include <limits>

namespace aether {

struct SubResult {
    std::string id;        // graphics | shader | compute | vram | stability
    std::string name;      // display name (Turkish source string)
    double value = 0;      // primary measurement
    std::string unit;      // FPS | GFLOPS | GB/s | %
    double score = 0;      // sub-score (0 when not applicable)
    double avgFps = 0, minFps = 0, maxFps = 0, p1LowFps = 0, avgFrameMs = 0, p99FrameMs = 0;
    double durationSec = 0;
};

struct BenchmarkResult {
    int64_t id = 0;
    std::string kind = "benchmark";            // benchmark | stress
    std::string timestamp;                     // ISO-8601 local time
    std::string gpuName, gpuVendor, driverVersion;
    std::string benchVersion;
    std::string preset;                        // preset identifier
    std::string configKey;                     // comparison key: version|preset|resolution|mode
    std::string configJson;                    // full test configuration
    int width = 0, height = 0;
    double durationSec = 0;
    double avgFps = 0, minFps = 0, maxFps = 0, p1LowFps = 0;
    double avgFrameMs = 0, p99FrameMs = 0, stdFrameMs = 0;
    double avgTemp = std::numeric_limits<double>::quiet_NaN(), maxTemp = std::numeric_limits<double>::quiet_NaN();
    double avgLoad = std::numeric_limits<double>::quiet_NaN();
    double avgPower = std::numeric_limits<double>::quiet_NaN(), avgClock = std::numeric_limits<double>::quiet_NaN();
    double score = 0;                          // overall score (benchmark only)
    std::string status = "completed";          // completed | aborted | failed
    std::string statusDetail;
    std::vector<SubResult> subs;
    std::vector<float> fpsTimeline;            // one value per second
    std::vector<float> tempTimeline;           // one value per second (NaN = N/A)
    std::string screenshotPath;
    // Which benchmark tests ran ("graphics,shader,compute,vram,stability,ai"); empty = all (legacy results).
    std::string testsRun;
    bool fullRun = true;                       // all five scored tests ran -> overall score is meaningful
    // AI test (separate score)
    double aiScore = 0;                        // 0 = AI test not run / failed
    double aiTops = 0;                         // measured TOPS (median)
    double aiTheoreticalTops = 0;              // manufacturer peak dense FP16 (0 = unknown)
    double aiShaderTops = 0, aiDirectMLTops = 0;
    std::string aiPath;                        // "DirectML FP16" | "DirectML FP32" | "Shader FP32"
    std::string aiConfigKey;                   // comparison key for the AI score
};

// Display text of a test measurement ("61.2 FPS", "24.15 TFLOPS", "512 GB/s", "93.4 %", "85.3 TOPS").
// Compute results stored by 1.0–1.1.1 are in GFLOPS; they are shown (and compared) in TFLOPS as well.
std::string formatMeasurement(const SubResult& s);
// Measurement normalised to the current unit (GFLOPS -> TFLOPS) so old and new results compare correctly.
double normalizedValue(const SubResult& s);
const char* normalizedUnit(const SubResult& s);

std::string resultToJson(const BenchmarkResult& r, int indent = 2);
bool resultFromJson(const std::string& json, BenchmarkResult& r);

} // namespace aether
