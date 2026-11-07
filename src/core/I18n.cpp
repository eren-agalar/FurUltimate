#include "I18n.h"
#include "I18nTable.h"
#include <unordered_map>
#include <string_view>

namespace aether {

namespace {
std::string g_lang = "tr";
const std::unordered_map<std::string_view, const char*>& englishTable() {
    static const std::unordered_map<std::string_view, const char*> table = [] {
        std::unordered_map<std::string_view, const char*> m;
        for (const auto& e : kEnglish) m.emplace(e.tr, e.en);
        return m;
    }();
    return table;
}
} // namespace

void I18n::setLanguage(const std::string& code) { g_lang = (code == "en") ? "en" : "tr"; }
const std::string& I18n::language() { return g_lang; }

const char* I18n::get(const char* source) {
    if (g_lang == "tr" || !source) return source;
    auto& t = englishTable();
    auto it = t.find(source);
    return it != t.end() ? it->second : source;
}

} // namespace aether
