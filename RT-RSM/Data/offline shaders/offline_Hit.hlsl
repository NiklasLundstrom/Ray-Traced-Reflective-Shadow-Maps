#include "../Common.hlsli"
#include "../hlslUtils.hlsli"
void sampleDiffuseLight(in float3 hitPoint, in float3 hitPointNormal, inout OfflineRayPayload payload);
void sampleRay(in float3 hitPoint, in float3 direction, inout OfflineRayPayload payload);
float3 sampleDirectLight(in float3 hitPoint, in float3 hitPointNormal, inout OfflineRayPayload payload);



RaytracingAccelerationStructure gRtScene : register(t0);

StructuredBuffer<uint> indices : register(t1);
StructuredBuffer<float3> normals : register(t2);
StructuredBuffer<float3> color : register(t3);

cbuffer LightBuffer : register(b0)
{
    float4x4 worldToView;
    float4x4 projection;
};

cbuffer lightPosition : register(b1)
{
    float3 lightPosition;
};

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
    else if (payload.depth == 1)
    {
        float3 incomingColor = sampleDirectLight(hitPoint, normal, payload);
        float3 materialColor = color[InstanceID()];
        payload.color = materialColor * incomingColor;
    }
    else
    {
		// get material color
        float3 materialColor = color[InstanceID()];
            int currentDepth = payload.depth;

        //if (nextRand(payload.seed) < 0.8f)
        //{
            float3 directColor = sampleDirectLight(hitPoint, normal, payload);
			payload.depth = currentDepth;
            sampleDiffuseLight(hitPoint, normal, payload);
            float3 incomingColor = payload.color + directColor;

            if (currentDepth == 2)
            {
                payload.color = incomingColor;
            }
            else
            {
                payload.color = materialColor * incomingColor;
            }
        //}
    }
}
[shader("closesthit")]
void areaLightChs(inout OfflineRayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    payload.color = float3(1.0f, 1.0f, 0.984f) * 8000.0f; //12500.0f;
}

float3 sampleDirectLight(in float3 hitPoint, in float3 hitPointNormal, inout OfflineRayPayload payload)
{
	// shoot ray directly to the light
    float3 direction = lightPosition - hitPoint;
		// Construct TBN matrix to position disk samples towards shadow ray direction
    float3 n = normalize(lightPosition - hitPoint);
    float3 rvec = normalize(mul(float4(hitPoint, 1.0f), worldToView).xyz);
    float3 b1 = normalize(rvec - n * dot(rvec, n));
    float3 b2 = cross(n, b1);
    float3x3 tbn = float3x3(b1, b2, n);

		// pick random sample
		// from Ray-tracing gems, 16.5.1.2
    float xi1 = nextRand(payload.seed);
    float xi2 = nextRand(payload.seed);
    float R = 0.0f; // Light radius
    float a = 2.0 * xi1 - 1.0;
    float b = 2.0 * xi2 - 1.0;
    float r;
    float phi;
    if (a * a > b * b)
    {
        r = R * a;
        phi = (PI / 4.0) * (b / a);
    }
    else
    {
        r = R * b;
        phi = (PI / 2.0) - (PI / 4.0) * (a / b);
    }
    float2 diskSample;
    diskSample.x = r * cos(phi);
    diskSample.y = r * sin(phi);
	
		// direction from https://github.com/Apress/ray-tracing-gems/blob/master/Ch_13_Ray_Traced_Shadows_Maintaining_Real-Time_Frame_Rates/dxrShadows/Data/DXRShadows.rt.hlsl
    float3 sampleDirection = lightPosition + (mul(float3(diskSample.x, diskSample.y, 0.0f), tbn)) - hitPoint;
	
    direction = normalize(direction);
    float angle = saturate(dot(direction, hitPointNormal));
    if (angle < 0.000001)
    {
        return float3(0.0, 0.0, 0.0);
    }
    RayDesc rayDiffuse;
    rayDiffuse.Origin = hitPoint;
    rayDiffuse.Direction = sampleDirection;
    rayDiffuse.TMin = 0.0001;
    rayDiffuse.TMax = 100000;
    payload.depth = 0;

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

    float3 incomingColor = angle * payload.color * 0.001f;
    return incomingColor;
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

    payload.depth -= 1;

    TraceRay(gRtScene,
					0 /*rayFlags*/,
					0xFE, /* ray mask*/ // <--- Mask out Area light
					0 /* ray index*/,
					1 /* total nbr of hitgroups*/,
					0 /*miss shader index*/,
					rayDiffuse,
					payload
				);

}