#include "../Common.hlsli"
 
Texture2D<float4> gHDRTexture : register(t0);
SamplerState gSampler : register(s0);


[shader("miss")]
void miss(inout OfflineRayPayload payload)
{
    //uint width, height;
    //gHDRTexture.GetDimensions(width, height);

    //float3 dir = WorldRayDirection();
    //dir.y = -dir.y;

    //float r = (1 / PI) * acos(dir.z) / length(dir.xy);
    //float2 coord = float2(((dir.xy * r) * 0.5 + 0.5));// * float2(width, height));
    //float4 c = gHDRTexture.SampleLevel(gSampler, coord, 0).rgba;
        
    //payload.color = c.rgb;
	
    payload.color = float3(0.0, 0.0, 0.0); //float3(201.0, 226.0, 255.0) * 5 / 255.0;

    }
