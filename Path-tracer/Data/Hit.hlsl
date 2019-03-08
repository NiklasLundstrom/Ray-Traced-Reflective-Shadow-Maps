///// Common /////////////////
struct RayPayload
{
	// rgb is color
	// w is distance. -1 means stop bouncing, i.e. miss or light source.
    float4 colorAndDistance;
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
    float3 color = A * barycentrics.x + B * barycentrics.y + C * barycentrics.z;

    float3 normal = normals[0];
	normal = normalize(mul(ObjectToWorld(), float4(normal, 0.0f)).xyz);

    normal = faceforward(normal, WorldRayDirection(), normal);

    float3 lightDir = normalize(float3(0.5, 0.5, -0.5)); // Take the value from Common!
    float nDotL = max(0.0f, dot(normal, lightDir));

	payload.colorAndDistance.rgb = color * nDotL;
    payload.colorAndDistance.w = RayTCurrent();

}

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

    float3 lightDir = normalize(float3(0.5, 0.5, -0.5));// Take the value from Common!
    float nDotL = max(0.f, dot(normal, lightDir));

    payload.colorAndDistance.rgb = float3(1.0f, 1.0f, 1.0f) * nDotL;
    payload.colorAndDistance.w = RayTCurrent();

}



[shader("closesthit")]
void planeChs(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    float hitT = RayTCurrent();
    float3 rayDirW = WorldRayDirection();
    float3 rayOriginW = WorldRayOrigin();

    float3 normal = normals[0];
    normal = normalize(mul(ObjectToWorld(), float4(normal, 0.0f)).xyz);
   
    float3 lightDir = normalize(float3(0.5, 0.5, -0.5)); // Take the value from Common!
    float nDotL = max(0.0f, dot(normal, lightDir));

    payload.colorAndDistance.rgb = float3(0.9f, 0.9f, 0.9f) * nDotL;
    payload.colorAndDistance.w = RayTCurrent();

}