#pragma once

#include "gfx/gfx.h"

class RenderGraphResourceAllocator
{
public:
    RenderGraphResourceAllocator(IGfxDevice* pDevice);
    ~RenderGraphResourceAllocator();

    void Reset();

    IGfxTexture* AllocateTexture(const GfxTextureDesc& desc, const std::string& name, GfxResourceState& initial_state);
    void Free(IGfxTexture* texture, GfxResourceState state);

    IGfxDescriptor* GetDescriptor(IGfxResource* resource, const GfxShaderResourceViewDesc& desc);

private:
    void DeleteDescriptor(IGfxResource* resource);

private:
    IGfxDevice* m_pDevice;

    struct FreeTexture
    {
        uint64_t lastUsedFrame;
        GfxResourceState lastUsedState;
        IGfxTexture* texture;
    };
    std::vector<FreeTexture> m_freeTextures;

    struct SRVDescriptor
    {
        IGfxResource* resource;
        IGfxDescriptor* descriptor;
        GfxShaderResourceViewDesc desc;
    };
    std::vector<SRVDescriptor> m_allocatedSRVs;
};