///// Common /////////////////
struct RayPayload
{
	// rgb is color
	// w is distance. -1 means stop bouncing, i.e. miss or light source.
    float4 colorAndDistance;
};
	
struct ShadowPayload
{
    bool hit;
};
////////////////////////////////


[shader("miss")]
void miss(inout RayPayload payload)
{
    payload.colorAndDistance.rgb = float3(0.4, 0.6, 0.2);
    payload.colorAndDistance.w = -1; // stop bouncing
}
