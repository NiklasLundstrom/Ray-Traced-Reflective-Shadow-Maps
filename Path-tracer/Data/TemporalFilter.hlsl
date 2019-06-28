// inspired by https://github.com/d3dcoder/d3d12book/blob/master/Chapter%2021%20Ambient%20Occlusion/Ssao/Shaders/SsaoBlur.hlsl

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


Texture2D<float4> gRtCurrent : register(t0);
Texture2D<float4> gRtPrevious : register(t1);
//Texture2D<float4> gRtPreviousPrevious : register(t2);
Texture2D<float4> gMotionVectors : register(t2);
//Texture2D<float> gDepthCurrent : register(t3);
//Texture2D<float> gDepthPrevious : register(t4);
SamplerState gSampler : register(s0);

float4 PSMain(PSInput input) : SV_TARGET
{
    float2 crd = input.uv;

	// get motion vector (x,y) and accepted reprojection (z)
    float3 motionVector = gMotionVectors.SampleLevel(gSampler, crd, 0).xyz;
    motionVector.y *= -1.0f;
    float2 reprojectedCrd = crd - motionVector.xy;

	// accepted reprojection
    float acceptReprojection = motionVector.z;

    float3 output;
    float historyLength;
    if (acceptReprojection)
    {
        float4 colorCurrent = gRtCurrent.SampleLevel(gSampler, crd, 0);
        float4 colorHistory = gRtPrevious.SampleLevel(gSampler, reprojectedCrd, 0);
        historyLength = colorHistory.a + 1.0f;

        float mixValue = 0.5f; //0.2f; /*max(0.2f, 1.0f / historyLength);*/
        output = mixValue * colorCurrent.rgb
			+ (1 - mixValue) * colorHistory.rgb;

    }
    else // discard old samples
    {
        output = gRtCurrent.SampleLevel(gSampler, crd, 0);
        historyLength = 1.0f;
    }

    return float4(output.rgb, historyLength);
}