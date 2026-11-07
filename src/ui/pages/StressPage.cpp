#include "../App.h"
#include "../../core/Strings.h"
#include "../../core/I18n.h"
#include <cmath>

namespace aether {

static float settingRow(App& app, Rect& col, const std::string& label, const std::string& hint) {
    const Theme& t = Theme::current();
    Rect r = col.cutTop(hint.empty() ? 22 : 38);
    app.ui.label(label, {r.x, r.y, r.w, 20}, t.text, 13, Weight::SemiBold);
    if (!hint.empty()) app.ui.label(hint, {r.x, r.y + 19, r.w, 18}, t.text3, 11.5f);
    col.cutTop(8);
    return r.y;
}

static const char* presetDesc(StressPreset p) {
    switch (p) {
        case StressPreset::Eco: return T("Düşük yük, ekran yenileme hızıyla sınırlı. Temel işlevsellik kontrolü için.");
        case StressPreset::Balanced: return T("Orta düzey sürekli yük. Günlük kararlılık testleri için önerilir.");
        case StressPreset::Extreme: return T("Yoğun shader, geometri, hesaplama ve bellek yükü. Yüksek güç ve sıcaklık üretir.");
        default: return T("Tüm iş yükü parametrelerini kendiniz yapılandırın.");
    }
}

void drawStressPage(App& app, Rect area) {
    const Theme& t = Theme::current();
    UI& ui = app.ui;
    Canvas& c = ui.c;
    pageHeader(app, area, T("GPU Stres Testi"), T("Gerçek Direct3D 11 iş yükleriyle uzun süreli kararlılık ve termal test"));
    StressStatus st = app.stress.status();
    bool busy = app.stress.busy();
    bool can = app.hwReady && app.gpu() && !app.bench.busy();
    int unit = (int)app.settings.tempUnit;

    float scroll = ui.beginScroll("st-scroll", area);
    Rect a{area.x, area.y - scroll, area.w - 8, 10000};
    float top = a.y;
    bool stack = a.w < 1100;
    Rect left = stack ? a : Rect{a.x, a.y, a.w * 0.44f, 10000};
    Rect right = stack ? a : Rect{a.x + a.w * 0.44f + 18, a.y, a.w * 0.56f - 18, 10000};

    // ---------------- configuration card
    StressConfig& cfg = app.stressCfg;
    bool custom = cfg.preset == StressPreset::Custom;
    float cfgH = custom ? 1010 : 612;
    Rect card = left.cutTop(cfgH);
    ui.card(card);
    Rect in = card.inset(22);
    ui.label(T("Yapılandırma"), in.cutTop(24), t.text, 15, Weight::SemiBold);
    in.cutTop(14);
    settingRow(app, in, T("İş yükü ön ayarı"), presetDesc(cfg.preset));
    int preset = (int)cfg.preset;
    if (ui.segmented("st-preset", in.cutTop(36), {"ECO", "BALANCED", "EXTREME", "CUSTOM"}, preset, !busy)) {
        cfg.preset = (StressPreset)preset;
        if (cfg.preset != StressPreset::Custom)
            cfg.quality = stressPresetQuality(cfg.preset, app.gpu() ? app.gpu()->adapter.dedicatedVideoMemory : (2ull << 30));
    }
    in.cutTop(18);
    settingRow(app, in, T("Çözünürlük"), "");
    ui.segmented("st-res", in.cutTop(34), {"720p", "1080p", "1440p", "2160p"}, app.stressResIndex, !busy);
    in.cutTop(16);
    settingRow(app, in, T("İşleme kalitesi"), T("Çözünürlük çarpanı (1,5× = süper örnekleme)"));
    ui.segmented("st-scale", in.cutTop(34), {"0.5×", "0.75×", "1.0×", "1.5×"}, app.stressScaleIndex, !busy);
    in.cutTop(16);
    settingRow(app, in, T("Süre"), T("Sürekli mod yalnızca elle durdurulana kadar çalışır"));
    ui.segmented("st-dur", in.cutTop(34), {T("1 dk"), T("5 dk"), T("10 dk"), T("30 dk"), T("60 dk"), T("Sürekli")}, app.stressDurIndex, !busy);
    in.cutTop(16);
    settingRow(app, in, T("Görüntü modu"), "");
    ui.segmented("st-mode", in.cutTop(34), {T("Pencere"), T("Tam ekran")}, app.stressModeIndex, !busy);
    in.cutTop(18);

    if (custom) {
        SceneQuality& q = cfg.quality;
        ui.caps(T("Özel iş yükü parametreleri"), in.cutTop(20), t.text2);
        in.cutTop(6);
        auto sl = [&](const char* key, const std::string& name, int& v, int lo, int hi, int step, const std::string& valText) {
            Rect r = in.cutTop(30);
            ui.label(name, {r.x, r.y, r.w * 0.42f, r.h}, t.text2, 12.5f);
            ui.label(valText, {r.right() - 90, r.y, 90, r.h}, t.text, 12.5f, Weight::SemiBold, Align::Trailing);
            ui.sliderInt(key, {r.x + r.w * 0.42f, r.y, r.w * 0.58f - 96, r.h}, v, lo, hi, step, !busy);
        };
        sl("q-steps", T("Işın yürütme adımı"), q.marchSteps, 32, 320, 8, format("%d", q.marchSteps));
        sl("q-refl", T("Yansıma sekmesi"), q.reflectionBounces, 0, 4, 1, format("%d", q.reflectionBounces));
        sl("q-shadow", T("Gölge adımı"), q.shadowSteps, 0, 96, 4, format("%d", q.shadowSteps));
        sl("q-vol", T("Hacimsel ışık örneği"), q.volumetricSamples, 0, 64, 4, format("%d", q.volumetricSamples));
        sl("q-ao", T("Ortam kapatma"), q.aoSamples, 0, 8, 1, format("%d", q.aoSamples));
        sl("q-oct", T("Yüzey detay oktavı"), q.detailOctaves, 0, 6, 1, format("%d", q.detailOctaves));
        int shardsK = q.shardInstances / 1000;
        sl("q-shards", T("Prosedürel geometri"), shardsK, 0, 300, 5, format("%dk", shardsK));
        q.shardInstances = shardsK * 1000;
        int partK = q.particleCount / 1024;
        sl("q-part", T("Parçacık sayısı"), partK, 0, 2048, 32, format("%dk", partK));
        q.particleCount = partK * 1024;
        sl("q-iter", T("Hesaplama iterasyonu"), q.computeIterations, 0, 16, 1, format("%d", q.computeIterations));
        uint64_t vram = app.gpu() ? app.gpu()->adapter.dedicatedVideoMemory : (2ull << 30);
        int capMB = (int)std::min<uint64_t>(1024, std::max<uint64_t>(64, vram / 4 / (1 << 20)));
        int memMB = (int)(q.memoryBytes >> 20);
        memMB = std::min(memMB, capMB);
        sl("q-mem", T("VRAM akış belleği"), memMB, 0, capMB, 16, format("%d MB", memMB));
        q.memoryBytes = (uint64_t)memMB << 20;
        sl("q-mempass", T("Bellek geçişi / kare"), q.memoryPasses, 0, 8, 1, format("%d", q.memoryPasses));
        in.cutTop(8);
        auto tg = [&](const char* key, const std::string& name, bool& v) {
            Rect r = in.cutTop(32);
            ui.label(name, {r.x, r.y, r.w - 50, r.h}, t.text2, 12.5f);
            ui.toggle(key, r, v, !busy);
        };
        tg("w-frag", T("Karmaşık fragment shader'lar"), q.fragment);
        tg("w-geo", T("Prosedürel geometri"), q.geometry);
        tg("w-tex", T("Doku örnekleme (3B gürültü dokusu)"), q.textures);
        tg("w-comp", T("Hesaplama shader'ı (parçacık simülasyonu)"), q.compute);
        tg("w-mem", T("Bellek yoğun iş yükü"), q.memory);
        ui.label(format(T("Bellek iş yükü üst sınırı: VRAM'in %%25'i ve en fazla 1 GB (%d MB)."), capMB), in.cutTop(20), t.text3, 11.5f);
    } else {
        SceneQuality q = stressPresetQuality(cfg.preset, app.gpu() ? app.gpu()->adapter.dedicatedVideoMemory : (2ull << 30));
        Rect sum = in.cutTop(96);
        c.fillRound(sum, 12, FG(0.025f));
        c.strokeRound(sum, 12, t.border, 1);
        Rect si = sum.inset(16, 12);
        ui.caps(T("İş yükü özeti"), {si.x, si.y, si.w, 16}, t.text2);
        TextStyle ss; ss.size = 12.5f; ss.wrap = true; ss.valign = Align::Leading;
        c.text(format(T("%d ışın adımı · %d yansıma · %d hacimsel örnek · %dk geometri örneği (%.1fM üçgen) · %dk parçacık · %d MB VRAM akışı"),
                      q.marchSteps, q.reflectionBounces, q.volumetricSamples, q.shardInstances / 1000, q.shardInstances * 24 / 1e6,
                      q.particleCount / 1024, (int)(q.memoryBytes >> 20)),
               {si.x, si.y + 22, si.w, si.h - 22}, t.text, ss);
    }

    // ---------------- status / control card
    float ctrlH = 520;
    Rect sc = stack ? (left.cutTop(18), left.cutTop(ctrlH)) : right.cutTop(ctrlH);
    ui.card(sc);
    Rect si = sc.inset(22);
    Rect head = si.cutTop(26);
    ui.label(T("Durum"), head, t.text, 15, Weight::SemiBold);
    std::string stText; Color stCol = t.text2;
    switch (st.state) {
        case EngineState::Idle: stText = T("Hazır"); break;
        case EngineState::Starting: stText = T("Başlatılıyor"); stCol = t.perf; break;
        case EngineState::Running: stText = T("Çalışıyor"); stCol = t.success; break;
        case EngineState::Paused: stText = T("Duraklatıldı"); stCol = t.warning; break;
        case EngineState::Stopping: stText = T("Durduruluyor"); stCol = t.warning; break;
        case EngineState::Finished: stText = T("Tamamlandı"); stCol = t.text2; break;
        case EngineState::Failed: stText = T("Başarısız"); stCol = t.critical; break;
    }
    ui.badge(stText, head.right(), head.y + 2, stCol, nullptr, true);
    si.cutTop(14);
    // ring + numbers
    Rect ringArea = si.cutTop(200);
    float rcx = ringArea.x + 100, rcy = ringArea.cy();
    float frac = st.durationSec > 0 ? (float)(st.elapsedSec / st.durationSec) : (busy ? std::fmod(ui.time() * 0.15f, 1.0f) : 0.0f);
    ui.ringGauge(rcx, rcy, 82, ui.anim(ui.id("st-ring"), frac, 6), t.perf, 7);
    TextStyle big; big.size = 34; big.weight = Weight::SemiBold; big.display = true; big.align = Align::Center;
    c.text(busy ? format("%.0f", st.fps) : "—", {rcx - 80, rcy - 34, 160, 44}, t.text, big);
    ui.label("FPS", {rcx - 80, rcy + 8, 160, 18}, t.text2, 12, Weight::SemiBold, Align::Center);
    ui.label(busy || st.elapsedSec > 0 ? formatDuration(st.elapsedSec) + (st.durationSec > 0 ? " / " + formatDuration(st.durationSec) : std::string(" · ∞")) : "",
             {rcx - 80, rcy + 30, 160, 18}, t.text3, 12, Weight::Medium, Align::Center);
    Rect nums{ringArea.x + 220, ringArea.y + 6, ringArea.w - 220, ringArea.h};
    auto metric = [&](int i, const std::string& k, const std::string& v, const Color& col) {
        float cw = nums.w / 2;
        Rect r{nums.x + (i % 2) * cw, nums.y + (i / 2) * 64, cw - 10, 60};
        ui.caps(k, {r.x, r.y, r.w, 16}, t.text3);
        TextStyle vs; vs.size = 20; vs.weight = Weight::SemiBold; vs.display = true;
        c.text(v, {r.x, r.y + 18, r.w, 30}, col, vs);
    };
    TelemetrySample tel = app.tel;
    bool warn = st.tempWarning;
    metric(0, T("Sıcaklık"), fmtTemp(tel.gpuTemp, unit), warn ? t.warning : (std::isnan(tel.gpuTemp) ? t.text3 : t.text));
    metric(1, T("GPU yükü"), naStr(tel.gpuLoad, "%.0f %%"), std::isnan(tel.gpuLoad) ? t.text3 : t.text);
    metric(2, T("Kare süresi"), busy ? format("%.2f ms", st.frameMs) : "—", t.text);
    metric(3, T("GPU süresi"), busy && st.gpuMs >= 0 ? format("%.2f ms", st.gpuMs) : "—", t.text);
    metric(4, T("Güç"), !std::isnan(tel.powerW) ? format("%.0f W", tel.powerW) : naStr(tel.powerPct, "%.0f %% TDP"),
           std::isnan(tel.powerW) && std::isnan(tel.powerPct) ? t.text3 : t.text);
    metric(5, T("Ort. / Min FPS"), st.frames > 0 ? format("%.0f / %.0f", st.avgFps, st.minFps) : "—", t.text);
    si.cutTop(6);
    // temperature threshold bar
    {
        Rect tb = si.cutTop(44);
        ui.label(T("Sıcaklık eşikleri"), {tb.x, tb.y, tb.w, 18}, t.text2, 12);
        ui.label(format(T("Uyarı %s · Acil durdurma %s"), fmtTemp((float)app.settings.warnTempC, unit).c_str(), fmtTemp((float)app.settings.stopTempC, unit).c_str()),
                 {tb.x, tb.y, tb.w, 18}, t.text3, 12, Weight::Regular, Align::Trailing);
        Rect bar{tb.x, tb.y + 28, tb.w, 6};
        float lo = 30, hi = (float)app.settings.stopTempC + 5;
        c.fillRound(bar, 3, FG(0.06f));
        float wx = bar.x + bar.w * (app.settings.warnTempC - lo) / (hi - lo), sx = bar.x + bar.w * (app.settings.stopTempC - lo) / (hi - lo);
        c.fillRound({wx, bar.y, sx - wx, bar.h}, 3, t.warning.alpha(0.25f));
        c.fillRound({sx, bar.y, bar.right() - sx, bar.h}, 3, t.critical.alpha(0.35f));
        if (!std::isnan(tel.gpuTemp)) {
            float px = bar.x + bar.w * std::clamp((tel.gpuTemp - lo) / (hi - lo), 0.0f, 1.0f);
            c.fillCircle(px, bar.cy(), 6, warn ? t.warning : t.perf);
            c.strokeCircle(px, bar.cy(), 6, t.bg, 2);
        }
    }
    si.cutTop(10);
    // banners
    auto banner = [&](const std::string& text, const Color& col, Icon icon) {
        Rect b = si.cutTop(40);
        c.fillRound(b, 10, col.alpha(0.10f));
        c.strokeRound(b, 10, col.alpha(0.25f), 1);
        drawIcon(c, icon, b.x + 12, b.cy() - 8, 16, col, 1.5f);
        TextStyle bs; bs.size = 12.5f; bs.wrap = true;
        c.text(text, {b.x + 38, b.y, b.w - 46, b.h}, t.text, bs);
        si.cutTop(8);
    };
    if (busy && st.noTempSensor) banner(T("Sıcaklık sensörü okunamıyor — acil durdurma koruması devre dışı. Testi gözetim altında çalıştırın."), t.warning, Icon::Warning);
    else if (busy && warn) banner(T("GPU sıcaklığı uyarı eşiğinin üzerinde."), t.warning, Icon::Thermometer);
    else if (st.tdrReductions > 0 && busy) banner(st.message, t.perf, Icon::Info);
    else if (!busy && (st.state == EngineState::Finished || st.state == EngineState::Failed) && !st.message.empty())
        banner(st.message, st.state == EngineState::Failed ? t.critical : t.text2, st.state == EngineState::Failed ? Icon::Warning : Icon::Info);
    else if (!can && !busy) banner(app.bench.busy() ? T("Benchmark çalışıyor.") : T("GPU algılanıyor…"), t.text2, Icon::Info);
    // controls
    Rect ctl{si.x, sc.bottom() - 22 - 42, si.w, 42};
    if (!busy) {
        if (ui.button("st-start", T("Stres testini başlat"), ctl, ButtonStyle::Primary, can, (int)Icon::Play)) {
            if (app.settings.confirmIntensive) app.openModal(Modal::ConfirmStress);
            else app.startStress();
        }
    } else {
        float bw = (ctl.w - 12) / 2;
        bool paused = st.state == EngineState::Paused;
        if (ui.button("st-pause", paused ? T("Devam et") : T("Duraklat"), {ctl.x, ctl.y, bw, ctl.h}, ButtonStyle::Secondary,
                      st.state == EngineState::Running || paused, paused ? (int)Icon::Play : (int)Icon::Pause)) {
            if (paused) app.stress.resume(); else app.stress.pause();
        }
        if (ui.button("st-stop", T("Durdur"), {ctl.x + bw + 12, ctl.y, bw, ctl.h}, ButtonStyle::Danger, true, (int)Icon::Stop)) app.stress.stop();
    }

    // ---------------- live chart
    Rect lc = stack ? (left.cutTop(18), left.cutTop(300)) : (right.cutTop(18), right.cutTop(300));
    ui.card(lc);
    Rect li = lc.inset(20);
    ui.label(T("Test sırasında FPS ve sıcaklık"), {li.x, li.y, li.w, 22}, t.text, 15, Weight::SemiBold);
    ui.label(T("Son 5 dakika"), {li.x, li.y + 22, li.w, 18}, t.text2, 12);
    std::vector<float> fps, temps;
    double newest = app.history.empty() ? 0 : app.history.back().t;
    for (auto& h : app.history) if (newest - h.t <= 300) { fps.push_back(h.fps); temps.push_back(std::isnan(h.gpuTemp) ? NA : (float)displayTemp(h.gpuTemp, app.settings.tempUnit)); }
    ChartOptions o; o.minY = 0; o.spanSeconds = 300; o.grid = app.settings.chartGrid;
    lineChart(ui, "st-chart", {li.x, li.y + 48, li.w, li.h - 48},
              {{fps, t.perf, "FPS", "", true}, {temps, t.warning, T("Sıcaklık"), tempUnitSuffix(app.settings.tempUnit), false}}, o);

    float endY = std::max(left.y, right.y);
    if (stack) endY = left.y;
    ui.endScroll(endY - top + 24);
    (void)a;
}

} // namespace aether
