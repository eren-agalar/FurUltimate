// Generates a tileable 3D noise volume used by the texture-sampling workload.
RWTexture3D<float4> noiseOut : register(u0);

float tileNoise(float3 p, float period)
{
    float3 i = floor(p); float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float r[8];
    [unroll] for (int k = 0; k < 8; ++k)
    {
        float3 o = float3(k & 1, (k >> 1) & 1, (k >> 2) & 1);
        r[k] = hash31(fmod(i + o, period) + 0.5);
    }
    return lerp(lerp(lerp(r[0], r[1], f.x), lerp(r[2], r[3], f.x), f.y),
                lerp(lerp(r[4], r[5], f.x), lerp(r[6], r[7], f.x), f.y), f.z);
}

[numthreads(4, 4, 4)]
void CSNoise(uint3 id : SV_DispatchThreadID)
{
    float3 p = (float3)id / 128.0;
    float4 v;
    v.x = tileNoise(p * 8.0, 8.0) * 0.5 + tileNoise(p * 16.0, 16.0) * 0.3 + tileNoise(p * 32.0, 32.0) * 0.2;
    v.y = tileNoise(p * 4.0, 4.0);
    v.z = tileNoise(p * 16.0, 16.0);
    v.w = tileNoise(p * 32.0, 32.0);
    noiseOut[id] = v;
}
