#pragma once
#include <string>
#include <cstdarg>

namespace aether {

enum class LogLevel { Debug, Info, Warn, Error };

// Thread-safe file + debugger logger. Files live in %LOCALAPPDATA%\FurUltimate\logs.
class Log {
public:
    static void init(const std::wstring& directory);
    static void shutdown();
    static void write(LogLevel level, const char* fmt, ...);
    static std::wstring currentFile();
};

#define LOGD(...) ::aether::Log::write(::aether::LogLevel::Debug, __VA_ARGS__)
#define LOGI(...) ::aether::Log::write(::aether::LogLevel::Info, __VA_ARGS__)
#define LOGW(...) ::aether::Log::write(::aether::LogLevel::Warn, __VA_ARGS__)
#define LOGE(...) ::aether::Log::write(::aether::LogLevel::Error, __VA_ARGS__)

} // namespace aether
