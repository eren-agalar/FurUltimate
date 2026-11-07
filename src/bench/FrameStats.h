#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace aether {

// Frame-time statistics. All definitions are documented in docs/BENCHMARK_METHODOLOGY.md.
struct FrameStatsSummary {
    uint64_t frames = 0;
    double durationSec = 0;
    double avgFps = 0;          // frames / measured wall time
    double minFps = 0;          // lowest 1-second window average
    double maxFps = 0;          // highest 1-second window average
    double p1LowFps = 0;        // 1000 / (99th percentile frame time)
    double avgFrameMs = 0, p50FrameMs = 0, p95FrameMs = 0, p99FrameMs = 0, maxFrameMs = 0, stdFrameMs = 0;
    int stutters = 0;           // frames longer than 2.5 × median frame time (and > 8 ms)
};

class FrameStats {
public:
    void reset() { m_ms.clear(); m_total = 0; m_window.clear(); m_windowFps.clear(); m_windowAcc = 0; m_windowFrames = 0; }
    void reserve(size_t n) { m_ms.reserve(n); }
    void add(double frameMs) {
        m_ms.push_back((float)frameMs);
        m_total += frameMs;
        m_windowAcc += frameMs;
        ++m_windowFrames;
        if (m_windowAcc >= 1000.0) {   // close a 1-second window
            m_windowFps.push_back(m_windowFrames * 1000.0 / m_windowAcc);
            m_windowAcc = 0; m_windowFrames = 0;
        }
    }
    double currentAvgFps() const { return m_total > 0 ? m_ms.size() * 1000.0 / m_total : 0; }
    double runningMinFps() const { return m_windowFps.empty() ? 0 : *std::min_element(m_windowFps.begin(), m_windowFps.end()); }
    double runningMaxFps() const { return m_windowFps.empty() ? 0 : *std::max_element(m_windowFps.begin(), m_windowFps.end()); }
    const std::vector<double>& windowFps() const { return m_windowFps; }
    size_t count() const { return m_ms.size(); }

    FrameStatsSummary summarize() const {
        FrameStatsSummary s;
        s.frames = m_ms.size();
        if (s.frames == 0) return s;
        s.durationSec = m_total / 1000.0;
        s.avgFps = s.frames * 1000.0 / m_total;
        std::vector<float> sorted(m_ms);
        std::sort(sorted.begin(), sorted.end());
        auto pct = [&](double p) {
            size_t idx = (size_t)std::min<double>(sorted.size() - 1, std::ceil(p * sorted.size()) - 1);
            return (double)sorted[idx];
        };
        s.avgFrameMs = m_total / s.frames;
        s.p50FrameMs = pct(0.50); s.p95FrameMs = pct(0.95); s.p99FrameMs = pct(0.99);
        s.maxFrameMs = sorted.back();
        double var = 0;
        for (float v : m_ms) var += (v - s.avgFrameMs) * (v - s.avgFrameMs);
        s.stdFrameMs = std::sqrt(var / s.frames);
        s.p1LowFps = s.p99FrameMs > 0 ? 1000.0 / s.p99FrameMs : 0;
        if (!m_windowFps.empty()) {
            s.minFps = *std::min_element(m_windowFps.begin(), m_windowFps.end());
            s.maxFps = *std::max_element(m_windowFps.begin(), m_windowFps.end());
        } else {
            s.minFps = s.maxFps = s.avgFps;   // run shorter than one second
        }
        double stutterLimit = std::max(8.0, s.p50FrameMs * 2.5);
        for (float v : m_ms) if (v > stutterLimit) ++s.stutters;
        return s;
    }
private:
    std::vector<float> m_ms;
    double m_total = 0;
    std::vector<double> m_window;
    std::vector<double> m_windowFps;
    double m_windowAcc = 0;
    int m_windowFrames = 0;
};

// Display-rate FPS meter: frames / elapsed time over windows of at least 0.25 s.
class FpsMeter {
public:
    float add(double dtSec) {
        m_acc += dtSec; ++m_n;
        if (m_acc >= 0.25 || m_value <= 0) { m_value = (float)(m_n / std::max(m_acc, 1e-6)); m_acc = 0; m_n = 0; }
        return m_value;
    }
    float value() const { return m_value; }
private:
    double m_acc = 0; int m_n = 0; float m_value = 0;
};

inline double median(std::vector<double> v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

} // namespace aether
