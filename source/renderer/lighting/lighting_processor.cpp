#include "lighting_processor.h"
#include "../renderer.h"
#include "../base_pass.h"

LightingProcessor::LightingProcessor(Renderer* pRenderer)
{
    m_pRenderer = pRenderer;

    m_pGTAO = eastl::make_unique<GTAO>(pRenderer);
    m_pRTShdow = eastl::make_unique<RTShadow>(pRenderer);
    m_pClusteredShading = eastl::make_unique<ClusteredShading>(pRenderer);
    m_pReflection = eastl::make_unique<HybridStochasticReflection>(pRenderer);
    m_pReSTIRGI = eastl::make_unique<ReSTIRGI>(pRenderer);
}

RenderGraphHandle LightingProcessor::Render(RenderGraph* pRenderGraph, RenderGraphHandle depth, RenderGraphHandle linear_depth, RenderGraphHandle velocity, uint32_t width, uint32_t height)
{
    RENDER_GRAPH_EVENT(pRenderGraph, "Lighting");

    BasePass* pBasePass = m_pRenderer->GetBassPass();
    RenderGraphHandle diffuse = pBasePass->GetDiffuseRT();
    RenderGraphHandle specular = pBasePass->GetSpecularRT();
    RenderGraphHandle normal = pBasePass->GetNormalRT();
    RenderGraphHandle emissive = pBasePass->GetEmissiveRT();
    RenderGraphHandle customData = pBasePass->GetCustomDataRT();

    RenderGraphHandle gtao = m_pGTAO->Render(pRenderGraph, depth, normal, width, height);
    RenderGraphHandle shadow = m_pRTShdow->Render(pRenderGraph, depth, normal, velocity, width, height);
    RenderGraphHandle direct_lighting = m_pClusteredShading->Render(pRenderGraph, diffuse, specular, normal, customData, depth, shadow, width, height);
    RenderGraphHandle indirect_specular = m_pReflection->Render(pRenderGraph, depth, linear_depth, normal, velocity, width, height);
    RenderGraphHandle indirect_diffuse = m_pReSTIRGI->Render(pRenderGraph, depth, linear_depth, normal, velocity, width, height);

    return CompositeLight(pRenderGraph, depth, gtao, direct_lighting, indirect_specular, indirect_diffuse, width, height);
}

RenderGraphHandle LightingProcessor::CompositeLight(RenderGraph* pRenderGraph, RenderGraphHandle depth, RenderGraphHandle ao, RenderGraphHandle direct_lighting,
    RenderGraphHandle indirect_specular, RenderGraphHandle indirect_diffuse, uint32_t width, uint32_t height)
{
    struct CompositeLightData
    {
        RenderGraphHandle diffuseRT;
        RenderGraphHandle specularRT;
        RenderGraphHandle normalRT;
        RenderGraphHandle emissiveRT;
        RenderGraphHandle customDataRT;
        RenderGraphHandle depthRT;
        RenderGraphHandle ao;

        RenderGraphHandle directLighting;
        RenderGraphHandle indirectSpecular;
        RenderGraphHandle indirectDiffuse;

        RenderGraphHandle output;
    };

    auto pass = pRenderGraph->AddPass<CompositeLightData>("CompositeLight", RenderPassType::Compute,
        [&](CompositeLightData& data, RenderGraphBuilder& builder)
        {
            BasePass* pBasePass = m_pRenderer->GetBassPass();

            data.diffuseRT = builder.Read(pBasePass->GetDiffuseRT());
            data.specularRT = builder.Read(pBasePass->GetSpecularRT());
            data.normalRT = builder.Read(pBasePass->GetNormalRT());
            data.emissiveRT = builder.Read(pBasePass->GetEmissiveRT());
            data.customDataRT = builder.Read(pBasePass->GetCustomDataRT());
            data.depthRT = builder.Read(depth);
            data.directLighting = builder.Read(direct_lighting);

            if (ao.IsValid())
            {
                data.ao = builder.Read(ao);
            }

            if (indirect_specular.IsValid())
            {
                data.indirectSpecular = builder.Read(indirect_specular);
            }

            if (indirect_diffuse.IsValid())
            {
                data.indirectDiffuse = builder.Read(indirect_diffuse);
            }

            RenderGraphTexture::Desc desc;
            desc.width = width;
            desc.height = height;
            desc.format = GfxFormat::RGBA16F;
            data.output = builder.Create<RenderGraphTexture>(desc, "SceneColor RT");
            data.output = builder.Write(data.output);
        },
        [=](const CompositeLightData& data, IGfxCommandList* pCommandList)
        {
            RenderGraphTexture* diffuseRT = pRenderGraph->GetTexture(data.diffuseRT);
            RenderGraphTexture* specularRT = pRenderGraph->GetTexture(data.specularRT);
            RenderGraphTexture* normalRT = pRenderGraph->GetTexture(data.normalRT);
            RenderGraphTexture* emissiveRT = pRenderGraph->GetTexture(data.emissiveRT);
            RenderGraphTexture* customDataRT = pRenderGraph->GetTexture(data.customDataRT);
            RenderGraphTexture* depthRT = pRenderGraph->GetTexture(data.depthRT);
            RenderGraphTexture* directLightingRT = pRenderGraph->GetTexture(data.directLighting);
            RenderGraphTexture* outputRT = pRenderGraph->GetTexture(data.output);
            RenderGraphTexture* aoRT = nullptr;
            RenderGraphTexture* indirectSpecularRT = nullptr;
            RenderGraphTexture* indirectDiffuseRT = nullptr;

            eastl::vector<eastl::string> defines;
            if (data.ao.IsValid())
            {
                aoRT = pRenderGraph->GetTexture(data.ao);

                defines.push_back("GTAO=1");

                if (aoRT->GetTexture()->GetDesc().format == GfxFormat::R32UI)
                {
                    defines.push_back("GTSO=1");
                }
            }

            if (data.indirectSpecular.IsValid())
            {
                indirectSpecularRT = pRenderGraph->GetTexture(data.indirectSpecular);
                defines.push_back("SPECULAR_GI=1");
            }

            if (data.indirectDiffuse.IsValid())
            {
                indirectDiffuseRT = pRenderGraph->GetTexture(data.indirectDiffuse);
                defines.push_back("DIFFUSE_GI=1");
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
            case RendererOutput::Roughness:
                defines.push_back("OUTPUT_ROUGHNESS=1");
                break;
            case RendererOutput::Emissive:
                defines.push_back("OUTPUT_EMISSIVE=1");
                break;
            case RendererOutput::ShadingModel:
                defines.push_back("OUTPUT_SHADING_MODEL=1");
                break;
            case RendererOutput::CustomData:
                defines.push_back("OUTPUT_CUSTOM_DATA=1");
                break;
            case RendererOutput::AO:
                defines.push_back("OUTPUT_AO=1");
                break;
            case RendererOutput::DirectLighting:
                defines.push_back("OUTPUT_DIRECT_LIGHTING=1");
                break;
            case RendererOutput::IndirectSpecular:
                defines.push_back("OUTPUT_INDIRECT_SPECULAR=1");
                break;
            case RendererOutput::IndirectDiffuse:
                defines.push_back("OUTPUT_INDIRECT_DIFFUSE=1");
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
                uint directLightingRT;
                uint aoRT;
                uint indirectSprcularRT;

                uint indirectDiffuseRT;
                uint customDataRT;
                float hsrMaxRoughness;
                uint outputRT;
            };

            CB1 cb1;
            cb1.diffuseRT = diffuseRT->GetSRV()->GetHeapIndex();
            cb1.specularRT = specularRT->GetSRV()->GetHeapIndex();
            cb1.normalRT = normalRT->GetSRV()->GetHeapIndex();
            cb1.emissiveRT = emissiveRT->GetSRV()->GetHeapIndex();
            cb1.customDataRT = customDataRT->GetSRV()->GetHeapIndex();
            cb1.depthRT = depthRT->GetSRV()->GetHeapIndex();
            cb1.directLightingRT = directLightingRT->GetSRV()->GetHeapIndex();
            if (aoRT)
            {
                cb1.aoRT = aoRT->GetSRV()->GetHeapIndex();
            }
            if (indirectSpecularRT)
            {
                cb1.indirectSprcularRT = indirectSpecularRT->IsImported() ? m_pReflection->GetOutputRadianceSRV()->GetHeapIndex() : indirectSpecularRT->GetSRV()->GetHeapIndex();
            }
            if (indirectDiffuseRT)
            {
                cb1.indirectDiffuseRT = indirectDiffuseRT->IsImported() ? m_pReSTIRGI->GetOutputRadianceSRV()->GetHeapIndex() : indirectDiffuseRT->GetSRV()->GetHeapIndex();
            }
            cb1.hsrMaxRoughness = m_pReflection->GetMaxRoughness();
            cb1.outputRT = outputRT->GetUAV()->GetHeapIndex();

            pCommandList->SetComputeConstants(1, &cb1, sizeof(cb1));

            pCommandList->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
        });

    return pass->output;
}
