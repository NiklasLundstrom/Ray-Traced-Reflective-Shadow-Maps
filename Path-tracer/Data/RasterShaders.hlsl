cbuffer LightBuffer : register(b0)
{
    float4x4 view;
    float4x4 projection;
};

cbuffer ModelTransform : register(b1)
{
    float4x4 model;
};

struct PSInput
{
    float4 position : SV_POSITION;
};

PSInput VSMain(
    float3 position : POSITION)
{
    PSInput vsOutput;

    float4 newPosition = float4(position, 1.0);

	newPosition = mul(newPosition, model);
    newPosition = mul(newPosition, view);
    newPosition = mul(newPosition, projection);

    vsOutput.position = newPosition;
    return vsOutput;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return float4(1.0, 0.0, 0.0, 1.0);

}