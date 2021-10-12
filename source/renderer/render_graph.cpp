#include "render_graph.h"
#include "core/engine.h"
#include "utils/profiler.h"

RenderGraph::RenderGraph(Renderer* pRenderer) :
    m_resourceAllocator(pRenderer->GetDevice())
{
}

void RenderGraph::Clear()
{
    for (size_t i = 0; i < m_objFinalizer.size(); ++i)
    {
        m_objFinalizer[i].finalizer(m_objFinalizer[i].obj);
    }
    m_objFinalizer.clear();

    m_graph.Clear();

    m_passes.clear();
    m_resourceNodes.clear();
    m_resources.clear();

    m_allocator.Reset();
    m_resourceAllocator.Reset();

    m_pOutputResource = nullptr;
}

void RenderGraph::Compile()
{
    CPU_EVENT("Render", "RenderGraph::Compile");

    m_graph.Cull();

    //allocate resources (todo : aliasing)
    for (size_t i = 0; i < m_resourceNodes.size(); ++i)
    {
        RenderGraphResourceNode* node = m_resourceNodes[i];
        RenderGraphResource* resource = node->GetResource();

        if (node->IsCulled())
        {
            continue;
        }

        std::vector<DAGEdge*> outgoing_edge = m_graph.GetOutgoingEdges(node);
        for (size_t i = 0; i < outgoing_edge.size(); ++i)
        {
            RenderGraphEdge* edge = (RenderGraphEdge*)outgoing_edge[i];
            RenderGraphPassBase* pass = (RenderGraphPassBase*)m_graph.GetNode(edge->GetToNode());

            if (pass->IsCulled())
            {
                continue;
            }

            resource->Resolve(edge, pass);
        }

        if (node->GetVersion() == 0)
        {
            resource->Realize();
        }
    }

    for (size_t i = 0; i < m_passes.size(); ++i)
    {
        RenderGraphPassBase* pass = m_passes[i];
        if (pass->IsCulled())
        {
            continue;
        }

        pass->Resolve(m_graph);
    }
}

void RenderGraph::Execute(IGfxCommandList* pCommandList)
{
    CPU_EVENT("Render", "RenderGraph::Execute");
    GPU_EVENT(pCommandList, "RenderGraph");

    for (size_t i = 0; i < m_passes.size(); ++i)
    {
        RenderGraphPassBase* pass = m_passes[i];
        if (pass->IsCulled())
        {
            continue;
        }

        pass->Execute(*this, pCommandList);
    }

    if (m_pOutputResource)
    {
        pCommandList->ResourceBarrier(m_pOutputResource->GetResource(), GFX_ALL_SUB_RESOURCE, m_pOutputResource->GetFinalState(), GfxResourceState::ShaderResourcePSOnly);
        m_pOutputResource->SetFinalState(GfxResourceState::ShaderResourcePSOnly);
    }
}

void RenderGraph::Present(const RenderGraphHandle& handle)
{
    RenderGraphResourceNode* node = m_resourceNodes[handle.node];
    node->MakeTarget();

    m_pOutputResource = GetResource(handle);
}

RenderGraphResource* RenderGraph::GetResource(const RenderGraphHandle& handle)
{
    return m_resources[handle.index];
}

bool RenderGraph::Export(const std::string& file)
{
    return m_graph.ExportGraphviz(file.c_str());
}

RenderGraphHandle RenderGraph::Read(RenderGraphPassBase* pass, const RenderGraphHandle& input, GfxResourceState usage, uint32_t subresource)
{
    RenderGraphResourceNode* input_node = m_resourceNodes[input.node];

    AllocatePOD<RenderGraphEdge>(m_graph, input_node, pass, usage, subresource);

    return input;
}

RenderGraphHandle RenderGraph::Write(RenderGraphPassBase* pass, const RenderGraphHandle& input, GfxResourceState usage, uint32_t subresource)
{
    RenderGraphResource* resource = m_resources[input.index];

    RenderGraphResourceNode* input_node = m_resourceNodes[input.node];
    AllocatePOD<RenderGraphEdge>(m_graph, input_node, pass, usage, subresource);

    RenderGraphResourceNode* output_node = AllocatePOD<RenderGraphResourceNode>(m_graph, resource, input_node->GetVersion() + 1);
    AllocatePOD<RenderGraphEdge>(m_graph, pass, output_node, usage, subresource);

    RenderGraphHandle output;        
    output.index = input.index;
    output.node = (uint16_t)m_resourceNodes.size();

    m_resourceNodes.push_back(output_node);

    return output;
}

RenderGraphHandle RenderGraph::WriteColor(RenderGraphPassBase* pass, uint32_t color_index, const RenderGraphHandle& input, uint32_t subresource, GfxRenderPassLoadOp load_op, const float4& clear_color)
{
    RenderGraphResource* resource = m_resources[input.index];

    GfxResourceState usage = GfxResourceState::RenderTarget;

    RenderGraphResourceNode* input_node = m_resourceNodes[input.node];
    AllocatePOD<RenderGraphEdgeColorAttchment>(m_graph, input_node, pass, usage, subresource, color_index, load_op, clear_color);

    RenderGraphResourceNode* output_node = AllocatePOD<RenderGraphResourceNode>(m_graph, resource, input_node->GetVersion() + 1);
    AllocatePOD<RenderGraphEdgeColorAttchment>(m_graph, pass, output_node, usage, subresource, color_index, load_op, clear_color);

    RenderGraphHandle output;
    output.index = input.index;
    output.node = (uint16_t)m_resourceNodes.size();

    m_resourceNodes.push_back(output_node);

    return output;
}

RenderGraphHandle RenderGraph::WriteDepth(RenderGraphPassBase* pass, const RenderGraphHandle& input, uint32_t subresource, bool read_only, GfxRenderPassLoadOp depth_load_op, GfxRenderPassLoadOp stencil_load_op, float clear_depth, uint32_t clear_stencil)
{
    RenderGraphResource* resource = m_resources[input.index];

    GfxResourceState usage = read_only ? GfxResourceState::DepthStencilReadOnly : GfxResourceState::DepthStencil;

    RenderGraphResourceNode* input_node = m_resourceNodes[input.node];
    AllocatePOD<RenderGraphEdgeDepthAttchment>(m_graph, input_node, pass, usage, subresource, depth_load_op, stencil_load_op, clear_depth, clear_stencil);

    RenderGraphResourceNode* output_node = AllocatePOD<RenderGraphResourceNode>(m_graph, resource, input_node->GetVersion() + 1);
    AllocatePOD<RenderGraphEdgeDepthAttchment>(m_graph, pass, output_node, usage, subresource, depth_load_op, stencil_load_op, clear_depth, clear_stencil);

    RenderGraphHandle output;
    output.index = input.index;
    output.node = (uint16_t)m_resourceNodes.size();

    m_resourceNodes.push_back(output_node);

    return output;
}
