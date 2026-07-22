#include <eng/MaterialPreview.h>

#include <eng/Renderer.h>
#include <eng/Log.h>

#include "ProceduralMeshes.h"
#include "RenderCore.h"

#include <Ogre.h>

#include <algorithm>

namespace eng {

namespace {
Ogre::ColourValue colour(float r, float g, float b, float a = 1.0f)
{
    return Ogre::ColourValue(r, g, b, a);
}
}

struct MaterialPreview::Impl {
    Renderer& renderer;
    RenderCore& core;
    int texSize = 96;
    Ogre::SceneManager* scene = nullptr;
    Ogre::Camera* camera = nullptr;
    Ogre::SceneNode* sphereNode = nullptr;
    Ogre::Entity* sphere = nullptr;
    Ogre::TexturePtr texture;
    Ogre::RenderTexture* target = nullptr;
    std::string meshName;
    std::string textureName;
    std::string lastMaterial;
    bool warnedFallback = false;

    Impl(Renderer& r, int size)
        : renderer(r), core(detail::coreOf(r)), texSize(std::clamp(size, 32, 512))
    {
        Ogre::Root* root = core.root();
        if (!root)
            return;
        scene = root->createSceneManager();
        scene->setAmbientLight(colour(0.20f, 0.22f, 0.24f));

        meshName = "__eng_material_preview_sphere";
        if (!Ogre::MeshManager::getSingleton().getByName(meshName))
            ProceduralMeshes::createSphere(meshName, 0.8f, 16, 24);
        sphere = scene->createEntity(meshName);
        sphere->setCastShadows(false);
        sphereNode = scene->getRootSceneNode()->createChildSceneNode();
        sphereNode->attachObject(sphere);

        Ogre::Light* key = scene->createLight("__eng_material_preview_key");
        key->setType(Ogre::Light::LT_POINT);
        key->setDiffuseColour(colour(1.0f, 0.92f, 0.78f));
        key->setAttenuation(7.0f, 1.0f, 0.12f, 0.02f);
        scene->getRootSceneNode()
            ->createChildSceneNode(Ogre::Vector3(-1.8f, 2.6f, 2.2f))
            ->attachObject(key);

        camera = scene->createCamera("__eng_material_preview_camera");
        camera->setNearClipDistance(0.05f);
        camera->setFarClipDistance(20.0f);
        camera->setFOVy(Ogre::Degree(38.0f));
        Ogre::SceneNode* camNode =
            scene->getRootSceneNode()->createChildSceneNode(
                Ogre::Vector3(0.0f, 0.15f, 3.4f));
        camNode->lookAt(Ogre::Vector3(0.0f, 0.05f, 0.0f),
                        Ogre::Node::TS_WORLD);
        camNode->attachObject(camera);

        textureName = "__eng_material_preview_rtt";
        texture = Ogre::TextureManager::getSingleton().createManual(
            textureName, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
            Ogre::TEX_TYPE_2D, uint32_t(texSize), uint32_t(texSize), 0,
            Ogre::PF_R8G8B8A8, Ogre::TU_RENDERTARGET);
        target = texture->getBuffer()->getRenderTarget();
        target->setAutoUpdated(false);
        Ogre::Viewport* vp = target->addViewport(camera);
        vp->setOverlaysEnabled(false);
        vp->setClearEveryFrame(true);
        vp->setBackgroundColour(colour(0.035f, 0.040f, 0.045f));
    }

    ~Impl()
    {
        if (texture) {
            texture->getBuffer()->getRenderTarget()->removeAllViewports();
            Ogre::TextureManager::getSingleton().remove(texture);
            texture.reset();
            target = nullptr;
        }
        if (scene && core.root())
            core.root()->destroySceneManager(scene);
        scene = nullptr;
    }
};

MaterialPreview::MaterialPreview(Renderer& r, int size)
    : mImpl(std::make_unique<Impl>(r, size))
{}

MaterialPreview::~MaterialPreview() = default;

void MaterialPreview::render(const std::string& materialName)
{
    Impl& s = *mImpl;
    if (!s.sphere || !s.target || materialName == s.lastMaterial)
        return;

    std::string material = materialName;
    if (!Ogre::MaterialManager::getSingleton().getByName(material)) {
        if (!s.warnedFallback) {
            log::warn("MaterialPreview: material '%s' missing; using Game/PrototypeFloor",
                      materialName.c_str());
            s.warnedFallback = true;
        }
        material = "Game/PrototypeFloor";
    }
    s.sphere->setMaterialName(material);
    s.target->update();
    s.lastMaterial = materialName;
}

uint64_t MaterialPreview::textureId() const
{
    const Impl& s = *mImpl;
    if (!s.texture)
        return 0;
    unsigned int id = 0;
    try {
        s.texture->getCustomAttribute("GLID", &id);
    } catch (const Ogre::Exception&) {
        return 0;
    }
    return uint64_t(id);
}

int MaterialPreview::size() const { return mImpl->texSize; }

} // namespace eng
