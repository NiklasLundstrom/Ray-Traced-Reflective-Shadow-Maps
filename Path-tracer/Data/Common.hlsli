///// Common /////////////////
struct RayPayload
{
	// rgb is color
	// w is distance. -1 means stop bouncing, i.e. miss or light source.
    float4 colorAndDistance;
    float3 normal;
};
	
	struct ShadowPayload
{
    bool hit;
};
////////////////////////////////


//// resources ////
float3 lightDir = normalize(float3(0.5, 0.5, -0.5));