// FurUltimate — automated unit and integration tests.
// Pure-logic tests always run; GPU tests run when a Direct3D 11 device is available (real GPU, WARP or Wine).
#include "core/Paths.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "core/Strings.h"
#include "core/FileIO.h"
#include "core/I18n.h"
#include "bench/FrameStats.h"
#include "bench/Score.h"
#include "bench/BenchmarkEngine.h"
#include "stress/StressEngine.h"
#include "data/Database.h"
#include "data/Result.h"
#include "report/PdfWriter.h"
#include "report/ReportExporter.h"
#include "gpu/GpuDevice.h"
#include "gpu/ShaderLibrary.h"
#include "telemetry/Telemetry.h"
#include "ui/Fonts.h"
#include "resource.h"
#include "bench/AiBench.h"
#include "bench/DmlCompat.h"
#include "net/Leaderboard.h"
#include "net/Updater.h"
#include "ui/Theme.h"
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace aether;

static int g_failed = 0, g_passed = 0, g_skipped = 0;
#define CHECK(cond) do { if (!(cond)) { printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ok = false; } } while (0)
#define APPROX(a, b, eps) CHECK(std::fabs((double)(a) - (double)(b)) <= (eps))

struct TestCase { const char* name; std::function<int()> fn; };   // returns 1 pass, 0 fail, -1 skipped
static std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }
struct Reg { Reg(const char* n, std::function<int()> f) { registry().push_back({n, f}); } };
#define TEST(name) static int name(); static Reg reg_##name(#name, name); static int name()

static std::wstring g_tmp;
static bool haveGpu() {
    AdapterInfo a;
    return GpuDeviceManager::primaryAdapter(a);
}

// ---------------------------------------------------------------- statistics & scoring
TEST(frame_stats_basic) {
    bool ok = true;
    FrameStats fs;
    for (int i = 0; i < 300; ++i) fs.add(10.0);        // 3 s at 100 FPS
    for (int i = 0; i < 3; ++i) fs.add(40.0);          // three hitches
    auto s = fs.summarize();
    CHECK(s.frames == 303);
    APPROX(s.durationSec, 3.12, 1e-9);
    APPROX(s.avgFps, 303 / 3.12, 1e-6);
    APPROX(s.p50FrameMs, 10.0, 1e-6);
    APPROX(s.p99FrameMs, 10.0, 1e-6);   // 3 / 303 < 1 %
    APPROX(s.maxFrameMs, 40.0, 1e-6);
    CHECK(s.stutters == 3);
    APPROX(s.maxFps, 100.0, 1e-6);
    CHECK(s.minFps <= s.maxFps);
    return ok;
}

TEST(frame_stats_empty_and_short) {
    bool ok = true;
    FrameStats fs;
    auto s = fs.summarize();
    CHECK(s.frames == 0 && s.avgFps == 0);
    fs.add(20.0);
    s = fs.summarize();
    APPROX(s.avgFps, 50.0, 1e-9);
    APPROX(s.minFps, 50.0, 1e-9);   // shorter than one window -> min = max = avg
    CHECK(median({}) == 0);
    APPROX(median({3, 1, 2}), 2, 1e-12);
    APPROX(median({4, 1, 2, 3}), 2.5, 1e-12);
    return ok;
}

TEST(score_model_documented_values) {
    bool ok = true;
    APPROX(score::graphics(score::kRefGraphicsFps), 1000, 1e-9);
    APPROX(score::shader(30), 500, 1e-9);
    APPROX(score::compute(score::kRefComputeGflops * 2), 2000, 1e-9);
    APPROX(score::vram(score::kRefVramGBs), 1000, 1e-9);
    APPROX(score::stability(50, 100), 500, 1e-9);
    APPROX(score::stability(150, 100), 1000, 1e-9);     // clamped
    APPROX(score::overall(1000, 1000, 1000, 1000, 1000), 1000, 1e-6);
    APPROX(score::kWeightGraphics + score::kWeightShader + score::kWeightCompute + score::kWeightVram + score::kWeightStability, 1.0, 1e-12);
    CHECK(score::overall(2000, 1000, 1000, 1000, 1000) > score::overall(1000, 1000, 1000, 1000, 1000));
    APPROX(score::overall(2000, 2000, 2000, 2000, 2000), 2000, 1e-6);
    CHECK(score::graphics(0) >= 1);   // never log(0)
    return ok;
}

// ---------------------------------------------------------------- serialization
static BenchmarkResult sampleResult() {
    BenchmarkResult r;
    r.kind = "benchmark"; r.timestamp = "2026-11-05T12:00:00";
    r.gpuName = "Test GPU · Çğİşöü"; r.gpuVendor = "NVIDIA"; r.driverVersion = "1.2.3.4";
    r.benchVersion = score::kBenchmarkVersion; r.preset = "STANDARD-1080P"; r.configKey = "FNM-Bench 1.0|STANDARD-1080P|FULL";
    r.configJson = "{\"preset\":\"STANDARD-1080P\"}";
    r.width = 1920; r.height = 1080; r.durationSec = 98.5;
    r.avgFps = 61.2; r.minFps = 55.1; r.maxFps = 70.4; r.p1LowFps = 50.3; r.avgFrameMs = 16.3; r.p99FrameMs = 19.9; r.stdFrameMs = 1.2;
    r.avgTemp = 71.5; r.maxTemp = 78.0;   // avgLoad / power / clock left as NaN (sensor unavailable)
    r.score = 1234; r.status = "completed";
    SubResult s; s.id = "graphics"; s.name = "Grafik İşleme"; s.value = 61.2; s.unit = "FPS"; s.score = 1020;
    r.subs.push_back(s);
    r.fpsTimeline = {60, 61, NAN, 62};
    r.tempTimeline = {70, NAN, 72, 73};
    return r;
}

TEST(result_json_roundtrip_preserves_na) {
    bool ok = true;
    BenchmarkResult r = sampleResult(), back;
    std::string j = resultToJson(r);
    CHECK(j.find("null") != std::string::npos);        // NaN serialized as null, never as a fake number
    CHECK(resultFromJson(j, back));
    CHECK(back.gpuName == r.gpuName);
    APPROX(back.score, 1234, 1e-9);
    CHECK(std::isnan(back.avgLoad) && std::isnan(back.avgPower));
    APPROX(back.maxTemp, 78.0, 1e-9);
    CHECK(back.subs.size() == 1 && back.subs[0].name == "Grafik İşleme");
    CHECK(back.fpsTimeline.size() == 4 && std::isnan(back.fpsTimeline[2]));
    CHECK(!resultFromJson("not json", back));
    return ok;
}

TEST(measurement_units_tflops) {
    bool ok = true;
    SubResult legacy; legacy.id = "compute"; legacy.unit = "GFLOPS"; legacy.value = 24150;   // stored by 1.0–1.1.1
    SubResult now; now.id = "compute"; now.unit = "TFLOPS"; now.value = 24.15;
    CHECK(formatMeasurement(legacy) == "24.15 TFLOPS");
    CHECK(formatMeasurement(now) == "24.15 TFLOPS");
    APPROX(normalizedValue(legacy), normalizedValue(now), 1e-9);
    CHECK(std::string(normalizedUnit(legacy)) == "TFLOPS");
    SubResult ai; ai.unit = "TOPS"; ai.value = 85.34;
    CHECK(formatMeasurement(ai) == "85.3 TOPS");
    SubResult fps; fps.unit = "FPS"; fps.value = 61.25;
    CHECK(formatMeasurement(fps) == "61.2 FPS" || formatMeasurement(fps) == "61.3 FPS");
    SubResult bw; bw.unit = "GB/s"; bw.value = 512.4;
    CHECK(formatMeasurement(bw) == "512 GB/s");
    APPROX(score::compute(24150), 2415, 1e-9);   // score formula unchanged (reference 10 TFLOPS = 10000 GFLOPS)
    return ok;
}

TEST(csv_export_escaping_and_na) {
    bool ok = true;
    CHECK(ReportExporter::csvEscape("a,b") == "\"a,b\"");
    CHECK(ReportExporter::csvEscape("say \"hi\"") == "\"say \"\"hi\"\"\"");
    CHECK(ReportExporter::csvEscape("plain") == "plain");
    std::string c = ReportExporter::csv(sampleResult());
    CHECK(c.rfind("\xEF\xBB\xBF", 0) == 0);
    CHECK(c.find("avg_gpu_utilization_pct,N/A") != std::string::npos);
    CHECK(c.find("score,1234") != std::string::npos);
    CHECK(c.find("graphics,61.200,FPS,1020") != std::string::npos);
    return ok;
}

// ---------------------------------------------------------------- settings
TEST(settings_persistence_and_clamping) {
    bool ok = true;
    std::wstring path = g_tmp + L"\\settings_test.json";
    DeleteFileW(path.c_str());
    SettingsStore& st = SettingsStore::instance();
    CHECK(!st.load(path));                      // missing file -> defaults
    Settings s = st.get();
    CHECK(s.language == "tr" && s.warnTempC == 85 && s.stopTempC == 95);
    s.language = "en"; s.warnTempC = 80; s.stopTempC = 90; s.refreshMs = 1000; s.tempUnit = TempUnit::Fahrenheit; s.theme = 1;
    st.set(s);
    CHECK(st.save());
    st.set(Settings{});
    CHECK(st.load(path));
    Settings l = st.get();
    CHECK(l.language == "en" && l.warnTempC == 80 && l.stopTempC == 90 && l.refreshMs == 1000 && l.theme == 1);
    CHECK(l.tempUnit == TempUnit::Fahrenheit);
    CHECK(st.fromJson("{\"safety\":{\"warnTempC\":100,\"stopTempC\":60},\"monitoring\":{\"refreshMs\":3}}"));
    Settings c = st.get();
    CHECK(c.stopTempC > c.warnTempC);          // emergency stop is always above the warning threshold
    CHECK(c.refreshMs == 250);
    CHECK(!st.fromJson("{broken"));
    APPROX(displayTemp(100, TempUnit::Fahrenheit), 212, 1e-9);
    st.set(Settings{});
    return ok;
}

TEST(i18n_fallback) {
    bool ok = true;
    I18n::setLanguage("en");
    CHECK(std::string(T("Genel Bakış")) == "Overview");
    CHECK(std::string(T("bu metnin çevirisi yok")) == "bu metnin çevirisi yok");   // falls back to source
    I18n::setLanguage("tr");
    CHECK(std::string(T("Genel Bakış")) == "Genel Bakış");
    return ok;
}

// ---------------------------------------------------------------- database
TEST(database_crud) {
    bool ok = true;
    std::wstring path = g_tmp + L"\\db_test.db";
    DeleteFileW(path.c_str());
    ResultDatabase db;
    std::string err;
    CHECK(db.open(path, err));
    CHECK(db.count() == 0);
    BenchmarkResult a = sampleResult(), b = sampleResult();
    b.kind = "stress"; b.score = 0;
    int64_t ia = db.insert(a), ib = db.insert(b);
    CHECK(ia > 0 && ib > ia);
    CHECK(db.count() == 2);
    BenchmarkResult g;
    CHECK(db.get(ia, g) && g.id == ia && g.gpuName == a.gpuName);
    CHECK(db.list("benchmark").size() == 1);
    CHECK(db.list("stress").size() == 1);
    CHECK(db.list().front().id == ib);          // newest first
    g.screenshotPath = "C:\\x.png";
    CHECK(db.update(g));
    BenchmarkResult g2;
    CHECK(db.get(ia, g2) && g2.screenshotPath == "C:\\x.png");
    CHECK(db.remove(ib) && db.count() == 1);
    db.close();
    ResultDatabase db2;                          // persistence across sessions
    CHECK(db2.open(path, err) && db2.count() == 1);
    CHECK(db2.clear() && db2.count() == 0);
    return ok;
}

// ---------------------------------------------------------------- PDF / reports
TEST(pdf_writer_structure_and_turkish_glyphs) {
    bool ok = true;
    std::vector<uint8_t> ttf;
    CHECK(Fonts::fontBytes(IDR_FONT_REGULAR, ttf));
    TrueTypeFont f;
    CHECK(f.load(ttf));
    for (uint32_t cp : {0x15Fu, 0x11Fu, 0x130u, 0x131u, 0xE7u, 0xF6u, 0xFCu}) CHECK(f.glyph(cp) != 0);   // ş ğ İ ı ç ö ü
    CHECK(utf8ToCodepoints("Ağ").size() == 2);
    PdfWriter p;
    int font = p.addFont(ttf);
    CHECK(font == 0);
    p.newPage();
    p.text(font, 12, 40, 40, "Sıcaklık ölçümü — Çalışıyor");
    p.newPage();
    p.rect(10, 10, 100, 100);
    auto bytes = p.build();
    std::string s(bytes.begin(), bytes.end());
    CHECK(s.rfind("%PDF-1.7", 0) == 0);
    CHECK(s.find("%%EOF") != std::string::npos);
    CHECK(s.find("/Count 2") != std::string::npos);
    CHECK(s.find("/FontFile2") != std::string::npos && s.find("/ToUnicode") != std::string::npos);
    // Every xref entry must point at the matching "N 0 obj" header.
    size_t sx = s.rfind("startxref\n");
    size_t xref = std::stoul(s.substr(sx + 10));
    CHECK(s.compare(xref, 4, "xref") == 0);
    size_t line = s.find('\n', s.find('\n', xref) + 1) + 1;   // skip "xref" and "0 N"
    line = s.find('\n', line) + 1;                              // skip free entry
    int checked = 0;
    for (int id = 1;; ++id) {
        if (s.compare(line, 7, "trailer") == 0) break;
        size_t off = std::stoul(s.substr(line, 10));
        std::string head = std::to_string(id) + " 0 obj";
        if (s.compare(off, head.size(), head) != 0) { ok = false; printf("    bad xref for object %d\n", id); break; }
        line = s.find('\n', line) + 1;
        ++checked;
    }
    CHECK(checked > 8);
    return ok;
}

TEST(report_export_files) {
    bool ok = true;
    BenchmarkResult r = sampleResult();
    BenchmarkResult other = sampleResult();
    other.configKey = "FNM-Bench 1.0|ULTRA-2160P|FULL";   // incompatible comparison must be refused in the report
    ReportContext ctx{"Windows 11 24H2", "Test CPU", "32.0 GB DDR5", "1.0.0"};
    std::string err;
    std::wstring pdf = g_tmp + L"\\r.pdf", json = g_tmp + L"\\r.json", csv = g_tmp + L"\\r.csv";
    CHECK(ReportExporter::exportPdf(r, &other, ctx, pdf, err));
    CHECK(ReportExporter::exportJson(r, json, err));
    CHECK(ReportExporter::exportCsv(r, csv, err));
    std::string a, b, c;
    CHECK(readFile(pdf, a) && a.size() > 50000 && a.rfind("%PDF", 0) == 0);
    CHECK(readFile(json, b) && b.find("\"benchmarkVersion\"") != std::string::npos);
    CHECK(readFile(csv, c) && c.find("config_key") != std::string::npos);
    BenchmarkResult s = sampleResult();
    s.kind = "stress"; s.status = "aborted"; s.statusDetail = "Acil durdurma: GPU sıcaklığı 96 °C";
    CHECK(ReportExporter::exportPdf(s, nullptr, ctx, g_tmp + L"\\s.pdf", err));
    return ok;
}

// ---------------------------------------------------------------- telemetry (missing sensors)
TEST(telemetry_missing_sensors_are_na) {
    bool ok = true;
    TelemetrySample s;
    CHECK(!has(s.gpuTemp) && !has(s.powerW) && !has(s.fanRpm) && !has(s.voltageMv) && !has(s.fps));
    CHECK(std::string(sourceName(TelemetrySource::None)) == "N/A");
    AdapterInfo a;
    if (!GpuDeviceManager::primaryAdapter(a)) return -1;
    TelemetryHub& hub = TelemetryHub::instance();
    hub.setInterval(100);
    hub.start(a.luid, a.vendorId, a.deviceId, a.subSysId, a.dedicatedVideoMemory);
    Sleep(450);
    TelemetrySample t = hub.latest();
    CHECK(t.t > 0);
    // Any value present must be physically plausible; absent values must stay NaN (never zero-filled).
    if (has(t.gpuTemp)) CHECK(t.gpuTemp > 0 && t.gpuTemp < 150);
    if (has(t.gpuLoad)) CHECK(t.gpuLoad >= 0 && t.gpuLoad <= 100);
    CHECK(!has(t.fps));                         // no workload running -> no FPS
    hub.publishFrameStats(60, 16.6f);
    Sleep(250);
    CHECK(has(hub.latest().fps));
    hub.clearFrameStats();
    std::vector<TelemetrySample> hist;
    hub.history(60, hist);
    CHECK(hist.size() >= 2);
    hub.stop();
    return ok;
}

// ---------------------------------------------------------------- GPU integration
TEST(gpu_detection) {
    bool ok = true;
    auto list = GpuDeviceManager::enumerateAdapters();
    if (list.empty()) return -1;
    for (auto& a : list) {
        CHECK(!a.name.empty());
        CHECK(a.vendorName().size() > 0);
    }
    AdapterInfo p;
    CHECK(GpuDeviceManager::primaryAdapter(p));
    ComPtr<ID3D11Device> dev; ComPtr<ID3D11DeviceContext> ctx; D3D_FEATURE_LEVEL fl{};
    CHECK(SUCCEEDED(GpuDeviceManager::createDevice(p.luid, dev, ctx, fl)));
    CHECK(fl >= D3D_FEATURE_LEVEL_11_0);
    return ok;
}

TEST(shader_compilation_all_entry_points) {
    bool ok = true;
    if (!haveGpu() || !ShaderLibrary::instance().available()) return -1;
    struct E { ShaderSource s; const char* e; const char* p; };
    const E entries[] = {
        {ShaderSource::Scene, "VSFullscreen", "vs_5_0"}, {ShaderSource::Scene, "PSScene", "ps_5_0"},
        {ShaderSource::Shards, "VSShard", "vs_5_0"}, {ShaderSource::Shards, "PSShard", "ps_5_0"},
        {ShaderSource::Particles, "CSParticles", "cs_5_0"}, {ShaderSource::Particles, "VSParticle", "vs_5_0"},
        {ShaderSource::Particles, "PSParticle", "ps_5_0"}, {ShaderSource::Noise3D, "CSNoise", "cs_5_0"},
        {ShaderSource::Memory, "CSMemory", "cs_5_0"}, {ShaderSource::Post, "PSBright", "ps_5_0"},
        {ShaderSource::Post, "PSBlurH", "ps_5_0"}, {ShaderSource::Post, "PSBlurV", "ps_5_0"},
        {ShaderSource::Post, "PSTonemap", "ps_5_0"}, {ShaderSource::Fractal, "PSFractal", "ps_5_0"},
        {ShaderSource::ComputeBench, "CSFma", "cs_5_0"}, {ShaderSource::VramBench, "CSCopy", "cs_5_0"},
    };
    for (auto& e : entries) {
        std::string err;
        auto blob = ShaderLibrary::instance().compile(e.s, e.e, e.p, err);
        if (blob.empty()) { printf("    %s: %s\n", e.e, err.c_str()); ok = false; }
    }
    return ok;
}

static StressConfig smallStress(const AdapterInfo& a) {
    StressConfig c;
    c.preset = StressPreset::Eco;
    c.width = 320; c.height = 180; c.renderScale = 0.5f;
    c.durationSec = 120;
    c.adapter = a.luid; c.gpuName = narrow(a.name); c.vramBytes = a.dedicatedVideoMemory;
    c.stopTempC = 200; c.warnTempC = 190;   // thresholds out of the way (sensor-independent test)
    return c;
}

TEST(stress_cancellation) {
    bool ok = true;
    AdapterInfo a;
    if (!GpuDeviceManager::primaryAdapter(a)) return -1;
    StressEngine e;
    std::atomic<int> finished{0};
    BenchmarkResult got;
    e.onFinished = [&](const BenchmarkResult& r) { got = r; ++finished; };
    CHECK(e.start(smallStress(a)));
    CHECK(!e.start(smallStress(a)));            // a second concurrent run is rejected
    DWORD t0 = GetTickCount();
    while (e.status().state != EngineState::Running && GetTickCount() - t0 < 60000) Sleep(50);
    CHECK(e.status().state == EngineState::Running);
    Sleep(2500);
    e.pause();
    Sleep(400);
    CHECK(e.status().state == EngineState::Paused);
    e.resume();
    Sleep(400);
    DWORD stopAt = GetTickCount();
    e.stop();                                   // immediate manual termination
    e.join();
    DWORD stopMs = GetTickCount() - stopAt;
    printf("    stop latency %lu ms\n", stopMs);
    CHECK(stopMs < 5000);
    CHECK(e.status().state == EngineState::Finished);
    CHECK(finished == 1 && got.status == "aborted" && got.kind == "stress");
    CHECK(got.avgFps > 0);
    return ok;
}

TEST(device_lost_recovery) {
    bool ok = true;
    AdapterInfo a;
    if (!GpuDeviceManager::primaryAdapter(a)) return -1;
    // The fault-injection hook reads the variable once per process, so run the faulted engine in a child process.
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = std::wstring(L"\"") + exe + L"\" --child-device-lost";
    SetEnvironmentVariableW(L"AETHER_SIMULATE_DEVICE_LOST", L"15");
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    BOOL started = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
    SetEnvironmentVariableW(L"AETHER_SIMULATE_DEVICE_LOST", nullptr);
    CHECK(started);
    if (!started) return 0;
    WaitForSingleObject(pi.hProcess, 180000);
    DWORD code = 99;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    CHECK(code == 0);
    return ok;
}

// Child: engine must stop safely on device loss, report it, and a fresh run must recreate the device.
static int childDeviceLost() {
    AdapterInfo a;
    if (!GpuDeviceManager::primaryAdapter(a)) return 3;
    StressEngine e;
    BenchmarkResult got;
    int n = 0;
    e.onFinished = [&](const BenchmarkResult& r) { got = r; ++n; };
    StressConfig c = smallStress(a);
    c.durationSec = 60;
    e.start(c);
    DWORD t0 = GetTickCount();
    while (e.busy() && GetTickCount() - t0 < 120000) Sleep(50);
    e.join();
    StressStatus st = e.status();
    printf("    child: state=%d message=%s\n", (int)st.state, st.message.c_str());
    if (st.state != EngineState::Failed) return 4;
    if (st.message.find("DXGI_ERROR_DEVICE_REMOVED") == std::string::npos) return 5;
    if (n == 1 && got.status != "failed") return 6;
    // Recreation: a new device + swap chain is created for the next run (the injected counter keeps failing
    // presents in this process, so success = clean start and clean failure again without hang/crash).
    if (!e.start(c)) return 7;
    t0 = GetTickCount();
    while (e.busy() && GetTickCount() - t0 < 120000) Sleep(50);
    e.join();
    return e.status().state == EngineState::Failed ? 0 : 8;
}

TEST(benchmark_execution_validation_mode) {
    bool ok = true;
    AdapterInfo a;
    if (!GpuDeviceManager::primaryAdapter(a)) return -1;
    BenchmarkEngine b;
    BenchmarkResult got;
    std::atomic<int> n{0};
    b.onFinished = [&](const BenchmarkResult& r) { got = r; ++n; };
    BenchConfig c;
    c.validation = true;
    c.adapter = a.luid; c.gpuName = narrow(a.name); c.vramBytes = a.dedicatedVideoMemory; c.stopTempC = 200;
    CHECK(b.start(c));
    DWORD t0 = GetTickCount();
    while (b.busy() && GetTickCount() - t0 < 300000) Sleep(100);
    b.join();
    CHECK(n == 1);
    printf("    status=%s score=%.0f detail=%s\n", got.status.c_str(), got.score, got.statusDetail.c_str());
    CHECK(got.status == "completed");
    CHECK(got.subs.size() == 6);            // five scored tests + AI
    for (auto& s : got.subs) CHECK(s.value > 0 && s.score >= 1);
    CHECK(got.score > 0);
    CHECK(got.fullRun);
    CHECK(got.aiScore >= 1 && got.aiTops > 0 && !got.aiPath.empty());
    CHECK(got.aiConfigKey.find(score::kAiVersion) == 0);
    CHECK(got.configKey.find("VALIDATION") != std::string::npos);
    CHECK(BenchmarkEngine::configKey(c) == got.configKey);
    BenchConfig full; 
    CHECK(BenchmarkEngine::configKey(full) != got.configKey);   // validation scores are never comparable
    return ok;
}

// ---------------------------------------------------------------- 1.1 features
TEST(ai_score_model_and_theoretical_table) {
    bool ok = true;
    APPROX(score::ai(score::kRefAiTops), 1000, 1e-9);
    APPROX(score::ai(100), 2000, 1e-9);
    CHECK(score::ai(0) >= 1);
    APPROX(theoreticalAiTops(0x10DE, "NVIDIA GeForce RTX 3080"), 119.1, 1e-9);
    APPROX(theoreticalAiTops(0x10DE, "NVIDIA GeForce RTX 3080 Ti"), 136.4, 1e-9);   // longer names win
    APPROX(theoreticalAiTops(0x10DE, "NVIDIA GeForce RTX 4070 SUPER"), 141.9, 1e-9);
    APPROX(theoreticalAiTops(0x1002, "AMD Radeon RX 7900 XTX"), 122.8, 1e-9);
    CHECK(theoreticalAiTops(0x10DE, "NVIDIA GeForce GTX 1080") == 0);              // unknown -> N/A, never guessed
    CHECK(theoreticalAiTops(0x8086, "Intel(R) Arc(TM) A770") == 0);
    return ok;
}

TEST(partial_run_config_keys) {
    bool ok = true;
    BenchConfig full;
    CHECK(BenchmarkEngine::isFullRun(full.testMask));
    CHECK(BenchmarkEngine::configKey(full) == std::string(score::kBenchmarkVersion) + "|STANDARD-1080P|FULL");
    BenchConfig noAi; noAi.testMask = kScoredMask;   // AI does not change the overall-score key
    CHECK(BenchmarkEngine::configKey(noAi) == BenchmarkEngine::configKey(full));
    BenchConfig one; one.testMask = 1u << 1;
    CHECK(!BenchmarkEngine::isFullRun(one.testMask));
    CHECK(BenchmarkEngine::configKey(one).find("|T:shader") != std::string::npos);
    BenchConfig ai; ai.testMask = 1u << kAiTestIndex;
    CHECK(BenchmarkEngine::configKey(ai).find("|T:ai") != std::string::npos);
    CHECK(BenchmarkEngine::plannedDuration(one) < BenchmarkEngine::plannedDuration(full));
    CHECK(BenchmarkEngine::aiConfigKey("DirectML FP16") == std::string(score::kAiVersion) + "|DirectML FP16");
    CHECK(std::string(benchTests()[kAiTestIndex].id) == "ai");
    return ok;
}

TEST(result_json_ai_fields_roundtrip) {
    bool ok = true;
    BenchmarkResult r = sampleResult(), back;
    r.aiScore = 1706; r.aiTops = 85.3; r.aiTheoreticalTops = 119.1; r.aiPath = "DirectML FP16";
    r.aiShaderTops = 18.2; r.aiDirectMLTops = 85.3; r.aiConfigKey = "FNM-AI 1.0|DirectML FP16";
    r.fullRun = false; r.testsRun = "ai";
    CHECK(resultFromJson(resultToJson(r), back));
    APPROX(back.aiScore, 1706, 1e-9); APPROX(back.aiTops, 85.3, 1e-9); APPROX(back.aiTheoreticalTops, 119.1, 1e-9);
    CHECK(back.aiPath == "DirectML FP16" && back.aiConfigKey == r.aiConfigKey && !back.fullRun && back.testsRun == "ai");
    BenchmarkResult legacy;   // results written by 1.0 have no AI block: defaults must stay valid
    CHECK(resultFromJson("{\"kind\":\"benchmark\",\"score\":900}", legacy) && legacy.fullRun && legacy.aiScore == 0);
    std::string csv = ReportExporter::csv(r);
    CHECK(csv.find("ai_score,1706") != std::string::npos && csv.find("ai_tops,85.30") != std::string::npos);
    CHECK(csv.find("score,N/A") != std::string::npos);   // partial run: no overall score
    return ok;
}

TEST(themes_and_leaderboard_settings) {
    bool ok = true;
    CHECK(Theme::kCount == 7);
    int light = 0;
    for (int i = 0; i < Theme::kCount; ++i) { if (Theme::get(i).light) ++light; CHECK(std::string(Theme::get(i).name).size() > 0); }
    CHECK(light == 1);
    Theme::select(6); CHECK(Theme::current().light && Theme::current().fg.r == 0); Theme::select(0);
    SettingsStore& st = SettingsStore::instance();
    std::wstring path = g_tmp + L"\\settings_lb.json";
    DeleteFileW(path.c_str());
    st.load(path);
    Settings s = st.get();
    CHECK(!s.leaderboardEnabled && s.playerId.empty());
    s.leaderboardEnabled = true; s.leaderboardAsked = true; s.nickname = "Çağan İşçi"; s.playerId = newPlayerId(); s.theme = 4;
    st.set(s); CHECK(st.save());
    st.set(Settings{}); CHECK(st.load(path));
    Settings l = st.get();
    CHECK(l.leaderboardEnabled && l.leaderboardAsked && l.nickname == "Çağan İşçi" && l.playerId == s.playerId && l.theme == 4);
    CHECK(l.playerId.size() == 36 && l.playerId[14] == '4');   // UUID v4
    CHECK(newPlayerId() != newPlayerId());
    st.set(Settings{});
    CHECK(overallBoard("HIGH-1440P") == "overall:HIGH-1440P" && aiBoard("Shader FP32") == "ai:Shader FP32");
    return ok;
}

TEST(single_test_run_has_no_overall_score) {
    bool ok = true;
    AdapterInfo a;
    if (!GpuDeviceManager::primaryAdapter(a)) return -1;
    BenchmarkEngine b;
    BenchmarkResult got;
    std::atomic<int> n{0};
    b.onFinished = [&](const BenchmarkResult& r) { got = r; ++n; };
    BenchConfig c;
    c.validation = true; c.testMask = 1u << kAiTestIndex;
    c.adapter = a.luid; c.gpuName = narrow(a.name); c.vramBytes = a.dedicatedVideoMemory; c.stopTempC = 200;
    CHECK(b.start(c));
    DWORD t0 = GetTickCount();
    while (b.busy() && GetTickCount() - t0 < 180000) Sleep(100);
    b.join();
    printf("    status=%s ai=%.0f (%.3f TOPS, %s)\n", got.status.c_str(), got.aiScore, got.aiTops, got.aiPath.c_str());
    CHECK(n == 1 && got.status == "completed");
    CHECK(!got.fullRun && got.score == 0);
    CHECK(got.subs.size() == 1 && got.subs[0].id == "ai");
    CHECK(got.aiScore >= 1 && got.testsRun == "ai");
    return ok;
}

// Runs only when a test server is provided: FNM_LB_TEST_URL / FNM_LB_TEST_KEY (see server/leaderboard/README.md).
TEST(updater_versions_release_parsing_and_hashes) {
    bool ok = true;
    int v[3];
    CHECK(parseVersion("v1.2.3", v) && v[0] == 1 && v[1] == 2 && v[2] == 3);
    CHECK(parseVersion("1.4", v) && v[0] == 1 && v[1] == 4 && v[2] == 0);
    CHECK(!parseVersion("latest", v));
    CHECK(isNewerVersion("v1.3.0", "1.2.0"));
    CHECK(isNewerVersion("1.10.0", "1.9.9"));
    CHECK(!isNewerVersion("1.2.0", "1.2.0"));
    CHECK(!isNewerVersion("1.1.9", "1.2.0"));
    CHECK(!isNewerVersion("garbage", "1.2.0"));

    const char* json = R"({"tag_name":"v1.4.0","name":"FurUltimate 1.4.0","html_url":"https://github.com/eren-agalar/FurUltimate/releases/tag/v1.4.0",
        "draft":false,"prerelease":false,"body":"## FurUltimate 1.4.0\n- **Faster** startup\n| a | b |\n\n## Türkçe\n- **Daha hızlı** açılış\n",
        "assets":[{"name":"FurUltimate-1.4.0-Setup.exe","size":100,"browser_download_url":"https://github.com/eren-agalar/FurUltimate/releases/download/v1.4.0/FurUltimate-1.4.0-Setup.exe",
                   "digest":"sha256:BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"},
                  {"name":"FurUltimate-1.4.0-win64.exe","size":200,"browser_download_url":"https://github.com/eren-agalar/FurUltimate/releases/download/v1.4.0/FurUltimate-1.4.0-win64.exe"},
                  {"name":"SHA256SUMS.txt","size":10,"browser_download_url":"https://github.com/eren-agalar/FurUltimate/releases/download/v1.4.0/SHA256SUMS.txt"}]})";
    ReleaseInfo rel;
    CHECK(parseRelease(json, rel));
    CHECK(rel.version == "1.4.0" && rel.tag == "v1.4.0" && rel.assets.size() == 3);
    const ReleaseAsset* setup = rel.find("-setup.exe");   // case-insensitive
    CHECK(setup && setup->sha256 == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const ReleaseAsset* exe = rel.find("-win64.exe");
    CHECK(exe && exe->sha256.empty() && exe->size == 200);
    CHECK(!parseRelease(R"({"tag_name":"v2.0.0","draft":true})", rel));
    CHECK(!parseRelease(R"({"tag_name":"v2.0.0","prerelease":true})", rel));
    CHECK(!parseRelease("not json", rel));

    std::string en = releaseNotesForDisplay("## FurUltimate 1.4.0\n- **Faster** startup\n| a | b |\n\n## Türkçe\n- **Daha hızlı** açılış\n", "en", 500);
    std::string tr = releaseNotesForDisplay("## FurUltimate 1.4.0\n- **Faster** startup\n| a | b |\n\n## Türkçe\n- **Daha hızlı** açılış\n", "tr", 500);
    CHECK(en.find("Faster startup") != std::string::npos && en.find("Daha") == std::string::npos && en.find('|') == std::string::npos);
    CHECK(tr.find("Daha hızlı açılış") != std::string::npos && tr.find("Faster") == std::string::npos);
    CHECK(releaseNotesForDisplay(std::string(2000, 'x'), "en", 100).size() <= 104);

    auto sums = parseSha256Sums("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  FurUltimate-1.4.0-win64.exe\r\n"
                                "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD *other.zip\nshort line\n");
    CHECK(sums.size() == 2 && sums["FurUltimate-1.4.0-win64.exe"] == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sums["other.zip"].size() == 64);

    std::wstring f = Paths::root() + L"\\sha_test.bin";
    CHECK(writeFileAtomic(f, "abc"));
    CHECK(sha256File(f) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");   // FIPS 180-2 test vector
    DeleteFileW(f.c_str());
    CHECK(sha256File(L"Z:\\does\\not\\exist.bin").empty());

    CHECK(isAllowedDownloadUrl("https://github.com/eren-agalar/FurUltimate/releases/download/v1.4.0/x.exe"));
    CHECK(isAllowedDownloadUrl("https://GITHUB.com/eren-agalar/furultimate/releases/download/v1.4.0/x.exe"));
    CHECK(!isAllowedDownloadUrl("https://github.com/someone-else/FurUltimate/releases/download/v1.4.0/x.exe"));
    CHECK(!isAllowedDownloadUrl("http://github.com/eren-agalar/FurUltimate/releases/download/v1.4.0/x.exe"));
    CHECK(!isAllowedDownloadUrl("https://evil.example/eren-agalar/FurUltimate/releases/download/x.exe"));
    return ok;
}

TEST(leaderboard_client_integration) {
    bool ok = true;
    char url[512] = {}, key[1024] = {};
    if (!GetEnvironmentVariableA("FNM_LB_TEST_URL", url, sizeof(url)) || !GetEnvironmentVariableA("FNM_LB_TEST_KEY", key, sizeof(key)))
        return -1;
    std::string cfg = std::string("{\"url\":\"") + url + "\",\"anonKey\":\"" + key + "\"}";
    writeFileAtomic(Paths::root() + L"\\leaderboard.json", cfg);
    LeaderboardClient& lb = LeaderboardClient::instance();
    lb.loadConfig();
    CHECK(lb.configured());
    std::string player = newPlayerId();
    const std::string nick = "Şükrü " + player.substr(0, 6);   // unique per run (the test server keeps old rows)
    auto submitAndWait = [&](double sc) {
        SubmitRequest q;
        q.playerId = player; q.nickname = nick; q.board = overallBoard("STANDARD-1080P");
        q.gpu = "Test GPU 9000"; q.vendor = "NVIDIA"; q.driver = "1.0"; q.benchVersion = score::kBenchmarkVersion; q.appVersion = "test";
        q.score = sc;
        q.detailsJson = format("{\"graphics\":%.0f,\"shader\":%.0f,\"compute\":%.0f,\"vram\":%.0f,\"stability\":1000}", sc, sc, sc, sc);
        lb.submit(q);
        for (int i = 0; i < 100; ++i) { auto v = lb.takeSubmitResults(); if (!v.empty()) return v.front(); Sleep(100); }
        return SubmitResult{};
    };
    // geometric mean with stability 1000 and the other four equal to x: exp(0.9 ln x + 0.1 ln 1000)
    double x = 800, overall = std::round(std::exp(0.9 * std::log(x) + 0.1 * std::log(1000.0)));
    SubmitResult r1 = submitAndWait(0);   // inconsistent on purpose (score 0) -> rejected
    CHECK(!r1.ok);
    auto sub = [&](double gx) {
        SubmitRequest q;
        q.playerId = player; q.nickname = nick; q.board = overallBoard("STANDARD-1080P");
        q.gpu = "Test GPU 9000"; q.vendor = "NVIDIA"; q.driver = "1.0"; q.benchVersion = score::kBenchmarkVersion; q.appVersion = "test";
        q.score = std::round(std::exp(0.9 * std::log(gx) + 0.1 * std::log(1000.0)));
        q.detailsJson = format("{\"graphics\":%.0f,\"shader\":%.0f,\"compute\":%.0f,\"vram\":%.0f,\"stability\":1000}", gx, gx, gx, gx);
        lb.submit(q);
        for (int i = 0; i < 100; ++i) { auto v = lb.takeSubmitResults(); if (!v.empty()) return v.front(); Sleep(100); }
        return SubmitResult{};
    };
    SubmitResult r2 = sub(x);
    printf("    submit: ok=%d status=%s rank=%d err=%s\n", (int)r2.ok, r2.status.c_str(), r2.rank, r2.error.c_str());
    CHECK(r2.ok && r2.status == "inserted" && r2.rank >= 1);
    lb.refresh(overallBoard("STANDARD-1080P"), player);
    for (int i = 0; i < 100 && (lb.snapshot().loading || !lb.snapshot().loaded); ++i) Sleep(100);
    auto snap = lb.snapshot();
    bool found = false, mine = false;
    for (auto& e : snap.players) if (e.nickname == nick && e.gpu == "Test GPU 9000") { found = true; APPROX(e.score, overall, 1.0); mine = e.mine; }
    CHECK(found && mine);
    bool gpuRow = false;
    for (auto& g : snap.gpus) if (g.gpu == "Test GPU 9000") gpuRow = g.players >= 1;
    CHECK(gpuRow);
    CHECK(!snap.mine.empty());
    lb.shutdown();
    DeleteFileW((Paths::root() + L"\\leaderboard.json").c_str());
    return ok;
}

// ---------------------------------------------------------------- 1.1.1: DirectML crash fix
// Fake COM object whose vtable slot 8 follows the MSVC convention for struct-returning methods:
// RCX = this, RDX = hidden return pointer, return value = that pointer.
static uintptr_t g_seenThis = 0;
static DML_BINDING_PROPERTIES* STDMETHODCALLTYPE fakeGetBindingProperties(void* self, DML_BINDING_PROPERTIES* ret) {
    g_seenThis = (uintptr_t)self;
    ret->RequiredDescriptorCount = 7; ret->TemporaryResourceSize = 1234567; ret->PersistentResourceSize = 89;
    return ret;
}
static void STDMETHODCALLTYPE fakeOther() {}

TEST(directml_struct_return_abi) {
    bool ok = true;
    void* vtbl[16];
    for (auto& v : vtbl) v = (void*)&fakeOther;
    vtbl[kDmlGetBindingPropertiesSlot] = (void*)&fakeGetBindingProperties;
    struct Fake { void** vt; } fake{vtbl};
    DML_BINDING_PROPERTIES p = dmlBindingProperties(reinterpret_cast<IDMLDispatchable*>(&fake));
    CHECK(g_seenThis == (uintptr_t)&fake);
    CHECK(p.RequiredDescriptorCount == 7 && p.TemporaryResourceSize == 1234567 && p.PersistentResourceSize == 89);
    // The slot index must match the interface layout: IUnknown(3) + IDMLObject(4) + IDMLDeviceChild(1).
    CHECK(kDmlGetBindingPropertiesSlot == 3 + 4 + 1);
    return ok;
}

TEST(directml_worker_isolation) {
    bool ok = true;
    AdapterInfo a;
    if (!GpuDeviceManager::primaryAdapter(a)) return -1;
    int ticks = 0;
    AiMeasurement m = runDirectMLIsolated(a.luid, 1.0, [&](double, double) { ++ticks; return true; });
    printf("    worker: ok=%d tops=%.2f precision=%s error=%s ticks=%d\n", (int)m.ok, m.tops, m.precision.c_str(), m.error.c_str(), ticks);
    // Either a real measurement (Windows with DirectML) or a clean, reported failure (no DirectML) — never a crash.
    CHECK(m.ok ? m.tops > 0 : !m.error.empty());
    CHECK(m.error.find("beklenmedik") == std::string::npos);   // the worker itself must exit normally
    // Live progress channel: the parent must receive the worker's running median before it finishes.
    SetEnvironmentVariableA("FNM_SIMULATE_DML_RESULT", "1");
    double maxLive = 0;
    AiMeasurement live = runDirectMLIsolated(a.luid, 1.0, [&](double, double tops) { maxLive = std::max(maxLive, tops); return true; });
    SetEnvironmentVariableA("FNM_SIMULATE_DML_RESULT", nullptr);
    printf("    live: max live %.1f TOPS, final ok=%d %.1f TOPS %s\n", maxLive, (int)live.ok, live.tops, live.precision.c_str());
    APPROX(maxLive, 42.0, 1e-6);
    CHECK(live.ok && std::fabs(live.tops - 42.0) < 1e-6 && live.precision == "FP16");
    // Fault injection: the worker crashes; the parent must survive and report it.
    SetEnvironmentVariableA("FNM_SIMULATE_DML_CRASH", "1");
    double t0 = (double)GetTickCount64();
    AiMeasurement c = runDirectMLIsolated(a.luid, 1.0, nullptr);
    double crashMs = (double)GetTickCount64() - t0;
    printf("    crash detected after %.0f ms\n", crashMs);
    CHECK(crashMs < 15000);   // reported promptly, not via the 45 s watchdog
    SetEnvironmentVariableA("FNM_SIMULATE_DML_CRASH", nullptr);
    printf("    crash: ok=%d error=%s\n", (int)c.ok, c.error.c_str());
    CHECK(!c.ok && c.error.find("beklenmedik") != std::string::npos);
    return ok;
}

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    g_tmp = std::wstring(tmp) + L"aether_tests";
    CreateDirectoryW(g_tmp.c_str(), nullptr);
    Paths::init(g_tmp);
    Log::init(Paths::logs());
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Fonts::init();
    if (argc > 1 && std::wstring(argv[1]) == L"--child-device-lost") return childDeviceLost();
    if (argc >= 7 && isAiDmlWorkerCommand(argc - 1, argv + 1)) return aiDmlWorkerMain(argc - 1, argv + 1);
    std::wstring filter = argc > 1 ? argv[1] : L"";
    printf("FurUltimate tests\n");
    for (auto& t : registry()) {
        if (!filter.empty() && widen(t.name).find(filter) == std::wstring::npos) continue;
        printf("[ RUN  ] %s\n", t.name);
        fflush(stdout);
        int r = t.fn();
        if (r < 0) { ++g_skipped; printf("[ SKIP ] %s (no Direct3D 11 device)\n", t.name); }
        else if (r) { ++g_passed; printf("[  OK  ] %s\n", t.name); }
        else { ++g_failed; printf("[ FAIL ] %s\n", t.name); }
        fflush(stdout);
    }
    printf("\n%d passed, %d failed, %d skipped\n", g_passed, g_failed, g_skipped);
    Log::shutdown();
    return g_failed ? 1 : 0;
}
