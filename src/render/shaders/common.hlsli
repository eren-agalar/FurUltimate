// FurUltimate — shared HLSL definitions (Shader Model 5.0)
// Copyright (c) FurUltimate contributors. All original work.

cbuffer FrameConstants : register(b0)
{
    row_major float4x4 viewProj;
    row_major float4x4 invViewProj;
    float3 camPos;        float time;
    float2 resolution;    float2 invResolution;
    int   marchSteps;     int   reflBounces;   int shadowSteps;  int volSamples;
    int   aoSamples;      int   detailOctaves; int useNoiseTex;  float morph;
    float3 lightDir;      float exposure;
    float3 lightColor;    float particleSize;
    uint  particleCount;  uint  shardCount;    uint frameIndex;  float bloomStrength;
    float4 extra;         // x: compute iterations, y: delta time, z: grain, w: vignette
};

Texture3D<float4> noiseTex : register(t0);
SamplerState linearWrap   : register(s0);
SamplerState linearClamp  : register(s1);

static const float PI = 3.14159265;

struct FullscreenOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

FullscreenOut VSFullscreen(uint id : SV_VertexID)
{
    FullscreenOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.uv = uv;
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

// Portable inverse trigonometry (minimax approximations, max error < 1e-5 rad). Avoids relying on
// compiler intrinsics that some HLSL toolchains (e.g. vkd3d-shader under Wine) do not provide.
float atanUnit(float x)
{
    float x2 = x * x;
    return x * (0.99997726 + x2 * (-0.33262347 + x2 * (0.19354346 + x2 * (-0.11643287 + x2 * (0.05265332 + x2 * -0.01172120)))));
}
float atan2p(float y, float x)
{
    float ax = abs(x), ay = abs(y);
    float r = atanUnit(min(ax, ay) / max(max(ax, ay), 1e-20));
    if (ay > ax) r = 1.57079637 - r;
    if (x < 0.0) r = 3.14159274 - r;
    return y < 0.0 ? -r : r;
}
float acosp(float x)
{
    float ax = abs(x);
    float r = sqrt(1.0 - ax) * (1.5707288 + ax * (-0.2121144 + ax * (0.0742610 - 0.0187293 * ax)));
    return x < 0.0 ? 3.14159265 - r : r;
}

float hash11(float p) { p = frac(p * 0.1031); p *= p + 33.33; p *= p + p; return frac(p); }
float hash31(float3 p) { p = frac(p * 0.1031); p += dot(p, p.zyx + 31.32); return frac((p.x + p.y) * p.z); }
float3 hash33(float3 p)
{
    p = frac(p * float3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return frac((p.xxy + p.yxx) * p.zyx);
}
float ign(float2 px, uint frame) // interleaved gradient noise
{
    px += 5.588238 * float(frame & 63u);
    return frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715))));
}

float valueNoise(float3 p)
{
    float3 i = floor(p); float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash31(i), n100 = hash31(i + float3(1,0,0));
    float n010 = hash31(i + float3(0,1,0)), n110 = hash31(i + float3(1,1,0));
    float n001 = hash31(i + float3(0,0,1)), n101 = hash31(i + float3(1,0,1));
    float n011 = hash31(i + float3(0,1,1)), n111 = hash31(i + float3(1,1,1));
    return lerp(lerp(lerp(n000, n100, f.x), lerp(n010, n110, f.x), f.y),
                lerp(lerp(n001, n101, f.x), lerp(n011, n111, f.x), f.y), f.z);
}

// Fractal noise: texture-sampled (texture workload) or ALU (procedural) depending on useNoiseTex.
float fbm(float3 p, int octaves)
{
    float sum = 0.0, amp = 0.5, norm = 0.0;
    [loop] for (int i = 0; i < octaves; ++i)
    {
        float n = useNoiseTex != 0 ? noiseTex.SampleLevel(linearWrap, p * 0.125, 0).x : valueNoise(p);
        sum += n * amp; norm += amp;
        p = p * 2.03 + float3(1.7, 9.2, 3.1);
        amp *= 0.5;
    }
    return norm > 0.0 ? sum / norm : 0.5;
}

float3x3 rotX(float a) { float s = sin(a), c = cos(a); return float3x3(1,0,0, 0,c,-s, 0,s,c); }
float3x3 rotY(float a) { float s = sin(a), c = cos(a); return float3x3(c,0,s, 0,1,0, -s,0,c); }
float3x3 rotZ(float a) { float s = sin(a), c = cos(a); return float3x3(c,-s,0, s,c,0, 0,0,1); }

float sdSphere(float3 p, float r) { return length(p) - r; }
float sdTorus(float3 p, float2 t) { float2 q = float2(length(p.xz) - t.x, p.y); return length(q) - t.y; }
float sdRoundCylinder(float3 p, float r, float rb, float h)
{
    float2 d = float2(length(p.xz) - r + rb, abs(p.y) - h);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - rb;
}
float gyroid(float3 p, float s) { p *= s; return dot(sin(p), cos(p.zxy)) / s; }

// Procedural studio environment: dark graphite cyclorama with soft-box and strip lights.
float3 studioEnv(float3 rd)
{
    float y = rd.y;
    float3 col = lerp(float3(0.010, 0.011, 0.013), float3(0.085, 0.092, 0.105), saturate(y * 0.6 + 0.45));
    float top = smoothstep(0.78, 0.97, y);
    col += top * float3(1.70, 1.75, 1.85);
    float az = atan2p(rd.z, rd.x);
    float band = smoothstep(-0.25, 0.05, y) * smoothstep(0.75, 0.35, y);
    float s1 = smoothstep(0.075, 0.0, abs(az - 1.95)) * band;
    float s2 = smoothstep(0.050, 0.0, abs(az + 1.15)) * band;
    float s3 = smoothstep(0.030, 0.0, abs(az - 0.35)) * smoothstep(0.0, 0.2, y) * smoothstep(0.5, 0.25, y);
    col += s1 * float3(1.25, 1.28, 1.34);
    col += s2 * float3(0.85, 1.25, 1.75);       // performance-accent cool strip
    col += s3 * float3(0.9, 0.92, 0.95);
    col += exp(-abs(y) * 22.0) * float3(0.035, 0.045, 0.060);
    return col;
}

// --- Physically based shading -------------------------------------------------
float D_GGX(float nh, float a) { float a2 = a * a; float d = nh * nh * (a2 - 1.0) + 1.0; return a2 / (PI * d * d + 1e-6); }
float V_SmithGGX(float nv, float nl, float a)
{
    float k = a * 0.5;
    return 0.25 / ((nv * (1.0 - k) + k) * (nl * (1.0 - k) + k) + 1e-5);
}
float3 F_Schlick(float3 f0, float c) { return f0 + (1.0 - f0) * pow(1.0 - saturate(c), 5.0); }

float3 brdf(float3 n, float3 v, float3 l, float3 albedo, float metallic, float rough)
{
    float3 h = normalize(v + l);
    float nl = saturate(dot(n, l)), nv = saturate(dot(n, v)) + 1e-4, nh = saturate(dot(n, h)), vh = saturate(dot(v, h));
    float a = max(rough * rough, 0.002);
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 F = F_Schlick(f0, vh);
    float3 spec = D_GGX(nh, a) * V_SmithGGX(nv, nl, a) * F;
    float3 diff = (1.0 - F) * (1.0 - metallic) * albedo / PI;
    return (diff + spec) * nl;
}

float3 acesFilm(float3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}
float3 linearToSrgb(float3 c)
{
    c = saturate(c);
    return c < 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}
