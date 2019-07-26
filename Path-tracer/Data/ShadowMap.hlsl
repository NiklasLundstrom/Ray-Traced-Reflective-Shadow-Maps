cbuffer LightBuffer : register(b0)
{
    float4x4 worldToView;
    float4x4 projection;
};

cbuffer ModelTransform : register(b1)
{
    float4x4 modelToWorld;
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
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float4 worldPosition : TEXCOORD0;
    float3 color : TEXCOORD1;
};

PSInput VSMain(float3 position : POSITION, uint index : SV_VertexID)
{
    PSInput vsOutput;

	// position
    float4 newPosition = float4(position, 1.0);
	newPosition = mul(modelToWorld, newPosition);
    vsOutput.worldPosition = newPosition;
    newPosition = mul(worldToView, newPosition);
    newPosition = mul(projection, newPosition);
    vsOutput.position = newPosition;

	// normal
    float3 normal = normals[index];
    normal = normalize(mul(modelToWorld, float4(normal, 0.0f)).xyz);
    normal = normal * 0.5 + 0.5;
    vsOutput.normal = normal;

	// color
    vsOutput.color = float3(r, g, b) * float3(1.0f, 1.0f, 1.0f);

    return vsOutput;
}

struct PS_OUTPUT
{
    float4 Position : SV_Target0;
    float4 Normal : SV_Target1;
    float4 Flux : SV_Target2;
};

PS_OUTPUT PSMain(PSInput input) : SV_TARGET
{
    PS_OUTPUT output;

    output.Position = input.worldPosition;
    output.Normal = float4(input.normal, 1.0);
    output.Flux = float4(input.color, 1.0);

    return output;

}