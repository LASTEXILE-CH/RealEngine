#include "restir_gi.h"
#include "../renderer.h"
#include "utils/gui_util.h"
#include "fmt/format.h"

ReSTIRGI::ReSTIRGI(Renderer* pRenderer)
{
    m_pRenderer = pRenderer;

    GfxComputePipelineDesc desc;
    desc.cs = pRenderer->GetShader("restir_gi/initial_sampling.hlsl", "main", "cs_6_6", {});
    m_pInitialSamplingPSO = pRenderer->GetPipelineState(desc, "ReSTIR GI/initial sampling PSO");

    desc.cs = pRenderer->GetShader("restir_gi/temporal_resampling.hlsl", "main", "cs_6_6", {});
    m_pTemporalResamplingPSO = pRenderer->GetPipelineState(desc, "ReSTIR GI/temporal resampling PSO");

    desc.cs = pRenderer->GetShader("restir_gi/spatial_resampling.hlsl", "main", "cs_6_6", {});
    m_pSpatialResamplingPSO = pRenderer->GetPipelineState(desc, "ReSTIR GI/spatial resampling PSO");

    m_pDenoiser = eastl::make_unique<GIDenoiser>(pRenderer);
}

RGHandle ReSTIRGI::Render(RenderGraph* pRenderGraph, RGHandle halfDepthNormal, RGHandle depth, RGHandle linear_depth, RGHandle normal, RGHandle velocity, uint32_t width, uint32_t height)
{
    GUI("Lighting", "ReSTIR GI (WIP)",
        [&]()
        {
            if (ImGui::Checkbox("Enable##ReSTIR GI", &m_bEnable))
            {
                ResetTemporalBuffers();
                m_pDenoiser->InvalidateHistory();
            }
            
            if (ImGui::Checkbox("Enable ReSTIR##ReSTIR GI", &m_bEnableReSTIR))
            {
                ResetTemporalBuffers();
            }
            
            if (ImGui::Checkbox("Enable Denoiser##ReSTIR GI", &m_bEnableDenoiser))
            {
                m_pDenoiser->InvalidateHistory();
            }
        });

    if (!m_bEnable)
    {
        return RGHandle();
    }

    RENDER_GRAPH_EVENT(pRenderGraph, "ReSTIR GI");

    uint32_t half_width = (width + 1) / 2;
    uint32_t half_height = (height + 1) / 2;

    struct RaytracePassData
    {
        RGHandle halfDepthNormal;
        RGHandle prevLinearDepth;
        RGHandle historyIrradiance;
        RGHandle outputRadiance;
        RGHandle outputRayDirection;
    };

    auto raytrace_pass = pRenderGraph->AddPass<RaytracePassData>("ReSTIR GI - initial sampling", 
        m_pRenderer->IsAsyncComputeEnabled() ? RenderPassType::AsyncCompute : RenderPassType::Compute,
        [&](RaytracePassData& data, RGBuilder& builder)
        {
            data.halfDepthNormal = builder.Read(halfDepthNormal);
            data.prevLinearDepth = builder.Read(m_pRenderer->GetPrevLinearDepthHandle());

            if (m_bEnableDenoiser)
            {
                m_pDenoiser->ImportHistoryTextures(pRenderGraph, width, height);
                data.historyIrradiance = builder.Read(m_pDenoiser->GetHistoryIrradiance());
            }

            RGTexture::Desc desc;
            desc.width = half_width;
            desc.height = half_height;
            desc.format = GfxFormat::R11G11B10F;
            data.outputRadiance = builder.Write(builder.Create<RGTexture>(desc, "ReSTIR GI/candidate radiance"));

            desc.format = GfxFormat::R32UI;
            data.outputRayDirection = builder.Write(builder.Create<RGTexture>(desc, "ReSTIR GI/candidate ray direction"));
        },
        [=](const RaytracePassData& data, IGfxCommandList* pCommandList)
        {
            InitialSampling(pCommandList, 
                pRenderGraph->GetTexture(data.halfDepthNormal),
                pRenderGraph->GetTexture(data.outputRadiance),
                pRenderGraph->GetTexture(data.outputRayDirection),
                half_width, half_height);
        });

    RGHandle radiance = raytrace_pass->outputRadiance;
    RGHandle rayDirection = raytrace_pass->outputRayDirection;
    RGHandle reservoir;

    if (m_bEnableReSTIR)
    {
        bool initialFrame = InitTemporalBuffers(half_width, half_height);
        eastl::swap(m_temporalReservoir[0], m_temporalReservoir[1]);

        struct TemporalReusePassData
        {
            RGHandle halfDepthNormal;
            RGHandle velocity;
            RGHandle candidateRadiance;
            RGHandle candidateRayDirection;
            RGHandle historyReservoirDepthNormal;
            RGHandle historyReservoirSampleRadiance;
            RGHandle historyReservoirRayDirection;
            RGHandle historyReservoir;
            RGHandle outputReservoirDepthNormal;
            RGHandle outputReservoirSampleRadiance;
            RGHandle outputReservoirRayDirection;
            RGHandle outputReservoir;
        };

        auto temporal_reuse_pass = pRenderGraph->AddPass<TemporalReusePassData>("ReSTIR GI - temporal resampling", RenderPassType::Compute,
            [&](TemporalReusePassData& data, RGBuilder& builder)
            {
                data.halfDepthNormal = builder.Read(halfDepthNormal);
                data.velocity = builder.Read(velocity);
                data.candidateRadiance = builder.Read(raytrace_pass->outputRadiance);
                data.candidateRayDirection = builder.Read(raytrace_pass->outputRayDirection);

                GfxResourceState textureState = initialFrame ? GfxResourceState::UnorderedAccess : GfxResourceState::ShaderResourceNonPS;

                data.historyReservoirDepthNormal = builder.Read(builder.Import(m_temporalReservoir[0].depthNormal->GetTexture(), GfxResourceState::UnorderedAccess));
                data.historyReservoirSampleRadiance = builder.Read(builder.Import(m_temporalReservoir[0].sampleRadiance->GetTexture(), textureState));
                data.historyReservoirRayDirection = builder.Read(builder.Import(m_temporalReservoir[0].rayDirection->GetTexture(), textureState));
                data.historyReservoir = builder.Read(builder.Import(m_temporalReservoir[0].reservoir->GetTexture(), textureState));

                data.outputReservoirDepthNormal = builder.Write(builder.Import(m_temporalReservoir[1].depthNormal->GetTexture(), textureState));
                data.outputReservoirSampleRadiance = builder.Write(builder.Import(m_temporalReservoir[1].sampleRadiance->GetTexture(), textureState));
                data.outputReservoirRayDirection = builder.Write(builder.Import(m_temporalReservoir[1].rayDirection->GetTexture(), textureState));
                data.outputReservoir = builder.Write(builder.Import(m_temporalReservoir[1].reservoir->GetTexture(), textureState));
            },
            [=](const TemporalReusePassData& data, IGfxCommandList* pCommandList)
            {
                TemporalResampling(pCommandList,
                    pRenderGraph->GetTexture(data.halfDepthNormal),
                    pRenderGraph->GetTexture(data.velocity),
                    pRenderGraph->GetTexture(data.candidateRadiance),
                    pRenderGraph->GetTexture(data.candidateRayDirection),
                    half_width, half_height, initialFrame);
            });

        struct SpatialReusePassData
        {
            RGHandle halfDepthNormal;
            RGHandle inputReservoirSampleRadiance;
            RGHandle inputReservoirRayDirection;
            RGHandle inputReservoir;
            RGHandle outputReservoirSampleRadiance;
            RGHandle outputReservoirRayDirection;
            RGHandle outputReservoir;
        };

        auto spatial_reuse_pass0 = pRenderGraph->AddPass<SpatialReusePassData>("ReSTIR GI - spatial resampling", RenderPassType::Compute,
            [&](SpatialReusePassData& data, RGBuilder& builder)
            {
                data.halfDepthNormal = builder.Read(halfDepthNormal);
                data.inputReservoirSampleRadiance = builder.Read(temporal_reuse_pass->outputReservoirSampleRadiance);
                data.inputReservoirRayDirection = builder.Read(temporal_reuse_pass->outputReservoirRayDirection);
                data.inputReservoir = builder.Read(temporal_reuse_pass->outputReservoir);

                RGTexture::Desc desc;
                desc.width = half_width;
                desc.height = half_height;
                desc.format = GfxFormat::R11G11B10F;
                data.outputReservoirSampleRadiance = builder.Write(builder.Create<RGTexture>(desc, "ReSTIR GI/spatial reservoir radiance"));

                desc.format = GfxFormat::R32UI;
                data.outputReservoirRayDirection = builder.Write(builder.Create<RGTexture>(desc, "ReSTIR GI/spatial reservoir rayDireciton"));

                desc.format = GfxFormat::RG16F;
                data.outputReservoir = builder.Write(builder.Create<RGTexture>(desc, "ReSTIR GI/spatial reservoir"));
            },
            [=](const SpatialReusePassData& data, IGfxCommandList* pCommandList)
            {
                SpatialResampling(pCommandList,
                    pRenderGraph->GetTexture(data.halfDepthNormal),
                    m_temporalReservoir[1].sampleRadiance->GetSRV(),
                    m_temporalReservoir[1].rayDirection->GetSRV(),
                    m_temporalReservoir[1].reservoir->GetSRV(),
                    pRenderGraph->GetTexture(data.outputReservoirSampleRadiance),
                    pRenderGraph->GetTexture(data.outputReservoirRayDirection),
                    pRenderGraph->GetTexture(data.outputReservoir),
                    half_width, half_height, 0);
            });

        auto spatial_reuse_pass1 = pRenderGraph->AddPass<SpatialReusePassData>("ReSTIR GI - spatial resampling", RenderPassType::Compute,
            [&](SpatialReusePassData& data, RGBuilder& builder)
            {
                data.halfDepthNormal = builder.Read(halfDepthNormal);
                data.inputReservoirSampleRadiance = builder.Read(spatial_reuse_pass0->outputReservoirSampleRadiance);
                data.inputReservoirRayDirection = builder.Read(spatial_reuse_pass0->outputReservoirRayDirection);
                data.inputReservoir = builder.Read(spatial_reuse_pass0->outputReservoir);

                RGTexture::Desc desc;
                desc.width = half_width;
                desc.height = half_height;
                desc.format = GfxFormat::R11G11B10F;
                data.outputReservoirSampleRadiance = builder.Write(builder.Create<RGTexture>(desc, "ReSTIR GI/spatial reservoir radiance"));

                desc.format = GfxFormat::R32UI;
                data.outputReservoirRayDirection = builder.Write(builder.Create<RGTexture>(desc, "ReSTIR GI/spatial reservoir rayDireciton"));

                desc.format = GfxFormat::RG16F;
                data.outputReservoir = builder.Write(builder.Create<RGTexture>(desc, "ReSTIR GI/spatial reservoir"));
            },
            [=](const SpatialReusePassData& data, IGfxCommandList* pCommandList)
            {
                SpatialResampling(pCommandList,
                    pRenderGraph->GetTexture(data.halfDepthNormal),
                    pRenderGraph->GetTexture(data.inputReservoirSampleRadiance)->GetSRV(),
                    pRenderGraph->GetTexture(data.inputReservoirRayDirection)->GetSRV(),
                    pRenderGraph->GetTexture(data.inputReservoir)->GetSRV(),
                    pRenderGraph->GetTexture(data.outputReservoirSampleRadiance),
                    pRenderGraph->GetTexture(data.outputReservoirRayDirection),
                    pRenderGraph->GetTexture(data.outputReservoir),
                    half_width, half_height, 1);
            });

        radiance = spatial_reuse_pass1->outputReservoirSampleRadiance;
        rayDirection = spatial_reuse_pass1->outputReservoirRayDirection;
        reservoir = spatial_reuse_pass1->outputReservoir;
    }

    struct ResolvePassData
    {
        RGHandle reservoir;
        RGHandle radiance;
        RGHandle rayDirection;
        RGHandle halfDepthNormal;
        RGHandle depth;
        RGHandle normal;
        RGHandle output;
        RGHandle outputVariance;
    };

    auto resolve_pass = pRenderGraph->AddPass<ResolvePassData>("ReSTIR GI - resolve", RenderPassType::Compute,
        [&](ResolvePassData& data, RGBuilder& builder)
        {
            if (m_bEnableReSTIR)
            {
                data.reservoir = builder.Read(reservoir);
            }
            data.radiance = builder.Read(radiance);
            data.rayDirection = builder.Read(rayDirection);
            data.halfDepthNormal = builder.Read(halfDepthNormal);
            data.depth = builder.Read(depth);
            data.normal = builder.Read(normal);

            RGTexture::Desc desc;
            desc.width = width;
            desc.height = height;

            if (m_bEnableDenoiser)
            {
                desc.format = GfxFormat::RGBA32UI;
                data.output = builder.Write(builder.Create<RGTexture>(desc, "ReSTIR GI/resolve output SH"));

                desc.format = GfxFormat::R8UNORM;
                data.outputVariance = builder.Write(builder.Create<RGTexture>(desc, "ReSTIR GI/resolve output variance"));
            }
            else
            {
                desc.format = GfxFormat::R11G11B10F;
                data.output = builder.Write(builder.Create<RGTexture>(desc, "ReSTIR GI/resolve output"));
            }
        },
        [=](const ResolvePassData& data, IGfxCommandList* pCommandList)
        {
            Resolve(pCommandList, 
                pRenderGraph->GetTexture(data.reservoir),
                pRenderGraph->GetTexture(data.radiance),
                pRenderGraph->GetTexture(data.rayDirection),
                pRenderGraph->GetTexture(data.halfDepthNormal),
                pRenderGraph->GetTexture(data.depth),
                pRenderGraph->GetTexture(data.normal),
                pRenderGraph->GetTexture(data.output),
                pRenderGraph->GetTexture(data.outputVariance),
                width, height);
        });

    if (!m_bEnableDenoiser)
    {
        return resolve_pass->output;
    }

    return m_pDenoiser->Render(pRenderGraph, resolve_pass->output, resolve_pass->outputVariance, depth, linear_depth, normal, velocity, width, height);
}

void ReSTIRGI::InitialSampling(IGfxCommandList* pCommandList, RGTexture* halfDepthNormal, RGTexture* outputRadiance, RGTexture* outputRayDirection, uint32_t width, uint32_t height)
{
    pCommandList->SetPipelineState(m_pInitialSamplingPSO);

    struct CB
    {
        uint halfDepthNormalTexture;
        uint prevLinearDepthTexture;
        uint historyIrradiance;
        uint outputRadianceUAV;
        uint outputRayDirectionUAV;
    };

    CB constants;
    constants.halfDepthNormalTexture = halfDepthNormal->GetSRV()->GetHeapIndex();
    constants.prevLinearDepthTexture = m_pRenderer->GetPrevLinearDepthTexture()->GetSRV()->GetHeapIndex();
    constants.historyIrradiance = m_bEnableDenoiser ? m_pDenoiser->GetHistoryIrradianceSRV()->GetHeapIndex() : GFX_INVALID_RESOURCE;
    constants.outputRadianceUAV = outputRadiance->GetUAV()->GetHeapIndex();
    constants.outputRayDirectionUAV = outputRayDirection->GetUAV()->GetHeapIndex();

    pCommandList->SetComputeConstants(0, &constants, sizeof(constants));
    pCommandList->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
}

void ReSTIRGI::TemporalResampling(IGfxCommandList* pCommandList, RGTexture* halfDepthNormal, RGTexture* velocity, 
    RGTexture* candidateRadiance, RGTexture* candidateRayDirection, uint32_t width, uint32_t height, bool historyInvalid)
{
    pCommandList->SetPipelineState(m_pTemporalResamplingPSO);

    struct CB
    {
        uint halfDepthNormal;
        uint velocity;
        uint candidateRadiance;
        uint candidateRayDirection;
        uint historyReservoirDepthNormal;
        uint historyReservoirSampleRadiance;
        uint historyReservoirRayDirection;
        uint historyReservoir;
        uint outputReservoirDepthNormal;
        uint outputReservoirSampleRadiance;
        uint outputReservoirRayDirection;
        uint outputReservoir;
        uint bHistoryInvalid;
    };

    CB cb;
    cb.halfDepthNormal = halfDepthNormal->GetSRV()->GetHeapIndex();
    cb.velocity = velocity->GetSRV()->GetHeapIndex();
    cb.candidateRadiance = candidateRadiance->GetSRV()->GetHeapIndex();
    cb.candidateRayDirection = candidateRayDirection->GetSRV()->GetHeapIndex();
    cb.historyReservoirDepthNormal = m_temporalReservoir[0].depthNormal->GetSRV()->GetHeapIndex();
    cb.historyReservoirSampleRadiance = m_temporalReservoir[0].sampleRadiance->GetSRV()->GetHeapIndex();
    cb.historyReservoirRayDirection = m_temporalReservoir[0].rayDirection->GetSRV()->GetHeapIndex();
    cb.historyReservoir = m_temporalReservoir[0].reservoir->GetSRV()->GetHeapIndex();
    cb.outputReservoirDepthNormal = m_temporalReservoir[1].depthNormal->GetUAV()->GetHeapIndex();
    cb.outputReservoirSampleRadiance = m_temporalReservoir[1].sampleRadiance->GetUAV()->GetHeapIndex();
    cb.outputReservoirRayDirection = m_temporalReservoir[1].rayDirection->GetUAV()->GetHeapIndex();
    cb.outputReservoir = m_temporalReservoir[1].reservoir->GetUAV()->GetHeapIndex();
    cb.bHistoryInvalid = historyInvalid;

    pCommandList->SetComputeConstants(1, &cb, sizeof(cb));
    pCommandList->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
}

void ReSTIRGI::SpatialResampling(IGfxCommandList* pCommandList, RGTexture* halfDepthNormal,
    IGfxDescriptor* inputReservoirSampleRadiance, IGfxDescriptor* inputReservoirRayDirection, IGfxDescriptor* inputReservoir,
    RGTexture* outputReservoirSampleRadiance, RGTexture* outputReservoirRayDireciton, RGTexture* outputReservoir, uint32_t width, uint32_t height, uint32_t pass_index)
{
    pCommandList->SetPipelineState(m_pSpatialResamplingPSO);

    struct Constants
    {
        uint halfDepthNormal;
        uint inputReservoirSampleRadiance;
        uint inputReservoirRayDirection;
        uint inputReservoir;
        uint outputReservoirSampleRadiance;
        uint outputReservoirRayDirection;
        uint outputReservoir;
        uint spatialPass;
    };

    Constants constants;
    constants.halfDepthNormal = halfDepthNormal->GetSRV()->GetHeapIndex();
    constants.inputReservoirSampleRadiance = inputReservoirSampleRadiance->GetHeapIndex();
    constants.inputReservoirRayDirection = inputReservoirRayDirection->GetHeapIndex();
    constants.inputReservoir = inputReservoir->GetHeapIndex();
    constants.outputReservoirSampleRadiance = outputReservoirSampleRadiance->GetUAV()->GetHeapIndex();
    constants.outputReservoirRayDirection = outputReservoirRayDireciton->GetUAV()->GetHeapIndex();
    constants.outputReservoir = outputReservoir->GetUAV()->GetHeapIndex();
    constants.spatialPass = pass_index;

    pCommandList->SetComputeConstants(1, &constants, sizeof(constants));
    pCommandList->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
}

void ReSTIRGI::Resolve(IGfxCommandList* pCommandList, RGTexture* reservoir, RGTexture* radiance, RGTexture* rayDirection, RGTexture* halfDepthNormal, RGTexture* depth, RGTexture* normal, 
    RGTexture* output, RGTexture* outputVariance, uint32_t width, uint32_t height)
{
    eastl::vector<eastl::string> defines;

    if (m_bEnableReSTIR)
    {
        defines.push_back("ENABLE_RESTIR=1");
    }

    if (m_bEnableDenoiser)
    {
        defines.push_back("OUTPUT_SH=1");
    }

    GfxComputePipelineDesc desc;
    desc.cs = m_pRenderer->GetShader("restir_gi/restir_resolve.hlsl", "main", "cs_6_6", defines);
    IGfxPipelineState* pso = m_pRenderer->GetPipelineState(desc, "ReSTIR GI/resolve PSO");

    pCommandList->SetPipelineState(pso);

    uint32_t constants[8] = { 
        reservoir ? reservoir->GetSRV()->GetHeapIndex() : GFX_INVALID_RESOURCE,
        radiance->GetSRV()->GetHeapIndex(),
        rayDirection->GetSRV()->GetHeapIndex(),
        halfDepthNormal->GetSRV()->GetHeapIndex(),
        depth->GetSRV()->GetHeapIndex(),
        normal->GetSRV()->GetHeapIndex(),
        output->GetUAV()->GetHeapIndex(),
        outputVariance ? outputVariance->GetUAV()->GetHeapIndex() : GFX_INVALID_RESOURCE,
    };
    pCommandList->SetComputeConstants(1, constants, sizeof(constants));
    pCommandList->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
}

bool ReSTIRGI::InitTemporalBuffers(uint32_t width, uint32_t height)
{
    bool initialFrame = false;

    if (m_temporalReservoir[0].reservoir == nullptr ||
        m_temporalReservoir[0].reservoir->GetTexture()->GetDesc().width != width ||
        m_temporalReservoir[0].reservoir->GetTexture()->GetDesc().height != height)
    {
        for (int i = 0; i < 2; ++i)
        {
            m_temporalReservoir[i].sampleRadiance.reset(m_pRenderer->CreateTexture2D(width, height, 1, GfxFormat::R11G11B10F, GfxTextureUsageUnorderedAccess,
                fmt::format("ReSTIR GI/temporal reservoir sampleRadiance {}", i).c_str()));
            m_temporalReservoir[i].rayDirection.reset(m_pRenderer->CreateTexture2D(width, height, 1, GfxFormat::R32UI, GfxTextureUsageUnorderedAccess,
                fmt::format("ReSTIR GI/temporal reservoir rayDirection {}", i).c_str()));
            m_temporalReservoir[i].depthNormal.reset(m_pRenderer->CreateTexture2D(width, height, 1, GfxFormat::RG32UI, GfxTextureUsageUnorderedAccess,
                fmt::format("ReSTIR GI/temporal reservoir depthNormal {}", i).c_str()));
            m_temporalReservoir[i].reservoir.reset(m_pRenderer->CreateTexture2D(width, height, 1, GfxFormat::RG16F, GfxTextureUsageUnorderedAccess,
                fmt::format("ReSTIR GI/temporal reservoir {}", i).c_str()));
        }

        initialFrame = true;
    }

    return initialFrame;
}

void ReSTIRGI::ResetTemporalBuffers()
{
    for (int i = 0; i < 2; ++i)
    {
        m_temporalReservoir[i].sampleRadiance.reset();
        m_temporalReservoir[i].rayDirection.reset();
        m_temporalReservoir[i].depthNormal.reset();
        m_temporalReservoir[i].reservoir.reset();
    }
}
