#include "post_processor.h"
#include "../renderer.h"

PostProcessor::PostProcessor(Renderer* pRenderer)
{
    m_pRenderer = pRenderer;

    m_pTAA = eastl::make_unique<TAA>(pRenderer);
    m_pAutomaticExposure = eastl::make_unique<AutomaticExposure>(pRenderer);
    m_pBloom = eastl::make_unique<Bloom>(pRenderer);
    m_pToneMapper = eastl::make_unique<Tonemapper>(pRenderer);
    m_pFXAA = eastl::make_unique<FXAA>(pRenderer);
    m_pCAS = eastl::make_unique<CAS>(pRenderer);
}

RenderGraphHandle PostProcessor::Process(RenderGraph* pRenderGraph, const PostProcessInput& input, uint32_t width, uint32_t height)
{
    RENDER_GRAPH_EVENT(pRenderGraph, "PostProcess");

    RenderGraphHandle outputHandle = input.sceneColorRT;
    
    outputHandle = m_pTAA->Render(pRenderGraph, outputHandle, input.sceneDepthRT, input.linearDepthRT, input.velocityRT, width, height);
    //outputHandle = m_pDOF->Render(pRenderGraph, outputHandle)

    RenderGraphHandle exposure = m_pAutomaticExposure->Render(pRenderGraph, outputHandle, width, height);
    RenderGraphHandle bloom = m_pBloom->Render(pRenderGraph, outputHandle, width, height);

    outputHandle = m_pToneMapper->Render(pRenderGraph, outputHandle, exposure, bloom, m_pBloom->GetIntensity(), width, height);
    outputHandle = m_pFXAA->Render(pRenderGraph, outputHandle, width, height);
    outputHandle = m_pCAS->Render(pRenderGraph, outputHandle, width, height);

    return outputHandle;
}
