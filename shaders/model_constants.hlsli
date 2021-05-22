#pragma once

struct ModelConstant
{
    float4x4 mtxWVP;
    float4x4 mtxWorld;
    float4x4 mtxNormal;
};

struct MaterialConstant
{
    uint linearSampler;
    uint albedoTexture;
    uint metallicRoughnessTexture;
    uint normalTexture;
    
    float3 albedo;
    float metallic;
    float roughness;
    float alphaCutoff;
};

#ifndef __cplusplus
ConstantBuffer<ModelConstant> ModelCB : register(b1);
ConstantBuffer<MaterialConstant> MaterialCB : register(b2);
#endif