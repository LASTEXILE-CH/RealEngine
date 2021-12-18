#include "model.hlsli"
#include "debug.hlsli"
#include "stats.hlsli"

cbuffer RootConstants : register(b0)
{
    uint c_meshletCount;
    uint c_meshletBuffer;
    uint c_meshletVerticesBuffer;
    uint c_meshletIndicesBuffer;
};

struct Meshlet
{
    float3 center;
    float radius;
    
    uint cone; //axis + cutoff, rgba8snorm
    
    uint vertexCount;
    uint triangleCount;
    
    uint vertexOffset;
    uint triangleOffset;
};

struct Payload
{
    uint meshletIndices[32];
};

groupshared Payload s_Payload;


bool Cull(Meshlet meshlet)
{
    // 1. frustum culling
    float3 center = mul(ModelCB.mtxWorld, float4(meshlet.center, 1.0)).xyz;
    float radius = meshlet.radius * ModelCB.scale;
    
    for (uint i = 0; i < 6; ++i)
    {
        if (dot(center, CameraCB.culling.planes[i].xyz) + CameraCB.culling.planes[i].w + radius < 0)
        {
            stats(STATS_FRUSTUM_CULLED_MESHLET, 1);
            return false;
        }
    }
    
    // 2. backface culling
    int16_t4 cone = unpack_s8s16((int8_t4_packed)meshlet.cone);
    float3 axis = cone.xyz / 127.0;
    float cutoff = cone.w / 127.0;
    
    axis = normalize(mul(ModelCB.mtxWorld, float4(axis, 0.0)).xyz);
    float3 view = center - CameraCB.culling.viewPos;
    
    if (dot(view, -axis) >= cutoff * length(view) + radius)
    {
        stats(STATS_BACKFACE_CULLED_MESHLET, 1);
        return false;
    }
    
    // 3. occlusion culling    
    Texture2D<float> hzbTexture = ResourceDescriptorHeap[SceneCB.reprojectedHZBSRV];
    uint2 hzbSize = uint2(SceneCB.reprojectedHZBWidth, SceneCB.reprojectedHZBHeight);
    
    if (!OcclusionCull(hzbTexture, hzbSize, center, radius))
    {
        //debug::DrawSphere(center, radius, float3(1, 0, 0));
        stats(STATS_OCCLUSION_CULLED_MESHLET, 1);
        return false;
    }

    return true;
}

[numthreads(32, 1, 1)]
void main_as(uint dispatchThreadID : SV_DispatchThreadID)
{
    bool visible = false;
    uint meshletIndex = dispatchThreadID;
    
    if (meshletIndex < c_meshletCount)
    {
        StructuredBuffer<Meshlet> meshletBuffer = ResourceDescriptorHeap[c_meshletBuffer];
        Meshlet meshlet = meshletBuffer[meshletIndex];
        
        visible = Cull(meshlet);
        
        stats(visible ? STATS_RENDERED_TRIANGLE : STATS_CULLED_TRIANGLE, meshlet.triangleCount);
    }
    
    if (visible)
    {
        uint index = WavePrefixCountBits(visible);
        s_Payload.meshletIndices[index] = meshletIndex;
    }

    uint visibleMeshletCount = WaveActiveCountBits(visible);
    DispatchMesh(visibleMeshletCount, 1, 1, s_Payload);
}

[numthreads(128, 1, 1)]
[outputtopology("triangle")]
void main_ms(
    uint groupThreadID : SV_GroupThreadID,
    uint groupID : SV_GroupID,
    in payload Payload payload,
    out indices uint3 indices[124],
    out vertices VertexOut vertices[64])
{
    uint meshletIndex = payload.meshletIndices[groupID];
    if(meshletIndex >= c_meshletCount)
    {
        return;
    }
    
    StructuredBuffer<Meshlet> meshletBuffer = ResourceDescriptorHeap[c_meshletBuffer];
    Meshlet meshlet = meshletBuffer[meshletIndex];
    
    SetMeshOutputCounts(meshlet.vertexCount, meshlet.triangleCount);
    
    if(groupThreadID < meshlet.triangleCount)
    {
        StructuredBuffer<uint16_t> meshletIndicesBuffer = ResourceDescriptorHeap[c_meshletIndicesBuffer];
        uint3 index = uint3(
            meshletIndicesBuffer[meshlet.triangleOffset + groupThreadID * 3],
            meshletIndicesBuffer[meshlet.triangleOffset + groupThreadID * 3 + 1],
            meshletIndicesBuffer[meshlet.triangleOffset + groupThreadID * 3 + 2]);
        
        indices[groupThreadID] = index;
    }
    
    if(groupThreadID < meshlet.vertexCount)
    {
        StructuredBuffer<uint> meshletVerticesBuffer = ResourceDescriptorHeap[c_meshletVerticesBuffer];
        uint vertex_id = meshletVerticesBuffer[meshlet.vertexOffset + groupThreadID];

        VertexOut v = GetVertex(vertex_id);
        v.meshlet = meshletIndex;
        
        vertices[groupThreadID] = v;
    }
}
