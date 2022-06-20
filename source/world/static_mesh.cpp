#include "static_mesh.h"
#include "mesh_material.h"
#include "resource_cache.h"
#include "core/engine.h"
#include "utils/gui_util.h"

StaticMesh::StaticMesh(const eastl::string& name)
{
    m_name = name;
}

StaticMesh::~StaticMesh()
{
    ResourceCache* cache = ResourceCache::GetInstance();

    cache->RelaseSceneBuffer(m_posBufferAddress);
    cache->RelaseSceneBuffer(m_uvBufferAddress);
    cache->RelaseSceneBuffer(m_normalBufferAddress);
    cache->RelaseSceneBuffer(m_tangentBufferAddress);

    cache->RelaseSceneBuffer(m_meshletBufferAddress);
    cache->RelaseSceneBuffer(m_meshletVerticesBufferAddress);
    cache->RelaseSceneBuffer(m_meshletIndicesBufferAddress);

    cache->RelaseSceneBuffer(m_indexBufferAddress);
}

bool StaticMesh::Create()
{
    //todo : need to cache blas for same models

    GfxRayTracingGeometry geometry;
    geometry.vertex_buffer = m_pRenderer->GetSceneStaticBuffer();
    geometry.vertex_buffer_offset = m_posBufferAddress;
    geometry.vertex_count = m_nVertexCount;
    geometry.vertex_stride = sizeof(float3);
    geometry.vertex_format = GfxFormat::RGB32F;
    geometry.index_buffer = m_pRenderer->GetSceneStaticBuffer();
    geometry.index_buffer_offset = m_indexBufferAddress;
    geometry.index_count = m_nIndexCount;
    geometry.index_format = m_indexBufferFormat;
    geometry.opaque = m_pMaterial->IsAlphaTest() ? false : true; //todo : alpha blend

    GfxRayTracingBLASDesc desc;
    desc.geometries.push_back(geometry);
    desc.flags = GfxRayTracingASFlagAllowCompaction | GfxRayTracingASFlagPreferFastTrace;

    IGfxDevice* device = m_pRenderer->GetDevice();
    m_pBLAS.reset(device->CreateRayTracingBLAS(desc, "BLAS : " + m_name));
    m_pRenderer->BuildRayTracingBLAS(m_pBLAS.get());

    return true;
}

void StaticMesh::Tick(float delta_time)
{
    if (m_pMaterial->IsAlphaBlend())
    {
        return; //todo
    }

    UpdateConstants();

    GfxRayTracingInstanceFlag flags = m_pMaterial->IsFrontFaceCCW() ? GfxRayTracingInstanceFlagFrontFaceCCW : 0;
    m_nInstanceIndex = m_pRenderer->AddInstance(m_instanceData, m_pBLAS.get(), flags);
}

void StaticMesh::UpdateConstants()
{
    m_pMaterial->UpdateConstants();

    m_instanceData.instanceType = (uint)InstanceType::Model;
    m_instanceData.indexBufferAddress = m_indexBufferAddress;
    m_instanceData.indexStride = m_indexBufferFormat == GfxFormat::R32UI ? 4 : 2;
    m_instanceData.triangleCount = m_nIndexCount / 3;

    m_instanceData.meshletCount = m_nMeshletCount;
    m_instanceData.meshletBufferAddress = m_meshletBufferAddress;
    m_instanceData.meshletVerticesBufferAddress = m_meshletVerticesBufferAddress;
    m_instanceData.meshletIndicesBufferAddress = m_meshletIndicesBufferAddress;

    m_instanceData.posBufferAddress = m_posBufferAddress;
    m_instanceData.uvBufferAddress = m_uvBufferAddress;
    m_instanceData.normalBufferAddress = m_normalBufferAddress;
    m_instanceData.tangentBufferAddress = m_tangentBufferAddress;

    m_instanceData.bVertexAnimation = false;
    m_instanceData.materialDataAddress = m_pRenderer->AllocateSceneConstant((void*)m_pMaterial->GetConstants(), sizeof(ModelMaterialConstant));
    m_instanceData.objectID = m_nID;
    m_instanceData.scale = max(max(abs(m_scale.x), abs(m_scale.y)), abs(m_scale.z));

    float4x4 T = translation_matrix(m_pos);
    float4x4 R = rotation_matrix(rotation_quat(m_rotation));
    float4x4 S = scaling_matrix(m_scale);
    float4x4 mtxWorld = mul(T, mul(R, S));

    m_instanceData.center = mul(mtxWorld, float4(m_center, 1.0)).xyz();
    m_instanceData.radius = m_radius * m_instanceData.scale;

    m_instanceData.mtxPrevWorld = m_instanceData.mtxWorld;
    m_instanceData.mtxWorld = mtxWorld;
    m_instanceData.mtxWorldInverseTranspose = transpose(inverse(mtxWorld));
}

void StaticMesh::Render(Renderer* pRenderer)
{
    if (m_pMaterial->IsAlphaBlend())
    {
        return; //todo
    }

    RenderBatch& bassPassBatch = pRenderer->AddBasePassBatch();
#if 1
    Dispatch(bassPassBatch, m_pMaterial->GetMeshletPSO());
#else
    Draw(bassPassBatch, m_pMaterial->GetPSO());
#endif

    if (!nearly_equal(m_instanceData.mtxPrevWorld, m_instanceData.mtxWorld))
    {
        RenderBatch& velocityPassBatch = pRenderer->AddVelocityPassBatch();
        Draw(velocityPassBatch, m_pMaterial->GetVelocityPSO());
    }

    if (pRenderer->IsEnableMouseHitTest())
    {
        RenderBatch& idPassBatch = pRenderer->AddObjectIDPassBatch();
        Draw(idPassBatch, m_pMaterial->GetIDPSO());
    }

    if (m_nID == pRenderer->GetMouseHitObjectID())
    {
        RenderBatch& outlinePassBatch = pRenderer->AddForwardPassBatch();
        Draw(outlinePassBatch, m_pMaterial->GetOutlinePSO());
    }
}

bool StaticMesh::FrustumCull(const float4* planes, uint32_t plane_count) const
{
    return ::FrustumCull(planes, plane_count, m_instanceData.center, m_instanceData.radius);
}

void StaticMesh::Draw(RenderBatch& batch, IGfxPipelineState* pso)
{
    uint32_t root_consts[1] = { m_nInstanceIndex };

    batch.label = m_name.c_str();
    batch.SetPipelineState(pso);
    batch.SetConstantBuffer(0, root_consts, sizeof(root_consts));

    batch.SetIndexBuffer(m_pRenderer->GetSceneStaticBuffer(), m_indexBufferAddress, m_indexBufferFormat);
    batch.DrawIndexed(m_nIndexCount);
}

void StaticMesh::Dispatch(RenderBatch& batch, IGfxPipelineState* pso)
{
    batch.label = m_name.c_str();
    batch.SetPipelineState(pso);
    batch.center = m_instanceData.center;
    batch.radius = m_instanceData.radius;
    batch.meshletCount = m_nMeshletCount;
    batch.instanceIndex = m_nInstanceIndex;
}

void StaticMesh::OnGui()
{
    IVisibleObject::OnGui();

    GUI("Inspector", "StaticMesh", []()
        {
            ImGui::Text("TODO");
        });
}