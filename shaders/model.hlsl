#include "model.hlsli"
#include "debug.hlsli"

VertexOut vs_main(uint vertex_id : SV_VertexID)
{
    VertexOut v = GetVertex(c_SceneConstantAddress, vertex_id);
    return v;
}

struct GBufferOutput
{
    float4 diffuseRT : SV_TARGET0;
    float4 specularRT : SV_TARGET1;
    float4 normalRT : SV_TARGET2;
    float3 emissiveRT : SV_TARGET3;
};

GBufferOutput ps_main(VertexOut input, bool isFrontFace : SV_IsFrontFace)
{
#if !NON_UNIFORM_RESOURCE
    input.sceneConstantAddress = c_SceneConstantAddress;
#endif

    PbrMetallicRoughness pbrMetallicRoughness = GetMaterialMetallicRoughness(input);
    PbrSpecularGlossiness pbrSpecularGlossiness = GetMaterialSpecularGlossiness(input);
    float3 N = GetMaterialNormal(input, isFrontFace);
    float ao = GetMaterialAO(input);
    float3 emissive = GetMaterialEmissive(input);
    
    float3 diffuse = float3(1, 1, 1);
    float3 specular = float3(0, 0, 0);
    float roughness = 1.0;
    float alpha = 1.0;
    
#if PBR_METALLIC_ROUGHNESS
    diffuse = pbrMetallicRoughness.albedo * (1.0 - pbrMetallicRoughness.metallic);
    specular = lerp(0.04, pbrMetallicRoughness.albedo, pbrMetallicRoughness.metallic);
    roughness = pbrMetallicRoughness.roughness;
    alpha = pbrMetallicRoughness.alpha;
    ao *= pbrMetallicRoughness.ao;
#endif //PBR_METALLIC_ROUGHNESS

#if PBR_SPECULAR_GLOSSINESS
    specular = pbrSpecularGlossiness.specular;
    diffuse = pbrSpecularGlossiness.diffuse * (1.0 - max(max(specular.r, specular.g), specular.b));
    roughness = 1.0 - pbrSpecularGlossiness.glossiness;
    alpha = pbrSpecularGlossiness.alpha;
#endif //PBR_SPECULAR_GLOSSINESS
    
#if ALPHA_TEST
    clip(alpha - GetMaterialConstant(input.sceneConstantAddress).alphaCutoff);
#endif
    
#define DEBUG_MESHLET 0
#if DEBUG_MESHLET
    uint mhash = Hash(input.meshlet);
    diffuse.xyz = float3(float(mhash & 255), float((mhash >> 8) & 255), float((mhash >> 16) & 255)) / 255.0;
    roughness = 1.0;
    emissive = float3(0.0, 0.0, 0.0);
#endif
    
    GBufferOutput output;
    output.diffuseRT = float4(diffuse, ao);
    output.specularRT = float4(specular, 0);
    output.normalRT = float4(OctNormalEncode(N), roughness);
    output.emissiveRT = emissive;
    
    return output;
}
