///// Common /////////////////
struct RayPayload
{
	// rgb is color
	// w is distance. -1 means stop bouncing, i.e. miss or light source.
    float4 colorAndDistance;
};
	
struct ShadowPayload
{
    bool hit;
};
////////////////////////////////

RaytracingAccelerationStructure gRtScene : register(t0);
RWTexture2D<float4> gOutput : register(u0);

cbuffer Camera : register(b0)
{
    float3 cameraPosition;
    float4 cameraDirection;
}

float3 linearToSrgb(float3 c)
{
    // Based on http://chilliant.blogspot.com/2012/08/srgb-approximations-for-hlsl.html
    float3 sq1 = sqrt(c);
    float3 sq2 = sqrt(sq1);
    float3 sq3 = sqrt(sq2);
    float3 srgb = 0.662002687 * sq1 + 0.684122060 * sq2 - 0.323583601 * sq3 - 0.0225411470 * c;
    return srgb;
}

[shader("raygeneration")]
void rayGen()
{
    uint3 launchIndex = DispatchRaysIndex();
    uint3 launchDim = DispatchRaysDimensions();

    float2 crd = float2(launchIndex.xy);
    float2 dims = float2(launchDim.xy);

    float2 d = ((crd / dims) * 2.f - 1.f);
    float aspectRatio = dims.x / dims.y;

    float yRotAngle = cameraDirection.w;
    float3x3 yRotMat = float3x3(cos(yRotAngle), 0, sin(yRotAngle), 0, 1, 0, -sin(yRotAngle), 0, cos(yRotAngle));
    
    RayDesc ray;
    ray.Origin = cameraPosition; // float3(0, 0, -2);//;
    ray.Direction = normalize(mul(float3(d.x * aspectRatio, -d.y, 1), yRotMat));

    ray.TMin = 0;
    ray.TMax = 100000;

    RayPayload payload;
    TraceRay(gRtScene, 0 /*rayFlags*/, 0xFF, 0 /* ray index*/, 2, 0, ray, payload);
    float3 col = linearToSrgb(payload.colorAndDistance.rgb);

    if (payload.colorAndDistance.w > 0) // it's a hit!
    {
	// Fire a shadow ray. The direction is hard-coded here, but can be fetched from a constant-buffer
        float3 lightDir = normalize(float3(0.5, 0.5, -0.5)); // Take from Common!
	
        RayDesc shadowRay;
        shadowRay.Origin = ray.Origin + ray.Direction * payload.colorAndDistance.w;
        shadowRay.Direction = lightDir;
        shadowRay.TMin = 0.2;
        shadowRay.TMax = 100000;
        ShadowPayload shadowPayload;
        TraceRay(gRtScene, 0 /*rayFlags*/, 0xFF, 1 /* ray index*/, 0, 1, shadowRay, shadowPayload);

		float factor = shadowPayload.hit ? 0.1 : 1.0;
        col = col * factor;
    }


        gOutput[launchIndex.xy] = float4(col, 1);
    }

