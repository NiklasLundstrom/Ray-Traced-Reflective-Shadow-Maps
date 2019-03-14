#include "Common.hlsli"

[shader("miss")]
void miss(inout RayPayload payload)
{
    payload.color = float3(0.0, 0.0, 0.0);
}
