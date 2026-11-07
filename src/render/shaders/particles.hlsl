// Compute workload: GPU particle simulation (curl-like flow field around the core)
// and additive billboard rendering of the simulated particles.

struct Particle { float3 pos; float life; float3 vel; float seed; };

// Particle state lives in an R32_FLOAT 2D texture (8 texels per particle, 4096 texels per row).
// R32_FLOAT typed UAV loads are guaranteed on feature level 11_0 and textures are supported by every HLSL toolchain.
static const uint PARTICLE_ROW = 4096;
RWTexture2D<float> particlesRW : register(u0);
Texture2D<float>   particlesRO : register(t1);

uint2 ptexel(uint i, uint k) { uint t = i * 8 + k; return uint2(t % PARTICLE_ROW, t / PARTICLE_ROW); }

Particle loadParticle(uint i)
{
    Particle p;
    p.pos  = float3(particlesRW[ptexel(i, 0)], particlesRW[ptexel(i, 1)], particlesRW[ptexel(i, 2)]);
    p.life = particlesRW[ptexel(i, 3)];
    p.vel  = float3(particlesRW[ptexel(i, 4)], particlesRW[ptexel(i, 5)], particlesRW[ptexel(i, 6)]);
    p.seed = particlesRW[ptexel(i, 7)];
    return p;
}
void storeParticle(uint i, Particle p)
{
    particlesRW[ptexel(i, 0)] = p.pos.x; particlesRW[ptexel(i, 1)] = p.pos.y; particlesRW[ptexel(i, 2)] = p.pos.z;
    particlesRW[ptexel(i, 3)] = p.life;
    particlesRW[ptexel(i, 4)] = p.vel.x; particlesRW[ptexel(i, 5)] = p.vel.y; particlesRW[ptexel(i, 6)] = p.vel.z;
    particlesRW[ptexel(i, 7)] = p.seed;
}

float3 flowField(float3 p, int iterations)
{
    // Analytic swirling field + iterated domain warping to scale ALU load.
    float3 f = float3(-p.z, 0.15 * sin(p.x * 1.7 + time), p.x);
    float3 q = p;
    [loop] for (int i = 0; i < iterations; ++i)
    {
        q = float3(sin(q.y * 1.31 + time * 0.2), sin(q.z * 1.17 - time * 0.13), sin(q.x * 1.23 + 0.7)) * 1.2 + q * 0.35;
        f += float3(cos(q.z), sin(q.x), cos(q.y)) * (0.35 / (i + 1));
    }
    float r = length(p);
    f += -p / max(r * r * r, 0.4) * 1.2;   // gentle attraction toward the core
    return f;
}

[numthreads(256, 1, 1)]
void CSParticles(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= particleCount) return;
    Particle pt = loadParticle(i);
    float dt = min(extra.y, 0.05);
    if (pt.life <= 0.0 || pt.seed == 0.0)
    {
        float3 h = hash33(float3(i, time * 13.1, pt.seed + 1.7));
        float a = h.x * 2.0 * PI;
        float r = 1.8 + h.y * 3.0;
        pt.pos = float3(cos(a) * r, (h.z - 0.5) * 2.4, sin(a) * r);
        pt.vel = 0.0;
        pt.life = 3.0 + h.z * 6.0;
        pt.seed = h.x + 0.001;
    }
    float3 f = flowField(pt.pos, (int)extra.x);
    pt.vel = lerp(pt.vel, f * 0.55, saturate(dt * 2.0));
    pt.pos += pt.vel * dt;
    pt.life -= dt;
    if (length(pt.pos) < 0.95 || pt.pos.y < -1.55) pt.life = 0.0;
    storeParticle(i, pt);
}

struct ParticleOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float alpha : TEXCOORD1; };

ParticleOut VSParticle(uint vid : SV_VertexID)
{
    uint pid = vid / 6;
    uint corner = vid % 6;
    static const float2 offs[6] = { float2(-1,-1), float2(1,-1), float2(-1,1), float2(-1,1), float2(1,-1), float2(1,1) };
    Particle pt;
    pt.pos = float3(particlesRO.Load(int3(ptexel(pid, 0), 0)), particlesRO.Load(int3(ptexel(pid, 1), 0)), particlesRO.Load(int3(ptexel(pid, 2), 0)));
    pt.life = particlesRO.Load(int3(ptexel(pid, 3), 0));
    pt.vel = 0.0;
    pt.seed = particlesRO.Load(int3(ptexel(pid, 7), 0));
    ParticleOut o;
    float4 c = mul(float4(pt.pos, 1.0), viewProj);
    float2 o2 = offs[corner];
    c.xy += o2 * particleSize * float2(resolution.y * invResolution.x, 1.0) * 0.004 * c.w / max(c.w, 1.0);
    o.pos = c;
    o.uv = o2;
    o.alpha = saturate(pt.life * 0.8) * saturate((9.0 - pt.life) * 0.5) * (0.4 + 0.6 * frac(pt.seed * 31.0));
    return o;
}

float4 PSParticle(ParticleOut i) : SV_Target
{
    float d = dot(i.uv, i.uv);
    float a = exp(-d * 4.0) * i.alpha;
    return float4(float3(0.75, 0.86, 1.0) * a * 1.6, 0.0);
}
