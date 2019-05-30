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

StructuredBuffer<float3> normals : register(t0);

struct PSInput
{
    float4 positionCurr : SV_POSITION;
    float4 worldPosition : TEXCOORD0;
    float4 positionPrev : TEXCOORD1;
    float3 position_xy_Curr : TEXCOORD2;
    float3 position_xy_Prev : TEXCOORD3;
    float3 normal : NORMAL;
};

PSInput VSMain(
    float3 position : POSITION, uint index : SV_VertexID)
{
    PSInput vsOutput;
	// current
		// world position
			float4 positionWorldCurr = float4(position, 1.0);
			positionWorldCurr = mul(modelToWorldCurr, positionWorldCurr);
			vsOutput.worldPosition = positionWorldCurr;
		// position screen space
			float4 positionScreenCurr = mul(worldToViewCurr, positionWorldCurr);
			positionScreenCurr = mul(projection, positionScreenCurr);
			vsOutput.positionCurr = positionScreenCurr;
			vsOutput.position_xy_Curr = positionScreenCurr.xyw;
		// normal
			float3 normal = normals[index];
			normal = normalize(mul(modelToWorldCurr, float4(normal, 0.0f)).xyz);
			normal = normal * 0.5 + 0.5;
			vsOutput.normal = normal;

	// prev position screen space
		float4 positionWorldPrev = float4(position, 1.0);
		positionWorldPrev = mul(modelToWorldPrev, positionWorldPrev);
		float4 positionScreenPrev = mul(worldToViewPrev, positionWorldPrev);
		positionScreenPrev = mul(projection, positionScreenPrev);
		vsOutput.positionPrev = positionScreenPrev;
		vsOutput.position_xy_Prev = positionScreenPrev.xyw;

    return vsOutput;
}

struct PS_OUTPUT
{
    float4 MotionVectors : SV_Target0;
    float4 Normal : SV_Target1;
};

PS_OUTPUT PSMain(PSInput input) : SV_TARGET
{
    PS_OUTPUT output;

    float2 ndcCurr = input.position_xy_Curr.xy / input.position_xy_Curr.z;
    float2 ndcPrev = input.position_xy_Prev.xy / input.position_xy_Prev.z;

    output.MotionVectors = float4(0.5f * (ndcCurr - ndcPrev), 0.0, 1.0);
    output.Normal = float4(input.normal, 1.0);

    return output;

}