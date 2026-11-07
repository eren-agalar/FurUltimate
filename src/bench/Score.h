#pragma once
#include <cmath>
#include <algorithm>
#include <string>

namespace aether {

// FurUltimate benchmark score model. Version string is stored with every result; scores are only comparable
// when both the version and the configuration key match. See docs/BENCHMARK_METHODOLOGY.md.
namespace score {

inline constexpr const char* kBenchmarkVersion = "FNM-Bench 1.0";

// Reference values: a sub-score of 1000 means the measured value equals the reference value.
inline constexpr double kRefGraphicsFps = 60.0;     // Graphics Rendering, average FPS
inline constexpr double kRefShaderFps = 60.0;       // Shader Performance, average FPS
inline constexpr double kRefComputeGflops = 10000.0;// Compute Performance, FP32 GFLOPS
inline constexpr double kRefVramGBs = 400.0;        // VRAM Performance, GB/s (read + write)

inline constexpr double kWeightGraphics = 0.35, kWeightShader = 0.20, kWeightCompute = 0.20,
                        kWeightVram = 0.15, kWeightStability = 0.10;

inline double graphics(double avgFps) { return std::max(1.0, 1000.0 * avgFps / kRefGraphicsFps); }
inline double shader(double avgFps) { return std::max(1.0, 1000.0 * avgFps / kRefShaderFps); }
inline double compute(double gflops) { return std::max(1.0, 1000.0 * gflops / kRefComputeGflops); }
inline double vram(double gbs) { return std::max(1.0, 1000.0 * gbs / kRefVramGBs); }
// Frame stability: ratio of 1% low FPS to average FPS (1.0 = perfectly consistent frame pacing).
inline double stability(double p1LowFps, double avgFps) {
    double r = avgFps > 0 ? std::clamp(p1LowFps / avgFps, 0.0, 1.0) : 0.0;
    return std::max(1.0, 1000.0 * r);
}
// Overall score: weighted geometric mean of the five sub-scores.
inline double overall(double g, double s, double c, double v, double f) {
    double l = kWeightGraphics * std::log(g) + kWeightShader * std::log(s) + kWeightCompute * std::log(c) +
               kWeightVram * std::log(v) + kWeightStability * std::log(f);
    return std::exp(l);
}

// --- AI score (separate from the overall score; never mixed into it) -------------------------------
inline constexpr const char* kAiVersion = "FNM-AI 1.0";
inline constexpr double kRefAiTops = 50.0;          // 1000 points = 50 TOPS of measured matrix throughput
inline double ai(double tops) { return std::max(1.0, 1000.0 * tops / kRefAiTops); }

} // namespace score
} // namespace aether
