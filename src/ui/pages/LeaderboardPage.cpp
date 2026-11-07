#include "../App.h"
#include "../../net/Leaderboard.h"
#include "../../core/Strings.h"
#include "../../core/I18n.h"
#include <cmath>

namespace aether {

static void emptyState(App& app, const Rect& r, Icon icon, const std::string& title, const std::string& body) {
    const Theme& t = Theme::current();
    UI& ui = app.ui;
    ui.card(r);
    drawIcon(ui.c, icon, r.cx() - 20, r.y + 48, 40, t.text3, 1.4f);
    ui.label(title, {r.x + 24, r.y + 104, r.w - 48, 26}, t.text, 17, Weight::SemiBold, Align::Center);
    TextStyle bs; bs.size = 13; bs.wrap = true; bs.align = Align::Center; bs.valign = Align::Leading;
    ui.c.text(body, {r.x + 48, r.y + 140, r.w - 96, r.h - 160}, t.text2, bs);
}

void drawLeaderboardPage(App& app, Rect area) {
    const Theme& t = Theme::current();
    UI& ui = app.ui;
    Canvas& c = ui.c;
    auto& lb = LeaderboardClient::instance();
    pageHeader(app, area, T("Liderlik Tablosu"), T("Dünya genelindeki FurUltimate sonuçları · ekran kartları ve oyuncular"));

    if (!lb.configured()) {
        emptyState(app, area.cutTop(std::min(360.0f, area.h)), Icon::Globe, T("Liderlik sunucusu yapılandırılmamış"),
                   T("Bu kopyada çevrimiçi liderlik sunucusu tanımlı değil. Dağıtımı yapan kişi, sunucu adresini ve genel anahtarı içeren leaderboard.json dosyasını uygulama klasörüne eklemelidir (bkz. server/leaderboard/README.md). Uygulamanın diğer tüm özellikleri çevrimdışı çalışır."));
        return;
    }
    app.refreshLeaderboard(false);
    auto snap = lb.snapshot();

    // Controls
    Rect bar = area.cutTop(40);
    int board = app.lbBoard;
    if (ui.segmented("lb-board", {bar.x, bar.y, 440, 36}, {"1080p", "1440p", "2160p", T("Yapay Zekâ")}, board)) {
        app.lbBoard = board;
        app.refreshLeaderboard(true);
    }
    ui.segmented("lb-view", {bar.x + 456, bar.y, 260, 36}, {T("Oyuncular"), T("Ekran kartları")}, app.lbView);
    if (ui.iconButton("lb-refresh", Icon::Refresh, {bar.right() - 36, bar.y, 36, 36}, T("Yenile"), !snap.loading)) app.refreshLeaderboard(true);
    if (snap.loading) ui.label(T("Yükleniyor…"), {bar.right() - 200, bar.y, 156, 36}, t.text3, 12, Weight::Regular, Align::Trailing);
    area.cutTop(14);

    bool stack = area.w < 1050;
    Rect side = stack ? area.cutBottom(240) : area.cutRight(std::min(380.0f, area.w * 0.32f));
    if (stack) area.cutBottom(16); else area.cutRight(18);
    Rect table = area;
    bool ai = app.lbBoard == 3;
    std::string myGpu = app.gpu() ? narrow(app.gpu()->adapter.name) : "";

    // ---------------- main table
    ui.card(table);
    Rect ti = table.inset(18, 16);
    Rect head = ti.cutTop(24);
    float colRank = 44, colScore = 110, colExtra = ai ? 90 : 0;
    auto cols = [&](const Rect& r, float& xName, float& wName, float& xGpu, float& wGpu) {
        float rest = r.w - colRank - colScore - colExtra;
        xName = r.x + colRank; wName = app.lbView == 0 ? rest * 0.38f : 0;
        xGpu = xName + wName; wGpu = rest - wName;
    };
    float xn, wn, xg, wg;
    cols(head, xn, wn, xg, wg);
    ui.caps("#", {head.x, head.y, colRank, 18}, t.text3);
    if (app.lbView == 0) ui.caps(T("Oyuncu"), {xn, head.y, wn, 18}, t.text3);
    ui.caps(T("Ekran kartı"), {xg, head.y, wg, 18}, t.text3);
    if (ai) ui.caps("TOPS", {head.right() - colScore - colExtra, head.y, colExtra - 10, 18}, t.text3, Align::Trailing);
    ui.caps(app.lbView == 0 ? T("Skor") : T("Ortalama skor"), {head.right() - colScore, head.y, colScore, 18}, t.text3, Align::Trailing);
    ti.cutTop(6);
    c.line(ti.x, ti.y, ti.right(), ti.y, t.border, 1);
    ti.cutTop(4);

    if (!snap.error.empty() && !snap.loading) {
        TextStyle es; es.size = 13; es.align = Align::Center; es.wrap = true;
        c.text(std::string(T("Liderlik tablosu yüklenemedi: ")) + snap.error, {ti.x + 20, ti.y + 40, ti.w - 40, 60}, t.warning, es);
    } else if (snap.loaded && ((app.lbView == 0 && snap.players.empty()) || (app.lbView == 1 && snap.gpus.empty()))) {
        TextStyle es; es.size = 13; es.align = Align::Center; es.wrap = true;
        c.text(T("Bu panoda henüz sonuç yok. İlk sırayı almak için tam bir benchmark çalıştırın."), {ti.x + 20, ti.y + 40, ti.w - 40, 60}, t.text3, es);
    } else {
        float sc = ui.beginScroll("lb-table", ti);
        float y = ti.y - sc, y0 = y;
        auto medal = [&](int rank) { return rank == 1 ? Color::hex(0xE8C76A) : rank == 2 ? Color::hex(0xC8D0DA) : rank == 3 ? Color::hex(0xD49A6A) : t.text3; };
        if (app.lbView == 0) {
            for (auto& e : snap.players) {
                Rect row{ti.x, y, ti.w - 6, 40};
                y += 42;
                if (row.bottom() < ti.y || row.y > ti.bottom()) continue;
                if (e.mine) { c.fillRound(row, 10, t.perf.alpha(0.10f)); c.strokeRound(row, 10, t.perf.alpha(0.35f), 1); }
                else if (e.gpu == myGpu) c.fillRound(row, 10, FG(0.025f));
                ui.label(format("%d", e.rank), {row.x + 6, row.y, colRank - 6, row.h}, medal(e.rank), 14, Weight::SemiBold, Align::Leading, true);
                float a, b2, gx, gw2; cols(row, a, b2, gx, gw2);
                ui.label(e.nickname + (e.mine ? std::string(" · ") + T("Siz") : ""), {a, row.y, b2 - 8, row.h}, e.mine ? t.perf : t.text, 13.5f, Weight::Medium);
                ui.label(e.gpu, {gx, row.y, gw2 - 8, row.h}, t.text2, 12.5f);
                if (ai) ui.label(format("%.1f", e.tops), {row.right() - colScore - colExtra, row.y, colExtra - 10, row.h}, t.text2, 12.5f, Weight::Regular, Align::Trailing);
                ui.label(format("%.0f", e.score), {row.right() - colScore, row.y, colScore - 6, row.h}, t.text, 15, Weight::SemiBold, Align::Trailing, true);
            }
        } else {
            double top = snap.gpus.empty() ? 1 : std::max(1.0, snap.gpus.front().avgScore);
            for (auto& g : snap.gpus) {
                Rect row{ti.x, y, ti.w - 6, 46};
                y += 48;
                if (row.bottom() < ti.y || row.y > ti.bottom()) continue;
                bool mineGpu = g.gpu == myGpu;
                if (mineGpu) { c.fillRound(row, 10, t.perf.alpha(0.10f)); c.strokeRound(row, 10, t.perf.alpha(0.35f), 1); }
                ui.label(format("%d", g.rank), {row.x + 6, row.y, colRank - 6, row.h}, medal(g.rank), 14, Weight::SemiBold, Align::Leading, true);
                float a, b2, gx, gw2; cols(row, a, b2, gx, gw2);
                ui.label(g.gpu + (mineGpu ? std::string(" · ") + T("Sizin kartınız") : ""), {gx, row.y + 4, gw2 - 8, 20}, mineGpu ? t.perf : t.text, 13.5f, Weight::Medium);
                ui.label(format(T("%d oyuncu · en iyi %.0f"), g.players, g.bestScore), {gx, row.y + 23, gw2 - 8, 18}, t.text3, 11.5f);
                ui.progressBar({gx + gw2 * 0.55f, row.y + 29, gw2 * 0.40f, 4}, (float)(g.avgScore / top), t.perf, false);
                ui.label(format("%.0f", g.avgScore), {row.right() - colScore, row.y, colScore - 6, row.h}, t.text, 15, Weight::SemiBold, Align::Trailing, true);
            }
        }
        ui.endScroll(y - y0);
    }

    // ---------------- side: my GPU / participation
    ui.card(side);
    Rect si = side.inset(20, 18);
    drawIcon(c, Icon::Chip, si.x, si.y + 2, 18, t.perf, 1.5f);
    ui.label(T("Benim kartım"), {si.x + 28, si.y, si.w - 28, 22}, t.text, 15, Weight::SemiBold);
    si.cutTop(34);
    ui.label(myGpu.empty() ? "N/A" : myGpu, si.cutTop(22), t.text, 14, Weight::SemiBold);
    si.cutTop(8);
    const GpuRankingEntry* mine = nullptr;
    for (auto& g : snap.gpus) if (g.gpu == myGpu) { mine = &g; break; }
    auto stat = [&](const std::string& k, const std::string& v, const Color& col) {
        Rect r = si.cutTop(50);
        ui.caps(k, {r.x, r.y, r.w, 16}, t.text3);
        ui.label(v, {r.x, r.y + 16, r.w, 30}, col, 20, Weight::SemiBold, Align::Leading, true);
    };
    if (mine) {
        stat(T("Kart ortalaması"), format("%.0f", mine->avgScore), t.text);
        int n = (int)snap.gpus.size();
        stat(T("Kart sıralaması"), format("#%d / %d", mine->rank, n), t.text);
        double pct = n > 1 ? 100.0 * (n - mine->rank) / (n - 1) : 100.0;
        stat(T("Yüzdelik dilim"), format(T("Kartların %%%.0f'inden hızlı"), pct), t.perf);
    } else {
        TextStyle ns; ns.size = 12.5f; ns.wrap = true; ns.valign = Align::Leading;
        c.text(T("Bu kart için bu panoda henüz sonuç yok."), si.cutTop(40), t.text3, ns);
    }
    for (auto& m : snap.mine)
        if (m.board == snap.board && m.gpu == myGpu) {
            stat(T("Kişisel rekorum"), format("%.0f · #%d / %d", m.score, m.rank, m.total), t.success);
            break;
        }
    // Participation
    Rect part = si.cutBottom(app.settings.leaderboardEnabled ? 62 : 120);
    c.line(part.x, part.y - 8, part.right(), part.y - 8, t.border, 1);
    if (app.settings.leaderboardEnabled) {
        ui.label(std::string(T("Takma ad: ")) + app.settings.nickname, {part.x, part.y + 4, part.w, 20}, t.text2, 12.5f);
        ui.label(T("Yalnızca kişisel rekorunuzu geçen skorlar gönderilir."), {part.x, part.y + 26, part.w, 18}, t.text3, 11.5f);
    } else {
        ui.label(T("Katılmak için takma ad girin"), {part.x, part.y + 2, part.w, 18}, t.text2, 12.5f);
        bool enter = ui.textField("lb-nick-side", {part.x, part.y + 26, part.w, 38}, app.lbNickDraft, 20, T("örn. NovaRider"));
        bool ok = trim(app.lbNickDraft).size() >= 2;
        if ((ui.button("lb-join", T("Liderlik tablosuna katıl"), {part.x, part.y + 72, part.w, 38}, ButtonStyle::Primary, ok) || (enter && ok))) {
            app.settings.nickname = trim(app.lbNickDraft);
            app.settings.leaderboardEnabled = true;
            app.settings.leaderboardAsked = true;
            app.markSettingsChanged();
            app.refreshLeaderboard(true);
            ui.toast(T("Liderlik tablosuna katıldınız"), ToastKind::Success);
        }
    }
}

} // namespace aether
