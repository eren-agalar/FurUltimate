#include "ReportExporter.h"
#include "PdfWriter.h"
#include "../resource.h"
#include "../ui/Fonts.h"
#include "../core/FileIO.h"
#include "../core/Strings.h"
#include "../core/I18n.h"
#include "../bench/Score.h"
#include <cmath>

namespace aether {

static std::string naOr(double v, const char* fmt) { return std::isnan(v) ? std::string("N/A") : format(fmt, v); }

bool ReportExporter::exportJson(const BenchmarkResult& r, const std::wstring& path, std::string& error) {
    std::string j = resultToJson(r, 2);
    if (!writeFileAtomic(path, j)) { error = T("JSON dosyası yazılamadı"); return false; }
    return true;
}

std::string ReportExporter::csvEscape(const std::string& s) {
    if (s.find_first_of(",\"\n\r") == std::string::npos) return s;
    std::string o = "\"";
    for (char c : s) { if (c == '"') o += '"'; o += c; }
    return o + "\"";
}

std::string ReportExporter::csv(const BenchmarkResult& r) {
    std::string o = "\xEF\xBB\xBF";   // UTF-8 BOM so spreadsheet apps detect the encoding
    auto row = [&](const std::string& k, const std::string& v) { o += csvEscape(k) + "," + csvEscape(v) + "\r\n"; };
    o += "section,key,value\r\n";
    auto srow = [&](const char* sec, const std::string& k, const std::string& v) { o += std::string(sec) + "," + csvEscape(k) + "," + csvEscape(v) + "\r\n"; };
    (void)row;
    srow("summary", "application", "FurUltimate");
    srow("summary", "id", std::to_string(r.id));
    srow("summary", "kind", r.kind);
    srow("summary", "timestamp", r.timestamp);
    srow("summary", "gpu", r.gpuName);
    srow("summary", "vendor", r.gpuVendor);
    srow("summary", "driver_version", r.driverVersion);
    srow("summary", "benchmark_version", r.benchVersion);
    srow("summary", "preset", r.preset);
    srow("summary", "config_key", r.configKey);
    srow("summary", "resolution", format("%dx%d", r.width, r.height));
    srow("summary", "duration_sec", format("%.2f", r.durationSec));
    srow("summary", "avg_fps", format("%.2f", r.avgFps));
    srow("summary", "min_fps", format("%.2f", r.minFps));
    srow("summary", "max_fps", format("%.2f", r.maxFps));
    srow("summary", "one_percent_low_fps", format("%.2f", r.p1LowFps));
    srow("summary", "avg_frame_ms", format("%.3f", r.avgFrameMs));
    srow("summary", "p99_frame_ms", format("%.3f", r.p99FrameMs));
    srow("summary", "frame_ms_stddev", format("%.3f", r.stdFrameMs));
    srow("summary", "avg_gpu_temp_c", naOr(r.avgTemp, "%.1f"));
    srow("summary", "max_gpu_temp_c", naOr(r.maxTemp, "%.1f"));
    srow("summary", "avg_gpu_utilization_pct", naOr(r.avgLoad, "%.1f"));
    srow("summary", "avg_power_w", naOr(r.avgPower, "%.1f"));
    srow("summary", "avg_core_clock_mhz", naOr(r.avgClock, "%.0f"));
    srow("summary", "score", r.kind == "benchmark" && r.status == "completed" && r.fullRun && r.score > 0 ? format("%.0f", r.score) : std::string("N/A"));
    srow("summary", "tests_run", r.testsRun.empty() ? std::string("all") : r.testsRun);
    srow("summary", "ai_score", r.aiScore > 0 ? format("%.0f", r.aiScore) : std::string("N/A"));
    srow("summary", "ai_tops", r.aiScore > 0 ? format("%.2f", r.aiTops) : std::string("N/A"));
    srow("summary", "ai_theoretical_tops", r.aiTheoreticalTops > 0 ? format("%.1f", r.aiTheoreticalTops) : std::string("N/A"));
    srow("summary", "ai_path", r.aiPath);
    srow("summary", "ai_config_key", r.aiConfigKey);
    srow("summary", "status", r.status);
    srow("summary", "status_detail", r.statusDetail);
    o += "\r\ntest,measurement,unit,sub_score,avg_fps,min_fps,max_fps,one_percent_low_fps,avg_frame_ms,p99_frame_ms,duration_sec\r\n";
    for (auto& s : r.subs)
        o += csvEscape(s.id) + "," + format("%.3f", normalizedValue(s)) + "," + csvEscape(normalizedUnit(s)) + "," + format("%.0f", s.score) + "," +
             format("%.2f,%.2f,%.2f,%.2f,%.3f,%.3f,%.2f", s.avgFps, s.minFps, s.maxFps, s.p1LowFps, s.avgFrameMs, s.p99FrameMs, s.durationSec) + "\r\n";
    o += "\r\nsecond,fps,gpu_temp_c\r\n";
    size_t n = std::max(r.fpsTimeline.size(), r.tempTimeline.size());
    for (size_t i = 0; i < n; ++i) {
        std::string f = i < r.fpsTimeline.size() ? naOr(r.fpsTimeline[i], "%.2f") : "";
        std::string t = i < r.tempTimeline.size() ? naOr(r.tempTimeline[i], "%.1f") : "";
        o += std::to_string(i + 1) + "," + f + "," + t + "\r\n";
    }
    return o;
}

bool ReportExporter::exportCsv(const BenchmarkResult& r, const std::wstring& path, std::string& error) {
    if (!writeFileAtomic(path, csv(r))) { error = T("CSV dosyası yazılamadı"); return false; }
    return true;
}

namespace {
constexpr uint32_t kInk = 0x0F1115, kInk2 = 0x4A5260, kMuted = 0x8A919C, kLine = 0xE3E6EA, kPanel = 0xF4F5F7;
constexpr uint32_t kDark = 0x111317, kDark2 = 0x20242B, kSilver = 0xC8D0DA, kAccent = 0x8FC9FF, kAccentInk = 0x2F7FC4;

void drawLogo(PdfWriter& p, double cx, double cy, double R) {
    // FurUltimate mark: gradient hexagon rim, dark badge, nested flame.
    const uint32_t rim[6] = {0xFFC24A, 0xFF8A3A, 0xFF5A2E, 0xFF2D8A, 0xB54BD0, 0x8B5CFF};
    std::vector<std::pair<double, double>> hex;
    for (int k = 0; k < 6; ++k) {
        double a = -1.5707963 + k * 1.0471976;
        hex.push_back({cx + std::cos(a) * R, cy + std::sin(a) * R});
    }
    p.setFill(0x120C1A);
    p.polygon(hex);
    for (int k = 0; k < 6; ++k) {
        p.setStroke(rim[k], R * 0.13);
        p.line(hex[k].first, hex[k].second, hex[(k + 1) % 6].first, hex[(k + 1) % 6].second);
        p.setFill(rim[k]);
        p.circle(hex[k].first, hex[k].second, R * 0.065, true);
    }
    auto flame = [&](double scale, double yOff, uint32_t col) {
        std::vector<std::pair<double, double>> pts;
        const double h = R * 0.60 * scale, w = R * 0.50 * scale, fy = cy + R * 0.10 + yOff;
        for (int i = 0; i < 40; ++i) {
            double th = i / 40.0 * 6.2831853;
            pts.push_back({cx + w * std::sin(th) * std::pow(std::fabs(std::sin(th * 0.5)), 1.5), fy - h * std::cos(th)});
        }
        p.setFill(col);
        p.polygon(pts);
    };
    flame(1.0, 0, 0xFF5A2E);
    flame(0.70, R * 0.14, 0xFFA640);
    flame(0.38, R * 0.30, 0xFFF5DA);
}
} // namespace

bool ReportExporter::exportPdf(const BenchmarkResult& r, const BenchmarkResult* cmp, const ReportContext& ctx,
                               const std::wstring& path, std::string& error) {
    PdfWriter p;
    std::vector<uint8_t> reg, semi, disp;
    if (!Fonts::fontBytes(IDR_FONT_REGULAR, reg) || !Fonts::fontBytes(IDR_FONT_SEMIBOLD, semi) || !Fonts::fontBytes(IDR_FONT_DISPLAY_SEMI, disp)) {
        error = T("Rapor fontları yüklenemedi");
        return false;
    }
    int fR = p.addFont(reg), fS = p.addFont(semi), fD = p.addFont(disp);
    if (fR < 0 || fS < 0 || fD < 0) { error = T("Rapor fontları okunamadı"); return false; }
    bool bench = r.kind == "benchmark";
    p.setInfo(std::string("FurUltimate — ") + (bench ? T("Benchmark Raporu") : T("Stres Testi Raporu")), "FurUltimate");
    const double W = PdfWriter::kPageW, M = 40;
    int page = 1;
    auto footer = [&] {
        p.setStroke(kLine, 0.6);
        p.line(M, 806, W - M, 806);
        p.setFill(kMuted);
        p.text(fR, 7.5, M, 820, std::string("FurUltimate ") + ctx.appVersion + " · " + T("Ölçümler bu cihazda gerçek GPU iş yükleriyle alınmıştır."));
        p.textRight(fR, 7.5, W - M, 820, format("%s %d", T("Sayfa"), page));
    };
    auto header = [&](const std::string& title) {
        p.setFill(kDark);
        p.rect(0, 0, W, 104);
        p.setFill(kDark2);
        p.rect(0, 104, W, 2);
        drawLogo(p, M + 16, 52, 15);
        p.setFill(0xF5F6F8);
        p.text(fS, 13, M + 42, 48, "FurUltimate");
        p.setFill(0x9198A3);
        p.text(fR, 8.5, M + 42, 62, T("Power. Precision. Performance."));
        p.setFill(0xF5F6F8);
        p.textRight(fS, 12, W - M, 48, title);
        p.setFill(0x9198A3);
        p.textRight(fR, 8.5, W - M, 62, r.timestamp.substr(0, 10) + "  " + (r.timestamp.size() >= 16 ? r.timestamp.substr(11, 5) : ""));
    };

    p.newPage();
    header(bench ? T("Benchmark Raporu") : T("Stres Testi Raporu"));
    double y = 140;

    // Hero: score or stress summary
    p.setFill(kPanel);
    p.roundRect(M, y, W - 2 * M, 112, 10);
    p.setFill(kMuted);
    p.text(fS, 8, M + 20, y + 24, bench ? T("FURULTIMATE SKORU") : T("STRES TESTİ"));
    p.setFill(kInk);
    if (bench) {
        std::string sc = r.status == "completed" && r.fullRun && r.score > 0 ? format("%.0f", r.score) : std::string("—");
        p.text(fD, 44, M + 18, y + 74, sc);
    } else {
        p.text(fD, 30, M + 18, y + 66, format("%.1f FPS", r.avgFps));
    }
    p.setFill(kInk2);
    std::string statusText = r.status == "completed" ? T("Tamamlandı") : (r.status == "aborted" ? T("Erken durduruldu") : T("Başarısız"));
    p.text(fR, 9, M + 20, y + 96, statusText + (r.statusDetail.empty() ? "" : " · " + r.statusDetail));
    double cx = r.aiScore > 0 ? M + 170 : M + 260;   // make room for the AI score block
    auto kv = [&](double x, double yy, const std::string& k, const std::string& v) {
        p.setFill(kMuted); p.text(fR, 7.5, x, yy, k);
        p.setFill(kInk); p.text(fS, 10, x, yy + 14, v);
    };
    kv(cx, y + 26, T("Sürüm"), r.benchVersion);
    kv(cx + 130, y + 26, T("Ön ayar"), r.preset);
    kv(cx, y + 66, T("Çözünürlük"), format("%d × %d", r.width, r.height));
    kv(cx + 130, y + 66, T("Süre"), formatDuration(r.durationSec));
    if (r.aiScore > 0) {
        double ax = W - M - 20;
        p.setFill(kMuted); p.textRight(fS, 8, ax, y + 24, T("AI SKORU"));
        p.setFill(kAccentInk); p.textRight(fD, 24, ax, y + 54, format("%.0f", r.aiScore));
        p.setFill(kInk2);
        p.textRight(fR, 8, ax, y + 70, r.aiTheoreticalTops > 0 ? format("%.1f TOPS · %s %.0f TOPS", r.aiTops, T("teorik"), r.aiTheoreticalTops)
                                                              : format("%.1f TOPS", r.aiTops));
        p.textRight(fR, 7.5, ax, y + 84, r.aiPath);
    }
    if (bench && !r.fullRun) { p.setFill(kInk2); p.text(fR, 8, M + 20, y + 108, T("Tek test çalıştırması — genel skor yok")); }
    y += 132;

    // System
    p.setFill(kInk); p.text(fS, 11, M, y, T("Sistem")); y += 10;
    p.setStroke(kLine, 0.6); p.line(M, y, W - M, y); y += 18;
    auto row2 = [&](const std::string& k1, const std::string& v1, const std::string& k2, const std::string& v2) {
        p.setFill(kMuted); p.text(fR, 8.5, M, y, k1); p.text(fR, 8.5, M + 258, y, k2);
        p.setFill(kInk); p.text(fS, 8.5, M + 92, y, v1.empty() ? "N/A" : v1); p.text(fS, 8.5, M + 392, y, v2.empty() ? "N/A" : v2);
        y += 16;
    };
    row2(T("GPU"), r.gpuName, T("Üretici"), r.gpuVendor);
    row2(T("Sürücü"), r.driverVersion, T("İşletim sistemi"), ctx.osName);
    row2(T("İşlemci"), ctx.cpuName, T("Bellek"), ctx.ramText);
    y += 10;

    // Tests table
    if (!r.subs.empty()) {
        p.setFill(kInk); p.text(fS, 11, M, y, bench ? T("Test sonuçları") : T("Ölçümler")); y += 10;
        p.setStroke(kLine, 0.6); p.line(M, y, W - M, y); y += 16;
        p.setFill(kMuted);
        p.text(fS, 7.5, M, y, T("TEST")); p.textRight(fS, 7.5, M + 300, y, T("ÖLÇÜM"));
        p.textRight(fS, 7.5, W - M, y, T("ALT SKOR")); y += 14;
        double maxSc = 1;
        for (auto& s : r.subs) maxSc = std::max(maxSc, s.score);
        for (auto& s : r.subs) {
            p.setFill(kInk); p.text(fS, 9.5, M, y, T(s.name.c_str()));
            std::string val = formatMeasurement(s);
            p.textRight(fR, 9.5, M + 300, y, val);
            p.setFill(0xE7EAEE); p.roundRect(M + 320, y - 7, 120, 6, 3);
            p.setFill(kAccentInk); p.roundRect(M + 320, y - 7, std::max(6.0, 120 * s.score / maxSc), 6, 3);
            p.setFill(kInk); p.textRight(fS, 9.5, W - M, y, format("%.0f", s.score));
            y += 20;
        }
        y += 6;
    }

    // Frame statistics + thermals
    p.setFill(kInk); p.text(fS, 11, M, y, T("Kare ve sıcaklık istatistikleri")); y += 10;
    p.setStroke(kLine, 0.6); p.line(M, y, W - M, y); y += 18;
    row2(T("Ortalama FPS"), format("%.1f", r.avgFps), T("Ortalama GPU sıcaklığı"), naOr(r.avgTemp, "%.1f °C"));
    row2(T("Minimum FPS"), format("%.1f", r.minFps), T("Maksimum GPU sıcaklığı"), naOr(r.maxTemp, "%.1f °C"));
    row2(T("Maksimum FPS"), format("%.1f", r.maxFps), T("Ortalama GPU kullanımı"), naOr(r.avgLoad, "%.1f %%"));
    row2(T("%1 düşük FPS"), format("%.1f", r.p1LowFps), T("Ortalama güç"), naOr(r.avgPower, "%.1f W"));
    row2(T("Kare süresi (ort.)"), format("%.2f ms", r.avgFrameMs), T("Ortalama çekirdek saati"), naOr(r.avgClock, "%.0f MHz"));
    row2(T("Kare süresi (P99)"), format("%.2f ms", r.p99FrameMs), T("Kare süresi sapması"), format("%.2f ms", r.stdFrameMs));
    y += 10;

    // FPS timeline chart
    if (r.fpsTimeline.size() >= 2 && y < 640) {
        p.setFill(kInk); p.text(fS, 11, M, y, bench ? T("Grafik testi FPS zaman çizelgesi") : T("FPS zaman çizelgesi")); y += 12;
        double cx0 = M, cw = W - 2 * M, ch = 110, cy0 = y;
        p.setFill(kPanel); p.roundRect(cx0, cy0, cw, ch, 8);
        double mx = 1;
        for (float v : r.fpsTimeline) if (!std::isnan(v)) mx = std::max(mx, (double)v);
        mx *= 1.15;
        p.setStroke(0xDDE1E6, 0.5);
        for (int g = 1; g < 4; ++g) p.line(cx0 + 10, cy0 + ch * g / 4, cx0 + cw - 10, cy0 + ch * g / 4);
        std::vector<std::pair<double, double>> pts;
        size_t n = r.fpsTimeline.size();
        for (size_t i = 0; i < n; ++i) {
            float v = r.fpsTimeline[i];
            if (std::isnan(v)) continue;
            pts.push_back({cx0 + 12 + (cw - 24) * i / (double)(n - 1), cy0 + ch - 10 - (ch - 20) * v / mx});
        }
        p.setStroke(kAccentInk, 1.4);
        p.polyline(pts);
        p.setFill(kMuted);
        p.text(fR, 7, cx0 + 12, cy0 + 12, format("%.0f FPS", mx));
        p.textRight(fR, 7, cx0 + cw - 12, cy0 + ch - 4, format("%zu s", n));
        y += ch + 18;
    }

    // Comparison
    if (cmp && y < 700) {
        p.setFill(kInk); p.text(fS, 11, M, y, T("Karşılaştırma")); y += 10;
        p.setStroke(kLine, 0.6); p.line(M, y, W - M, y); y += 16;
        if (cmp->configKey != r.configKey) {
            p.setFill(0xB07A1A);
            p.text(fR, 8.5, M, y, T("Karşılaştırma yapılmadı: benchmark sürümleri veya ayarları farklı."));
            y += 16;
        } else {
            p.setFill(kMuted);
            p.text(fR, 8.5, M, y, format("#%lld · %s · %s", (long long)cmp->id, cmp->timestamp.c_str(), cmp->gpuName.c_str()));
            y += 16;
            auto delta = [&](const std::string& k, double a, double b, const char* f) {
                double d = b != 0 ? (a - b) / b * 100.0 : 0;
                p.setFill(kInk2); p.text(fR, 8.5, M, y, k);
                p.setFill(kInk); p.textRight(fS, 8.5, M + 330, y, format(f, a));
                p.setFill(kMuted); p.textRight(fR, 8.5, M + 420, y, format(f, b));
                p.setFill(d >= 0 ? 0x2E8B57 : 0xC0504D); p.textRight(fS, 8.5, W - M, y, format("%+.1f %%", d));
                y += 15;
            };
            if (bench) delta(T("Skor"), r.score, cmp->score, "%.0f");
            delta(T("Ortalama FPS"), r.avgFps, cmp->avgFps, "%.1f");
            delta(T("%1 düşük FPS"), r.p1LowFps, cmp->p1LowFps, "%.1f");
            for (auto& s : r.subs)
                for (auto& o : cmp->subs)
                    if (o.id == s.id) delta(T(s.name.c_str()), normalizedValue(s), normalizedValue(o), "%.2f");
        }
        y += 6;
    }

    // Disclaimer
    double dy = std::max(y + 4, 742.0);
    if (dy > 780) dy = 780;
    p.setFill(kPanel); p.roundRect(M, dy - 14, W - 2 * M, 44, 6);
    p.setFill(kInk2);
    p.text(fR, 7.5, M + 12, dy + 2, T("Bu rapor tek bir ölçüm oturumunu yansıtır ve bir donanım teşhisi değildir. Tek bir skor, GPU'nun sağlıklı"));
    p.text(fR, 7.5, M + 12, dy + 13, T("veya arızalı olduğunu göstermez. Skorlar yalnızca aynı benchmark sürümü ve ayarlarıyla karşılaştırılabilir."));
    p.text(fR, 7.5, M + 12, dy + 24, std::string(T("Yöntem: ")) + score::kBenchmarkVersion + " — docs/BENCHMARK_METHODOLOGY.md");
    footer();
    return p.save(path, error);
}

} // namespace aether
