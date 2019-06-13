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

    int itr;
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
static const float weights[5] = {0.0625f, 0.25f, 0.375f, 0.25f, 0.0625f};
static const int gMaxBlurRadius = 32;

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
    //float weights[11] =  { w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10 };

    int blurRadius = 1 << itr; // pow(2, itr)
    int blurHalfRadius = blurRadius >> 1;

	//
	// Fill local thread storage to reduce bandwidth.  To blur 
	// N pixels, we will need to load N + 2*BlurRadius pixels
	// due to the blur radius.
	//

	// This thread group runs N threads.  To get the extra 2*BlurRadius pixels, 
	// have 2*BlurRadius threads sample an extra pixel.
    if (groupThreadID.x < blurRadius)
    {
		// Clamp out of bound samples that occur at image borders.
        int x = max(dispatchThreadID.x - blurRadius, 0);
        gCache[groupThreadID.x] = gInput[int2(x, dispatchThreadID.y)];
        gDepthCache[groupThreadID.x] = gDepth[int2(x, dispatchThreadID.y)];
        gNormalCache[groupThreadID.x] = gNormal[int2(x, dispatchThreadID.y)];
    }
    if (groupThreadID.x >= N - blurRadius)
    {
		// Clamp out of bound samples that occur at image borders.
        int x = min(dispatchThreadID.x + blurRadius, width - 1);
        gCache[groupThreadID.x + 2 * blurRadius] = gInput[int2(x, dispatchThreadID.y)];
        gDepthCache[groupThreadID.x + 2 * blurRadius] = gDepth[int2(x, dispatchThreadID.y)];
        gNormalCache[groupThreadID.x + 2 * blurRadius] = gNormal[int2(x, dispatchThreadID.y)];
    }

	// Clamp out of bound samples that occur at image borders.
    gCache[groupThreadID.x + blurRadius] = gInput[min(dispatchThreadID.xy, int2(width, height) - 1)];
    gDepthCache[groupThreadID.x + blurRadius] = gDepth[min(dispatchThreadID.xy, int2(width, height) - 1)];
    gNormalCache[groupThreadID.x + blurRadius] = gNormal[min(dispatchThreadID.xy, int2(width, height) - 1)];

	// Wait for all threads to finish.
    GroupMemoryBarrierWithGroupSync();

	//
	// Now blur each pixel.
	//
    float4 blurColor = float4(0, 0, 0, 0);
    float weightSum = 0.0f;

    float depthCenter = makeDepthLinear(gDepthCache[groupThreadID.x + blurRadius]);
    float3 normalCenter = gNormalCache[groupThreadID.x + blurRadius].xyz * 2 - 1;
    float3 colorCenter = gCache[groupThreadID.x + blurRadius].rgb;
	

    for (int i = -2; i <= 2; i++)
    {
        int k = groupThreadID.x + blurRadius + i * blurHalfRadius;
		//// test depth
  //      if (abs(1.0f - depthCenter / makeDepthLinear(gDepthCache[k])) >= 0.1f)
  //      {
  //          continue;
  //      }
  //      if (dot(normalCenter, (gNormalCache[k].xyz * 2 - 1)) < 0.9f)
  //      {
  //          continue;
  //      }
        //blurColor += weights[i + 2] * gCache[k];
        //weightSum += weights[i + 2];
        float w_n = pow(max(0.0f, dot(normalCenter, (gNormalCache[k].xyz * 2 - 1))), 10.0f); //^sigma
        float w_z = exp(-abs(depthCenter - makeDepthLinear(gDepthCache[k])));// /(sigma * gradient)
        float w_l =/* ((itr == 1) ? 1.0f :*/ exp(-length(colorCenter - gCache[k].rgb) / 0.1f); // /(sigma*var)
        float w = w_n;// * /*w_z **/w_l;

        blurColor += weights[i + 2] * w * gCache[k];
        weightSum += weights[i + 2] * w;

    }

    blurColor /= weightSum;

    gOutput[dispatchThreadID.xy] = /*gCache[groupThreadID.x + blurRadius];*/    blurColor;
}


[numthreads(1, N, 1)]
void VertBlurCS(int3 groupThreadID : SV_GroupThreadID, int3 dispatchThreadID : SV_DispatchThreadID)
{
    uint width;
    uint height;
    gInput.GetDimensions(width, height);

	// Put in an array for each indexing.
    //float weights[11] = {w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10};

    int blurRadius = 1 << itr; // pow(2, itr)
    int blurHalfRadius = blurRadius >> 1;

	//
	// Fill local thread storage to reduce bandwidth.  To blur 
	// N pixels, we will need to load N + 2*BlurRadius pixels
	// due to the blur radius.
	//

	// This thread group runs N threads.  To get the extra 2*BlurRadius pixels, 
	// have 2*BlurRadius threads sample an extra pixel.
    if (groupThreadID.y < blurRadius)
    {
		// Clamp out of bound samples that occur at image borders.
        int y = max(dispatchThreadID.y - blurRadius, 0);
        gCache[groupThreadID.y] = gInput[int2(dispatchThreadID.x, y)];
        gDepthCache[groupThreadID.y] = gDepth[int2(dispatchThreadID.x, y)];
        gNormalCache[groupThreadID.y] = gNormal[int2(dispatchThreadID.x, y)];
    }
    if (groupThreadID.y >= N - blurRadius)
    {
		// Clamp out of bound samples that occur at image borders.
        int y = min(dispatchThreadID.y + blurRadius, height - 1);
        gCache[groupThreadID.y + 2 * blurRadius] = gInput[int2(dispatchThreadID.x, y)];
        gDepthCache[groupThreadID.y + 2 * blurRadius] = gDepth[int2(dispatchThreadID.x, y)];
        gNormalCache[groupThreadID.y + 2 * blurRadius] = gNormal[int2(dispatchThreadID.x, y)];
    }

	// Clamp out of bound samples that occur at image borders.
    gCache[groupThreadID.y + blurRadius] = gInput[min(dispatchThreadID.xy, int2(width, height) - 1)];
    gDepthCache[groupThreadID.y + blurRadius] = gDepth[min(dispatchThreadID.xy, int2(width, height) - 1)];
    gNormalCache[groupThreadID.y + blurRadius] = gNormal[min(dispatchThreadID.xy, int2(width, height) - 1)];

	// Wait for all threads to finish.
    GroupMemoryBarrierWithGroupSync();

	//
	// Now blur each pixel.
	//
    float4 blurColor = float4(0, 0, 0, 0);
    float weightSum = 0.0f;

    float depthCenter = makeDepthLinear(gDepthCache[groupThreadID.y + blurRadius]);
    float3 normalCenter = gNormalCache[groupThreadID.y + blurRadius].xyz * 2 - 1;
    float3 colorCenter = gCache[groupThreadID.y + blurRadius].rgb;

    for (int i = -2; i <= 2; i++)
    {
        int k = groupThreadID.y + blurRadius + i * blurHalfRadius;
		//// test depth
  //      if (abs(1.0f - depthCenter / makeDepthLinear(gDepthCache[k])) >= 0.1f)
  //      {
  //          continue;
  //      }
  //      if (dot(normalCenter, (gNormalCache[k].xyz * 2 - 1)) < 0.9f)
  //      {
  //          continue;
  //      }
  //      blurColor += weights[i + 2] * gCache[k];
  //      weightSum += weights[i + 2];
        float w_n = pow(max(0.0f, dot(normalCenter, (gNormalCache[k].xyz * 2 - 1))), 100.0f); //^sigma
        float w_z = exp(-abs(depthCenter - makeDepthLinear(gDepthCache[k]))); // /(sigma * gradient)
        float w_l =/* ((itr == 1) ? 1.0f : */exp(-length(colorCenter - gCache[k].rgb) / 0.1f); // /(sigma*var)
        float w = w_n;// * /*w_z **///w_l;

        blurColor += weights[i + 2] * w * gCache[k];
        weightSum += weights[i + 2] * w;
    }

    blurColor /= weightSum;

    gOutput[dispatchThreadID.xy] = /*gCache[groupThreadID.y + blurRadius];*/    blurColor;
}