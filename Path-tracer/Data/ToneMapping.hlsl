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


Texture2D<float4> gInput : register(t0);
Texture2D<float> gShadowMap_Depth : register(t1);
Texture2D<float4> gShadowMap_Position : register(t2);
Texture2D<float4> gShadowMap_Normal : register(t3);
Texture2D<float4> gShadowMap_Flux : register(t4);
Texture2D<float4> gMotionVectors : register(t5);
Texture2D<float4> gGbufferColor : register(t6);


float4 PSMain(PSInput input) : SV_TARGET
{
    uint masterWidth;
    uint masterHeight;
    gInput.GetDimensions(masterWidth, masterHeight);
    float2 crd = input.uv * float2(masterWidth, masterHeight);
    float3 output = gInput[crd].rgb * gGbufferColor[crd].rgb;

	// Tone Map
    output = linearToSrgb(ACESFitted(2.5 * output.rgb));


	// Render Shadow map to the side
    uint shadowWidth;
    uint shadowHeight;
    gShadowMap_Depth.GetDimensions(shadowWidth, shadowHeight);
    int scale = 4;
    if (crd.x < shadowWidth / scale && crd.y < shadowHeight / scale)
    {
        float zPrim = gShadowMap_Depth[crd * scale];
        float f = 100.0f; // Sync this value to the C++ code!
        float n = 0.1f;
		// Transform to linear view space
        float z = f * n / (f - zPrim * (f - n));
        zPrim = z * zPrim;
        zPrim /= f; // z <- 0..1
        output = zPrim * float3(1.0, 1.0, 1.0);
    }
    else if (crd.x < shadowWidth / scale && crd.y < 2 * shadowHeight / scale)
    {
        uint2 coords = crd;
        coords.y -= shadowHeight / scale;
        float3 cPrim = gShadowMap_Position[coords * scale].rgb;
        output = cPrim;
    }
    else if (crd.x < shadowWidth / scale && crd.y < 3 * shadowHeight / scale)
    {
        uint2 coords = crd;
        coords.y -= 2 * shadowHeight / scale;
        float3 cPrim = gShadowMap_Normal[coords * scale].rgb;
        output = cPrim;
    }
    else if (crd.x < shadowWidth / scale && crd.y < 4 * shadowHeight / scale)
    {
        uint2 coords = crd;
        coords.y -= 3 * shadowHeight / scale;
        float3 cPrim = gShadowMap_Flux[coords * scale].rgb;
        output = cPrim;
    }
    else if (crd.x < shadowWidth / scale && crd.y < 5 * shadowHeight / scale)
    {
        uint2 coords = crd;
        coords.y -= 4 * shadowHeight / scale;
        float4 motionVectors = gMotionVectors[coords * scale * 2];
        motionVectors.y *= -1;
        float3 cPrim = 5.0f * motionVectors.rgb + 0.5f;
        output = cPrim;
    }


    return float4(output, 1);
}