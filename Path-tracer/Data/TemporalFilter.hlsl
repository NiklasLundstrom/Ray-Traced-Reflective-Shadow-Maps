// inspired by https://github.com/d3dcoder/d3d12book/blob/master/Chapter%2021%20Ambient%20Occlusion/Ssao/Shaders/SsaoBlur.hlsl


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


Texture2D<float4> gRtCurrent : register(t0);
Texture2D<float4> gRtPrevious : register(t1);
Texture2D<float4> gRtPreviousPrevious : register(t2);


float4 PSMain(PSInput input) : SV_TARGET
{
    uint masterWidth;
    uint masterHeight;
    gRtCurrent.GetDimensions(masterWidth, masterHeight);
    float2 crd = input.uv * float2(masterWidth, masterHeight);
    float3 output = (gRtCurrent[crd].rgb + gRtPrevious[crd].rgb + gRtPreviousPrevious[crd].rgb) / 3.0f;

	
    return float4(output, 1);
}