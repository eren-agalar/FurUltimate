// Memory-intensive workload: streams a large buffer through the GPU (read + transform + write).
// Working set stored as 4096-wide R32G32B32A32_UINT textures (16 bytes per texel).
Texture2D<uint4>   memSrc : register(t2);
RWTexture2D<uint4> memDst : register(u1);

cbuffer MemoryConstants : register(b1)
{
    uint memElements;   // number of 16-byte elements
    uint memStride;     // total threads in the dispatch (grid stride)
    uint memSalt;
    uint memPad;
};

[numthreads(256, 1, 1)]
void CSMemory(uint3 id : SV_DispatchThreadID)
{
    [loop] for (uint e = id.x; e < memElements; e += memStride)
    {
        uint2 c = uint2(e & 4095u, e >> 12);
        uint4 v = memSrc.Load(int3(c, 0));
        v = v * 1664525u + (1013904223u ^ memSalt);
        memDst[c] = v;
    }
}
