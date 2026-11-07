// VRAM bandwidth benchmark: coalesced 16-byte copy. Bytes moved = 2 * surface size per pass.
// 4096-wide R32G32B32A32_UINT textures: one 16-byte texel per element.
Texture2D<uint4>   bwSrc : register(t0);
RWTexture2D<uint4> bwDst : register(u0);

cbuffer CopyConstants : register(b1)
{
    uint bwElements;
    uint bwStride;
    uint bwPad0;
    uint bwPad1;
};

[numthreads(256, 1, 1)]
void CSCopy(uint3 id : SV_DispatchThreadID)
{
    [loop] for (uint e = id.x; e < bwElements; e += bwStride)
        { uint2 c = uint2(e & 4095u, e >> 12); bwDst[c] = bwSrc.Load(int3(c, 0)); }
}

// Fills a surface with incompressible pseudo-random data. Without this the copy would move zeros, which
// GPU memory compression shrinks drastically, producing inflated and run-to-run unstable bandwidth numbers.
RWTexture2D<uint4> bwFill : register(u1);

uint mixHash(uint x)
{
    x = (x ^ (x >> 16)) * 0x7feb352du;
    x = (x ^ (x >> 15)) * 0x846ca68bu;
    return x ^ (x >> 16);
}

[numthreads(256, 1, 1)]
void CSFill(uint3 id : SV_DispatchThreadID)
{
    [loop] for (uint e = id.x; e < bwElements; e += bwStride)
    {
        uint2 c = uint2(e & 4095u, e >> 12);
        uint h = mixHash(e * 4u + bwPad0 * 0x9E3779B9u);
        bwFill[c] = uint4(h, mixHash(h + 1u), mixHash(h + 2u), mixHash(h + 3u));
    }
}
