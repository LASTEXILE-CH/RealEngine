#include "gui.h"
#include "engine.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"

GUI::GUI()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	io.BackendRendererName = "imgui_impl_realengine";
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.

	ImGui::StyleColorsDark();
	//ImGui::StyleColorsClassic();

	ImGui_ImplWin32_Init(Engine::GetInstance()->GetWindowHandle());
}

GUI::~GUI()
{
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

bool GUI::Init()
{
	Renderer* pRenderer = Engine::GetInstance()->GetRenderer();

	//TODO : Build texture atlas
	ImGuiIO& io = ImGui::GetIO();
	unsigned char* pixels;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

	io.Fonts->TexID = (ImTextureID)m_fontTextureSRV;

	GfxGraphicsPipelineDesc desc;
	desc.vs = pRenderer->GetShader("imgui.hlsl", "vs_main", "vs_6_6", {});
	desc.ps = pRenderer->GetShader("imgui.hlsl", "ps_main", "ps_6_6", {});
	desc.blend_state[0].blend_enable = true;
	desc.blend_state[0].color_src = GfxBlendFactor::SrcAlpha;
	desc.blend_state[0].color_dst = GfxBlendFactor::InvSrcAlpha;
	desc.blend_state[0].alpha_src = GfxBlendFactor::One;
	desc.blend_state[0].alpha_dst = GfxBlendFactor::InvSrcAlpha;
	desc.rt_format[0] = pRenderer->GetSwapchain()->GetBackBuffer()->GetDesc().format;

	m_pPSO = pRenderer->GetPipelineState(desc, "GUI PSO");

	return true;
}

void GUI::NewFrame()
{
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	//test code
	ImGui::Begin("Hello, world!");
	ImGui::Text("%.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
	ImGui::End();

	ImGui::ShowDemoWindow();
}

void GUI::Render(IGfxCommandList* pCommandList)
{
	RENDER_EVENT(pCommandList, "GUI");
	ImGui::Render();

	Renderer* pRenderer = Engine::GetInstance()->GetRenderer();
	IGfxDevice* pDevice = pRenderer->GetDevice();
	ImDrawData* draw_data = ImGui::GetDrawData();

	// Avoid rendering when minimized
	if (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f)
	{
		return;
	}

	uint32_t frame_index = pDevice->GetFrameID() % MAX_INFLIGHT_FRAMES;

	if (m_pVertexBuffer[frame_index] == nullptr || m_pVertexBuffer[frame_index]->GetDesc().size < draw_data->TotalVtxCount * sizeof(ImDrawVert))
	{
		GfxBufferDesc desc;
		desc.stride = sizeof(ImDrawVert);
		desc.size = (draw_data->TotalVtxCount + 5000) * sizeof(ImDrawVert);
		desc.memory_type = GfxMemoryType::CpuToGpu;
		desc.usage = GfxBufferUsageStructuredBuffer;

		m_pVertexBuffer[frame_index].reset(pDevice->CreateBuffer(desc, "GUI VB"));

		GfxShaderResourceViewDesc srvDesc = {};
		srvDesc.type = GfxShaderResourceViewType::StructuredBuffer;
		srvDesc.buffer.offset = 0;
		srvDesc.buffer.size = desc.size;

		pDevice->ReleaseResourceDescriptor(m_vertexBufferSRV[frame_index]);
		m_vertexBufferSRV[frame_index] = pDevice->CreateShaderResourceView(m_pVertexBuffer[frame_index].get(), srvDesc);
	}

	if (m_pIndexBuffer[frame_index] == nullptr || m_pIndexBuffer[frame_index]->GetDesc().size < draw_data->TotalIdxCount * sizeof(ImDrawIdx))
	{
		GfxBufferDesc desc;
		desc.stride = sizeof(ImDrawIdx);
		desc.size = (draw_data->TotalIdxCount + 10000) * sizeof(ImDrawIdx);
		desc.format = GfxFormat::R16UI;
		desc.memory_type = GfxMemoryType::CpuToGpu;

		m_pIndexBuffer[frame_index].reset(pDevice->CreateBuffer(desc, "GUI IB"));
	}

	ImDrawVert* vtx_dst = (ImDrawVert*)m_pVertexBuffer[frame_index]->GetCpuAddress();
	ImDrawIdx* idx_dst = (ImDrawIdx*)m_pIndexBuffer[frame_index]->GetCpuAddress();
	for (int n = 0; n < draw_data->CmdListsCount; n++)
	{
		const ImDrawList* cmd_list = draw_data->CmdLists[n];
		memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
		memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
		vtx_dst += cmd_list->VtxBuffer.Size;
		idx_dst += cmd_list->IdxBuffer.Size;
	}

	SetupRenderState(pCommandList, frame_index);

	// Render command lists
	// (Because we merged all buffers into a single one, we maintain our own offset into them)
	int global_vtx_offset = 0;
	int global_idx_offset = 0;
	ImVec2 clip_off = draw_data->DisplayPos;
	for (int n = 0; n < draw_data->CmdListsCount; n++)
	{
		const ImDrawList* cmd_list = draw_data->CmdLists[n];
		for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
		{
			const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
			if (pcmd->UserCallback != NULL)
			{
				// User callback, registered via ImDrawList::AddCallback()
				// (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
				if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
				{
					SetupRenderState(pCommandList, frame_index);
				}
				else
				{
					pcmd->UserCallback(cmd_list, pcmd);
				}
			}
			else
			{
				// Apply Scissor, Bind texture, Draw
				float x = pcmd->ClipRect.x - clip_off.x;
				float y = pcmd->ClipRect.y - clip_off.y;
				float width = pcmd->ClipRect.z - pcmd->ClipRect.x;
				float height = pcmd->ClipRect.w - pcmd->ClipRect.y;

				if (width > 0 && height > 0)
				{				
					pCommandList->SetScissorRect((uint32_t)x, (uint32_t)y, (uint32_t)width, (uint32_t)height);

					uint32_t resource_ids[4] = { m_vertexBufferSRV[frame_index], pcmd->VtxOffset + global_vtx_offset, m_fontTextureSRV, pRenderer->GetLinearSampler() };
					pCommandList->SetConstantBuffer(GfxPipelineType::Graphics, 0, resource_ids, sizeof(resource_ids));

					pCommandList->DrawIndexed(pcmd->ElemCount, pcmd->IdxOffset + global_idx_offset, 1);
				}
			}
		}
		global_idx_offset += cmd_list->IdxBuffer.Size;
		global_vtx_offset += cmd_list->VtxBuffer.Size;
	}
}

void GUI::SetupRenderState(IGfxCommandList* pCommandList, uint32_t frame_index)
{
	ImDrawData* draw_data = ImGui::GetDrawData();

	pCommandList->SetViewport(0, 0, (uint32_t)draw_data->DisplaySize.x, (uint32_t)draw_data->DisplaySize.y);
	pCommandList->SetPipelineState(m_pPSO);
	pCommandList->SetIndexBuffer(m_pIndexBuffer[frame_index].get());

	float L = draw_data->DisplayPos.x;
	float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
	float T = draw_data->DisplayPos.y;
	float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
	float mvp[4][4] =
	{
		{ 2.0f/(R-L),   0.0f,           0.0f,       0.0f },
		{ 0.0f,         2.0f/(T-B),     0.0f,       0.0f },
		{ 0.0f,         0.0f,           0.5f,       0.0f },
		{ (R+L)/(L-R),  (T+B)/(B-T),    0.5f,       1.0f },
	};

	pCommandList->SetConstantBuffer(GfxPipelineType::Graphics, 1, mvp, sizeof(mvp));
}