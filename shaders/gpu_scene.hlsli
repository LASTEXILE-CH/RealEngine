#pragma once

#include "global_constants.hlsli"

enum class InstanceType : uint
{
    Model = 1 << 0,
};

struct InstanceData
{
    uint instanceType;
    uint indexBufferAddress;
    uint indexStride;
    uint triangleCount;

    uint meshletCount;
    uint meshletBufferAddress;
    uint meshletVerticesBufferAddress;
    uint meshletIndicesBufferAddress;

    uint posBufferAddress;
    uint uvBufferAddress;
    uint normalBufferAddress;
    uint tangentBufferAddress;
    
    uint bVertexAnimation;
    uint materialDataAddress;
    uint objectID;
    float scale;
    
    float3 center;
    float radius;
    
    float4x4 mtxWorld;
    float4x4 mtxWorldInverseTranspose;
    float4x4 mtxPrevWorld;

    InstanceType GetInstanceType()
    {
        return (InstanceType)instanceType;
    }
};

#ifndef __cplusplus

template<typename T>
T LoadSceneStaticBuffer(uint bufferAddress, uint element_id)
{
    ByteAddressBuffer sceneBuffer = ResourceDescriptorHeap[SceneCB.sceneStaticBufferSRV];
    return sceneBuffer.Load<T>(bufferAddress + sizeof(T) * element_id);
}

template<typename T>
T LoadSceneAnimationBuffer(uint bufferAddress, uint element_id)
{
    ByteAddressBuffer sceneBuffer = ResourceDescriptorHeap[SceneCB.sceneAnimationBufferSRV];
    return sceneBuffer.Load<T>(bufferAddress + sizeof(T) * element_id);
}

template<typename T>
void StoreSceneAnimationBuffer(uint bufferAddress, uint element_id, T value)
{
    RWByteAddressBuffer sceneBuffer = ResourceDescriptorHeap[SceneCB.sceneAnimationBufferUAV];
    sceneBuffer.Store<T>(bufferAddress + sizeof(T) * element_id, value);
}

template<typename T>
T LoadSceneConstantBuffer(uint bufferAddress)
{
    ByteAddressBuffer constantBuffer = ResourceDescriptorHeap[SceneCB.sceneConstantBufferSRV];
    return constantBuffer.Load<T>(bufferAddress);
}

InstanceData GetInstanceData(uint instance_id)
{
    return LoadSceneConstantBuffer<InstanceData>(SceneCB.instanceDataAddress + sizeof(InstanceData) * instance_id);
}

uint3 GetPrimitiveIndices(uint instance_id, uint primitive_id)
{
    InstanceData instanceData = GetInstanceData(instance_id);
    
    if (instanceData.indexStride == 2)
    {
        return LoadSceneStaticBuffer<uint16_t3>(instanceData.indexBufferAddress, primitive_id);
    }
    else
    {
        return LoadSceneStaticBuffer<uint3>(instanceData.indexBufferAddress, primitive_id);
    }
}

#endif //__cplusplus