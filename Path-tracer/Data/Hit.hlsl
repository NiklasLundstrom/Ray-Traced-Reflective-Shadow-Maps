#include "Common.hlsli"
#include "hlslUtils.hlsli"
float3 sampleIndirectLight(float3 hitPoint, float3 hitPointNormal);

#define HYBRID


RaytracingAccelerationStructure gRtScene : register(t0);

StructuredBuffer<uint> indices : register(t1);
StructuredBuffer<float3> normals : register(t2);

Texture2D<float> gShadowMap_Depth : register(t0, space1);
Texture2D<float4> gShadowMap_Position : register(t1, space1);
Texture2D<float4> gShadowMap_Normal : register(t2, space1);
Texture2D<float4> gShadowMap_Flux : register(t3, space1);


[shader("closesthit")]
void robotChs(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
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
        //normal = faceforward(normal, rayDirW, normal);


	// get material color
        float3 materialColor = float3(1.0f, 1.0f, 1.0f) * 0.5f;

	#ifdef HYBRID
        float3 indirectColor = sampleIndirectLight(hitPoint, normal);

        float3 incomingColor = indirectColor;
	#else


	// define out direction
        float3 outDir;

        if (nextRand(payload.seed) < 0.2)
        {
			// reflection direction
            outDir = normalize(reflect(rayDirW, normal));
        }
		else
        {
			// diffuse random direction
            outDir = getCosHemisphereSample(payload.seed, normal);
        }
	// set up ray
        RayDesc rayDiffuse;
        rayDiffuse.Origin = hitPoint;
        rayDiffuse.Direction = outDir;
        rayDiffuse.TMin = 0.0001; // watch out for this value
        rayDiffuse.TMax = 100000;

        payload.depth -= 1;

        TraceRay(
				gRtScene,
				0 /*rayFlags*/,
				0xFF, /* ray mask*/
				0 /* ray index*/,
				2 /* total nbr of hitgroups*/,
				0 /*miss shader index*/,
				rayDiffuse,
				payload
			);
       
        float3 incomingColor = payload.color;
	
#endif

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
        //normal = faceforward(normal, rayDirW, normal);

		// get material color
        float3 materialColor;// = float3(1.0f, 1.0f, 1.0f);
        int id = InstanceID();
        if (id <=1 )
        {
            materialColor = float3(1.0f, 1.0f, 1.0f) * 0.5f;
        }
        
		// reflection direction
        //float3 reflectDir = normalize(normalize(rayDirW) - 2 * dot(normal, normalize(rayDirW)) * normal);
        
		

        
	#ifdef HYBRID
        float3 indirectColor = sampleIndirectLight(hitPoint, normal);

        float3 incomingColor = indirectColor;
	#else
		// set up diffuse ray
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
					2 /* total nbr of hitgroups*/, 
					0 /*miss shader index*/, 
					rayDiffuse, 
					payload
				);
       
        float3 incomingColor = payload.color;

	#endif


            payload.color = materialColor * incomingColor;

        }
    }

[shader("closesthit")]
void areaLightChs(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    payload.color = float3(1.0f, 0.0f, 1.0f)*5.0f;
}



float3 sampleIndirectLight(float3 hitPoint, float3 hitPointNormal)
{
	
    uint shadowWidth;
    uint shadowHeight;
    gShadowMap_Position.GetDimensions(shadowWidth, shadowHeight);

		// set up shadow rays
    ShadowPayload shadowPayload;
    float3 indirectColor = float3(0.0, 0.0, 0.0);

    RayDesc rayShadow;
    rayShadow.Origin = hitPoint;
    rayShadow.TMin = 0.0001;

    for (int i = 0; i < shadowWidth; i += 4)
    {
        for (int j = 0; j < shadowHeight; j += 4)
        {
				// sample shadow map
            float3 lightPos = gShadowMap_Position[uint2(i, j)].xyz;
            float3 direction = lightPos - hitPoint;
            float distance = length(direction);
            direction = normalize(direction);

				// set up ray
            rayShadow.TMax = distance - 0.0001; // minus 0.0001?
            rayShadow.Direction = direction;

            TraceRay(
							gRtScene,
							0 /*rayFlags*/,
							0xFF, /* ray mask*/
							1 /* ray index*/,
							2 /* total nbr of hitgroups*/,
							1 /*miss shader index*/,
							rayShadow,
							shadowPayload
						);

            if (shadowPayload.hit == false)// we reached the light point
            {
                float angle = dot(direction, hitPointNormal);
                indirectColor += angle * gShadowMap_Flux[uint2(i, j)].xyz;
            }
        }
    }

    indirectColor /= shadowWidth * shadowHeight / 16;

    return indirectColor;

}