#include "debug.hlsli"

struct VSOutput
{
    float4 position : SV_Position;
    float4 color : COLOR;
};

VSOutput vs_main(uint vertex_id : SV_VertexID)
{
    StructuredBuffer<DebugLineVertex> vertexBuffer = ResourceDescriptorHeap[SceneCB.debugLineVertexBufferSRV];
    
    VSOutput output;
    output.position = mul(CameraCB.mtxViewProjectionNoJitter, float4(vertexBuffer[vertex_id].position, 1.0));
    output.color = RGBA8UnormToFloat4(vertexBuffer[vertex_id].color);

    return output;
}

float4 ps_main(VSOutput input) : SV_Target
{
    return input.color;
}