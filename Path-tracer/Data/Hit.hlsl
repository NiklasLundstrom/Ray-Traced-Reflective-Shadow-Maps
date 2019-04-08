#include "Common.hlsli"
#include "hlslUtils.hlsli"

RaytracingAccelerationStructure gRtScene : register(t0);

StructuredBuffer<uint> indices : register(t1);
StructuredBuffer<float3> normals : register(t2);


bool myRefract(in float3 v, in float3 n, in float ni_over_nt, inout float3 refracted)
{
	// taken from
	// http://www.realtimerendering.com/raytracing/Ray%20Tracing%20in%20a%20Weekend.pdf
	//
    float3 uv = normalize(v);
    float dt = dot(uv, n);
    float discriminant = 1.0f - ni_over_nt * ni_over_nt * (1 - dt * dt);
    if (discriminant > 0)
    {
        refracted = ni_over_nt * (uv - n * dt) - n * sqrt(discriminant);
        return true;
    }
	else
        return false;
}

float schlick(in float cosine, in float ref_idx)
{
	// taken from
	// http://www.realtimerendering.com/raytracing/Ray%20Tracing%20in%20a%20Weekend.pdf
	//
    float r0 = (1 - ref_idx) / (1 + ref_idx);
    r0 = r0 * r0;
    return r0 + (1 - r0) * pow((1 - cosine), 5);
}

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
        float3 materialColor = float3(1.0f, 1.0f, 1.0f) * 0.5f;

	// define next ray
        float3 outDir;

        if (InstanceID() == 0)
        {
            outDir = getCosHemisphereSample(payload.seed, normal);
        }
		else // Instance 1
        {
			// reflection direction
				float3 reflectDir = normalize(reflect(rayDirW, normal));

            float cosine;
            float reflectProb;

			// refraction
				float3 refractDir;
				float ref_idx = 1.5;
				float ni_over_nt = (dot(rayDirW, normal) > 0) ? ref_idx : 1.0 / ref_idx;
				cosine = (dot(rayDirW, normal) > 0) ? ref_idx : 1.0;
				normal = faceforward(normal, rayDirW, normal);
				cosine *= -dot(rayDirW, normal) / length(rayDirW);
				

                reflectProb = (myRefract(rayDirW, normal, ni_over_nt, refractDir)) 
								? schlick(cosine, ref_idx) : 1.0f;
            
				outDir = (nextRand(payload.seed) < reflectProb)
							? reflectDir : refractDir;
            
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
        float3 materialColor; // = float3(1.0f, 1.0f, 1.0f);
        int id = InstanceID();
        if (id <= 1)
        {
            materialColor = float3(1.0f, 1.0f, 1.0f) * 0.5f;
        }
        
	// reflection direction
    //float3 reflectDir = normalize(normalize(rayDirW) - 2 * dot(normal, normalize(rayDirW)) * normal);
        

	// set up ray
        RayDesc rayDiffuse;
        rayDiffuse.Origin = hitPoint;
        rayDiffuse.Direction = getCosHemisphereSample(payload.seed, normal);
        rayDiffuse.TMin = 0.0001; // watch out for this value
        rayDiffuse.TMax = 100000;

        payload.depth -= 1;

        TraceRay(gRtScene,
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

    void areaLightChs
    (inout
    RayPayload payload, in BuiltInTriangleIntersectionAttributes
    attribs)
{
        payload.color = float3(1.0f, 0.0f, 1.0f) * 0.0f;
    }