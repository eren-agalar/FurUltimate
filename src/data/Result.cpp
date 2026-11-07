#include "Result.h"
#include <nlohmann/json.hpp>
#include <cstdio>

namespace aether {

using nlohmann::json;

static json num(double v) { return std::isnan(v) ? json(nullptr) : json(v); }
static double getNum(const json& j, const char* k) {
    if (!j.contains(k) || j[k].is_null()) return std::numeric_limits<double>::quiet_NaN();
    try { return j[k].get<double>(); } catch (...) { return std::numeric_limits<double>::quiet_NaN(); }
}
template <class T> static void get(const json& j, const char* k, T& out) {
    if (j.contains(k) && !j[k].is_null()) { try { out = j[k].get<T>(); } catch (...) {} }
}

double normalizedValue(const SubResult& s) { return s.unit == "GFLOPS" ? s.value / 1000.0 : s.value; }
const char* normalizedUnit(const SubResult& s) { return s.unit == "GFLOPS" ? "TFLOPS" : s.unit.c_str(); }

std::string formatMeasurement(const SubResult& s) {
    char buf[64];
    double v = normalizedValue(s);
    std::string u = normalizedUnit(s);
    if (u == "TFLOPS") snprintf(buf, sizeof(buf), "%.2f TFLOPS", v);
    else if (u == "%") snprintf(buf, sizeof(buf), "%.1f %%", v);
    else if (u == "FPS" || u == "TOPS") snprintf(buf, sizeof(buf), "%.1f %s", v, u.c_str());
    else snprintf(buf, sizeof(buf), "%.0f %s", v, u.c_str());
    return buf;
}

std::string resultToJson(const BenchmarkResult& r, int indent) {
    json j;
    j["application"] = "FurUltimate";
    j["id"] = r.id; j["kind"] = r.kind; j["timestamp"] = r.timestamp;
    j["gpu"] = {{"name", r.gpuName}, {"vendor", r.gpuVendor}, {"driverVersion", r.driverVersion}};
    j["benchmarkVersion"] = r.benchVersion; j["preset"] = r.preset; j["configKey"] = r.configKey;
    json cfg = json::parse(r.configJson.empty() ? "{}" : r.configJson, nullptr, false);
    j["configuration"] = cfg.is_discarded() ? json::object() : cfg;
    j["resolution"] = {{"width", r.width}, {"height", r.height}};
    j["durationSec"] = r.durationSec;
    j["fps"] = {{"average", r.avgFps}, {"minimum", r.minFps}, {"maximum", r.maxFps}, {"onePercentLow", r.p1LowFps}};
    j["frameTimeMs"] = {{"average", r.avgFrameMs}, {"p99", r.p99FrameMs}, {"stdDev", r.stdFrameMs}};
    j["temperatureC"] = {{"average", num(r.avgTemp)}, {"maximum", num(r.maxTemp)}};
    j["gpuUtilizationPct"] = {{"average", num(r.avgLoad)}};
    j["powerW"] = {{"average", num(r.avgPower)}};
    j["coreClockMHz"] = {{"average", num(r.avgClock)}};
    j["score"] = r.score;
    j["status"] = r.status; j["statusDetail"] = r.statusDetail;
    json subs = json::array();
    for (auto& s : r.subs)
        subs.push_back({{"id", s.id}, {"name", s.name}, {"value", s.value}, {"unit", s.unit}, {"score", s.score},
                        {"avgFps", s.avgFps}, {"minFps", s.minFps}, {"maxFps", s.maxFps}, {"onePercentLowFps", s.p1LowFps},
                        {"avgFrameMs", s.avgFrameMs}, {"p99FrameMs", s.p99FrameMs}, {"durationSec", s.durationSec}});
    j["tests"] = subs;
    json fps = json::array(), temp = json::array();
    for (float v : r.fpsTimeline) fps.push_back(num(v));
    for (float v : r.tempTimeline) temp.push_back(num(v));
    j["timeline"] = {{"fps", fps}, {"temperatureC", temp}};
    j["screenshot"] = r.screenshotPath;
    j["testsRun"] = r.testsRun;
    j["fullRun"] = r.fullRun;
    j["ai"] = {{"score", r.aiScore}, {"tops", r.aiTops}, {"theoreticalTops", r.aiTheoreticalTops}, {"shaderTops", r.aiShaderTops},
               {"directmlTops", r.aiDirectMLTops}, {"path", r.aiPath}, {"configKey", r.aiConfigKey}};
    return j.dump(indent);
}

bool resultFromJson(const std::string& text, BenchmarkResult& r) {
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return false;
    get(j, "id", r.id); get(j, "kind", r.kind); get(j, "timestamp", r.timestamp);
    if (j.contains("gpu")) { get(j["gpu"], "name", r.gpuName); get(j["gpu"], "vendor", r.gpuVendor); get(j["gpu"], "driverVersion", r.driverVersion); }
    get(j, "benchmarkVersion", r.benchVersion); get(j, "preset", r.preset); get(j, "configKey", r.configKey);
    if (j.contains("configuration")) r.configJson = j["configuration"].dump();
    if (j.contains("resolution")) { get(j["resolution"], "width", r.width); get(j["resolution"], "height", r.height); }
    get(j, "durationSec", r.durationSec);
    if (j.contains("fps")) { auto& f = j["fps"]; get(f, "average", r.avgFps); get(f, "minimum", r.minFps); get(f, "maximum", r.maxFps); get(f, "onePercentLow", r.p1LowFps); }
    if (j.contains("frameTimeMs")) { auto& f = j["frameTimeMs"]; get(f, "average", r.avgFrameMs); get(f, "p99", r.p99FrameMs); get(f, "stdDev", r.stdFrameMs); }
    if (j.contains("temperatureC")) { r.avgTemp = getNum(j["temperatureC"], "average"); r.maxTemp = getNum(j["temperatureC"], "maximum"); }
    if (j.contains("gpuUtilizationPct")) r.avgLoad = getNum(j["gpuUtilizationPct"], "average");
    if (j.contains("powerW")) r.avgPower = getNum(j["powerW"], "average");
    if (j.contains("coreClockMHz")) r.avgClock = getNum(j["coreClockMHz"], "average");
    get(j, "score", r.score); get(j, "status", r.status); get(j, "statusDetail", r.statusDetail);
    r.subs.clear();
    if (j.contains("tests") && j["tests"].is_array()) {
        for (auto& t : j["tests"]) {
            SubResult s;
            get(t, "id", s.id); get(t, "name", s.name); get(t, "value", s.value); get(t, "unit", s.unit); get(t, "score", s.score);
            get(t, "avgFps", s.avgFps); get(t, "minFps", s.minFps); get(t, "maxFps", s.maxFps); get(t, "onePercentLowFps", s.p1LowFps);
            get(t, "avgFrameMs", s.avgFrameMs); get(t, "p99FrameMs", s.p99FrameMs); get(t, "durationSec", s.durationSec);
            r.subs.push_back(s);
        }
    }
    r.fpsTimeline.clear(); r.tempTimeline.clear();
    if (j.contains("timeline")) {
        auto& tl = j["timeline"];
        if (tl.contains("fps")) for (auto& v : tl["fps"]) r.fpsTimeline.push_back(v.is_null() ? NAN : v.get<float>());
        if (tl.contains("temperatureC")) for (auto& v : tl["temperatureC"]) r.tempTimeline.push_back(v.is_null() ? NAN : v.get<float>());
    }
    get(j, "screenshot", r.screenshotPath);
    get(j, "testsRun", r.testsRun);
    get(j, "fullRun", r.fullRun);
    if (j.contains("ai") && j["ai"].is_object()) {
        auto& a = j["ai"];
        get(a, "score", r.aiScore); get(a, "tops", r.aiTops); get(a, "theoreticalTops", r.aiTheoreticalTops);
        get(a, "shaderTops", r.aiShaderTops); get(a, "directmlTops", r.aiDirectMLTops); get(a, "path", r.aiPath);
        get(a, "configKey", r.aiConfigKey);
    }
    return true;
}

} // namespace aether
