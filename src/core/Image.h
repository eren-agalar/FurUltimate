#pragma once
#include <string>
#include <cstdint>

namespace aether {
// Saves 32-bit BGRA pixels as PNG through WIC. Thread-safe (initializes COM on the calling thread if needed).
bool savePngBGRA(const std::wstring& path, uint32_t width, uint32_t height, uint32_t pitch, const uint8_t* pixels);
}
