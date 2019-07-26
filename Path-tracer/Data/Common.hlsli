///// Ray-tracing /////////////////
struct RayPayload
{
    float4 indirectColor;
    float3 directColor;
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