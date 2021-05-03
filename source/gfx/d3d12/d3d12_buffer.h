#pragma once

#include "d3d12_header.h"
#include "../i_gfx_buffer.h"

class D3D12Device;

namespace D3D12MA
{
	class Allocation;
}

class D3D12Buffer : public IGfxBuffer
{
public:
	D3D12Buffer(D3D12Device* pDevice, const GfxBufferDesc& desc, const std::string& name);
	~D3D12Buffer();

	virtual void* GetHandle() const override { return m_pBuffer; }
	virtual void* GetCpuAddress() override;
	virtual uint64_t GetGpuAddress() override;
	virtual uint32_t GetRequiredStagingBufferSize() const override;

	bool Create();

private:
	ID3D12Resource* m_pBuffer = nullptr;
	D3D12MA::Allocation* m_pAllocation = nullptr;
	void* m_pCpuAddress = nullptr;
};
