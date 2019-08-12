///// Ray-tracing /////////////////
struct RayPayload
{
    float4 color; //packed as [float3(indirect), float(direct)]
    //float numRays;
    uint seed;
    //int depth;
};
	
struct ShadowPayload
{
    bool hit;
};

struct OfflineRayPayload
{
    float3 color;
    uint seed;
    int depth;
};

//// constants ////
static const float PI = 3.14159265f;

//// Post processing ///////////////
float makeDepthLinear(float oldDepth)
{
    float zPrim = oldDepth;
    float f = 100.0f; // Sync this value to the C++ code!
    float n = 0.1f;
		// Transform to linear view space
    float z = f * n / (f - zPrim * (f - n));
    zPrim = z * zPrim;
    zPrim /= f; // z <- 0..1
    return zPrim;
};

static const float2 gTexCoords[6] =
{
    float2(0.0f, 1.0f),
	float2(0.0f, 0.0f),
	float2(1.0f, 0.0f),
	float2(0.0f, 1.0f),
	float2(1.0f, 0.0f),
	float2(1.0f, 1.0f)
};

// pack/unpack normals
uint dirToOct(float3 normal)
{
    float2 p = normal.xy * (1.0 / dot(abs(normal), float3(1.0f, 1.0f, 1.0f)));
    float2 e = normal.z > 0.0 ? p : (1.0 - abs(p.yx)) * (step(0.0, p) * 2.0 - (float2) (1.0));
    return (asuint(f32tof16(e.y)) << 16) + (asuint(f32tof16(e.x)));
}
float3 oct_to_dir(uint octo)
{
    float2 e = float2(f16tof32((octo) & 0xffff), f16tof32((octo >> 16) & 0xffff));
    float3 v = float3(e, 1.0 - abs(e.x) - abs(e.y));
    if (v.z < 0.0)
        v.xy = (1.0 - abs(v.yx)) * (step(0.0, v.xy) * 2.0 - (float2) (1.0));
    return normalize(v);
}