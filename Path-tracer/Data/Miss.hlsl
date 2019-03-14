#include "Common.hlsli"

[shader("miss")]
void miss(inout RayPayload payload)
{
    float3 dir = WorldRayDirection();
    payload.colorAndDistance.rgb = float3(0.0, 0.0, 0.0);
    payload.colorAndDistance.w = -1; // stop bouncing
}
