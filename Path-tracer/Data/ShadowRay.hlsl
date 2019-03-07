///// Common /////////////////
	struct RayPayload
	{
		float3 color;
	};
	
	struct ShadowPayload
	{
		bool hit;
	};
////////////////////////////////

[shader("closesthit")]
void shadowChs(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    payload.hit = true;
}

[shader("miss")]
void shadowMiss(inout ShadowPayload payload)
{
    payload.hit = false;
}
