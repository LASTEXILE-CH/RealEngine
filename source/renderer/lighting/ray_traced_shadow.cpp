#include "ray_traced_shadow.h"
#include "../renderer.h"
#include "utils/gui_util.h"

RTShadow::RTShadow(Renderer* pRenderer)
{
    m_pRenderer = pRenderer;
    m_pDenoiser = eastl::make_unique<ShadowDenoiser>(pRenderer);

    GfxComputePipelineDesc psoDesc;
    psoDesc.cs = pRenderer->GetShader("ray_traced_shadow/ray_trace.hlsl", "main", "cs_6_6", {});
    m_pRaytracePSO = pRenderer->GetPipelineState(psoDesc, "RTShadow PSO");
}

RenderGraphHandle RTShadow::Render(RenderGraph* pRenderGraph, RenderGraphHandle depthRT, RenderGraphHandle normalRT, RenderGraphHandle velocityRT, uint32_t width, uint32_t height)
{
    GUI("Lighting", "Shadow", [&]()
        {
            ImGui::Checkbox("Enable Denoiser##RTShadow", &m_bEnableDenoiser);
        });

    RENDER_GRAPH_EVENT(pRenderGraph, "RTShadow");

    struct RTShadowData
    {
        RenderGraphHandle depth;
        RenderGraphHandle normal;
        RenderGraphHandle shadow;
    };

    auto rtshadow_pass = pRenderGraph->AddPass<RTShadowData>("RTShadow raytrace", RenderPassType::Compute,
        [&](RTShadowData& data, RenderGraphBuilder& builder)
        {
            data.depth = builder.Read(depthRT);
            data.normal = builder.Read(normalRT);

            RenderGraphTexture::Desc desc;
            desc.width = width;
            desc.height = height;
            desc.format = GfxFormat::R8UNORM;
            desc.usage = GfxTextureUsageUnorderedAccess;
            data.shadow = builder.Create<RenderGraphTexture>(desc, "RTShadow raytraced shadow");
            data.shadow = builder.Write(data.shadow);
        },
        [=](const RTShadowData& data, IGfxCommandList* pCommandList)
        {
            RenderGraphTexture* depthRT = (RenderGraphTexture*)pRenderGraph->GetResource(data.depth);
            RenderGraphTexture* normalRT = (RenderGraphTexture*)pRenderGraph->GetResource(data.normal);
            RenderGraphTexture* shadowRT = (RenderGraphTexture*)pRenderGraph->GetResource(data.shadow);

            RayTrace(pCommandList, depthRT->GetSRV(), normalRT->GetSRV(), shadowRT->GetUAV(), width, height);
        });

    if (!m_bEnableDenoiser)
    {
        return rtshadow_pass->shadow;
    }
    
    return m_pDenoiser->Render(pRenderGraph, rtshadow_pass->shadow, depthRT, normalRT, velocityRT, width, height);
}

void RTShadow::RayTrace(IGfxCommandList* pCommandList, IGfxDescriptor* depthSRV, IGfxDescriptor* normalSRV, IGfxDescriptor* shadowUAV, uint32_t width, uint32_t height)
{
    pCommandList->SetPipelineState(m_pRaytracePSO);

    uint constants[3] = { depthSRV->GetHeapIndex(), normalSRV->GetHeapIndex(), shadowUAV->GetHeapIndex() };
    pCommandList->SetComputeConstants(0, constants, sizeof(constants));
    pCommandList->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
}

