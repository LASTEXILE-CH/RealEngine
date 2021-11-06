#include "global_constants.hlsli"

cbuffer CB : register(b1)
{
    uint c_posBuffer;
    uint c_prevPosBuffer;
    uint c_albedoTexture;
    float c_alphaCutoff;

    float4x4 c_mtxWVP;
    float4x4 c_mtxWVPNoJitter;
    float4x4 c_mtxPrevWVPNoJitter;
};

struct VSOutput
{
    float4 pos : SV_POSITION;
    
    float4 clipPos : CLIP_POSITION0;
    float4 prevClipPos : CLIP_POSITION1;
};

VSOutput vs_main(uint vertex_id : SV_VertexID)
{
    StructuredBuffer<float3> posBuffer = ResourceDescriptorHeap[c_posBuffer];
    
    float4 pos = float4(posBuffer[vertex_id], 1.0);

    VSOutput output;
    output.pos = mul(c_mtxWVP, pos);
    
    float4 prevPos = pos;
#if ANIME_POS
    StructuredBuffer<float3> prevPosBuffer = ResourceDescriptorHeap[c_prevPosBuffer];
    prevPos = float4(prevPosBuffer[vertex_id], 1.0);
#endif
    
    output.clipPos = mul(c_mtxWVPNoJitter, pos);
    output.prevClipPos = mul(c_mtxPrevWVPNoJitter, prevPos);
    
    return output;
}

float4 ps_main(VSOutput input) : SV_TARGET0
{    
#if ALBEDO_TEXTURE && ALPHA_TEST
    SamplerState linearSampler = SamplerDescriptorHeap[SceneCB.linearRepeatSampler];
    Texture2D albedoTexture = ResourceDescriptorHeap[c_albedoTexture];
	float4 albedo = albedoTexture.Sample(linearSampler, input.uv);
    
    clip(albedo.a - c_alphaCutoff);
#endif
    
    float2 clipPos = input.clipPos.xy / input.clipPos.w;
    float2 screenUV = clipPos.xy * float2(0.5, -0.5) + 0.5;
    float2 screenPos = screenUV * float2(SceneCB.viewWidth, SceneCB.viewHeight);
    
    float2 prevClipPos = input.prevClipPos.xy / input.prevClipPos.w;
    float2 prevScreenUV = prevClipPos.xy * float2(0.5, -0.5) + 0.5;
    float2 prevScreenPos = prevScreenUV * float2(SceneCB.viewWidth, SceneCB.viewHeight);
    
    float2 motion = prevScreenPos.xy - screenPos.xy;
    
    float linearZ = input.clipPos.w;
    float prevLinearZ = input.prevClipPos.w;    
    float deltaZ = prevLinearZ - linearZ;
    
    return float4(motion, deltaZ, 1);
}