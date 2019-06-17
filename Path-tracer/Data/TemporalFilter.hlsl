// inspired by https://github.com/d3dcoder/d3d12book/blob/master/Chapter%2021%20Ambient%20Occlusion/Ssao/Shaders/SsaoBlur.hlsl


float makeDepthLinear(float oldDepth)
{
    float zPrim = oldDepth;
    float f = 100.0f; // Sync this value to the C++ code!
    float n = 0.1f;
		// Transform to linear view space
    float z = f * n / (f - zPrim * (f - n));
    zPrim = z * zPrim;
    zPrim /= f; // z <- 0..1
    return zPrim;
};

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
//Texture2D<float4> gRtPreviousPrevious : register(t2);
Texture2D<float4> gMotionVectors : register(t2);
Texture2D<float> gDepthCurrent : register(t3);
Texture2D<float> gDepthPrevious : register(t4);
SamplerState gSampler : register(s0);

float4 PSMain(PSInput input) : SV_TARGET
{
	// for test
    float depthDifference = 0.0f;
	//


    uint masterWidth;
    uint masterHeight;
    gRtCurrent.GetDimensions(masterWidth, masterHeight);
    float2 crd = input.uv;
    float2 reprojectedCrd;

    bool acceptReprojection = true;
    
	// get current depth
    float depthCurr = gDepthCurrent.SampleLevel(gSampler, crd, 0);
    depthCurr = makeDepthLinear(depthCurr);

    //if we see the sky
    if (depthCurr > 0.9999f)
    {
        acceptReprojection = false;
    }
	else
    {
		// get motion vector
        float2 motionVector = gMotionVectors.SampleLevel(gSampler, crd, 0).rgb.xy;
        motionVector.y *= -1.0f;
		// reproject
        reprojectedCrd = crd - motionVector;

		// if outside of previous frame
        if (reprojectedCrd.x < 0 || reprojectedCrd.x > 1 || reprojectedCrd.y < 0 || reprojectedCrd.y > 1)
        {
            acceptReprojection = false;
        }
        else
        {
			// get previous depth
            float depthPreviousReprojected = gDepthPrevious.SampleLevel(gSampler, reprojectedCrd, 0);
            depthPreviousReprojected = makeDepthLinear(depthPreviousReprojected);
			// compare depth
            /*float*/ depthDifference = abs(1.0f - depthCurr / depthPreviousReprojected);
            acceptReprojection = depthDifference < 0.1f;
        }
    }

    float3 output;
    if (acceptReprojection)
    {
        float mixValue = 0.1;
        output = mixValue * gRtCurrent.SampleLevel(gSampler, crd, 0).rgb
			+ (1 - mixValue) * gRtPrevious.SampleLevel(gSampler, reprojectedCrd, 0).rgb;
    }
    else // discard old samples
    {
        output = gRtCurrent.SampleLevel(gSampler, crd, 0);
    }

    return float4(output, 1.0f);
}