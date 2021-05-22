#pragma once

#include "i_visible_object.h"

struct cgltf_data;
struct cgltf_node;
struct cgltf_primitive;
struct cgltf_material;
struct cgltf_accessor;

class Model : public IVisibleObject
{
	struct Material
	{
		std::string name;

		Texture* albedoTexture = nullptr;
		Texture* metallicRoughnessTexture = nullptr;
		Texture* normalTexture = nullptr;

		float3 albedoColor;
		float metallic;
		float roughness;
		float alphaCutoff;
		bool alphaTest;
	};

	struct Mesh
	{
		std::string name;
		std::unique_ptr<Material> material;

		std::unique_ptr<IGfxBuffer> indexBuffer;
		uint32_t indexCount = 0;

		std::unique_ptr<IGfxBuffer> posBuffer;
		std::unique_ptr<IGfxBuffer> uvBuffer;
		std::unique_ptr<IGfxBuffer> normalBuffer;
		std::unique_ptr<IGfxBuffer> tangentBuffer;

		std::unique_ptr<IGfxDescriptor> posBufferSRV;
		std::unique_ptr<IGfxDescriptor> uvBufferSRV;
		std::unique_ptr<IGfxDescriptor> normalBufferSRV;
		std::unique_ptr<IGfxDescriptor> tangentBufferSRV;
	};

	struct Node
	{
		std::string name;
		float4x4 localToParentMatrix;
		std::vector<std::unique_ptr<Mesh>> meshes;
		std::vector<std::unique_ptr<Node>> childNodes;
	};

public:
    virtual void Load(tinyxml2::XMLElement* element) override;
    virtual void Store(tinyxml2::XMLElement* element) override;
    virtual bool Create() override;
    virtual void Tick(float delta_time) override;
	virtual void Render(Renderer* pRenderer) override;

private:
	void RenderBassPass(IGfxCommandList* pCommandList, Renderer* pRenderer, Camera* pCamera, Node* pNode, const float4x4& parentWorld);

	Texture* LoadTexture(const std::string& file, bool srgb);
	Node* LoadNode(const cgltf_node* gltf_node);
	Mesh* LoadMesh(const cgltf_primitive* gltf_primitive, const std::string& name);
	Material* LoadMaterial(const cgltf_material* gltf_material);
	IGfxBuffer* LoadIndexBuffer(const cgltf_accessor* accessor, const std::string& name);
	void LoadVertexBuffer(const cgltf_accessor* accessor, const std::string& name, bool convertToLH, IGfxBuffer** buffer, IGfxDescriptor** descriptor);

private:
    std::string m_file;

	std::unique_ptr<Node> m_pRootNode;
    std::unordered_map<std::string, std::unique_ptr<Texture>> m_textures;
};
