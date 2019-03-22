#include "Common.hlsli"
#include "hlslUtils.hlsli"

RaytracingAccelerationStructure gRtScene : register(t0);

StructuredBuffer<uint> indices : register(t1);
StructuredBuffer<float3> normals : register(t2);


[shader("closesthit")]
void teapotChs(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    if (payload.depth <= 0)
    {
        payload.color = float3(0.0, 0.0, 0.0);
    }
    else
    {
	// get hit point
        float hitT = RayTCurrent();
        float3 rayDirW = WorldRayDirection();
        float3 rayOriginW = WorldRayOrigin();
        float3 hitPoint = rayOriginW + rayDirW * hitT;

	// get normal
        uint vertIndex = 3 * PrimitiveIndex();
        float3 n0 = normals[indices[vertIndex + 0]];
        float3 n1 = normals[indices[vertIndex + 1]];
        float3 n2 = normals[indices[vertIndex + 2]];
        float3 normal = n0 * (1 - attribs.barycentrics.x - attribs.barycentrics.y)
					+ n1 * attribs.barycentrics.x
					+ n2 * attribs.barycentrics.y;
        normal = normalize(mul(ObjectToWorld(), float4(normal, 0.0f)).xyz);

	// get material color
        float3 materialColor = float3(1.0f, 1.0f, 1.0f) * 0.95f;

	// reflection direction
        // float3 reflectDir = normalize(normalize(rayDirW) - 2 * dot(normal, normalize(rayDirW)) * normal);

	// set up ray
        RayDesc rayDiffuse;
        rayDiffuse.Origin = hitPoint;
        rayDiffuse.Direction = getCosHemisphereSample(payload.seed, normal);
        rayDiffuse.TMin = 0.0001; // watch out for this value
        rayDiffuse.TMax = 100000;

        payload.depth -= 1;

		TraceRay(
					gRtScene, 
					0 /*rayFlags*/, 
					0xFF, /* ray mask*/
					0 /* ray index*/, 
					1 /* total nbr of hitgroups*/, 
					0 /*miss shader index*/, 
					rayDiffuse, 
					payload
				);
       
        float3 incomingColor =  payload.color;
	
        payload.color = materialColor * incomingColor;
    }
}

[shader("closesthit")]
void planeChs(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    if (payload.depth <= 0)
    {
        payload.color = float3(0.0, 0.0, 0.0);
    }
    else
    {
		// get hit point
        float hitT = RayTCurrent();
        float3 rayDirW = WorldRayDirection();
        float3 rayOriginW = WorldRayOrigin();
        float3 hitPoint = rayOriginW + rayDirW * hitT;

		// get normal
        float3 normal = normals[0];
        normal = normalize(mul(ObjectToWorld(), float4(normal, 0.0f)).xyz);
        normal = faceforward(normal, rayDirW, normal);

		// get material color
        float3 materialColor;// = float3(1.0f, 1.0f, 1.0f);
        int id = InstanceID();
        if (id < 3)
        {
            materialColor = float3(1.0f, 1.0f, 1.0f) * 0.95f;
        }
        else if (id == 3)
        {
            materialColor = float3(1.0f, 0.0f, 0.0f) * 0.95f;
        }
        else if (id == 4)
        {
            materialColor = float3(0.0f, 1.0f, 0.0f) * 0.95f;
        }

		// set up ray
        RayDesc rayDiffuse;
        rayDiffuse.Origin = hitPoint;
        rayDiffuse.Direction = getCosHemisphereSample(payload.seed, normal);
        rayDiffuse.TMin = 0.0001; // watch out for this value
        rayDiffuse.TMax = 100000;

        payload.depth -= 1;

        TraceRay(	gRtScene,
					0 /*rayFlags*/, 
					0xFF, /* ray mask*/
					0 /* ray index*/, 
					1 /* total nbr of hitgroups*/, 
					0 /*miss shader index*/, 
					rayDiffuse, 
					payload
				);
       
        float3 incomingColor = payload.color;

        payload.color = materialColor * incomingColor;
    }
}

[shader("closesthit")]
void areaLightChs(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    payload.color = float3(1.0f, 1.0f, 1.0f);
}