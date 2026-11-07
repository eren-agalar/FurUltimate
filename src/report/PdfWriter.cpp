#include "PdfWriter.h"
#include "../core/FileIO.h"
#include "../core/Strings.h"
#include <miniz.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace aether {

// ------------------------------------------------------------------ UTF-8
std::vector<uint32_t> utf8ToCodepoints(const std::string& s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp; int n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c >> 5) == 6) { cp = c & 0x1F; n = 2; }
        else if ((c >> 4) == 14) { cp = c & 0x0F; n = 3; }
        else if ((c >> 3) == 30) { cp = c & 0x07; n = 4; }
        else { ++i; continue; }
        if (i + n > s.size()) break;
        for (int k = 1; k < n; ++k) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        out.push_back(cp);
        i += n;
    }
    return out;
}

// ------------------------------------------------------------------ TrueType
static uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
static int16_t bes16(const uint8_t* p) { return (int16_t)be16(p); }
static uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

bool TrueTypeFont::load(std::vector<uint8_t> data) {
    m_data = std::move(data);
    const uint8_t* d = m_data.data();
    size_t size = m_data.size();
    if (size < 12) return false;
    uint16_t numTables = be16(d + 4);
    std::map<std::string, std::pair<uint32_t, uint32_t>> tables;
    for (int i = 0; i < numTables; ++i) {
        const uint8_t* t = d + 12 + i * 16;
        if (t + 16 > d + size) return false;
        tables[std::string((const char*)t, 4)] = {be32(t + 8), be32(t + 12)};
    }
    auto tab = [&](const char* name) -> const uint8_t* {
        auto it = tables.find(name);
        if (it == tables.end() || it->second.first + it->second.second > size) return nullptr;
        return d + it->second.first;
    };
    const uint8_t* head = tab("head"); const uint8_t* hhea = tab("hhea"); const uint8_t* hmtx = tab("hmtx");
    const uint8_t* cmap = tab("cmap"); const uint8_t* maxp = tab("maxp");
    if (!head || !hhea || !hmtx || !cmap || !maxp) return false;
    m_upem = be16(head + 18);
    m_ascent = bes16(hhea + 4);
    m_descent = bes16(hhea + 6);
    uint16_t numGlyphs = be16(maxp + 4);
    uint16_t numHMetrics = be16(hhea + 34);
    m_advances.assign(numGlyphs, 0);
    uint16_t lastAdv = 0;
    for (uint16_t g = 0; g < numGlyphs; ++g) {
        if (g < numHMetrics) lastAdv = be16(hmtx + g * 4);
        m_advances[g] = lastAdv;
    }
    // cmap: prefer (3,10) format 12, then (3,1) format 4.
    uint16_t nsub = be16(cmap + 2);
    const uint8_t* best = nullptr;
    int bestFmt = 0;
    for (int i = 0; i < nsub; ++i) {
        const uint8_t* r = cmap + 4 + i * 8;
        uint16_t pid = be16(r), eid = be16(r + 2);
        const uint8_t* sub = cmap + be32(r + 4);
        uint16_t fmt = be16(sub);
        if (pid == 3 && eid == 10 && fmt == 12) { best = sub; bestFmt = 12; break; }
        if (pid == 3 && eid == 1 && fmt == 4 && bestFmt != 12) { best = sub; bestFmt = 4; }
        if (pid == 0 && fmt == 4 && !best) { best = sub; bestFmt = 4; }
    }
    if (!best) return false;
    if (bestFmt == 12) {
        uint32_t ngroups = be32(best + 12);
        for (uint32_t i = 0; i < ngroups; ++i) {
            const uint8_t* g = best + 16 + i * 12;
            uint32_t s = be32(g), e = be32(g + 4), gid = be32(g + 8);
            if (e - s > 0x10000) continue;
            for (uint32_t c = s; c <= e; ++c) m_cmap[c] = (uint16_t)(gid + (c - s));
        }
    } else {
        uint16_t segX2 = be16(best + 6);
        const uint8_t* ends = best + 14;
        const uint8_t* starts = ends + segX2 + 2;
        const uint8_t* deltas = starts + segX2;
        const uint8_t* offsets = deltas + segX2;
        for (int i = 0; i < segX2 / 2; ++i) {
            uint16_t e = be16(ends + i * 2), s = be16(starts + i * 2);
            int16_t delta = bes16(deltas + i * 2);
            uint16_t ro = be16(offsets + i * 2);
            for (uint32_t c = s; c <= e && c != 0xFFFF; ++c) {
                uint16_t gid;
                if (ro == 0) gid = (uint16_t)(c + delta);
                else {
                    const uint8_t* p = offsets + i * 2 + ro + (c - s) * 2;
                    gid = be16(p);
                    if (gid) gid = (uint16_t)(gid + delta);
                }
                if (gid) m_cmap[c] = gid;
            }
        }
    }
    if (const uint8_t* name = tab("name")) {   // PostScript name (nameID 6)
        uint16_t count = be16(name + 2), strOff = be16(name + 4);
        for (int i = 0; i < count; ++i) {
            const uint8_t* r = name + 6 + i * 12;
            if (be16(r + 6) != 6) continue;
            uint16_t pid = be16(r), len = be16(r + 8), off = be16(r + 10);
            const uint8_t* s = name + strOff + off;
            std::string ps;
            if (pid == 3 || pid == 0) { for (int k = 0; k + 1 < len; k += 2) ps += (char)s[k + 1]; }
            else ps.assign((const char*)s, len);
            if (!ps.empty()) { m_psName = ps; break; }
        }
    }
    return true;
}

uint16_t TrueTypeFont::glyph(uint32_t cp) const {
    auto it = m_cmap.find(cp);
    return it == m_cmap.end() ? 0 : it->second;
}
int TrueTypeFont::advance(uint16_t gid) const { return gid < m_advances.size() ? m_advances[gid] : 0; }

// ------------------------------------------------------------------ PdfWriter
std::string PdfWriter::num(double v) {
    char b[32];
    snprintf(b, sizeof(b), "%.3f", v);
    std::string s = b;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s == "-0") s = "0";
    return s.empty() ? "0" : s;
}

int PdfWriter::addFont(const std::vector<uint8_t>& ttf) {
    FontEntry e;
    if (!e.ttf.load(ttf)) return -1;
    m_fonts.push_back(std::move(e));
    return (int)m_fonts.size() - 1;
}

void PdfWriter::newPage() { m_pages.emplace_back(); }
std::string& PdfWriter::cur() { if (m_pages.empty()) newPage(); return m_pages.back(); }

static void rgbParts(uint32_t rgb, double& r, double& g, double& b) {
    r = ((rgb >> 16) & 0xFF) / 255.0; g = ((rgb >> 8) & 0xFF) / 255.0; b = (rgb & 0xFF) / 255.0;
}
void PdfWriter::setFill(uint32_t rgb) { double r, g, b; rgbParts(rgb, r, g, b); cur() += num(r) + " " + num(g) + " " + num(b) + " rg\n"; }
void PdfWriter::setStroke(uint32_t rgb, double w) {
    double r, g, b; rgbParts(rgb, r, g, b);
    cur() += num(r) + " " + num(g) + " " + num(b) + " RG " + num(w) + " w 1 J 1 j\n";
}
void PdfWriter::rect(double x, double y, double w, double h) {
    cur() += num(x) + " " + num(kPageH - y - h) + " " + num(w) + " " + num(h) + " re f\n";
}
void PdfWriter::strokeRect(double x, double y, double w, double h) {
    cur() += num(x) + " " + num(kPageH - y - h) + " " + num(w) + " " + num(h) + " re S\n";
}
void PdfWriter::roundRect(double x, double y, double w, double h, double r) {
    r = std::min(r, std::min(w, h) / 2);
    const double k = 0.5523 * r;
    double X0 = x, X1 = x + w, Y0 = kPageH - y - h, Y1 = kPageH - y;
    std::string& s = cur();
    s += num(X0 + r) + " " + num(Y0) + " m ";
    s += num(X1 - r) + " " + num(Y0) + " l ";
    s += num(X1 - r + k) + " " + num(Y0) + " " + num(X1) + " " + num(Y0 + r - k) + " " + num(X1) + " " + num(Y0 + r) + " c ";
    s += num(X1) + " " + num(Y1 - r) + " l ";
    s += num(X1) + " " + num(Y1 - r + k) + " " + num(X1 - r + k) + " " + num(Y1) + " " + num(X1 - r) + " " + num(Y1) + " c ";
    s += num(X0 + r) + " " + num(Y1) + " l ";
    s += num(X0 + r - k) + " " + num(Y1) + " " + num(X0) + " " + num(Y1 - r + k) + " " + num(X0) + " " + num(Y1 - r) + " c ";
    s += num(X0) + " " + num(Y0 + r) + " l ";
    s += num(X0) + " " + num(Y0 + r - k) + " " + num(X0 + r - k) + " " + num(Y0) + " " + num(X0 + r) + " " + num(Y0) + " c f\n";
}
void PdfWriter::line(double x1, double y1, double x2, double y2) {
    cur() += num(x1) + " " + num(kPageH - y1) + " m " + num(x2) + " " + num(kPageH - y2) + " l S\n";
}
void PdfWriter::polyline(const std::vector<std::pair<double, double>>& pts) {
    if (pts.size() < 2) return;
    std::string& s = cur();
    s += num(pts[0].first) + " " + num(kPageH - pts[0].second) + " m ";
    for (size_t i = 1; i < pts.size(); ++i) s += num(pts[i].first) + " " + num(kPageH - pts[i].second) + " l ";
    s += "S\n";
}
void PdfWriter::polygon(const std::vector<std::pair<double, double>>& pts) {
    if (pts.size() < 3) return;
    std::string& s = cur();
    s += num(pts[0].first) + " " + num(kPageH - pts[0].second) + " m ";
    for (size_t i = 1; i < pts.size(); ++i) s += num(pts[i].first) + " " + num(kPageH - pts[i].second) + " l ";
    s += "h f\n";
}
void PdfWriter::circle(double cx, double cy, double r, bool fill) {
    const double k = 0.5523 * r;
    double Y = kPageH - cy;
    std::string& s = cur();
    s += num(cx + r) + " " + num(Y) + " m ";
    s += num(cx + r) + " " + num(Y + k) + " " + num(cx + k) + " " + num(Y + r) + " " + num(cx) + " " + num(Y + r) + " c ";
    s += num(cx - k) + " " + num(Y + r) + " " + num(cx - r) + " " + num(Y + k) + " " + num(cx - r) + " " + num(Y) + " c ";
    s += num(cx - r) + " " + num(Y - k) + " " + num(cx - k) + " " + num(Y - r) + " " + num(cx) + " " + num(Y - r) + " c ";
    s += num(cx + k) + " " + num(Y - r) + " " + num(cx + r) + " " + num(Y - k) + " " + num(cx + r) + " " + num(Y) + " c ";
    s += fill ? "f\n" : "S\n";
}
void PdfWriter::arcStroke(double cx, double cy, double r, double a0, double a1) {
    std::vector<std::pair<double, double>> pts;
    int n = std::max(8, (int)(std::fabs(a1 - a0) * 24));
    for (int i = 0; i <= n; ++i) {
        double a = a0 + (a1 - a0) * i / n;
        pts.push_back({cx + std::cos(a) * r, cy + std::sin(a) * r});
    }
    polyline(pts);
}

double PdfWriter::measure(int font, double size, const std::string& utf8) const {
    if (font < 0 || font >= (int)m_fonts.size()) return 0;
    const auto& f = m_fonts[font].ttf;
    double w = 0;
    for (uint32_t cp : utf8ToCodepoints(utf8)) w += f.advance(f.glyph(cp));
    return w * size / f.unitsPerEm();
}

double PdfWriter::text(int font, double size, double x, double y, const std::string& utf8) {
    if (font < 0 || font >= (int)m_fonts.size()) return 0;
    FontEntry& fe = m_fonts[font];
    std::string hex;
    double w = 0;
    for (uint32_t cp : utf8ToCodepoints(utf8)) {
        uint16_t g = fe.ttf.glyph(cp);
        fe.used.insert(g);
        if (g) fe.toUnicode[g] = cp;
        char b[8];
        snprintf(b, sizeof(b), "%04X", g);
        hex += b;
        w += fe.ttf.advance(g);
    }
    cur() += "BT /F" + std::to_string(font) + " " + num(size) + " Tf " + num(x) + " " + num(kPageH - y) + " Td <" + hex + "> Tj ET\n";
    return w * size / fe.ttf.unitsPerEm();
}

double PdfWriter::textRight(int font, double size, double xRight, double y, const std::string& utf8) {
    double w = measure(font, size, utf8);
    return text(font, size, xRight - w, y, utf8);
}

static std::string deflate(const std::string& in) {
    mz_ulong bound = mz_compressBound((mz_ulong)in.size());
    std::string out(bound, '\0');
    if (mz_compress2((unsigned char*)out.data(), &bound, (const unsigned char*)in.data(), (mz_ulong)in.size(), 9) != MZ_OK) return {};
    out.resize(bound);
    return out;
}

static std::string pdfString(const std::string& utf8) {   // UTF-16BE with BOM, hex encoded
    std::string hex = "<FEFF";
    char b[8];
    for (uint32_t cp : utf8ToCodepoints(utf8)) {
        if (cp >= 0x10000) {
            cp -= 0x10000;
            snprintf(b, sizeof(b), "%04X", 0xD800 + (cp >> 10)); hex += b;
            snprintf(b, sizeof(b), "%04X", 0xDC00 + (cp & 0x3FF)); hex += b;
        } else { snprintf(b, sizeof(b), "%04X", cp); hex += b; }
    }
    return hex + ">";
}

std::vector<uint8_t> PdfWriter::build() {
    if (m_pages.empty()) newPage();
    std::string out = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
    std::vector<size_t> offsets(1, 0);
    auto beginObj = [&](int id) {
        if ((int)offsets.size() <= id) offsets.resize(id + 1, 0);
        offsets[id] = out.size();
        out += std::to_string(id) + " 0 obj\n";
    };
    auto stream = [&](int id, const std::string& dict, const std::string& raw) {
        std::string z = deflate(raw);
        beginObj(id);
        if (!z.empty()) out += "<< " + dict + " /Filter /FlateDecode /Length " + std::to_string(z.size()) + " >>\nstream\n" + z + "\nendstream\nendobj\n";
        else out += "<< " + dict + " /Length " + std::to_string(raw.size()) + " >>\nstream\n" + raw + "\nendstream\nendobj\n";
    };
    // Object ids: 1 catalog, 2 pages, 3 info, then fonts (5 objects each), then pages (2 each).
    int next = 4;
    std::vector<int> fontIds;
    for (size_t fi = 0; fi < m_fonts.size(); ++fi) {
        FontEntry& fe = m_fonts[fi];
        int type0 = next++, cid = next++, desc = next++, file = next++, tounicode = next++;
        fontIds.push_back(type0);
        const auto& f = fe.ttf;
        double scale = 1000.0 / f.unitsPerEm();
        std::string base = "FNMARK+" + f.postscriptName();
        stream(file, "/Length1 " + std::to_string(f.data().size()), std::string(f.data().begin(), f.data().end()));
        beginObj(desc);
        out += "<< /Type /FontDescriptor /FontName /" + base + " /Flags 32 /FontBBox [-1000 " + num(f.descent() * scale) +
               " 2500 " + num(f.ascent() * scale) + "] /ItalicAngle 0 /Ascent " + num(f.ascent() * scale) + " /Descent " +
               num(f.descent() * scale) + " /CapHeight " + num(f.ascent() * scale * 0.9) + " /StemV 80 /FontFile2 " +
               std::to_string(file) + " 0 R >>\nendobj\n";
        std::string w = "[";
        for (uint16_t g : fe.used) w += std::to_string(g) + " [" + num(f.advance(g) * scale) + "] ";
        w += "]";
        beginObj(cid);
        out += "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /" + base +
               " /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /FontDescriptor " +
               std::to_string(desc) + " 0 R /CIDToGIDMap /Identity /DW 600 /W " + w + " >>\nendobj\n";
        std::string cmap = "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n"
                           "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
                           "/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";
        std::vector<std::pair<uint16_t, uint32_t>> pairs(fe.toUnicode.begin(), fe.toUnicode.end());
        for (size_t i = 0; i < pairs.size(); i += 100) {
            size_t n = std::min<size_t>(100, pairs.size() - i);
            cmap += std::to_string(n) + " beginbfchar\n";
            for (size_t k = 0; k < n; ++k) {
                char b[32];
                uint32_t cp = pairs[i + k].second;
                if (cp > 0xFFFF) cp = 0xFFFD;
                snprintf(b, sizeof(b), "<%04X> <%04X>\n", pairs[i + k].first, cp);
                cmap += b;
            }
            cmap += "endbfchar\n";
        }
        cmap += "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
        stream(tounicode, "", cmap);
        beginObj(type0);
        out += "<< /Type /Font /Subtype /Type0 /BaseFont /" + base + " /Encoding /Identity-H /DescendantFonts [" +
               std::to_string(cid) + " 0 R] /ToUnicode " + std::to_string(tounicode) + " 0 R >>\nendobj\n";
    }
    std::string fontRes = "/Font << ";
    for (size_t i = 0; i < fontIds.size(); ++i) fontRes += "/F" + std::to_string(i) + " " + std::to_string(fontIds[i]) + " 0 R ";
    fontRes += ">>";
    std::vector<int> pageIds;
    for (auto& content : m_pages) {
        int page = next++, cs = next++;
        pageIds.push_back(page);
        stream(cs, "", content);
        beginObj(page);
        out += "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + num(kPageW) + " " + num(kPageH) + "] /Resources << " + fontRes +
               " >> /Contents " + std::to_string(cs) + " 0 R >>\nendobj\n";
    }
    beginObj(2);
    out += "<< /Type /Pages /Count " + std::to_string(pageIds.size()) + " /Kids [";
    for (int id : pageIds) out += std::to_string(id) + " 0 R ";
    out += "] >>\nendobj\n";
    beginObj(1);
    out += "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";
    beginObj(3);
    out += "<< /Title " + pdfString(m_title) + " /Author " + pdfString(m_author) + " /Creator " + pdfString("FurUltimate") +
           " /Producer " + pdfString("FurUltimate PDF") + " >>\nendobj\n";
    size_t xref = out.size();
    out += "xref\n0 " + std::to_string(offsets.size()) + "\n0000000000 65535 f \n";
    char b[32];
    for (size_t i = 1; i < offsets.size(); ++i) { snprintf(b, sizeof(b), "%010zu 00000 n \n", offsets[i]); out += b; }
    out += "trailer\n<< /Size " + std::to_string(offsets.size()) + " /Root 1 0 R /Info 3 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    return std::vector<uint8_t>(out.begin(), out.end());
}

bool PdfWriter::save(const std::wstring& path, std::string& error) {
    auto bytes = build();
    if (!writeFile(path, bytes.data(), bytes.size())) { error = "PDF dosyası yazılamadı"; return false; }
    return true;
}

} // namespace aether
