#pragma once

#include "i_gfx_resource.h"

class IGfxBuffer : public IGfxResource
{
public:
	const GfxBufferDesc& GetDesc() const { return m_desc; }

	virtual void* GetCpuAddress() = 0;
	virtual uint64_t GetGpuAddress() = 0;

protected:
	GfxBufferDesc m_desc = {};
};