#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>

namespace aether {

using Microsoft::WRL::ComPtr;

enum class ShaderSource { Scene, Shards, Particles, Noise3D, Memory, Post, Fractal, ComputeBench, VramBench, AiGemm, AiGemmSimple };

// Compiles embedded HLSL with the system d3dcompiler_47.dll (present on every Windows 10/11 install),
// caching bytecode on disk so subsequent launches start instantly.
class ShaderLibrary {
public:
    static ShaderLibrary& instance();
    bool available();
    // Returns compiled bytecode or empty on failure (error text in `error`).
    std::vector<uint8_t> compile(ShaderSource src, const char* entry, const char* profile, std::string& error);

    ComPtr<ID3D11VertexShader> vs(ID3D11Device* dev, ShaderSource src, const char* entry, std::string& err);
    ComPtr<ID3D11PixelShader>  ps(ID3D11Device* dev, ShaderSource src, const char* entry, std::string& err);
    ComPtr<ID3D11ComputeShader> cs(ID3D11Device* dev, ShaderSource src, const char* entry, std::string& err);

    static std::string sourceText(ShaderSource src);
private:
    ShaderLibrary() = default;
    bool loadCompiler();
    std::mutex m_mutex;
    void* m_compileFn = nullptr;
    bool m_tried = false;
    std::unordered_map<uint64_t, std::vector<uint8_t>> m_memCache;
};

} // namespace aether
