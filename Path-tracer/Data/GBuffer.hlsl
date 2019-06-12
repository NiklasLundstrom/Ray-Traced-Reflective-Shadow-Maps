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

cbuffer color : register(b2)
{
    float r;
    float g;
    float b;
};

StructuredBuffer<float3> normals : register(t0);

struct PSInput
{
    float4 positionCurr : SV_POSITION;
    float3 normal : NORMAL;
    float3 color : TEXCOORD0;
    float4 worldPosition : TEXCOORD1;
};

PSInput VSMain(float3 position : POSITION, uint index : SV_VertexID)
{
    PSInput vsOutput;
	// world position
		float4 positionWorldCurr = float4(position, 1.0);
		positionWorldCurr = mul(modelToWorldCurr, positionWorldCurr);
		vsOutput.worldPosition = positionWorldCurr;
	// position screen space
		float4 positionScreenCurr = mul(worldToViewCurr, positionWorldCurr);
		positionScreenCurr = mul(projection, positionScreenCurr);
		vsOutput.positionCurr = positionScreenCurr;
	// normal
		float3 normal = normals[index];
		normal = normalize(mul(modelToWorldCurr, float4(normal, 0.0f)).xyz);
		normal = normal * 0.5 + 0.5;
		vsOutput.normal = normal;
	// color
		vsOutput.color = float3(r, g, b);

    return vsOutput;
}

struct PS_OUTPUT
{
    float4 Normal : SV_Target0;
    float4 Color : SV_Target1;
    float4 Position : SV_Target2;
};

PS_OUTPUT PSMain(PSInput input) : SV_TARGET
{
    PS_OUTPUT output;
    output.Normal = float4(input.normal, 1.0);
    output.Color = float4(input.color, 1.0);
    output.Position = input.worldPosition;

    return output;
}