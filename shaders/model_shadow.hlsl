#include "common.hlsli"
#include "model_constants.hlsli"

cbuffer CB : register(b0)
{
    uint c_posBuffer;
    uint c_uvBuffer;
    uint c_albedoTexture;
    uint c_linearSampler;
};

struct VSOutput
{
    float4 pos : SV_POSITION;
#if ALBEDO_TEXTURE && ALPHA_TEST
    float2 uv : TEXCOORD;
#endif
};

VSOutput vs_main(uint vertex_id : SV_VertexID)
{
    StructuredBuffer<float3> posBuffer = ResourceDescriptorHeap[c_posBuffer];
    StructuredBuffer<float2> uvBuffer = ResourceDescriptorHeap[c_uvBuffer];
    
    float4 pos = float4(posBuffer[vertex_id], 1.0);
    
    VSOutput output;
    output.pos = mul(ModelCB.mtxWVP, pos);

#if ALBEDO_TEXTURE && ALPHA_TEST
    output.uv = uvBuffer[vertex_id];
#endif
    
    return output;
}