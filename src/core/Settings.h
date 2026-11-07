#pragma once
#include <string>
#include <mutex>

namespace aether {

enum class TempUnit { Celsius = 0, Fahrenheit = 1 };

struct Settings {
    // General
    std::string language = "tr";          // "tr" | "en"
    int startPage = 0;                    // index of the page shown at startup
    bool startMaximized = false;
    bool monitorOnStartup = true;         // start telemetry sampling immediately
    // Appearance
    int theme = 0;                        // 0 Grafit, 1 Gece, 2 Okyanus, 3 Zümrüt, 4 Kehribar, 5 Bordo, 6 Açık
    bool reduceMotion = false;
    bool chartGrid = true;
    // Monitoring
    int refreshMs = 500;                  // telemetry sampling interval
    TempUnit tempUnit = TempUnit::Celsius;
    int chartWindowSec = 120;
    // FPS display
    bool showFpsOverlay = true;           // overlay in test window
    bool showFrameTimeInOverlay = true;
    // Benchmark
    bool benchFullscreen = false;
    // Safety
    int warnTempC = 85;
    int stopTempC = 95;
    int defaultStressMinutes = 10;
    bool confirmIntensive = true;
    bool tdrGuard = true;                 // auto-reduce load if a frame exceeds the GPU timeout budget
    // Data
    int keepLogDays = 14;
    // Online leaderboard (opt-in)
    bool leaderboardEnabled = false;
    bool leaderboardAsked = false;        // consent dialog already shown once
    std::string nickname;
    std::string playerId;                 // anonymous random UUID of this installation

    void clamp();
};

class SettingsStore {
public:
    static SettingsStore& instance();
    bool load(const std::wstring& path);
    bool save() const;
    Settings get() const;
    void set(const Settings& s);
    std::string toJson() const;
    bool fromJson(const std::string& json);
private:
    mutable std::mutex m_mutex;
    Settings m_settings;
    std::wstring m_path;
};

double displayTemp(double celsius, TempUnit unit);
const char* tempUnitSuffix(TempUnit unit);

} // namespace aether
