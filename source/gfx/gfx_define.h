#pragma once

#include <stdint.h>
#include <string>

enum class GfxRenderBackend
{
	D3D12,
	//todo : maybe Vulkan
};

enum class GfxFormat
{
	Unknown,
	
	RGBA32F,
	RGBA32UI,
	RGBA32SI,
	RGBA16F,
	RGBA16UI,
	RGBA16SI,
	RGBA16UNORM,
	RGBA16SNORM,
	RGBA8UI,
	RGBA8SI,
	RGBA8UNORM,
	RGBA8SNORM,
	RGBA8SRGB,

	RG32F,
	RG32UI,
	RG32SI,
	RG16F,
	RG16UI,
	RG16SI,
	RG16UNORM,
	RG16SNORM,
	RG8UI,
	RG8SI,
	RG8UNORM,
	RG8SNORM,

	R32F,
	R32UI,
	R32SI,
	R16F,
	R16UI,
	R16SI,
	R16UNORM,
	R16SNORM,
	R8UI,
	R8SI,
	R8UNORM,
	R8SNORM,

	D32F,
	D32FS8,
	D16,
};

enum class GfxMemoryType
{
	GpuOnly,
	CpuOnly,  //staging buffers
	CpuToGpu, //frequently updated buffers
	GpuToCpu, //readback
};

enum class GfxAllocationType
{
	Committed,
	Placed,
	//todo : SubAllocatedBuffer,
};

enum GfxBufferUsageBit
{
	GfxBufferUsageConstantBuffer    = 1 << 0,
	GfxBufferUsageStructuredBuffer  = 1 << 1,
	GfxBufferUsageTypedBuffer       = 1 << 2,
	GfxBufferUsageRawBuffer         = 1 << 3,
	GfxBufferUsageUnorderedAccess   = 1 << 4,
};
using GfxBufferUsageFlags = uint32_t;

enum GfxTextureUsageBit
{
	GfxTextureUsageShaderResource   = 1 << 0,
	GfxTextureUsageRenderTarget     = 1 << 1,
	GfxTextureUsageDepthStencil     = 1 << 2,
	GfxTextureUsageUnorderedAccess  = 1 << 3,
};
using GfxTextureUsageFlags = uint32_t;

enum class GfxTextureType
{
	Texture2D,
	Texture2DArray,
	Texture3D,
	TextureCube,
	TextureCubeArray,
};

enum class GfxCommandQueue
{
	Graphics,
	Compute,
	Copy,
};

enum class GfxResourceState
{
	Common,
	RenderTarget,
	UnorderedAccess,
	DepthStencil,
	DepthStencilReadOnly,
	ShaderResource,
	ShaderResourcePSOnly,
	IndirectArg,
	CopyDst,
	CopySrc,
	ResolveDst,
	ResolveSrc,
	Present,
};

enum class GfxRenderPassLoadOp
{
	Load,
	Clear,
	DontCare,
};

enum class GfxRenderPassStoreOp
{
	Store,
	DontCare,
};

enum class GfxShaderResourceViewType
{
	Texture2D,
	Texture2DArray,
	Texture3D,
	TextureCube,
	TextureCubeArray,
	StructuredBuffer,
	TypedBuffer,
	RawBuffer,
};

enum class GfxUnorderedAccessViewType
{
	Texture2D,
	Texture2DArray,
	Texture3D,
	StructuredBuffer,
	TypedBuffer,
	RawBuffer,
};

static const uint32_t GFX_ALL_SUB_RESOURCE = 0xFFFFFFFF;

struct GfxDeviceDesc
{
	GfxRenderBackend backend = GfxRenderBackend::D3D12;
	uint32_t max_frame_lag = 3;
};

struct GfxSwapchainDesc
{
	void* windowHandle = nullptr;
	uint32_t width = 1;
	uint32_t height = 1;
	uint32_t backbuffer_count = 3;
	GfxFormat backbuffer_format = GfxFormat::RGBA8SRGB;
	bool enableVsync = true;
};

struct GfxBufferDesc
{
	uint32_t stride = 1;
	uint32_t size = 1;
	GfxFormat format = GfxFormat::Unknown;
	GfxMemoryType memory_type = GfxMemoryType::GpuOnly;
	GfxAllocationType alloc_type = GfxAllocationType::Committed;
	GfxBufferUsageFlags usage = 0;
};

struct GfxTextureDesc
{
	uint32_t width = 1;
	uint32_t height = 1;
	uint32_t depth = 1;
	uint32_t mip_levels = 1;
	uint32_t array_size = 1;
	GfxTextureType type = GfxTextureType::Texture2D;
	GfxFormat format = GfxFormat::Unknown;
	GfxMemoryType memory_type = GfxMemoryType::GpuOnly;
	GfxAllocationType alloc_type = GfxAllocationType::Committed;
	GfxTextureUsageFlags usage = GfxTextureUsageShaderResource;
};

struct GfxRenderTargetViewDesc
{
	uint32_t mip_slice = 0;
	uint32_t array_slice = 0;
	uint32_t plane_slice = 0;
};

struct GfxDepthStencilViewDesc
{
	uint32_t mip_slice = 0;
	uint32_t array_slice = 0;
};

struct GfxContantBufferViewDesc
{
	uint32_t size = 0;
	uint32_t offset = 0;
};

struct GfxShaderResourceViewDesc
{
	GfxShaderResourceViewType type;

	union
	{
		struct
		{
			uint32_t mip_slice = 0;
			uint32_t mip_levels = uint32_t(-1);
			uint32_t array_slice = 0;
			uint32_t array_size = 1;
			uint32_t plane_slice = 0;
		} texture;

		struct
		{
			uint32_t size;
			uint32_t offset;
		} buffer;
	};
};

struct GfxUnorderedAccessViewDesc
{
	GfxUnorderedAccessViewType type;

	union
	{
		struct
		{
			uint32_t mip_slice = 0;
			uint32_t array_slice = 0;
			uint32_t array_size = 1;
			uint32_t plane_slice = 0;
		} texture;

		struct
		{
			uint32_t size;
			uint32_t offset;
		} buffer;
	};
};

class IGfxTexture;

struct GfxRenderPassColorAttachment
{
	IGfxTexture* texture = nullptr;
	uint32_t mip_slice = 0;
	uint32_t array_slice = 0;
	GfxRenderPassLoadOp load_op = GfxRenderPassLoadOp::Load;
	GfxRenderPassStoreOp store_op = GfxRenderPassStoreOp::Store;
	struct { float value[4]; } clear_color = {};
};

struct GfxRenderPassDepthAttachment
{
	IGfxTexture* texture = nullptr;
	uint32_t mip_slice = 0;
	uint32_t array_slice = 0;
	GfxRenderPassLoadOp load_op = GfxRenderPassLoadOp::Load;
	GfxRenderPassStoreOp store_op = GfxRenderPassStoreOp::Store;
	GfxRenderPassLoadOp stencil_load_op = GfxRenderPassLoadOp::Load;
	GfxRenderPassStoreOp stencil_store_op = GfxRenderPassStoreOp::Store;
	float clear_depth = 0.0f;
	uint32_t clear_stencil = 0;
};

struct GfxRenderPassDesc
{
	GfxRenderPassColorAttachment color[8];
	GfxRenderPassDepthAttachment depth;
};

struct GfxShaderDesc
{
	std::string file;
	std::string entry_point;
	std::string profile;
};