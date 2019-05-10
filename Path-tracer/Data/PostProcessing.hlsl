RWTexture2D<float4> gRtOutput : register(u0);


[numthreads(1, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    uint width;
    uint height;
    gRtOutput.GetDimensions(width, height);
    float4 tempTexture[512][512];

    int kernelWidth = 3;
    int kernelSide = (kernelWidth - 1) / 2;
    float4 window[9];
    

    for (int y = kernelSide; y < height - kernelSide; y++)
    {
        for (int x = kernelSide; x < width - kernelSide; x++)
        {
            int count = 0;
            float4 sum = float4(0.0, 0.0, 0.0, 0.0);
            for (int i = -kernelSide; i < kernelSide; i++)
            {
                for (int j = -kernelSide; j < kernelSide; j++)
                {
                    window[count] = gRtOutput[uint2(x + i, y + j)];
                    sum += window[count];
                    count++;
                }

            }

			// get median
          


            tempTexture[y][x] = sum / 9.0f;

            }

    }

    for (int y2 = kernelSide; y2 < height - kernelSide; y2++)
    {
        for (int x2 = kernelSide; x2 < width - kernelSide; x2++)
        {
            gRtOutput[uint2(x2, y2)] = tempTexture[y2][x2];

        }
    }
    



    }