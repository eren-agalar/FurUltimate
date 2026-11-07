#pragma once
#include "Math.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <string>
#include <cstdint>

namespace aether {

using Microsoft::WRL::ComPtr;

// Workload parameters for the hero scene. Every field maps to a real GPU cost.
struct SceneQuality {
    int marchSteps = 128;          // primary ray-march steps (fragment ALU)
    int reflectionBounces = 2;     // secondary ray-march bounces
    int shadowSteps = 32;          // soft-shadow ray-march steps
    int volumetricSamples = 16;    // light-shaft samples per pixel
    int aoSamples = 4;             // ambient-occlusion taps
    int detailOctaves = 3;         // surface detail noise octaves (texture sampling when enabled)
    int shardInstances = 20000;    // procedural geometry instances (24 triangles each)
    int particleCount = 262144;    // GPU-simulated particles
    int computeIterations = 4;     // flow-field iterations per particle per frame
    uint64_t memoryBytes = 256ull << 20;  // VRAM streaming working set (split in two buffers)
    int memoryPasses = 2;          // streaming passes per frame
    float renderScale = 1.0f;      // render resolution multiplier
    bool fragment = true, geometry = true, textures = true, compute = true, memory = true;
    bool bloom = true;
};

struct CameraState { Vec3 eye, target; float fovY = 0.75f; };

// Deterministic camera paths (functions of scene time only => reproducible benchmark runs).
CameraState stressCamera(double t);
CameraState cinematicCamera(double t, double duration);
CameraState fractalCamera(double t);

// Non-blocking GPU frame timer using timestamp queries (ring of in-flight frames).
class GpuTimer {
public:
    bool init(ID3D11Device* dev);
    void begin(ID3D11DeviceContext* ctx);
    void end(ID3D11DeviceContext* ctx);
    // Most recent resolved GPU time in ms, or negative if none available yet.
    double poll(ID3D11DeviceContext* ctx);
private:
    static const int N = 6;
    ComPtr<ID3D11Query> m_disjoint[N], m_begin[N], m_end[N];
    int m_write = 0, m_read = 0, m_inFlight = 0;
    double m_last = -1;
};

class SceneRenderer {
public:
    bool init(ID3D11Device* dev, ID3D11DeviceContext* ctx, std::string& error);
    // Applies quality; (re)allocates buffers within the provided VRAM budget. Returns false on allocation failure.
    bool setQuality(const SceneQuality& q, std::string& error);
    const SceneQuality& quality() const { return m_q; }
    // Sets the base output resolution (render resolution = base * renderScale).
    bool setResolution(uint32_t w, uint32_t h, std::string& error);
    uint32_t renderWidth() const { return m_rw; }
    uint32_t renderHeight() const { return m_rh; }

    enum class Mode { Scene, Fractal };
    // Renders one frame into the HDR target, then tonemaps into `target` (letterboxed to its size).
    void render(Mode mode, double time, float dt, const CameraState& cam, ID3D11RenderTargetView* target,
                uint32_t targetW, uint32_t targetH);
    uint64_t allocatedBytes() const { return m_allocated; }

private:
    bool createTargets(std::string& error);
    bool createParticles(int count, std::string& error);
    bool createMemory(uint64_t bytes, std::string& error);
    void fullscreen(ID3D11PixelShader* ps);

    ID3D11Device* m_dev = nullptr;
    ID3D11DeviceContext* m_ctx = nullptr;
    SceneQuality m_q;
    uint32_t m_baseW = 1280, m_baseH = 720, m_rw = 0, m_rh = 0;
    uint32_t m_frame = 0;
    uint64_t m_allocated = 0;

    ComPtr<ID3D11VertexShader> m_vsFull, m_vsShard, m_vsParticle;
    ComPtr<ID3D11PixelShader> m_psScene, m_psShard, m_psParticle, m_psBright, m_psBlurH, m_psBlurV, m_psTonemap, m_psFractal;
    ComPtr<ID3D11ComputeShader> m_csParticles, m_csNoise, m_csMemory;
    ComPtr<ID3D11Buffer> m_cbFrame, m_cbMemory;
    ComPtr<ID3D11SamplerState> m_sampWrap, m_sampClamp;
    ComPtr<ID3D11DepthStencilState> m_dsWrite, m_dsTestOnly, m_dsOff;
    ComPtr<ID3D11BlendState> m_blendAdd;
    ComPtr<ID3D11RasterizerState> m_rasterNoCull;

    ComPtr<ID3D11Texture2D> m_hdr, m_depth, m_bloomA, m_bloomB;
    ComPtr<ID3D11RenderTargetView> m_hdrRtv, m_bloomARtv, m_bloomBRtv;
    ComPtr<ID3D11ShaderResourceView> m_hdrSrv, m_bloomASrv, m_bloomBSrv;
    ComPtr<ID3D11DepthStencilView> m_dsv;

    ComPtr<ID3D11Texture3D> m_noise;
    ComPtr<ID3D11ShaderResourceView> m_noiseSrv;

    int m_particleCapacity = 0;
    ComPtr<ID3D11Texture2D> m_particles;
    ComPtr<ID3D11UnorderedAccessView> m_particlesUav;
    ComPtr<ID3D11ShaderResourceView> m_particlesSrv;

    uint64_t m_memoryBytes = 0;
    ComPtr<ID3D11Texture2D> m_memSrc, m_memDst;
    ComPtr<ID3D11ShaderResourceView> m_memSrcSrv;
    ComPtr<ID3D11UnorderedAccessView> m_memDstUav;
    uint64_t m_targetBytes = 0, m_particleBytes = 0;
};

} // namespace aether
