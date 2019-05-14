Texture2D<float4> gRtOutput : register(t0);
RWTexture2D<float4> gPpOutput : register(u0);


[numthreads(1, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    uint width;
    uint height;
    gRtOutput.GetDimensions(width, height);

    uint2 centerCrd = (DTid.xy );

    int kernelWidth = 1;
    int kernelSide = (kernelWidth - 1) / 2; 

 
            float4 sum = float4(0.0, 0.0, 0.0, 0.0);
            for (int i = -kernelSide; i <= kernelSide; i++)
            {
                for (int j = -kernelSide; j <= kernelSide; j++)
                {
                    sum += gRtOutput[centerCrd + uint2(i, j)];

                }

            }

			// get median
          


    gPpOutput[centerCrd] = /* gRtOutput[centerCrd];*/sum / ( kernelWidth * kernelWidth);




    }