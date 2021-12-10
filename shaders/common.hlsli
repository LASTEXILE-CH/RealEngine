#pragma once

#include "global_constants.hlsli"

static const float M_PI = 3.141592653f;

float3 DiffuseBRDF(float3 diffuse)
{
    return diffuse; // / M_PI;
}

float3 D_GGX(float3 N, float3 H, float a)
{
    float a2 = a * a;
    float NdotH = saturate(dot(N, H));
    
    float denom = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    denom = /*M_PI */denom * denom;
    
    return a2 * rcp(denom);
}

//http://graphicrants.blogspot.com/2013/08/specular-brdf-reference.html
float3 V_SmithGGX(float3 N, float3 V, float3 L, float a)
{
    float a2 = a * a;
    float NdotV = saturate(dot(N, V));
    float NdotL = saturate(dot(N, L));

    float G_V = NdotV + sqrt((NdotV - NdotV * a2) * NdotV + a2);
    float G_L = NdotL + sqrt((NdotL - NdotL * a2) * NdotL + a2);
    return rcp(G_V * G_L);
}

float3 F_Schlick(float3 V, float3 H, float3 F0)
{
    float VdotH = saturate(dot(V, H));
    return F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
}

float3 SpecularBRDF(float3 N, float3 V, float3 L, float3 specular, float roughness, out float3 F)
{
    roughness = max(roughness, 0.03);

    float a = roughness * roughness;
    float3 H = normalize(V + L);

    float3 D = D_GGX(N, H, a);
    float3 Vis = V_SmithGGX(N, V, L, a);
    F = F_Schlick(V, H, specular);

    return D * Vis * F;
}

float3 BRDF(float3 L, float3 V, float3 N, float3 diffuse, float3 specular, float roughness)
{
    float3 F;
    float3 specular_brdf = SpecularBRDF(N, V, L, specular, roughness, F);
    float3 diffuse_brdf = DiffuseBRDF(diffuse) * (1.0 - F);    
    
    float NdotL = saturate(dot(N, L));

    return (diffuse_brdf + specular_brdf) * NdotL;
}

template<typename T>
T LinearToSrgbChannel(T lin)
{
    if (lin < 0.00313067)
        return lin * 12.92;
    return pow(lin, (1.0 / 2.4)) * 1.055 - 0.055;
}

template<typename T>
T LinearToSrgb(T lin)
{
    return T(LinearToSrgbChannel(lin.r), LinearToSrgbChannel(lin.g), LinearToSrgbChannel(lin.b));
}

template<typename T>
T SrgbToLinear(T Color)
{
    Color = max(6.10352e-5, Color);
    //return Color > 0.04045 ? pow(Color * (1.0 / 1.055) + 0.0521327, 2.4) : Color * (1.0 / 12.92);
    return lerp(Color * (1.0 / 12.92), pow(Color * (1.0 / 1.055) + 0.0521327, 2.4), Color > 0.04045);
}

//pack float2[0,1] in rgb8unorm, each float is 12 bits
float3 EncodeRGB8Unorm(float2 v)
{
    uint2 int12 = (uint2)round(v * 4095);
    uint3 int8 = uint3(int12.x & 0xFF, int12.y & 0xFF, ((int12.x >> 4) & 0xF0) | ((int12.y >> 8) & 0xF));
    return int8 / 255.0;
}

float2 DecodeRGB8Unorm(float3 v)
{
    uint3 int8 = (uint3)round(v * 255.0);
    uint2 int12 = uint2(int8.x | ((int8.z & 0xF0) << 4), int8.y | ((int8.z & 0xF) << 8));
    return int12 / 4095.0;
}
 
float3 OctNormalEncode(float3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    //n.xy = n.z >= 0.0 ? n.xy : (1.0 - abs(n.yx)) * (n.xy >= 0.0 ? 1.0 : -1.0);
    n.xy = n.z >= 0.0 ? n.xy : (1.0 - abs(n.yx)) * lerp(-1.0, 1.0, n.xy >= 0.0);
    
    n.xy = n.xy * 0.5 + 0.5;
    return EncodeRGB8Unorm(n.xy);
}
 
// https://twitter.com/Stubbesaurus/status/937994790553227264
float3 OctNormalDecode(float3 f)
{
    float2 e = DecodeRGB8Unorm(f);
    e = e * 2.0 - 1.0;
 
    float3 n = float3(e.x, e.y, 1.0 - abs(e.x) - abs(e.y));
    float t = saturate(-n.z);
    //n.xy += n.xy >= 0.0 ? -t : t;
    n.xy += lerp(t, -t, n.xy >= 0.0);
    return normalize(n);
}

float GetLinearDepth(float depth)
{
    return 1.0f / (depth * CameraCB.linearZParams.x - CameraCB.linearZParams.y);
}

float3 GetWorldPosition(uint2 screenPos, float depth)
{
    float2 screenUV = ((float2) screenPos + 0.5) * float2(SceneCB.rcpViewWidth, SceneCB.rcpViewHeight);
    float4 clipPos = float4((screenUV * 2.0 - 1.0) * float2(1.0, -1.0), depth, 1.0);
    float4 worldPos = mul(CameraCB.mtxViewProjectionInverse, clipPos);
    worldPos.xyz /= worldPos.w;
    
    return worldPos.xyz;
}

float3 GetNdcPos(float4 clipPos)
{
    return clipPos.xyz / clipPos.w;
}

//[-1, 1] -> [0, 1]
float2 GetScreenUV(float2 ndcPos)
{
    return ndcPos * float2(0.5, -0.5) + 0.5;
}

//[-1, 1] -> [0, width/height]
float2 GetScreenPosition(float2 ndcPos)
{
    return GetScreenUV(ndcPos) * float2(SceneCB.viewWidth, SceneCB.viewHeight);
}

//[0, width/height] -> [-1, 1]
float2 GetNdcPosition(float2 screenPos)
{
    float2 screenUV = screenPos * float2(SceneCB.rcpViewWidth, SceneCB.rcpViewHeight);
    return (screenUV * 2.0 - 1.0) * float2(1.0, -1.0);
}

float4 RGBA8UnormToFloat4(uint packed)
{
    //uint16_t4 unpacked = unpack_u8u16((uint8_t4_packed)packed);
    uint32_t4 unpacked = unpack_u8u32((uint8_t4_packed)packed);

    return unpacked / 255.0f;
}

uint Float4ToRGBA8Unorm(float4 input)
{
    //uint16_t4 unpacked = uint16_t4(input * 255.0 + 0.5);
    uint32_t4 unpacked = uint32_t4(input * 255.0 + 0.5);
    
    return (uint)pack_u8(unpacked);
}