#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace aether {

// Minimal TrueType reader: Unicode → glyph id (cmap 4/12) and advance widths (hmtx).
class TrueTypeFont {
public:
    bool load(std::vector<uint8_t> data);
    uint16_t glyph(uint32_t codepoint) const;
    int advance(uint16_t gid) const;          // in font units
    int unitsPerEm() const { return m_upem; }
    int ascent() const { return m_ascent; }
    int descent() const { return m_descent; }
    std::string postscriptName() const { return m_psName; }
    const std::vector<uint8_t>& data() const { return m_data; }
private:
    std::vector<uint8_t> m_data;
    std::map<uint32_t, uint16_t> m_cmap;
    std::vector<uint16_t> m_advances;
    int m_upem = 1000, m_ascent = 800, m_descent = -200;
    std::string m_psName = "Font";
};

// Small PDF 1.7 writer with embedded TrueType (CIDFontType2 / Identity-H) fonts, vector drawing and
// Flate-compressed streams. Coordinates use a top-left origin in points (converted internally).
class PdfWriter {
public:
    static constexpr double kPageW = 595.28, kPageH = 841.89;   // A4
    int addFont(const std::vector<uint8_t>& ttf);               // returns font index or -1
    void newPage();
    void setFill(uint32_t rgb);
    void setStroke(uint32_t rgb, double width);
    void rect(double x, double y, double w, double h);           // fill
    void roundRect(double x, double y, double w, double h, double r);
    void strokeRect(double x, double y, double w, double h);
    void line(double x1, double y1, double x2, double y2);
    void polyline(const std::vector<std::pair<double, double>>& pts);
    void polygon(const std::vector<std::pair<double, double>>& pts);   // closed, filled with the fill colour
    void circle(double cx, double cy, double r, bool fill);
    void arcStroke(double cx, double cy, double r, double a0, double a1);
    // Text: baseline at y. Returns advance width in points.
    double text(int font, double size, double x, double y, const std::string& utf8);
    double textRight(int font, double size, double xRight, double y, const std::string& utf8);
    double measure(int font, double size, const std::string& utf8) const;
    void setInfo(const std::string& title, const std::string& author) { m_title = title; m_author = author; }
    bool save(const std::wstring& path, std::string& error);
    std::vector<uint8_t> build();
    int pageCount() const { return (int)m_pages.size(); }
private:
    struct FontEntry { TrueTypeFont ttf; std::set<uint16_t> used; std::map<uint16_t, uint32_t> toUnicode; };
    std::string& cur();
    static std::string num(double v);
    std::vector<FontEntry> m_fonts;
    std::vector<std::string> m_pages;
    std::string m_title, m_author;
};

std::vector<uint32_t> utf8ToCodepoints(const std::string& s);

} // namespace aether
