// inspired by https://github.com/d3dcoder/d3d12book/blob/master/Chapter%2021%20Ambient%20Occlusion/Ssao/Shaders/SsaoBlur.hlsl

#include "ToneMapping.hlsli"

float3 linearToSrgb(float3 c)
{
    // Based on http://chilliant.blogspot.com/2012/08/srgb-approximations-for-hlsl.html
    float3 sq1 = sqrt(c);
    float3 sq2 = sqrt(sq1);
    float3 sq3 = sqrt(sq2);
    float3 srgb = 0.662002687 * sq1 + 0.684122060 * sq2 - 0.323583601 * sq3 - 0.0225411470 * c;
    return srgb;
}

static const float2 gTexCoords[6] =
{
    float2(0.0f, 1.0f),
    float2(0.0f, 0.0f),
    float2(1.0f, 0.0f),
    float2(0.0f, 1.0f),
    float2(1.0f, 0.0f),
    float2(1.0f, 1.0f)
};

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