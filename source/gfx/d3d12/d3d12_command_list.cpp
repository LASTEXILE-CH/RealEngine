#include "d3d12_command_list.h"
#include "d3d12_device.h"
#include "d3d12_fence.h"
#include "d3d12_texture.h"
#include "d3d12_buffer.h"
#include "d3d12_pipeline_state.h"
#include "pix_runtime.h"
#include "../gfx.h"
#include "utils/assert.h"

D3D12CommandList::D3D12CommandList(IGfxDevice* pDevice, GfxCommandQueue queue_type, const std::string& name)
{
	m_pDevice = pDevice;
	m_name = name;
	m_queueType = queue_type;
}

D3D12CommandList::~D3D12CommandList()
{
	D3D12Device* pDevice = (D3D12Device*)m_pDevice;
	pDevice->Delete(m_pCommandAllocator);
	pDevice->Delete(m_pCommandList);
}

bool D3D12CommandList::Create()
{
	D3D12Device* pDevice = (D3D12Device*)m_pDevice;
	D3D12_COMMAND_LIST_TYPE type;

	switch (m_queueType)
	{
	case GfxCommandQueue::Graphics:
		type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		m_pCommandQueue = pDevice->GetGraphicsQueue();
		break;
	case GfxCommandQueue::Compute:
		//todo
		RE_ASSERT(false);
		break;
	case GfxCommandQueue::Copy:
		type = D3D12_COMMAND_LIST_TYPE_COPY;
		m_pCommandQueue = pDevice->GetCopyQueue();
		break;
	default:
		break;
	}

	ID3D12Device* pD3D12Device = (ID3D12Device*)pDevice->GetHandle();
	HRESULT hr = pD3D12Device->CreateCommandAllocator(type, IID_PPV_ARGS(&m_pCommandAllocator));
	if (FAILED(hr))
	{
		return false;
	}
	m_pCommandAllocator->SetName(string_to_wstring(m_name + " allocator").c_str());

	hr = pD3D12Device->CreateCommandList(0, type, m_pCommandAllocator, nullptr, IID_PPV_ARGS(&m_pCommandList));
	if (FAILED(hr))
	{
		return false;
	}

	m_pCommandList->SetName(string_to_wstring(m_name).c_str());
	m_pCommandList->Close();

	return true;
}

void D3D12CommandList::Begin()
{
	m_pCurrentPSO = nullptr;

	m_pCommandAllocator->Reset();
	m_pCommandList->Reset(m_pCommandAllocator, nullptr);

	if (m_queueType == GfxCommandQueue::Graphics || m_queueType == GfxCommandQueue::Compute)
	{
		D3D12Device* pDevice = (D3D12Device*)m_pDevice;
		ID3D12DescriptorHeap* heaps[2] = { pDevice->GetResourceDescriptorHeap(), pDevice->GetSamplerDescriptorHeap() };
		m_pCommandList->SetDescriptorHeaps(2, heaps);

		ID3D12RootSignature* pRootSignature = pDevice->GetRootSignature();
		m_pCommandList->SetComputeRootSignature(pRootSignature);

		if(m_queueType == GfxCommandQueue::Graphics)
		{
			m_pCommandList->SetGraphicsRootSignature(pRootSignature);
		}
	}
}

void D3D12CommandList::End()
{
	FlushPendingBarrier();

	m_pCommandList->Close();
}

void D3D12CommandList::BeginEvent(const std::string& event_name)
{
	pix::BeginEvent(m_pCommandList, event_name.c_str());
}

void D3D12CommandList::EndEvent()
{
	pix::EndEvent(m_pCommandList);
}

void D3D12CommandList::CopyBufferToTexture(IGfxTexture* texture, uint32_t mip_level, uint32_t array_slice, IGfxBuffer* buffer, uint32_t offset)
{
	const GfxTextureDesc& desc = texture->GetDesc();
	uint32_t subresource = array_slice * desc.mip_levels + mip_level;

	D3D12_TEXTURE_COPY_LOCATION dst = {};
	dst.pResource = (ID3D12Resource*)texture->GetHandle();
	dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dst.SubresourceIndex = subresource;

	uint32_t min_width = GetFormatBlockWidth(desc.format);
	uint32_t min_height = GetFormatBlockHeight(desc.format);
	uint32_t w = max(desc.width >> mip_level, min_width);
	uint32_t h = max(desc.height >> mip_level, min_height);
	uint32_t d = max(desc.depth >> mip_level, 1);

	D3D12_TEXTURE_COPY_LOCATION src = {};
	src.pResource = (ID3D12Resource*)buffer->GetHandle();
	src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	src.PlacedFootprint.Offset = offset;
	src.PlacedFootprint.Footprint.Format = dxgi_format(desc.format);
	src.PlacedFootprint.Footprint.Width = w;
	src.PlacedFootprint.Footprint.Height = h;
	src.PlacedFootprint.Footprint.Depth = d;
	src.PlacedFootprint.Footprint.RowPitch = GetFormatRowPitch(desc.format, w);

	m_pCommandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
}

void D3D12CommandList::Wait(IGfxFence* fence, uint64_t value)
{
	m_pCommandQueue->Wait((ID3D12Fence*)fence->GetHandle(), value);
}

void D3D12CommandList::Signal(IGfxFence* fence, uint64_t value)
{
	m_pCommandQueue->Signal((ID3D12Fence*)fence->GetHandle(), value);
}

void D3D12CommandList::Submit()
{
	ID3D12CommandList* ppCommandLists[] = { m_pCommandList };
	m_pCommandQueue->ExecuteCommandLists(1, ppCommandLists);
}

void D3D12CommandList::ResourceBarrier(IGfxResource* resource, uint32_t sub_resource, GfxResourceState old_state, GfxResourceState new_state)
{
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = (ID3D12Resource*)resource->GetHandle();
	barrier.Transition.Subresource = sub_resource;
	barrier.Transition.StateBefore = d3d12_resource_state(old_state);
	barrier.Transition.StateAfter = d3d12_resource_state(new_state);

	m_pendingBarriers.push_back(barrier);
}

void D3D12CommandList::UavBarrier(IGfxResource* resource)
{
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	barrier.UAV.pResource = (ID3D12Resource*)resource->GetHandle();

	m_pendingBarriers.push_back(barrier);
}

void D3D12CommandList::BeginRenderPass(const GfxRenderPassDesc& render_pass)
{
	FlushPendingBarrier();

	D3D12_RENDER_PASS_RENDER_TARGET_DESC rtDesc[8] = {};
	D3D12_RENDER_PASS_DEPTH_STENCIL_DESC dsDesc = {};

	unsigned int rt_count = 0;
	for (int i = 0; i < 8; ++i)
	{
		if (render_pass.color[i].texture == nullptr)
		{
			break;
		}

		rtDesc[i].cpuDescriptor = ((D3D12Texture*)render_pass.color[i].texture)->GetRTV(render_pass.color[i].mip_slice, render_pass.color[i].array_slice);
		rtDesc[i].BeginningAccess.Type = d3d12_render_pass_loadop(render_pass.color[i].load_op);
		rtDesc[i].BeginningAccess.Clear.ClearValue.Format = dxgi_format(render_pass.color[i].texture->GetDesc().format);
		memcpy(rtDesc[i].BeginningAccess.Clear.ClearValue.Color, render_pass.color[i].clear_color.value, sizeof(float) * 4);
		rtDesc[i].EndingAccess.Type = d3d12_render_pass_storeop(render_pass.color[i].store_op);

		++rt_count;
	}

	if (render_pass.depth.texture != nullptr)
	{
		dsDesc.cpuDescriptor = ((D3D12Texture*)render_pass.depth.texture)->GetDSV(render_pass.depth.mip_slice, render_pass.depth.array_slice);
		dsDesc.DepthBeginningAccess.Type = d3d12_render_pass_loadop(render_pass.depth.load_op);
		dsDesc.DepthBeginningAccess.Clear.ClearValue.Format = dxgi_format(render_pass.depth.texture->GetDesc().format);
		dsDesc.DepthBeginningAccess.Clear.ClearValue.DepthStencil.Depth = render_pass.depth.clear_depth;
		dsDesc.DepthBeginningAccess.Clear.ClearValue.DepthStencil.Stencil = render_pass.depth.clear_stencil;

		dsDesc.StencilBeginningAccess.Type = d3d12_render_pass_loadop(render_pass.depth.stencil_load_op);
		dsDesc.StencilBeginningAccess.Clear.ClearValue.Format = dxgi_format(render_pass.depth.texture->GetDesc().format);
		dsDesc.StencilBeginningAccess.Clear.ClearValue.DepthStencil.Depth = render_pass.depth.clear_depth;
		dsDesc.StencilBeginningAccess.Clear.ClearValue.DepthStencil.Stencil = render_pass.depth.clear_stencil;

		dsDesc.DepthEndingAccess.Type = d3d12_render_pass_storeop(render_pass.depth.store_op);
		dsDesc.StencilEndingAccess.Type = d3d12_render_pass_storeop(render_pass.depth.stencil_store_op);
	}

	m_pCommandList->BeginRenderPass(rt_count, rtDesc, 
		render_pass.depth.texture != nullptr ? &dsDesc : nullptr,
		D3D12_RENDER_PASS_FLAG_NONE);
}

void D3D12CommandList::EndRenderPass()
{
	m_pCommandList->EndRenderPass();
}

void D3D12CommandList::SetPipelineState(IGfxPipelineState* state)
{
	if (m_pCurrentPSO != state)
	{
		m_pCurrentPSO = state;

		m_pCommandList->SetPipelineState((ID3D12PipelineState*)state->GetHandle());

		if (state->GetType() == GfxPipelineType::Graphics)
		{
			m_pCommandList->IASetPrimitiveTopology(((D3D12GraphicsPipelineState*)state)->GetPrimitiveTopology());
		}
	}
}

void D3D12CommandList::SetIndexBuffer(IGfxBuffer* buffer)
{
	D3D12_INDEX_BUFFER_VIEW ibv;
	ibv.BufferLocation = buffer->GetGpuAddress();
	ibv.SizeInBytes = buffer->GetDesc().size;
	ibv.Format = dxgi_format(buffer->GetDesc().format);

	m_pCommandList->IASetIndexBuffer(&ibv);
}

void D3D12CommandList::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
	D3D12_VIEWPORT vp = { (float)x, (float)y, (float)width, (float)height, 0.0f, 1.0f };
	m_pCommandList->RSSetViewports(1, &vp);

	SetScissorRect(x, y, width, height);
}

void D3D12CommandList::SetScissorRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
	D3D12_RECT rect = { (LONG)x, (LONG)y, LONG(x + width), LONG(y + height) };
	m_pCommandList->RSSetScissorRects(1, &rect);
}

void D3D12CommandList::SetConstantBuffer(GfxPipelineType type, uint32_t slot, void* data, size_t data_size)
{
	RE_ASSERT(slot != 0 || data_size == 16);

	bool graphics = type == GfxPipelineType::Graphics || type == GfxPipelineType::MeshShading;

	if (slot == 0)
	{
		if (graphics)
		{
			m_pCommandList->SetGraphicsRoot32BitConstants(0, 4, data, 0);
		}
		else
		{
			m_pCommandList->SetComputeRoot32BitConstants(0, 4, data, 0);
		}
	}
	else
	{
		RE_ASSERT(slot <= 4);

		D3D12_GPU_VIRTUAL_ADDRESS address = ((D3D12Device*)m_pDevice)->AllocateConstantBuffer(data, data_size);
		RE_ASSERT(address);

		if (graphics)
		{
			m_pCommandList->SetGraphicsRootConstantBufferView(slot, address);
		}
		else
		{
			m_pCommandList->SetComputeRootConstantBufferView(slot, address);
		}
	}
}

void D3D12CommandList::Draw(uint32_t vertex_count, uint32_t instance_count)
{
	m_pCommandList->DrawInstanced(vertex_count, instance_count, 0, 0);
}

void D3D12CommandList::DrawIndexed(uint32_t index_count, uint32_t instance_count, uint32_t index_offset)
{
	m_pCommandList->DrawIndexedInstanced(index_count, instance_count, index_offset, 0, 0);
}

void D3D12CommandList::FlushPendingBarrier()
{
	if (!m_pendingBarriers.empty())
	{
		m_pCommandList->ResourceBarrier((UINT)m_pendingBarriers.size(), m_pendingBarriers.data());
		m_pendingBarriers.clear();
	}
}
