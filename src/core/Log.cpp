#include "Log.h"
#include "Strings.h"
#include <windows.h>
#include <mutex>
#include <cstdio>
#include <ctime>

namespace aether {

namespace {
std::mutex g_mutex;
FILE* g_file = nullptr;
std::wstring g_path;
}

void Log::init(const std::wstring& directory) {
    std::lock_guard<std::mutex> lock(g_mutex);
    CreateDirectoryW(directory.c_str(), nullptr);
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t name[64];
    swprintf(name, 64, L"aether_%04d%02d%02d_%02d%02d%02d.log", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    g_path = directory + L"\\" + name;
    g_file = _wfopen(g_path.c_str(), L"wb");
}

void Log::shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) { fclose(g_file); g_file = nullptr; }
}

std::wstring Log::currentFile() { return g_path; }

void Log::write(LogLevel level, const char* fmt, ...) {
    char msg[2048];
    va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    static const char* names[] = {"DEBUG", "INFO ", "WARN ", "ERROR"};
    SYSTEMTIME st; GetLocalTime(&st);
    char line[2200];
    snprintf(line, sizeof(line), "%02d:%02d:%02d.%03d [%s] [t%lu] %s\r\n", st.wHour, st.wMinute, st.wSecond,
             st.wMilliseconds, names[(int)level], GetCurrentThreadId(), msg);
    std::lock_guard<std::mutex> lock(g_mutex);
    OutputDebugStringA(line);
    if (g_file) { fputs(line, g_file); fflush(g_file); }
}

} // namespace aether
