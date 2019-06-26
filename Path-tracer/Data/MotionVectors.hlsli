#include "Common.hlsli"


bool acceptReprojection(float2 motionVectors, SamplerState gSampler, float2 crd, Texture2D<float> gDepthCurrent, Texture2D<float> gDepthPrevious)
{
    bool acceptReprojection = true;
    
	// get current depth
    float depthCurr = /*depthCurrent;*/ gDepthCurrent.SampleLevel(gSampler, crd, 0);
    depthCurr = makeDepthLinear(depthCurr);

    //if we see the sky
    if (depthCurr > 0.9999f)
    {
        acceptReprojection = false;
    }
    else
    {
		// get motion vector
        float2 motionVector = motionVectors;
        motionVector.y *= -1.0f;
		// reproject
        float2 reprojectedCrd = crd - motionVector;

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
            float depthDifference = abs(1.0f - depthCurr / depthPreviousReprojected);
            acceptReprojection = depthDifference < 0.1f;
        }
    }
    return acceptReprojection;

}