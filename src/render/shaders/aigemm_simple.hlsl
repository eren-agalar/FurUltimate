// AI performance (shader path, cache-based variant): FP32 matrix multiplication C = A x B, the core operation of neural networks.
// Square matrices, size multiple of 64. Each 16x16 thread group computes a 64x64 tile of C; each thread a 4x4 block.
// FLOPs per dispatch = 2 * size^3 (one multiply + one add per multiply-accumulate).
Texture2D<float4>   gemmA : register(t0);   // A[row][k]: texel (k/4, row)
Texture2D<float4>   gemmB : register(t1);   // B[k][col]: texel (col/4, k)
RWTexture2D<float4> gemmC : register(u0);   // C[row][col]: texel (col/4, row)

// Fallback kernel without group-shared memory (for HLSL toolchains lacking barriers, e.g. vkd3d under Wine).
// Same work and FLOP count; operands are read through the texture cache instead of shared memory.
[numthreads(16, 16, 1)]
void CSGemmSimple(uint3 dt : SV_DispatchThreadID)
{
    uint w, h;
    gemmA.GetDimensions(w, h);
    uint col4 = dt.x, row = dt.y * 4;
    float4 acc0 = 0, acc1 = 0, acc2 = 0, acc3 = 0;
    [loop] for (uint k4 = 0; k4 < w; ++k4)
    {
        float4 a0 = gemmA.Load(int3(k4, row + 0, 0)), a1 = gemmA.Load(int3(k4, row + 1, 0));
        float4 a2 = gemmA.Load(int3(k4, row + 2, 0)), a3 = gemmA.Load(int3(k4, row + 3, 0));
        [unroll] for (uint j = 0; j < 4; ++j)
        {
            float4 b = gemmB.Load(int3(col4, k4 * 4 + j, 0));
            acc0 += a0[j] * b; acc1 += a1[j] * b; acc2 += a2[j] * b; acc3 += a3[j] * b;
        }
    }
    gemmC[uint2(col4, row + 0)] = acc0;
    gemmC[uint2(col4, row + 1)] = acc1;
    gemmC[uint2(col4, row + 2)] = acc2;
    gemmC[uint2(col4, row + 3)] = acc3;
}
