#include "gpu_driven_stats.h"
#include "renderer.h"
#include "stats.hlsli"

GpuDrivenStats::GpuDrivenStats(Renderer* pRenderer)
{
    m_pRenderer = pRenderer;

    GfxComputePipelineDesc desc;
    desc.cs = pRenderer->GetShader("stats.hlsl", "main", "cs_6_6", {});
    m_pPSO = pRenderer->GetPipelineState(desc, "GpuDrivenStats::m_pPSO");

    m_pStatsBuffer.reset(pRenderer->CreateTypedBuffer(nullptr, GfxFormat::R32UI, STATS_MAX_TYPE_COUNT, "GpuDrivenStats::m_pStatsBuffer", GfxMemoryType::GpuOnly, true));
}

void GpuDrivenStats::Clear(IGfxCommandList* pCommandList)
{
    GPU_EVENT(pCommandList, "GpuDrivenStats clear");

    pCommandList->ResourceBarrier(m_pStatsBuffer->GetBuffer(), 0, GfxResourceState::ShaderResourceNonPS, GfxResourceState::UnorderedAccess);

    uint32_t clear_value[4] = { 0, 0, 0, 0 };
    pCommandList->ClearUAV(m_pStatsBuffer->GetBuffer(), m_pStatsBuffer->GetUAV(), clear_value);
    pCommandList->UavBarrier(m_pStatsBuffer->GetBuffer());
}

void GpuDrivenStats::Draw(IGfxCommandList* pCommandList)
{
    GPU_EVENT(pCommandList, "GpuDrivenStats");

    pCommandList->ResourceBarrier(m_pStatsBuffer->GetBuffer(), 0, GfxResourceState::UnorderedAccess, GfxResourceState::ShaderResourceNonPS);

    uint32_t root_constants[1] = { m_pStatsBuffer->GetSRV()->GetHeapIndex() };

    pCommandList->SetPipelineState(m_pPSO);
    pCommandList->SetComputeConstants(0, root_constants, sizeof(root_constants));
    pCommandList->Dispatch(1, 1, 1);
}
