#include "Strings.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <vector>

namespace aether {

std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}

std::string narrow(std::wstring_view s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

std::string format(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    va_list ap2; va_copy(ap2, ap);
    int n = vsnprintf(nullptr, 0, fmt, ap);
    va_end(ap);
    std::string out(n > 0 ? n : 0, '\0');
    if (n > 0) vsnprintf(out.data(), n + 1, fmt, ap2);
    va_end(ap2);
    return out;
}

std::wstring wformat(const wchar_t* fmt, ...) {
    std::vector<wchar_t> buf(1024);
    va_list ap; va_start(ap, fmt);
    vswprintf(buf.data(), buf.size(), fmt, ap);
    va_end(ap);
    return buf.data();
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string formatBytes(uint64_t bytes) {
    const double gb = 1024.0 * 1024.0 * 1024.0;
    if (bytes >= (uint64_t)gb) return format("%.1f GB", bytes / gb);
    return format("%.0f MB", bytes / (1024.0 * 1024.0));
}

std::string nowIso8601() {
    SYSTEMTIME st; GetLocalTime(&st);
    return format("%04d-%02d-%02dT%02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

std::string formatDuration(double seconds) {
    if (seconds < 0) seconds = 0;
    int s = (int)(seconds + 0.5);
    int h = s / 3600, m = (s / 60) % 60, sec = s % 60;
    if (h > 0) return format("%d:%02d:%02d", h, m, sec);
    return format("%02d:%02d", m, sec);
}

} // namespace aether
