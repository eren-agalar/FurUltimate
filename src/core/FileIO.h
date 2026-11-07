#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace aether {
bool readFile(const std::wstring& path, std::string& out);
bool writeFile(const std::wstring& path, const void* data, size_t size);
bool writeFileAtomic(const std::wstring& path, const std::string& data);
bool fileExists(const std::wstring& path);
}
