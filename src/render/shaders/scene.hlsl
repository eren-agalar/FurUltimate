// FurUltimate — "Nova Core" raymarched hero scene.
// A hollow gyroid titanium sphere with an emissive heart, orbital rings and a graphite pedestal,
// lit by a studio rig with soft shadows, multi-bounce reflections and volumetric light shafts.

static const float FLOOR_Y = -1.62;

struct Hit { float d; float mat; };

float coreShell(float3 q, out float innerD)
{
    float s = 4.4 + morph * 1.1 * sin(time * 0.31);
    float sph = sdSphere(q, 1.05);
    float g = abs(gyroid(q + float3(0.0, time * 0.05, 0.0), s)) - 0.032;
    float shell = max(sph, g * 0.62);
    shell = max(shell, -sdSphere(q, 0.80));
    innerD = sdSphere(q, 0.40 + 0.015 * sin(time * 2.1));
    return shell;
}

Hit mapScene(float3 p, bool detail)
{
    Hit h; h.d = p.y - FLOOR_Y; h.mat = 5.0;

    float ped = sdRoundCylinder(p - float3(0.0, FLOOR_Y + 0.07, 0.0), 1.05, 0.035, 0.07);
    if (ped < h.d) { h.d = ped; h.mat = 4.0; }

    float3 q = p - float3(0.0, 0.18 + 0.05 * sin(time * 0.6), 0.0);
    q = mul(rotY(time * 0.22), q);
    q = mul(rotX(0.32), q);

    float innerD;
    float shell = coreShell(q, innerD);
    if (detail && shell < 0.06 && detailOctaves > 0)
        shell += (fbm(q * 6.0, detailOctaves) - 0.5) * 0.012;
    if (shell < h.d) { h.d = shell; h.mat = 1.0; }
    if (innerD < h.d) { h.d = innerD; h.mat = 3.0; }

    float3 r1 = mul(rotX(1.15 + time * 0.37), q);
    float3 r2 = mul(rotZ(0.85 - time * 0.29), q);
    float rings = min(sdTorus(r1, float2(1.40, 0.020)), sdTorus(r2, float2(1.60, 0.014)));
    if (rings < h.d) { h.d = rings; h.mat = 2.0; }
    return h;
}

float march(float3 ro, float3 rd, int steps, out float mat)
{
    float t = 0.0; mat = -1.0;
    [loop] for (int i = 0; i < steps; ++i)
    {
        Hit h = mapScene(ro + rd * t, true);
        if (h.d < 0.0006 * (1.0 + t)) { mat = h.mat; return t; }
        t += h.d;
        if (t > 40.0) break;
    }
    return t;
}

float3 calcNormal(float3 p)
{
    const float2 k = float2(1.0, -1.0);
    const float e = 0.0008;
    return normalize(k.xyy * mapScene(p + k.xyy * e, true).d + k.yyx * mapScene(p + k.yyx * e, true).d +
                     k.yxy * mapScene(p + k.yxy * e, true).d + k.xxx * mapScene(p + k.xxx * e, true).d);
}

float softShadow(float3 ro, float3 rd, int steps, float tmax)
{
    float res = 1.0, t = 0.02;
    [loop] for (int i = 0; i < steps; ++i)
    {
        float h = mapScene(ro + rd * t, false).d;
        res = min(res, 10.0 * h / t);
        t += clamp(h, 0.02, 0.35);
        if (res < 0.002 || t > tmax) break;
    }
    return saturate(res);
}

float ambientOcclusion(float3 p, float3 n)
{
    float occ = 0.0, w = 1.0;
    [loop] for (int i = 1; i <= aoSamples; ++i)
    {
        float h = 0.025 + 0.06 * i;
        occ += (h - mapScene(p + n * h, false).d) * w;
        w *= 0.72;
    }
    return saturate(1.0 - 2.2 * occ);
}

struct Surface { float3 albedo; float metallic; float rough; float3 emissive; };

Surface materialFor(float mat, float3 p)
{
    Surface s; s.emissive = 0.0;
    if (mat < 1.5)      { s.albedo = float3(0.86, 0.88, 0.91); s.metallic = 1.0; s.rough = 0.16; }
    else if (mat < 2.5) { s.albedo = float3(0.78, 0.84, 0.92); s.metallic = 1.0; s.rough = 0.30; }
    else if (mat < 3.5)
    {
        s.albedo = 0.0; s.metallic = 0.0; s.rough = 1.0;
        float pulse = 0.85 + 0.15 * sin(time * 2.1);
        s.emissive = float3(0.56, 0.79, 1.0) * 3.2 * pulse;
    }
    else if (mat < 4.5) { s.albedo = float3(0.20, 0.21, 0.23); s.metallic = 0.75; s.rough = 0.38; }
    else
    {
        float r = length(p.xz);
        float grid = smoothstep(0.985, 1.0, max(abs(frac(p.x * 0.5) - 0.5), abs(frac(p.z * 0.5) - 0.5)) * 2.0);
        s.albedo = float3(0.020, 0.021, 0.024) + grid * 0.015 * exp(-r * 0.25);
        s.metallic = 0.0; s.rough = 0.10 + 0.04 * grid;
    }
    return s;
}

float3 coreLightPos() { return float3(0.0, 0.18 + 0.05 * sin(time * 0.6), 0.0); }

float3 directLight(float3 p, float3 n, float3 v, Surface s, bool shadows)
{
    float3 l = normalize(-lightDir);
    float sh = shadows ? softShadow(p + n * 0.003, l, shadowSteps, 12.0) : 1.0;
    float3 col = brdf(n, v, l, s.albedo, s.metallic, s.rough) * lightColor * sh;

    float3 lr = normalize(float3(-0.75, 0.35, 0.65));       // cool rim light
    col += brdf(n, v, lr, s.albedo, s.metallic, s.rough) * float3(0.35, 0.55, 0.85) * 1.6;

    float3 toCore = coreLightPos() - p;                      // emissive heart as a point light
    float dc = length(toCore);
    float3 lc = toCore / max(dc, 1e-3);
    float pulse = 0.85 + 0.15 * sin(time * 2.1);
    col += brdf(n, v, lc, s.albedo, s.metallic, s.rough) * float3(0.56, 0.79, 1.0) * (3.0 * pulse / (1.0 + dc * dc * 3.0));

    float amb = saturate(n.y * 0.5 + 0.5);
    col += s.albedo * (1.0 - s.metallic) * lerp(float3(0.008, 0.009, 0.011), float3(0.03, 0.033, 0.04), amb);
    return col;
}

float3 volumetrics(float3 ro, float3 rd, float tEnd, float2 px)
{
    if (volSamples <= 0) return 0.0;
    float3 spotPos = float3(0.6, 6.5, 0.4);
    float3 spotDir = normalize(float3(-0.08, -1.0, -0.05));
    float3 core = coreLightPos();
    float tMax = min(tEnd, 16.0);
    float stepLen = tMax / volSamples;
    float jitter = ign(px, frameIndex);
    float3 acc = 0.0;
    float trans = 1.0;
    int volShadow = max(4, shadowSteps / 4);
    [loop] for (int i = 0; i < volSamples; ++i)
    {
        float t = (i + jitter) * stepLen;
        float3 p = ro + rd * t;
        float dens = 0.020 * (0.35 + 0.65 * fbm(p * 0.55 + float3(0.0, -time * 0.07, time * 0.03), 2));
        dens *= exp(-max(p.y - FLOOR_Y, 0.0) * 0.18);

        float3 toSpot = spotPos - p;
        float ds = length(toSpot);
        float3 ls = toSpot / ds;
        float cone = smoothstep(0.86, 0.97, dot(-ls, spotDir));
        float3 scatter = 0.0;
        if (cone > 0.001)
        {
            float vis = softShadow(p, ls, volShadow, ds);
            float phase = 0.25 + 0.75 * pow(saturate(dot(rd, -ls) * 0.5 + 0.5), 4.0);
            scatter += cone * vis * phase * float3(1.0, 1.0, 1.04) * 9.0;
        }
        float dc = length(core - p);
        scatter += float3(0.56, 0.79, 1.0) * (0.9 / (1.0 + dc * dc * 4.0));
        acc += trans * dens * scatter * stepLen;
        trans *= exp(-dens * stepLen * 0.6);
    }
    return acc;
}

struct SceneOut { float4 color : SV_Target0; float depth : SV_Depth; };

SceneOut PSScene(FullscreenOut input)
{
    float2 px = input.pos.xy;
    float2 ndc = float2(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0);
    float4 nearP = mul(float4(ndc, 0.0, 1.0), invViewProj);
    float4 farP  = mul(float4(ndc, 1.0, 1.0), invViewProj);
    float3 ro0 = camPos;
    float3 rd0 = normalize(farP.xyz / farP.w - nearP.xyz / nearP.w);

    float3 ro = ro0, rd = rd0;
    float3 col = 0.0, throughput = 1.0;
    float firstT = 40.0, depth = 1.0;

    [loop] for (int b = 0; b <= reflBounces; ++b)
    {
        float mat;
        int steps = b == 0 ? marchSteps : max(24, marchSteps / (b + 1));
        float t = march(ro, rd, steps, mat);
        if (mat < 0.0) { col += throughput * studioEnv(rd); break; }
        float3 p = ro + rd * t;
        float3 n = calcNormal(p);
        if (b == 0)
        {
            firstT = t;
            float4 c = mul(float4(p, 1.0), viewProj);
            depth = saturate(c.z / c.w);
        }
        Surface s = materialFor(mat, p);
        float3 v = -rd;
        float3 lit = directLight(p, n, v, s, b == 0 || shadowSteps > 32);
        if (b == 0 && aoSamples > 0) lit *= lerp(0.35, 1.0, ambientOcclusion(p, n));
        float fade = mat > 4.5 ? exp(-max(length(p.xz) - 3.0, 0.0) * 0.22) : 1.0;
        col += throughput * (lit + s.emissive) * fade;

        float3 f0 = lerp(float3(0.04, 0.04, 0.04), s.albedo, s.metallic);
        float3 F = F_Schlick(f0, dot(n, v)) * (1.0 - s.rough * 0.65) * fade;
        if (mat > 2.5 && mat < 3.5) break;
        throughput *= F;
        float3 rdR = reflect(rd, n);
        if (b == reflBounces || max(throughput.x, max(throughput.y, throughput.z)) < 0.015)
        {
            col += throughput * studioEnv(normalize(lerp(rdR, n, s.rough * 0.5)));
            break;
        }
        ro = p + n * 0.004;
        rd = rdR;
    }

    col += volumetrics(ro0, rd0, firstT, px);
    float fogAmt = 1.0 - exp(-firstT * firstT * 0.0009);
    col = lerp(col, float3(0.008, 0.009, 0.011), fogAmt);

    SceneOut o;
    o.color = float4(col, 1.0);
    o.depth = depth;
    return o;
}
