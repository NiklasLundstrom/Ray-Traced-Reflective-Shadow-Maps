///// Common /////////////////
struct RayPayload
{
    float3 color;
    uint seed;
    //int depth;
};
	
////////////////////////////////
struct ShadowPayload
{
    bool hit;
};

//// resources ////
    static const float PI = 3.14159265f;