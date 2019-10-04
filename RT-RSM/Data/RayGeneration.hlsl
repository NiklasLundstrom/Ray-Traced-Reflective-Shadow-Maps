#include "Common.hlsli"
#include "hlslUtils.hlsli"

RaytracingAccelerationStructure gRtScene : register(t0);
RWTexture2D<float4> gOutput : register(u0);

cbuffer Camera : register(b0)
{
    float4x4 viewMatInv;
    float4x4 projMatInv;
    float3 cameraPosition;
    float cameraYAngle;
    int frameCount;
};

[shader("raygeneration")]
void rayGen()
{
    uint3 launchIndex = DispatchRaysIndex();
    uint3 launchDim = DispatchRaysDimensions();

    float2 crd = float2(launchIndex.xy);
    float2 dims = float2(launchDim.xy);

    float2 d = (((crd + 0.5f) / dims) * 2.f - 1.f);
    float aspectRatio = dims.x / dims.y;

    float3x3 yRotMat = float3x3(cos(cameraYAngle), 0, sin(cameraYAngle), 0, 1, 0, -sin(cameraYAngle), 0, cos(cameraYAngle));
    
    RayDesc ray;
    ray.Origin = mul(viewMatInv, float4(0, 0, 0, 1));// cameraPosition.xyz;
    float4 target = mul(projMatInv, float4(d.x /** aspectRatio*/, -d.y, 1, 1));
    ray.Direction = mul(viewMatInv, float4(target.xyz, 0)); //normalize(mul(float3(d.x * aspectRatio, -d.y, 1), yRotMat));

    ray.TMin = 0;
    ray.TMax = 100000;

    uint randSeed = initRand(launchIndex.x + launchIndex.y * launchDim.x, frameCount, 16);
            
    RayPayload payload;

    nextRand(randSeed);

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
    float4 color = payload.color.rgba; //[rgb=indirect, a=direct]

    gOutput[launchIndex.xy] = color;
}

