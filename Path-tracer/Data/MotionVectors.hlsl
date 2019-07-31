#include "MotionVectors.hlsli"

cbuffer CameraMatrixBuffer : register(b0)
{
    float4x4 worldToViewCurr;
    float4x4 projection;
    float4x4 worldToViewPrev;
};

cbuffer ModelTransform : register(b1)
{
    float4x4 modelToWorldCurr;
    float4x4 modelToWorldPrev;
};

Texture2D<float> gDepthCurrent : register(t0);
Texture2D<float> gDepthPrevious : register(t1);
Texture2D<float4> gNormalCurrent : register(t2);
Texture2D<float4> gNormalPrevious : register(t3);
SamplerState gSampler : register(s0);


struct PSInput
{
    float4 positionCurr : SV_POSITION;
    //float4 positionPrev : TEXCOORD1;
    float3 position_xy_Curr : TEXCOORD2;
    float3 position_xy_Prev : TEXCOORD3;
};

PSInput VSMain(float3 position : POSITION, uint index : SV_VertexID)
{
    PSInput vsOutput;

	// current position screen space
		float4 positionWorldCurr = float4(position, 1.0);
		positionWorldCurr = mul(modelToWorldCurr, positionWorldCurr);
		float4 positionScreenCurr = mul(worldToViewCurr, positionWorldCurr);
		positionScreenCurr = mul(projection, positionScreenCurr);
		vsOutput.position_xy_Curr = positionScreenCurr.xyw;
	
	// position to pass on
		vsOutput.positionCurr = positionScreenCurr;

	// prev position screen space
		float4 positionWorldPrev = float4(position, 1.0);
		positionWorldPrev = mul(modelToWorldPrev, positionWorldPrev);
		float4 positionScreenPrev = mul(worldToViewPrev, positionWorldPrev);
		positionScreenPrev = mul(projection, positionScreenPrev);
		vsOutput.position_xy_Prev = positionScreenPrev.xyw;


    return vsOutput;
}

struct PS_OUTPUT
{
    float4 MotionVectors : SV_Target0;
};

PS_OUTPUT PSMain(PSInput input) : SV_TARGET
{
    PS_OUTPUT output;

    float2 ndcCurr = input.position_xy_Curr.xy / input.position_xy_Curr.z;
    float2 ndcPrev = input.position_xy_Prev.xy / input.position_xy_Prev.z;

    output.MotionVectors.xy = float2(0.5f * (ndcCurr - ndcPrev));
   
    float2 crd = ndcCurr*0.5f + 0.5f;
    crd.y = 1.0f - crd.y;
    float accepted = (float) acceptReprojection(output.MotionVectors.xy, gSampler, crd, gDepthCurrent, gDepthPrevious, gNormalCurrent, gNormalPrevious);
    
	
	output.MotionVectors.zw = float2(accepted, 1.0f);
    return output;

}

