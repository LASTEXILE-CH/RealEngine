#pragma once

#include "reflection_denoiser.h"

class HybridStochasticReflection
{
public:
    HybridStochasticReflection(Renderer* pRenderer);

    RenderGraphHandle Render(RenderGraph* pRenderGraph, RenderGraphHandle depth, RenderGraphHandle linear_depth, RenderGraphHandle normal, RenderGraphHandle velocity, uint32_t width, uint32_t height);

    IGfxDescriptor* GetOutputRadianceSRV() const { return m_pDenoiser->GetOutputRadianceSRV(); }
    float GetMaxRoughness() const { return m_maxRoughness; }

private:
    void ClassifyTiles(IGfxCommandList* pCommandList, RenderGraphTexture* depth, RenderGraphTexture* normal,
        RenderGraphBuffer* rayListUAV, RenderGraphBuffer* tileListUAV, RenderGraphBuffer* rayCounterUAV, uint32_t width, uint32_t height);
    void PrepareIndirectArgs(IGfxCommandList* pCommandList, RenderGraphBuffer* rayCounterSRV, RenderGraphBuffer* indirectArgsUAV, RenderGraphBuffer* denoiserArgsUAV);
    void SSR(IGfxCommandList* pCommandList, RenderGraphTexture* normal, RenderGraphTexture* depth, RenderGraphTexture* velocity, RenderGraphTexture* outputUAV,
        RenderGraphBuffer* rayCounter, RenderGraphBuffer* rayList, RenderGraphBuffer* indirectArgs, RenderGraphBuffer* hwRayCounterUAV, RenderGraphBuffer* hwRayListUAV);

    void PrepareRaytraceIndirectArgs(IGfxCommandList* pCommandList, RenderGraphBuffer* rayCounterSRV, RenderGraphBuffer* indirectArgsUAV);
    void Raytrace(IGfxCommandList* pCommandList, RenderGraphTexture* normal, RenderGraphTexture* depth, RenderGraphTexture* outputUAV,
        RenderGraphBuffer* rayCounter, RenderGraphBuffer* rayList, RenderGraphBuffer* indirectArgs);

    void SetRootConstants(IGfxCommandList* pCommandList);
private:
    Renderer* m_pRenderer;
    eastl::unique_ptr<ReflectionDenoiser> m_pDenoiser;

    IGfxPipelineState* m_pTileClassificationPSO = nullptr;
    IGfxPipelineState* m_pPrepareIndirectArgsPSO = nullptr;
    IGfxPipelineState* m_pSSRPSO = nullptr;

    IGfxPipelineState* m_pPrepareRTArgsPSO = nullptr;
    IGfxPipelineState* m_pRaytracePSO = nullptr;

    bool m_bEnable = true;
    bool m_bEnableSWRay = true;
    bool m_bEnableHWRay = true;
    bool m_bEnableDenoiser = true;

    uint m_samplesPerQuad = 1;
    float m_ssrThickness = 0.1f;
    float m_maxRoughness = 0.6f;
    float m_temporalStability = 0.5f;
};