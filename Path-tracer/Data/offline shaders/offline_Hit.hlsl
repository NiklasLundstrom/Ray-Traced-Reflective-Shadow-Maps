#include "../Common.hlsli"
#include "../hlslUtils.hlsli"
void sampleDiffuseLight(in float3 hitPoint, in float3 hitPointNormal, inout OfflineRayPayload payload);
void sampleRay(in float3 hitPoint, in float3 direction, inout OfflineRayPayload payload);


RaytracingAccelerationStructure gRtScene : register(t0);

StructuredBuffer<uint> indices : register(t1);
StructuredBuffer<float3> normals : register(t2);
StructuredBuffer<float3> color : register(t3);



[shader("closesthit")]
void modelChs(inout OfflineRayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
	// get hit point
    float hitT = RayTCurrent();
    float3 rayDirW = WorldRayDirection();
    float3 rayOriginW = WorldRayOrigin();
    float3 hitPoint = rayOriginW + rayDirW * hitT;
    uint2 pixelCrd = DispatchRaysIndex().xy;

	// get normal
    uint vertIndex = 3 * PrimitiveIndex();
    float3 n0 = normals[indices[vertIndex + 0]];
    float3 n1 = normals[indices[vertIndex + 1]];
    float3 n2 = normals[indices[vertIndex + 2]];
    float3 normal = n0 * (1 - attribs.barycentrics.x - attribs.barycentrics.y)
				+ n1 * attribs.barycentrics.x
				+ n2 * attribs.barycentrics.y;
    normal = normalize(mul(ObjectToWorld(), float4(normal, 0.0f)).xyz);
       
    if (payload.depth <= 0)
    {
        payload.color = float3(0.0, 0.0, 0.0);
    }
    else
    {
		// get material color
        float3 materialColor = color[InstanceID()];

		// set up ray
        RayDesc rayDiffuse;
        rayDiffuse.Origin = hitPoint;
        rayDiffuse.Direction = getCosHemisphereSample(payload.seed, normal);
        rayDiffuse.TMin = 0.0001;
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

        float3 incomingColor = payload.color;

        if (payload.depth == 1)
        {
            payload.color = incomingColor;
        }
        else
        {
            payload.color = materialColor * incomingColor;
        }
    }
}
[shader("closesthit")]
void areaLightChs(inout OfflineRayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    payload.color = float3(1.0f, 1.0f, 0.984f) * 8000.0f; //12500.0f;
}



void sampleDiffuseLight(in float3 hitPoint, in float3 hitPointNormal, inout OfflineRayPayload payload)
{
    float3 direction = getCosHemisphereSample(payload.seed, hitPointNormal);
    sampleRay(hitPoint, direction, payload);
}

void sampleRay(in float3 hitPoint, in float3 direction, inout OfflineRayPayload payload)
{
	// set up diffuse ray
    RayDesc rayDiffuse;
    rayDiffuse.Origin = hitPoint;
    rayDiffuse.Direction = direction;
    rayDiffuse.TMin = 0.0001; // watch out for this value
    rayDiffuse.TMax = 100000;

        //payload.depth -= 1;

    TraceRay(gRtScene,
					0 /*rayFlags*/,
					0xFF, /* ray mask*/
					0 /* ray index*/,
					2 /* total nbr of hitgroups*/,
					0 /*miss shader index*/,
					rayDiffuse,
					payload
				);

}