#include "Settings.h"
#include "Log.h"
#include "Strings.h"
#include "FileIO.h"
#include <nlohmann/json.hpp>
#include <algorithm>

namespace aether {

using nlohmann::json;

void Settings::clamp() {
    if (language != "tr" && language != "en") language = "tr";
    startPage = std::clamp(startPage, 0, 5);
    theme = std::clamp(theme, 0, 6);
    static const int allowedRefresh[] = {250, 500, 1000, 2000};
    int best = 500;
    for (int r : allowedRefresh) if (std::abs(r - refreshMs) < std::abs(best - refreshMs)) best = r;
    refreshMs = best;
    chartWindowSec = std::clamp(chartWindowSec, 30, 1800);
    warnTempC = std::clamp(warnTempC, 50, 105);
    stopTempC = std::clamp(stopTempC, 55, 110);
    if (stopTempC <= warnTempC) stopTempC = std::min(110, warnTempC + 5);
    defaultStressMinutes = std::clamp(defaultStressMinutes, 1, 240);
    keepLogDays = std::clamp(keepLogDays, 1, 365);
    if (nickname.size() > 60) nickname.resize(60);
}

SettingsStore& SettingsStore::instance() { static SettingsStore s; return s; }

std::string SettingsStore::toJson() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const Settings& s = m_settings;
    json j;
    j["schema"] = 1;
    j["general"] = {{"language", s.language}, {"startPage", s.startPage}, {"startMaximized", s.startMaximized},
                    {"monitorOnStartup", s.monitorOnStartup}};
    j["appearance"] = {{"theme", s.theme}, {"reduceMotion", s.reduceMotion}, {"chartGrid", s.chartGrid}};
    j["monitoring"] = {{"refreshMs", s.refreshMs}, {"tempUnit", (int)s.tempUnit}, {"chartWindowSec", s.chartWindowSec}};
    j["display"] = {{"showFpsOverlay", s.showFpsOverlay}, {"showFrameTimeInOverlay", s.showFrameTimeInOverlay}};
    j["benchmark"] = {{"fullscreen", s.benchFullscreen}};
    j["safety"] = {{"warnTempC", s.warnTempC}, {"stopTempC", s.stopTempC}, {"defaultStressMinutes", s.defaultStressMinutes},
                   {"confirmIntensive", s.confirmIntensive}, {"tdrGuard", s.tdrGuard}};
    j["data"] = {{"keepLogDays", s.keepLogDays}};
    j["leaderboard"] = {{"enabled", s.leaderboardEnabled}, {"asked", s.leaderboardAsked}, {"nickname", s.nickname},
                        {"playerId", s.playerId}};
    j["updates"] = {{"autoCheck", s.autoCheckUpdates}, {"skipped", s.skippedUpdate}, {"lastCheck", s.lastUpdateCheck},
                    {"lastRunVersion", s.lastRunVersion}};
    return j.dump(2);
}

template <class T> static void rd(const json& j, const char* sec, const char* key, T& out) {
    if (j.contains(sec) && j[sec].is_object() && j[sec].contains(key)) {
        try { out = j[sec][key].get<T>(); } catch (...) {}
    }
}

bool SettingsStore::fromJson(const std::string& text) {
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return false;
    Settings s;
    rd(j, "general", "language", s.language);
    rd(j, "general", "startPage", s.startPage);
    rd(j, "general", "startMaximized", s.startMaximized);
    rd(j, "general", "monitorOnStartup", s.monitorOnStartup);
    rd(j, "appearance", "theme", s.theme);
    rd(j, "appearance", "reduceMotion", s.reduceMotion);
    rd(j, "appearance", "chartGrid", s.chartGrid);
    rd(j, "monitoring", "refreshMs", s.refreshMs);
    int unit = 0; rd(j, "monitoring", "tempUnit", unit); s.tempUnit = unit == 1 ? TempUnit::Fahrenheit : TempUnit::Celsius;
    rd(j, "monitoring", "chartWindowSec", s.chartWindowSec);
    rd(j, "display", "showFpsOverlay", s.showFpsOverlay);
    rd(j, "display", "showFrameTimeInOverlay", s.showFrameTimeInOverlay);
    rd(j, "benchmark", "fullscreen", s.benchFullscreen);
    rd(j, "safety", "warnTempC", s.warnTempC);
    rd(j, "safety", "stopTempC", s.stopTempC);
    rd(j, "safety", "defaultStressMinutes", s.defaultStressMinutes);
    rd(j, "safety", "confirmIntensive", s.confirmIntensive);
    rd(j, "safety", "tdrGuard", s.tdrGuard);
    rd(j, "data", "keepLogDays", s.keepLogDays);
    rd(j, "leaderboard", "enabled", s.leaderboardEnabled);
    rd(j, "leaderboard", "asked", s.leaderboardAsked);
    rd(j, "leaderboard", "nickname", s.nickname);
    rd(j, "leaderboard", "playerId", s.playerId);
    rd(j, "updates", "autoCheck", s.autoCheckUpdates);
    rd(j, "updates", "skipped", s.skippedUpdate);
    rd(j, "updates", "lastCheck", s.lastUpdateCheck);
    rd(j, "updates", "lastRunVersion", s.lastRunVersion);
    s.clamp();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_settings = s;
    return true;
}

bool SettingsStore::load(const std::wstring& path) {
    m_path = path;
    std::string text;
    if (!readFile(path, text)) { LOGI("Settings file not found, using defaults"); return false; }
    if (!fromJson(text)) { LOGW("Settings file is invalid, using defaults"); return false; }
    return true;
}

bool SettingsStore::save() const {
    if (m_path.empty()) return false;
    bool ok = writeFileAtomic(m_path, toJson());
    if (!ok) LOGE("Failed to save settings");
    return ok;
}

Settings SettingsStore::get() const { std::lock_guard<std::mutex> lock(m_mutex); return m_settings; }
void SettingsStore::set(const Settings& s) { std::lock_guard<std::mutex> lock(m_mutex); m_settings = s; m_settings.clamp(); }

double displayTemp(double c, TempUnit unit) { return unit == TempUnit::Fahrenheit ? c * 9.0 / 5.0 + 32.0 : c; }
const char* tempUnitSuffix(TempUnit unit) { return unit == TempUnit::Fahrenheit ? "°F" : "°C"; }

} // namespace aether
