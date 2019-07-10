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


Texture2D<float4> gRtIndirectCurrent : register(t0);
Texture2D<float4> gRtDirectCurrent : register(t1);
Texture2D<float4> gIndirectColorHistory : register(t2);
Texture2D<float4> gDirectColorHistory : register(t3);
Texture2D<float4> gMotionVectors : register(t4);

SamplerState gSampler : register(s0);

struct PS_OUT
{
    float4 indirectOutput : SV_TARGET0;
    float4 directOutput : SV_TARGET1;
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

    float3 indirectOutput;
    float3 directOutput;
    float historyLength;
    if (acceptReprojection)
    {
		// indirect
        float4 indirectColorCurrent = gRtIndirectCurrent.SampleLevel(gSampler, crd, 0);
        float4 indirectColorHistory = gIndirectColorHistory.SampleLevel(gSampler, reprojectedCrd, 0);
        historyLength = indirectColorHistory.a + 1.0f;

        float mixValue = 0.1f; //0.05f; /*max(0.2f, 1.0f / historyLength);*/
        indirectOutput = mixValue * indirectColorCurrent.rgb + (1 - mixValue) * indirectColorHistory.rgb;

		// direct
        float4 directColorCurrent = gRtDirectCurrent.SampleLevel(gSampler, crd, 0);
        float4 directColorHistory = gDirectColorHistory.SampleLevel(gSampler, reprojectedCrd, 0);

        mixValue = 0.3f;//0.3
        directOutput = mixValue * directColorCurrent.rgb + (1 - mixValue) * directColorHistory.rgb;

    }
    else // discard old samples
    {
		// indirect
        indirectOutput = gRtIndirectCurrent.SampleLevel(gSampler, crd, 0);
        historyLength = 1.0f;

		// direct
        directOutput = gRtDirectCurrent.SampleLevel(gSampler, crd, 0);
    }

    PS_OUT output;
    output.indirectOutput = float4(indirectOutput, historyLength);
    output.directOutput = float4(directOutput, 1.0f);
    return output;

}