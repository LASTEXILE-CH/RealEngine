#include "common.hlsli"
#include "model_constants.hlsli"
#include "global_constants.hlsli"

cbuffer VertexCB : register(b0)
{
    uint c_posBuffer;
    uint c_uvBuffer;
    uint c_normalBuffer;
    uint c_tangentBuffer;
};

struct VSOutput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
#if NORMAL_TEXTURE
    float3 tangent : TANGENT;
#endif
    float3 worldPos : TEXCOORD1;
};

VSOutput vs_main(uint vertex_id : SV_VertexID)
{
    StructuredBuffer<float3> posBuffer = ResourceDescriptorHeap[c_posBuffer];
    StructuredBuffer<float2> uvBuffer = ResourceDescriptorHeap[c_uvBuffer];
    StructuredBuffer<float3> normalBuffer = ResourceDescriptorHeap[c_normalBuffer];
    StructuredBuffer<float3> tangentBuffer = ResourceDescriptorHeap[c_tangentBuffer];
    
    float4 pos = float4(posBuffer[vertex_id], 1.0);
    
    VSOutput output;
    output.pos = mul(ModelCB.mtxWVP, pos);
    output.uv = uvBuffer[vertex_id];
    output.normal = mul(ModelCB.mtxNormal, float4(normalBuffer[vertex_id], 0.0f)).xyz;
    output.worldPos = mul(ModelCB.mtxWorld, pos).xyz;
    
#if NORMAL_TEXTURE
    output.tangent = mul(ModelCB.mtxWorld, float4(tangentBuffer[vertex_id], 0.0f)).xyz;
#endif
    
    return output;
}

float Shadow(float3 worldPos, float3 worldNormal, float4x4 mtxLightVP, Texture2D shadowRT, SamplerComparisonState shadowSampler)
{
    float4 shadow_coord = mul(mtxLightVP, float4(worldPos + worldNormal * 0.001, 1.0));
    shadow_coord /= shadow_coord.w;
    shadow_coord.xy = shadow_coord.xy * float2(0.5, -0.5) + 0.5;
    
#if 0
    return shadowRT.SampleCmpLevelZero(shadowSampler, shadow_coord.xy, shadow_coord.z).x;
#else
    const float halfTexel = 0.5 / 2048;
    float visibility = shadowRT.SampleCmpLevelZero(shadowSampler, shadow_coord.xy + float2(halfTexel, halfTexel), shadow_coord.z).x;
    visibility += shadowRT.SampleCmpLevelZero(shadowSampler, shadow_coord.xy + float2(-halfTexel, halfTexel), shadow_coord.z).x;
    visibility += shadowRT.SampleCmpLevelZero(shadowSampler, shadow_coord.xy + float2(halfTexel, halfTexel), shadow_coord.z).x;
    visibility += shadowRT.SampleCmpLevelZero(shadowSampler, shadow_coord.xy + float2(-halfTexel, -halfTexel), shadow_coord.z).x;
    return visibility / 4.0f;
#endif
}

float4 ps_main(VSOutput input) : SV_TARGET
{   
    SamplerState linearSampler = SamplerDescriptorHeap[MaterialCB.linearSampler];

    float3 V = normalize(CameraCB.cameraPos - input.worldPos);
    float3 N = normalize(input.normal);
    float3 ambient = 0.2;

    float4 albedo = float4(MaterialCB.albedo.xyz, 1.0);
    float metallic = MaterialCB.metallic;
    float roughness = MaterialCB.roughness;

#if ALBEDO_TEXTURE
    Texture2D albedoTexture = ResourceDescriptorHeap[MaterialCB.albedoTexture];
	albedo *= albedoTexture.Sample(linearSampler, input.uv);
#endif
    
#if ALPHA_TEST
    clip(albedo.a - MaterialCB.alphaCutoff);
#endif

#if METALLIC_ROUGHNESS_TEXTURE
    Texture2D metallicRoughnessTexture = ResourceDescriptorHeap[MaterialCB.metallicRoughnessTexture];
    float4 metallicRoughness = metallicRoughnessTexture.Sample(linearSampler, input.uv);
    metallic *= metallicRoughness.b;
    roughness *= metallicRoughness.g;
#endif

#if NORMAL_TEXTURE
    float3 T = normalize(input.tangent);
    float3 B = normalize(cross(T, N));

    Texture2D normalTexture = ResourceDescriptorHeap[MaterialCB.normalTexture];
    float3 normal = normalTexture.Sample(linearSampler, input.uv).xyz;
    normal = normal * 2.0 - 1.0;

    N = normalize(normal.x * T + normal.y * B + normal.z * N);
#endif
    
    float3 diffuse = albedo.xyz * (1.0 - metallic);
    float3 specular = lerp(0.04, albedo.xyz, metallic);

    Texture2D shadowRT = ResourceDescriptorHeap[SceneCB.shadowRT];
    SamplerComparisonState shadowSampler = SamplerDescriptorHeap[SceneCB.shadowSampler];
    float visibility = Shadow(input.worldPos, N, SceneCB.mtxLightVP, shadowRT, shadowSampler);
    
    float3 direct_light = BRDF(SceneCB.lightDir, V, N, diffuse, specular, roughness) * visibility * SceneCB.lightColor;
    
    float3 indirect_light = diffuse * ambient;

    float3 radiance = direct_light + indirect_light;

    return float4(radiance, 1.0);
}
