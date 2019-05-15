#include "Common.hlsli"
#include "hlslUtils.hlsli"

RaytracingAccelerationStructure gRtScene : register(t0);
RWTexture2D<float4> gOutput : register(u0);

cbuffer Camera : register(b0)
{
    float3 cameraPosition;
    float cameraYAngle;
    int frameCount;
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

    float3x3 yRotMat = float3x3(cos(cameraYAngle), 0, sin(cameraYAngle), 0, 1, 0, -sin(cameraYAngle), 0, cos(cameraYAngle));
    
    RayDesc ray;
    ray.Origin = cameraPosition.xyz;
    ray.Direction = normalize(mul(float3(d.x * aspectRatio, -d.y, 1), yRotMat));

    ray.TMin = 0;
    ray.TMax = 100000;

    uint randSeed = initRand(launchIndex.x + launchIndex.y * launchDim.x, frameCount, 16);
            
    RayPayload payload;
    float3 color = float3(0.0, 0.0, 0.0);

    int numSamples = 1;
    for (int i = 0; i < numSamples; i++)
    {
        nextRand(randSeed);

        payload.depth = 1;
        payload.seed = randSeed;
        TraceRay(
					gRtScene,
					0 /*rayFlags*/, 
					0xFF /*ray mask*/, 
					0 /* ray index*/, 
					2 /* total nbr of hit groups*/, 
					0 /* miss shader index*/, 
					ray, 
					payload
				);
        color += payload.color;
    }
    color /= numSamples;

    gOutput[launchIndex.xy] = float4(color, 1);
}

