#include "StressEngine.h"
#include "../render/RenderSurface.h"
#include "../telemetry/Telemetry.h"
#include "../bench/FrameStats.h"
#include "../core/Log.h"
#include "../core/Strings.h"
#include "../core/Paths.h"
#include "../core/I18n.h"
#include "../core/Settings.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>

namespace aether {

const char* stressPresetName(StressPreset p) {
    switch (p) {
        case StressPreset::Eco: return "ECO";
        case StressPreset::Balanced: return "BALANCED";
        case StressPreset::Extreme: return "EXTREME";
        default: return "CUSTOM";
    }
}

// Upper bound for workload memory: never more than 25 % of dedicated VRAM and never above 1 GiB.
static uint64_t memoryBudget(uint64_t vram, uint64_t want) {
    uint64_t cap = vram > 0 ? vram / 4 : (256ull << 20);
    return std::min<uint64_t>({want, cap, 1024ull << 20});
}

SceneQuality stressPresetQuality(StressPreset p, uint64_t vram) {
    SceneQuality q;
    switch (p) {
        case StressPreset::Eco:
            q.marchSteps = 64; q.reflectionBounces = 1; q.shadowSteps = 16; q.volumetricSamples = 0; q.aoSamples = 0;
            q.detailOctaves = 1; q.shardInstances = 2000; q.particleCount = 32768; q.computeIterations = 1;
            q.memoryBytes = memoryBudget(vram, 64ull << 20); q.memoryPasses = 1;
            break;
        case StressPreset::Balanced:
            q.marchSteps = 128; q.reflectionBounces = 2; q.shadowSteps = 32; q.volumetricSamples = 16; q.aoSamples = 4;
            q.detailOctaves = 3; q.shardInstances = 20000; q.particleCount = 262144; q.computeIterations = 4;
            q.memoryBytes = memoryBudget(vram, 256ull << 20); q.memoryPasses = 2;
            break;
        case StressPreset::Extreme:
            q.marchSteps = 220; q.reflectionBounces = 3; q.shadowSteps = 64; q.volumetricSamples = 48; q.aoSamples = 6;
            q.detailOctaves = 5; q.shardInstances = 120000; q.particleCount = 1048576; q.computeIterations = 8;
            q.memoryBytes = memoryBudget(vram, 1024ull << 20); q.memoryPasses = 4;
            break;
        case StressPreset::Custom:
            break;
    }
    return q;
}

// Full status panel inside the test window: mirrors the "Durum" card of the main window.
static void drawStatusPanel(RenderSurface& s, const StressConfig& cfg, const StressStatus& st, const TelemetrySample& t, float fps,
                            float frameMs, float gpuMs, double elapsed, uint32_t rw, uint32_t rh) {
    const OverlayPalette& p = cfg.palette;
    const TempUnit unit = (TempUnit)cfg.tempUnit;
    auto temp = [&](float c) {
        return has(c) ? widen(format("%.0f %s", displayTemp(c, unit), tempUnitSuffix(unit))) : std::wstring(L"N/A");
    };
    const float x = 20, y = 20, w = 384, h = 292;
    s.panel(x, y, w, h, 16, p.panel, p.panelAlpha);
    // Header
    s.text(x + 18, y + 14, widen(std::string("FurUltimate · ") + T("GPU Stres Testi")), 11, p.text2, 1, true);
    bool paused = st.state == EngineState::Paused;
    s.textRight(x + w - 18, y + 14, widen(paused ? T("Duraklatıldı") : T("Çalışıyor")), 11, paused ? p.warn : p.success, 1, true);
    // Ring: progress of the timed run (continuous mode shows a slow sweep)
    float cx = x + 84, cy = y + 108, r = 54;
    const float a0 = 2.356f, sweep = 4.712f;
    float frac = cfg.durationSec > 0 ? (float)std::min(1.0, elapsed / cfg.durationSec) : (float)std::fmod(elapsed / 60.0, 1.0);
    s.arc(cx, cy, r, a0, a0 + sweep, p.text3, 0.35f, 6);
    if (frac > 0.002f) s.arc(cx, cy, r, a0, a0 + sweep * frac, p.accent, 1.0f, 6);
    s.textCenter(cx, cy - 26, wformat(L"%.0f", fps), 30, p.text, 1, true);
    s.textCenter(cx, cy + 10, L"FPS", 11, p.text2, 1, true);
    std::string tm = formatDuration(elapsed) + (cfg.durationSec > 0 ? " / " + formatDuration(cfg.durationSec) : std::string(" · ∞"));
    s.textCenter(cx, cy + r - 2, widen(tm), 11, p.text3);
    // Metrics grid (2 x 3)
    struct M { std::string k; std::wstring v; uint32_t c; };
    std::wstring power = has(t.powerW) ? wformat(L"%.0f W", t.powerW) : has(t.powerPct) ? wformat(L"%.0f %% TDP", t.powerPct) : L"N/A";
    M ms[6] = {
        {T("Sıcaklık"), temp(t.gpuTemp), st.tempWarning ? p.warn : (has(t.gpuTemp) ? p.text : p.text3)},
        {T("GPU yükü"), has(t.gpuLoad) ? wformat(L"%.0f %%", t.gpuLoad) : L"N/A", has(t.gpuLoad) ? p.text : p.text3},
        {cfg.overlayFrameTime ? T("Kare süresi") : T("Çözünürlük"),
         cfg.overlayFrameTime ? wformat(L"%.2f ms", frameMs) : wformat(L"%u×%u", rw, rh), p.text},
        {cfg.overlayFrameTime ? T("GPU süresi") : T("Ön ayar"),
         cfg.overlayFrameTime ? (gpuMs >= 0 ? wformat(L"%.2f ms", gpuMs) : std::wstring(L"—")) : widen(stressPresetName(cfg.preset)), p.text},
        {T("Güç"), power, power == L"N/A" ? p.text3 : p.text},
        {T("Ort. / Min FPS"), st.frames > 0 ? wformat(L"%.0f / %.0f", st.avgFps, st.minFps) : std::wstring(L"—"), p.text},
    };
    float gx = x + 168, gw = (w - 168 - 18) / 2;
    for (int i = 0; i < 6; ++i) {
        float mx = gx + (i % 2) * gw, my = y + 40 + (i / 2) * 46;
        s.text(mx, my, widen(ms[i].k), 10, p.text3, 1, true, gw - 4);
        s.text(mx, my + 15, ms[i].v, 16, ms[i].c, 1, true, gw - 4);
    }
    // Thresholds
    float ty = y + 186;
    s.hline(x + 18, x + w - 18, ty - 8, p.text3, 0.25f);
    s.text(x + 18, ty, widen(T("Sıcaklık eşikleri")), 11, p.text2);
    s.textRight(x + w - 18, ty, widen(std::string(T("Uyarı")) + " ") + temp((float)cfg.warnTempC) + L" · " +
                    widen(std::string(T("Acil durdurma")) + " ") + temp((float)cfg.stopTempC), 11, p.text3);
    float bx = x + 18, bw = w - 36, by = ty + 22;
    float lo = 30, hi = (float)cfg.stopTempC + 5;
    float wx = bx + bw * (cfg.warnTempC - lo) / (hi - lo), sx = bx + bw * (cfg.stopTempC - lo) / (hi - lo);
    s.panel(bx, by, bw, 5, 2.5f, p.text3, 0.35f);
    s.panel(wx, by, sx - wx, 5, 2.5f, p.warn, 0.45f);
    s.panel(sx, by, bx + bw - sx, 5, 2.5f, p.crit, 0.6f);
    if (has(t.gpuTemp)) {
        float px = bx + bw * std::clamp((t.gpuTemp - lo) / (hi - lo), 0.0f, 1.0f);
        s.circle(px, by + 2.5f, 6.5f, p.panel, 1.0f);
        s.circle(px, by + 2.5f, 5, st.tempWarning ? p.warn : p.accent, 1.0f);
    }
    // Status line: warnings take priority over the configuration summary
    float ly = by + 18;
    std::string line;
    uint32_t lc = p.text2;
    if (st.noTempSensor) { line = T("Sıcaklık sensörü okunamıyor — testi gözetim altında çalıştırın"); lc = p.warn; }
    else if (st.tempWarning) { line = T("GPU sıcaklığı uyarı eşiğinin üzerinde."); lc = p.warn; }
    else if (st.tdrReductions > 0) { line = T("Yük otomatik azaltıldı (GPU zaman aşımı koruması)"); lc = p.accent; }
    else line = format("%s · %u×%u · %.2g×", stressPresetName(cfg.preset), rw, rh, cfg.renderScale);
    s.text(x + 18, ly, widen(line), 11.5f, lc, 1, false, w - 36);
    s.text(x + 18, ly + 22, widen(T("Esc durdur · Boşluk duraklat · F12 ekran görüntüsü")), 10.5f, p.text3, 1, false, w - 36);
}

StressEngine::~StressEngine() { stop(); join(); }

bool StressEngine::busy() const {
    std::lock_guard<std::mutex> l(m_mutex);
    return m_status.state == EngineState::Starting || m_status.state == EngineState::Running ||
           m_status.state == EngineState::Paused || m_status.state == EngineState::Stopping;
}

bool StressEngine::start(const StressConfig& cfg) {
    if (busy()) return false;
    join();
    m_stop = false; m_pause = false;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        m_status = StressStatus{};
        m_status.state = EngineState::Starting;
        m_status.durationSec = cfg.durationSec;
        m_status.message = "Shader'lar hazırlanıyor…";
    }
    m_thread = std::thread([this, cfg] { run(cfg); });
    return true;
}

void StressEngine::pause() { m_pause = true; }
void StressEngine::resume() { m_pause = false; }
void StressEngine::stop() { m_stop = true; }
void StressEngine::join() { if (m_thread.joinable()) m_thread.join(); }

StressStatus StressEngine::status() const { std::lock_guard<std::mutex> l(m_mutex); return m_status; }

void StressEngine::setState(EngineState s, const std::string& msg) {
    std::lock_guard<std::mutex> l(m_mutex);
    m_status.state = s;
    if (!msg.empty()) m_status.message = msg;
}

void StressEngine::run(StressConfig cfg) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    LOGI("Stress test starting: preset=%s %ux%u scale=%.2f duration=%d", stressPresetName(cfg.preset), cfg.width, cfg.height,
         cfg.renderScale, cfg.durationSec);
    SceneQuality q = cfg.preset == StressPreset::Custom ? cfg.quality : stressPresetQuality(cfg.preset, cfg.vramBytes);
    q.memoryBytes = memoryBudget(cfg.vramBytes, q.memoryBytes);
    q.renderScale = cfg.renderScale;

    RenderSurface surf;
    SceneRenderer scene;
    std::string err;
    SurfaceOptions so;
    so.fullscreen = cfg.fullscreen;
    so.monitor = cfg.monitor;
    so.width = 1280; so.height = 720;
    so.title = widen(std::string("FurUltimate — ") + T("GPU Stres Testi") + " (" + stressPresetName(cfg.preset) + ")");

    BenchmarkResult result;
    result.kind = "stress";
    result.timestamp = nowIso8601();
    result.gpuName = cfg.gpuName; result.gpuVendor = cfg.gpuVendor; result.driverVersion = cfg.driverVersion;
    result.benchVersion = "FNM-Stress 1.0";
    result.preset = stressPresetName(cfg.preset);
    result.width = cfg.width; result.height = cfg.height;
    nlohmann::json cj = {{"preset", result.preset}, {"renderScale", cfg.renderScale}, {"durationSec", cfg.durationSec},
                         {"fullscreen", cfg.fullscreen}, {"vsync", cfg.vsync}, {"marchSteps", q.marchSteps},
                         {"reflectionBounces", q.reflectionBounces}, {"shadowSteps", q.shadowSteps},
                         {"volumetricSamples", q.volumetricSamples}, {"aoSamples", q.aoSamples},
                         {"detailOctaves", q.detailOctaves}, {"shardInstances", q.shardInstances},
                         {"particleCount", q.particleCount}, {"computeIterations", q.computeIterations},
                         {"memoryBytes", q.memoryBytes}, {"memoryPasses", q.memoryPasses},
                         {"workloads", {{"fragment", q.fragment}, {"geometry", q.geometry}, {"texture", q.textures},
                                        {"compute", q.compute}, {"memory", q.memory}}},
                         {"warnTempC", cfg.warnTempC}, {"stopTempC", cfg.stopTempC}};
    result.configJson = cj.dump();
    result.configKey = format("%s|%s|%ux%u|%.2f", result.benchVersion.c_str(), result.preset.c_str(), cfg.width, cfg.height, cfg.renderScale);

    auto fail = [&](const std::string& msg) {
        LOGE("Stress test failed: %s", msg.c_str());
        surf.destroy();
        TelemetryHub::instance().clearFrameStats();
        setState(EngineState::Failed, msg);
        CoUninitialize();
    };

    if (!surf.create(cfg.adapter, so, err)) return fail(err);
    if (!scene.init(surf.device(), surf.context(), err)) return fail(err);
    if (!scene.setQuality(q, err) || !scene.setResolution(cfg.width, cfg.height, err))
        return fail(std::string(T("GPU belleği ayrılamadı: ")) + err);

    GpuTimer timer;
    timer.init(surf.device());
    FrameStats stats;
    stats.reserve(200000);
    LARGE_INTEGER freq, last, start;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    last = start;
    double active = 0;            // elapsed time excluding pauses
    double sceneTime = 0;
    double sumTemp = 0, sumLoad = 0, sumPower = 0, sumClock = 0;
    int nTemp = 0, nLoad = 0, nPower = 0, nClock = 0;
    float maxTemp = NA;
    double lastSafetyCheck = -1;
    int framesThisSecond = 0;
    double secondAcc = 0;
    std::string stopReason;
    std::string finalStatus = "completed";
    bool noSensorNoted = false;
    float smoothFps = 0;
    FpsMeter meter;

    setState(EngineState::Running, T("Çalışıyor"));
    {
        std::lock_guard<std::mutex> l(m_mutex);
        m_status.allocatedBytes = scene.allocatedBytes();
        m_status.renderW = scene.renderWidth(); m_status.renderH = scene.renderHeight();
    }

    while (true) {
        bool open = surf.pump();
        if (!open || surf.escPressed.exchange(false)) { stopReason = T("Kullanıcı tarafından durduruldu"); finalStatus = "aborted"; break; }
        if (m_stop) { stopReason = T("Kullanıcı tarafından durduruldu"); finalStatus = "aborted"; break; }
        if (surf.spacePressed.exchange(false)) m_pause = !m_pause.load();
        if (!surf.handleResize()) { stopReason = T("Pencere yeniden boyutlandırılamadı (GPU cihazı kayboldu)"); finalStatus = "failed"; break; }

        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        double dt = double(now.QuadPart - last.QuadPart) / double(freq.QuadPart);
        double rawDt = dt;   // unclamped frame interval (statistics)
        last = now;

        if (m_pause) {
            setState(EngineState::Paused, T("Duraklatıldı"));
            TelemetryHub::instance().clearFrameStats();
            // Keep the window responsive with a lightweight frame (no workload).
            const float c[4] = {0.035f, 0.039f, 0.047f, 1};
            surf.context()->ClearRenderTargetView(surf.backbuffer(), c);
            if (surf.beginOverlay()) {
                drawStatusPanel(surf, cfg, status(), TelemetryHub::instance().latest(), 0, 0, -1, active, scene.renderWidth(), scene.renderHeight());
                surf.text(424, 28, widen(T("Duraklatıldı")), 28, cfg.palette.text, 1, true);
                surf.text(424, 70, widen(T("Devam etmek için Boşluk, durdurmak için Esc")), 14, cfg.palette.text2);
                surf.endOverlay();
            }
            surf.present(true);
            Sleep(30);
            continue;
        }
        if (dt > 0.5) dt = 0.5;   // ignore pause gaps / hitches for the scene clock
        active += dt;
        sceneTime += dt;

        // --- Safety: temperature thresholds (checked 4× per second) ---
        if (active - lastSafetyCheck >= 0.25) {
            lastSafetyCheck = active;
            TelemetrySample t = TelemetryHub::instance().latest();
            float temp = t.gpuTemp;
            float hot = t.hotspotTemp;
            bool warn = false;
            if (has(temp)) {
                sumTemp += temp; ++nTemp;
                maxTemp = has(maxTemp) ? std::max(maxTemp, temp) : temp;
                if (temp >= cfg.stopTempC) {
                    stopReason = format(T("Acil durdurma: GPU sıcaklığı %.0f °C (eşik %d °C)"), temp, cfg.stopTempC);
                    finalStatus = "aborted";
                    break;
                }
                warn = temp >= cfg.warnTempC;
            } else if (!noSensorNoted) {
                noSensorNoted = true;
                std::lock_guard<std::mutex> l(m_mutex);
                m_status.noTempSensor = true;
            }
            if (has(hot) && hot >= cfg.stopTempC + 10) {
                stopReason = format(T("Acil durdurma: sıcak nokta %.0f °C"), hot);
                finalStatus = "aborted";
                break;
            }
            if (has(t.gpuLoad)) { sumLoad += t.gpuLoad; ++nLoad; }
            if (has(t.powerW)) { sumPower += t.powerW; ++nPower; }
            if (has(t.coreClockMHz)) { sumClock += t.coreClockMHz; ++nClock; }
            std::lock_guard<std::mutex> l(m_mutex);
            m_status.tempWarning = warn;
        }

        // --- Render one frame ---
        timer.begin(surf.context());
        scene.render(SceneRenderer::Mode::Scene, sceneTime, (float)dt, stressCamera(sceneTime), surf.backbuffer(), surf.width(), surf.height());
        timer.end(surf.context());
        double gpuMs = timer.poll(surf.context());

        double frameMs = rawDt * 1000.0;
        if (active > 0.5) stats.add(frameMs);   // skip the first half second (pipeline warm-up)
        ++framesThisSecond; secondAcc += rawDt;
        if (secondAcc >= 1.0) {
            result.fpsTimeline.push_back((float)(framesThisSecond / secondAcc));
            TelemetrySample t = TelemetryHub::instance().latest();
            result.tempTimeline.push_back(t.gpuTemp);
            framesThisSecond = 0; secondAcc = 0;
        }
        float fpsNow = frameMs > 0 ? (float)(1000.0 / frameMs) : 0;
        smoothFps = meter.add(rawDt);
        TelemetryHub::instance().publishFrameStats(smoothFps, (float)frameMs);

        if (cfg.overlay && surf.beginOverlay()) {
            drawStatusPanel(surf, cfg, status(), TelemetryHub::instance().latest(), smoothFps, (float)frameMs, (float)gpuMs, active,
                            scene.renderWidth(), scene.renderHeight());
            surf.endOverlay();
        }

        if (surf.screenshotRequested.exchange(false)) {
            std::string stamp = nowIso8601();
            for (auto& ch : stamp) if (ch == ':') ch = '-';
            std::wstring p = Paths::screenshots() + L"\\" + widen("stress_" + stamp + ".png");
            if (surf.saveScreenshot(p)) result.screenshotPath = narrow(p);
        }

        HRESULT hr = surf.present(cfg.vsync);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG) {
            HRESULT reason = surf.deviceStatus();
            stopReason = std::string(T("GPU sürücüsü sıfırlandı veya cihaz kaldırıldı — test güvenli şekilde durduruldu (")) +
                         GpuDeviceManager::hresultText(hr) + (FAILED(reason) && reason != hr ? ", " + GpuDeviceManager::hresultText(reason) : std::string()) + ")";
            finalStatus = "failed";
            LOGE("Device lost during stress test: %s", GpuDeviceManager::hresultText(reason).c_str());
            break;
        } else if (FAILED(hr) && hr != DXGI_STATUS_OCCLUDED) {
            stopReason = std::string(T("Görüntü sunumu başarısız: ")) + GpuDeviceManager::hresultText(hr);
            finalStatus = "failed";
            break;
        }

        // --- TDR guard: if a single frame approaches the Windows GPU timeout (2 s), reduce the load ---
        if (cfg.tdrGuard && gpuMs > 400.0) {
            SceneQuality nq = scene.quality();
            nq.marchSteps = std::max(32, nq.marchSteps * 3 / 4);
            nq.volumetricSamples = nq.volumetricSamples / 2;
            nq.shadowSteps = std::max(8, nq.shadowSteps * 3 / 4);
            nq.reflectionBounces = std::max(0, nq.reflectionBounces - 1);
            nq.renderScale = std::max(0.5f, nq.renderScale * 0.85f);
            std::string e2;
            scene.setQuality(nq, e2);
            LOGW("TDR guard: GPU frame %.0f ms, workload reduced", gpuMs);
            std::lock_guard<std::mutex> l(m_mutex);
            ++m_status.tdrReductions;
            m_status.message = T("Kare süresi GPU zaman aşımı sınırına yaklaştı; yük otomatik azaltıldı");
        }

        {
            std::lock_guard<std::mutex> l(m_mutex);
            m_status.state = EngineState::Running;
            m_status.elapsedSec = active;
            m_status.fps = fpsNow;
            m_status.frameMs = (float)frameMs;
            m_status.gpuMs = (float)gpuMs;
            m_status.avgFps = (float)stats.currentAvgFps();
            m_status.minFps = (float)stats.runningMinFps();
            m_status.maxFps = (float)stats.runningMaxFps();
            m_status.frames = stats.count();
            m_status.renderW = scene.renderWidth(); m_status.renderH = scene.renderHeight();
            m_status.allocatedBytes = scene.allocatedBytes();
        }
        if (cfg.durationSec > 0 && active >= cfg.durationSec) { stopReason = T("Süre tamamlandı"); break; }
    }

    setState(EngineState::Stopping, T("Durduruluyor…"));
    TelemetryHub::instance().clearFrameStats();
    FrameStatsSummary s = stats.summarize();
    result.durationSec = active;
    result.avgFps = s.avgFps; result.minFps = s.minFps; result.maxFps = s.maxFps; result.p1LowFps = s.p1LowFps;
    result.avgFrameMs = s.avgFrameMs; result.p99FrameMs = s.p99FrameMs; result.stdFrameMs = s.stdFrameMs;
    if (nTemp) { result.avgTemp = sumTemp / nTemp; result.maxTemp = maxTemp; }
    if (nLoad) result.avgLoad = sumLoad / nLoad;
    if (nPower) result.avgPower = sumPower / nPower;
    if (nClock) result.avgClock = sumClock / nClock;
    result.status = finalStatus;
    result.statusDetail = stopReason;
    surf.destroy();
    LOGI("Stress test ended: %s (%s), avg %.1f FPS over %.1f s", finalStatus.c_str(), stopReason.c_str(), s.avgFps, active);
    if (onFinished && active > 1.0) onFinished(result);
    setState(finalStatus == "failed" ? EngineState::Failed : EngineState::Finished, stopReason);
    CoUninitialize();
}

} // namespace aether
