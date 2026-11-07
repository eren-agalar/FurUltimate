#include "../App.h"
#include "../../core/Strings.h"
#include "../../core/I18n.h"
#include "../../core/Paths.h"
#include "../../core/Log.h"
#include "../../net/Leaderboard.h"

namespace aether {

struct SettingsLayout {
    App& app;
    Rect in;
    bool changed = false;
    Rect row(const std::string& label, const std::string& hint, float controlW) {
        const Theme& t = Theme::current();
        Rect r = in.cutTop(hint.empty() ? 44 : 54);
        app.ui.label(label, {r.x, r.y + (hint.empty() ? 12 : 8), r.w - controlW - 16, 20}, t.text, 13.5f, Weight::Medium);
        if (!hint.empty()) app.ui.label(hint, {r.x, r.y + 28, r.w - controlW - 16, 18}, t.text3, 11.5f);
        app.ui.c.line(r.x, r.bottom(), r.right(), r.bottom(), FG(0.04f), 1);
        return {r.right() - controlW, r.cy() - 17, controlW, 34};
    }
};

static Rect settingsCard(App& app, Rect& col, const std::string& title, Icon icon, float h) {
    const Theme& t = Theme::current();
    Rect card = col.cutTop(h);
    col.cutTop(16);
    app.ui.card(card);
    Rect in = card.inset(22, 18);
    drawIcon(app.ui.c, icon, in.x, in.y + 2, 18, t.perf, 1.5f);
    app.ui.label(title, {in.x + 28, in.y, in.w, 22}, t.text, 15, Weight::SemiBold);
    in.cutTop(30);
    return in;
}

void drawSettingsPage(App& app, Rect area) {
    const Theme& t = Theme::current();
    UI& ui = app.ui;
    Settings& s = app.settings;
    pageHeader(app, area, T("Ayarlar"), T("Değişiklikler otomatik olarak kaydedilir"));
    float scroll = ui.beginScroll("set-scroll", area);
    Rect a{area.x, area.y - scroll, area.w - 8, 10000};
    float top = a.y;
    bool two = a.w > 1150;
    Rect L = two ? Rect{a.x, a.y, a.w / 2 - 8, 10000} : a;
    Rect R = two ? Rect{a.x + a.w / 2 + 8, a.y, a.w / 2 - 8, 10000} : a;
    Rect& R2 = two ? R : L;
    Settings before = s;

    { // General
        SettingsLayout lay{app, settingsCard(app, L, T("Genel"), Icon::Settings, 290)};
        int lang = s.language == "en" ? 1 : 0;
        if (ui.segmented("s-lang", lay.row(T("Dil"), T("Arayüz dili (dil seçimi mimarisi)"), 220), {"Türkçe", "English"}, lang))
            s.language = lang == 1 ? "en" : "tr";
        static const char* pages[] = {"GPU Stres Testi", "Benchmark", "Liderlik Tablosu", "Sonuçlar", "Donanım", "Ayarlar"};
        Rect pr = lay.row(T("Başlangıç sayfası"), "", 260);
        {
            // stepper: < name >
            Rect l{pr.x, pr.y, 34, 34}, r{pr.right() - 34, pr.y, 34, 34};
            if (ui.button("s-pg-prev", "‹", l, ButtonStyle::Secondary)) s.startPage = (s.startPage + 5) % 6;
            if (ui.button("s-pg-next", "›", r, ButtonStyle::Secondary)) s.startPage = (s.startPage + 1) % 6;
            ui.label(T(pages[s.startPage]), {pr.x + 40, pr.y, pr.w - 80, 34}, t.text, 13, Weight::SemiBold, Align::Center);
        }
        ui.toggle("s-max", lay.row(T("Büyütülmüş başlat"), T("Uygulama penceresi tam boyutta açılır"), 60), s.startMaximized);
        ui.toggle("s-mon", lay.row(T("Başlangıçta izlemeyi başlat"), T("Telemetri örneklemesi uygulama açılınca başlar"), 60), s.monitorOnStartup);
    }
    { // Appearance: theme gallery with live colour previews
        float rows = 2;
        Rect in = settingsCard(app, R2, T("Görünüm"), Icon::Overview, 60 + 28 + rows * 96 + 120);
        ui.label(T("Tema"), in.cutTop(22), t.text, 13.5f, Weight::Medium);
        in.cutTop(6);
        int perRow = 4;
        float gap = 10, tw = (in.w - gap * (perRow - 1)) / perRow, th = 86;
        for (int i = 0; i < Theme::kCount; ++i) {
            const Theme& th2 = Theme::get(i);
            Rect tile{in.x + (i % perRow) * (tw + gap), in.y + (i / perRow) * (th + gap), tw, th};
            bool sel = s.theme == i;
            uint64_t k = ui.id(format("theme%d", i));
            if (ui.pressedOn(k, tile)) s.theme = i;
            float h = ui.anim(k ^ 0x5, ui.hovered(tile) ? 1.0f : 0.0f, 18);
            // Mini preview of the palette: background, surface card, accent and text lines.
            Rect pv{tile.x + 6, tile.y + 6, tile.w - 12, 48};
            ui.c.fillRound(pv, 8, th2.bg);
            ui.c.fillRound({pv.x + 6, pv.y + 6, pv.w * 0.30f, pv.h - 12}, 4, th2.sidebar);
            Rect card{pv.x + pv.w * 0.30f + 10, pv.y + 6, pv.w * 0.70f - 16, pv.h - 12};
            ui.c.fillRound(card, 5, th2.surface);
            ui.c.fillRound({card.x + 6, card.y + 6, card.w * 0.55f, 4}, 2, th2.text);
            ui.c.fillRound({card.x + 6, card.y + 14, card.w * 0.35f, 3}, 1.5f, th2.text2);
            ui.c.fillRound({card.x + 6, card.bottom() - 10, card.w - 12, 4}, 2, th2.perf);
            ui.c.fillCircle(pv.x + pv.w * 0.15f + 3, pv.y + 14, 4, th2.perf);
            ui.label(T(th2.name), {tile.x + 8, tile.y + 58, tile.w - 16, 22}, sel ? t.text : t.text2, 12.5f, sel ? Weight::SemiBold : Weight::Medium);
            if (sel) {
                ui.c.strokeRound(tile, 12, t.perf, 2);
                drawIcon(ui.c, Icon::Check, tile.right() - 24, tile.y + 60, 16, t.perf, 1.8f);
            } else {
                ui.c.strokeRound(tile, 12, mix(t.border, t.borderStrong, h), 1);
            }
            ui.focusRing(tile, 12, k);
        }
        in.cutTop(rows * (th + gap) + 4);
        SettingsLayout lay{app, in};
        ui.toggle("s-motion", lay.row(T("Hareketi azalt"), T("Animasyonları ve geçişleri kapatır"), 60), s.reduceMotion);
        ui.toggle("s-grid", lay.row(T("Grafik ızgarası"), "", 60), s.chartGrid);
    }
    { // Monitoring
        SettingsLayout lay{app, settingsCard(app, L, T("Telemetri"), Icon::Monitoring, 182)};
        static const int ms[] = {250, 500, 1000, 2000};
        int idx = s.refreshMs <= 250 ? 0 : s.refreshMs <= 500 ? 1 : s.refreshMs <= 1000 ? 2 : 3;
        if (ui.segmented("s-ref", lay.row(T("Yenileme aralığı"), T("Daha kısa aralık biraz daha fazla CPU kullanır"), 300), {"250 ms", "500 ms", "1 s", "2 s"}, idx))
            s.refreshMs = ms[idx];
        int tu = (int)s.tempUnit;
        if (ui.segmented("s-unit", lay.row(T("Sıcaklık birimi"), "", 160), {"°C", "°F"}, tu)) s.tempUnit = (TempUnit)tu;
    }
    { // FPS display + benchmark
        SettingsLayout lay{app, settingsCard(app, R2, T("FPS gösterimi ve benchmark"), Icon::Benchmark, 236)};
        ui.toggle("s-ovl", lay.row(T("Test penceresinde FPS katmanı"), T("FPS, sıcaklık ve süre bilgisini gösterir"), 60), s.showFpsOverlay);
        ui.toggle("s-ovlft", lay.row(T("Kare süresini göster"), T("Kare ve GPU süresi (ms)"), 60), s.showFrameTimeInOverlay);
        if (ui.toggle("s-bfs", lay.row(T("Benchmark varsayılan tam ekran"), "", 60), s.benchFullscreen)) app.benchCfg.fullscreen = s.benchFullscreen;
    }
    { // Safety
        SettingsLayout lay{app, settingsCard(app, L, T("Güvenlik"), Icon::Shield, 392)};
        Rect r = lay.row(T("Sıcaklık uyarı eşiği"), T("Aşıldığında uyarı gösterilir"), 300);
        ui.label(fmtTemp((float)s.warnTempC, (int)s.tempUnit), {r.right() - 64, r.y, 64, r.h}, t.text, 13, Weight::SemiBold, Align::Trailing);
        ui.sliderInt("s-warn", {r.x, r.y, r.w - 72, r.h}, s.warnTempC, 60, 100, 1);
        r = lay.row(T("Acil durdurma eşiği"), T("Aşıldığında test anında durdurulur"), 300);
        ui.label(fmtTemp((float)s.stopTempC, (int)s.tempUnit), {r.right() - 64, r.y, 64, r.h}, t.critical, 13, Weight::SemiBold, Align::Trailing);
        ui.sliderInt("s-stop", {r.x, r.y, r.w - 72, r.h}, s.stopTempC, 65, 105, 1);
        r = lay.row(T("Varsayılan stres süresi"), "", 300);
        ui.label(format("%d %s", s.defaultStressMinutes, T("dk")), {r.right() - 64, r.y, 64, r.h}, t.text, 13, Weight::SemiBold, Align::Trailing);
        ui.sliderInt("s-dmin", {r.x, r.y, r.w - 72, r.h}, s.defaultStressMinutes, 1, 120, 1);
        ui.toggle("s-conf", lay.row(T("Yoğun testlerden önce onay iste"), "", 60), s.confirmIntensive);
        ui.toggle("s-tdr", lay.row(T("GPU zaman aşımı koruması"), T("Tek kare 400 ms'yi aşarsa stres yükünü otomatik azaltır"), 60), s.tdrGuard);
        if (s.stopTempC <= s.warnTempC) s.stopTempC = std::min(110, s.warnTempC + 5);
    }
    { // Leaderboard
        SettingsLayout lay{app, settingsCard(app, L, T("Liderlik tablosu"), Icon::Trophy, 250)};
        bool configured = LeaderboardClient::instance().configured();
        ui.toggle("s-lb", lay.row(T("Skorlarımı liderlik tablosuna gönder"),
                                  configured ? T("Yalnızca kişisel rekorunuzu geçen skorlar gönderilir") : T("Sunucu yapılandırılmamış (leaderboard.json)"), 60),
                  s.leaderboardEnabled, configured);
        Rect nr = lay.row(T("Takma ad"), T("2–20 karakter; liderlik tablosunda görünür"), 220);
        std::string nick = s.nickname;
        ui.textField("s-nick", nr, nick, 20, T("örn. NovaRider"), configured);
        if (nick != s.nickname) s.nickname = nick;
        ui.label(format("%s: %s", T("Oyuncu kimliği"), s.playerId.substr(0, 8).c_str()) + "…", lay.in.cutTop(24), t.text3, 11.5f);
        if (s.leaderboardEnabled) s.leaderboardAsked = true;
    }
    { // Data
        SettingsLayout lay{app, settingsCard(app, R2, T("Veri depolama"), Icon::Folder, 330)};
        Rect r = lay.row(T("Veri klasörü"), narrow(Paths::root()), 150);
        if (ui.button("s-open", T("Klasörü aç"), r, ButtonStyle::Secondary, true, (int)Icon::Folder)) app.openFolder(Paths::root());
        r = lay.row(T("Ekran görüntüleri"), narrow(Paths::screenshots()), 150);
        if (ui.button("s-shots", T("Klasörü aç"), r, ButtonStyle::Secondary, true, (int)Icon::Folder)) app.openFolder(Paths::screenshots());
        r = lay.row(T("Günlük dosyaları"), format(T("%d gün saklanır"), s.keepLogDays), 150);
        if (ui.button("s-logs", T("Klasörü aç"), r, ButtonStyle::Secondary, true, (int)Icon::Folder)) app.openFolder(Paths::logs());
        r = lay.row(T("Sonuç geçmişi"), format(T("%d kayıt · SQLite"), app.db.count()), 150);
        if (ui.button("s-clear", T("Geçmişi sil"), r, ButtonStyle::Danger, app.db.count() > 0, (int)Icon::Trash)) app.openModal(Modal::ConfirmClearHistory);
        r = lay.row(T("Varsayılanlara dön"), T("Tüm ayarları ilk değerlerine sıfırlar"), 150);
        if (ui.button("s-reset", T("Sıfırla"), r, ButtonStyle::Ghost)) { s = Settings{}; ui.toast(T("Ayarlar varsayılanlara döndü"), ToastKind::Info); }
    }
    float endY = std::max(L.y, R.y);
    ui.endScroll(endY - top + 10);

    bool changed = before.language != s.language || before.startPage != s.startPage || before.startMaximized != s.startMaximized ||
                   before.monitorOnStartup != s.monitorOnStartup || before.theme != s.theme || before.reduceMotion != s.reduceMotion ||
                   before.chartGrid != s.chartGrid || before.refreshMs != s.refreshMs || before.tempUnit != s.tempUnit ||
                   before.chartWindowSec != s.chartWindowSec || before.showFpsOverlay != s.showFpsOverlay ||
                   before.showFrameTimeInOverlay != s.showFrameTimeInOverlay || before.benchFullscreen != s.benchFullscreen ||
                   before.warnTempC != s.warnTempC || before.stopTempC != s.stopTempC || before.defaultStressMinutes != s.defaultStressMinutes ||
                   before.confirmIntensive != s.confirmIntensive || before.tdrGuard != s.tdrGuard || before.keepLogDays != s.keepLogDays ||
                   before.leaderboardEnabled != s.leaderboardEnabled || before.nickname != s.nickname || before.leaderboardAsked != s.leaderboardAsked;
    if (changed) {
        s.clamp();
        app.applySettings();
        app.markSettingsChanged();
    }
}

} // namespace aether
