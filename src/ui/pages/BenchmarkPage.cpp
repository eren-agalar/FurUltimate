#include "../App.h"
#include "../../bench/Score.h"
#include "../../core/Strings.h"
#include "../../core/I18n.h"
#include <cmath>

namespace aether {

void drawBenchmarkPage(App& app, Rect area) {
    const Theme& t = Theme::current();
    UI& ui = app.ui;
    Canvas& c = ui.c;
    pageHeader(app, area, T("Benchmark"), std::string(score::kBenchmarkVersion) + " · " + T("Beş skorlu test + Yapay Zekâ · tekrarlanabilir ve belgelenmiş GPU ölçümü"));
    BenchStatus st = app.bench.status();
    bool busy = app.bench.busy();
    bool can = app.hwReady && app.gpu() && !app.stress.busy();
    float scroll = ui.beginScroll("bm-scroll", area);
    Rect a{area.x, area.y - scroll, area.w - 8, 10000};
    float top = a.y;
    bool stack = a.w < 1100;
    Rect left = stack ? a : Rect{a.x, a.y, a.w * 0.5f - 9, 10000};
    Rect right = stack ? a : Rect{a.x + a.w * 0.5f + 9, a.y, a.w * 0.5f - 9, 10000};

    // ---------- configuration + test list
    Rect card = left.cutTop(770);
    ui.card(card);
    Rect in = card.inset(22);
    ui.label(T("Çalıştırma ayarları"), in.cutTop(24), t.text, 15, Weight::SemiBold);
    in.cutTop(16);
    ui.label(T("Ön ayar"), in.cutTop(20), t.text, 13, Weight::SemiBold);
    in.cutTop(6);
    int preset = (int)app.benchCfg.preset;
    if (ui.segmented("bm-preset", in.cutTop(36), {T("Standart 1080p"), T("Yüksek 1440p"), T("Ultra 2160p")}, preset, !busy))
        app.benchCfg.preset = (BenchPreset)preset;
    in.cutTop(14);
    {
        Rect r = in.cutTop(34);
        ui.label(T("Hızlı mod"), {r.x, r.y, r.w - 60, 18}, t.text, 13, Weight::SemiBold);
        ui.label(T("Kısa süreler · skor liderlik tablosuna girmez"), {r.x, r.y + 17, r.w - 60, 16}, t.text3, 11.5f);
        ui.toggle("bm-quick", r, app.benchCfg.quick, !busy);
        in.cutTop(10);
        r = in.cutTop(34);
        ui.label(T("Tam ekran"), {r.x, r.y, r.w - 60, 18}, t.text, 13, Weight::SemiBold);
        ui.label(T("Kenarlıksız tam ekran test penceresi"), {r.x, r.y + 17, r.w - 60, 16}, t.text3, 11.5f);
        ui.toggle("bm-fs", r, app.benchCfg.fullscreen, !busy);
    }
    in.cutTop(18);
    ui.caps(T("Testler"), in.cutTop(18), t.text2);
    in.cutTop(6);
    const BenchTestInfo* tests = benchTests();
    for (int i = 0; i < kBenchTestCount; ++i) {
        Rect r = in.cutTop(62);
        bool inRun = (st.testMask & (1u << i)) != 0;
        bool current = busy && st.testIndex == i;
        bool done = inRun && ((busy && st.testIndex > i) || (!busy && st.subScores[i] > 0 && st.state == EngineState::Finished));
        bool skipped = busy && !inRun;
        if (skipped) c.pushOpacity(0.45f);
        float hl = ui.anim(ui.id(format("bm-row%d", i)), current ? 1.0f : 0.0f, 10);
        if (hl > 0.01f) { c.fillRound(r, 12, t.perf.alpha(0.07f * hl)); c.strokeRound(r, 12, t.perf.alpha(0.25f * hl), 1); }
        Rect num{r.x + 12, r.cy() - 15, 30, 30};
        c.fillRound(num, 15, done ? t.success.alpha(0.15f) : FG(0.05f));
        if (done) drawIcon(c, Icon::Check, num.x + 7, num.y + 7, 16, t.success, 1.8f);
        else ui.label(format("%d", i + 1), num, current ? t.perf : t.text2, 13, Weight::SemiBold, Align::Center);
        bool aiRow = i == kAiTestIndex;
        ui.label(T(tests[i].name), {r.x + 56, r.y + 10, r.w - 240, 20}, t.text, 13.5f, Weight::SemiBold);
        if (aiRow) ui.badge(T("Ayrı AI skoru"), r.x + 64 + c.measure(T(tests[i].name), TextStyle{13.5f, Weight::SemiBold}), r.y + 10, t.perf);
        TextStyle ds; ds.size = 11.5f;
        c.text(T(tests[i].description), {r.x + 56, r.y + 31, r.w - 240, 18}, t.text3, ds);
        float vx = r.right() - 48 - 160;
        if (st.subScores[i] > 0 && inRun) {
            std::string v = i == 4 ? format("%.1f %%", st.subValues[i]) : i == 2 ? format("%.2f TFLOPS", st.subValues[i])
                          : i == 3 ? format("%.0f GB/s", st.subValues[i]) : aiRow ? format("%.1f TOPS", st.subValues[i]) : format("%.1f FPS", st.subValues[i]);
            ui.label(v, {vx, r.y + 10, 154, 20}, t.text, 13, Weight::SemiBold, Align::Trailing);
            ui.label(format("%s %.0f", aiRow ? T("AI skoru") : T("Alt skor"), st.subScores[i]), {vx, r.y + 31, 154, 18}, t.text3, 11.5f,
                     Weight::Regular, Align::Trailing);
        } else if (current) {
            std::string live = st.warmup ? T("Isınma") : T("Ölçülüyor");
            ui.label(live, {vx, r.y + 10, 154, 20}, t.perf, 12.5f, Weight::SemiBold, Align::Trailing);
        }
        if (skipped) c.popOpacity();
        // Run only this test
        if (!busy && ui.iconButton(format("bm-one%d", i), Icon::Play, {r.right() - 42, r.cy() - 16, 32, 32},
                                   T("Yalnızca bu testi çalıştır"), can)) {
            app.pendingBenchMask = 1u << i;
            if (app.settings.confirmIntensive) app.openModal(Modal::ConfirmBenchmark);
            else app.startBenchmark();
        }
    }
    in.cutTop(10);
    double planned = BenchmarkEngine::plannedDuration(app.benchCfg);
    uint32_t rw, rh;
    benchPresetResolution(app.benchCfg.preset, rw, rh);
    ui.label(format(T("Tahmini süre %s · İşleme çözünürlüğü %u×%u · Anahtar %s"), formatDuration(planned).c_str(), rw, rh,
                    BenchmarkEngine::configKey(app.benchCfg).c_str()),
             in.cutTop(18), t.text3, 11.5f);
    Rect ctl{in.x, card.bottom() - 22 - 42, in.w, 42};
    if (!busy) {
        if (ui.button("bm-start", T("Tüm testleri başlat"), ctl, ButtonStyle::Primary, can, (int)Icon::Play)) {
            app.pendingBenchMask = 0x3F;
            if (app.settings.confirmIntensive) app.openModal(Modal::ConfirmBenchmark);
            else app.startBenchmark();
        }
    } else if (ui.button("bm-stop", T("İptal et"), ctl, ButtonStyle::Danger, true, (int)Icon::Stop)) {
        app.bench.stop();
    }

    // ---------- live progress / result
    Rect pc = stack ? (left.cutTop(18), left.cutTop(770)) : right.cutTop(770);
    ui.card(pc);
    Rect pi = pc.inset(22);
    if (busy || st.state == EngineState::Starting) {
        ui.label(T("Canlı ilerleme"), pi.cutTop(24), t.text, 15, Weight::SemiBold);
        pi.cutTop(12);
        Rect pr = pi.cutTop(18);
        ui.label(st.testIndex >= 0 ? std::string(T(tests[st.testIndex].name)) : std::string(st.message), pr, t.text2, 12.5f);
        ui.label(format("%.0f %%", st.overallProgress * 100), pr, t.text, 12.5f, Weight::SemiBold, Align::Trailing);
        pi.cutTop(8);
        ui.progressBar(pi.cutTop(6), (float)st.overallProgress, t.perf);
        pi.cutTop(30);
        TextStyle big; big.size = 72; big.weight = Weight::SemiBold; big.display = true; big.align = Align::Center;
        bool compute = st.liveUnit == "TFLOPS" || st.liveUnit == "GB/s" || st.liveUnit == "TOPS";
        std::string bigText = !compute ? (st.fps > 0 ? format("%.0f", st.fps) : "—")
                            : format(st.liveUnit == "TFLOPS" ? "%.2f" : st.liveUnit == "TOPS" ? "%.1f" : "%.0f", st.liveValue);
        c.text(bigText, pi.cutTop(90), t.text, big);
        ui.label(compute ? st.liveUnit : std::string("FPS"), pi.cutTop(22), t.text2, 13, Weight::SemiBold, Align::Center);
        pi.cutTop(28);
        float cw = pi.w / 4;
        Rect mr = pi.cutTop(64);
        auto m = [&](int i, const std::string& k, const std::string& v) {
            Rect r{mr.x + i * cw, mr.y, cw, mr.h};
            ui.caps(k, {r.x, r.y, r.w, 16}, t.text3, Align::Center);
            TextStyle vs; vs.size = 20; vs.weight = Weight::SemiBold; vs.display = true; vs.align = Align::Center;
            c.text(v, {r.x, r.y + 20, r.w, 30}, t.text, vs);
        };
        bool fpsTest = !compute;
        m(0, T("Ortalama"), fpsTest && st.avgFps > 0 ? format("%.1f", st.avgFps) : "—");
        m(1, T("Minimum"), fpsTest && st.minFps > 0 ? format("%.0f", st.minFps) : "—");
        m(2, T("Maksimum"), fpsTest && st.maxFps > 0 ? format("%.0f", st.maxFps) : "—");
        m(3, T("Kare süresi"), fpsTest && st.frameMs > 0 ? format("%.2f ms", st.frameMs) : "—");
        pi.cutTop(18);
        Rect mr2 = pi.cutTop(64);
        mr = mr2;
        m(0, T("GPU yükü"), naStr(app.tel.gpuLoad, "%.0f %%"));
        m(1, T("Sıcaklık"), fmtTemp(app.tel.gpuTemp, (int)app.settings.tempUnit));
        m(2, T("Geçen süre"), formatDuration(st.elapsedSec));
        m(3, T("Toplam"), formatDuration(st.totalSec));
        pi.cutTop(18);
        TextStyle ns; ns.size = 12; ns.wrap = true; ns.align = Align::Center;
        c.text(st.warmup ? T("Isınma turları ölçüme dahil edilmez; sürücü önbellekleri ve saat hızları dengelenir.")
                         : T("Ölçüm sırasında test penceresini öne alın ve diğer uygulamaları kapalı tutun."),
               pi.cutTop(40), t.text3, ns);
    } else if (app.haveLastBench) {
        const BenchmarkResult& r = app.lastBenchResult;
        bool ok = r.status == "completed";
        Rect h = pi.cutTop(24);
        ui.label(ok ? T("Sonuç") : T("Çalıştırma tamamlanamadı"), h, t.text, 15, Weight::SemiBold);
        ui.badge(r.preset, h.right(), h.y + 2, t.perf, nullptr, true);
        pi.cutTop(20);
        ui.caps(T("FurUltimate skoru"), pi.cutTop(18), t.text2, Align::Center);
        TextStyle big; big.size = 84; big.weight = Weight::SemiBold; big.display = true; big.align = Align::Center;
        bool hasOverall = ok && r.fullRun && r.score > 0;
        float sv = ui.anim(ui.id("bm-score-count"), hasOverall ? (float)r.score : 0.0f, 3.5f);
        c.text(hasOverall ? format("%.0f", sv) : "—", pi.cutTop(104), t.text, big);
        std::string sub = !ok ? r.statusDetail
                        : !r.fullRun ? std::string(T("Tek test çalıştırması · genel skor yalnızca 5 testin tamamıyla hesaplanır"))
                        : format("%s · %d×%d · %s", r.benchVersion.c_str(), r.width, r.height, formatDuration(r.durationSec).c_str());
        ui.label(sub, pi.cutTop(20), ok ? t.text3 : t.warning, 12, Weight::Regular, Align::Center);
        if (r.aiScore > 0) {
            pi.cutTop(12);
            Rect ar = pi.cutTop(54);
            c.fillRound(ar, 12, t.perf.alpha(0.08f));
            c.strokeRound(ar, 12, t.perf.alpha(0.25f), 1);
            drawIcon(c, Icon::AI, ar.x + 16, ar.cy() - 10, 20, t.perf, 1.5f);
            ui.label(T("AI skoru"), {ar.x + 46, ar.y + 8, 200, 18}, t.text2, 12, Weight::SemiBold);
            std::string tops = r.aiTheoreticalTops > 0 ? format("(%.1f TOPS · %s %.0f TOPS)", r.aiTops, T("teorik"), r.aiTheoreticalTops)
                                                       : format("(%.1f TOPS)", r.aiTops);
            float sw = c.measure(format("%.0f", r.aiScore), TextStyle{22, Weight::SemiBold, true});
            ui.label(format("%.0f", r.aiScore), {ar.x + 46, ar.y + 24, 140, 26}, t.text, 22, Weight::SemiBold, Align::Leading, true);
            ui.label(tops, {ar.x + 54 + sw, ar.y + 28, ar.w - 120 - sw, 20}, t.text2, 12.5f);
            ui.label(r.aiPath, {ar.right() - 150, ar.y + 8, 136, 18}, t.text3, 11.5f, Weight::Regular, Align::Trailing);
        }
        pi.cutTop(20);
        double mx = 1;
        for (auto& s : r.subs) mx = std::max(mx, s.score);
        for (auto& s : r.subs) {
            Rect row = pi.cutTop(40);
            ui.label(T(s.name.c_str()), {row.x, row.y, row.w * 0.4f, 20}, t.text, 13, Weight::Medium);
            std::string v = formatMeasurement(s);
            ui.label(v, {row.x, row.y, row.w - 60, 20}, t.text2, 12.5f, Weight::Regular, Align::Trailing);
            ui.label(format("%.0f", s.score), {row.right() - 56, row.y, 56, 20}, t.text, 13, Weight::SemiBold, Align::Trailing);
            ui.progressBar({row.x, row.y + 26, row.w, 4}, ui.anim(ui.id("bm-bar" + s.id), (float)(s.score / mx), 5), t.perf, false);
        }
        Rect btn{pi.x, pc.bottom() - 22 - 40, (pi.w - 12) / 2, 40};
        if (ui.button("bm-pdf", T("PDF raporu"), btn, ButtonStyle::Secondary, true, (int)Icon::Export)) app.exportResult(r, 0);
        if (ui.button("bm-res", T("Sonuçlarda aç"), btn.offset(btn.w + 12, 0), ButtonStyle::Ghost, true, (int)Icon::ChevronRight)) {
            app.selectedResult = r.id;
            app.navigate(Page::Results);
        }
    } else {
        ui.label(T("Puanlama yöntemi"), pi.cutTop(24), t.text, 15, Weight::SemiBold);
        pi.cutTop(14);
        TextStyle bs; bs.size = 13; bs.wrap = true; bs.valign = Align::Leading;
        std::string text = format(
            T("Her test sabit bir iş yükünü sabit bir süre boyunca çalıştırır. Alt skor = 1000 × ölçüm / referans değer.\n\nReferanslar: Grafik %.0f FPS, Shader %.0f FPS, Hesaplama %.0f TFLOPS, VRAM %.0f GB/s. Kare kararlılığı = 1000 × (%%1 düşük FPS / ortalama FPS).\n\nGenel skor, ağırlıklı geometrik ortalamadır: Grafik %%35, Shader %%20, Hesaplama %%20, VRAM %%15, Kararlılık %%10.\n\nSkorlar yalnızca aynı benchmark sürümü ve aynı ön ayarla karşılaştırılabilir. Erken durdurulan çalıştırmalara genel skor verilmez."),
            score::kRefGraphicsFps, score::kRefShaderFps, score::kRefComputeGflops / 1000.0, score::kRefVramGBs);
        c.text(text, pi.cutTop(330), t.text2, bs);
        if (st.state == EngineState::Failed && !st.message.empty()) {
            Rect b = pi.cutTop(52);
            c.fillRound(b, 10, t.critical.alpha(0.10f));
            c.strokeRound(b, 10, t.critical.alpha(0.25f), 1);
            TextStyle es; es.size = 12.5f; es.wrap = true;
            c.text(st.message, b.inset(14, 6), t.text, es);
        }
    }
    float endY = stack ? left.y : std::max(left.y, right.y);
    ui.endScroll(endY - top + 24);
}

} // namespace aether
