#include "restir_gi.h"
#include "../renderer.h"
#include "utils/gui_util.h"

ReSTIRGI::ReSTIRGI(Renderer* pRenderer)
{
    m_pRenderer = pRenderer;

    GfxComputePipelineDesc desc;
    desc.cs = pRenderer->GetShader("restir_gi/ray_trace.hlsl", "main", "cs_6_6", {});
    m_pRaytracePSO = pRenderer->GetPipelineState(desc, "ReSTIR GI - raytrace PSO");
}

RenderGraphHandle ReSTIRGI::Render(RenderGraph* pRenderGraph, RenderGraphHandle depth, RenderGraphHandle normal, uint32_t width, uint32_t height)
{
    GUI("Lighting", "ReSTIR GI",
        [&]()
        {
            ImGui::Checkbox("Enable##ReSTIR GI", &m_bEnable);
        });

    if (!m_bEnable)
    {
        return RenderGraphHandle();
    }

    RENDER_GRAPH_EVENT(pRenderGraph, "ReSTIR GI");

    struct RaytracePassData
    {
        RenderGraphHandle depth;
        RenderGraphHandle normal;
        RenderGraphHandle output;
    };

    auto raytrace_pass = pRenderGraph->AddPass<RaytracePassData>("ReSTIR GI - raytrace", RenderPassType::Compute,
        [&](RaytracePassData& data, RenderGraphBuilder& builder)
        {
            data.depth = builder.Read(depth);
            data.normal = builder.Read(normal);

            RenderGraphTexture::Desc desc;
            desc.width = width;
            desc.height = height;
            desc.format = GfxFormat::RGBA16F;
            data.output = builder.Create<RenderGraphTexture>(desc, "ReSTIR GI raw output");
            data.output = builder.Write(data.output);
        },
        [=](const RaytracePassData& data, IGfxCommandList* pCommandList)
        {
            RenderGraphTexture* depth = (RenderGraphTexture*)pRenderGraph->GetResource(data.depth);
            RenderGraphTexture* normal = (RenderGraphTexture*)pRenderGraph->GetResource(data.normal);
            RenderGraphTexture* output = (RenderGraphTexture*)pRenderGraph->GetResource(data.output);
            Raytrace(pCommandList, depth->GetSRV(), normal->GetSRV(), output->GetUAV(), width, height);
        });

    return raytrace_pass->output;
}

void ReSTIRGI::Raytrace(IGfxCommandList* pCommandList, IGfxDescriptor* depth, IGfxDescriptor* normal, IGfxDescriptor* output, uint32_t width, uint32_t height)
{
    pCommandList->SetPipelineState(m_pRaytracePSO);

    uint32_t constants[3] = { depth->GetHeapIndex(), normal->GetHeapIndex(), output->GetHeapIndex() };
    pCommandList->SetComputeConstants(0, constants, sizeof(constants));

    pCommandList->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
}
