#include "ToneMapping.hlsli"
#include "Common.hlsli"

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

PSInput VSMain(uint index : SV_VertexID)
{
    PSInput vsOutput;

    vsOutput.uv = gTexCoords[index];

	// Quad covering screen in NDC space.
    vsOutput.position = float4(2.0f * vsOutput.uv.x - 1.0f, 1.0f - 2.0f * vsOutput.uv.y, 0.0f, 1.0f);

    return vsOutput;
}


Texture2D<float4> gIndirectInput : register(t0);
Texture2D<float4> gDirectInput : register(t1);
Texture2D<float4> gShadowMap_Normal : register(t2);
Texture2D<float4> gMotionVectors : register(t3);
Texture2D<float4> gGbufferColor : register(t4);


float4 PSMain(PSInput input) : SV_TARGET
{
    uint masterWidth;
    uint masterHeight;
    gIndirectInput.GetDimensions(masterWidth, masterHeight);
    float2 crd = input.uv * float2(masterWidth, masterHeight);
    float3 light = gIndirectInput[crd].rgb + gDirectInput[crd].rgb;
	// Tone Map
    float3 output = light * gGbufferColor[crd].rgb;
    output = linearToSrgb(ACESFitted(2.5 * output.rgb));


	// Render Shadow map to the side
    uint shadowWidth;
    uint shadowHeight;
    gShadowMap_Normal.GetDimensions(shadowWidth, shadowHeight);
    int scale = 4;
    if (crd.x < shadowWidth / scale && crd.y < shadowHeight / scale)
    {
        uint2 coords = crd;
        coords.y -= 0 * shadowHeight / scale;
        float3 cPrim = gShadowMap_Normal[coords * scale].rgb;
        output = cPrim;
    }
    else if (crd.x < shadowWidth / scale && crd.y < 2 * shadowHeight / scale)
    {
        uint2 coords = crd;
        coords.y -= 1 * shadowHeight / scale;
        float4 motionVectors = gMotionVectors[coords * scale * 2];
        motionVectors.y *= -1;
        float3 cPrim = float3(5.0f * motionVectors.rg + 0.5f, 1.0f - 0.5f * motionVectors.b);
        output = cPrim;
    }

    return float4(output, 1);
}