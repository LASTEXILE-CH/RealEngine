#include "renderer.h"
#include "texture_loader.h"
#include "core/engine.h"
#include "utils/profiler.h"
#include "global_constants.hlsli"

Renderer::Renderer() : m_resizeConnection({})
{
    m_pShaderCompiler = std::make_unique<ShaderCompiler>();
    m_pShaderCache = std::make_unique<ShaderCache>(this);
    m_pPipelineCache = std::make_unique<PipelineStateCache>(this);

    m_resizeConnection = Engine::GetInstance()->WindowResizeSignal.connect(this, &Renderer::OnWindowResize);
}

Renderer::~Renderer()
{
    WaitGpuFinished();
    m_pRenderGraph->Clear();

    Engine::GetInstance()->WindowResizeSignal.disconnect(m_resizeConnection);
}

void Renderer::CreateDevice(void* window_handle, uint32_t window_width, uint32_t window_height, bool enable_vsync)
{
    m_nWindowWidth = window_width;
    m_nWindowHeight = window_height;

    GfxDeviceDesc desc;
    desc.max_frame_lag = MAX_INFLIGHT_FRAMES;
    m_pDevice.reset(CreateGfxDevice(desc));

    GfxSwapchainDesc swapchainDesc;
    swapchainDesc.window_handle = window_handle;
    swapchainDesc.width = window_width;
    swapchainDesc.height = window_height;
    swapchainDesc.enable_vsync = enable_vsync;
    m_pSwapchain.reset(m_pDevice->CreateSwapchain(swapchainDesc, "Renderer::m_pSwapchain"));

    m_pFrameFence.reset(m_pDevice->CreateFence("Renderer::m_pFrameFence"));

    for (int i = 0; i < MAX_INFLIGHT_FRAMES; ++i)
    {
        std::string name = "Renderer::m_pCommandLists[" + std::to_string(i) + "]";
        m_pCommandLists[i].reset(m_pDevice->CreateCommandList(GfxCommandQueue::Graphics, name));
    }

    for (int i = 0; i < MAX_INFLIGHT_FRAMES; ++i)
    {
        std::string name = "Renderer::m_pComputeCommandLists[" + std::to_string(i) + "]";
        m_pComputeCommandLists[i].reset(m_pDevice->CreateCommandList(GfxCommandQueue::Compute, name));
    }

    m_pUploadFence.reset(m_pDevice->CreateFence("Renderer::m_pUploadFence"));

    for (int i = 0; i < MAX_INFLIGHT_FRAMES; ++i)
    {
        std::string name = "Renderer::m_pUploadCommandList[" + std::to_string(i) + "]";
        m_pUploadCommandList[i].reset(m_pDevice->CreateCommandList(GfxCommandQueue::Copy, name));

        m_pStagingBufferAllocator[i] = std::make_unique<StagingBufferAllocator>(this);
    }

    CreateCommonResources();

    m_pRenderGraph.reset(new RenderGraph(this));
    m_pLightingProcessor.reset(new LightingProcessor(this));
    m_pPostProcessor.reset(new PostProcessor(this));
    m_pGpuDebugLine.reset(new GpuDebugLine(this));
}

void Renderer::RenderFrame()
{
    CPU_EVENT("Render", "Renderer::RenderFrame");

    BeginFrame();
    UploadResources();
    Render();
    EndFrame();

    MouseHitTest();
}

void Renderer::BeginFrame()
{
    CPU_EVENT("Render", "Renderer::BeginFrame");

    uint32_t frame_index = m_pDevice->GetFrameID() % MAX_INFLIGHT_FRAMES;
    {
        CPU_EVENT("Render", "IGfxFence::Wait");
        m_pFrameFence->Wait(m_nFrameFenceValue[frame_index]);
    }
    m_pDevice->BeginFrame();

    IGfxCommandList* pCommandList = m_pCommandLists[frame_index].get();
    pCommandList->ResetAllocator();
    pCommandList->Begin();

    IGfxCommandList* pComputeCommandList = m_pComputeCommandLists[frame_index].get();
    pComputeCommandList->ResetAllocator();
}

void Renderer::UploadResources()
{
    CPU_EVENT("Render", "Renderer::UploadResources");

    if (m_pendingTextureUploads.empty() && m_pendingBufferUpload.empty())
    {
        return;
    }

    uint32_t frame_index = m_pDevice->GetFrameID() % MAX_INFLIGHT_FRAMES;
    IGfxCommandList* pUploadCommandList = m_pUploadCommandList[frame_index].get();
    pUploadCommandList->Begin();

    {
        GPU_EVENT_DEBUG(pUploadCommandList, "Renderer::UploadResources");

        for (size_t i = 0; i < m_pendingTextureUploads.size(); ++i)
        {
            const TextureUpload& upload = m_pendingTextureUploads[i];
            pUploadCommandList->CopyBufferToTexture(upload.texture, upload.mip_level, upload.array_slice, 
                upload.staging_buffer.buffer, upload.staging_buffer.offset + upload.offset);
        }
        m_pendingTextureUploads.clear();

        for (size_t i = 0; i < m_pendingBufferUpload.size(); ++i)
        {
            const BufferUpload& upload = m_pendingBufferUpload[i];
            pUploadCommandList->CopyBuffer(upload.buffer, 0,
                upload.staging_buffer.buffer, upload.staging_buffer.offset, upload.staging_buffer.size);
        }
        m_pendingBufferUpload.clear();
    }

    pUploadCommandList->End();
    pUploadCommandList->Submit();

    m_nCurrentUploadFenceValue++;
    pUploadCommandList->Signal(m_pUploadFence.get(), m_nCurrentUploadFenceValue);

    IGfxCommandList* pCommandList = m_pCommandLists[frame_index].get();
    pCommandList->Wait(m_pUploadFence.get(), m_nCurrentUploadFenceValue);
}

void Renderer::FlushComputePass(IGfxCommandList* pCommandList)
{
    GPU_EVENT(pCommandList, "ComputePasses");

    for (size_t i = 0; i < m_computeBuffers.size(); ++i)
    {
        pCommandList->ResourceBarrier(m_computeBuffers[i], 0, GfxResourceState::ShaderResourceNonPS, GfxResourceState::UnorderedAccess);
    }

    for (size_t i = 0; i < m_computePassBatchs.size(); ++i)
    {
        m_computePassBatchs[i](pCommandList);
    }
    m_computePassBatchs.clear();

    for (size_t i = 0; i < m_computeBuffers.size(); ++i)
    {
        pCommandList->ResourceBarrier(m_computeBuffers[i], 0, GfxResourceState::UnorderedAccess, GfxResourceState::ShaderResourceNonPS);
    }
    m_computeBuffers.clear();
}

void Renderer::SetupGlobalConstants(IGfxCommandList* pCommandList)
{
    World* world = Engine::GetInstance()->GetWorld();
    Camera* camera = world->GetCamera();
    ILight* light = world->GetPrimaryLight();

    camera->SetupCameraCB(pCommandList);

    SceneConstant sceneCB;
    sceneCB.lightDir = light->GetLightDirection();
    //sceneCB.shadowRT = shadowMapRT->GetSRV()->GetHeapIndex();
    sceneCB.lightColor = light->GetLightColor() * light->GetLightIntensity();
    sceneCB.shadowSampler = m_pShadowSampler->GetHeapIndex();
    sceneCB.mtxLightVP = light->GetShadowMatrix();
    sceneCB.viewWidth = m_nWindowWidth;
    sceneCB.viewHeight = m_nWindowHeight;
    sceneCB.rcpViewWidth = 1.0f / m_nWindowWidth;
    sceneCB.rcpViewHeight = 1.0f / m_nWindowHeight;
    sceneCB.debugLineDrawCommandUAV = m_pGpuDebugLine->GetArugumentsBufferUAV()->GetHeapIndex();
    sceneCB.debugLineVertexBufferSRV = m_pGpuDebugLine->GetVertexBufferSRV()->GetHeapIndex();
    sceneCB.debugLineVertexBufferUAV = m_pGpuDebugLine->GetVertexBufferUAV()->GetHeapIndex();
    sceneCB.pointRepeatSampler = m_pPointRepeatSampler->GetHeapIndex();
    sceneCB.pointClampSampler = m_pPointClampSampler->GetHeapIndex();
    sceneCB.linearRepeatSampler = m_pLinearRepeatSampler->GetHeapIndex();
    sceneCB.linearClampSampler = m_pLinearClampSampler->GetHeapIndex();
    sceneCB.aniso2xSampler = m_pAniso2xSampler->GetHeapIndex();
    sceneCB.aniso4xSampler = m_pAniso4xSampler->GetHeapIndex();
    sceneCB.aniso8xSampler = m_pAniso8xSampler->GetHeapIndex();
    sceneCB.aniso16xSampler = m_pAniso16xSampler->GetHeapIndex();
    sceneCB.envTexture = m_pEnvTexture->GetSRV()->GetHeapIndex();
    sceneCB.brdfTexture = m_pBrdfTexture->GetSRV()->GetHeapIndex();

    pCommandList->SetGraphicsConstants(4, &sceneCB, sizeof(sceneCB));
    pCommandList->SetComputeConstants(4, &sceneCB, sizeof(sceneCB));
}

void Renderer::ImportPrevFrameTextures()
{
    if (m_pPrevLinearDepthTexture == nullptr ||
        m_pPrevLinearDepthTexture->GetTexture()->GetDesc().width != m_nWindowWidth ||
        m_pPrevLinearDepthTexture->GetTexture()->GetDesc().height != m_nWindowHeight)
    {
        m_pPrevLinearDepthTexture.reset(CreateTexture2D(m_nWindowWidth, m_nWindowHeight, 1, GfxFormat::R32F, GfxTextureUsageShaderResource, "Prev LinearDepth"));
    }

    m_prevLinearDepthHandle = m_pRenderGraph->Import(m_pPrevLinearDepthTexture->GetTexture(), GfxResourceState::CopyDst);
}

void Renderer::Render()
{
    CPU_EVENT("Render", "Renderer::Render");

    uint32_t frame_index = m_pDevice->GetFrameID() % MAX_INFLIGHT_FRAMES;
    IGfxCommandList* pCommandList = m_pCommandLists[frame_index].get();
    IGfxCommandList* pComputeCommandList = m_pComputeCommandLists[frame_index].get();

    std::string event_name = "Render Frame " + std::to_string(m_pDevice->GetFrameID());
    GPU_EVENT_DEBUG(pCommandList, event_name.c_str());

    GPU_EVENT_PROFILER(pCommandList, "Render Frame");

    m_pGpuDebugLine->Clear(pCommandList);
    FlushComputePass(pCommandList);
    SetupGlobalConstants(pCommandList);

    m_pRenderGraph->Clear();
    
    ImportPrevFrameTextures();

    RenderGraphHandle outputColorHandle, outputDepthHandle;
    BuildRenderGraph(outputColorHandle, outputDepthHandle);

    m_pRenderGraph->Compile();
    m_pRenderGraph->Execute(pCommandList, pComputeCommandList);

    RenderBackbufferPass(pCommandList, outputColorHandle, outputDepthHandle);
}

void Renderer::RenderBackbufferPass(IGfxCommandList* pCommandList, RenderGraphHandle colorRTHandle, RenderGraphHandle depthRTHandle)
{
    GPU_EVENT(pCommandList, "Backbuffer Pass");

    pCommandList->ResourceBarrier(m_pSwapchain->GetBackBuffer(), 0, GfxResourceState::Present, GfxResourceState::RenderTarget);
    m_pGpuDebugLine->BarrierForDraw(pCommandList);

    RenderGraphTexture* depthRT = (RenderGraphTexture*)m_pRenderGraph->GetResource(depthRTHandle);

    GfxRenderPassDesc render_pass;
    render_pass.color[0].texture = m_pSwapchain->GetBackBuffer();
    render_pass.color[0].load_op = GfxRenderPassLoadOp::DontCare;
    render_pass.depth.texture = depthRT->GetTexture();
    render_pass.depth.store_op = GfxRenderPassStoreOp::DontCare;
    render_pass.depth.stencil_store_op = GfxRenderPassStoreOp::DontCare;
    pCommandList->BeginRenderPass(render_pass);

    CopyToBackbuffer(pCommandList, colorRTHandle);
    m_pGpuDebugLine->Draw(pCommandList);
    Engine::GetInstance()->GetGUI()->Render(pCommandList);

    pCommandList->EndRenderPass();
    pCommandList->ResourceBarrier(m_pSwapchain->GetBackBuffer(), 0, GfxResourceState::RenderTarget, GfxResourceState::Present);
}

void Renderer::CopyToBackbuffer(IGfxCommandList* pCommandList, RenderGraphHandle colorRTHandle)
{
    GPU_EVENT(pCommandList, "CopyToBackbuffer");

    RenderGraphTexture* inputRT = (RenderGraphTexture*)m_pRenderGraph->GetResource(colorRTHandle);
    uint32_t constants[4] = { inputRT->GetSRV()->GetHeapIndex(), m_pPointClampSampler->GetHeapIndex(), 0, 0 };
    pCommandList->SetGraphicsConstants(0, constants, sizeof(constants));
    pCommandList->SetPipelineState(m_pCopyPSO);
    pCommandList->Draw(3);
}

void Renderer::EndFrame()
{
    CPU_EVENT("Render", "Renderer::EndFrame");

    uint32_t frame_index = m_pDevice->GetFrameID() % MAX_INFLIGHT_FRAMES;
    IGfxCommandList* pCommandList = m_pCommandLists[frame_index].get();
    pCommandList->End();

    ++m_nCurrentFrameFenceValue;
    m_nFrameFenceValue[frame_index] = m_nCurrentFrameFenceValue;

    pCommandList->Submit();
    {
        CPU_EVENT("Render", "IGfxSwapchain::Present");
        m_pSwapchain->Present();
    }
    pCommandList->Signal(m_pFrameFence.get(), m_nCurrentFrameFenceValue);

    m_pStagingBufferAllocator[frame_index]->Reset();

    m_pDevice->EndFrame();
}

void Renderer::WaitGpuFinished()
{
    m_pFrameFence->Wait(m_nCurrentFrameFenceValue);
}

void Renderer::RequestMouseHitTest(uint32_t x, uint32_t y)
{
    m_nMouseX = x;
    m_nMouseY = y;
    m_bEnableObjectIDRendering = true;
}

void Renderer::MouseHitTest()
{
    if (m_bEnableObjectIDRendering)
    {
        WaitGpuFinished();

        uint8_t* data = (uint8_t*)m_pObjectIDBuffer->GetCpuAddress();
        uint32_t data_offset = m_nObjectIDRowPitch * m_nMouseY + m_nMouseX * sizeof(uint32_t);
        memcpy(&m_nMouseHitObjectID, data + data_offset, sizeof(uint32_t));

        m_bEnableObjectIDRendering = false;
    }
}

IGfxShader* Renderer::GetShader(const std::string& file, const std::string& entry_point, const std::string& profile, const std::vector<std::string>& defines)
{
    return m_pShaderCache->GetShader(file, entry_point, profile, defines);
}

IGfxPipelineState* Renderer::GetPipelineState(const GfxGraphicsPipelineDesc& desc, const std::string& name)
{
    return m_pPipelineCache->GetPipelineState(desc, name);
}

IGfxPipelineState* Renderer::GetPipelineState(const GfxComputePipelineDesc& desc, const std::string& name)
{
    return m_pPipelineCache->GetPipelineState(desc, name);
}

void Renderer::CreateCommonResources()
{
    GfxSamplerDesc desc;
    m_pPointRepeatSampler.reset(m_pDevice->CreateSampler(desc, "Renderer::m_pPointRepeatSampler"));

    desc.min_filter = GfxFilter::Linear;
    desc.mag_filter = GfxFilter::Linear;
    desc.mip_filter = GfxFilter::Linear;
    m_pLinearRepeatSampler.reset(m_pDevice->CreateSampler(desc, "Renderer::m_pLinearRepeatSampler"));

    desc.min_filter = GfxFilter::Point;
    desc.mag_filter = GfxFilter::Point;
    desc.mip_filter = GfxFilter::Point;
    desc.address_u = GfxSamplerAddressMode::ClampToEdge;
    desc.address_v = GfxSamplerAddressMode::ClampToEdge;
    desc.address_w = GfxSamplerAddressMode::ClampToEdge;
    m_pPointClampSampler.reset(m_pDevice->CreateSampler(desc, "Renderer::m_pPointClampSampler"));

    desc.min_filter = GfxFilter::Linear;
    desc.mag_filter = GfxFilter::Linear;
    desc.mip_filter = GfxFilter::Linear;
    m_pLinearClampSampler.reset(m_pDevice->CreateSampler(desc, "Renderer::m_pLinearClampSampler"));

    desc.min_filter = GfxFilter::Linear;
    desc.mag_filter = GfxFilter::Linear;
    desc.mip_filter = GfxFilter::Point;
    desc.address_u = GfxSamplerAddressMode::ClampToEdge;
    desc.address_v = GfxSamplerAddressMode::ClampToEdge;
    desc.address_w = GfxSamplerAddressMode::ClampToEdge;
    desc.enable_compare = true;
    desc.compare_func = GfxCompareFunc::LessEqual;
    m_pShadowSampler.reset(m_pDevice->CreateSampler(desc, "Renderer::m_pShadowSampler"));

    desc.min_filter = GfxFilter::Linear;
    desc.mag_filter = GfxFilter::Linear;
    desc.mip_filter = GfxFilter::Linear;
    desc.address_u = GfxSamplerAddressMode::Repeat;
    desc.address_v = GfxSamplerAddressMode::Repeat;
    desc.address_w = GfxSamplerAddressMode::Repeat;
    desc.enable_anisotropy = true;
    desc.max_anisotropy = 2.0f;
    m_pAniso2xSampler.reset(m_pDevice->CreateSampler(desc, "Renderer::m_pAniso2xSampler"));

    desc.max_anisotropy = 4.0f;
    m_pAniso4xSampler.reset(m_pDevice->CreateSampler(desc, "Renderer::m_pAniso4xSampler"));

    desc.max_anisotropy = 8.0f;
    m_pAniso8xSampler.reset(m_pDevice->CreateSampler(desc, "Renderer::m_pAniso8xSampler"));

    desc.max_anisotropy = 16.0f;
    m_pAniso16xSampler.reset(m_pDevice->CreateSampler(desc, "Renderer::m_pAniso16xSampler"));
    
    std::string asset_path = Engine::GetInstance()->GetAssetPath();
    m_pBrdfTexture = CreateTexture2D(asset_path + "textures/PreintegratedGF.dds", false);
    m_pEnvTexture.reset(CreateTextureCube(asset_path + "textures/output_pmrem.dds"));

    GfxGraphicsPipelineDesc psoDesc;
    psoDesc.vs = GetShader("copy.hlsl", "vs_main", "vs_6_6", {});
    psoDesc.ps = GetShader("copy.hlsl", "ps_main", "ps_6_6", {});
    psoDesc.rt_format[0] = m_pSwapchain->GetDesc().backbuffer_format;
    m_pCopyPSO = GetPipelineState(psoDesc, "Copy PSO");
}

void Renderer::OnWindowResize(void* window, uint32_t width, uint32_t height)
{
    WaitGpuFinished();

    if (m_pSwapchain->GetDesc().window_handle == window)
    {
        m_pSwapchain->Resize(width, height);

        m_nWindowWidth = width;
        m_nWindowHeight = height;
    }
}

IndexBuffer* Renderer::CreateIndexBuffer(void* data, uint32_t stride, uint32_t index_count, const std::string& name, GfxMemoryType memory_type)
{
    IndexBuffer* buffer = new IndexBuffer(name);
    if (!buffer->Create(stride, index_count, memory_type))
    {
        delete buffer;
        return nullptr;
    }

    if (data)
    {
        UploadBuffer(buffer->GetBuffer(), data, stride * index_count);
    }

    return buffer;
}

StructuredBuffer* Renderer::CreateStructuredBuffer(void* data, uint32_t stride, uint32_t element_count, const std::string& name, GfxMemoryType memory_type, bool uav)
{
    StructuredBuffer* buffer = new StructuredBuffer(name);
    if (!buffer->Create(stride, element_count, memory_type, uav))
    {
        delete buffer;
        return nullptr;
    }

    if (data)
    {
        UploadBuffer(buffer->GetBuffer(), data, stride * element_count);
    }

    return buffer;
}

RawBuffer* Renderer::CreateRawBuffer(void* data, uint32_t size, const std::string& name, GfxMemoryType memory_type, bool uav)
{
    RawBuffer* buffer = new RawBuffer(name);
    if (!buffer->Create(size, memory_type, uav))
    {
        delete buffer;
        return nullptr;
    }

    if (data)
    {
        UploadBuffer(buffer->GetBuffer(), data, size);
    }

    return buffer;
}

Texture2D* Renderer::CreateTexture2D(const std::string& file, bool srgb, bool cached)
{
    if (cached)
    {
        auto iter = m_cachedTextures.find(file);
        if (iter != m_cachedTextures.end())
        {
            return iter->second.get();
        }
    }

    TextureLoader loader;
    if (!loader.Load(file, srgb))
    {
        return nullptr;
    }

    Texture2D* texture = new Texture2D(file);
    if (!texture->Create(loader.GetWidth(), loader.GetHeight(), loader.GetMipLevels(), loader.GetFormat(), GfxTextureUsageShaderResource))
    {
        delete texture;
        return nullptr;
    }

    UploadTexture(texture->GetTexture(), loader.GetData());

    if (cached)
    {
        m_cachedTextures.insert(std::make_pair(file, std::unique_ptr<Texture2D>(texture)));
    }

    return texture;
}

Texture2D* Renderer::CreateTexture2D(uint32_t width, uint32_t height, uint32_t levels, GfxFormat format, GfxTextureUsageFlags flags, const std::string& name)
{
    Texture2D* texture = new Texture2D(name);
    if (!texture->Create(width, height, levels, format, flags))
    {
        delete texture;
        return nullptr;
    }
    return texture;
}

TextureCube* Renderer::CreateTextureCube(const std::string& file, bool srgb)
{
    TextureLoader loader;
    if (!loader.Load(file, srgb))
    {
        return nullptr;
    }

    TextureCube* texture = new TextureCube(file);
    if (!texture->Create(loader.GetWidth(), loader.GetHeight(), loader.GetMipLevels(), loader.GetFormat(), GfxTextureUsageShaderResource))
    {
        delete texture;
        return nullptr;
    }

    UploadTexture(texture->GetTexture(), loader.GetData());

    return texture;
}

inline void image_copy(char* dst_data, uint32_t dst_row_pitch, char* src_data, uint32_t src_row_pitch, uint32_t row_num, uint32_t d)
{
    uint32_t src_slice_size = src_row_pitch * row_num;
    uint32_t dst_slice_size = dst_row_pitch * row_num;

    for (uint32_t z = 0; z < d; z++)
    {
        char* dst_slice = dst_data + dst_slice_size * z;
        char* src_slice = src_data + src_slice_size * z;

        for (uint32_t row = 0; row < row_num; ++row)
        {
            memcpy(dst_slice + dst_row_pitch * row,
                src_slice + src_row_pitch * row,
                src_row_pitch);
        }
    }
}

void Renderer::UploadTexture(IGfxTexture* texture, void* data)
{
    uint32_t frame_index = m_pDevice->GetFrameID() % MAX_INFLIGHT_FRAMES;
    StagingBufferAllocator* pAllocator = m_pStagingBufferAllocator[frame_index].get();

    uint32_t required_size = texture->GetRequiredStagingBufferSize();
    StagingBuffer buffer = pAllocator->Allocate(required_size);

    const GfxTextureDesc& desc = texture->GetDesc();

    char* dst_data = (char*)buffer.buffer->GetCpuAddress() + buffer.offset;
    uint32_t dst_offset = 0;
    uint32_t src_offset = 0;

    for (uint32_t slice = 0; slice < desc.array_size; ++slice)
    {
        for (uint32_t mip = 0; mip < desc.mip_levels; ++mip)
        {
            uint32_t min_width = GetFormatBlockWidth(desc.format);
            uint32_t min_height = GetFormatBlockHeight(desc.format);
            uint32_t w = max(desc.width >> mip, min_width);
            uint32_t h = max(desc.height >> mip, min_height);
            uint32_t d = max(desc.depth >> mip, 1);

            uint32_t src_row_pitch = GetFormatRowPitch(desc.format, w) * GetFormatBlockHeight(desc.format);
            uint32_t dst_row_pitch = texture->GetRowPitch(mip);

            uint32_t row_num = h / GetFormatBlockHeight(desc.format);

            image_copy(dst_data + dst_offset, dst_row_pitch,
                (char*)data + src_offset, src_row_pitch,
                row_num, d);

            TextureUpload upload;
            upload.texture = texture;
            upload.mip_level = mip;
            upload.array_slice = slice;
            upload.staging_buffer = buffer;
            upload.offset = dst_offset;
            m_pendingTextureUploads.push_back(upload);

#define ALIGN(address, alignment) (((address) + (alignment) - 1) & ~((alignment) - 1)) 

            dst_offset += ALIGN(dst_row_pitch * row_num, 512); //512 : D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT
            src_offset += src_row_pitch * row_num;
        }
    }
}

void Renderer::UploadBuffer(IGfxBuffer* buffer, void* data, uint32_t data_size)
{
    uint32_t frame_index = m_pDevice->GetFrameID() % MAX_INFLIGHT_FRAMES;
    StagingBufferAllocator* pAllocator = m_pStagingBufferAllocator[frame_index].get();

    uint32_t required_size = buffer->GetRequiredStagingBufferSize();
    StagingBuffer staging_buffer = pAllocator->Allocate(required_size);

    char* dst_data = (char*)staging_buffer.buffer->GetCpuAddress() + staging_buffer.offset;
    memcpy(dst_data, data, data_size);

    BufferUpload upload;
    upload.buffer = buffer;
    upload.staging_buffer = staging_buffer;
    m_pendingBufferUpload.push_back(upload);
}
