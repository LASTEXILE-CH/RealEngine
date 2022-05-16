#include "reflection_denoiser.h"
#include "../renderer.h"

ReflectionDenoiser::ReflectionDenoiser(Renderer* pRenderer)
{
    m_pRenderer = pRenderer;

    GfxShaderCompilerFlags flags = 0;
#ifdef _DEBUG
    if (m_pRenderer->GetDevice()->GetVendor() == GfxVendor::AMD)
    {
        flags = GfxShaderCompilerFlagO1; //todo : AMD crashes with -O0
    }
#endif

    GfxComputePipelineDesc desc;
    desc.cs = pRenderer->GetShader("hybrid_stochastic_reflection/denoiser_reproject.hlsl", "main", "cs_6_6", {}, flags);
    m_pReprojectPSO = pRenderer->GetPipelineState(desc, "HSR denoiser reproject PSO");

    desc.cs = pRenderer->GetShader("hybrid_stochastic_reflection/denoiser_prefilter.hlsl", "main", "cs_6_6", {});
    m_pPrefilterPSO = pRenderer->GetPipelineState(desc, "HSR denoiser prefilter PSO");

    desc.cs = pRenderer->GetShader("hybrid_stochastic_reflection/denoiser_resolve_temporal.hlsl", "main", "cs_6_6", {});
    m_pResolveTemporalPSO = pRenderer->GetPipelineState(desc, "HSR denoiser resolve temporal PSO");
}

void ReflectionDenoiser::ImportTextures(RenderGraph* pRenderGraph, uint32_t width, uint32_t height)
{
    if (m_pRadianceHistory == nullptr ||
        m_pRadianceHistory->GetTexture()->GetDesc().width != width ||
        m_pRadianceHistory->GetTexture()->GetDesc().height != height)
    {
        m_bHistoryInvalid = true;
        m_pRadianceHistory.reset(m_pRenderer->CreateTexture2D(width, height, 1, GfxFormat::RGBA16F, GfxTextureUsageUnorderedAccess, "ReflectionDenoiser::m_pRadianceHistory"));
        m_pVarianceHistory.reset(m_pRenderer->CreateTexture2D(width, height, 1, GfxFormat::R16F, GfxTextureUsageUnorderedAccess, "ReflectionDenoiser::m_pVarianceHistory"));

        m_pSampleCountInput.reset(m_pRenderer->CreateTexture2D(width, height, 1, GfxFormat::R16F, GfxTextureUsageUnorderedAccess, "ReflectionDenoiser::m_pSampleCount0"));
        m_pSampleCountOutput.reset(m_pRenderer->CreateTexture2D(width, height, 1, GfxFormat::R16F, GfxTextureUsageUnorderedAccess, "ReflectionDenoiser::m_pSampleCount1"));
    }

    eastl::swap(m_pSampleCountInput, m_pSampleCountOutput);

    m_radianceHistory = pRenderGraph->Import(m_pRadianceHistory->GetTexture(), m_bHistoryInvalid ? GfxResourceState::UnorderedAccess : GfxResourceState::ShaderResourceNonPS);
    m_varianceHistory = pRenderGraph->Import(m_pVarianceHistory->GetTexture(), GfxResourceState::UnorderedAccess);
    m_sampleCountInput = pRenderGraph->Import(m_pSampleCountInput->GetTexture(), m_bHistoryInvalid ? GfxResourceState::UnorderedAccess : GfxResourceState::ShaderResourceNonPS);
    m_sampleCountOutput = pRenderGraph->Import(m_pSampleCountOutput->GetTexture(), m_bHistoryInvalid ? GfxResourceState::UnorderedAccess : GfxResourceState::ShaderResourceNonPS);
}

RenderGraphHandle ReflectionDenoiser::Render(RenderGraph* pRenderGraph, RenderGraphHandle indirectArgs, RenderGraphHandle tileListBuffer, RenderGraphHandle input, 
    RenderGraphHandle depth, RenderGraphHandle linear_depth, RenderGraphHandle normal, RenderGraphHandle velocity, uint32_t width, uint32_t height)
{
    RENDER_GRAPH_EVENT(pRenderGraph, "ReflectionDenoiser");

    struct ReprojectPassData
    {
        RenderGraphHandle indirectArgs;
        RenderGraphHandle tileListBuffer;

        RenderGraphHandle depth;
        RenderGraphHandle normal;
        RenderGraphHandle velocity;
        RenderGraphHandle prevLinearDepth;
        RenderGraphHandle prevNormal;

        RenderGraphHandle inputRadiance;
        RenderGraphHandle historyRadiance;
        RenderGraphHandle historyVariance;
        RenderGraphHandle historySampleCount;

        RenderGraphHandle outputRadiance;
        RenderGraphHandle outputVariance;
        RenderGraphHandle outputAvgRadiance;
        RenderGraphHandle outputSampleCount;
    };

    auto reproject_pass = pRenderGraph->AddPass<ReprojectPassData>("ReflectionDenoiser - Reproject", RenderPassType::Compute,
        [&](ReprojectPassData& data, RenderGraphBuilder& builder)
        {
            data.indirectArgs = builder.ReadIndirectArg(indirectArgs);
            data.tileListBuffer = builder.Read(tileListBuffer);
            
            data.depth = builder.Read(depth);
            data.normal = builder.Read(normal);
            data.velocity = builder.Read(velocity);
            data.prevLinearDepth = builder.Read(m_pRenderer->GetPrevLinearDepthHandle());
            data.prevNormal = builder.Read(m_pRenderer->GetPrevNormalHandle());

            data.inputRadiance = builder.Read(input);
            data.historyRadiance = builder.Read(m_radianceHistory);
            data.historyVariance = builder.Read(m_varianceHistory);
            data.historySampleCount = builder.Read(m_sampleCountInput);

            RenderGraphTexture::Desc desc;
            desc.width = width;
            desc.height = height;
            desc.format = GfxFormat::RGBA16F;
            data.outputRadiance = builder.Create<RenderGraphTexture>(desc, "ReflectionDenoiser reprojected radiance");
            data.outputRadiance = builder.Write(data.outputRadiance);

            desc.format = GfxFormat::R16F;
            data.outputVariance = builder.Create<RenderGraphTexture>(desc, "ReflectionDenoiser reprojected variance");
            data.outputVariance = builder.Write(data.outputVariance);
             
            desc.width = (width + 7) / 8;
            desc.height = (height + 7) / 8;
            desc.format = GfxFormat::RGBA16F;
            data.outputAvgRadiance = builder.Create<RenderGraphTexture>(desc, "ReflectionDenoiser average radiance");
            data.outputAvgRadiance = builder.Write(data.outputAvgRadiance);

            data.outputSampleCount = builder.Write(m_sampleCountOutput);
        },
        [=](const ReprojectPassData& data, IGfxCommandList* pCommandList)
        {
            RenderGraphBuffer* indirectArgs = (RenderGraphBuffer*)pRenderGraph->GetResource(data.indirectArgs);
            RenderGraphBuffer* tileListBuffer = (RenderGraphBuffer*)pRenderGraph->GetResource(data.tileListBuffer);

            RenderGraphTexture* depth = (RenderGraphTexture*)pRenderGraph->GetResource(data.depth);
            RenderGraphTexture* normal = (RenderGraphTexture*)pRenderGraph->GetResource(data.normal);
            RenderGraphTexture* velocity = (RenderGraphTexture*)pRenderGraph->GetResource(data.velocity);

            RenderGraphTexture* inputRadiance = (RenderGraphTexture*)pRenderGraph->GetResource(data.inputRadiance);

            RenderGraphTexture* outputRadiance = (RenderGraphTexture*)pRenderGraph->GetResource(data.outputRadiance);
            RenderGraphTexture* outputVariance = (RenderGraphTexture*)pRenderGraph->GetResource(data.outputVariance);
            RenderGraphTexture* outputAvgRadiance = (RenderGraphTexture*)pRenderGraph->GetResource(data.outputAvgRadiance);

            Reproject(pCommandList, indirectArgs->GetBuffer(), tileListBuffer->GetSRV(), depth->GetSRV(), normal->GetSRV(), velocity->GetSRV(), inputRadiance->GetSRV(),
                outputRadiance->GetUAV(), outputVariance->GetUAV(), outputAvgRadiance->GetUAV());
        });

    struct PrefilterPassData
    {
        RenderGraphHandle indirectArgs;
        RenderGraphHandle tileListBuffer;

        RenderGraphHandle linearDepth;
        RenderGraphHandle normal;

        RenderGraphHandle inputRadiance;
        RenderGraphHandle inputVariance;
        RenderGraphHandle avgRadiance;

        RenderGraphHandle outputRadiance;
        RenderGraphHandle outputVariance;
    };

    auto prefilter_pass = pRenderGraph->AddPass<PrefilterPassData>("ReflectionDenoiser - Prefilter", RenderPassType::Compute,
        [&](PrefilterPassData& data, RenderGraphBuilder& builder)
        {
            data.indirectArgs = builder.ReadIndirectArg(indirectArgs);
            data.tileListBuffer = builder.Read(tileListBuffer);
            data.linearDepth = builder.Read(linear_depth);
            data.normal = builder.Read(normal);

            data.inputRadiance = builder.Read(input);
            data.inputVariance = builder.Read(reproject_pass->outputVariance);
            data.avgRadiance = builder.Read(reproject_pass->outputAvgRadiance);

            RenderGraphTexture::Desc desc;
            desc.width = width;
            desc.height = height;
            desc.format = GfxFormat::RGBA16F;
            data.outputRadiance = builder.Create<RenderGraphTexture>(desc, "ReflectionDenoiser prefilter radiance");
            data.outputRadiance = builder.Write(data.outputRadiance);

            desc.format = GfxFormat::R16F;
            data.outputVariance = builder.Create<RenderGraphTexture>(desc, "ReflectionDenoiser prefilter variance");
            data.outputVariance = builder.Write(data.outputVariance);
        },
        [=](const PrefilterPassData& data, IGfxCommandList* pCommandList)
        {
            RenderGraphBuffer* indirectArgs = (RenderGraphBuffer*)pRenderGraph->GetResource(data.indirectArgs);
            RenderGraphBuffer* tileListBuffer = (RenderGraphBuffer*)pRenderGraph->GetResource(data.tileListBuffer);

            RenderGraphTexture* linearDepth = (RenderGraphTexture*)pRenderGraph->GetResource(data.linearDepth);
            RenderGraphTexture* normal = (RenderGraphTexture*)pRenderGraph->GetResource(data.normal);

            RenderGraphTexture* inputRadiance = (RenderGraphTexture*)pRenderGraph->GetResource(data.inputRadiance);
            RenderGraphTexture* inputVariance = (RenderGraphTexture*)pRenderGraph->GetResource(data.inputVariance);
            RenderGraphTexture* avgRadiance = (RenderGraphTexture*)pRenderGraph->GetResource(data.avgRadiance);

            RenderGraphTexture* outputRadiance = (RenderGraphTexture*)pRenderGraph->GetResource(data.outputRadiance);
            RenderGraphTexture* outputVariance = (RenderGraphTexture*)pRenderGraph->GetResource(data.outputVariance);

            Prefilter(pCommandList, indirectArgs->GetBuffer(), tileListBuffer->GetSRV(), linearDepth->GetSRV(), normal->GetSRV(),
                inputRadiance->GetSRV(), inputVariance->GetSRV(), 
                avgRadiance->GetSRV(), outputRadiance->GetUAV(), outputVariance->GetUAV());
        });

    struct ResovleTemporalPassData
    {
        RenderGraphHandle indirectArgs;
        RenderGraphHandle tileListBuffer;

        RenderGraphHandle normal;
        RenderGraphHandle inputRadiance;
        RenderGraphHandle reprojectedRadiance;
        RenderGraphHandle avgRadiance;
        RenderGraphHandle inputVariance;
        RenderGraphHandle sampleCount;

        RenderGraphHandle outputRadiance;
        RenderGraphHandle outputVariance;
    };

    auto resolve_temporal_pass = pRenderGraph->AddPass<ResovleTemporalPassData>("ReflectionDenoiser - ResovleTemporal", RenderPassType::Compute,
        [&](ResovleTemporalPassData& data, RenderGraphBuilder& builder)
        {
            data.indirectArgs = builder.ReadIndirectArg(indirectArgs);
            data.tileListBuffer = builder.Read(tileListBuffer);

            data.normal = builder.Read(normal);
            data.inputRadiance = builder.Read(prefilter_pass->outputRadiance);
            data.inputVariance = builder.Read(prefilter_pass->outputVariance);
            data.reprojectedRadiance = builder.Read(reproject_pass->outputRadiance);
            data.avgRadiance = builder.Read(reproject_pass->outputAvgRadiance);
            data.sampleCount = builder.Read(reproject_pass->outputSampleCount);

            data.outputRadiance = builder.Write(m_radianceHistory);
            data.outputVariance = builder.Write(m_varianceHistory);

            builder.MakeTarget(); //todo
        },
        [=](const ResovleTemporalPassData& data, IGfxCommandList* pCommandList)
        {
            RenderGraphBuffer* indirectArgs = (RenderGraphBuffer*)pRenderGraph->GetResource(data.indirectArgs);
            RenderGraphBuffer* tileListBuffer = (RenderGraphBuffer*)pRenderGraph->GetResource(data.tileListBuffer);

            RenderGraphTexture* normal = (RenderGraphTexture*)pRenderGraph->GetResource(data.normal);
            RenderGraphTexture* inputRadiance = (RenderGraphTexture*)pRenderGraph->GetResource(data.inputRadiance);
            RenderGraphTexture* inputVariance = (RenderGraphTexture*)pRenderGraph->GetResource(data.inputVariance);
            RenderGraphTexture* reprojectedRadiance = (RenderGraphTexture*)pRenderGraph->GetResource(data.reprojectedRadiance);
            RenderGraphTexture* avgRadiance = (RenderGraphTexture*)pRenderGraph->GetResource(data.avgRadiance);

            IGfxDescriptor* sampleCount = m_pSampleCountOutput->GetSRV();
            IGfxDescriptor* outputRadianceUAV = m_pRadianceHistory->GetUAV();
            IGfxDescriptor* outputVarianceUAV = m_pVarianceHistory->GetUAV();

			ResolveTemporal(pCommandList, indirectArgs->GetBuffer(), tileListBuffer->GetSRV(), normal->GetSRV(), inputRadiance->GetSRV(), inputVariance->GetSRV(),
                reprojectedRadiance->GetSRV(), avgRadiance->GetSRV(), sampleCount, outputRadianceUAV, outputVarianceUAV);
        });

    return resolve_temporal_pass->outputRadiance;
}

void ReflectionDenoiser::Reproject(IGfxCommandList* pCommandList, IGfxBuffer* indirectArgs, IGfxDescriptor* tileList, 
    IGfxDescriptor* depth, IGfxDescriptor* normal, IGfxDescriptor* velocity, IGfxDescriptor* inputRadiance,
    IGfxDescriptor* outputRadianceUAV, IGfxDescriptor* outputVariancceUAV, IGfxDescriptor* outputAvgRadianceUAV)
{
    pCommandList->SetPipelineState(m_pReprojectPSO);

    struct Constants
    {
        uint tileListBuffer;
        float temporalStability;

        uint depthTexture;
        uint normalTexture;
        uint velocityTexture;
        uint prevLinearDepthTexture;
        uint prevNormalTexture;

        uint inputRadianceTexture;
        uint historyRadianceTexture;
        uint historyVarianceTexture;
        uint historySampleNumTexture;

        uint outputRadianceUAV;
        uint outputVarianceUAV;
        uint outputAvgRadianceUAV;
        uint outputSampleNumUAV;
    };

    Constants constants;
    constants.tileListBuffer = tileList->GetHeapIndex();
    constants.temporalStability = m_temporalStability;
    constants.depthTexture = depth->GetHeapIndex();
    constants.normalTexture = normal->GetHeapIndex();
    constants.velocityTexture = velocity->GetHeapIndex();
    constants.prevLinearDepthTexture = m_pRenderer->GetPrevLinearDepthTexture()->GetSRV()->GetHeapIndex();
    constants.prevNormalTexture = m_pRenderer->GetPrevNormalTexture()->GetSRV()->GetHeapIndex();
    constants.inputRadianceTexture = inputRadiance->GetHeapIndex();
    constants.historyRadianceTexture = m_pRadianceHistory->GetSRV()->GetHeapIndex();
    constants.historyVarianceTexture = m_pVarianceHistory->GetSRV()->GetHeapIndex();
    constants.historySampleNumTexture = m_pSampleCountInput->GetSRV()->GetHeapIndex();
    constants.outputRadianceUAV = outputRadianceUAV->GetHeapIndex();
    constants.outputVarianceUAV = outputVariancceUAV->GetHeapIndex();
    constants.outputAvgRadianceUAV = outputAvgRadianceUAV->GetHeapIndex();
    constants.outputSampleNumUAV = m_pSampleCountOutput->GetUAV()->GetHeapIndex();

    pCommandList->SetComputeConstants(1, &constants, sizeof(constants));
    pCommandList->DispatchIndirect(indirectArgs, 0);

    if (m_bHistoryInvalid)
    {
        m_bHistoryInvalid = false;
    }
}

void ReflectionDenoiser::Prefilter(IGfxCommandList* pCommandList, IGfxBuffer* indirectArgs, IGfxDescriptor* tileList,
    IGfxDescriptor* linear_depth, IGfxDescriptor* normal, IGfxDescriptor* inputRadiance, IGfxDescriptor* inputVariance, IGfxDescriptor* avgRadiance,
    IGfxDescriptor* outputRadianceUAV, IGfxDescriptor* outputVariancceUAV)
{
    pCommandList->SetPipelineState(m_pPrefilterPSO);

    struct Constants
    {
        uint tileListBuffer;
        uint linearDepthTexture;
        uint normalTexture;
        uint radianceTexture;
        uint varianceTexture;
        uint avgRadianceTexture;
        uint outputRadianceUAV;
        uint outputVarianceUAV;
    };

    Constants constants;
    constants.tileListBuffer = tileList->GetHeapIndex();
    constants.linearDepthTexture = linear_depth->GetHeapIndex();
    constants.normalTexture = normal->GetHeapIndex();
    constants.radianceTexture = inputRadiance->GetHeapIndex();
    constants.varianceTexture = inputVariance->GetHeapIndex();
    constants.avgRadianceTexture = avgRadiance->GetHeapIndex();
    constants.outputRadianceUAV = outputRadianceUAV->GetHeapIndex();
    constants.outputVarianceUAV = outputVariancceUAV->GetHeapIndex();

    pCommandList->SetComputeConstants(1, &constants, sizeof(constants));
    pCommandList->DispatchIndirect(indirectArgs, 0);
}

void ReflectionDenoiser::ResolveTemporal(IGfxCommandList* pCommandList, IGfxBuffer* indirectArgs, IGfxDescriptor* tileList,
    IGfxDescriptor* normal, IGfxDescriptor* inputRadiance, IGfxDescriptor* inputVariance, IGfxDescriptor* reprojectedRadiance, IGfxDescriptor* avgRadiance,
    IGfxDescriptor* sampleCount, IGfxDescriptor* outputRadianceUAV, IGfxDescriptor* outputVariancceUAV)
{
    pCommandList->SetPipelineState(m_pResolveTemporalPSO);

    struct Constants
    {
        uint tileListBuffer;
        float temporalStability;
        uint normalTexture;
        uint inputRadianceTexture;
        uint reprojectedRadianceTexture;
        uint avgRadianceTexture;
        uint inputVarianceTexture;
        uint sampleCountTexture;
        uint outputRadianceUAV;
        uint outputVarianceUAV;
    };

    Constants constants;
    constants.tileListBuffer = tileList->GetHeapIndex();
    constants.temporalStability = m_temporalStability;
    constants.normalTexture = normal->GetHeapIndex();
    constants.inputRadianceTexture = inputRadiance->GetHeapIndex();
    constants.reprojectedRadianceTexture = reprojectedRadiance->GetHeapIndex();
    constants.avgRadianceTexture = avgRadiance->GetHeapIndex();
    constants.inputVarianceTexture = inputVariance->GetHeapIndex();
    constants.sampleCountTexture = sampleCount->GetHeapIndex();
    constants.outputRadianceUAV = outputRadianceUAV->GetHeapIndex();
    constants.outputVarianceUAV = outputVariancceUAV->GetHeapIndex();

    pCommandList->SetComputeConstants(1, &constants, sizeof(constants));
    pCommandList->DispatchIndirect(indirectArgs, 0);
}
