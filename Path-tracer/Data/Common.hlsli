///// Common /////////////////
struct RayPayload
{
    float3 color;
    uint seed;
    int depth;
};
	
struct ShadowPayload
{
    bool hit;
};
////////////////////////////////


//// resources ////
