#pragma once

#include "../render_graph.h"
#include "../resource/texture_2d.h"
#include "reflection_denoiser.h"

class ReSTIRGI
{
public:
    ReSTIRGI(Renderer* pRenderer);

    RGHandle Render(RenderGraph* pRenderGraph, RGHandle halfDepthNormal, RGHandle depth, RGHandle linear_depth, RGHandle normal, RGHandle velocity, uint32_t width, uint32_t height);

    IGfxDescriptor* GetOutputRadianceSRV() const { return m_pDenoiser->GetHistoryRadianceSRV(); }

private:
    void InitialSampling(IGfxCommandList* pCommandList, RGTexture* halfDepthNormal, RGTexture* outputRadiance, uint32_t width, uint32_t height);
    void TemporalResampling(IGfxCommandList* pCommandList, RGTexture* halfDepthNormal, RGTexture* velocity, RGTexture* candidateRadiance, uint32_t width, uint32_t height);
    void SpatialResampling(IGfxCommandList* pCommandList, RGTexture* halfDepthNormal,
        IGfxDescriptor* inputReservoirSampleRadiance, IGfxDescriptor* inputReservoir,
        RGTexture* outputReservoirSampleRadiance, RGTexture* outputReservoir, uint32_t width, uint32_t height, uint32_t pass_index);

    void Resolve(IGfxCommandList* pCommandList, RGTexture* reservoir, RGTexture* radiance, RGTexture* normal,
        RGTexture* output, uint32_t width, uint32_t height);

    bool InitTemporalBuffers(uint32_t width, uint32_t height);

private:
    Renderer* m_pRenderer;

    IGfxPipelineState* m_pInitialSamplingPSO = nullptr;
    IGfxPipelineState* m_pTemporalResamplingPSO = nullptr;
    IGfxPipelineState* m_pSpatialResamplingPSO = nullptr;
    IGfxPipelineState* m_pResolvePSO = nullptr;

    struct TemporalReservoirBuffer
    {
        eastl::unique_ptr<Texture2D> sampleRadiance; //r11g11b10f
        eastl::unique_ptr<Texture2D> reservoir; //rg16f - M/W
    };

    TemporalReservoirBuffer m_temporalReservoir[2];

    eastl::unique_ptr<ReflectionDenoiser> m_pDenoiser; //todo : recurrent blur denoiser

    bool m_bEnable = true;
    bool m_bEnableReSTIR = true;
    bool m_bEnableDenoiser = true;
};
