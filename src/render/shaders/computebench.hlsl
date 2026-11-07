// Compute-performance benchmark: dependent FMA chains. FLOPs per thread are exactly known:
//   iterations * 16 (unroll) * 4 (float4 chains) * 4 (lanes) * 2 (mul + add)
RWBuffer<float4> fmaOut : register(u0);

cbuffer FmaConstants : register(b1)
{
    uint fmaIterations;
    float fmaMul;
    float fmaAdd;
    float fmaSeed;
};

[numthreads(256, 1, 1)]
void CSFma(uint3 id : SV_DispatchThreadID)
{
    float s = (float)id.x * 1e-7 + fmaSeed;
    float4 a = float4(s, s + 0.1, s + 0.2, s + 0.3);
    float4 b = a + 0.5, c = a + 0.25, d = a + 0.75;
    [loop] for (uint i = 0; i < fmaIterations; ++i)
    {
        [unroll] for (int k = 0; k < 16; ++k)
        {
            a = a * fmaMul + fmaAdd;   // compiled to a single mad instruction
            b = b * fmaMul + fmaAdd;
            c = c * fmaMul + fmaAdd;
            d = d * fmaMul + fmaAdd;
        }
    }
    fmaOut[id.x] = a + b + c + d;
}
