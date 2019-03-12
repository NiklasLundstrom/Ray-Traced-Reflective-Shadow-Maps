#include "Common.hlsli"

[shader("miss")]
void miss(inout RayPayload payload)
{
    float3 dir = WorldRayDirection();
    payload.colorAndDistance.rgb = float3(1.0, dir.x>0?1.0:0.0, 1.0) * (dir.z < -0.5 ? 1.0 : 0.0);
    payload.colorAndDistance.w = -1; // stop bouncing
}
