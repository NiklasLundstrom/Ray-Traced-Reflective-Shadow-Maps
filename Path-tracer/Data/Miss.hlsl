#include "Common.hlsli"
 
Texture2D<float4> gHDRTexture : register(t0);
//SamplerState gSampler : register(s0);


[shader("miss")]
void miss(inout RayPayload payload)
{
    uint width, height;
    gHDRTexture.GetDimensions(width, height);

    float3 dir = WorldRayDirection();
    dir.y = -dir.y;

    float r = (1 / PI) * acos(dir.z) / length(dir.xy);
    float4 c = gHDRTexture[uint2(((dir.xy * r) * 0.5 + 0.5) * uint2(width, height))].rgba;
        
    payload.color = c.rgb; //(c.r>0 ? 1.0 : 0.2) * float3(1.0, 1.0, 1.0);
	
	//

    }
