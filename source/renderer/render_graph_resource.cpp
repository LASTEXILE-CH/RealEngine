#include "render_graph_resource.h"
#include "render_graph.h"

void RenderGraphResource::Resolve(RenderGraphEdge* edge, RenderGraphPassBase* pass)
{
    if (pass->GetId() >= m_lastPass)
    {
        m_lastState = edge->GetUsage();
    }

    m_firstPass = eastl::min(m_firstPass, pass->GetId());
    m_lastPass = eastl::max(m_lastPass, pass->GetId());

    //for resources used in async compute, we should extend its lifetime range
    if (pass->GetType() == RenderPassType::AsyncCompute)
    {
        m_firstPass = eastl::min(m_firstPass, pass->GetWaitGraphicsPassID());
        m_lastPass = eastl::max(m_lastPass, pass->GetSignalGraphicsPassID());
    }
}

RGTexture::RGTexture(RenderGraphResourceAllocator& allocator, const eastl::string& name, const Desc& desc) :
    RenderGraphResource(name),
    m_allocator(allocator)
{
    m_desc = desc;
}

RGTexture::RGTexture(RenderGraphResourceAllocator& allocator, IGfxTexture* texture, GfxResourceState state) :
    RenderGraphResource(texture->GetName()),
    m_allocator(allocator)
{
    m_desc = texture->GetDesc();
    m_pTexture = texture;
    m_initialState = state;
    m_bImported = true;
}

RGTexture::~RGTexture()
{
    if (!m_bImported)
    {
        if (m_bOutput)
        {
            m_allocator.FreeNonOverlappingTexture(m_pTexture, m_lastState);
        }
        else
        {
            m_allocator.Free(m_pTexture, m_lastState);
        }
    }
}

IGfxDescriptor* RGTexture::GetSRV()
{
    RE_ASSERT(!IsImported()); 
    return m_allocator.GetDescriptor(m_pTexture, GfxShaderResourceViewDesc());
}

IGfxDescriptor* RGTexture::GetUAV()
{
    RE_ASSERT(!IsImported()); 
    return m_allocator.GetDescriptor(m_pTexture, GfxUnorderedAccessViewDesc());
}

IGfxDescriptor* RGTexture::GetUAV(uint32_t mip, uint32_t slice)
{
    RE_ASSERT(!IsImported());
    GfxUnorderedAccessViewDesc desc;
    desc.texture.mip_slice = mip;
    desc.texture.array_slice = slice;
    return m_allocator.GetDescriptor(m_pTexture, desc);
}

void RGTexture::Resolve(RenderGraphEdge* edge, RenderGraphPassBase* pass)
{
    RenderGraphResource::Resolve(edge, pass);

    GfxResourceState usage = edge->GetUsage();
    switch (usage)
    {
    case GfxResourceState::RenderTarget:
        m_desc.usage |= GfxTextureUsageRenderTarget;
        break;
    case GfxResourceState::UnorderedAccess:
        m_desc.usage |= GfxTextureUsageUnorderedAccess;
        break;
    case GfxResourceState::DepthStencil:
    case GfxResourceState::DepthStencilReadOnly:
        m_desc.usage |= GfxTextureUsageDepthStencil;
        break;
    default:
        break;
    }
}

void RGTexture::Realize()
{
    if (!m_bImported)
    {
        if (m_bOutput)
        {
            m_pTexture = m_allocator.AllocateNonOverlappingTexture(m_desc, m_name, m_initialState);
        }
        else
        {
            m_pTexture = m_allocator.AllocateTexture(m_firstPass, m_lastPass, m_desc, m_name, m_initialState);
        }
    }
}

IGfxResource* RGTexture::GetAliasedPrevResource()
{
    return m_allocator.GetAliasedPrevResource(m_pTexture, m_firstPass);
}

RGBuffer::RGBuffer(RenderGraphResourceAllocator& allocator, const eastl::string& name, const Desc& desc) :
    RenderGraphResource(name),
    m_allocator(allocator)
{
    m_desc = desc;
}

RGBuffer::RGBuffer(RenderGraphResourceAllocator& allocator, IGfxBuffer* buffer, GfxResourceState state) :
    RenderGraphResource(buffer->GetName()),
    m_allocator(allocator)
{
    m_desc = buffer->GetDesc();
    m_pBuffer = buffer;
    m_initialState = state;
    m_bImported = true;
}

RGBuffer::~RGBuffer()
{
    if (!m_bImported)
    {
        m_allocator.Free(m_pBuffer, m_lastState);
    }
}

IGfxDescriptor* RGBuffer::GetSRV()
{
    RE_ASSERT(!IsImported());

    const GfxBufferDesc& bufferDesc = m_pBuffer->GetDesc();
    GfxShaderResourceViewDesc desc;

    if (bufferDesc.usage & GfxBufferUsageStructuredBuffer)
    {
        desc.type = GfxShaderResourceViewType::StructuredBuffer;
    }
    else if (bufferDesc.usage & GfxBufferUsageTypedBuffer)
    {
        desc.type = GfxShaderResourceViewType::TypedBuffer;
    }
    else if (bufferDesc.usage & GfxBufferUsageRawBuffer)
    {
        desc.type = GfxShaderResourceViewType::RawBuffer;
    }

    desc.buffer.offset = 0;
    desc.buffer.size = bufferDesc.size;

    return m_allocator.GetDescriptor(m_pBuffer, desc);
}

IGfxDescriptor* RGBuffer::GetUAV()
{
    RE_ASSERT(!IsImported());

    const GfxBufferDesc& bufferDesc = m_pBuffer->GetDesc();
    RE_ASSERT(bufferDesc.usage & GfxBufferUsageUnorderedAccess);

    GfxUnorderedAccessViewDesc desc;

    if (bufferDesc.usage & GfxBufferUsageStructuredBuffer)
    {
        desc.type = GfxUnorderedAccessViewType::StructuredBuffer;
    }
    else if (bufferDesc.usage & GfxBufferUsageTypedBuffer)
    {
        desc.type = GfxUnorderedAccessViewType::TypedBuffer;
    }
    else if (bufferDesc.usage & GfxBufferUsageRawBuffer)
    {
        desc.type = GfxUnorderedAccessViewType::RawBuffer;
    }

    desc.buffer.offset = 0;
    desc.buffer.size = bufferDesc.size;

    return m_allocator.GetDescriptor(m_pBuffer, desc);
}

void RGBuffer::Resolve(RenderGraphEdge* edge, RenderGraphPassBase* pass)
{
    RenderGraphResource::Resolve(edge, pass);

    if (edge->GetUsage() == GfxResourceState::UnorderedAccess)
    {
        m_desc.usage |= GfxBufferUsageUnorderedAccess;
    }
}

void RGBuffer::Realize()
{
    if (!m_bImported)
    {
        m_pBuffer = m_allocator.AllocateBuffer(m_firstPass, m_lastPass, m_desc, m_name, m_initialState);
    }
}

IGfxResource* RGBuffer::GetAliasedPrevResource()
{
    return m_allocator.GetAliasedPrevResource(m_pBuffer, m_firstPass);
}
