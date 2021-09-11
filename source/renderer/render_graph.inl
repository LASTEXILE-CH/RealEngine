#pragma once

#include "render_graph.h"
#include "render_graph_builder.h"

class RenderGraphResourceNode : public DAGNode
{
public:
    RenderGraphResourceNode(DirectedAcyclicGraph& graph, RenderGraphResource* resource, uint32_t version) :
        DAGNode(graph)
    {
        m_pResource = resource;
        m_version = version;
    }

    RenderGraphResource* GetResource() const { return m_pResource; }
    uint32_t GetVersion() const { return m_version; }

    virtual std::string GetGraphvizName() const override 
    {
        std::string s = m_pResource->GetName();
        s.append("\nversion:");
        s.append(std::to_string(m_version));
        return s;
    }

    virtual const char* GetGraphvizColor() const { return !IsCulled() ? "lightskyblue1" : "lightskyblue4"; }
    virtual const char* GetGraphvizShape() const { return "ellipse"; }

private:
    RenderGraphResource* m_pResource;
    uint32_t m_version;
};

class RenderGraphEdge : public DAGEdge
{
public:
    RenderGraphEdge(DirectedAcyclicGraph& graph, DAGNode* from, DAGNode* to, GfxResourceState usage, uint32_t subresource) :
        DAGEdge(graph, from, to)
    {
        m_usage = usage;
        m_subresource = subresource;
    }

    GfxResourceState GetUsage() const { return m_usage; }
    uint32_t GetSubresource() const { return m_subresource; }

private:
    GfxResourceState m_usage;
    uint32_t m_subresource;
};

template<typename T>
void ClassFinalizer(void* p)
{
    ((T*)p)->~T();
}

template<typename T, typename... ArgsT>
inline T* RenderGraph::Allocate(ArgsT&&... arguments)
{
    T* p = (T*)m_allocator.Alloc(sizeof(T));
    new (p) T(arguments...);

    ObjFinalizer finalizer;
    finalizer.obj = p;
    finalizer.finalizer = &ClassFinalizer<T>;
    m_objFinalizer.push_back(finalizer);

    return p;
}

template<typename T, typename... ArgsT>
inline T* RenderGraph::AllocatePOD(ArgsT&&... arguments)
{
    T* p = (T*)m_allocator.Alloc(sizeof(T));
    new (p) T(arguments...);

    return p;
}

template<typename Data, typename Setup, typename Exec>
inline RenderGraphPass<Data>& RenderGraph::AddPass(const char* name, const Setup& setup, const Exec& execute)
{
    auto* pass = Allocate<RenderGraphPass<Data>>(name, m_graph, execute);

    RenderGraphBuilder builder(this, pass);
    setup(pass->GetData(), builder);

    m_passes.push_back(pass);

    return *pass;
}

template<typename Resource>
inline RenderGraphHandle RenderGraph::Create(const typename Resource::Desc& desc, const char* name)
{
    auto* resource = Allocate<Resource>(m_resourceAllocator, name, desc);
    auto* node = AllocatePOD<RenderGraphResourceNode>(m_graph, resource, 0);

    RenderGraphHandle handle;
    handle.index = (uint16_t)m_resources.size();
    handle.node = (uint16_t)m_resourceNodes.size();

    m_resources.push_back(resource);
    m_resourceNodes.push_back(node);

    return handle;
}