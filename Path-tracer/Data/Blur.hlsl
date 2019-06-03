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

// taken from https://github.com/d3dcoder/d3d12book/blob/master/Chapter%2013%20The%20Compute%20Shader/Blur
//=============================================================================
// Performs a separable Guassian blur with a blur radius up to 5 pixels.
//=============================================================================
cbuffer cbSettings : register(b0)
{
	// We cannot have an array entry in a constant buffer that gets mapped onto
	// root constants, so list each element.  	

    int gBlurRadius;
	// Support up to 11 blur weights.
    float w0;
    float w1;
    float w2;
    float w3;
    float w4;
    float w5;
    float w6;
    float w7;
    float w8;
    float w9;
    float w10;
};
    //static const int gBlurRadius = 5;

static const int gMaxBlurRadius = 5;

Texture2D gInput : register(t0);
Texture2D gDepth : register(t1);
Texture2D gNormal : register(t2);
RWTexture2D<float4> gOutput : register(u0);

#define N 256
#define CacheSize (N + 2*gMaxBlurRadius)

groupshared float4 gCache[CacheSize];
groupshared float gDepthCache[CacheSize];
groupshared float4 gNormalCache[CacheSize];

[numthreads(N, 1, 1)]
void HorzBlurCS(int3 groupThreadID : SV_GroupThreadID, int3 dispatchThreadID : SV_DispatchThreadID)
{
    uint width;
    uint height;
    gInput.GetDimensions(width, height);

	// Put in an array for each indexing.
    float weights[11] =  { w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10 };

	//
	// Fill local thread storage to reduce bandwidth.  To blur 
	// N pixels, we will need to load N + 2*BlurRadius pixels
	// due to the blur radius.
	//

	// This thread group runs N threads.  To get the extra 2*BlurRadius pixels, 
	// have 2*BlurRadius threads sample an extra pixel.
    if (groupThreadID.x < gBlurRadius)
    {
		// Clamp out of bound samples that occur at image borders.
        int x = max(dispatchThreadID.x - gBlurRadius, 0);
        gCache[groupThreadID.x] = gInput[int2(x, dispatchThreadID.y)];
        gDepthCache[groupThreadID.x] = gDepth[int2(x, dispatchThreadID.y)];
        gNormalCache[groupThreadID.x] = gNormal[int2(x, dispatchThreadID.y)];
    }
    if (groupThreadID.x >= N - gBlurRadius)
    {
		// Clamp out of bound samples that occur at image borders.
        int x = min(dispatchThreadID.x + gBlurRadius, width - 1);
        gCache[groupThreadID.x + 2 * gBlurRadius] = gInput[int2(x, dispatchThreadID.y)];
        gDepthCache[groupThreadID.x + 2 * gBlurRadius] = gDepth[int2(x, dispatchThreadID.y)];
        gNormalCache[groupThreadID.x + 2 * gBlurRadius] = gNormal[int2(x, dispatchThreadID.y)];
    }

	// Clamp out of bound samples that occur at image borders.
    gCache[groupThreadID.x + gBlurRadius] = gInput[min(dispatchThreadID.xy, int2(width, height) - 1)];
    gDepthCache[groupThreadID.x + gBlurRadius] = gDepth[min(dispatchThreadID.xy, int2(width, height) - 1)];
    gNormalCache[groupThreadID.x + gBlurRadius] = gNormal[min(dispatchThreadID.xy, int2(width, height) - 1)];

	// Wait for all threads to finish.
    GroupMemoryBarrierWithGroupSync();

	//
	// Now blur each pixel.
	//
    float4 blurColor = float4(0, 0, 0, 0);
    float weightSum = 0.0f;

    blurColor += weights[gBlurRadius] * gCache[groupThreadID.x + gBlurRadius];
    weightSum += weights[gBlurRadius];
    float depthCenter = makeDepthLinear(gDepthCache[groupThreadID.x + gBlurRadius]);
    float3 normalCenter = gNormalCache[groupThreadID.x + gBlurRadius].xyz * 2 - 1;
	

    for (int i = -1; i >= -gBlurRadius; i--)
    {
        int k = groupThreadID.x + gBlurRadius + i;
		// test depth
        if (abs(1.0f - depthCenter / makeDepthLinear(gDepthCache[k])) >= 0.1f)
        {
            break;
        }
        if (dot(normalCenter, (gNormalCache[k].xyz * 2 - 1)) < 0.9f)
        {
            break;
        }
        blurColor += weights[i + gBlurRadius] * gCache[k];
        weightSum += weights[i + gBlurRadius];
        }
    for (int j = 1; j <= gBlurRadius; j++)
    {
        int k = groupThreadID.x + gBlurRadius + j;
		// test depth
        if (abs(1.0f - depthCenter / makeDepthLinear(gDepthCache[k])) >= 0.1f)
        {
            break;
        }
        if (dot(normalCenter, (gNormalCache[k].xyz * 2 - 1)) < 0.9f)
        {
            break;
        }
        blurColor += weights[j + gBlurRadius] * gCache[k];
        weightSum += weights[j + gBlurRadius];
    }

    blurColor /= weightSum;

    gOutput[dispatchThreadID.xy] = /*gCache[groupThreadID.x + gBlurRadius];*/ blurColor;
}


[numthreads(1, N, 1)]
void VertBlurCS(int3 groupThreadID : SV_GroupThreadID, int3 dispatchThreadID : SV_DispatchThreadID)
{
    uint width;
    uint height;
    gInput.GetDimensions(width, height);

	// Put in an array for each indexing.
    float weights[11] = {w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10};

	//
	// Fill local thread storage to reduce bandwidth.  To blur 
	// N pixels, we will need to load N + 2*BlurRadius pixels
	// due to the blur radius.
	//

	// This thread group runs N threads.  To get the extra 2*BlurRadius pixels, 
	// have 2*BlurRadius threads sample an extra pixel.
    if (groupThreadID.y < gBlurRadius)
    {
		// Clamp out of bound samples that occur at image borders.
        int y = max(dispatchThreadID.y - gBlurRadius, 0);
        gCache[groupThreadID.y] = gInput[int2(dispatchThreadID.x, y)];
        gDepthCache[groupThreadID.y] = gDepth[int2(dispatchThreadID.x, y)];
        gNormalCache[groupThreadID.y] = gNormal[int2(dispatchThreadID.x, y)];
    }
    if (groupThreadID.y >= N - gBlurRadius)
    {
		// Clamp out of bound samples that occur at image borders.
        int y = min(dispatchThreadID.y + gBlurRadius, height - 1);
        gCache[groupThreadID.y + 2 * gBlurRadius] = gInput[int2(dispatchThreadID.x, y)];
        gDepthCache[groupThreadID.y + 2 * gBlurRadius] = gDepth[int2(dispatchThreadID.x, y)];
        gNormalCache[groupThreadID.y + 2 * gBlurRadius] = gNormal[int2(dispatchThreadID.x, y)];
    }

	// Clamp out of bound samples that occur at image borders.
    gCache[groupThreadID.y + gBlurRadius] = gInput[min(dispatchThreadID.xy, int2(width, height) - 1)];
    gDepthCache[groupThreadID.y + gBlurRadius] = gDepth[min(dispatchThreadID.xy, int2(width, height) - 1)];
    gNormalCache[groupThreadID.y + gBlurRadius] = gNormal[min(dispatchThreadID.xy, int2(width, height) - 1)];

	// Wait for all threads to finish.
    GroupMemoryBarrierWithGroupSync();

	//
	// Now blur each pixel.
	//
    float4 blurColor = float4(0, 0, 0, 0);
    float weightSum = 0.0f;

    blurColor += weights[gBlurRadius] * gCache[groupThreadID.y + gBlurRadius];
    weightSum += weights[gBlurRadius];
    float depthCenter = makeDepthLinear(gDepthCache[groupThreadID.y + gBlurRadius]);
    float3 normalCenter = gNormalCache[groupThreadID.y + gBlurRadius].xyz * 2 - 1;

    for (int i = -1; i >= -gBlurRadius; i--)
    {
        int k = groupThreadID.y + gBlurRadius + i;	
		// test depth
        if (abs(1.0f - depthCenter / makeDepthLinear(gDepthCache[k])) >= 0.1f)
        {
            break;
        }
        if (dot(normalCenter, (gNormalCache[k].xyz * 2 - 1)) < 0.9f)
        {
            break;
        }
		blurColor += weights[i + gBlurRadius] * gCache[k];
        weightSum += weights[i + gBlurRadius];
    }
    for (int j = 1; j <= gBlurRadius; j++)
    {
        int k = groupThreadID.y + gBlurRadius + j;
		// test depth
        if (abs(1.0f - depthCenter / makeDepthLinear(gDepthCache[k])) >= 0.1f)
        {
            break;
        }
        if (dot(normalCenter, (gNormalCache[k].xyz * 2 - 1)) < 0.9f)
        {
            break;
        }
        blurColor += weights[j + gBlurRadius] * gCache[k];
        weightSum += weights[j + gBlurRadius];
    }

    blurColor /= weightSum;

    gOutput[dispatchThreadID.xy] = /*gCache[groupThreadID.y + gBlurRadius];*/ blurColor;
}