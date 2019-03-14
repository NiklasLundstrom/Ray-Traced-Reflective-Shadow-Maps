#include "Common.hlsli"

RaytracingAccelerationStructure gRtScene : register(t0);

StructuredBuffer<uint> indices : register(t1);
StructuredBuffer<float3> normals : register(t2);


[shader("closesthit")]
void teapotChs(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    uint vertIndex = 3 * PrimitiveIndex();
    float3 n0 = normals[indices[vertIndex + 0]];
    float3 n1 = normals[indices[vertIndex + 1]];
    float3 n2 = normals[indices[vertIndex + 2]];
    float3 normal =   n0 * (1 - attribs.barycentrics.x - attribs.barycentrics.y)
					+ n1 * attribs.barycentrics.x
					+ n2 * attribs.barycentrics.y;
    normal = normalize(mul(ObjectToWorld(), float4(normal, 0.0f)).xyz);

    payload.colorAndDistance.rgb = float3(0.8f,0.8f, 0.8f);
    payload.colorAndDistance.w = RayTCurrent();
    payload.normal = normal;

}

[shader("closesthit")]
void planeChs(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    float hitT = RayTCurrent();
    float3 rayDirW = WorldRayDirection();
    float3 rayOriginW = WorldRayOrigin();

    float3 normal = normals[0];
    normal = normalize(mul(ObjectToWorld(), float4(normal, 0.0f)).xyz);
    payload.normal = normal;

    int id = InstanceID();
    if (id < 3)
    {
        payload.colorAndDistance.rgb = float3(1.0f, 1.0f, 1.0f);
    }
    else if (id == 3)
    {
        payload.colorAndDistance.rgb = float3(1.0f, 0.0f, 0.0f);
    }
    else if (id == 4)
    {
        payload.colorAndDistance.rgb = float3(0.0f, 1.0f, 0.0f);
    }

	payload.colorAndDistance.w = RayTCurrent();

    }

[shader("closesthit")]
void areaLightChs(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    payload.colorAndDistance.rgb = float3(1.0f, 1.0f, 1.0f);
    payload.colorAndDistance.w = -1;

}