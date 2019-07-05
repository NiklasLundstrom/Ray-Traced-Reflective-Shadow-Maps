#include "Common.hlsli"
#include "hlslUtils.hlsli"
float4 sampleIndirectLight(in float3 hitPoint, in float3 hitPointNormal, inout RayPayload payload, in float acceptedReprojection);
float3 sampleDirectLight(in float3 hitPoint, in float3 hitPointNormal, inout RayPayload payload);
void sampleDiffuseLight(in float3 hitPoint, in float3 hitPointNormal, inout RayPayload payload);
void sampleRay(in float3 hitPoint, in float3 direction, inout RayPayload payload);

#define HYBRID


RaytracingAccelerationStructure gRtScene : register(t0);

StructuredBuffer<uint> indices : register(t1);
StructuredBuffer<float3> normals : register(t2);

cbuffer LightBuffer : register(b0, space1)
{
    float4x4 worldToView;
    float4x4 projection;
};

cbuffer lightPosition : register(b1, space1)
{
    float3 lightPosition;
};

//Texture2D<float> gShadowMap_Depth : register(t0, space1);
Texture2D<float4> gShadowMap_Position : register(t1, space1);
Texture2D<float4> gShadowMap_Normal : register(t2, space1);
Texture2D<float4> gShadowMap_Flux : register(t3, space1);
Texture2D<float4> gMotionVector : register(t4, space1);


[shader("closesthit")]
void modelChs(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
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
       
	// get motion vector info
    float acceptedReprojection = gMotionVector[pixelCrd].z;

    float3 directColor = float3(0.0, 0.0, 0.0);
    for (int i = 0; i < 10; i++)
    {
        directColor += sampleDirectLight(hitPoint, normal, payload);
    }
    directColor /= 10.0f;
    float4 indirectColorNumRays = sampleIndirectLight(hitPoint, normal, payload, acceptedReprojection);
    float3 indirectColor = indirectColorNumRays.rgb;
    float numRays = indirectColorNumRays.a;

        //float3 incomingColor = directColor + indirectColor;


    payload.indirectColor = float4(indirectColor, numRays);
    payload.directColor = float3(directColor);
}

[shader("closesthit")]
void areaLightChs(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    payload.indirectColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
    payload.directColor = float3(1.0f, 0.0f, 1.0f) * 15.0f;
}



float4 sampleIndirectLight(in float3 hitPoint, in float3 hitPointNormal, inout RayPayload payload, in float acceptedReprojection)
{
    uint shadowWidth;
    uint shadowHeight;
    gShadowMap_Position.GetDimensions(shadowWidth, shadowHeight);

	// Project hit point into the light
    float4 newPosition = float4(hitPoint, 1.0);
    newPosition = mul(worldToView, newPosition);
    newPosition = mul(projection, newPosition);
    newPosition /= newPosition.w;
    float px = newPosition.x / newPosition.z;
    float py = newPosition.y / newPosition.z;
    px = px * 0.5f + 0.5f;
    py = py * 0.5f + 0.5f;
	// if outside range, clamp it
    if (px > 1.0f || px < 0.0f || py > 1.0f || py < 0.0f)
    {
        px = saturate(px);
        py = saturate(py);
		
        return float4(0.0f, 0.0f, 1.0f, 0.0f);
    }
    px = px * shadowWidth;
    py = (1 - py) * shadowHeight;
    uint2 crd;
    crd.x = floor(px);
    crd.y = floor(py);

    

	// set up shadow rays
    ShadowPayload shadowPayload;
    float3 indirectColor = float3(0.0, 0.0, 0.0);

    RayDesc rayShadow;
    rayShadow.Origin = hitPoint;
    rayShadow.TMin = 0.0001;

    int numRaySamples = 0;
    int numTotSamples = 0;
    int maxNumRays = acceptedReprojection ? 10 : 600;
    int maxNumTot = acceptedReprojection ? 50 : 600;
    while (numRaySamples < maxNumRays && numTotSamples < maxNumTot)
    {
        if (numTotSamples > 100 && numRaySamples == 0)
        {
            break;
        }


		// pick random sample
        float xi1 = nextRand(payload.seed);
        float xi2 = nextRand(payload.seed);
        float rMax = 400.0f;
        int i = floor(rMax * xi1 * sin(2 * PI * xi2));
        int j = floor(rMax * xi1 * cos(2 * PI * xi2));
			//int i = xi1 * shadowWidth;
			//int j = xi2 * shadowHeight;


		// outside shadow map texture
        if ((crd.x + i) < 0 || (crd.y + j) < 0 || (crd.x + i) >= shadowWidth || (crd.y + j) >= shadowHeight)
        {
            //return float3(0.0, 1.0, 0.0);
            continue;
        }
        numTotSamples++;

		// sample shadow map
        float4 lightPosData = gShadowMap_Position[crd + uint2(i, j)];
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
        float3 pixelLightNormal = gShadowMap_Normal[crd + uint2(i, j)].rgb;
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
								* gShadowMap_Flux[crd + uint2(i, j)].rgb * 625.0f / max((distance * distance), 0.1f) * xi1 * xi1 * 20.0f;
        }

    }

    if (numRaySamples > 0)
    {
        indirectColor /= numTotSamples; // (shadowWidth * shadowHeight / 25); //numSamples;
    }
    return float4(indirectColor, numRaySamples);

}

float3 sampleDirectLight(in float3 hitPoint, in float3 hitPointNormal, inout RayPayload payload)
{
    ShadowPayload shadowPayload;
 
    RayDesc rayShadow;
    rayShadow.Origin = hitPoint;

    float3 direction = lightPosition - hitPoint;
    float distance = length(direction);


	// Construct TBN matrix to position disk samples towards shadow ray direction

    float3 n = normalize(lightPosition - hitPoint);

    float3 rvec = normalize(mul(float4(hitPoint, 1.0f), worldToView));

    float3 b1 = normalize(rvec - n * dot(rvec, n));

    float3 b2 = cross(n, b1);



    float3x3 tbn = float3x3(b1, b2, n);

	// pick random sample
	// from Ray-tracing gems, 16.5.1.2
    float xi1 = nextRand(payload.seed);
    float xi2 = nextRand(payload.seed);
    float R = 2.0f; // Light radius
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
    //float3 sampleDirection = mul(tbn,float3(x, y, z) );
	

    direction = normalize(direction);
    float angle = saturate(dot(direction, hitPointNormal));
    if (angle < 0.0001)
    {
        return float3(0.0, 0.0, 0.0);
    }
    rayShadow.Direction = /*direction*/sampleDirection;
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
        
        outColor = angle * float3(1.0, 1.0, 1.0) * 0.3f;
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