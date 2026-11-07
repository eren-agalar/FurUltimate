#include "App.h"
#include "../net/Updater.h"
#include "Fonts.h"
#include "../resource.h"
#include "../core/Log.h"
#include "../core/Paths.h"
#include "../core/Strings.h"
#include "../core/I18n.h"
#include "../report/ReportExporter.h"
#include "../bench/Score.h"
#include "../net/Leaderboard.h"
#include <nlohmann/json.hpp>
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <cmath>

namespace aether {

static const wchar_t* kMainClass = L"FurUltimateMain";

const char* pageTitle(Page p) {
    switch (p) {
        case Page::Stress: return T("GPU Stres Testi");
        case Page::Benchmark: return T("Benchmark");
        case Page::Leaderboard: return T("Liderlik Tablosu");
        case Page::Results: return T("Sonuçlar");
        case Page::Hardware: return T("Donanım");
        case Page::Settings: return T("Ayarlar");
        default: return "";
    }
}
Icon pageIcon(Page p) {
    switch (p) {
        case Page::Stress: return Icon::Stress;
        case Page::Benchmark: return Icon::Benchmark;
        case Page::Leaderboard: return Icon::Trophy;
        case Page::Results: return Icon::Results;
        case Page::Hardware: return Icon::Hardware;
        default: return Icon::Settings;
    }
}
int columnsFor(float width, int maxCols) {
    int c = width > 1500 ? 4 : width > 1120 ? 3 : width > 760 ? 2 : 1;
    return std::min(c, maxCols);
}
std::string naStr(float v, const char* fmt) { return std::isnan(v) ? std::string("N/A") : format(fmt, v); }
std::string naStr(double v, const char* fmt) { return std::isnan(v) ? std::string("N/A") : format(fmt, v); }

// ------------------------------------------------------------------ shared page helpers
float pageHeader(App& app, Rect& area, const std::string& title, const std::string& subtitle) {
    const Theme& t = Theme::current();
    Rect h = area.cutTop(78);
    app.ui.label(title, {h.x, h.y + 6, h.w - 300, 38}, t.text, type::heading, Weight::SemiBold, Align::Leading, true);
    app.ui.label(subtitle, {h.x, h.y + 44, h.w - 300, 22}, t.text2, type::body);
    area.cutTop(6);
    return 84;
}

void statTile(App& app, const Rect& r, Icon icon, const std::string& label, const std::string& value, const std::string& unit,
              const std::string& sub, const Color& accent, bool available) {
    const Theme& t = Theme::current();
    UI& ui = app.ui;
    ui.card(r);
    Rect in = r.inset(18);
    drawIcon(ui.c, icon, in.x, in.y, 16, available ? accent : t.text3, 1.5f);
    ui.caps(label, {in.x + 24, in.y - 1, in.w - 24, 18}, t.text2);
    TextStyle vs; vs.size = 30; vs.weight = Weight::SemiBold; vs.display = true; vs.valign = Align::Trailing;
    float vw = ui.c.measure(value, vs);
    Rect vr{in.x, in.y + 22, in.w, 42};
    ui.c.text(value, vr, available ? t.text : t.text3, vs);
    if (!unit.empty() && available) {
        TextStyle us; us.size = 13; us.weight = Weight::Medium; us.valign = Align::Trailing;
        ui.c.text(unit, {in.x + vw + 6, in.y + 22, in.w - vw, 38}, t.text2, us);
    }
    if (!sub.empty()) ui.label(sub, {in.x, in.y + 68, in.w, 18}, available ? t.text2 : t.text3, 12);
}

void keyValueRow(App& app, const Rect& r, const std::string& key, const std::string& value, bool detected) {
    const Theme& t = Theme::current();
    app.ui.label(key, {r.x, r.y, r.w * 0.42f, r.h}, t.text2, 13);
    bool na = !detected || value.empty() || value == "N/A";
    app.ui.label(na ? "N/A" : value, {r.x + r.w * 0.42f, r.y, r.w * 0.58f, r.h}, na ? t.text3 : t.text, 13,
                 na ? Weight::Regular : Weight::Medium);
    app.ui.c.line(r.x, r.bottom(), r.right(), r.bottom(), FG(0.04f), 1);
}

// ------------------------------------------------------------------ window plumbing
LRESULT CALLBACK App::wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    App* self = (App*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        self = (App*)((CREATESTRUCTW*)lp)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
        self->m_hwnd = h;
    }
    if (self) return self->handle(msg, wp, lp);
    return DefWindowProcW(h, msg, wp, lp);
}

LRESULT App::handle(UINT msg, WPARAM wp, LPARAM lp) {
    float s = canvas.scale();
    switch (msg) {
        case WM_MOUSEMOVE: {
            m_input.mx = GET_X_LPARAM(lp) / s; m_input.my = GET_Y_LPARAM(lp) / s;
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, m_hwnd, 0};
            TrackMouseEvent(&tme);
            m_needFrame = true;
            return 0;
        }
        case WM_MOUSELEAVE: m_input.mx = m_input.my = -1; m_needFrame = true; return 0;
        case WM_LBUTTONDOWN: SetCapture(m_hwnd); m_input.down = true; m_input.pressed = true; m_needFrame = true; return 0;
        case WM_LBUTTONUP: ReleaseCapture(); m_input.down = false; m_input.released = true; m_needFrame = true; return 0;
        case WM_MOUSEWHEEL: m_input.wheel += GET_WHEEL_DELTA_WPARAM(wp) / 120.0f; m_needFrame = true; return 0;
        case WM_KEYDOWN:
            m_input.key = (int)wp;
            m_input.shift = GetKeyState(VK_SHIFT) < 0;
            m_input.ctrl = GetKeyState(VK_CONTROL) < 0;
            m_needFrame = true;
            return 0;
        case WM_CHAR:
            if (wp >= 32 || wp == 8 || wp == 13) m_input.chars.push_back((wchar_t)wp);
            m_needFrame = true;
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) { SetCursor(LoadCursorW(nullptr, ui.wantsHandCursor() ? IDC_HAND : IDC_ARROW)); return TRUE; }
            break;
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED) { canvas.resize(LOWORD(lp), HIWORD(lp)); frame(); }
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mm = (MINMAXINFO*)lp;
            mm->ptMinTrackSize.x = (LONG)(1040 * s);
            mm->ptMinTrackSize.y = (LONG)(680 * s);
            return 0;
        }
        case WM_DPICHANGED: {
            canvas.setDpi((float)HIWORD(wp));
            RECT* r = (RECT*)lp;
            SetWindowPos(m_hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            m_needFrame = true;
            return 0;
        }
        case WM_APP + 1: m_needFrame = true; return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(m_hwnd, &ps); EndPaint(m_hwnd, &ps); m_needFrame = true; return 0; }
        case WM_CLOSE:
            m_running = false;
            return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

void App::detectHardwareAsync() {
    m_hwThread = std::thread([this] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        SystemHardware detected = HardwareDetector::detect();
        hw = std::move(detected);
        if (const GpuHardware* g = gpu()) {
            if (settings.monitorOnStartup || true) {
                TelemetryHub::instance().setInterval(settings.refreshMs);
                TelemetryHub::instance().start(g->adapter.luid, g->adapter.vendorId, g->adapter.deviceId, g->adapter.subSysId,
                                               g->adapter.dedicatedVideoMemory);
            }
            std::string arch = TelemetryHub::instance().vendorArchitecture();
            if (!arch.empty()) { hw.gpus[hw.primaryGpu].architecture = arch; hw.gpus[hw.primaryGpu].architectureInferred = false; }
            std::string mem = TelemetryHub::instance().vendorMemoryType();
            if (!mem.empty()) hw.gpus[hw.primaryGpu].memoryType = mem;
        }
        hwReady.store(true, std::memory_order_release);
        PostMessageW(m_hwnd, WM_APP + 1, 0, 0);
        CoUninitialize();
    });
}

void App::applySettings() {
    Theme::select(settings.theme);
    if (m_hwnd) {   // keep the native title bar in the theme's colours
        const Theme& t = Theme::current();
        BOOL dark = t.light ? FALSE : TRUE;
        DwmSetWindowAttribute(m_hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
        COLORREF cap = RGB((int)(t.bg.r * 255), (int)(t.bg.g * 255), (int)(t.bg.b * 255));
        COLORREF txt = RGB((int)(t.text2.r * 255), (int)(t.text2.g * 255), (int)(t.text2.b * 255));
        DwmSetWindowAttribute(m_hwnd, 35 /*DWMWA_CAPTION_COLOR*/, &cap, sizeof(cap));
        DwmSetWindowAttribute(m_hwnd, 36 /*DWMWA_TEXT_COLOR*/, &txt, sizeof(txt));
    }
    I18n::setLanguage(settings.language);
    TelemetryHub::instance().setInterval(settings.refreshMs);
}

void App::navigate(Page p) {
    if (p == page) return;
    prevPage = page;
    page = p;
    pageT = 0;
}

void App::refreshResults() {
    std::string kind = resultFilter == 1 ? "benchmark" : resultFilter == 2 ? "stress" : "";
    results = db.list(kind, 500);
    resultsDirty = false;
}

void App::openFolder(const std::wstring& path) { ShellExecuteW(m_hwnd, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

std::wstring App::saveDialog(const wchar_t* defName, const wchar_t* filterName, const wchar_t* ext) {
    wchar_t file[MAX_PATH] = {};
    wcsncpy(file, defName, MAX_PATH - 1);
    std::wstring filter = std::wstring(filterName) + L" (*." + ext + L")" + L'\0' + L"*." + ext + L'\0' + L'\0';
    OPENFILENAMEW of{sizeof(of)};
    of.hwndOwner = m_hwnd;
    of.lpstrFilter = filter.c_str();
    of.lpstrFile = file;
    of.nMaxFile = MAX_PATH;
    of.lpstrDefExt = ext;
    of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&of)) return L"";
    return file;
}

std::string App::systemSummaryText() const {
    std::string s = "FurUltimate " AETHER_VERSION "\r\n";
    if (const GpuHardware* g = gpu()) {
        s += "GPU: " + narrow(g->adapter.name) + "\r\n";
        s += "Vendor: " + g->adapter.vendorName() + format(" (VEN_%04X DEV_%04X SUBSYS_%08X REV_%02X)\r\n", g->adapter.vendorId,
                                                           g->adapter.deviceId, g->adapter.subSysId, g->adapter.revision);
        s += "VRAM: " + formatBytes(g->adapter.dedicatedVideoMemory) + "\r\n";
        s += "Driver: " + (g->adapter.driverVersion.empty() ? std::string("N/A") : g->adapter.driverVersion) + " (" +
             (g->driverDate.empty() ? std::string("N/A") : g->driverDate) + ")\r\n";
        s += "Direct3D 11 FL: " + std::string(featureLevelName(g->adapter.maxFeatureLevel)) + ", Direct3D 12 FL: " +
             (g->d3d12FeatureLevel.empty() ? std::string("N/A") : g->d3d12FeatureLevel) + "\r\n";
    }
    s += "CPU: " + (hw.cpu.brand.empty() ? std::string("N/A") : hw.cpu.brand) + format(" (%d/%d)\r\n", hw.cpu.cores, hw.cpu.threads);
    s += "RAM: " + formatBytes(hw.memory.totalBytes) + (hw.memory.type.empty() ? "" : " " + hw.memory.type) + "\r\n";
    s += "OS: " + hw.os.name + " " + hw.os.version + " (build " + hw.os.build + ")\r\n";
    return s;
}

void App::exportResult(const BenchmarkResult& r, int fmt) {
    std::string stamp = r.timestamp;
    for (auto& ch : stamp) if (ch == ':') ch = '-';
    std::wstring base = widen(format("AETHER_%s_%s", r.kind.c_str(), stamp.c_str()));
    std::wstring path;
    std::string err;
    bool ok = false;
    if (fmt == 0) {
        path = saveDialog((base + L".pdf").c_str(), L"PDF", L"pdf");
        if (path.empty()) return;
        ReportContext ctx;
        ctx.osName = hw.os.name + " " + hw.os.version;
        ctx.cpuName = hw.cpu.brand;
        ctx.ramText = formatBytes(hw.memory.totalBytes) + (hw.memory.type.empty() ? "" : " " + hw.memory.type);
        ctx.appVersion = AETHER_VERSION;
        BenchmarkResult other;
        const BenchmarkResult* cmp = nullptr;
        if (compareResult && compareResult != r.id && db.get(compareResult, other)) cmp = &other;
        ok = ReportExporter::exportPdf(r, cmp, ctx, path, err);
    } else if (fmt == 1) {
        path = saveDialog((base + L".json").c_str(), L"JSON", L"json");
        if (path.empty()) return;
        ok = ReportExporter::exportJson(r, path, err);
    } else {
        path = saveDialog((base + L".csv").c_str(), L"CSV", L"csv");
        if (path.empty()) return;
        ok = ReportExporter::exportCsv(r, path, err);
    }
    if (ok) ui.toast(std::string(T("Dışa aktarıldı: ")) + narrow(path), ToastKind::Success);
    else ui.toast(std::string(T("Dışa aktarma başarısız: ")) + err, ToastKind::Error);
}

void App::captureWindow() {
    std::string stamp = nowIso8601();
    for (auto& ch : stamp) if (ch == ':') ch = '-';
    canvas.requestCapture(Paths::screenshots() + L"\\aether_" + widen(stamp) + L".png");
    m_needFrame = true;
}

void App::startStress() {
    const GpuHardware* g = gpu();
    if (!g) return;
    static const uint32_t resW[] = {1280, 1920, 2560, 3840}, resH[] = {720, 1080, 1440, 2160};
    static const float scales[] = {0.5f, 0.75f, 1.0f, 1.5f};
    static const int durations[] = {60, 300, 600, 1800, 3600, 0};
    StressConfig c = stressCfg;
    c.palette = overlayPalette();
    c.tempUnit = (int)settings.tempUnit;
    c.width = resW[stressResIndex]; c.height = resH[stressResIndex];
    c.renderScale = scales[stressScaleIndex];
    c.durationSec = stressDurationOverride > 0 ? stressDurationOverride : durations[stressDurIndex];
    c.fullscreen = stressModeIndex == 1;
    c.vsync = c.preset == StressPreset::Eco;
    c.warnTempC = settings.warnTempC; c.stopTempC = settings.stopTempC;
    c.tdrGuard = settings.tdrGuard;
    c.overlay = settings.showFpsOverlay; c.overlayFrameTime = settings.showFrameTimeInOverlay;
    c.adapter = g->adapter.luid;
    c.gpuName = narrow(g->adapter.name); c.gpuVendor = g->adapter.vendorName();
    c.driverVersion = g->adapter.driverVersion;
    c.vramBytes = g->adapter.dedicatedVideoMemory;
    c.monitor = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
    if (bench.busy()) { ui.toast(T("Benchmark çalışırken stres testi başlatılamaz"), ToastKind::Warning); return; }
    if (stress.start(c)) ui.toast(T("Stres testi başlatıldı — Esc ile anında durdurabilirsiniz"), ToastKind::Info);
}

void App::startBenchmark() {
    const GpuHardware* g = gpu();
    if (!g) return;
    if (stress.busy()) { ui.toast(T("Stres testi çalışırken benchmark başlatılamaz"), ToastKind::Warning); return; }
    BenchConfig c = benchCfg;
    c.palette = overlayPalette();
    c.testMask = pendingBenchMask ? pendingBenchMask : 0x3F;
    c.adapter = g->adapter.luid;
    c.gpuName = narrow(g->adapter.name); c.gpuVendor = g->adapter.vendorName();
    c.driverVersion = g->adapter.driverVersion;
    c.vramBytes = g->adapter.dedicatedVideoMemory;
    c.vendorId = g->adapter.vendorId;
    c.stopTempC = settings.stopTempC;
    c.overlay = settings.showFpsOverlay;
    c.monitor = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
    haveLastBench = false;
    if (bench.start(c)) ui.toast(T("Benchmark başlatıldı — Esc ile iptal edebilirsiniz"), ToastKind::Info);
}

OverlayPalette App::overlayPalette() const {
    const Theme& t = Theme::current();
    auto rgb = [](const Color& c) { return ((uint32_t)(c.r * 255) << 16) | ((uint32_t)(c.g * 255) << 8) | (uint32_t)(c.b * 255); };
    OverlayPalette p;
    p.panel = rgb(t.light ? t.surface : t.bg); p.text = rgb(t.text); p.text2 = rgb(t.text2); p.text3 = rgb(t.text3);
    p.accent = rgb(t.perf); p.warn = rgb(t.warning); p.crit = rgb(t.critical); p.success = rgb(t.success);
    p.panelAlpha = t.light ? 0.90f : 0.80f;
    return p;
}

std::string App::lbBoardId() const {
    static const char* presets[] = {"STANDARD-1080P", "HIGH-1440P", "ULTRA-2160P"};
    if (lbBoard < 3) return overallBoard(presets[lbBoard]);
    // AI board: the path this GPU produced last (DirectML FP16 by default).
    for (auto& r : results) if (r.aiScore > 0 && !r.aiPath.empty()) return aiBoard(r.aiPath);
    return aiBoard("DirectML FP16");
}

void App::refreshLeaderboard(bool force) {
    auto& lb = LeaderboardClient::instance();
    if (!lb.configured()) return;
    double now = (double)GetTickCount64() / 1000.0;
    if (!force && now - lbLastRefresh < 60) return;
    lbLastRefresh = now;
    lb.refresh(lbBoardId(), settings.leaderboardEnabled ? settings.playerId : std::string());
}

static bool isQuickRun(const BenchmarkResult& r) { return r.configKey.find("|QUICK") != std::string::npos; }

void App::submitLeaderboard(const BenchmarkResult& r) {
    auto& lb = LeaderboardClient::instance();
    if (!lb.configured() || !settings.leaderboardEnabled || settings.nickname.size() < 2 || isQuickRun(r)) return;
    // Personal-best rule: compare with the best earlier result of the same board and GPU in the local history.
    auto previousBest = [&](bool ai) {
        double best = 0;
        for (auto& o : db.list("benchmark", 2000)) {
            if (o.id == r.id || o.status != "completed" || o.gpuName != r.gpuName) continue;
            if (ai) { if (o.aiScore > 0 && o.aiConfigKey == r.aiConfigKey) best = std::max(best, o.aiScore); }
            else if (o.fullRun && o.configKey == r.configKey) best = std::max(best, o.score);
        }
        return best;
    };
    bool fullBoard = r.fullRun && r.score > 0 && r.configKey.size() > 5 && r.configKey.substr(r.configKey.size() - 5) == "|FULL" &&
                     r.preset.rfind("VALIDATION", 0) != 0;
    if (fullBoard && r.score > previousBest(false)) {
        SubmitRequest q;
        q.playerId = settings.playerId; q.nickname = settings.nickname; q.board = overallBoard(r.preset);
        q.gpu = r.gpuName; q.vendor = r.gpuVendor; q.driver = r.driverVersion;
        q.benchVersion = r.benchVersion; q.appVersion = AETHER_VERSION; q.score = r.score;
        nlohmann::json d;
        for (auto& s : r.subs) if (s.id != "ai") d[s.id] = s.score;
        q.detailsJson = d.dump();
        lb.submit(q);
    }
    if (r.aiScore > 0 && r.preset.rfind("VALIDATION", 0) != 0 && r.aiScore > previousBest(true)) {
        SubmitRequest q;
        q.playerId = settings.playerId; q.nickname = settings.nickname; q.board = aiBoard(r.aiPath);
        q.gpu = r.gpuName; q.vendor = r.gpuVendor; q.driver = r.driverVersion;
        q.benchVersion = score::kAiVersion; q.appVersion = AETHER_VERSION; q.score = r.aiScore; q.tops = r.aiTops;
        nlohmann::json d = {{"tops", r.aiTops}, {"theoreticalTops", r.aiTheoreticalTops}, {"path", r.aiPath}};
        q.detailsJson = d.dump();
        lb.submit(q);
    }
}

void App::maybeSubmitLeaderboard(const BenchmarkResult& r) {
    if (!LeaderboardClient::instance().configured()) return;
    if (isQuickRun(r)) {   // quick-mode runs never reach the online leaderboard
        if (settings.leaderboardEnabled) ui.toast(T("Hızlı mod kullanıldı · skor liderlik tablosuna gönderilmedi"), ToastKind::Info);
        return;
    }
    bool eligible = (r.fullRun && r.score > 0) || r.aiScore > 0;
    if (!eligible || r.preset.rfind("VALIDATION", 0) == 0) return;
    if (settings.leaderboardEnabled) { submitLeaderboard(r); return; }
    if (!settings.leaderboardAsked && modal == Modal::None) {   // ask once, after the first eligible result
        lbPending = r; lbHavePending = true;
        lbNickDraft = settings.nickname;
        openModal(Modal::LeaderboardJoin);
    }
}

void App::pollEngines() {
    std::deque<BenchmarkResult> pending;
    {
        std::lock_guard<std::mutex> l(m_pendingMutex);
        pending.swap(m_pending);
    }
    for (auto& r : pending) {
        r.id = db.insert(r);
        if (r.kind == "benchmark") { lastBenchResult = r; haveLastBench = true; }
        selectedResult = r.id;
        resultsDirty = true;
        if (r.status == "completed") {
            std::string msg = r.kind != "benchmark" ? std::string(T("Stres testi tamamlandı"))
                            : r.fullRun ? format(T("Benchmark tamamlandı · Skor %.0f"), r.score) : std::string(T("Test tamamlandı"));
            if (r.aiScore > 0) msg += format(" · %s %.0f (%.1f TOPS)", T("AI skoru"), r.aiScore, r.aiTops);
            ui.toast(msg, ToastKind::Success);
            if (r.kind == "benchmark") maybeSubmitLeaderboard(r);
        }
        else
            ui.toast(std::string(r.kind == "benchmark" ? T("Benchmark durduruldu: ") : T("Stres testi durduruldu: ")) + r.statusDetail,
                     r.status == "failed" ? ToastKind::Error : ToastKind::Warning);
    }
    for (auto& s : LeaderboardClient::instance().takeSubmitResults()) {
        if (s.ok && (s.status == "improved" || s.status == "inserted")) {
            ui.toast(format(T("Yeni kişisel rekor! Liderlik tablosu güncellendi · Sıra #%d"), s.rank), ToastKind::Success);
            refreshLeaderboard(true);
        } else if (!s.ok) {
            ui.toast(std::string(T("Liderlik tablosuna gönderilemedi: ")) + s.error, ToastKind::Warning);
        }
    }
    bool busy = stress.busy() || bench.busy();
    if (m_wasBusy && !busy) {
        StressStatus ss = stress.status();
        BenchStatus bs = bench.status();
        if (ss.state == EngineState::Failed && pending.empty()) ui.toast(std::string(T("Stres testi başarısız: ")) + ss.message, ToastKind::Error);
        if (bs.state == EngineState::Failed && pending.empty()) ui.toast(std::string(T("Benchmark başarısız: ")) + bs.message, ToastKind::Error);
        SetForegroundWindow(m_hwnd);
    }
    m_wasBusy = busy;
}

// ------------------------------------------------------------------ drawing
void App::drawBackground() {
    const Theme& t = Theme::current();
    Canvas& c = canvas;
    c.clear(t.bg);
    float w = c.width(), h = c.height();
    c.fillRadial(w * 0.82f, -h * 0.05f, w * 0.55f, h * 0.55f, t.perf.alpha(0.055f), t.perf.alpha(0.0f));
    c.fillRadial(w * 0.18f, h * 1.05f, w * 0.45f, h * 0.40f, FG(0.025f), FG(0));
}

void App::drawSidebar(const Rect& r0) {
    const Theme& t = Theme::current();
    Canvas& c = canvas;
    Rect r = r0;
    c.fillRect(r, t.sidebar.alpha(0.92f));
    c.line(r.right() - 0.5f, r.y, r.right() - 0.5f, r.bottom(), t.border, 1);
    // Brand
    Rect brand = r.cutTop(92);
    drawLogoMark(c, brand.x + 38, brand.y + 46, 16, ui.time() * (settings.reduceMotion ? 0.0f : 0.25f));
    TextStyle w1; w1.size = 22; w1.weight = Weight::SemiBold; w1.display = true; w1.tracking = 2.0f;
    c.text("FUR", {brand.x + 66, brand.y + 24, 160, 26}, t.text, w1);
    TextStyle w2; w2.size = 10; w2.weight = Weight::SemiBold; w2.tracking = 3.4f;
    c.text("ULTIMATE", {brand.x + 67, brand.y + 50, 150, 14}, Color::hex(0xFF6A3D), w2);
    c.fillRoundGradientH({brand.x + 67, brand.y + 67, 92, 2.5f}, 1.25f, Color::hex(0xFFC24A), Color::hex(0x8B5CFF));
    if (!settings.reduceMotion) ui.anim(ui.id("logo-spin"), std::fmod(ui.time(), 2.0f) < 1 ? 1.0f : 0.0f, 0.5f);

    r.cutTop(8);
    Rect nav = r.inset(12, 0);
    float itemH = 40, gap = 4;
    float target = (float)(int)page * (itemH + gap);
    m_navIndicator = ui.anim(ui.id("nav-ind"), target, 16);
    // active pill
    Rect pill{nav.x, nav.y + m_navIndicator, nav.w, itemH};
    c.fillRound(pill, 10, t.surface);
    c.fillRoundGradient(pill, 10, FG(0.05f), FG(0.0f));
    c.strokeRound(pill, 10, t.border, 1);
    c.fillRound({pill.x + 1, pill.y + 11, 2.5f, itemH - 22}, 1.25f, t.perf);
    for (int i = 0; i < (int)Page::Count; ++i) {
        Rect it{nav.x, nav.y + i * (itemH + gap), nav.w, itemH};
        std::string key = format("nav%d", i);
        uint64_t k = ui.id(key);
        if (ui.pressedOn(k, it)) navigate((Page)i);
        float h = ui.anim(k ^ 0x1, ui.hovered(it) ? 1.0f : 0.0f, 18);
        bool active = (int)page == i;
        if (!active && h > 0.01f) c.fillRound(it, 10, FG(0.03f * h));
        Color fg = active ? t.text : mix(t.text2, t.text, h * 0.7f);
        drawIcon(c, pageIcon((Page)i), it.x + 14, it.cy() - 9, 18, active ? t.perf : fg, 1.5f);
        TextStyle st; st.size = 13.5f; st.weight = active ? Weight::SemiBold : Weight::Medium;
        c.text(pageTitle((Page)i), {it.x + 44, it.y, it.w - 50, it.h}, fg, st);
        ui.focusRing(it, 10, k);
        if (i == (int)Page::Stress && stress.busy()) c.fillCircle(it.right() - 16, it.cy(), 3.5f, t.warning);
        if (i == (int)Page::Benchmark && bench.busy()) c.fillCircle(it.right() - 16, it.cy(), 3.5f, t.perf);
    }
    // Update available pill (above the GPU footer)
    {
        UpdateStatus us = Updater::instance().status();
        bool show = !us.release.version.empty() && isNewerVersion(us.release.version, AETHER_VERSION) &&
                    us.state != UpdateState::Checking && us.state != UpdateState::UpToDate;
        if (show) {
            Rect pr = r0;
            pr = pr.cutBottom(118 + 52).cutTop(52).inset(12, 6);
            uint64_t k = ui.id("upd-pill");
            float h = ui.anim(k ^ 0x1, ui.hovered(pr) ? 1.0f : 0.0f, 18);
            c.fillRoundGradientH(pr, 12, Color::hex(0xFF5A2E, 0.16f + 0.08f * h), Color::hex(0x8B5CFF, 0.16f + 0.08f * h));
            c.strokeRound(pr, 12, Color::hex(0xFF6A3D, 0.55f), 1);
            drawIcon(c, Icon::Refresh, pr.x + 12, pr.cy() - 8, 16, Color::hex(0xFF8A4A), 1.6f);
            TextStyle st; st.size = 12.5f; st.weight = Weight::SemiBold;
            c.text(format(T("Güncelleme var · %s"), us.release.version.c_str()), {pr.x + 36, pr.y, pr.w - 40, pr.h}, t.text, st);
            if (ui.pressedOn(k, pr)) openModal(Modal::Update);
        }
    }
    // Footer: live GPU status
    Rect foot = r0;
    foot = foot.cutBottom(118).inset(12, 10);
    c.fillRound(foot, 12, FG(0.025f));
    c.strokeRound(foot, 12, t.border, 1);
    Rect f = foot.inset(14, 12);
    if (hwReady && gpu()) {
        TextStyle st; st.size = 12; st.weight = Weight::SemiBold;
        c.text(narrow(gpu()->adapter.name), {f.x, f.y, f.w, 18}, t.text, st);
        TextStyle sm; sm.size = 11.5f;
        float temp = tel.gpuTemp, load = tel.gpuLoad;
        c.text(std::string(T("Sıcaklık")), {f.x, f.y + 26, f.w / 2, 16}, t.text3, sm);
        c.text(std::string(T("Yük")), {f.x + f.w / 2, f.y + 26, f.w / 2, 16}, t.text3, sm);
        TextStyle v; v.size = 17; v.weight = Weight::SemiBold; v.display = true;
        Color tc = std::isnan(temp) ? t.text3 : (temp >= settings.warnTempC ? t.warning : t.text);
        c.text(fmtTemp(temp, (int)settings.tempUnit), {f.x, f.y + 42, f.w / 2, 24}, tc, v);
        c.text(naStr(load, "%.0f %%"), {f.x + f.w / 2, f.y + 42, f.w / 2, 24}, std::isnan(load) ? t.text3 : t.text, v);
    } else {
        TextStyle sm; sm.size = 12;
        c.text(T("Donanım algılanıyor…"), {f.x, f.y, f.w, 18}, t.text2, sm);
        ui.progressBar({f.x, f.y + 34, f.w, 4}, std::fmod(ui.time() * 0.5f, 1.0f), t.perf);
    }
}

void App::drawModal() {
    const Theme& t = Theme::current();
    modalT = ui.anim(ui.id("modalT"), modal != Modal::None ? 1.0f : 0.0f, 16);
    if (modal == Modal::None) return;
    if (modal == Modal::Update) { drawUpdateModal(); return; }
    Canvas& c = canvas;
    c.fillRect({0, 0, c.width(), c.height()}, Color(0, 0, 0, 0.55f * modalT));
    float w = 540, h = modal == Modal::About ? 340 : modal == Modal::LeaderboardJoin ? 360 : 300;
    Rect card{(c.width() - w) / 2, (c.height() - h) / 2 + (1 - modalT) * 16, w, h};
    c.pushOpacity(modalT);
    c.fillRound(card.offset(0, 10), 18, Color(0, 0, 0, 0.45f));
    ui.card(card, 18);
    Rect in = card.inset(28);
    std::string title, body;
    Icon icon = Icon::Warning;
    Color ic = t.warning;
    std::string okLabel = T("Devam et");
    ButtonStyle okStyle = ButtonStyle::Primary;
    switch (modal) {
        case Modal::ConfirmStress:
            title = T("Yoğun GPU yükü başlatılsın mı?");
            body = format(T("Stres testi GPU'yu uzun süre yüksek güç ve sıcaklıkta çalıştırır. Uyarı eşiği %d °C, acil durdurma eşiği %d °C olarak ayarlı. Test penceresinde Esc tuşu testi anında durdurur. Yetersiz soğutma, aşırı hız aşırtma (overclock) veya zayıf güç kaynağı olan sistemlerde testi gözetimsiz çalıştırmayın."),
                          settings.warnTempC, settings.stopTempC);
            okLabel = T("Stres testini başlat");
            break;
        case Modal::ConfirmBenchmark:
            title = T("Benchmark başlatılsın mı?");
            body = pendingBenchMask == 0x3F
                       ? T("Benchmark birkaç dakika boyunca altı testi sırayla çalıştırır ve GPU'yu tam yükte kullanır. Doğru sonuç için diğer uygulamaları kapatın ve test penceresini öne alın. Esc tuşu çalıştırmayı iptal eder.")
                       : T("Seçilen test GPU'yu tam yükte çalıştırır. Tek test çalıştırmaları alt skor üretir; genel skor yalnızca tüm testlerle hesaplanır. Esc tuşu çalıştırmayı iptal eder.");
            okLabel = T("Benchmark'ı başlat");
            icon = Icon::Benchmark; ic = t.perf;
            break;
        case Modal::ConfirmClearHistory:
            title = T("Tüm sonuç geçmişi silinsin mi?");
            body = T("Yerel veritabanındaki tüm benchmark ve stres testi kayıtları kalıcı olarak silinir. Bu işlem geri alınamaz. Dışa aktarılmış PDF/JSON/CSV dosyaları etkilenmez.");
            okLabel = T("Geçmişi sil"); okStyle = ButtonStyle::Danger; icon = Icon::Trash; ic = t.critical;
            break;
        case Modal::ConfirmDelete:
            title = T("Bu sonuç silinsin mi?");
            body = T("Seçili kayıt yerel veritabanından kalıcı olarak silinir. Bu işlem geri alınamaz.");
            okLabel = T("Sil"); okStyle = ButtonStyle::Danger; icon = Icon::Trash; ic = t.critical;
            break;
        case Modal::LeaderboardJoin:
            title = T("Liderlik tablosuna katılmak ister misiniz?");
            body = T("Benchmark skorlarınız, yalnızca kişisel rekorunuzu geçtiğinde herkese açık liderlik tablosuna gönderilir. Gönderilen veriler: takma ad, ekran kartı modeli, sürücü sürümü ve skorlar. Kişisel bilgi gönderilmez. Bu tercihi Ayarlar'dan istediğiniz zaman değiştirebilirsiniz.");
            okLabel = T("Katıl"); icon = Icon::Trophy; ic = t.perf;
            break;
        case Modal::About:
            title = "FurUltimate";
            body = std::string(T("Sürüm ")) + AETHER_VERSION + " · " + score::kBenchmarkVersion + "\n\n" +
                   T("Power. Precision. Performance.") + "\n\n" +
                   T("Üçüncü taraf bileşenler: SQLite (kamu malı), nlohmann/json (MIT), miniz (MIT), DirectML başlığı (MIT), Inter yazı tipi (SIL OFL 1.1). Ayrıntılar THIRD_PARTY_NOTICES.txt dosyasındadır.");
            okLabel = T("Kapat"); icon = Icon::Info; ic = t.perf;
            break;
        default: break;
    }
    c.fillRound({in.x, in.y, 40, 40}, 12, ic.alpha(0.12f));
    drawIcon(c, icon, in.x + 10, in.y + 10, 20, ic, 1.6f);
    ui.label(title, {in.x + 56, in.y, in.w - 56, 40}, t.text, 18, Weight::SemiBold);
    TextStyle bs; bs.size = 13.5f; bs.wrap = true; bs.valign = Align::Leading;
    c.text(body, {in.x, in.y + 60, in.w, in.h - 120}, t.text2, bs);
    Rect btns = in.cutBottom(40);
    bool ok = false, cancel = false;
    bool enterJoin = false;
    if (modal == Modal::LeaderboardJoin) {
        Rect nr = in.cutBottom(98);
        ui.label(T("Takma ad (2–20 karakter)"), {nr.x, nr.y + 10, nr.w, 18}, t.text2, 12);
        enterJoin = ui.textField("lb-nick", {nr.x, nr.y + 32, nr.w, 40}, lbNickDraft, 20, T("örn. NovaRider"));
    }
    bool nickOk = trim(lbNickDraft).size() >= 2;
    if (modal == Modal::About) {
        ok = ui.button("modal-ok", okLabel, btns.cutRight(140), ButtonStyle::Secondary);
    } else {
        bool okEnabled = modal != Modal::LeaderboardJoin || nickOk;
        ok = ui.button("modal-ok", okLabel, btns.cutRight(std::max(170.0f, c.measure(okLabel, TextStyle{13}) + 48)), okStyle, okEnabled);
        if (enterJoin && nickOk) ok = true;
        btns.cutRight(10);
        cancel = ui.button("modal-cancel", modal == Modal::LeaderboardJoin ? T("Şimdi değil") : T("Vazgeç"), btns.cutRight(130), ButtonStyle::Ghost);
    }
    c.popOpacity();
    if (ui.input().key == VK_ESCAPE && !ui.textFieldFocused()) cancel = true;
    if (ok) {
        Modal m = modal;
        modal = Modal::None;
        if (m == Modal::LeaderboardJoin) {
            settings.nickname = trim(lbNickDraft);
            settings.leaderboardEnabled = true;
            settings.leaderboardAsked = true;
            markSettingsChanged();
            if (lbHavePending) { submitLeaderboard(lbPending); lbHavePending = false; }
            ui.toast(T("Liderlik tablosuna katıldınız"), ToastKind::Success);
        }
        if (m == Modal::ConfirmStress) startStress();
        else if (m == Modal::ConfirmBenchmark) startBenchmark();
        else if (m == Modal::ConfirmClearHistory) { db.clear(); resultsDirty = true; selectedResult = compareResult = 0; ui.toast(T("Geçmiş temizlendi"), ToastKind::Success); }
        else if (m == Modal::ConfirmDelete) {
            if (db.remove(pendingDelete)) ui.toast(T("Sonuç silindi"), ToastKind::Success);
            if (selectedResult == pendingDelete) selectedResult = 0;
            if (compareResult == pendingDelete) compareResult = 0;
            resultsDirty = true;
        }
    } else if (cancel) {
        if (modal == Modal::LeaderboardJoin) { settings.leaderboardAsked = true; lbHavePending = false; markSettingsChanged(); }
        modal = Modal::None;
    }
}

void App::checkForUpdates(bool manual) {
    if (!manual) {
        int64_t now = (int64_t)time(nullptr);
        if (now - settings.lastUpdateCheck < 3600) return;   // automatic checks: at most once per hour
        settings.lastUpdateCheck = now;
        markSettingsChanged();
    }
    Updater::instance().check(manual);
}

void App::pollUpdates() {
    UpdateStatus us = Updater::instance().status();
    int st = (int)us.state;
    bool changed = st != updateSeenState;
    updateSeenState = st;
    bool busy = stress.busy() || bench.busy();
    switch (us.state) {
        case UpdateState::Available:
            // Offer each new release once per session; automatic checks respect "skip this version".
            if (updatePromptedVersion != us.release.version && modal == Modal::None && !busy && m_autorun.empty() &&
                (us.manualCheck || us.release.version != settings.skippedUpdate)) {
                updatePromptedVersion = us.release.version;
                openModal(Modal::Update);
            }
            break;
        case UpdateState::UpToDate:
            if (changed && us.manualCheck) ui.toast(format(T("FurUltimate güncel · sürüm %s"), AETHER_VERSION), ToastKind::Success);
            break;
        case UpdateState::Error:
            if (changed && modal != Modal::Update && (us.manualCheck || !us.release.version.empty()))
                ui.toast(std::string(T("Güncelleme: ")) + us.error, ToastKind::Warning);
            break;
        case UpdateState::RestartPending:
            if (changed) {
                LOGI("Closing for update");
                ShowWindow(m_hwnd, SW_HIDE);   // disappear at once; shutdown (thread joins) finishes in the background
                SettingsStore::instance().set(settings);
                SettingsStore::instance().save();
                settingsDirty = false;
                PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
            }
            break;
        default: break;
    }
}

void App::drawUpdateModal() {
    const Theme& t = Theme::current();
    Canvas& c = canvas;
    Updater& up = Updater::instance();
    UpdateStatus us = up.status();
    const ReleaseInfo& rel = us.release;
    c.fillRect({0, 0, c.width(), c.height()}, Color(0, 0, 0, 0.55f * modalT));
    float w = 580, h = 460;
    Rect card{(c.width() - w) / 2, (c.height() - h) / 2 + (1 - modalT) * 16, w, h};
    c.pushOpacity(modalT);
    c.fillRound(card.offset(0, 10), 18, Color(0, 0, 0, 0.45f));
    ui.card(card, 18);
    Rect in = card.inset(28);
    // Header: brand mark + title
    drawLogoMark(c, in.x + 20, in.y + 20, 15, ui.time() * (settings.reduceMotion ? 0.0f : 0.25f));
    ui.label(rel.version.empty() ? std::string(T("Güncelleme")) : format(T("Yeni sürüm: FurUltimate %s"), rel.version.c_str()),
             {in.x + 56, in.y - 2, in.w - 56, 26}, t.text, 18, Weight::SemiBold);
    bool installed = Updater::isInstalledCopy();
    ui.label(format(T("Yüklü sürüm %s · %s"), AETHER_VERSION, installed ? T("kurulu kopya") : T("taşınabilir / tek exe")),
             {in.x + 56, in.y + 24, in.w - 56, 18}, t.text3, 12);
    in.cutTop(62);
    // Release notes
    ui.caps(T("Yenilikler"), in.cutTop(18), t.text2);
    in.cutTop(6);
    Rect notes = in.cutTop(180);
    c.fillRound(notes, 12, FG(0.03f));
    c.strokeRound(notes, 12, t.border, 1);
    std::string text = releaseNotesForDisplay(rel.notes, settings.language, 560);
    if (text.empty()) text = T("Bu sürüm için not yok.");
    TextStyle ns; ns.size = 12.5f; ns.wrap = true; ns.valign = Align::Leading;
    c.text(text, notes.inset(16, 12), t.text2, ns);
    in.cutTop(16);

    Rect btns = in.cutBottom(40);
    Rect info = in;
    bool close = ui.input().key == VK_ESCAPE;
    switch (us.state) {
        case UpdateState::Available: {
            TextStyle hs; hs.size = 12; hs.wrap = true; hs.valign = Align::Leading;
            c.text(installed ? T("Kurulum için Windows yönetici izni isteyecek. Uygulama kapanır, güncellenir ve kendiliğinden yeniden açılır. Sonuçlarınız ve ayarlarınız korunur.")
                             : T("Uygulama dosyası doğrulanmış yeni sürümle değiştirilir ve yeniden başlatılır. Sonuçlarınız ve ayarlarınız korunur."),
                   {info.x, info.y, info.w, 40}, t.text3, hs);
            bool busy = stress.busy() || bench.busy();
            if (ui.button("upd-now", T("Şimdi güncelle"), btns.cutRight(170), ButtonStyle::Primary, !busy, (int)Icon::Refresh)) up.install();
            btns.cutRight(10);
            if (ui.button("upd-later", T("Daha sonra"), btns.cutRight(120), ButtonStyle::Ghost)) close = true;
            if (ui.button("upd-skip", T("Bu sürümü atla"), btns.cutLeft(150), ButtonStyle::Ghost)) {
                settings.skippedUpdate = rel.version;
                markSettingsChanged();
                close = true;
            }
            if (busy) ui.label(T("Test çalışırken güncellenemez"), {info.x, info.y + 44, info.w, 16}, t.warning, 12);
            break;
        }
        case UpdateState::Downloading: {
            float frac = us.total > 0 ? float(double(us.received) / double(us.total)) : 0.0f;
            ui.label(T("İndiriliyor…"), {info.x, info.y, info.w / 2, 18}, t.text, 13, Weight::SemiBold);
            ui.label(us.total > 0 ? format("%.1f / %.1f MB · %.0f %%", us.received / 1048576.0, us.total / 1048576.0, frac * 100)
                                  : format("%.1f MB", us.received / 1048576.0),
                     {info.x + info.w / 2, info.y, info.w / 2, 18}, t.text2, 12.5f, Weight::Medium, Align::Trailing);
            ui.progressBar({info.x, info.y + 28, info.w, 6}, us.total > 0 ? frac : std::fmod(ui.time() * 0.5f, 1.0f), Color::hex(0xFF6A3D));
            if (ui.button("upd-cancel", T("İptal"), btns.cutRight(120), ButtonStyle::Ghost)) up.cancel();
            close = false;
            break;
        }
        case UpdateState::Verifying:
        case UpdateState::Installing:
        case UpdateState::RestartPending:
            ui.label(us.state == UpdateState::Verifying ? T("SHA-256 doğrulanıyor…")
                                                        : installed ? T("Kurulum başlatılıyor… onay penceresine izin verin") : T("Yeniden başlatılıyor…"),
                     {info.x, info.y, info.w, 18}, t.text, 13, Weight::SemiBold);
            ui.progressBar({info.x, info.y + 28, info.w, 6}, std::fmod(ui.time() * 0.5f, 1.0f), Color::hex(0xFF6A3D));
            close = false;
            break;
        case UpdateState::Error: {
            TextStyle es; es.size = 12.5f; es.wrap = true; es.valign = Align::Leading;
            c.text(us.error, {info.x, info.y, info.w, 40}, t.warning, es);
            if (!rel.version.empty() && ui.button("upd-retry", T("Tekrar dene"), btns.cutRight(140), ButtonStyle::Primary)) up.backToAvailable();
            btns.cutRight(10);
            if (!rel.htmlUrl.empty() && ui.button("upd-page", T("Sürüm sayfası"), btns.cutRight(140), ButtonStyle::Secondary, true, (int)Icon::Globe))
                ShellExecuteW(m_hwnd, L"open", widen(rel.htmlUrl).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            if (ui.button("upd-close", T("Kapat"), btns.cutLeft(110), ButtonStyle::Ghost)) close = true;
            break;
        }
        default:
            if (ui.button("upd-close2", T("Kapat"), btns.cutRight(110), ButtonStyle::Ghost)) close = true;
            break;
    }
    c.popOpacity();
    if (close) modal = Modal::None;
}

static std::string argValue(const std::wstring& cmd, const wchar_t* key) {
    size_t p = cmd.find(key);
    if (p == std::wstring::npos) return {};
    p += wcslen(key);
    size_t e = cmd.find(L' ', p);
    return narrow(cmd.substr(p, e == std::wstring::npos ? std::wstring::npos : e - p));
}

void App::parseCommandLine() {
    std::wstring cmd = GetCommandLineW();
    std::string v;
    if (!(v = argValue(cmd, L"--page=")).empty()) page = (Page)std::clamp(atoi(v.c_str()), 0, (int)Page::Count - 1);
    m_autorun = argValue(cmd, L"--autorun=");
    if (!(v = argValue(cmd, L"--duration=")).empty()) m_autoDuration = std::max(1, atoi(v.c_str()));
    if (!(v = argValue(cmd, L"--capture-after=")).empty()) m_captureAfter = (float)atof(v.c_str());
    m_exitAfterRun = cmd.find(L"--exit-after-run") != std::wstring::npos;
    if (!(v = argValue(cmd, L"--tests=")).empty()) {   // e.g. --tests=graphics,ai
        uint32_t mask = 0;
        const BenchTestInfo* tests = benchTests();
        for (int i = 0; i < kBenchTestCount; ++i)
            if (("," + v + ",").find("," + std::string(tests[i].id) + ",") != std::string::npos) mask |= 1u << i;
        if (mask) m_autoMask = mask;
    }
    if (!m_autorun.empty()) LOGI("Automation: autorun=%s duration=%d exit=%d", m_autorun.c_str(), m_autoDuration, (int)m_exitAfterRun);
}

void App::automationTick(float dt) {
    m_autoClock += dt;
    if (m_captureAfter >= 0 && m_autoClock >= m_captureAfter) { m_captureAfter = -1; captureWindow(); }
    if (m_autorun.empty() || !hwReady) return;
    if (!m_autoStarted) {
        m_autoStarted = true;
        if (m_autorun.rfind("stress", 0) == 0) {
            stressCfg.preset = m_autorun == "stress-eco" ? StressPreset::Eco : m_autorun == "stress-extreme" ? StressPreset::Extreme : StressPreset::Balanced;
            stressDurIndex = 1;
            navigate(Page::Stress);
            stressDurationOverride = m_autoDuration;
            startStress();
            stressDurationOverride = -1;
        } else if (m_autorun.rfind("bench", 0) == 0) {
            benchCfg.quick = m_autorun == "bench-quick";
            benchCfg.validation = m_autorun == "bench-validation";
            pendingBenchMask = m_autoMask;
            navigate(Page::Benchmark);
            startBenchmark();
        }
        return;
    }
    bool busy = stress.busy() || bench.busy();
    if (busy) m_autoSawBusy = true;
    if (m_autoSawBusy && !busy && m_exitAfterRun) {
        m_exitAfterRun = false;
        m_autorun.clear();
        LOGI("Automation run finished; exiting");
        PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
    }
}

void App::frame() {
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    float dt = m_last.QuadPart ? float(double(now.QuadPart - m_last.QuadPart) / double(m_freq.QuadPart)) : 0.016f;
    m_last = now;

    if (hwReady.load(std::memory_order_acquire)) {
        TelemetrySample s = TelemetryHub::instance().latest();
        if (s.t != lastSampleT) {
            lastSampleT = s.t;
            tel = s;
            TelemetryHub::instance().history(1800.0, history);
        }
    }
    pollEngines();
    pollUpdates();
    automationTick(dt);
    if (resultsDirty && db.isOpen()) refreshResults();
    if (settingsDirty) {
        settingsSaveTimer -= dt;
        if (settingsSaveTimer <= 0) {
            SettingsStore::instance().set(settings);
            SettingsStore::instance().save();
            settingsDirty = false;
        }
    }

    // Global shortcuts
    if (modal == Modal::None) {
        if (m_input.ctrl && m_input.key >= '1' && m_input.key < '1' + (int)Page::Count) navigate((Page)(m_input.key - '1'));
        if (m_input.key == VK_F12) captureWindow();
    }

    ui.setInputEnabled(true);
    ui.beginFrame(m_input, dt, settings.reduceMotion);
    if (!canvas.begin()) return;
    drawBackground();
    const float W = canvas.width(), H = canvas.height();
    ui.setInputEnabled(modal == Modal::None);
    Rect full{0, 0, W, H};
    Rect side = full.cutLeft(236);
    drawSidebar(side);

    Rect content = full.inset(36, 24);
    // Global top-right actions
    {
        Rect bar{content.right() - 360, content.y + 8, 360, 36};
        Rect cam = bar.cutRight(36);
        if (ui.iconButton("cam", Icon::Camera, cam, T("Pencere ekran görüntüsü al (F12)"))) captureWindow();
        bar.cutRight(8);
        Rect info = bar.cutRight(36);
        if (ui.iconButton("about", Icon::Info, info, T("Hakkında"))) openModal(Modal::About);
        bar.cutRight(10);
        const Theme& t = Theme::current();
        std::string pillText;
        Color pc = t.perf;
        Page target = page;
        if (stress.busy()) {
            StressStatus s = stress.status();
            pillText = std::string(T("Stres testi")) + " · " + formatDuration(s.elapsedSec);
            pc = s.tempWarning ? t.warning : t.warning;
            target = Page::Stress;
        } else if (bench.busy()) {
            BenchStatus b = bench.status();
            pillText = std::string(T("Benchmark")) + format(" · %.0f %%", b.overallProgress * 100);
            target = Page::Benchmark;
        }
        if (!pillText.empty()) {
            TextStyle st; st.size = 12.5f; st.weight = Weight::SemiBold;
            float pw = canvas.measure(pillText, st) + 40;
            Rect pr = bar.cutRight(pw).inset(0, 2);
            if (ui.pressedOn(ui.id("status-pill"), pr)) navigate(target);
            canvas.fillRound(pr, pr.h / 2, pc.alpha(0.12f));
            canvas.strokeRound(pr, pr.h / 2, pc.alpha(0.3f), 1);
            float pulse = 0.5f + 0.5f * std::sin(ui.time() * 4);
            canvas.fillCircle(pr.x + 16, pr.cy(), 3.5f + pulse, pc.alpha(0.35f));
            canvas.fillCircle(pr.x + 16, pr.cy(), 3.5f, pc);
            canvas.text(pillText, {pr.x + 28, pr.y, pr.w - 32, pr.h}, t.text, st);
        }
    }

    pageT = ui.anim(ui.id("pageT"), 1.0f, settings.reduceMotion ? 100.0f : 9.0f);
    if (pageT < 0.999f && !settings.reduceMotion) {
        float e = 1 - (1 - pageT) * (1 - pageT);
        canvas.pushOpacity(e);
        canvas.setTranslation(0, (1 - e) * 10);
    }
    switch (page) {
        case Page::Stress: drawStressPage(*this, content); break;
        case Page::Benchmark: drawBenchmarkPage(*this, content); break;
        case Page::Leaderboard: drawLeaderboardPage(*this, content); break;
        case Page::Results: drawResultsPage(*this, content); break;
        case Page::Hardware: drawHardwarePage(*this, content); break;
        case Page::Settings: drawSettingsPage(*this, content); break;
        default: break;
    }
    canvas.setTranslation(0, 0);
    if (pageT < 0.999f && !settings.reduceMotion) canvas.popOpacity();

    ui.setInputEnabled(true);
    drawModal();
    ui.endFrame();
    canvas.end();
    std::wstring cap; bool capOk;
    if (canvas.takeCaptureResult(cap, capOk)) {
        if (capOk) ui.toast(std::string(T("Ekran görüntüsü kaydedildi: ")) + narrow(cap), ToastKind::Success);
        else ui.toast(T("Ekran görüntüsü kaydedilemedi"), ToastKind::Error);
    }
    m_input.pressed = m_input.released = false;
    m_input.wheel = 0;
    m_input.key = 0;
    m_input.chars.clear();
}

int App::run(HINSTANCE inst, int show) {
    QueryPerformanceFrequency(&m_freq);
    settings = SettingsStore::instance().get();
    applySettings();
    Fonts::init();

    std::string dbErr;
    if (!db.open(Paths::database(), dbErr)) LOGE("Database open failed: %s", dbErr.c_str());

    StressPreset presets[] = {StressPreset::Eco, StressPreset::Balanced, StressPreset::Extreme, StressPreset::Custom};
    (void)presets;
    stressCfg.preset = StressPreset::Balanced;
    stressCfg.quality = stressPresetQuality(StressPreset::Balanced, 4ull << 30);
    {
        int m = settings.defaultStressMinutes;
        stressDurIndex = m <= 1 ? 0 : m <= 5 ? 1 : m <= 10 ? 2 : m <= 30 ? 3 : 4;
    }
    benchCfg.fullscreen = settings.benchFullscreen;
    stress.onFinished = [this](const BenchmarkResult& r) {
        { std::lock_guard<std::mutex> l(m_pendingMutex); m_pending.push_back(r); }
        PostMessageW(m_hwnd, WM_APP + 1, 0, 0);
    };
    bench.onFinished = [this](const BenchmarkResult& r) {
        { std::lock_guard<std::mutex> l(m_pendingMutex); m_pending.push_back(r); }
        PostMessageW(m_hwnd, WM_APP + 1, 0, 0);
    };
    page = (Page)std::clamp(settings.startPage, 0, (int)Page::Count - 1);
    parseCommandLine();
    LeaderboardClient::instance().loadConfig();
    if (settings.playerId.empty()) { settings.playerId = newPlayerId(); markSettingsChanged(); }

    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16, 0);
    wc.hbrBackground = CreateSolidBrush(RGB(9, 10, 12));
    wc.lpszClassName = kMainClass;
    RegisterClassExW(&wc);

    UINT dpi = 96;
    using GetDpiForSystemFn = UINT(WINAPI*)();
    if (auto fn = (GetDpiForSystemFn)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForSystem")) dpi = fn();
    float s = dpi / 96.0f;
    RECT work; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int ww = std::min((int)(1480 * s), (int)(work.right - work.left) - 40);
    int wh = std::min((int)(920 * s), (int)(work.bottom - work.top) - 40);
    int x = work.left + ((work.right - work.left) - ww) / 2, y = work.top + ((work.bottom - work.top) - wh) / 2;
    m_hwnd = CreateWindowExW(0, kMainClass, L"FurUltimate", WS_OVERLAPPEDWINDOW, x, y, ww, wh, nullptr, nullptr, inst, this);
    if (!m_hwnd) return 1;
    applySettings();

    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    if (auto fn = (GetDpiForWindowFn)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")) dpi = fn(m_hwnd);
    canvas.setDpi((float)dpi);
    std::string err;
    if (!canvas.init(m_hwnd, err)) {
        MessageBoxW(m_hwnd, widen(err).c_str(), L"FurUltimate", MB_ICONERROR);
        return 2;
    }
    canvas.setDpi((float)dpi);
    detectHardwareAsync();
    if (!settings.lastRunVersion.empty() && settings.lastRunVersion != AETHER_VERSION && isNewerVersion(AETHER_VERSION, settings.lastRunVersion))
        ui.toast(format(T("FurUltimate %s sürümüne güncellendi"), AETHER_VERSION), ToastKind::Success);
    if (settings.lastRunVersion != AETHER_VERSION) { settings.lastRunVersion = AETHER_VERSION; markSettingsChanged(); }
    if (settings.autoCheckUpdates && m_autorun.empty()) checkForUpdates(false);
    ShowWindow(m_hwnd, settings.startMaximized ? SW_SHOWMAXIMIZED : show);
    UpdateWindow(m_hwnd);

    float idle = 0;
    while (m_running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) m_running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!m_running) break;
        bool busy = stress.busy() || bench.busy();
        bool newSample = hwReady && TelemetryHub::instance().latest().t != lastSampleT;
        if (m_needFrame || ui.animating() || busy || newSample || modalT > 0.001f || (int)Updater::instance().status().state != updateSeenState) {
            m_needFrame = false;
            frame();
            idle = 0;
        } else {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 100, QS_ALLINPUT);
            idle += 0.1f;
        }
    }
    // Safe shutdown: stop workloads, wait for worker threads, flush settings.
    stress.stop(); bench.stop();
    stress.join(); bench.join();
    Updater::instance().shutdown();
    if (m_hwThread.joinable()) m_hwThread.join();
    TelemetryHub::instance().stop();
    LeaderboardClient::instance().shutdown();
    SettingsStore::instance().set(settings);
    SettingsStore::instance().save();
    db.close();
    canvas.shutdown();
    DestroyWindow(m_hwnd);
    return 0;
}

} // namespace aether
