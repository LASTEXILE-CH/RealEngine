#include "lighting_processor.h"
#include "../renderer.h"

LightingProcessor::LightingProcessor(Renderer* pRenderer)
{
    m_pRenderer = pRenderer;

    m_pGTAO = std::make_unique<GTAO>(pRenderer);
    m_pRTShdow = std::make_unique<RTShadow>(pRenderer);
    m_pClusteredShading = std::make_unique<ClusteredShading>(pRenderer);
}

RenderGraphHandle LightingProcessor::Process(RenderGraph* pRenderGraph, const LightingProcessInput& input, uint32_t width, uint32_t height)
{
    RENDER_GRAPH_EVENT(pRenderGraph, "Lighting");

    RenderGraphHandle gtao = m_pGTAO->Render(pRenderGraph, input.depthRT, input.normalRT, width, height);
    RenderGraphHandle shadow = m_pRTShdow->Render(pRenderGraph, input.depthRT, input.normalRT, width, height);

    return CompositeLight(pRenderGraph, input, gtao, shadow, width, height);
}

RenderGraphHandle LightingProcessor::CompositeLight(RenderGraph* pRenderGraph, const LightingProcessInput& input, RenderGraphHandle ao, RenderGraphHandle shadow, uint32_t width, uint32_t height)
{
    struct CompositeLightData
    {
        LightingProcessInput input;
        RenderGraphHandle ao;
        RenderGraphHandle shadow;

        RenderGraphHandle output;
    };

    auto pass = pRenderGraph->AddPass<CompositeLightData>("CompositeLight",
        [&](CompositeLightData& data, RenderGraphBuilder& builder)
        {
            data.input.diffuseRT = builder.Read(input.diffuseRT, GfxResourceState::ShaderResourceNonPS);
            data.input.specularRT = builder.Read(input.specularRT, GfxResourceState::ShaderResourceNonPS);
            data.input.normalRT = builder.Read(input.normalRT, GfxResourceState::ShaderResourceNonPS);
            data.input.emissiveRT = builder.Read(input.emissiveRT, GfxResourceState::ShaderResourceNonPS);
            data.input.depthRT = builder.Read(input.depthRT, GfxResourceState::ShaderResourceNonPS);

            if (ao.IsValid())
            {
                data.ao = builder.Read(ao, GfxResourceState::ShaderResourceNonPS);
            }

            data.shadow = builder.Read(shadow, GfxResourceState::ShaderResourceNonPS);

            RenderGraphTexture::Desc desc;
            desc.width = width;
            desc.height = height;
            desc.format = GfxFormat::RGBA16F;
            desc.usage = GfxTextureUsageUnorderedAccess | GfxTextureUsageRenderTarget;
            data.output = builder.Create<RenderGraphTexture>(desc, "SceneColor RT");
            data.output = builder.Write(data.output, GfxResourceState::UnorderedAccess);
        },
        [=](const CompositeLightData& data, IGfxCommandList* pCommandList)
        {
            RenderGraphTexture* diffuseRT = (RenderGraphTexture*)pRenderGraph->GetResource(data.input.diffuseRT);
            RenderGraphTexture* specularRT = (RenderGraphTexture*)pRenderGraph->GetResource(data.input.specularRT);
            RenderGraphTexture* normalRT = (RenderGraphTexture*)pRenderGraph->GetResource(data.input.normalRT);
            RenderGraphTexture* emissiveRT = (RenderGraphTexture*)pRenderGraph->GetResource(data.input.emissiveRT);
            RenderGraphTexture* depthRT = (RenderGraphTexture*)pRenderGraph->GetResource(data.input.depthRT);
            RenderGraphTexture* shadowRT = (RenderGraphTexture*)pRenderGraph->GetResource(data.shadow);
            RenderGraphTexture* ouputRT = (RenderGraphTexture*)pRenderGraph->GetResource(data.output);

            std::vector<std::string> defines;
            if (data.ao.IsValid())
            {
                defines.push_back("GTAO=1");
            }

            switch (m_pRenderer->GetOutputType())
            {
            case RendererOutput::Default:
                defines.push_back("OUTPUT_DEFAULT=1");
                break;
            case RendererOutput::Diffuse:
                defines.push_back("OUTPUT_DIFFUSE=1");
                break;
            case RendererOutput::Specular:
                defines.push_back("OUTPUT_SPECULAR=1");
                break;
            case RendererOutput::WorldNormal:
                defines.push_back("OUTPUT_WORLDNORMAL=1");
                break;
            case RendererOutput::Emissive:
                defines.push_back("OUTPUT_EMISSIVE=1");
                break;
            case RendererOutput::AO:
                defines.push_back("OUTPUT_AO=1");
                break;
            case RendererOutput::Shadow:
                defines.push_back("OUTPUT_SHADOW=1");
                break;
            default:
                break;
            }

            GfxComputePipelineDesc psoDesc;
            psoDesc.cs = m_pRenderer->GetShader("composite_light.hlsl", "main", "cs_6_6", defines);
            IGfxPipelineState* pso = m_pRenderer->GetPipelineState(psoDesc, "CompositeLight PSO");

            pCommandList->SetPipelineState(pso);

            struct CB1
            {
                uint diffuseRT;
                uint specularRT;
                uint normalRT;
                uint emissiveRT;

                uint depthRT;
                uint shadowRT;
                uint aoRT;
                uint outputRT;
            };

            CB1 cb1;
            cb1.diffuseRT = diffuseRT->GetSRV()->GetHeapIndex();
            cb1.specularRT = specularRT->GetSRV()->GetHeapIndex();
            cb1.normalRT = normalRT->GetSRV()->GetHeapIndex();
            cb1.emissiveRT = emissiveRT->GetSRV()->GetHeapIndex();
            cb1.depthRT = depthRT->GetSRV()->GetHeapIndex();
            cb1.shadowRT = shadowRT->GetSRV()->GetHeapIndex();
            if (data.ao.IsValid())
            {
                RenderGraphTexture* aoRT = (RenderGraphTexture*)pRenderGraph->GetResource(data.ao);
                cb1.aoRT = aoRT->GetSRV()->GetHeapIndex();
            }
            cb1.outputRT = ouputRT->GetUAV()->GetHeapIndex();

            pCommandList->SetComputeConstants(1, &cb1, sizeof(cb1));

            pCommandList->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
        });

    return pass->output;
}
