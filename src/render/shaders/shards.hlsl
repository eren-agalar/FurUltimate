// Procedural geometry workload: orbiting crystalline shards generated entirely in the vertex shader.
// Each instance is a 12-sided bicone (24 triangles); no vertex buffers are used.

static const uint SHARD_SIDES = 12;
static const uint SHARD_VERTS = SHARD_SIDES * 6;   // 2 cones * sides * 3

struct ShardOut
{
    float4 pos    : SV_Position;
    float3 world  : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float  tint   : TEXCOORD2;
};

float3 shardLocalVertex(uint v)
{
    uint tri = v / 3;
    uint corner = v % 3;
    uint side = tri % SHARD_SIDES;
    bool top = tri < SHARD_SIDES;
    if (corner == 0) return float3(0.0, top ? 1.0 : -1.0, 0.0);
    uint s = (corner == 1) == top ? side : (side + 1) % SHARD_SIDES;
    float a = (float)s / SHARD_SIDES * 2.0 * PI;
    return float3(cos(a) * 0.32, 0.0, sin(a) * 0.32);
}

float3 shardTransform(float3 lp, uint inst, out float tint)
{
    float h0 = hash11(inst * 1.37 + 0.5), h1 = hash11(inst * 7.11 + 2.0), h2 = hash11(inst * 3.97 + 5.0), h3 = hash11(inst * 0.73 + 9.0);
    // A thin, slightly tilted debris ring orbiting the core (Keplerian speeds).
    float radius = 1.85 + h0 * h0 * 1.7;
    float speed = 0.55 / sqrt(radius);
    float angle = h1 * 2.0 * PI + time * speed;
    float height = (h2 - 0.5) * 0.18 * (radius - 1.4) + 0.12;
    float size = 0.010 + h3 * h3 * 0.028;
    float3x3 spin = mul(rotY(time * (0.6 + h2 * 1.8) + h0 * 9.0), rotX(h3 * 6.28 + time * (0.4 + h1)));
    float3 p = mul(spin, lp * float3(size, size * (1.6 + h1 * 1.6), size));
    tint = h2;
    float3 ringPos = mul(rotX(0.32), float3(cos(angle) * radius, height, sin(angle) * radius));
    return p + ringPos;
}

ShardOut VSShard(uint gvid : SV_VertexID)
{
    // One non-instanced draw: instance index derived from the vertex id (portable across HLSL toolchains).
    uint inst = gvid / SHARD_VERTS;
    uint vid = gvid % SHARD_VERTS;
    uint triBase = (vid / 3) * 3;
    float tint;
    float3 w0 = shardTransform(shardLocalVertex(triBase + 0), inst, tint);
    float3 w1 = shardTransform(shardLocalVertex(triBase + 1), inst, tint);
    float3 w2 = shardTransform(shardLocalVertex(triBase + 2), inst, tint);
    float3 w = vid % 3 == 0 ? w0 : (vid % 3 == 1 ? w1 : w2);
    ShardOut o;
    o.world = w;
    o.normal = normalize(cross(w1 - w0, w2 - w0));
    o.tint = tint;
    o.pos = mul(float4(w, 1.0), viewProj);
    return o;
}

float4 PSShard(ShardOut i) : SV_Target
{
    float3 n = normalize(i.normal);
    float3 v = normalize(camPos - i.world);
    if (dot(n, v) < 0.0) n = -n;
    float3 albedo = lerp(float3(0.70, 0.72, 0.76), float3(0.62, 0.78, 0.95), step(0.82, i.tint));
    float rough = 0.22 + 0.25 * frac(i.tint * 17.0);
    float3 col = brdf(n, v, normalize(-lightDir), albedo, 1.0, rough) * lightColor;
    float3 toCore = float3(0.0, 0.18, 0.0) - i.world;
    float dc = length(toCore);
    col += brdf(n, v, toCore / dc, albedo, 1.0, rough) * float3(0.56, 0.79, 1.0) * (3.0 / (1.0 + dc * dc * 2.0));
    float3 f0 = albedo;
    col += F_Schlick(f0, dot(n, v)) * studioEnv(reflect(-v, n)) * (1.0 - rough * 0.6);
    float fog = 1.0 - exp(-dot(i.world - camPos, i.world - camPos) * 0.0009);
    col = lerp(col, float3(0.008, 0.009, 0.011), fog);
    return float4(col, 1.0);
}
