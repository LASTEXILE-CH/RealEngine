#pragma once

#include "i_gfx_resource.h"

class IGfxHeap : public IGfxResource
{
public:
    const GfxHeapDesc& GetDesc() const { return m_desc; }

protected:
    GfxHeapDesc m_desc = {};
};