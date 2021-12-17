#pragma once

#include "render_graph.h"
#include "resource/typed_buffer.h"

class Renderer;

class HZBOcclusionCulling
{
public:
    HZBOcclusionCulling(Renderer* pRenderer);

    void GenerateHZB(RenderGraph* graph);

    uint32_t GetHZBMipCount() const { return m_nHZBMipCount; }
    RenderGraphHandle GetHZBMip(uint32_t mip) const;

private:
    void CalcHZBSize();

    void ReprojectDepth(IGfxCommandList* pCommandList, IGfxDescriptor* prevLinearDepthSRV, IGfxDescriptor* reprojectedDepthUAV);
    void DilateDepth(IGfxCommandList* pCommandList, IGfxDescriptor* reprojectedDepthSRV, IGfxDescriptor* hzbMip0UAV);
    void BuildHZB(IGfxCommandList* pCommandList, RenderGraphTexture* texture);

    void ResetCounterBuffer(IGfxCommandList* pCommandList);

private:
    Renderer* m_pRenderer = nullptr;

    IGfxPipelineState* m_pDepthReprojectionPSO = nullptr;
    IGfxPipelineState* m_pDepthDilationPSO = nullptr;
    IGfxPipelineState* m_pDepthMipFilterPSO = nullptr;

    uint32_t m_nHZBMipCount = 0;
    uint2 m_hzbSize;

    static const uint32_t MAX_HZB_MIP_COUNT = 13; //spd limits
    RenderGraphHandle m_hzbMips[MAX_HZB_MIP_COUNT] = {};

    std::unique_ptr<TypedBuffer> m_pSPDCounterBuffer;
};