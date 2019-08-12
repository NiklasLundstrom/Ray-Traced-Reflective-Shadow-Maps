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
Texture2D<float4> gRtDirectCurrent : register(t1);
Texture2D<float4> gColorHistory : register(t2);
Texture2D<float4> gDirectColorHistory : register(t3);
Texture2D<float4> gMotionVectors : register(t4);

cbuffer cbSettings : register(b0)
{
    bool dropHistory;
    bool OFFLINE;
};

SamplerState gSampler : register(s0);

struct PS_OUT
{
    float4 output : SV_TARGET0;
};

PS_OUT PSMain(PSInput input) : SV_TARGET
{
    float2 crd = input.uv;

// get motion vector (x,y) and accepted reprojection (z)
    float3 motionVector = gMotionVectors.SampleLevel(gSampler, crd, 0).xyz;
    motionVector.y *= -1.0f;
    float2 reprojectedCrd = crd - motionVector.xy;

// accepted reprojection
    float acceptReprojection = motionVector.z;

    float4 colorOutput;
    //float directOutput;
    if (OFFLINE || (acceptReprojection && !dropHistory))
    {
        if (OFFLINE)
        {
            float4 colorCurrent = gRtDirectCurrent.SampleLevel(gSampler, crd, 0);
            float4 colorHistory = gColorHistory.SampleLevel(gSampler, reprojectedCrd, 0);

            float historyLength = colorHistory.a + 1.0f;
            float mixValue = 1.0f / historyLength;
            colorOutput.rgb = mixValue * colorCurrent.rgb + (1 - mixValue) * colorHistory.rgb;
            colorOutput.a = historyLength;
        }
        else
        {
			float4 colorCurrent = gRtCurrent.SampleLevel(gSampler, crd, 0);
            float4 colorHistory = gColorHistory.SampleLevel(gSampler, reprojectedCrd, 0);

		// indirect
            float3 indirectColorCurrent = colorCurrent.rgb;
            float3 indirectColorHistory = colorHistory.rgb;

            float mixValue = 0.04f; 
            colorOutput.rgb = mixValue * indirectColorCurrent + (1 - mixValue) * indirectColorHistory;

		// direct
            float directColorCurrent = colorCurrent.a;
            float directColorHistory = colorHistory.a;

            mixValue = 0.3f;
            colorOutput.a = mixValue * directColorCurrent + (1 - mixValue) * directColorHistory;
        }
    }
    else // discard old samples
    {
        if (OFFLINE)
        {
            colorOutput.rgb = gRtDirectCurrent.SampleLevel(gSampler, crd, 0);
            colorOutput.a = 1.0f;
        }
        else
        {
            colorOutput = gRtCurrent.SampleLevel(gSampler, crd, 0);
        }
    }

    PS_OUT output;
    output.output = colorOutput;
    return output;

}