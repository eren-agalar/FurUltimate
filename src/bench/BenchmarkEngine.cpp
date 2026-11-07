#include "BenchmarkEngine.h"
#include "FrameStats.h"
#include "Score.h"
#include "AiBench.h"
#include "../render/RenderSurface.h"
#include "../gpu/ShaderLibrary.h"
#include "../telemetry/Telemetry.h"
#include "../core/Log.h"
#include "../core/Strings.h"
#include "../core/I18n.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstring>

namespace aether {

static const BenchTestInfo kTests[kBenchTestCount] = {
    {"graphics", "Grafik İşleme", "Sinematik Nova Core sahnesi: ışın yürütme, yansımalar, hacimsel ışık, prosedürel geometri"},
    {"shader", "Shader Performansı", "Mandelbulb fraktal mesafe tahmini — ALU yoğun piksel shader"},
    {"compute", "Hesaplama Performansı", "Bağımlı FMA zincirleri — FP32 TFLOPS (GPU zaman damgası ile)"},
    {"vram", "VRAM Performansı", "Büyük tampon kopyalama — okuma + yazma bant genişliği (GB/s)"},
    {"stability", "Kare Kararlılığı", "Sürekli yük altında kare süresi tutarlılığı (%1 düşük / ortalama)"},
    {"ai", "Yapay Zekâ Performansı", "Matris çarpımı (sinir ağlarının temel işlemi) — DirectML FP16 ve shader FP32, TOPS"},
};
const BenchTestInfo* benchTests() { return kTests; }

const char* benchPresetId(BenchPreset p) {
    switch (p) {
        case BenchPreset::High1440: return "HIGH-1440P";
        case BenchPreset::Ultra2160: return "ULTRA-2160P";
        default: return "STANDARD-1080P";
    }
}
void benchPresetResolution(BenchPreset p, uint32_t& w, uint32_t& h) {
    switch (p) {
        case BenchPreset::High1440: w = 2560; h = 1440; break;
        case BenchPreset::Ultra2160: w = 3840; h = 2160; break;
        default: w = 1920; h = 1080; break;
    }
}

namespace {
struct Durations { double gWarm, gRun, sWarm, sRun, cRun, vRun, fWarm, fRun, aiRun; };
Durations durations(bool quick, bool validation = false) {
    if (validation) return {0.5, 4, 0.5, 3, 2, 2, 0.5, 4, 2};
    if (quick) return {2, 12, 1.5, 8, 4, 4, 1.5, 12, 6};
    return {3, 30, 2, 20, 8, 8, 2, 30, 12};
}
// Planned wall time of test i (warm-up included). AI time is split between the shader and DirectML paths.
double testDuration(const Durations& d, int i) {
    switch (i) {
        case 0: return d.gWarm + d.gRun;
        case 1: return d.sWarm + d.sRun;
        case 2: return d.cRun;
        case 3: return d.vRun;
        case 4: return d.fWarm + d.fRun;
        default: return d.aiRun + 2.0;
    }
}
SceneQuality benchSceneQuality() {
    SceneQuality q;
    q.marchSteps = 160; q.reflectionBounces = 2; q.shadowSteps = 40; q.volumetricSamples = 24; q.aoSamples = 4;
    q.detailOctaves = 3; q.shardInstances = 40000; q.particleCount = 262144; q.computeIterations = 4;
    q.memoryBytes = 128ull << 20; q.memoryPasses = 1; q.renderScale = 1.0f;
    return q;
}
SceneQuality stabilityQuality() {
    SceneQuality q = benchSceneQuality();
    q.marchSteps = 128; q.volumetricSamples = 16; q.shardInstances = 20000;
    return q;
}
SceneQuality fractalQuality() {
    SceneQuality q;
    q.marchSteps = 160; q.detailOctaves = 10;   // detailOctaves = Mandelbulb iterations in fractal mode
    q.compute = false; q.memory = false; q.geometry = false; q.bloom = true; q.renderScale = 1.0f;
    return q;
}
struct alignas(16) FmaCB { uint32_t iterations; float mul, add, seed; };
struct alignas(16) CopyCB { uint32_t elements, stride, pad0, pad1; };

// Blocking GPU timing of a block of commands using timestamp queries.
class BlockingTimer {
public:
    bool init(ID3D11Device* d) {
        D3D11_QUERY_DESC dj{D3D11_QUERY_TIMESTAMP_DISJOINT, 0}, ts{D3D11_QUERY_TIMESTAMP, 0}, ev{D3D11_QUERY_EVENT, 0};
        return SUCCEEDED(d->CreateQuery(&dj, m_dj.GetAddressOf())) && SUCCEEDED(d->CreateQuery(&ts, m_b.GetAddressOf())) &&
               SUCCEEDED(d->CreateQuery(&ts, m_e.GetAddressOf())) && SUCCEEDED(d->CreateQuery(&ev, m_ev.GetAddressOf()));
    }
    // Waits until all previously submitted GPU work (e.g. the overlay frame) has finished, so the timed batch
    // starts on an idle queue and the wall-clock plausibility check measures only this batch.
    void drain(ID3D11DeviceContext* c) {
        c->End(m_ev.Get());
        c->Flush();
        BOOL done = FALSE;
        DWORD t0 = GetTickCount();
        while (c->GetData(m_ev.Get(), &done, sizeof(done), 0) == S_FALSE && GetTickCount() - t0 < 5000) Sleep(0);
    }
    template <class F> double measure(ID3D11DeviceContext* c, F&& work) {
        if (!m_qpf.QuadPart) QueryPerformanceFrequency(&m_qpf);
        drain(c);
        LARGE_INTEGER w0; QueryPerformanceCounter(&w0);
        c->Begin(m_dj.Get());
        c->End(m_b.Get());
        work();
        c->End(m_e.Get());
        c->End(m_dj.Get());
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
        UINT64 b = 0, e = 0;
        DWORD t0 = GetTickCount();
        while (c->GetData(m_dj.Get(), &dj, sizeof(dj), 0) == S_FALSE) { if (GetTickCount() - t0 > 5000) return -1; Sleep(0); }
        while (c->GetData(m_b.Get(), &b, sizeof(b), 0) == S_FALSE) Sleep(0);
        while (c->GetData(m_e.Get(), &e, sizeof(e), 0) == S_FALSE) Sleep(0);
        LARGE_INTEGER w1; QueryPerformanceCounter(&w1);
        double wallMs = double(w1.QuadPart - w0.QuadPart) * 1000.0 / double(m_qpf.QuadPart);
        if (dj.Disjoint || dj.Frequency == 0 || e < b) return -1;
        double gpuMs = double(e - b) * 1000.0 / double(dj.Frequency);
        // Plausibility guard: the queries resolve only after the work finished, so the GPU interval can never be
        // much shorter than the wall-clock interval around it. Broken timestamp implementations (seen on software
        // rasterizers) report impossible durations; fall back to wall-clock time instead of reporting inflated results.
        if (wallMs > 2.0 && gpuMs < wallMs * 0.5) {
            if (!m_warned && !m_quiet) { LOGW("GPU timestamps inconsistent (%.3f ms GPU vs %.3f ms wall); using wall-clock timing", gpuMs, wallMs); m_warned = true; }
            return wallMs;
        }
        return gpuMs;
    }
public:
    void setQuiet(bool q) { m_quiet = q; }   // warm-up / calibration: first-use driver work is expected
private:
    bool m_quiet = false;
    LARGE_INTEGER m_qpf{};
    bool m_warned = false;
    ComPtr<ID3D11Query> m_dj, m_b, m_e, m_ev;
};
} // namespace

BenchmarkEngine::~BenchmarkEngine() { stop(); join(); }

std::string BenchmarkEngine::configKey(const BenchConfig& cfg) {
    std::string key = cfg.validation ? format("%s|VALIDATION-270P|VALIDATION", score::kBenchmarkVersion)
                                     : format("%s|%s|%s", score::kBenchmarkVersion, benchPresetId(cfg.preset), cfg.quick ? "QUICK" : "FULL");
    // Partial runs (single tests) get their own key so they are only compared with identical partial runs.
    if (!isFullRun(cfg.testMask)) {
        key += "|T:";
        bool first = true;
        for (int i = 0; i < kBenchTestCount; ++i)
            if (cfg.testMask & (1u << i)) { key += (first ? "" : ",") + std::string(kTests[i].id); first = false; }
    }
    return key;
}
std::string BenchmarkEngine::aiConfigKey(const std::string& path) { return format("%s|%s", score::kAiVersion, path.c_str()); }
double BenchmarkEngine::plannedDuration(const BenchConfig& cfg) {
    Durations d = durations(cfg.quick, cfg.validation);
    double t = 3.0;
    for (int i = 0; i < kBenchTestCount; ++i) if (cfg.testMask & (1u << i)) t += testDuration(d, i);
    return t;
}

bool BenchmarkEngine::busy() const {
    std::lock_guard<std::mutex> l(m_mutex);
    return m_status.state == EngineState::Starting || m_status.state == EngineState::Running || m_status.state == EngineState::Stopping;
}

bool BenchmarkEngine::start(const BenchConfig& cfg) {
    if (busy()) return false;
    join();
    m_stop = false;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        m_status = BenchStatus{};
        m_status.state = EngineState::Starting;
        m_status.totalSec = plannedDuration(cfg);
        m_status.testMask = cfg.testMask;
        m_status.message = T("Shader'lar hazırlanıyor…");
    }
    m_thread = std::thread([this, cfg] { run(cfg); });
    return true;
}
void BenchmarkEngine::stop() { m_stop = true; }
void BenchmarkEngine::join() { if (m_thread.joinable()) m_thread.join(); }
BenchStatus BenchmarkEngine::status() const { std::lock_guard<std::mutex> l(m_mutex); return m_status; }

void BenchmarkEngine::run(BenchConfig cfg) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const Durations D = durations(cfg.quick, cfg.validation);
    uint32_t rw, rh;
    benchPresetResolution(cfg.preset, rw, rh);
    if (cfg.validation) { rw = 480; rh = 270; }
    LOGI("Benchmark starting: %s", configKey(cfg).c_str());

    BenchmarkResult result;
    result.kind = "benchmark";
    result.timestamp = nowIso8601();
    result.gpuName = cfg.gpuName; result.gpuVendor = cfg.gpuVendor; result.driverVersion = cfg.driverVersion;
    result.benchVersion = score::kBenchmarkVersion;
    result.preset = cfg.validation ? "VALIDATION-270P" : benchPresetId(cfg.preset);
    result.configKey = configKey(cfg);
    result.width = rw; result.height = rh;
    nlohmann::json cj = {{"preset", result.preset}, {"quick", cfg.quick}, {"validation", cfg.validation}, {"fullscreen", cfg.fullscreen},
                         {"renderWidth", rw}, {"renderHeight", rh},
                         {"durationsSec", {{"graphics", D.gRun}, {"shader", D.sRun}, {"compute", D.cRun}, {"vram", D.vRun}, {"stability", D.fRun}}},
                         {"reference", {{"graphicsFps", score::kRefGraphicsFps}, {"shaderFps", score::kRefShaderFps},
                                        {"computeGflops", score::kRefComputeGflops}, {"vramGBs", score::kRefVramGBs}}},
                         {"weights", {{"graphics", score::kWeightGraphics}, {"shader", score::kWeightShader},
                                      {"compute", score::kWeightCompute}, {"vram", score::kWeightVram}, {"stability", score::kWeightStability}}}};
    result.configJson = cj.dump();

    RenderSurface surf;
    SceneRenderer scene;
    std::string err;
    SurfaceOptions so;
    so.fullscreen = cfg.fullscreen; so.monitor = cfg.monitor; so.width = 1280; so.height = 720;
    so.title = widen(std::string("FurUltimate — ") + T("Benchmark"));

    auto finish = [&](EngineState st, const std::string& msg) {
        surf.destroy();
        TelemetryHub::instance().clearFrameStats();
        std::lock_guard<std::mutex> l(m_mutex);
        m_status.state = st;
        m_status.message = msg;
    };
    auto fail = [&](const std::string& msg) { LOGE("Benchmark failed: %s", msg.c_str()); finish(EngineState::Failed, msg); CoUninitialize(); };

    if (!surf.create(cfg.adapter, so, err)) return fail(err);
    if (!scene.init(surf.device(), surf.context(), err)) return fail(err);
    std::string e;
    auto& lib = ShaderLibrary::instance();
    ComPtr<ID3D11ComputeShader> csFma = lib.cs(surf.device(), ShaderSource::ComputeBench, "CSFma", e);
    ComPtr<ID3D11ComputeShader> csCopy = lib.cs(surf.device(), ShaderSource::VramBench, "CSCopy", e);
    ComPtr<ID3D11ComputeShader> csFill = lib.cs(surf.device(), ShaderSource::VramBench, "CSFill", e);
    if (!csFma || !csCopy || !csFill) return fail(std::string(T("Shader derlenemedi: ")) + e);

    LARGE_INTEGER freq, runStart;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&runStart);
    auto now = [&] { LARGE_INTEGER n; QueryPerformanceCounter(&n); return double(n.QuadPart) / double(freq.QuadPart); };
    double t0 = now();
    std::string abortReason, abortStatus;
    double sumTemp = 0, sumLoad = 0, sumPower = 0, sumClock = 0; int nTemp = 0, nLoad = 0, nPower = 0, nClock = 0;
    float maxTemp = NA;
    double lastSample = -1;
    int testIndex = 0;

    if ((cfg.testMask & 0x3F) == 0) cfg.testMask = 0x3F;
    double plannedBefore[kBenchTestCount] = {};
    int ordinal[kBenchTestCount] = {}, selectedCount = 0;
    double plannedTotal = 0;
    for (int i = 0; i < kBenchTestCount; ++i) {
        plannedBefore[i] = plannedTotal;
        if (cfg.testMask & (1u << i)) { plannedTotal += testDuration(D, i); ordinal[i] = ++selectedCount; }
    }
    plannedTotal = std::max(plannedTotal, 1.0);
    auto selected = [&](int i) { return (cfg.testMask & (1u << i)) != 0; };

    // Returns false if the run must stop (user, temperature, device loss).
    auto tick = [&](double testFrac) -> bool {
        if (!surf.pump() || surf.escPressed.exchange(false) || m_stop) { abortReason = T("Kullanıcı tarafından durduruldu"); abortStatus = "aborted"; return false; }
        if (!surf.handleResize()) { abortReason = T("GPU cihazı kayboldu"); abortStatus = "failed"; return false; }
        double t = now() - t0;
        if (t - lastSample >= 0.5) {
            lastSample = t;
            TelemetrySample s = TelemetryHub::instance().latest();
            if (has(s.gpuTemp)) {
                sumTemp += s.gpuTemp; ++nTemp; maxTemp = has(maxTemp) ? std::max(maxTemp, s.gpuTemp) : s.gpuTemp;
                if (s.gpuTemp >= cfg.stopTempC) {
                    abortReason = format(T("Acil durdurma: GPU sıcaklığı %.0f °C (eşik %d °C)"), s.gpuTemp, cfg.stopTempC);
                    abortStatus = "aborted";
                    return false;
                }
            }
            if (has(s.gpuLoad)) { sumLoad += s.gpuLoad; ++nLoad; }
            if (has(s.powerW)) { sumPower += s.powerW; ++nPower; }
            if (has(s.coreClockMHz)) { sumClock += s.coreClockMHz; ++nClock; }
        }
        std::lock_guard<std::mutex> l(m_mutex);
        m_status.state = EngineState::Running;
        m_status.testIndex = testIndex;
        m_status.testProgress = std::clamp(testFrac, 0.0, 1.0);
        m_status.elapsedSec = t;
        m_status.overallProgress = std::clamp((plannedBefore[testIndex] + testFrac * testDuration(D, testIndex)) / plannedTotal, 0.0, 1.0);
        return true;
    };

    auto overlay = [&](const std::wstring& big, const std::wstring& sub, double frac) {
        if (!cfg.overlay || !surf.beginOverlay()) return;
        const OverlayPalette& p = cfg.palette;
        surf.panel(20, 20, 380, 148, 16, p.panel, p.panelAlpha);
        surf.text(38, 34, widen(format("FurUltimate · %s %d/%d", T("Test"), ordinal[testIndex], selectedCount)), 11, p.text2, 1, true);
        surf.text(38, 54, widen(T(kTests[testIndex].name)), 15, p.text, 1, true);
        surf.text(38, 76, big, 30, p.text, 1, true);
        surf.text(38, 118, sub, 11.5f, p.text2);
        TelemetrySample tl = TelemetryHub::instance().latest();
        std::wstring tt = has(tl.gpuTemp) ? wformat(L"%.0f °C", tl.gpuTemp) : std::wstring(L"N/A");
        surf.textRight(382, 34, tt, 11, p.text2, 1, true);
        surf.bar(38, 146, 344, 4, (float)frac, p.accent);
        surf.endOverlay();
    };

    auto presentChecked = [&]() -> bool {
        HRESULT hr = surf.present(false);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG) {
            HRESULT reason = surf.deviceStatus();
            abortReason = std::string(T("GPU sürücüsü sıfırlandı veya cihaz kaldırıldı — test güvenli şekilde durduruldu (")) +
                          GpuDeviceManager::hresultText(hr) + (FAILED(reason) && reason != hr ? ", " + GpuDeviceManager::hresultText(reason) : std::string()) + ")";
            abortStatus = "failed";
            return false;
        }
        return true;
    };

    // Frame-based test loop used by the graphics, shader and stability tests.
    auto renderTest = [&](SceneRenderer::Mode mode, const SceneQuality& q, double warm, double run, int camKind,
                          FrameStats& stats) -> bool {
        std::string qe;
        if (!scene.setQuality(q, qe) || !scene.setResolution(rw, rh, qe)) {
            abortReason = std::string(T("GPU belleği ayrılamadı: ")) + qe; abortStatus = "failed"; return false;
        }
        GpuTimer gt; gt.init(surf.device());
        double start = now(), last = start, sceneTime = 0;
        bool measuring = false;
        double measureStart = 0;
        float smooth = 0;
        FpsMeter meter;
        while (true) {
            double t = now();
            double dt = t - last; last = t;
            double el = t - start;
            if (!measuring && el >= warm) { measuring = true; measureStart = t; sceneTime = 0; stats.reset(); }
            double frac = measuring ? (t - measureStart) / run : 0.0;
            if (measuring && t - measureStart >= run) break;
            if (!tick(warm + run > 0 ? (el / (warm + run)) : 1)) return false;
            sceneTime += std::min(dt, 0.25);
            CameraState cam = camKind == 0 ? cinematicCamera(sceneTime, run) : (camKind == 1 ? fractalCamera(sceneTime) : stressCamera(sceneTime));
            gt.begin(surf.context());
            scene.render(mode, sceneTime, (float)std::min(dt, 0.05), cam, surf.backbuffer(), surf.width(), surf.height());
            gt.end(surf.context());
            double gpuMs = gt.poll(surf.context());
            if (gpuMs > 1500.0) {   // too slow for this preset — would risk a driver timeout (TDR)
                abortReason = T("Bu ön ayar bu GPU için çok ağır (kare süresi GPU zaman aşımı sınırına yaklaştı). Daha düşük bir ön ayar seçin.");
                abortStatus = "failed";
                return false;
            }
            if (measuring) stats.add(dt * 1000.0);
            float fpsNow = dt > 0 ? (float)(1.0 / dt) : 0;
            smooth = meter.add(dt);
            (void)fpsNow;
            TelemetryHub::instance().publishFrameStats(smooth, (float)(dt * 1000.0));
            overlay(wformat(L"%.0f FPS", smooth),
                    measuring ? widen(format("%s %.1f · %ux%u", T("Ortalama"), stats.currentAvgFps(), rw, rh))
                              : widen(T("Isınma turu (ölçülmüyor)")), measuring ? frac : 0.0);
            {
                std::lock_guard<std::mutex> l(m_mutex);
                m_status.warmup = !measuring;
                m_status.fps = smooth; m_status.frameMs = (float)(dt * 1000.0);
                m_status.avgFps = (float)stats.currentAvgFps();
                m_status.minFps = (float)stats.runningMinFps(); m_status.maxFps = (float)stats.runningMaxFps();
                m_status.liveUnit = "FPS"; m_status.liveValue = stats.currentAvgFps();
            }
            if (!presentChecked()) return false;
        }
        return true;
    };

    // Simple frame (clear + overlay) used between compute batches to keep the window responsive.
    auto idleFrame = [&](const std::wstring& big, const std::wstring& sub, double frac) -> bool {
        const float c[4] = {0.035f, 0.039f, 0.047f, 1};
        surf.context()->ClearRenderTargetView(surf.backbuffer(), c);
        overlay(big, sub, frac);
        return presentChecked();
    };

    auto addSub = [&](int idx, double value, const char* unit, double sc, const FrameStatsSummary* fs, double dur) {
        SubResult s;
        s.id = kTests[idx].id; s.name = kTests[idx].name; s.value = value; s.unit = unit; s.score = sc; s.durationSec = dur;
        if (fs) { s.avgFps = fs->avgFps; s.minFps = fs->minFps; s.maxFps = fs->maxFps; s.p1LowFps = fs->p1LowFps; s.avgFrameMs = fs->avgFrameMs; s.p99FrameMs = fs->p99FrameMs; }
        result.subs.push_back(s);
        std::lock_guard<std::mutex> l(m_mutex);
        m_status.subScores[idx] = sc;
        m_status.subValues[idx] = value;
    };

    bool ok = true;
    double gScore = 0, sScore = 0, cScore = 0, vScore = 0, fScore = 0;
    FrameStatsSummary graphicsSummary{};

    // 1. Graphics rendering ---------------------------------------------------------------
    testIndex = 0;
    if (selected(0)) {
        FrameStats st; st.reserve(100000);
        ok = renderTest(SceneRenderer::Mode::Scene, benchSceneQuality(), D.gWarm, D.gRun, 0, st);
        if (ok) {
            graphicsSummary = st.summarize();
            gScore = score::graphics(graphicsSummary.avgFps);
            addSub(0, graphicsSummary.avgFps, "FPS", gScore, &graphicsSummary, graphicsSummary.durationSec);
            for (double f : st.windowFps()) result.fpsTimeline.push_back((float)f);
        }
    }
    // 2. Shader performance ---------------------------------------------------------------
    if (ok && selected(1)) {
        testIndex = 1;
        FrameStats st; st.reserve(100000);
        ok = renderTest(SceneRenderer::Mode::Fractal, fractalQuality(), D.sWarm, D.sRun, 1, st);
        if (ok) {
            FrameStatsSummary s = st.summarize();
            sScore = score::shader(s.avgFps);
            addSub(1, s.avgFps, "FPS", sScore, &s, s.durationSec);
        }
    }
    TelemetryHub::instance().clearFrameStats();
    BlockingTimer bt;
    bt.init(surf.device());
    ID3D11DeviceContext* ctx = surf.context();
    ComPtr<ID3D11Buffer> cb;
    {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = 16; bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        surf.device()->CreateBuffer(&bd, nullptr, cb.GetAddressOf());
    }
    auto writeCB = [&](const void* data) {
        D3D11_MAPPED_SUBRESOURCE ms;
        if (SUCCEEDED(ctx->Map(cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) { memcpy(ms.pData, data, 16); ctx->Unmap(cb.Get(), 0); }
    };
    // Release scene GPU memory before the compute tests.
    if (ok) { SceneQuality lean = fractalQuality(); std::string qe; scene.setQuality(lean, qe); }

    // 3. Compute performance --------------------------------------------------------------
    if (ok && selected(2)) {
        testIndex = 2;
        const uint32_t threads = 1u << 20;
        ComPtr<ID3D11Buffer> outBuf;
        ComPtr<ID3D11UnorderedAccessView> uav;
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = threads * 16; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        D3D11_UNORDERED_ACCESS_VIEW_DESC fu{};
        fu.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; fu.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; fu.Buffer.NumElements = threads;
        if (FAILED(surf.device()->CreateBuffer(&bd, nullptr, outBuf.GetAddressOf())) ||
            FAILED(surf.device()->CreateUnorderedAccessView(outBuf.Get(), &fu, uav.GetAddressOf()))) {
            ok = false; abortReason = T("Hesaplama tamponu ayrılamadı"); abortStatus = "failed";
        } else {
            ctx->CSSetShader(csFma.Get(), nullptr, 0);
            ctx->CSSetUnorderedAccessViews(0, 1, uav.GetAddressOf(), nullptr);
            ctx->CSSetConstantBuffers(1, 1, cb.GetAddressOf());
            FmaCB f{32, 0.99999f, 0.00001f, 0.5f};
            auto dispatchMs = [&](uint32_t iters) {
                f.iterations = iters; writeCB(&f);
                return bt.measure(ctx, [&] { ctx->Dispatch(threads / 256, 1, 1); });
            };
            // Calibrate: target ~25 ms per dispatch (well below the 2 s driver timeout).
            bt.setQuiet(true);
            double ms = dispatchMs(32);
            uint32_t iters = 32;
            for (int k = 0; k < 6 && ms > 0 && ms < 12.0 && iters < 8192; ++k) { iters *= 2; ms = dispatchMs(iters); }
            // Warm-up so clocks reach their full-performance state before sampling.
            for (double ws = now(); now() - ws < (cfg.validation ? 0.2 : cfg.quick ? 0.5 : 1.0);) {
                if (!tick(0.0)) { ok = false; break; }
                dispatchMs(iters);
            }
            bt.setQuiet(false);
            std::vector<double> samples;
            double start = now(), lastComputeUi = -1;
            while (ok && now() - start < D.cRun) {
                if (!tick((now() - start) / D.cRun)) { ok = false; break; }
                f.seed = (float)(samples.size() % 7) * 0.01f + 0.5f;
                double m = dispatchMs(iters);
                if (m > 0) {
                    double flops = double(threads) * iters * 16.0 * 4.0 * 4.0 * 2.0;
                    samples.push_back(flops / (m / 1000.0) / 1e9);
                }
                double med = median(samples);
                { std::lock_guard<std::mutex> l(m_mutex); m_status.liveValue = med / 1000.0; m_status.liveUnit = "TFLOPS"; m_status.fps = 0; }
                if (now() - lastComputeUi >= 0.1) {   // overlay at most 10x per second (keeps the GPU busy)
                    lastComputeUi = now();
                    if (!idleFrame(wformat(L"%.2f TFLOPS", med / 1000.0), widen(format("FP32 FMA · %u %s", iters, T("iterasyon"))), (now() - start) / D.cRun)) { ok = false; break; }
                    ctx->CSSetShader(csFma.Get(), nullptr, 0);
                    ctx->CSSetUnorderedAccessViews(0, 1, uav.GetAddressOf(), nullptr);
                    ctx->CSSetConstantBuffers(1, 1, cb.GetAddressOf());
                }
            }
            ID3D11UnorderedAccessView* nu = nullptr;
            ctx->CSSetUnorderedAccessViews(0, 1, &nu, nullptr);
            if (ok) {
                if (samples.empty()) { ok = false; abortReason = T("GPU zaman damgası sorguları desteklenmiyor"); abortStatus = "failed"; }
                else { double g = median(samples); cScore = score::compute(g); addSub(2, g / 1000.0, "TFLOPS", cScore, nullptr, D.cRun); }
            }
        }
    }
    // 4. VRAM performance -----------------------------------------------------------------
    if (ok && selected(3)) {
        testIndex = 3;
        uint64_t per = cfg.vramBytes > 0 ? cfg.vramBytes / 8 : (256ull << 20);
        per = std::clamp<uint64_t>(per, 64ull << 20, 512ull << 20) & ~uint64_t(0xFFFF);
        ComPtr<ID3D11Texture2D> src, dst;
        ComPtr<ID3D11ShaderResourceView> srv;
        ComPtr<ID3D11UnorderedAccessView> uav;
        bool allocated = false;
        for (int attempt = 0; attempt < 3 && !allocated; ++attempt) {
            D3D11_TEXTURE2D_DESC td{};
            td.Width = 4096; td.Height = (UINT)(per / 16 / 4096);
            td.MipLevels = 1; td.ArraySize = 1; td.SampleDesc.Count = 1;
            td.Format = DXGI_FORMAT_R32G32B32A32_UINT; td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            src.Reset(); dst.Reset(); srv.Reset(); uav.Reset();
            if (SUCCEEDED(surf.device()->CreateTexture2D(&td, nullptr, src.GetAddressOf())) &&
                SUCCEEDED(surf.device()->CreateTexture2D(&td, nullptr, dst.GetAddressOf())) &&
                SUCCEEDED(surf.device()->CreateShaderResourceView(src.Get(), nullptr, srv.GetAddressOf())) &&
                SUCCEEDED(surf.device()->CreateUnorderedAccessView(dst.Get(), nullptr, uav.GetAddressOf())))
                allocated = true;
            else per = (per / 2) & ~uint64_t(0xFFFF);
        }
        if (!allocated) { ok = false; abortReason = T("VRAM test tamponu ayrılamadı"); abortStatus = "failed"; }
        else {
            CopyCB c{};
            c.elements = (uint32_t)(per / 16);
            uint32_t groups = std::min<uint32_t>((c.elements + 255) / 256, 65535u);
            c.stride = groups * 256;
            ID3D11UnorderedAccessView* nu = nullptr; ID3D11ShaderResourceView* ns = nullptr;
            // 1) Fill both surfaces with incompressible data (memory compression would otherwise distort results).
            {
                ComPtr<ID3D11UnorderedAccessView> srcUav;
                surf.device()->CreateUnorderedAccessView(src.Get(), nullptr, srcUav.GetAddressOf());
                ctx->CSSetShader(csFill.Get(), nullptr, 0);
                ctx->CSSetConstantBuffers(1, 1, cb.GetAddressOf());
                for (int k = 0; k < 2; ++k) {
                    CopyCB f = c; f.pad0 = 0x51ED0000u + k;
                    writeCB(&f);
                    ID3D11UnorderedAccessView* target = k == 0 ? srcUav.Get() : uav.Get();
                    ctx->CSSetUnorderedAccessViews(1, 1, &target, nullptr);
                    ctx->Dispatch(groups, 1, 1);
                }
                ctx->CSSetUnorderedAccessViews(1, 1, &nu, nullptr);
            }
            writeCB(&c);
            auto bind = [&] {
                ctx->CSSetShader(csCopy.Get(), nullptr, 0);
                ctx->CSSetShaderResources(0, 1, srv.GetAddressOf());
                ctx->CSSetUnorderedAccessViews(0, 1, uav.GetAddressOf(), nullptr);
                ctx->CSSetConstantBuffers(1, 1, cb.GetAddressOf());
            };
            const double movedPerPass = double(per) * 2.0;   // read + write
            const std::wstring subText = widen(format("%s · %s", T("Okuma + yazma"), formatBytes(per * 2).c_str()));
            double lastUi = -1;
            auto ui = [&](double value, double frac) -> bool {   // overlay at most 10x per second
                if (now() - lastUi < 0.1) return true;
                lastUi = now();
                bool r = idleFrame(value > 0 ? wformat(L"%.0f GB/s", value) : std::wstring(widen(T("Isınma"))), subText, frac);
                bind();
                return r;
            };
            // 2) Warm-up: keep the memory busy so clocks reach their full-performance state, and calibrate the
            //    batch size to ~25 ms of work (long batches average out scheduling noise).
            bind();
            double warm = cfg.validation ? 0.2 : cfg.quick ? 0.6 : 1.0;
            double warmStart = now();
            int passes = 4;
            bt.setQuiet(true);
            while (ok && now() - warmStart < warm) {
                if (!tick(0.0) || !ui(0, 0.0)) { ok = false; break; }
                double m = bt.measure(ctx, [&] { for (int i = 0; i < passes; ++i) ctx->Dispatch(groups, 1, 1); });
                if (m > 0) passes = std::clamp((int)std::lround(passes * 25.0 / std::max(m, 0.1)), 2, 64);
            }
            // 3) Measurement.
            bt.setQuiet(false);
            std::vector<double> samples;
            double start = now();
            while (ok && now() - start < D.vRun) {
                double frac = (now() - start) / D.vRun;
                if (!tick(frac)) { ok = false; break; }
                double m = bt.measure(ctx, [&] { for (int i = 0; i < passes; ++i) ctx->Dispatch(groups, 1, 1); });
                if (m > 0) samples.push_back(movedPerPass * passes / (m / 1000.0) / 1e9);
                double med = median(samples);
                { std::lock_guard<std::mutex> l(m_mutex); m_status.liveValue = med; m_status.liveUnit = "GB/s"; }
                if (!ui(med, frac)) { ok = false; break; }
            }
            if (!samples.empty()) {
                std::vector<double> sorted = samples;
                std::sort(sorted.begin(), sorted.end());
                double med = median(samples);
                double p10 = sorted[sorted.size() / 10], p90 = sorted[sorted.size() * 9 / 10];
                LOGI("VRAM bandwidth: median %.1f GB/s (p10 %.1f, p90 %.1f, spread %.1f %%), %zu samples, %d passes x %s",
                     med, p10, p90, med > 0 ? (p90 - p10) / med * 100.0 : 0.0, samples.size(), passes, formatBytes(per * 2).c_str());
            }
            ctx->CSSetUnorderedAccessViews(0, 1, &nu, nullptr);
            ctx->CSSetShaderResources(0, 1, &ns);
            if (ok) {
                if (samples.empty()) { ok = false; abortReason = T("GPU zaman damgası sorguları desteklenmiyor"); abortStatus = "failed"; }
                else { double g = median(samples); vScore = score::vram(g); addSub(3, g, "GB/s", vScore, nullptr, D.vRun); }
            }
        }
    }
    // 5. Frame stability ------------------------------------------------------------------
    if (ok && selected(4)) {
        testIndex = 4;
        FrameStats st; st.reserve(100000);
        ok = renderTest(SceneRenderer::Mode::Scene, stabilityQuality(), D.fWarm, D.fRun, 2, st);
        if (ok) {
            FrameStatsSummary s = st.summarize();
            fScore = score::stability(s.p1LowFps, s.avgFps);
            double ratio = s.avgFps > 0 ? 100.0 * std::min(1.0, s.p1LowFps / s.avgFps) : 0;
            addSub(4, ratio, "%", fScore, &s, s.durationSec);
        }
    }

    // 6. AI performance (separate AI score) --------------------------------------------------
    if (ok && selected(kAiTestIndex)) {
        testIndex = kAiTestIndex;
        { SceneQuality lean = fractalQuality(); std::string qe; scene.setQuality(lean, qe); }
        TelemetryHub::instance().clearFrameStats();
        auto aiTick = [&](const char* phase, double base, double span) {
            return [&, phase, base, span](double frac, double tops) {
                if (!tick(base + frac * span)) return false;
                { std::lock_guard<std::mutex> l(m_mutex); m_status.liveValue = tops; m_status.liveUnit = "TOPS"; m_status.fps = 0; }
                return idleFrame(wformat(L"%.1f TOPS", tops), widen(phase), base + frac * span);
            };
        };
        // A: shader FP32 matrix multiplication (every GPU)
        AiMeasurement shader = runShaderGemm(surf.device(), surf.context(), D.aiRun * 0.4, aiTick("Shader FP32 GEMM", 0.0, 0.4));
        if (shader.error == "cancelled") ok = false;
        // B: DirectML (FP16 tensor / matrix units when supported). Uses its own Direct3D 12 device.
        AiMeasurement dmlRes;
        if (ok) {
            idleFrame(L"…", widen(T("DirectML hazırlanıyor…")), 0.4);
            dmlRes = runDirectMLIsolated(cfg.adapter, D.aiRun * 0.6, aiTick("DirectML GEMM", 0.4, 0.6));
            if (dmlRes.error == "cancelled") ok = false;
        }
        if (ok) {
            result.aiShaderTops = shader.ok ? shader.tops : 0;
            result.aiDirectMLTops = dmlRes.ok ? dmlRes.tops : 0;
            if (dmlRes.ok && dmlRes.tops >= result.aiShaderTops) { result.aiTops = dmlRes.tops; result.aiPath = "DirectML " + dmlRes.precision; }
            else if (shader.ok) { result.aiTops = shader.tops; result.aiPath = "Shader FP32"; }
            if (result.aiTops > 0) {
                result.aiScore = std::round(score::ai(result.aiTops));
                result.aiTheoreticalTops = theoreticalAiTops(cfg.vendorId, cfg.gpuName);
                result.aiConfigKey = aiConfigKey(result.aiPath);
                addSub(kAiTestIndex, result.aiTops, "TOPS", result.aiScore, nullptr, D.aiRun);
                std::lock_guard<std::mutex> l(m_mutex);
                m_status.aiTops = result.aiTops; m_status.aiPath = result.aiPath; m_status.aiTheoreticalTops = result.aiTheoreticalTops;
                LOGI("AI score %.0f (%.1f TOPS, %s; shader %.1f, DirectML %.1f)", result.aiScore, result.aiTops, result.aiPath.c_str(),
                     result.aiShaderTops, result.aiDirectMLTops);
            } else {
                ok = false; abortStatus = "failed";
                abortReason = std::string(T("AI testi ölçüm alamadı: ")) + (shader.error.empty() ? dmlRes.error : shader.error);
            }
        }
    }

    double total = now() - t0;
    result.durationSec = total;
    result.avgFps = graphicsSummary.avgFps; result.minFps = graphicsSummary.minFps; result.maxFps = graphicsSummary.maxFps;
    result.p1LowFps = graphicsSummary.p1LowFps; result.avgFrameMs = graphicsSummary.avgFrameMs;
    result.p99FrameMs = graphicsSummary.p99FrameMs; result.stdFrameMs = graphicsSummary.stdFrameMs;
    if (nTemp) { result.avgTemp = sumTemp / nTemp; result.maxTemp = maxTemp; }
    if (nLoad) result.avgLoad = sumLoad / nLoad;
    if (nPower) result.avgPower = sumPower / nPower;
    if (nClock) result.avgClock = sumClock / nClock;
    result.fullRun = isFullRun(cfg.testMask);
    for (int i = 0; i < kBenchTestCount; ++i)
        if (selected(i)) result.testsRun += (result.testsRun.empty() ? "" : ",") + std::string(kTests[i].id);
    if (ok && !result.fullRun) {
        result.score = 0;   // single / partial runs report sub-scores only
        result.status = "completed";
        LOGI("Partial benchmark completed (%s)", result.testsRun.c_str());
    } else if (ok) {
        result.score = std::round(score::overall(gScore, sScore, cScore, vScore, fScore));
        result.status = "completed";
        LOGI("Benchmark completed: score %.0f (G %.0f S %.0f C %.0f V %.0f F %.0f)", result.score, gScore, sScore, cScore, vScore, fScore);
    } else {
        result.score = 0;   // incomplete runs never receive an overall score
        result.status = abortStatus.empty() ? "aborted" : abortStatus;
        result.statusDetail = abortReason;
        LOGW("Benchmark ended early: %s", abortReason.c_str());
    }
    surf.destroy();
    TelemetryHub::instance().clearFrameStats();
    if (onFinished && (ok || !result.subs.empty())) onFinished(result);
    {
        std::lock_guard<std::mutex> l(m_mutex);
        m_status.state = ok ? EngineState::Finished : (result.status == "failed" ? EngineState::Failed : EngineState::Finished);
        m_status.overallProgress = ok ? 1.0 : m_status.overallProgress;
        m_status.message = !ok ? abortReason : result.fullRun ? format(T("Tamamlandı · Skor %.0f"), result.score) : std::string(T("Tamamlandı"));
    }
    CoUninitialize();
}

} // namespace aether
