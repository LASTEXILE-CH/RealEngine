#pragma once

#include "i_visible_object.h"

class SkySphere : public IVisibleObject
{
public:
    virtual void Load(tinyxml2::XMLElement* element) override {}
    virtual void Store(tinyxml2::XMLElement* element) override {}
    virtual bool Create() override;
    virtual void Tick(float delta_time) override;
    virtual void Render(Renderer* pRenderer) override;

private:
    void RenderSky(IGfxCommandList* pCommandList, Renderer* pRenderer, Camera* pCamera);

private:
    IGfxPipelineState* m_pPSO = nullptr;

    uint32_t m_nIndexCount = 0;
    std::unique_ptr<IGfxBuffer> m_pIndexBuffer;
    std::unique_ptr<IGfxBuffer> m_pVertexBuffer;
    std::unique_ptr<IGfxDescriptor> m_pVertexBufferSRV;
};