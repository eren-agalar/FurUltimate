// Post-processing: bloom (bright-pass + separable blur) and filmic tonemapping.
Texture2D<float4> srcTex   : register(t3);
Texture2D<float4> bloomTex : register(t4);

float4 PSBright(FullscreenOut i) : SV_Target
{
    // 4 bilinear taps covering a 4x4 footprint, Karis-weighted to suppress fireflies, soft-knee threshold.
    float2 texel = invResolution;     // invResolution = 1 / source size here
    float3 c = 0.0;
    float wsum = 0.0;
    [unroll] for (int k = 0; k < 4; ++k)
    {
        float2 o = float2((k & 1) ? 1.0 : -1.0, (k & 2) ? 1.0 : -1.0);
        float3 s = srcTex.SampleLevel(linearClamp, i.uv + texel * o, 0).rgb;
        float w = 1.0 / (1.0 + dot(s, float3(0.2126, 0.7152, 0.0722)));
        c += s * w; wsum += w;
    }
    c /= wsum;
    float l = dot(c, float3(0.2126, 0.7152, 0.0722));
    const float threshold = 1.1, knee = 0.6;
    float soft = clamp(l - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-4);
    float k2 = max(soft, l - threshold) / max(l, 1e-4);
    return float4(min(c * k2, 8.0), 1.0);
}

static const float W[7] = { 0.1736, 0.1576, 0.1180, 0.0729, 0.0371, 0.0156, 0.0054 };

float4 blur(float2 uv, float2 dir)
{
    float3 c = srcTex.SampleLevel(linearClamp, uv, 0).rgb * W[0];
    [unroll] for (int k = 1; k < 7; ++k)
    {
        c += srcTex.SampleLevel(linearClamp, uv + dir * k * 1.25, 0).rgb * W[k];
        c += srcTex.SampleLevel(linearClamp, uv - dir * k * 1.25, 0).rgb * W[k];
    }
    return float4(c, 1.0);
}
float4 PSBlurH(FullscreenOut i) : SV_Target { return blur(i.uv, float2(invResolution.x, 0.0)); }
float4 PSBlurV(FullscreenOut i) : SV_Target { return blur(i.uv, float2(0.0, invResolution.y)); }

float4 PSTonemap(FullscreenOut i) : SV_Target
{
    float3 hdr = srcTex.SampleLevel(linearClamp, i.uv, 0).rgb;
    hdr += bloomTex.SampleLevel(linearClamp, i.uv, 0).rgb * bloomStrength;
    float3 c = acesFilm(hdr * exposure);
    float2 d = i.uv - 0.5;
    c *= lerp(1.0, saturate(1.0 - dot(d, d) * 1.6), extra.w);
    float grain = (hash31(float3(i.pos.xy, frameIndex & 255u)) - 0.5) * extra.z;
    return float4(linearToSrgb(c) + grain, 1.0);
}
