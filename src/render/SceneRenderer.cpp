#include "SceneRenderer.h"
#include "../gpu/ShaderLibrary.h"
#include "../core/Log.h"
#include "../core/Strings.h"
#include <algorithm>
#include <cstring>

namespace aether {

namespace {

struct alignas(16) FrameConstants {
    float viewProj[16];
    float invViewProj[16];
    float camPos[3]; float time;
    float resolution[2]; float invResolution[2];
    int marchSteps, reflBounces, shadowSteps, volSamples;
    int aoSamples, detailOctaves, useNoiseTex; float morph;
    float lightDir[3]; float exposure;
    float lightColor[3]; float particleSize;
    uint32_t particleCount, shardCount, frameIndex; float bloomStrength;
    float extra[4];
};
static_assert(sizeof(FrameConstants) == 256, "FrameConstants layout must match HLSL");

struct alignas(16) MemoryConstants { uint32_t elements, stride, salt, pad; };

constexpr uint32_t kShardVerts = 12 * 6;
constexpr uint32_t kParticleStride = 32;
constexpr uint32_t kNoiseSize = 128;

} // namespace

// ---------------------------------------------------------------- cameras
CameraState stressCamera(double t) {
    CameraState c;
    float a = (float)(t * 0.12);
    float r = 5.2f + 0.6f * std::sin((float)t * 0.21f);
    c.eye = Vec3(std::cos(a) * r, 0.9f + 0.55f * std::sin((float)t * 0.17f), std::sin(a) * r);
    c.target = Vec3(0, 0.05f, 0);
    c.fovY = 0.72f;
    return c;
}

// Cinematic sequence: five shots blended with smooth transitions over the benchmark duration.
CameraState cinematicCamera(double t, double duration) {
    struct Shot { Vec3 e0, e1, t0, t1; float fov; };
    static const Shot shots[] = {
        {{0.0f, 0.2f, -9.0f}, {0.0f, 0.4f, -5.2f}, {0, 0.1f, 0}, {0, 0.15f, 0}, 0.62f},         // slow push-in
        {{-4.5f, -1.2f, -2.2f}, {-2.6f, -1.1f, 2.8f}, {0, -0.3f, 0}, {0, 0.1f, 0}, 0.70f},      // low tracking along floor
        {{1.8f, 0.6f, -1.9f}, {2.2f, 0.1f, 1.6f}, {0, 0.18f, 0}, {0, 0.18f, 0}, 0.55f},          // close orbit of the shell
        {{0.4f, 5.8f, -1.2f}, {-1.4f, 3.6f, -3.6f}, {0, 0, 0}, {0, 0.1f, 0}, 0.75f},            // crane down from above
        {{5.8f, 1.6f, 4.2f}, {7.4f, 2.2f, 1.0f}, {0, 0.1f, 0}, {0, 0.2f, 0}, 0.66f},             // wide finale
    };
    const int n = (int)(sizeof(shots) / sizeof(shots[0]));
    double shotLen = duration / n;
    int i = std::clamp((int)(t / shotLen), 0, n - 1);
    float u = (float)std::clamp((t - i * shotLen) / shotLen, 0.0, 1.0);
    float e = u * u * (3 - 2 * u) * 0.6f + u * 0.4f;
    const Shot& s = shots[i];
    CameraState c;
    c.eye = lerp(s.e0, s.e1, e);
    c.target = lerp(s.t0, s.t1, e);
    c.fovY = s.fov;
    if (i + 1 < n && u > 0.92f) {   // short dissolve-like camera blend into the next shot
        float b = smoothstepf(0.92f, 1.0f, u);
        const Shot& nx = shots[i + 1];
        c.eye = lerp(c.eye, nx.e0, b);
        c.target = lerp(c.target, nx.t0, b);
        c.fovY = c.fovY + (nx.fov - c.fovY) * b;
    }
    return c;
}

CameraState fractalCamera(double t) {
    CameraState c;
    float a = (float)(t * 0.18) + 0.6f;
    float r = 2.35f - 0.25f * std::sin((float)t * 0.23f);
    c.eye = Vec3(std::cos(a) * r, 0.55f * std::sin((float)t * 0.15f), std::sin(a) * r);
    c.target = Vec3(0, 0, 0);
    c.fovY = 0.8f;
    return c;
}

// ---------------------------------------------------------------- GpuTimer
bool GpuTimer::init(ID3D11Device* dev) {
    D3D11_QUERY_DESC dj{D3D11_QUERY_TIMESTAMP_DISJOINT, 0}, ts{D3D11_QUERY_TIMESTAMP, 0};
    for (int i = 0; i < N; ++i) {
        if (FAILED(dev->CreateQuery(&dj, m_disjoint[i].GetAddressOf())) || FAILED(dev->CreateQuery(&ts, m_begin[i].GetAddressOf())) ||
            FAILED(dev->CreateQuery(&ts, m_end[i].GetAddressOf())))
            return false;
    }
    return true;
}
void GpuTimer::begin(ID3D11DeviceContext* ctx) {
    if (m_inFlight >= N) return;
    ctx->Begin(m_disjoint[m_write].Get());
    ctx->End(m_begin[m_write].Get());
}
void GpuTimer::end(ID3D11DeviceContext* ctx) {
    if (m_inFlight >= N) return;
    ctx->End(m_end[m_write].Get());
    ctx->End(m_disjoint[m_write].Get());
    m_write = (m_write + 1) % N;
    ++m_inFlight;
}
double GpuTimer::poll(ID3D11DeviceContext* ctx) {
    while (m_inFlight > 0) {
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
        if (ctx->GetData(m_disjoint[m_read].Get(), &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) break;
        UINT64 b = 0, e = 0;
        if (ctx->GetData(m_begin[m_read].Get(), &b, sizeof(b), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) break;
        if (ctx->GetData(m_end[m_read].Get(), &e, sizeof(e), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) break;
        if (!dj.Disjoint && dj.Frequency > 0 && e >= b) m_last = double(e - b) * 1000.0 / double(dj.Frequency);
        m_read = (m_read + 1) % N;
        --m_inFlight;
    }
    return m_last;
}

// ---------------------------------------------------------------- SceneRenderer
bool SceneRenderer::init(ID3D11Device* dev, ID3D11DeviceContext* ctx, std::string& error) {
    m_dev = dev; m_ctx = ctx;
    auto& lib = ShaderLibrary::instance();
    std::string e;
    m_vsFull = lib.vs(dev, ShaderSource::Scene, "VSFullscreen", e);
    m_psScene = lib.ps(dev, ShaderSource::Scene, "PSScene", e);
    m_vsShard = lib.vs(dev, ShaderSource::Shards, "VSShard", e);
    m_psShard = lib.ps(dev, ShaderSource::Shards, "PSShard", e);
    m_csParticles = lib.cs(dev, ShaderSource::Particles, "CSParticles", e);
    m_vsParticle = lib.vs(dev, ShaderSource::Particles, "VSParticle", e);
    m_psParticle = lib.ps(dev, ShaderSource::Particles, "PSParticle", e);
    m_csNoise = lib.cs(dev, ShaderSource::Noise3D, "CSNoise", e);
    m_csMemory = lib.cs(dev, ShaderSource::Memory, "CSMemory", e);
    m_psBright = lib.ps(dev, ShaderSource::Post, "PSBright", e);
    m_psBlurH = lib.ps(dev, ShaderSource::Post, "PSBlurH", e);
    m_psBlurV = lib.ps(dev, ShaderSource::Post, "PSBlurV", e);
    m_psTonemap = lib.ps(dev, ShaderSource::Post, "PSTonemap", e);
    m_psFractal = lib.ps(dev, ShaderSource::Fractal, "PSFractal", e);
    if (!m_vsFull || !m_psScene || !m_vsShard || !m_psShard || !m_csParticles || !m_vsParticle || !m_psParticle ||
        !m_csNoise || !m_csMemory || !m_psBright || !m_psBlurH || !m_psBlurV || !m_psTonemap || !m_psFractal) {
        error = "Shader derlenemedi: " + e;
        return false;
    }

    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(FrameConstants); cb.Usage = D3D11_USAGE_DYNAMIC; cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(dev->CreateBuffer(&cb, nullptr, m_cbFrame.GetAddressOf()))) { error = "CB"; return false; }
    cb.ByteWidth = sizeof(MemoryConstants);
    if (FAILED(dev->CreateBuffer(&cb, nullptr, m_cbMemory.GetAddressOf()))) { error = "CB"; return false; }

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    dev->CreateSamplerState(&sd, m_sampWrap.GetAddressOf());
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    dev->CreateSamplerState(&sd, m_sampClamp.GetAddressOf());

    D3D11_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = TRUE; ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    dev->CreateDepthStencilState(&ds, m_dsWrite.GetAddressOf());
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dev->CreateDepthStencilState(&ds, m_dsTestOnly.GetAddressOf());
    ds.DepthEnable = FALSE;
    dev->CreateDepthStencilState(&ds, m_dsOff.GetAddressOf());

    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO; bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    dev->CreateBlendState(&bd, m_blendAdd.GetAddressOf());

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE;
    dev->CreateRasterizerState(&rd, m_rasterNoCull.GetAddressOf());

    // 3D noise volume generated on the GPU.
    D3D11_TEXTURE3D_DESC td{};
    td.Width = td.Height = td.Depth = kNoiseSize; td.MipLevels = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    if (FAILED(dev->CreateTexture3D(&td, nullptr, m_noise.GetAddressOf()))) { error = "Noise texture"; return false; }
    dev->CreateShaderResourceView(m_noise.Get(), nullptr, m_noiseSrv.GetAddressOf());
    ComPtr<ID3D11UnorderedAccessView> noiseUav;
    dev->CreateUnorderedAccessView(m_noise.Get(), nullptr, noiseUav.GetAddressOf());
    ctx->CSSetShader(m_csNoise.Get(), nullptr, 0);
    ctx->CSSetUnorderedAccessViews(0, 1, noiseUav.GetAddressOf(), nullptr);
    ctx->Dispatch(kNoiseSize / 4, kNoiseSize / 4, kNoiseSize / 4);
    ID3D11UnorderedAccessView* nullUav[2] = {};
    ctx->CSSetUnorderedAccessViews(0, 2, nullUav, nullptr);
    m_allocated = (uint64_t)kNoiseSize * kNoiseSize * kNoiseSize * 4;
    return true;
}

bool SceneRenderer::createTargets(std::string& error) {
    m_hdr.Reset(); m_depth.Reset(); m_bloomA.Reset(); m_bloomB.Reset();
    m_hdrRtv.Reset(); m_hdrSrv.Reset(); m_dsv.Reset();
    m_bloomARtv.Reset(); m_bloomBRtv.Reset(); m_bloomASrv.Reset(); m_bloomBSrv.Reset();
    m_allocated -= m_targetBytes; m_targetBytes = 0;

    D3D11_TEXTURE2D_DESC td{};
    td.Width = m_rw; td.Height = m_rh; td.MipLevels = 1; td.ArraySize = 1; td.SampleDesc.Count = 1;
    td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(m_dev->CreateTexture2D(&td, nullptr, m_hdr.GetAddressOf()))) { error = "HDR hedefi oluşturulamadı"; return false; }
    m_dev->CreateRenderTargetView(m_hdr.Get(), nullptr, m_hdrRtv.GetAddressOf());
    m_dev->CreateShaderResourceView(m_hdr.Get(), nullptr, m_hdrSrv.GetAddressOf());

    td.Format = DXGI_FORMAT_D32_FLOAT; td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(m_dev->CreateTexture2D(&td, nullptr, m_depth.GetAddressOf()))) { error = "Derinlik hedefi oluşturulamadı"; return false; }
    m_dev->CreateDepthStencilView(m_depth.Get(), nullptr, m_dsv.GetAddressOf());

    td.Width = std::max(1u, m_rw / 4); td.Height = std::max(1u, m_rh / 4);
    td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(m_dev->CreateTexture2D(&td, nullptr, m_bloomA.GetAddressOf())) ||
        FAILED(m_dev->CreateTexture2D(&td, nullptr, m_bloomB.GetAddressOf()))) { error = "Bloom hedefi"; return false; }
    m_dev->CreateRenderTargetView(m_bloomA.Get(), nullptr, m_bloomARtv.GetAddressOf());
    m_dev->CreateRenderTargetView(m_bloomB.Get(), nullptr, m_bloomBRtv.GetAddressOf());
    m_dev->CreateShaderResourceView(m_bloomA.Get(), nullptr, m_bloomASrv.GetAddressOf());
    m_dev->CreateShaderResourceView(m_bloomB.Get(), nullptr, m_bloomBSrv.GetAddressOf());
    m_targetBytes = (uint64_t)m_rw * m_rh * (8 + 4) + (uint64_t)td.Width * td.Height * 16;
    m_allocated += m_targetBytes;
    return true;
}

bool SceneRenderer::setResolution(uint32_t w, uint32_t h, std::string& error) {
    m_baseW = std::max(64u, w); m_baseH = std::max(64u, h);
    uint32_t rw = std::clamp((uint32_t)(m_baseW * m_q.renderScale), 64u, 8192u);
    uint32_t rh = std::clamp((uint32_t)(m_baseH * m_q.renderScale), 64u, 8192u);
    if (rw == m_rw && rh == m_rh && m_hdr) return true;
    m_rw = rw; m_rh = rh;
    return createTargets(error);
}

bool SceneRenderer::createParticles(int count, std::string& error) {
    m_particles.Reset(); m_particlesUav.Reset(); m_particlesSrv.Reset();
    m_allocated -= m_particleBytes; m_particleBytes = 0;
    m_particleCapacity = 0;
    if (count <= 0) return true;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = 4096; td.Height = (UINT)(((uint64_t)count * 8 + 4095) / 4096);
    td.MipLevels = 1; td.ArraySize = 1; td.SampleDesc.Count = 1;
    td.Format = DXGI_FORMAT_R32_FLOAT; td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(m_dev->CreateTexture2D(&td, nullptr, m_particles.GetAddressOf())) ||
        FAILED(m_dev->CreateUnorderedAccessView(m_particles.Get(), nullptr, m_particlesUav.GetAddressOf())) ||
        FAILED(m_dev->CreateShaderResourceView(m_particles.Get(), nullptr, m_particlesSrv.GetAddressOf()))) {
        error = "Parçacık dokusu ayrılamadı";
        return false;
    }
    const float zeros[4] = {0, 0, 0, 0};
    m_ctx->ClearUnorderedAccessViewFloat(m_particlesUav.Get(), zeros);
    m_particleCapacity = count;
    m_particleBytes = (uint64_t)td.Width * td.Height * 4;
    m_allocated += m_particleBytes;
    return true;
}

bool SceneRenderer::createMemory(uint64_t bytes, std::string& error) {
    m_memSrc.Reset(); m_memDst.Reset(); m_memSrcSrv.Reset(); m_memDstUav.Reset();
    m_allocated -= m_memoryBytes;
    m_memoryBytes = 0;
    if (bytes < (1u << 20)) return true;
    uint64_t half = (bytes / 2) & ~uint64_t(0xFFFF);
    half = std::min<uint64_t>(half, 1024ull << 20);   // hard cap per buffer (D3D11 resource limits)
    D3D11_TEXTURE2D_DESC td{};
    td.Width = 4096; td.Height = (UINT)(half / 16 / 4096);
    td.MipLevels = 1; td.ArraySize = 1; td.SampleDesc.Count = 1;
    td.Format = DXGI_FORMAT_R32G32B32A32_UINT; td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    if (FAILED(m_dev->CreateTexture2D(&td, nullptr, m_memSrc.GetAddressOf())) ||
        FAILED(m_dev->CreateTexture2D(&td, nullptr, m_memDst.GetAddressOf())) ||
        FAILED(m_dev->CreateShaderResourceView(m_memSrc.Get(), nullptr, m_memSrcSrv.GetAddressOf())) ||
        FAILED(m_dev->CreateUnorderedAccessView(m_memDst.Get(), nullptr, m_memDstUav.GetAddressOf()))) {
        m_memSrc.Reset(); m_memDst.Reset(); m_memSrcSrv.Reset(); m_memDstUav.Reset();
        error = "VRAM iş yükü belleği ayrılamadı";
        return false;
    }
    m_memoryBytes = half * 2;
    m_allocated += m_memoryBytes;
    return true;
}

bool SceneRenderer::setQuality(const SceneQuality& q, std::string& error) {
    bool scaleChanged = q.renderScale != m_q.renderScale;
    m_q = q;
    int wantParticles = q.compute ? q.particleCount : 0;
    if (wantParticles != m_particleCapacity && !createParticles(wantParticles, error)) return false;
    uint64_t wantMem = q.memory ? q.memoryBytes : 0;
    uint64_t curMem = m_memoryBytes;
    if ((wantMem == 0) != (curMem == 0) || (wantMem && (wantMem / 2 & ~uint64_t(0xFFFF)) * 2 != curMem)) {
        if (!createMemory(wantMem, error)) return false;
    }
    if (scaleChanged || !m_hdr) {
        m_rw = 0;
        return setResolution(m_baseW, m_baseH, error);
    }
    return true;
}

void SceneRenderer::fullscreen(ID3D11PixelShader* ps) {
    m_ctx->VSSetShader(m_vsFull.Get(), nullptr, 0);
    m_ctx->PSSetShader(ps, nullptr, 0);
    m_ctx->Draw(3, 0);
}

void SceneRenderer::render(Mode mode, double time, float dt, const CameraState& cam, ID3D11RenderTargetView* target,
                           uint32_t targetW, uint32_t targetH) {
    ID3D11DeviceContext* c = m_ctx;
    ++m_frame;
    float aspect = (float)m_rw / (float)m_rh;
    Mat4 view = lookAtLH(cam.eye, cam.target, Vec3(0, 1, 0));
    Mat4 proj = perspectiveLH(cam.fovY, aspect, 0.05f, 60.0f);
    Mat4 vp = view * proj, ivp;
    inverse(vp, ivp);

    FrameConstants fc{};
    std::memcpy(fc.viewProj, vp.m, 64);
    std::memcpy(fc.invViewProj, ivp.m, 64);
    fc.camPos[0] = cam.eye.x; fc.camPos[1] = cam.eye.y; fc.camPos[2] = cam.eye.z;
    fc.time = (float)time;
    fc.resolution[0] = (float)m_rw; fc.resolution[1] = (float)m_rh;
    fc.invResolution[0] = 1.0f / m_rw; fc.invResolution[1] = 1.0f / m_rh;
    if (mode == Mode::Fractal) {
        fc.marchSteps = m_q.marchSteps; fc.detailOctaves = m_q.detailOctaves;
    } else {
        fc.marchSteps = m_q.fragment ? m_q.marchSteps : 0;
        fc.reflBounces = m_q.reflectionBounces; fc.shadowSteps = m_q.shadowSteps;
        fc.volSamples = m_q.volumetricSamples; fc.aoSamples = m_q.aoSamples; fc.detailOctaves = m_q.detailOctaves;
    }
    fc.useNoiseTex = m_q.textures ? 1 : 0;
    fc.morph = 1.0f;
    Vec3 ld = normalize(Vec3(-0.45f, -0.85f, 0.35f));
    fc.lightDir[0] = ld.x; fc.lightDir[1] = ld.y; fc.lightDir[2] = ld.z;
    fc.exposure = 1.05f;
    fc.lightColor[0] = 3.1f; fc.lightColor[1] = 3.05f; fc.lightColor[2] = 2.95f;
    fc.particleSize = 1.0f;
    fc.particleCount = (uint32_t)m_particleCapacity;
    fc.shardCount = (uint32_t)m_q.shardInstances;
    fc.frameIndex = m_frame;
    fc.bloomStrength = m_q.bloom ? 0.55f : 0.0f;
    fc.extra[0] = (float)m_q.computeIterations; fc.extra[1] = dt; fc.extra[2] = 0.012f; fc.extra[3] = 1.0f;

    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(c->Map(m_cbFrame.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        std::memcpy(ms.pData, &fc, sizeof(fc));
        c->Unmap(m_cbFrame.Get(), 0);
    }
    ID3D11Buffer* cbs[2] = {m_cbFrame.Get(), m_cbMemory.Get()};
    ID3D11SamplerState* samps[2] = {m_sampWrap.Get(), m_sampClamp.Get()};
    c->VSSetConstantBuffers(0, 1, cbs); c->PSSetConstantBuffers(0, 1, cbs); c->CSSetConstantBuffers(0, 2, cbs);
    c->PSSetSamplers(0, 2, samps); c->CSSetSamplers(0, 2, samps); c->VSSetSamplers(0, 2, samps);
    c->PSSetShaderResources(0, 1, m_noiseSrv.GetAddressOf());
    c->CSSetShaderResources(0, 1, m_noiseSrv.GetAddressOf());
    ID3D11UnorderedAccessView* nullUav[2] = {};
    ID3D11ShaderResourceView* nullSrv[5] = {};

    if (mode == Mode::Scene) {
        // Compute: particle simulation.
        if (m_particleCapacity > 0) {
            c->CSSetShader(m_csParticles.Get(), nullptr, 0);
            c->CSSetUnorderedAccessViews(0, 1, m_particlesUav.GetAddressOf(), nullptr);
            c->Dispatch((m_particleCapacity + 255) / 256, 1, 1);
            c->CSSetUnorderedAccessViews(0, 1, nullUav, nullptr);
        }
        // Memory: stream the VRAM working set.
        if (m_memoryBytes > 0 && m_q.memoryPasses > 0) {
            MemoryConstants mc{};
            mc.elements = (uint32_t)(m_memoryBytes / 2 / 16);
            uint32_t groups = std::min<uint32_t>((mc.elements + 255) / 256, 65535u);
            mc.stride = groups * 256;
            mc.salt = m_frame;
            if (SUCCEEDED(c->Map(m_cbMemory.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
                std::memcpy(ms.pData, &mc, sizeof(mc));
                c->Unmap(m_cbMemory.Get(), 0);
            }
            c->CSSetShader(m_csMemory.Get(), nullptr, 0);
            c->CSSetShaderResources(2, 1, m_memSrcSrv.GetAddressOf());
            c->CSSetUnorderedAccessViews(1, 1, m_memDstUav.GetAddressOf(), nullptr);
            for (int i = 0; i < m_q.memoryPasses; ++i) c->Dispatch(groups, 1, 1);
            c->CSSetUnorderedAccessViews(1, 1, nullUav, nullptr);
            c->CSSetShaderResources(2, 1, nullSrv);
        }
    }

    D3D11_VIEWPORT vpo{0, 0, (float)m_rw, (float)m_rh, 0, 1};
    c->RSSetViewports(1, &vpo);
    c->RSSetState(m_rasterNoCull.Get());
    const float clear[4] = {0.008f, 0.009f, 0.011f, 1};
    c->ClearRenderTargetView(m_hdrRtv.Get(), clear);
    c->ClearDepthStencilView(m_dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    c->OMSetRenderTargets(1, m_hdrRtv.GetAddressOf(), m_dsv.Get());
    c->IASetInputLayout(nullptr);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);

    if (mode == Mode::Fractal) {
        c->OMSetDepthStencilState(m_dsOff.Get(), 0);
        fullscreen(m_psFractal.Get());
    } else {
        c->OMSetDepthStencilState(m_dsWrite.Get(), 0);
        if (m_q.fragment) fullscreen(m_psScene.Get());
        if (m_q.geometry && m_q.shardInstances > 0) {
            c->VSSetShader(m_vsShard.Get(), nullptr, 0);
            c->PSSetShader(m_psShard.Get(), nullptr, 0);
            c->Draw(kShardVerts * (UINT)m_q.shardInstances, 0);
        }
        if (m_particleCapacity > 0) {
            c->OMSetDepthStencilState(m_dsTestOnly.Get(), 0);
            c->OMSetBlendState(m_blendAdd.Get(), nullptr, 0xFFFFFFFF);
            c->VSSetShaderResources(1, 1, m_particlesSrv.GetAddressOf());
            c->VSSetShader(m_vsParticle.Get(), nullptr, 0);
            c->PSSetShader(m_psParticle.Get(), nullptr, 0);
            c->Draw((UINT)m_particleCapacity * 6, 0);
            c->VSSetShaderResources(1, 1, nullSrv);
            c->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        }
    }
    c->OMSetDepthStencilState(m_dsOff.Get(), 0);

    // Bloom chain at quarter resolution. The post shaders read their texel size from invResolution,
    // so the constant buffer is patched per pass.
    auto setInvRes = [&](float w, float h) {
        fc.invResolution[0] = 1.0f / w; fc.invResolution[1] = 1.0f / h;
        if (SUCCEEDED(c->Map(m_cbFrame.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
            std::memcpy(ms.pData, &fc, sizeof(fc));
            c->Unmap(m_cbFrame.Get(), 0);
        }
    };
    uint32_t bw = std::max(1u, m_rw / 4), bh = std::max(1u, m_rh / 4);
    if (m_q.bloom) {
        D3D11_VIEWPORT bv{0, 0, (float)bw, (float)bh, 0, 1};
        c->RSSetViewports(1, &bv);
        setInvRes((float)m_rw, (float)m_rh);
        c->OMSetRenderTargets(1, m_bloomARtv.GetAddressOf(), nullptr);
        c->PSSetShaderResources(3, 1, m_hdrSrv.GetAddressOf());
        fullscreen(m_psBright.Get());
        c->PSSetShaderResources(3, 1, nullSrv);
        setInvRes((float)bw, (float)bh);
        c->OMSetRenderTargets(1, m_bloomBRtv.GetAddressOf(), nullptr);
        c->PSSetShaderResources(3, 1, m_bloomASrv.GetAddressOf());
        fullscreen(m_psBlurH.Get());
        c->PSSetShaderResources(3, 1, nullSrv);
        c->OMSetRenderTargets(1, m_bloomARtv.GetAddressOf(), nullptr);
        c->PSSetShaderResources(3, 1, m_bloomBSrv.GetAddressOf());
        fullscreen(m_psBlurV.Get());
        c->PSSetShaderResources(3, 1, nullSrv);
    }

    // Tonemap into the presentation target with aspect-preserving letterbox.
    float sx = (float)targetW / m_rw, sy = (float)targetH / m_rh, s = std::min(sx, sy);
    float w = m_rw * s, h = m_rh * s;
    D3D11_VIEWPORT ov{((float)targetW - w) * 0.5f, ((float)targetH - h) * 0.5f, w, h, 0, 1};
    const float black[4] = {0.035f, 0.039f, 0.047f, 1};
    c->OMSetRenderTargets(1, &target, nullptr);
    c->ClearRenderTargetView(target, black);
    c->RSSetViewports(1, &ov);
    ID3D11ShaderResourceView* post[2] = {m_hdrSrv.Get(), m_q.bloom ? m_bloomASrv.Get() : nullptr};
    c->PSSetShaderResources(3, 2, post);
    fullscreen(m_psTonemap.Get());
    c->PSSetShaderResources(3, 2, nullSrv);
    c->OMSetRenderTargets(0, nullptr, nullptr);
}

} // namespace aether
