#include "Common.hlsli"
#include "hlslUtils.hlsli"
float3 sampleIndirectLight(in float3 hitPoint, in float3 hitPointNormal, inout RayPayload payload);
float3 sampleDirectLight(in float3 hitPoint, in float3 hitPointNormal);
void sampleDiffuseLight(in float3 hitPoint, in float3 hitPointNormal, inout RayPayload payload);
void sampleRay(in float3 hitPoint, in float3 direction, inout RayPayload payload);

#define HYBRID


RaytracingAccelerationStructure gRtScene : register(t0);

StructuredBuffer<uint> indices : register(t1);
StructuredBuffer<float3> normals : register(t2);

cbuffer lightPosition : register(b0, space1)
{
    float3 lightPosition;
};
Texture2D<float> gShadowMap_Depth : register(t0, space1);
Texture2D<float4> gShadowMap_Position : register(t1, space1);
Texture2D<float4> gShadowMap_Normal : register(t2, space1);
Texture2D<float4> gShadowMap_Flux : register(t3, space1);


[shader("closesthit")]
void modelChs(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
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
        float3 materialColor = float3(1.0f, 1.0f, 1.0f) * 0.5f;

#ifdef HYBRID
        float3 directColor = sampleDirectLight(hitPoint, normal);
        float3 indirectColor = sampleIndirectLight(hitPoint, normal, payload);

        float3 incomingColor = directColor + indirectColor;
#else


        if (nextRand(payload.seed) < 0.2)
        {
			// reflection direction
            float3 direction = normalize(reflect(rayDirW, normal));
            sampleRay(hitPoint, direction, payload);
        }
		else
        {
			// diffuse random direction
			sampleDiffuseLight(hitPoint, normal, payload);
        }
        float3 incomingColor = payload.color;
	
#endif

            payload.color = materialColor * incomingColor;
        }
    }

[shader("closesthit")]
void areaLightChs(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    payload.color = float3(1.0f, 0.0f, 1.0f)*15.0f;
}



float3 sampleIndirectLight(in float3 hitPoint, in float3 hitPointNormal, inout RayPayload payload)
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

    int numRaySamples = 0;
    int numTotSamples = 0;
    //while(numRaySamples < 40 && numTotSamples < 400)
    //{

    for (int i = 0; i < shadowWidth; i += 5)
    {
        for (int j = 0; j < shadowHeight; j += 5)
        {

        numTotSamples++;
		// pick random sample
    //int i = round(nextRand(payload.seed) * shadowWidth);
    //int j = round(nextRand(payload.seed) * shadowHeight);


			// sample shadow map
                float4 lightPosData = gShadowMap_Position[uint2(i, j)];
                if (lightPosData.w == 0)
                {
                    continue;
                }
			

                float3 lightPos = lightPosData.xyz;
                float3 direction = lightPos - hitPoint;
                float distance = length(direction);
                direction = normalize(direction);

			// do not sample if light is below the surface or
			// on the same plane as the hit point 
                float angleHitPoint = saturate(dot(direction, hitPointNormal));
                if (angleHitPoint < 0.0001)
                {
                    continue;
                }
			// do not sample if hit point is below the pixel light's surface,
			// or on the same plane
                float3 pixelLightNormal = gShadowMap_Normal[uint2(i, j)].rgb;
                pixelLightNormal = pixelLightNormal * 2 - 1;

                float angleLightPoint = saturate(dot(-direction, pixelLightNormal));
                if (angleLightPoint < 0.0001)
                {
                    continue;
                }

			//else 
                numRaySamples++;

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
                    indirectColor += angleHitPoint
								 * angleLightPoint
								 * gShadowMap_Flux[uint2(i, j)].rgb * 625.0f / max((distance * distance), 0.0f);
        }
    }
}
    //}

    if (numRaySamples > 0)
    {
        indirectColor /= numTotSamples;// (shadowWidth * shadowHeight / 25); //numSamples;
    }
    return
indirectColor;

}

float3 sampleDirectLight(in float3 hitPoint, in float3 hitPointNormal)
{
    ShadowPayload shadowPayload;
 
    RayDesc rayShadow;
    rayShadow.Origin = hitPoint;
    float3 direction = lightPosition - hitPoint;
    float distance = length(direction);
	direction = normalize(direction);
	float angle = saturate( dot(direction, hitPointNormal));
    if (angle < 0.0001)
    {
        return float3(0.0, 0.0, 0.0);
    }
    rayShadow.Direction = direction;
    rayShadow.TMin = 0.0001;
    rayShadow.TMax = distance - 0.0001;

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

    float3 outColor;
    if (shadowPayload.hit == false) // no occlusion
    {
        
        outColor = angle * float3(1.0, 0.0, 0.0);
    }
    else // shadow
    {
        outColor = float3(0.0, 0.0, 0.0);
    }

    return outColor;

}

void sampleDiffuseLight(in float3 hitPoint, in float3 hitPointNormal, inout RayPayload payload)
{
        float3 direction = getCosHemisphereSample(payload.seed, hitPointNormal);
        sampleRay(hitPoint, direction, payload);
}

void sampleRay(in float3 hitPoint, in float3 direction, inout RayPayload payload)
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
					0xFF, /* ray mask*/
					0 /* ray index*/,
					2 /* total nbr of hitgroups*/,
					0 /*miss shader index*/,
					rayDiffuse,
					payload
				);


    }