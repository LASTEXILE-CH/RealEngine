#include "world.h"
#include "core/engine.h"
#include "gltf_loader.h"
#include "sky_sphere.h"
#include "directional_light.h"
#include "utils/assert.h"
#include "utils/string.h"
#include "utils/profiler.h"
#include "tinyxml2/tinyxml2.h"

World::World()
{
    m_pCamera = std::make_unique<Camera>();
    m_pCamera->EnableJitter(true);
}

void World::LoadScene(const std::string& file)
{    
    tinyxml2::XMLDocument doc;
    if (tinyxml2::XML_SUCCESS != doc.LoadFile(file.c_str()))
    {
        return;
    }

    ClearScene();

    tinyxml2::XMLNode* root_node = doc.FirstChild();
    RE_ASSERT(root_node != nullptr && strcmp(root_node->Value(), "scene") == 0);

    for (tinyxml2::XMLElement* element = root_node->FirstChildElement(); element != nullptr; element = (tinyxml2::XMLElement*)element->NextSibling())
    {
        CreateVisibleObject(element);
    }
}

void World::SaveScene(const std::string& file)
{
}

void World::AddObject(IVisibleObject* object)
{
    RE_ASSERT(object != nullptr);
    m_objects.push_back(std::unique_ptr<IVisibleObject>(object));
}

void World::AddLight(ILight* light)
{
    RE_ASSERT(light != nullptr);
    m_lights.push_back(std::unique_ptr<ILight>(light));
}

void World::Tick(float delta_time)
{
    CPU_EVENT("Tick", "World::Tick");

    m_pCamera->Tick(delta_time);

    for (auto iter = m_objects.begin(); iter != m_objects.end(); ++iter)
    {
        (*iter)->Tick(delta_time);
    }

    for (auto iter = m_lights.begin(); iter != m_lights.end(); ++iter)
    {
        (*iter)->Tick(delta_time);
    }

    //todo : culling, ...

    Renderer* pRenderer = Engine::GetInstance()->GetRenderer();

    for (auto iter = m_objects.begin(); iter != m_objects.end(); ++iter)
    {
        (*iter)->Render(pRenderer);
    }
}

IVisibleObject* World::GetSelectedObject() const
{
    //todo : implements mouse pick

    return m_objects.begin()->get();
}

ILight* World::GetPrimaryLight() const
{
    RE_ASSERT(m_pPrimaryLight != nullptr);
    return m_pPrimaryLight;
}

void World::ClearScene()
{
    m_objects.clear();
    m_lights.clear();
    m_pPrimaryLight = nullptr;
}

inline float3 str_to_float3(const std::string& str)
{
    std::vector<float> v;
    v.reserve(3);
    string_to_float_array(str, v);
    return float3(v[0], v[1], v[2]);
}

inline void LoadVisibleObject(tinyxml2::XMLElement* element, IVisibleObject* object)
{
    const tinyxml2::XMLAttribute* position = element->FindAttribute("position");
    if (position)
    {
        object->SetPosition(str_to_float3(position->Value()));
    }

    const tinyxml2::XMLAttribute* rotation = element->FindAttribute("rotation");
    if (rotation)
    {
        object->SetRotation(str_to_float3(rotation->Value()));
    }

    const tinyxml2::XMLAttribute* scale = element->FindAttribute("scale");
    if (scale)
    {
        object->SetScale(str_to_float3(scale->Value()));
    }
}

void World::CreateVisibleObject(tinyxml2::XMLElement* element)
{
    if (strcmp(element->Value(), "light") == 0)
    {
        CreateLight(element);
    }
    else if (strcmp(element->Value(), "camera") == 0)
    {
        CreateCamera(element);
    }
    else if (strcmp(element->Value(), "model") == 0)
    {
        CreateModel(element);
    }
    else if(strcmp(element->Value(), "skysphere") == 0)
    {
        CreateSky(element);
    }
}

void World::CreateLight(tinyxml2::XMLElement* element)
{
    ILight* light = nullptr;

    const tinyxml2::XMLAttribute* type = element->FindAttribute("type");
    RE_ASSERT(type != nullptr);

    if (strcmp(type->Value(), "directional") == 0)
    {
        light = new DirectionalLight();
    }
    else
    {
        //todo
        RE_ASSERT(false);
    }

    LoadVisibleObject(element, light);

    if (!light->Create())
    {
        delete light;
        return;
    }

    AddLight(light);

    const tinyxml2::XMLAttribute* primary = element->FindAttribute("primary");
    if (primary && primary->BoolValue())
    {
        m_pPrimaryLight = light;
    }
}

void World::CreateCamera(tinyxml2::XMLElement* element)
{
    const tinyxml2::XMLAttribute* position = element->FindAttribute("position");
    if (position)
    {
        m_pCamera->SetPosition(str_to_float3(position->Value()));
    }

    const tinyxml2::XMLAttribute* rotation = element->FindAttribute("rotation");
    if (rotation)
    {
        m_pCamera->SetRotation(str_to_float3(rotation->Value()));
    }
}

void World::CreateModel(tinyxml2::XMLElement* element)
{
    GLTFLoader loader(this, element);
    loader.Load();
}

void World::CreateSky(tinyxml2::XMLElement* element)
{
    IVisibleObject* object = new SkySphere();
    RE_ASSERT(object != nullptr);

    LoadVisibleObject(element, object);

    if (!object->Create())
    {
        delete object;
        return;
    }

    AddObject(object);
}
