#include "ShaderLibrary.h"
#include "EmbeddedShaders.h"
#include "../core/Log.h"
#include "../core/Paths.h"
#include "../core/FileIO.h"
#include "../core/Strings.h"
#include <d3dcompiler.h>

namespace aether {

using D3DCompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR,
                                      UINT, UINT, ID3DBlob**, ID3DBlob**);

static const char* kCacheVersion = "agl-shader-cache-v1";

ShaderLibrary& ShaderLibrary::instance() { static ShaderLibrary lib; return lib; }

bool ShaderLibrary::loadCompiler() {
    if (m_tried) return m_compileFn != nullptr;
    m_tried = true;
    HMODULE m = LoadLibraryW(L"d3dcompiler_47.dll");
    if (!m) { LOGE("d3dcompiler_47.dll could not be loaded"); return false; }
    m_compileFn = (void*)GetProcAddress(m, "D3DCompile");
    if (!m_compileFn) LOGE("D3DCompile entry point missing");
    return m_compileFn != nullptr;
}

bool ShaderLibrary::available() { std::lock_guard<std::mutex> l(m_mutex); return loadCompiler(); }

std::string ShaderLibrary::sourceText(ShaderSource src) {
    using namespace shaders;
    std::string common = common_hlsli;
    switch (src) {
        case ShaderSource::Scene: return common + scene_hlsl;
        case ShaderSource::Shards: return common + shards_hlsl;
        case ShaderSource::Particles: return common + particles_hlsl;
        case ShaderSource::Noise3D: return common + noise3d_hlsl;
        case ShaderSource::Memory: return common + memory_hlsl;
        case ShaderSource::Post: return common + post_hlsl;
        case ShaderSource::Fractal: return common + fractal_hlsl;
        case ShaderSource::ComputeBench: return computebench_hlsl;
        case ShaderSource::VramBench: return vrambench_hlsl;
        case ShaderSource::AiGemm: return aigemm_hlsl;
        case ShaderSource::AiGemmSimple: return aigemm_simple_hlsl;
    }
    return {};
}

static uint64_t fnv1a(const std::string& s, uint64_t h = 1469598103934665603ull) {
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}

std::vector<uint8_t> ShaderLibrary::compile(ShaderSource src, const char* entry, const char* profile, std::string& error) {
    std::string text = sourceText(src);
    uint64_t key = fnv1a(text + "|" + entry + "|" + profile + "|" + kCacheVersion);
    {
        std::lock_guard<std::mutex> l(m_mutex);
        auto it = m_memCache.find(key);
        if (it != m_memCache.end()) return it->second;
    }
    std::wstring cacheDir = Paths::root() + L"\\shadercache";
    CreateDirectoryW(cacheDir.c_str(), nullptr);
    std::wstring cacheFile = cacheDir + L"\\" + widen(format("%016llx.cso", (unsigned long long)key));
    std::string cached;
    if (!Paths::root().empty() && readFile(cacheFile, cached) && cached.size() > 16) {
        std::vector<uint8_t> blob(cached.begin(), cached.end());
        std::lock_guard<std::mutex> l(m_mutex);
        m_memCache[key] = blob;
        return blob;
    }
    D3DCompileFn fn = nullptr;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (!loadCompiler()) { error = "d3dcompiler_47.dll bulunamadı"; return {}; }
        fn = (D3DCompileFn)m_compileFn;
    }
    ComPtr<ID3DBlob> code, errors;
    DWORD t0 = GetTickCount();
    HRESULT hr = fn(text.data(), text.size(), "aether", nullptr, nullptr, entry, profile,
                    D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(hr)) {
        error = errors ? std::string((const char*)errors->GetBufferPointer(), errors->GetBufferSize()) : "D3DCompile failed";
        LOGE("Shader compile failed (%s %s): %s", entry, profile, error.c_str());
        return {};
    }
    LOGI("Compiled shader %s (%s) in %lu ms", entry, profile, GetTickCount() - t0);
    std::vector<uint8_t> blob((const uint8_t*)code->GetBufferPointer(),
                              (const uint8_t*)code->GetBufferPointer() + code->GetBufferSize());
    if (!Paths::root().empty()) writeFile(cacheFile, blob.data(), blob.size());
    std::lock_guard<std::mutex> l(m_mutex);
    m_memCache[key] = blob;
    return blob;
}

ComPtr<ID3D11VertexShader> ShaderLibrary::vs(ID3D11Device* dev, ShaderSource src, const char* entry, std::string& err) {
    auto b = compile(src, entry, "vs_5_0", err);
    ComPtr<ID3D11VertexShader> s;
    if (!b.empty() && FAILED(dev->CreateVertexShader(b.data(), b.size(), nullptr, s.GetAddressOf()))) err = "CreateVertexShader failed";
    return s;
}
ComPtr<ID3D11PixelShader> ShaderLibrary::ps(ID3D11Device* dev, ShaderSource src, const char* entry, std::string& err) {
    auto b = compile(src, entry, "ps_5_0", err);
    ComPtr<ID3D11PixelShader> s;
    if (!b.empty() && FAILED(dev->CreatePixelShader(b.data(), b.size(), nullptr, s.GetAddressOf()))) err = "CreatePixelShader failed";
    return s;
}
ComPtr<ID3D11ComputeShader> ShaderLibrary::cs(ID3D11Device* dev, ShaderSource src, const char* entry, std::string& err) {
    auto b = compile(src, entry, "cs_5_0", err);
    ComPtr<ID3D11ComputeShader> s;
    if (!b.empty() && FAILED(dev->CreateComputeShader(b.data(), b.size(), nullptr, s.GetAddressOf()))) err = "CreateComputeShader failed";
    return s;
}

} // namespace aether
