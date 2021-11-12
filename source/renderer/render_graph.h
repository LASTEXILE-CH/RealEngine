#pragma once

#include "render_graph_pass.h"
#include "render_graph_handle.h"
#include "render_graph_resource.h"
#include "render_graph_blackboard.h"
#include "utils/linear_allocator.h"
#include "utils/math.h"

class RenderGraphResourceNode;
class Renderer;

class RenderGraph
{
    friend class RenderGraphBuilder;
public:
    RenderGraph(Renderer* pRenderer);

    template<typename Data, typename Setup, typename Exec>
    RenderGraphPass<Data>& AddPass(const char* name, const Setup& setup, const Exec& execute);

    void Clear();
    void Compile();
    void Execute(IGfxCommandList* pCommandList);

    void Present(const RenderGraphHandle& handle, GfxResourceState filnal_state);

    RenderGraphResource* GetResource(const RenderGraphHandle& handle);
    const DirectedAcyclicGraph& GetDAG() const { return m_graph; }

    bool Export(const std::string& file);

private:
    template<typename T, typename... ArgsT>
    T* Allocate(ArgsT&&... arguments);

    template<typename T, typename... ArgsT>
    T* AllocatePOD(ArgsT&&... arguments);

    template<typename Resource>
    RenderGraphHandle Create(const typename Resource::Desc& desc, const char* name);

    RenderGraphHandle Import(IGfxTexture* texture, GfxResourceState state);

    RenderGraphHandle Read(RenderGraphPassBase* pass, const RenderGraphHandle& input, GfxResourceState usage, uint32_t subresource);
    RenderGraphHandle Write(RenderGraphPassBase* pass, const RenderGraphHandle& input, GfxResourceState usage, uint32_t subresource);

    RenderGraphHandle WriteColor(RenderGraphPassBase* pass, uint32_t color_index, const RenderGraphHandle& input, uint32_t subresource, GfxRenderPassLoadOp load_op, const float4& clear_color);
    RenderGraphHandle WriteDepth(RenderGraphPassBase* pass, const RenderGraphHandle& input, uint32_t subresource, bool read_only, GfxRenderPassLoadOp depth_load_op, GfxRenderPassLoadOp stencil_load_op, float clear_depth, uint32_t clear_stencil);

private:
    LinearAllocator m_allocator { 32 * 1024 };
    RenderGraphResourceAllocator m_resourceAllocator;
    DirectedAcyclicGraph m_graph;
    RenderGraphBlackboard m_blackboard;

    std::vector<RenderGraphPassBase*> m_passes;
    std::vector<RenderGraphResource*> m_resources;
    std::vector<RenderGraphResourceNode*> m_resourceNodes;

    struct ObjFinalizer
    {
        void* obj;
        void(*finalizer)(void*);
    };
    std::vector<ObjFinalizer>  m_objFinalizer;

    struct PresentTarget
    {
        RenderGraphResource* resource;
        GfxResourceState state;
    };
    std::vector<PresentTarget> m_outputResources;
};

#include "render_graph.inl"