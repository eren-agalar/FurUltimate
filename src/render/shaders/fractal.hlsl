// Shader-performance workload: power-8 Mandelbulb distance estimator (ALU bound).
float mandelbulbDE(float3 p, int iters, out float trap)
{
    float3 z = p;
    float dr = 1.0, r = 0.0;
    trap = 1e9;
    [loop] for (int i = 0; i < iters; ++i)
    {
        r = length(z);
        if (r > 2.0) break;
        float theta = acosp(clamp(z.z / r, -1.0, 1.0)) * 8.0;
        float phi = atan2p(z.y, z.x) * 8.0;
        float zr = pow(r, 8.0);
        dr = pow(r, 7.0) * 8.0 * dr + 1.0;
        z = zr * float3(sin(theta) * cos(phi), sin(phi) * sin(theta), cos(theta)) + p;
        trap = min(trap, dot(z, z));
    }
    return 0.5 * log(max(r, 1e-6)) * r / dr;
}

float4 PSFractal(FullscreenOut input) : SV_Target
{
    float2 ndc = float2(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0);
    float4 nearP = mul(float4(ndc, 0.0, 1.0), invViewProj);
    float4 farP  = mul(float4(ndc, 1.0, 1.0), invViewProj);
    float3 ro = camPos;
    float3 rd = normalize(farP.xyz / farP.w - nearP.xyz / nearP.w);
    int iters = detailOctaves;
    float t = 0.0, trap = 0.0;
    int i;
    bool hit = false;
    [loop] for (i = 0; i < marchSteps; ++i)
    {
        float d = mandelbulbDE(ro + rd * t, iters, trap);
        if (d < 0.0004 * (1.0 + t)) { hit = true; break; }
        t += d;
        if (t > 6.0) break;
    }
    float3 col = studioEnv(rd) * 0.5;
    if (hit)
    {
        float3 p = ro + rd * t;
        float tr;
        const float e = 0.0006;
        float3 n = normalize(float3(
            mandelbulbDE(p + float3(e, 0, 0), iters, tr) - mandelbulbDE(p - float3(e, 0, 0), iters, tr),
            mandelbulbDE(p + float3(0, e, 0), iters, tr) - mandelbulbDE(p - float3(0, e, 0), iters, tr),
            mandelbulbDE(p + float3(0, 0, e), iters, tr) - mandelbulbDE(p - float3(0, 0, e), iters, tr)));
        float ao = saturate(1.0 - (float)i / marchSteps);
        float3 albedo = lerp(float3(0.78, 0.80, 0.84), float3(0.56, 0.79, 1.0), saturate(sqrt(trap) * 0.6));
        float3 v = -rd;
        col = brdf(n, v, normalize(-lightDir), albedo, 0.85, 0.28) * lightColor;
        col += brdf(n, v, normalize(float3(-0.7, 0.2, 0.6)), albedo, 0.85, 0.35) * float3(0.4, 0.6, 0.9);
        col += F_Schlick(albedo * 0.8, dot(n, v)) * studioEnv(reflect(rd, n)) * 0.6;
        col *= ao * ao;
    }
    return float4(col, 1.0);
}
