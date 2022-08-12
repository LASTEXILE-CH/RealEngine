#include "../ray_trace.hlsli"
#include "../random.hlsli"
#include "../importance_sampling.hlsli"

cbuffer CB : register(b1)
{
    uint c_depthTexture;
    uint c_normalTexture;
    uint c_prevLinearDepthTexture;
    uint c_historyRadiance;
    uint c_outputRadianceUAV;
    uint c_outputHitNormalUAV;
    uint c_outputRayUAV;
}

float3 GetIndirectDiffuseLighting(float3 position, rt::MaterialData material)
{
    Texture2D historyRadianceTexture = ResourceDescriptorHeap[c_historyRadiance];
    SamplerState linearSampler = SamplerDescriptorHeap[SceneCB.bilinearClampSampler];
    
    Texture2D prevLinearDepthTexture = ResourceDescriptorHeap[c_prevLinearDepthTexture];
    SamplerState pointSampler = SamplerDescriptorHeap[SceneCB.pointClampSampler];
    
    float4 prevClipPos = mul(CameraCB.mtxPrevViewProjection, float4(position, 1.0));
    float3 prevNdcPos = GetNdcPosition(prevClipPos);
    float2 prevUV = GetScreenUV(prevNdcPos.xy);
    float prevLinearDepth = prevLinearDepthTexture.SampleLevel(pointSampler, prevUV, 0.0).x;
    
    if (any(prevUV < 0.0) || any(prevUV > 1.0) ||
        abs(GetLinearDepth(prevNdcPos.z) - prevLinearDepth) > 0.05)
    {
        return 0.0; //todo : maybe some kind of world radiance cache
    }
    
    float3 historyRadiance = historyRadianceTexture.SampleLevel(linearSampler, prevUV, 0).xyz;
    return historyRadiance * material.diffuse;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    Texture2D<float> depthTexture = ResourceDescriptorHeap[c_depthTexture];
    Texture2D normalTexture = ResourceDescriptorHeap[c_normalTexture];
    RWTexture2D<float4> outputRadianceUAV = ResourceDescriptorHeap[c_outputRadianceUAV];
    RWTexture2D<float2> outputHitNormalUAV = ResourceDescriptorHeap[c_outputHitNormalUAV];
    RWTexture2D<float2> outputRayUAV = ResourceDescriptorHeap[c_outputRayUAV];
    
    float depth = depthTexture[dispatchThreadID.xy];    
    if (depth == 0.0)
    {
        outputRadianceUAV[dispatchThreadID.xy] = 0.xxxx;
        outputHitNormalUAV[dispatchThreadID.xy] = 0.xx;
        outputRayUAV[dispatchThreadID.xy] = 0.xx;
        return;
    }

    float3 worldPos = GetWorldPosition(dispatchThreadID.xy, depth);
    float3 N = DecodeNormal(normalTexture[dispatchThreadID.xy].xyz);
    
    BNDS<1> bnds = BNDS<1>::Create(dispatchThreadID.xy, SceneCB.renderSize);
    float3 direction = SampleUniformHemisphere(bnds.RandomFloat2(), N); //uniform sample hemisphere, following the ReSTIR GI paper
    float pdf = 1.0 / (2.0 * M_PI);

    RayDesc ray;
    ray.Origin = worldPos + N * 0.01;
    ray.Direction = direction;
    ray.TMin = 0.00001;
    ray.TMax = 1000.0;

    rt::RayCone cone = rt::RayCone::FromGBuffer(GetLinearDepth(depth));
    rt::HitInfo hitInfo = (rt::HitInfo)0;
    
    float3 radiance = 0.0;
    float3 hitNormal = 0.0;
    
    if (rt::TraceRay(ray, hitInfo))
    {
        cone.Propagate(0.0, hitInfo.rayT); // using 0 since no curvature measure at second hit
        rt::MaterialData material = rt::GetMaterial(ray, hitInfo, cone);
        
        RayDesc ray;
        ray.Origin = hitInfo.position + material.worldNormal * 0.01;
        ray.Direction = SceneCB.lightDir;
        ray.TMin = 0.00001;
        ray.TMax = 1000.0;
        float visibility = rt::TraceVisibilityRay(ray) ? 1.0 : 0.0;
        float3 direct_lighting = DefaultBRDF(SceneCB.lightDir, -direction, material.worldNormal, material.diffuse, material.specular, material.roughness) * visibility;
        
        float3 indirect_lighting = GetIndirectDiffuseLighting(hitInfo.position, material);
        
        radiance = material.emissive + direct_lighting + indirect_lighting;
        hitNormal = material.worldNormal;
    }
    else
    {
        TextureCube skyTexture = ResourceDescriptorHeap[SceneCB.skyCubeTexture];
        SamplerState linearSampler = SamplerDescriptorHeap[SceneCB.bilinearClampSampler];
        radiance = skyTexture.SampleLevel(linearSampler, direction, 0).xyz;
        hitNormal = -direction;
    }
    
    outputRadianceUAV[dispatchThreadID.xy] = float4(radiance, hitInfo.rayT);
    outputHitNormalUAV[dispatchThreadID.xy] = OctEncode(hitNormal) * 0.5 + 0.5;
    outputRayUAV[dispatchThreadID.xy] = OctEncode(direction) * 0.5 + 0.5;
}