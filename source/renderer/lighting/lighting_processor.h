#pragma once

#include "gtao.h"
#include "ray_traced_shadow.h"
#include "clustered_shading.h"
#include "hybrid_stochastic_reflection.h"

class LightingProcessor
{
public:
    LightingProcessor(Renderer* pRenderer);

    RenderGraphHandle Process(RenderGraph* pRenderGraph, uint32_t width, uint32_t height);
    
private:
    RenderGraphHandle CompositeLight(RenderGraph* pRenderGraph, RenderGraphHandle ao, RenderGraphHandle direct_lighting, uint32_t width, uint32_t height);

private:
    Renderer* m_pRenderer;

    std::unique_ptr<GTAO> m_pGTAO;
    std::unique_ptr<RTShadow> m_pRTShdow;
    std::unique_ptr<HybridStochasticReflection> m_pReflection;
    std::unique_ptr<ClusteredShading> m_pClusteredShading;
};