#include "../App.h"
#include "../../core/Strings.h"
#include "../../core/I18n.h"
#include "../../core/FileIO.h"
#include <cmath>

namespace aether {

static const BenchmarkResult* findResult(App& app, int64_t id) {
    for (auto& r : app.results) if (r.id == id) return &r;
    return nullptr;
}

static std::string statusLabel(const BenchmarkResult& r) {
    return r.status == "completed" ? T("Tamamlandı") : r.status == "aborted" ? T("Durduruldu") : T("Başarısız");
}

static void metricGrid(App& app, Rect& in, const BenchmarkResult& r) {
    const Theme& t = Theme::current();
    int unit = (int)app.settings.tempUnit;
    struct M { std::string k, v; };
    std::vector<M> ms = {
        {T("Ortalama FPS"), format("%.1f", r.avgFps)}, {T("Minimum FPS"), format("%.1f", r.minFps)},
        {T("Maksimum FPS"), format("%.1f", r.maxFps)}, {T("%1 düşük"), format("%.1f", r.p1LowFps)},
        {T("Kare süresi"), format("%.2f ms", r.avgFrameMs)}, {T("P99 kare"), format("%.2f ms", r.p99FrameMs)},
        {T("Ort. sıcaklık"), fmtTemp((float)r.avgTemp, unit)}, {T("Maks. sıcaklık"), fmtTemp((float)r.maxTemp, unit)},
        {T("Ort. GPU yükü"), naStr(r.avgLoad, "%.0f %%")}, {T("Ort. güç"), naStr(r.avgPower, "%.0f W")},
        {T("Çözünürlük"), format("%d×%d", r.width, r.height)}, {T("Süre"), formatDuration(r.durationSec)},
    };
    int cols = in.w > 640 ? 4 : 3;
    float cw = in.w / cols;
    for (size_t i = 0; i < ms.size(); i += cols) {
        Rect row = in.cutTop(52);
        for (int k = 0; k < cols && i + k < ms.size(); ++k) {
            Rect c{row.x + k * cw, row.y, cw - 8, row.h};
            app.ui.caps(ms[i + k].k, {c.x, c.y, c.w, 16}, t.text3);
            bool na = ms[i + k].v.find("N/A") != std::string::npos;
            app.ui.label(ms[i + k].v, {c.x, c.y + 18, c.w, 26}, na ? t.text3 : t.text, 17, Weight::SemiBold, Align::Leading, true);
        }
    }
}

void drawResultsPage(App& app, Rect area) {
    const Theme& t = Theme::current();
    UI& ui = app.ui;
    Canvas& c = ui.c;
    pageHeader(app, area, T("Sonuçlar"), format(T("Yerel geçmiş · %d kayıt"), (int)app.results.size()));
    bool stack = area.w < 980;
    Rect listR = stack ? area.cutTop(std::min(360.0f, area.h * 0.45f)) : area.cutLeft(std::min(420.0f, area.w * 0.36f));
    if (stack) area.cutTop(16); else area.cutLeft(18);
    Rect detail = area;

    // ---------------- history list
    ui.card(listR);
    Rect li = listR.inset(16);
    Rect fb = li.cutTop(34);
    if (ui.segmented("res-filter", fb, {T("Tümü"), T("Benchmark"), T("Stres")}, app.resultFilter)) app.resultsDirty = true;
    li.cutTop(12);
    if (app.results.empty()) {
        TextStyle es; es.size = 13; es.align = Align::Center; es.wrap = true;
        drawIcon(c, Icon::Results, li.cx() - 16, li.y + 50, 32, t.text3, 1.4f);
        c.text(T("Henüz kayıt yok. Bir benchmark veya stres testi çalıştırdığınızda sonuçlar burada saklanır."), {li.x + 20, li.y + 96, li.w - 40, 60}, t.text3, es);
    } else {
        float sc = ui.beginScroll("res-list", li);
        float y = li.y - sc;
        float y0 = y;
        for (auto& r : app.results) {
            Rect row{li.x, y, li.w - 6, 64};
            y += 68;
            if (row.bottom() < li.y || row.y > li.bottom()) continue;
            bool sel = r.id == app.selectedResult;
            if (ui.selectableRow(format("res%lld", (long long)r.id), row, sel)) app.selectedResult = r.id;
            Rect in = row.inset(14, 10);
            bool bench = r.kind == "benchmark";
            drawIcon(c, bench ? Icon::Benchmark : Icon::Stress, in.x, in.y + 3, 16, bench ? t.perf : t.warning, 1.5f);
            ui.label(bench ? T("Benchmark") : T("Stres testi"), {in.x + 24, in.y, in.w * 0.5f, 20}, t.text, 13, Weight::SemiBold);
            std::string val = !bench ? format("%.0f FPS", r.avgFps)
                            : (r.status == "completed" && r.fullRun && r.score > 0) ? format("%.0f", r.score)
                            : r.aiScore > 0 ? format("AI %.0f", r.aiScore) : std::string("—");
            ui.label(val, {in.x, in.y, in.w, 20}, t.text, 15, Weight::SemiBold, Align::Trailing, true);
            ui.label(r.timestamp.substr(0, 10) + " " + (r.timestamp.size() >= 16 ? r.timestamp.substr(11, 5) : "") + " · " + r.preset,
                     {in.x + 24, in.y + 22, in.w - 120, 18}, t.text3, 11.5f);
            if (r.status != "completed") ui.badge(statusLabel(r), in.right(), in.y + 21, r.status == "failed" ? t.critical : t.warning, nullptr, true);
            else if (r.id == app.compareResult) ui.badge(T("Karşılaştırma"), in.right(), in.y + 21, t.perf, nullptr, true);
        }
        ui.endScroll(y - y0);
    }

    // ---------------- detail
    const BenchmarkResult* r = findResult(app, app.selectedResult);
    if (!r && !app.results.empty()) { app.selectedResult = app.results.front().id; r = &app.results.front(); }
    ui.card(detail);
    if (!r) {
        TextStyle es; es.size = 14; es.align = Align::Center;
        c.text(T("Ayrıntıları görmek için bir sonuç seçin"), detail, t.text3, es);
        return;
    }
    const BenchmarkResult* cmp = app.compareResult && app.compareResult != r->id ? findResult(app, app.compareResult) : nullptr;
    float ds = ui.beginScroll("res-detail", detail.inset(2));
    Rect d{detail.x + 24, detail.y + 22 - ds, detail.w - 48, 10000};
    float top = d.y;
    bool bench = r->kind == "benchmark";
    // header
    Rect h = d.cutTop(30);
    ui.label(r->gpuName.empty() ? "N/A" : r->gpuName, {h.x, h.y, h.w - 200, 30}, t.text, 18, Weight::SemiBold);
    ui.badge(statusLabel(*r), h.right(), h.y + 5, r->status == "completed" ? t.success : r->status == "failed" ? t.critical : t.warning, nullptr, true);
    std::string ts = r->timestamp;
    if (ts.size() > 10 && ts[10] == 'T') ts[10] = ' ';
    ui.label(format("#%lld · %s · %s · %s %s", (long long)r->id, ts.c_str(), r->benchVersion.c_str(), T("Sürücü"),
                    r->driverVersion.empty() ? "N/A" : r->driverVersion.c_str()),
             d.cutTop(22), t.text3, 12);
    if (!r->statusDetail.empty()) ui.label(r->statusDetail, d.cutTop(20), r->status == "completed" ? t.text2 : t.warning, 12);
    d.cutTop(14);
    // actions
    Rect act = d.cutTop(38);
    float bx = act.x;
    auto actBtn = [&](const char* key, const std::string& label, Icon icon, ButtonStyle style, float w) {
        bool b = ui.button(key, label, {bx, act.y, w, act.h}, style, true, (int)icon);
        bx += w + 8;
        return b;
    };
    if (actBtn("r-pdf", "PDF", Icon::Export, ButtonStyle::Primary, 92)) app.exportResult(*r, 0);
    if (actBtn("r-json", "JSON", Icon::Export, ButtonStyle::Secondary, 96)) app.exportResult(*r, 1);
    if (actBtn("r-csv", "CSV", Icon::Export, ButtonStyle::Secondary, 90)) app.exportResult(*r, 2);
    bool isCmp = app.compareResult == r->id;
    if (actBtn("r-cmp", isCmp ? T("Karşılaştırmadan çıkar") : T("Karşılaştırmaya ekle"), Icon::Compare, ButtonStyle::Secondary, 200))
        app.compareResult = isCmp ? 0 : r->id;
    if (!r->screenshotPath.empty() && fileExists(widen(r->screenshotPath)))
        if (actBtn("r-shot", T("Görüntü"), Icon::Camera, ButtonStyle::Ghost, 104)) app.openFolder(widen(r->screenshotPath));
    if (ui.iconButton("r-del", Icon::Trash, {act.right() - 38, act.y, 38, 38}, T("Sonucu sil"))) { app.pendingDelete = r->id; app.openModal(Modal::ConfirmDelete); }
    d.cutTop(22);

    // score hero
    if (bench) {
        Rect hero = d.cutTop(110);
        c.fillRound(hero, 14, FG(0.025f));
        c.strokeRound(hero, 14, t.border, 1);
        c.pushClip(hero);
        c.fillRadial(hero.x + 80, hero.cy(), 220, 120, t.perf.alpha(0.08f), t.perf.alpha(0));
        c.popClip();
        Rect hi = hero.inset(22, 16);
        ui.caps(T("FurUltimate skoru"), {hi.x, hi.y, 200, 16}, t.text2);
        TextStyle sc; sc.size = 52; sc.weight = Weight::SemiBold; sc.display = true;
        c.text(r->status == "completed" && r->fullRun && r->score > 0 ? format("%.0f", r->score) : "—", {hi.x, hi.y + 16, 240, 62}, t.text, sc);
        if (r->aiScore > 0) {
            float ax = hi.right() - 230;
            ui.caps(T("AI skoru"), {ax, hi.y, 230, 16}, t.text2, Align::Trailing);
            ui.label(format("%.0f", r->aiScore), {ax, hi.y + 16, 230, 34}, t.perf, 28, Weight::SemiBold, Align::Trailing, true);
            ui.label(r->aiTheoreticalTops > 0 ? format("%.1f TOPS · %s %.0f TOPS", r->aiTops, T("teorik"), r->aiTheoreticalTops)
                                              : format("%.1f TOPS · %s", r->aiTops, r->aiPath.c_str()),
                     {ax - 40, hi.y + 52, 270, 18}, t.text3, 11.5f, Weight::Regular, Align::Trailing);
        }
        ui.label(r->preset, {hi.x + 250, hi.y + 4, hi.w - 250, 18}, t.text2, 12.5f, Weight::SemiBold);
        ui.label(r->configKey, {hi.x + 250, hi.y + 24, hi.w - 500, 18}, t.text3, 11.5f);
        if (!r->fullRun) ui.label(T("Tek test çalıştırması — genel skor yok"), {hi.x + 250, hi.y + 44, hi.w - 500, 18}, t.text2, 11.5f);
        if (cmp) {
            bool comparable = cmp->configKey == r->configKey && cmp->kind == r->kind;
            if (comparable && cmp->score > 0 && r->score > 0) {
                double dlt = (r->score - cmp->score) / cmp->score * 100.0;
                ui.label(format("%+.1f %% %s #%lld", dlt, T("karşılaştırmaya göre"), (long long)cmp->id), {hi.x + 250, hi.y + 50, hi.w - 250, 22},
                         dlt >= 0 ? t.success : t.critical, 14, Weight::SemiBold);
            } else {
                ui.label(T("Karşılaştırılamaz: sürüm veya ayarlar farklı"), {hi.x + 250, hi.y + 50, hi.w - 250, 22}, t.warning, 12.5f, Weight::Medium);
            }
        }
        d.cutTop(20);
    }
    metricGrid(app, d, *r);
    d.cutTop(14);

    // subtests
    if (!r->subs.empty()) {
        ui.label(T("Testler"), d.cutTop(24), t.text, 15, Weight::SemiBold);
        d.cutTop(6);
        double mx = 1;
        for (auto& s : r->subs) mx = std::max(mx, s.score);
        bool comparable = cmp && cmp->configKey == r->configKey;
        for (auto& s : r->subs) {
            Rect row = d.cutTop(46);
            ui.label(T(s.name.c_str()), {row.x, row.y, row.w * 0.4f, 20}, t.text, 13, Weight::Medium);
            std::string v = formatMeasurement(s);
            float rx = row.right() - 60;
            if (comparable) {
                for (auto& o : cmp->subs) if (o.id == s.id && normalizedValue(o) > 0) {
                    double dl = (normalizedValue(s) - normalizedValue(o)) / normalizedValue(o) * 100;
                    ui.label(format("%+.1f %%", dl), {rx - 210, row.y, 80, 20}, dl >= 0 ? t.success : t.critical, 12, Weight::SemiBold, Align::Trailing);
                }
            }
            ui.label(v, {rx - 130, row.y, 124, 20}, t.text2, 12.5f, Weight::Regular, Align::Trailing);
            ui.label(format("%.0f", s.score), {rx, row.y, 60, 20}, t.text, 13, Weight::SemiBold, Align::Trailing);
            ui.progressBar({row.x, row.y + 28, row.w, 4}, (float)(s.score / mx), t.perf, false);
        }
        d.cutTop(10);
    }

    // timeline
    if (r->fpsTimeline.size() >= 2) {
        ui.label(bench ? T("Grafik testi FPS zaman çizelgesi") : T("FPS ve sıcaklık zaman çizelgesi"), d.cutTop(24), t.text, 15, Weight::SemiBold);
        Rect cr = d.cutTop(220);
        std::vector<ChartSeries> ser;
        ser.push_back({r->fpsTimeline, t.perf, "FPS", "", true});
        if (cmp && cmp->configKey == r->configKey && cmp->fpsTimeline.size() >= 2)
            ser.push_back({cmp->fpsTimeline, t.text2, format("#%lld", (long long)cmp->id), "FPS", false});
        if (!bench && !r->tempTimeline.empty()) {
            std::vector<float> tt;
            for (float v : r->tempTimeline) tt.push_back(std::isnan(v) ? NA : (float)displayTemp(v, app.settings.tempUnit));
            ser.push_back({tt, t.warning, T("Sıcaklık"), tempUnitSuffix(app.settings.tempUnit), false});
        }
        ChartOptions o; o.minY = 0; o.spanSeconds = (float)r->fpsTimeline.size(); o.grid = app.settings.chartGrid;
        lineChart(ui, "res-chart", cr, ser, o);
        d.cutTop(10);
    }
    TextStyle ns; ns.size = 11.5f; ns.wrap = true;
    c.text(T("Tek bir sonuç GPU'nun sağlıklı veya arızalı olduğunu göstermez. Karşılaştırmalar yalnızca aynı benchmark sürümü ve ayarlarıyla yapılır."),
           d.cutTop(36), t.text3, ns);
    ui.endScroll(d.y - top + 30);
}

} // namespace aether
