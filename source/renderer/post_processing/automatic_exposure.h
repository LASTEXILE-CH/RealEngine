#pragma once

#include "../render_graph.h"
#include "../resource/typed_buffer.h"
#include "../resource/texture_2d.h"

class AutomaticExposure
{
public:
    AutomaticExposure(Renderer* pRenderer);

    RenderGraphHandle Render(RenderGraph* pRenderGraph, RenderGraphHandle sceneColorRT, uint32_t width, uint32_t height);

private:
    void ComputeLuminanceSize(uint32_t width, uint32_t height);
    void InitLuminance(IGfxCommandList* pCommandList, IGfxDescriptor* input, IGfxDescriptor* output);
    void ReduceLuminance(IGfxCommandList* pCommandList, RenderGraphTexture* texture);
    void Exposure(IGfxCommandList* pCommandList, IGfxDescriptor* avgLuminance, IGfxDescriptor* output);

private:
    Renderer* m_pRenderer;

    IGfxPipelineState* m_pLuminanceReductionPSO = nullptr;

    std::unique_ptr<TypedBuffer> m_pSPDCounterBuffer;
    std::unique_ptr<Texture2D> m_pPreviousEV100;

    enum class ExposureMode
    {
        Automatic,
        AutomaticHistogram,
        Manual,
    };

    enum class MeteringMode
    {
        Average,
        Spot,
        CenterWeighted,
    };

    ExposureMode m_exposuremode = ExposureMode::Automatic;
    MeteringMode m_meteringMode = MeteringMode::CenterWeighted;

    uint2 m_luminanceSize;
    uint32_t m_luminanceMips = 0;

    float m_minLuminance = 0.001f;
    float m_maxLuminance = 10.0f;
    float m_adaptionSpeed = 1.0f;
    bool m_bHistoryInvalid = true;
    bool m_bDebugEV100 = false;
};