#pragma once
#include <string>
#include <string_view>
#include <cstdint>

namespace aether {

std::wstring widen(std::string_view utf8);
std::string narrow(std::wstring_view wide);
std::string format(const char* fmt, ...);
std::wstring wformat(const wchar_t* fmt, ...);
std::string trim(const std::string& s);
// Format bytes as "8.0 GB" style.
std::string formatBytes(uint64_t bytes);
// Current local time as ISO-8601 (YYYY-MM-DDTHH:MM:SS)
std::string nowIso8601();
// Human readable duration (e.g. "02:31" or "1:02:31")
std::string formatDuration(double seconds);

} // namespace aether
