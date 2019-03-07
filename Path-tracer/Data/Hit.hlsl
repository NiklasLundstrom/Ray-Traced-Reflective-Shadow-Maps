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

StructuredBuffer<uint> indices : register(t1);
StructuredBuffer<float3> normals : register(t2);

[shader("closesthit")]
void triangleChs(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    float3 barycentrics = float3(1.0 - attribs.barycentrics.x - attribs.barycentrics.y, attribs.barycentrics.x, attribs.barycentrics.y);
    payload.color = A * barycentrics.x + B * barycentrics.y + C * barycentrics.z;
}

[shader("closesthit")]
void teapotChs(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    uint vertIndex = 3 * PrimitiveIndex();
    float3 n0 = normals[indices[vertIndex + 0]];
    float3 n1 = normals[indices[vertIndex + 1]];
    float3 n2 = normals[indices[vertIndex + 2]];
    float3 normal = n0 * (1 - attribs.barycentrics.x - attribs.barycentrics.y)
					+ n1 * attribs.barycentrics.x
					+ n2 * attribs.barycentrics.y;
    normal = normalize(mul(ObjectToWorld(), float4(normal, 0.0f)).xyz);

    float3 lightDir = normalize(float3(0.5, 0.5, -0.5));// Hard coded!
    float nDotL = max(0.f, dot(normal, lightDir));

    payload.color = float3(1.0f, 1.0f, 1.0f) * nDotL;
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