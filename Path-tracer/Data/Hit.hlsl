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

RaytracingAccelerationStructure gRtScene : register(t0);

cbuffer PerFrame : register(b0)
{
    float3 A;
    float3 B;
    float3 C;
}

[shader("closesthit")]
void triangleChs(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    float3 barycentrics = float3(1.0 - attribs.barycentrics.x - attribs.barycentrics.y, attribs.barycentrics.x, attribs.barycentrics.y);
    payload.color = A * barycentrics.x + B * barycentrics.y + C * barycentrics.z;
}

[shader("closesthit")]
void teapotChs(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    payload.color = float3(1.0f, 0.0f, 0.0f);
}



[shader("closesthit")]
void planeChs(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    float hitT = RayTCurrent();
    float3 rayDirW = WorldRayDirection();
    float3 rayOriginW = WorldRayOrigin();

    // Find the world-space hit position
    float3 posW = rayOriginW + hitT * rayDirW;

    // Fire a shadow ray. The direction is hard-coded here, but can be fetched from a constant-buffer
    RayDesc ray;
    ray.Origin = posW;
    ray.Direction = normalize(float3(0.5, 0.5, -0.5));
    ray.TMin = 0.01;
    ray.TMax = 100000;
    ShadowPayload shadowPayload;
    TraceRay(gRtScene, 0  /*rayFlags*/, 0xFF, 1 /* ray index*/, 0, 1, ray, shadowPayload);

    float factor = shadowPayload.hit ? 0.1 : 1.0;
    payload.color = float3(0.9f, 0.9f, 0.9f) * factor;
}