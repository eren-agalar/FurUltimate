// AI performance (shader path): tiled FP32 matrix multiplication C = A x B, the core operation of neural networks.
// Square matrices, size multiple of 64. Each 16x16 thread group computes a 64x64 tile of C; each thread a 4x4 block.
// FLOPs per dispatch = 2 * size^3 (one multiply + one add per multiply-accumulate).
Texture2D<float4>   gemmA : register(t0);   // A[row][k]: texel (k/4, row)
Texture2D<float4>   gemmB : register(t1);   // B[k][col]: texel (col/4, k)
RWTexture2D<float4> gemmC : register(u0);   // C[row][col]: texel (col/4, row)

groupshared float  tileA[16][64];   // [k][row]
groupshared float4 tileB[16][16];   // [k][col/4]

[numthreads(16, 16, 1)]
void CSGemm(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID, uint flat : SV_GroupIndex)
{
    uint w, h;
    gemmA.GetDimensions(w, h);
    uint kTexels = w;                 // size / 4
    uint rowBase = gid.y * 64;
    uint colBase4 = gid.x * 16;
    float4 acc0 = 0, acc1 = 0, acc2 = 0, acc3 = 0;
    [loop] for (uint k4 = 0; k4 < kTexels; k4 += 4)
    {
        // A tile: 64 rows x 16 k  -> 256 texels, one per thread
        uint ar = flat >> 2, ak = flat & 3;
        float4 va = gemmA.Load(int3(k4 + ak, rowBase + ar, 0));
        tileA[ak * 4 + 0][ar] = va.x; tileA[ak * 4 + 1][ar] = va.y; tileA[ak * 4 + 2][ar] = va.z; tileA[ak * 4 + 3][ar] = va.w;
        // B tile: 16 k x 64 cols -> 256 texels
        uint bk = flat >> 4, bc = flat & 15;
        tileB[bk][bc] = gemmB.Load(int3(colBase4 + bc, k4 * 4 + bk, 0));
        GroupMemoryBarrierWithGroupSync();
        [unroll] for (uint kk = 0; kk < 16; ++kk)
        {
            float4 b = tileB[kk][tid.x];
            acc0 += tileA[kk][tid.y * 4 + 0] * b;
            acc1 += tileA[kk][tid.y * 4 + 1] * b;
            acc2 += tileA[kk][tid.y * 4 + 2] * b;
            acc3 += tileA[kk][tid.y * 4 + 3] * b;
        }
        GroupMemoryBarrierWithGroupSync();
    }
    uint col4 = colBase4 + tid.x, row = rowBase + tid.y * 4;
    gemmC[uint2(col4, row + 0)] = acc0;
    gemmC[uint2(col4, row + 1)] = acc1;
    gemmC[uint2(col4, row + 2)] = acc2;
    gemmC[uint2(col4, row + 3)] = acc3;
}

