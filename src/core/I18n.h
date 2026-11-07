#pragma once
#include <string>

namespace aether {

// Localization architecture:
//  * Source strings are written in Turkish (the primary product language).
//  * Additional languages provide a translation table keyed by the Turkish source string.
//  * Missing translations fall back to the Turkish source, never to an empty string.
class I18n {
public:
    static void setLanguage(const std::string& code);   // "tr" | "en"
    static const std::string& language();
    static const char* get(const char* source);
};

inline const char* T(const char* s) { return I18n::get(s); }

} // namespace aether
