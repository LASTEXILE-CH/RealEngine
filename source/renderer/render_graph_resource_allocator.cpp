#include "render_graph_resource_allocator.h"

RenderGraphResourceAllocator::RenderGraphResourceAllocator(IGfxDevice* pDevice)
{
    m_pDevice = pDevice;
}

RenderGraphResourceAllocator::~RenderGraphResourceAllocator()
{
    for (auto iter = m_freeTextures.begin(); iter != m_freeTextures.end(); ++iter)
    {
        DeleteDescriptor(iter->texture);
        delete iter->texture;
    }
}

void RenderGraphResourceAllocator::Reset()
{
    uint64_t current_frame = m_pDevice->GetFrameID();

    for (auto iter = m_freeTextures.begin(); iter != m_freeTextures.end(); )
    {
        if (current_frame - iter->lastUsedFrame > 30)
        {
            DeleteDescriptor(iter->texture);

            delete iter->texture;
            iter = m_freeTextures.erase(iter);
        }
        else
        {
            ++iter;
        }
    }
}

IGfxTexture* RenderGraphResourceAllocator::AllocateTexture(const GfxTextureDesc& desc, const std::string& name, GfxResourceState& initial_state)
{
    for (auto iter = m_freeTextures.begin(); iter != m_freeTextures.end(); ++iter)
    {
        IGfxTexture* texture = iter->texture;
        if (texture->GetDesc() == desc)
        {
            initial_state = iter->lastUsedState;

            m_freeTextures.erase(iter);
            return texture;
        }
    }

    if (IsDepthFormat(desc.format))
    {
        initial_state = GfxResourceState::DepthStencil;
    }
    else if (desc.usage & GfxTextureUsageRenderTarget)
    {
        initial_state = GfxResourceState::RenderTarget;
    }

    return m_pDevice->CreateTexture(desc, "RGTexture " + name);
}

void RenderGraphResourceAllocator::Free(IGfxTexture* texture, GfxResourceState state)
{
    if (texture != nullptr)
    {
        m_freeTextures.push_back({ m_pDevice->GetFrameID(), state, texture });
    }
}

IGfxDescriptor* RenderGraphResourceAllocator::GetDescriptor(IGfxResource* resource, const GfxShaderResourceViewDesc& desc)
{
    for (size_t i = 0; i < m_allocatedSRVs.size(); ++i)
    {
        if (m_allocatedSRVs[i].resource == resource &&
            m_allocatedSRVs[i].desc == desc)
        {
            return m_allocatedSRVs[i].descriptor;
        }
    }

    IGfxDescriptor* srv = m_pDevice->CreateShaderResourceView(resource, desc, resource->GetName());
    m_allocatedSRVs.push_back({ resource, srv, desc });

    return srv;
}

IGfxDescriptor* RenderGraphResourceAllocator::GetDescriptor(IGfxResource* resource, const GfxUnorderedAccessViewDesc& desc)
{
    for (size_t i = 0; i < m_allocatedUAVs.size(); ++i)
    {
        if (m_allocatedUAVs[i].resource == resource &&
            m_allocatedUAVs[i].desc == desc)
        {
            return m_allocatedUAVs[i].descriptor;
        }
    }

    IGfxDescriptor* srv = m_pDevice->CreateUnorderedAccessView(resource, desc, resource->GetName());
    m_allocatedUAVs.push_back({ resource, srv, desc });

    return srv;
}

void RenderGraphResourceAllocator::DeleteDescriptor(IGfxResource* resource)
{
    for (auto iter = m_allocatedSRVs.begin(); iter != m_allocatedSRVs.end(); )
    {
        if (iter->resource == resource)
        {
            delete iter->descriptor;
            iter = m_allocatedSRVs.erase(iter);
        }
        else
        {
            ++iter;
        }
    }

    for (auto iter = m_allocatedUAVs.begin(); iter != m_allocatedUAVs.end(); )
    {
        if (iter->resource == resource)
        {
            delete iter->descriptor;
            iter = m_allocatedUAVs.erase(iter);
        }
        else
        {
            ++iter;
        }
    }
}
