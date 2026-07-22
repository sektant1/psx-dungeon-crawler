#include <eng/Renderer.h>

#include <eng/Log.h>

#include "ObjLoader.h"
#include "Particles.h"
#include "ProceduralMeshes.h"
#include "RenderCore.h"

#include <Ogre.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <regex>
#include <unordered_map>
#include <vector>

namespace eng {

namespace {

Ogre::Vector3 toOgre(glm::vec3 v) { return {v.x, v.y, v.z}; }
Ogre::Quaternion toOgre(glm::quat q) { return {q.w, q.x, q.y, q.z}; }
Ogre::ColourValue toColour(glm::vec3 c) { return Ogre::ColourValue(c.x, c.y, c.z); }

Ogre::Matrix4 toOgre(const glm::mat4& m) // glm column-major -> Ogre row-major
{
    Ogre::Matrix4 o;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            o[r][c] = m[c][r];
    return o;
}

} // namespace

struct Renderer::Impl {
    RenderCore core;
    Particles particles; // data-driven pooled particle effects
    std::vector<Ogre::SceneNode*> nodes; // nodes[id-1]; id 1 == scene root
    std::vector<std::string> meshNames;  // meshNames[id-1]
    std::vector<Ogre::Light*> lights;    // lights[id-1]
    std::vector<Ogre::BillboardSet*> sprites; // sprites[id-1]
    std::vector<std::string> spriteMaterials;
    std::vector<std::string> generatedTextures;
    int nameCounter = 0;
    EnvState env;
    // Original sub-entity materials, saved while the wireframe debug view
    // holds every entity on PSX/DebugWireframe.
    std::unordered_map<Ogre::SubEntity*, std::string> savedMaterials;

    // Immediate-mode debug line overlay (physics collider wireframes etc.)
    // Created lazily on first use; recreated if clearScene destroyed it.
    Ogre::ManualObject* debugLines = nullptr;
    // Post-chain settings stashed by setWireframeDebug(true) and restored
    // on toggle-off (the view bypasses them so lines stay crisp).
    struct {
        int pixelSize = 3;
        bool dither = false, bloom = true, grade = false;
    } preWireframe;

    // Static geometry batches: records kept after build() so the wireframe
    // debug view can rebuild with the wire material and restore.
    struct StaticBatch {
        Ogre::StaticGeometry* sg = nullptr;
        struct Rec {
            MeshHandle mesh;
            std::string material;
            glm::vec3 pos;
            float yawDeg;
        };
        std::vector<Rec> recs;
        bool built = false;
    };
    std::vector<StaticBatch> staticBatches; // staticBatches[id-1]

    // (Re)fills a StaticGeometry from its records. materialOverride empty =
    // each record's own material (normal view); non-empty = forced (wireframe).
    void fillStaticBatch(StaticBatch& b, const std::string& materialOverride)
    {
        Ogre::SceneManager* sm = core.sceneMgr();
        if (b.built)
            b.sg->reset();
        for (const auto& rec : b.recs) {
            const std::string& mat =
                materialOverride.empty() ? rec.material : materialOverride;
            Ogre::Entity* e = sm->createEntity(mesh(rec.mesh, "staticBatch"));
            e->setMaterialName(mat);
            e->setCastShadows(false);
            b.sg->addEntity(e, toOgre(rec.pos),
                            Ogre::Quaternion(Ogre::Degree(rec.yawDeg),
                                             Ogre::Vector3::UNIT_Y));
            sm->destroyEntity(e);
        }
        b.sg->build();
        b.built = true;
    }

    Ogre::SceneNode* node(NodeHandle h, const char* what)
    {
        if (!h.valid() || h.id > nodes.size())
            log::fatal("Renderer: invalid node handle %u in %s", h.id, what);
        return nodes[h.id - 1];
    }
    const std::string& mesh(MeshHandle h, const char* what)
    {
        if (!h.valid() || h.id > meshNames.size())
            log::fatal("Renderer: invalid mesh handle %u in %s", h.id, what);
        return meshNames[h.id - 1];
    }
    MeshHandle registerMesh(std::string name)
    {
        meshNames.push_back(std::move(name));
        return {static_cast<uint32_t>(meshNames.size())};
    }
    std::string nextName(const char* prefix)
    {
        return std::string(prefix) + std::to_string(++nameCounter);
    }
};

Renderer::Renderer() : mImpl(new Impl) {}
Renderer::~Renderer() = default;

MeshHandle Renderer::loadObj(const std::string& path, const glm::mat4* bake)
{
    const std::string name = mImpl->nextName("mesh");
    try {
        ObjLoader::load(path, name,
                        bake ? toOgre(*bake) : Ogre::Matrix4::IDENTITY);
    } catch (const std::exception& e) {
        log::fatal("Renderer: loadObj('%s') failed: %s", path.c_str(), e.what());
    }
    return mImpl->registerMesh(name);
}

MeshHandle Renderer::createInteriorBox(float size, int subdivide)
{
    const std::string name = mImpl->nextName("mesh");
    ProceduralMeshes::createInteriorBox(name, size, subdivide);
    return mImpl->registerMesh(name);
}

MeshHandle Renderer::createPlane(float size)
{
    const std::string name = mImpl->nextName("mesh");
    ProceduralMeshes::createPlane(name, size);
    return mImpl->registerMesh(name);
}

MeshHandle Renderer::createBeveledBox(float bevel)
{
    const std::string name = mImpl->nextName("beveled_box");
    ProceduralMeshes::createBeveledBox(name, bevel);
    return mImpl->registerMesh(name);
}

MeshHandle Renderer::createCone(float radius, float height, int segments)
{
    const std::string name = mImpl->nextName("cone");
    ProceduralMeshes::createCone(name, radius, height, segments);
    return mImpl->registerMesh(name);
}

MeshHandle Renderer::createSphere(float radius, int rings, int segments)
{
    const std::string name = mImpl->nextName("sphere");
    ProceduralMeshes::createSphere(name, radius, rings, segments);
    return mImpl->registerMesh(name);
}

MeshHandle Renderer::createPortalRing(float outerRadius, float innerRadius,
                                      float depth, int segments)
{
    const std::string name = mImpl->nextName("portal_ring");
    ProceduralMeshes::createPortalRing(name, outerRadius, innerRadius, depth,
                                       segments);
    return mImpl->registerMesh(name);
}

MeshHandle Renderer::createPortalDisc(float radius, int segments)
{
    const std::string name = mImpl->nextName("portal_disc");
    ProceduralMeshes::createPortalDisc(name, radius, segments);
    return mImpl->registerMesh(name);
}

NodeHandle Renderer::createNode(NodeHandle parent, glm::vec3 position)
{
    Ogre::SceneNode* n =
        mImpl->node(parent, "createNode")->createChildSceneNode(toOgre(position));
    mImpl->nodes.push_back(n);
    return {static_cast<uint32_t>(mImpl->nodes.size())};
}

void Renderer::setPosition(NodeHandle node, glm::vec3 position)
{
    mImpl->node(node, "setPosition")->setPosition(toOgre(position));
}

void Renderer::setOrientation(NodeHandle node, glm::quat orientation)
{
    mImpl->node(node, "setOrientation")->setOrientation(toOgre(orientation));
}

void Renderer::setScale(NodeHandle node, glm::vec3 scale)
{
    mImpl->node(node, "setScale")->setScale(toOgre(scale));
}

void Renderer::setNodeVisible(NodeHandle node, bool show)
{
    mImpl->node(node, "setNodeVisible")->setVisible(show);
}

void Renderer::destroyNode(NodeHandle node)
{
    if (!node.valid() || node.id > mImpl->nodes.size()) return;
    Ogre::SceneNode* n = mImpl->nodes[node.id - 1];
    if (!n) return;
    Ogre::SceneManager* sm = mImpl->core.sceneMgr();

    // Snapshot attached objects (can't mutate the node's map while iterating).
    std::vector<Ogre::MovableObject*> objs;
    objs.reserve(n->numAttachedObjects());
    for (size_t i = 0; i < n->numAttachedObjects(); ++i)
        objs.push_back(n->getAttachedObject(i));
    for (Ogre::MovableObject* o : objs) {
        n->detachObject(o);
        // Pool-owned particle systems recycle themselves; only detach them.
        if (o->getMovableType() == "ParticleSystem") continue;
        if (o->getMovableType() == "Light")
            for (auto& lp : mImpl->lights) if (lp == o) lp = nullptr;
        sm->destroyMovableObject(o); // handles Light/Entity/etc.
    }
    n->removeAndDestroyAllChildren();
    if (n->getParentSceneNode()) n->getParentSceneNode()->removeChild(n);
    sm->destroySceneNode(n);
    mImpl->nodes[node.id - 1] = nullptr; // stale slot; handle never reused
}

void Renderer::attachMesh(NodeHandle node, MeshHandle mesh,
                          const std::string& materialName, bool castShadows,
                          bool renderOnTop)
{
    if (!Ogre::MaterialManager::getSingleton().getByName(materialName))
        log::fatal("Renderer: unknown material '%s'", materialName.c_str());
    Ogre::Entity* e =
        mImpl->core.sceneMgr()->createEntity(mImpl->mesh(mesh, "attachMesh"));
    e->setMaterialName(materialName);
    e->setCastShadows(castShadows);
    if (renderOnTop)
        e->setRenderQueueGroup(Ogre::RENDER_QUEUE_8);
    if (mImpl->env.wireframe) { // debug view active: join it immediately
        for (Ogre::SubEntity* se : e->getSubEntities()) {
            mImpl->savedMaterials[se] = materialName;
            se->setMaterialName("PSX/DebugWireframe");
        }
    }
    mImpl->node(node, "attachMesh")->attachObject(e);
}

std::string Renderer::createSpriteMaterial(const SpriteClip& clip)
{
    const char* base = clip.blend == SpriteBlend::Alpha ? "Sprite/Alpha"
                     : clip.blend == SpriteBlend::Additive ? "Sprite/Additive"
                     : clip.blend == SpriteBlend::Overlay ? "Sprite/Overlay"
                                                          : "Sprite/Opaque";
    Ogre::MaterialPtr source = Ogre::MaterialManager::getSingleton().getByName(base);
    if (!source)
        log::fatal("Renderer: sprite template '%s' is missing", base);
    const std::string name = mImpl->nextName("sprite_material");
    Ogre::MaterialPtr material = source->clone(name);
    Ogre::Pass* pass = material->getTechnique(0)->getPass(0);
    std::string texture = clip.texture;
    const bool generated = bool(Ogre::TextureManager::getSingleton().getByName(texture));
    if (!generated &&
        !Ogre::ResourceGroupManager::getSingleton().resourceExistsInAnyGroup(texture)) {
        log::error("Sprite: texture '%s' is missing; using PINKY.png",
                   texture.c_str());
        texture = "PINKY.png";
    }
    pass->getTextureUnitState(0)->setTextureName(texture);
    auto vp = pass->getVertexProgramParameters();
    vp->setNamedConstant("spriteGrid",
                         Ogre::Vector2(float(std::max(1, clip.grid.x)),
                                       float(std::max(1, clip.grid.y))));
    vp->setNamedConstant("spriteFrameCount", float(std::max(1, clip.frameCount)));
    vp->setNamedConstant("spriteFps", std::max(0.0f, clip.framesPerSecond));
    vp->setNamedConstant("spriteScroll", Ogre::Vector2(clip.scrollVelocity.x,
                                                        clip.scrollVelocity.y));
    vp->setNamedConstant("spriteUvScale", Ogre::Vector2(clip.uvScale.x,
                                                         clip.uvScale.y));
    vp->setNamedConstant("spritePhase", clip.phaseSeconds);
    auto fp = pass->getFragmentProgramParameters();
    fp->setNamedConstant("spriteTint", Ogre::Vector4(clip.tint.r, clip.tint.g,
                                                      clip.tint.b, clip.tint.a));
    fp->setNamedConstant("spriteAlphaCutoff", clip.alphaCutoff);
    material->load();
    mImpl->spriteMaterials.push_back(name);
    return name;
}

SpriteHandle Renderer::attachSprite(NodeHandle node, const SpriteClip& clip)
{
    Ogre::SceneManager* sm = mImpl->core.sceneMgr();
    Ogre::BillboardSet* set = sm->createBillboardSet(mImpl->nextName("sprite"), 1);
    set->setMaterialName(createSpriteMaterial(clip));
    set->setDefaultDimensions(std::max(0.001f, clip.worldSize.x),
                              std::max(0.001f, clip.worldSize.y));
    set->createBillboard(Ogre::Vector3::ZERO, Ogre::ColourValue::White);
    mImpl->node(node, "attachSprite")->attachObject(set);
    mImpl->sprites.push_back(set);
    return {static_cast<uint32_t>(mImpl->sprites.size())};
}

SpriteHandle Renderer::attachTextSprite(NodeHandle node, const std::string& text,
                                        const TextSpriteStyle& style)
{
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    ImFont* font = atlas->Fonts.empty() ? nullptr : atlas->Fonts[0];
    if (!font)
        log::fatal("Renderer: text sprite requested before font atlas exists");

    unsigned char* atlasPixels = nullptr;
    int atlasWidth = 0, atlasHeight = 0;
    atlas->GetTexDataAsRGBA32(&atlasPixels, &atlasWidth, &atlasHeight);
    const int padding = std::max(2, style.paddingPixels);
    const auto textWidth = [&](const std::string& value) {
        float width = 0.0f;
        for (unsigned char c : value)
            width += font->FindGlyph(c)->AdvanceX;
        return width;
    };
    // The current UI font is ASCII. Collapse UTF-8 punctuation to one dash
    // instead of producing one '?' for every continuation byte.
    std::string printable;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 128) {
            printable.push_back(char(c));
            ++i;
        } else {
            printable.push_back('-');
            ++i;
            while (i < text.size() &&
                   (static_cast<unsigned char>(text[i]) & 0xc0) == 0x80)
                ++i;
        }
    }
    std::vector<std::string> lines(1);
    std::string word;
    const auto appendWord = [&]() {
        if (word.empty()) return;
        std::string candidate = lines.back().empty()
            ? word : lines.back() + " " + word;
        if (!lines.back().empty() &&
            textWidth(candidate) > float(std::max(32, style.maxWidthPixels))) {
            lines.push_back(word);
        } else {
            lines.back() = std::move(candidate);
        }
        word.clear();
    };
    for (char c : printable) {
        if (c == '\n') {
            appendWord();
            if (!lines.back().empty()) lines.emplace_back();
        } else if (c == ' ' || c == '\t') {
            appendWord();
        } else {
            word.push_back(c);
        }
    }
    appendWord();
    if (lines.back().empty() && lines.size() > 1) lines.pop_back();
    std::vector<float> lineWidths;
    float widest = 0.0f;
    for (const std::string& line : lines) {
        const float lineWidth = textWidth(line);
        lineWidths.push_back(lineWidth);
        widest = std::max(widest, lineWidth);
    }
    const int lineHeight = int(std::ceil(font->FontSize));
    const int lineSpacing = std::max(0, style.lineSpacingPixels);
    const int accentWidth = std::max(0, style.accentWidthPixels);
    const int accentGutter = accentWidth > 0 ? accentWidth + 2 : 0;
    const int width = std::max(
        8, int(std::ceil(widest)) + padding * 2 + accentGutter);
    const int height = std::max(
        8, lineHeight * int(lines.size()) +
               lineSpacing * std::max(0, int(lines.size()) - 1) + padding * 2);
    std::vector<unsigned char> pixels(size_t(width * height * 4), 0);
    std::vector<std::pair<std::regex, glm::vec4>> colourRules;
    colourRules.reserve(style.colourRules.size());
    for (const auto& rule : style.colourRules) {
        try {
            colourRules.emplace_back(
                std::regex(rule.pattern, std::regex::icase), rule.colour);
        } catch (const std::regex_error&) {
            log::error("TextSprite: invalid colour regex '%s'",
                       rule.pattern.c_str());
        }
    }
    const auto byte = [](float v) {
        return static_cast<unsigned char>(glm::clamp(v, 0.0f, 1.0f) * 255.0f);
    };
    const auto put = [&](int x, int y, glm::vec4 colour) {
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        const size_t i = size_t((y * width + x) * 4);
        pixels[i + 0] = byte(colour.r); pixels[i + 1] = byte(colour.g);
        pixels[i + 2] = byte(colour.b); pixels[i + 3] = byte(colour.a);
    };
    // Overlay plaques are intentionally opaque, so antialiased glyph alpha
    // must be resolved into the plaque here rather than left for GPU blend.
    const auto blendText = [&](int x, int y, glm::vec4 colour, float coverage) {
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        const size_t i = size_t((y * width + x) * 4);
        const float a = glm::clamp(coverage * colour.a, 0.0f, 1.0f);
        pixels[i + 0] = byte(glm::mix(float(pixels[i + 0]) / 255.0f, colour.r, a));
        pixels[i + 1] = byte(glm::mix(float(pixels[i + 1]) / 255.0f, colour.g, a));
        pixels[i + 2] = byte(glm::mix(float(pixels[i + 2]) / 255.0f, colour.b, a));
        pixels[i + 3] = 255;
    };
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const bool border = x < 2 || y < 2 || x >= width - 2 || y >= height - 2;
            put(x, y, border ? style.borderColour : style.backgroundColour);
        }
    for (int y = 2; y < height - 2; ++y)
        for (int x = 2; x < 2 + accentWidth; ++x)
            put(x, y, style.accentColour);
    for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
      float pen = float(accentGutter) +
          (float(width - accentGutter) - lineWidths[lineIndex]) * 0.5f;
      const float lineTop = float(padding + int(lineIndex) *
                                  (lineHeight + lineSpacing));
      const std::string& line = lines[lineIndex];
      for (size_t charIndex = 0; charIndex < line.size(); ++charIndex) {
        const unsigned char c = static_cast<unsigned char>(line[charIndex]);
        const ImFontGlyph* glyph = font->FindGlyph(c);
        glm::vec4 colour = style.textColour;
        if (c != ' ') {
            size_t begin = charIndex, end = charIndex + 1;
            while (begin > 0 && line[begin - 1] != ' ') --begin;
            while (end < line.size() && line[end] != ' ') ++end;
            const std::string currentWord = line.substr(begin, end - begin);
            for (const auto& rule : colourRules)
                if (std::regex_search(currentWord, rule.first)) {
                    colour = rule.second;
                    break;
                }
        }
        const int sx0 = int(std::round(glyph->U0 * atlasWidth));
        const int sy0 = int(std::round(glyph->V0 * atlasHeight));
        const int sx1 = int(std::round(glyph->U1 * atlasWidth));
        const int sy1 = int(std::round(glyph->V1 * atlasHeight));
        const int dx0 = int(std::round(pen + glyph->X0));
        const int dy0 = int(std::round(lineTop + glyph->Y0));
        for (int sy = sy0; sy < sy1; ++sy)
            for (int sx = sx0; sx < sx1; ++sx) {
                const unsigned char alpha = atlasPixels[(sy * atlasWidth + sx) * 4 + 3];
                if (!alpha) continue;
                blendText(dx0 + sx - sx0, dy0 + sy - sy0, colour,
                          float(alpha) / 255.0f);
            }
        pen += glyph->AdvanceX;
      }
    }

    const std::string textureName = mImpl->nextName("text_sprite_texture");
    Ogre::TexturePtr texture = Ogre::TextureManager::getSingleton().createManual(
        textureName, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
        Ogre::TEX_TYPE_2D, uint32_t(width), uint32_t(height), 0,
        Ogre::PF_BYTE_RGBA, Ogre::TU_STATIC_WRITE_ONLY);
    const Ogre::PixelBox source(uint32_t(width), uint32_t(height), 1,
                                Ogre::PF_BYTE_RGBA, pixels.data());
    texture->getBuffer()->blitFromMemory(source);
    mImpl->generatedTextures.push_back(textureName);

    SpriteClip clip;
    clip.texture = textureName;
    const float pixelToWorld = style.worldHeight /
        float(lineHeight + padding * 2);
    clip.worldSize = {float(width) * pixelToWorld,
                      float(height) * pixelToWorld};
    // Text plaques are deliberately opaque: the PSX compositor consumes a
    // second normal/depth target, and blending that target makes translucent
    // billboards invalidate the full-screen reconstruction pass.
    clip.blend = SpriteBlend::Overlay;
    const SpriteHandle sprite = attachSprite(node, clip);
    // Queue 80 is after ordinary scene geometry but remains inside the PSX
    // compositor's scene pass (queue 99/overlay is intentionally excluded).
    mImpl->sprites[sprite.id - 1]->setRenderQueueGroup(Ogre::RENDER_QUEUE_8);
    return sprite;
}

void Renderer::setSpriteVisible(SpriteHandle sprite, bool visible)
{
    if (!sprite.valid() || sprite.id > mImpl->sprites.size())
        log::fatal("Renderer: invalid sprite handle %u", sprite.id);
    mImpl->sprites[sprite.id - 1]->setVisible(visible);
}

StaticBatchHandle Renderer::createStaticBatch(glm::vec3 regionSize)
{
    Impl::StaticBatch b;
    b.sg = mImpl->core.sceneMgr()->createStaticGeometry(
        mImpl->nextName("staticbatch"));
    b.sg->setRegionDimensions(toOgre(regionSize));
    b.sg->setCastShadows(false); // tiles receive shadows, never cast
    mImpl->staticBatches.push_back(std::move(b));
    return {static_cast<uint32_t>(mImpl->staticBatches.size())};
}

void Renderer::addToStaticBatch(StaticBatchHandle batch, MeshHandle mesh,
                                const std::string& materialName,
                                glm::vec3 pos, float yawDeg)
{
    if (!batch.valid() || batch.id > mImpl->staticBatches.size())
        log::fatal("Renderer: invalid batch handle %u in addToStaticBatch",
                   batch.id);
    if (!Ogre::MaterialManager::getSingleton().getByName(materialName))
        log::fatal("Renderer: unknown material '%s'", materialName.c_str());
    mImpl->staticBatches[batch.id - 1].recs.push_back(
        {mesh, materialName, pos, yawDeg});
}

void Renderer::buildStaticBatch(StaticBatchHandle batch)
{
    if (!batch.valid() || batch.id > mImpl->staticBatches.size())
        log::fatal("Renderer: invalid batch handle %u in buildStaticBatch",
                   batch.id);
    // Respect an already-active wireframe view (PSX_WIREFRAME startup).
    mImpl->fillStaticBatch(mImpl->staticBatches[batch.id - 1],
                           mImpl->env.wireframe ? "PSX/DebugWireframe" : "");
}

void Renderer::setStaticBatchVisible(StaticBatchHandle batch, bool visible)
{
    if (!batch.valid() || batch.id > mImpl->staticBatches.size())
        log::fatal("Renderer: invalid batch handle %u in setStaticBatchVisible",
                   batch.id);
    mImpl->staticBatches[batch.id - 1].sg->setVisible(visible);
}

void Renderer::clearScene()
{
    Ogre::SceneManager* sm = mImpl->core.sceneMgr();
    // Detach + destroy every SceneNode under the root, then free the objects
    // those nodes referenced (Ogre owns them; removing nodes alone leaks).
    mImpl->particles.clear(); // drop pooled-system bookkeeping before Ogre frees them
    sm->getRootSceneNode()->removeAndDestroyAllChildren();
    sm->destroyAllStaticGeometry();
    sm->destroyAllParticleSystems();
    sm->destroyAllBillboardSets();
    sm->destroyAllEntities();
    sm->destroyAllManualObjects();
    sm->destroyAllLights();
    auto& materials = Ogre::MaterialManager::getSingleton();
    for (const std::string& name : mImpl->spriteMaterials)
        if (materials.getByName(name)) materials.remove(name);
    auto& textures = Ogre::TextureManager::getSingleton();
    for (const std::string& name : mImpl->generatedTextures)
        if (textures.getByName(name)) textures.remove(name);
    // Entities are gone; now free their Ogre::Mesh resources (otherwise the
    // next level's loadObj collides on the same resource name). Only the
    // meshes this Renderer created are in meshNames.
    auto& mm = Ogre::MeshManager::getSingleton();
    for (const std::string& name : mImpl->meshNames)
        if (mm.getByName(name))
            mm.remove(name);
    // Reset handle bookkeeping; re-register the root as kRootNode (id 1), the
    // same way detail::registerRoot does at startup. nameCounter stays
    // MONOTONIC across levels so freshly created Ogre objects can never reuse
    // a name that a lingering resource still holds.
    mImpl->nodes.clear();
    mImpl->lights.clear();
    mImpl->sprites.clear();
    mImpl->spriteMaterials.clear();
    mImpl->generatedTextures.clear();
    mImpl->staticBatches.clear();
    mImpl->savedMaterials.clear();
    mImpl->meshNames.clear();
    mImpl->debugLines = nullptr; // destroyAllManualObjects freed it
    mImpl->nodes.push_back(sm->getRootSceneNode());
}

ParticleEffectId Renderer::registerParticleEffect(const ParticleEffectDesc& desc)
{
    return mImpl->particles.registerEffect(desc);
}

ParticleEffectId Renderer::particleEffectId(const std::string& name)
{
    return mImpl->particles.find(name);
}

ParticlesHandle Renderer::spawnParticles(const std::string& name, NodeHandle parent,
                                         glm::vec3 localPos)
{
    return spawnParticles(mImpl->particles.find(name), parent, localPos);
}

ParticlesHandle Renderer::spawnParticles(const std::string& name, glm::vec3 worldPos)
{
    return spawnParticles(mImpl->particles.find(name), worldPos);
}

ParticlesHandle Renderer::spawnParticles(ParticleEffectId fx, NodeHandle parent,
                                         glm::vec3 localPos)
{
    Ogre::SceneNode* n = mImpl->node(parent, "spawnParticles");
    // A non-zero local offset gets its own child node so the particle inherits
    // an offset transform without disturbing the parent (the particle system
    // itself has no per-attachment offset in Ogre).
    if (glm::dot(localPos, localPos) > 1e-8f)
        n = n->createChildSceneNode(toOgre(localPos));
    return mImpl->particles.spawn(fx, n, glm::vec3(0.0f));
}

ParticlesHandle Renderer::spawnParticles(ParticleEffectId fx, glm::vec3 worldPos)
{
    NodeHandle n = createNode(kRootNode, worldPos);
    return spawnParticles(fx, n, glm::vec3(0.0f));
}

void Renderer::stopParticles(ParticlesHandle h) { mImpl->particles.stop(h); }
void Renderer::despawnParticles(ParticlesHandle h) { mImpl->particles.despawn(h); }
void Renderer::setParticleQuality(float q) { mImpl->particles.setQuality(q); }
void Renderer::updateParticles(float dt) { mImpl->particles.update(dt); }

void Renderer::attachCamera(NodeHandle node)
{
    Ogre::Camera* cam = mImpl->core.camera();
    if (cam->getParentSceneNode())
        cam->detachFromParent();
    mImpl->node(node, "attachCamera")->attachObject(cam);
}

LightHandle Renderer::attachLight(NodeHandle node, const LightDesc& desc)
{
    Ogre::Light* l = mImpl->core.sceneMgr()->createLight();
    l->setType(desc.type == LightDesc::Type::Directional
                   ? Ogre::Light::LT_DIRECTIONAL
                   : Ogre::Light::LT_POINT);
    l->setDiffuseColour(toColour(desc.colour));
    l->setSpecularColour(Ogre::ColourValue::Black); // PSX: specular_disabled
    // Ogre culls lights against the camera frustum using the attenuation
    // range (findLightsAffectingFrustum); a tight range makes wall lights
    // pop off the moment the source leaves the view. Keep Ogre's range
    // huge so lights stay registered, and pass the real falloff range
    // through the constant-attenuation slot -- the PSX shader reads
    // lightAtten.y (see psx_lighting.glsl) and ignores the rest.
    if (desc.type == LightDesc::Type::Point)
        l->setAttenuation(1000.0f, desc.range, 0.0f, 0.0f);
    l->setCastShadows(desc.castShadows);
    // The huge Ogre-side attenuation range would also become the stencil
    // volume extrusion distance; cap it to something scene-sized.
    if (desc.castShadows)
        l->setShadowFarDistance(desc.range * 2.0f);
    mImpl->node(node, "attachLight")->attachObject(l);
    mImpl->lights.push_back(l);
    return {static_cast<uint32_t>(mImpl->lights.size())};
}

void Renderer::setLightColour(LightHandle light, glm::vec3 colour)
{
    if (!light.valid() || light.id > mImpl->lights.size())
        log::fatal("Renderer: invalid light handle %u in setLightColour",
                   light.id);
    mImpl->lights[light.id - 1]->setDiffuseColour(toColour(colour));
}

void Renderer::setCameraFov(float degrees)
{
    mImpl->env.fovDeg = degrees;
    mImpl->core.camera()->setFOVy(Ogre::Degree(degrees));
}

void Renderer::setCameraClip(float nearDist, float farDist)
{
    nearDist = std::clamp(nearDist, 0.01f, 10.0f);
    farDist = std::max(farDist, nearDist + 1.0f);
    mImpl->env.nearClip = nearDist;
    mImpl->env.farClip = farDist;
    mImpl->core.camera()->setNearClipDistance(nearDist);
    mImpl->core.camera()->setFarClipDistance(farDist);
}

namespace {
// Applies `set` to every VS/FS param set that defines paramName.
template <typename SetFn>
void applyMaterialParam(const std::string& materialName,
                        const std::string& paramName, SetFn&& set)
{
    Ogre::MaterialPtr mat =
        Ogre::MaterialManager::getSingleton().getByName(materialName);
    if (!mat)
        log::fatal("Renderer: unknown material '%s'", materialName.c_str());
    bool found = false;
    for (Ogre::Technique* tech : mat->getTechniques()) {
        for (Ogre::Pass* pass : tech->getPasses()) {
            Ogre::GpuProgramParametersSharedPtr sets[2];
            if (pass->hasVertexProgram())
                sets[0] = pass->getVertexProgramParameters();
            if (pass->hasFragmentProgram())
                sets[1] = pass->getFragmentProgramParameters();
            for (auto& params : sets) {
                if (params && params->_findNamedConstantDefinition(paramName, false)) {
                    set(params);
                    found = true;
                }
            }
        }
    }
    if (!found)
        log::fatal("Renderer: material '%s' has no param '%s'",
                   materialName.c_str(), paramName.c_str());
}
} // namespace

void Renderer::setMaterialParam(const std::string& m, const std::string& p, float v)
{
    applyMaterialParam(m, p, [&](auto& params) { params->setNamedConstant(p, v); });
    mImpl->core.markPostChainDirty();
}
void Renderer::setMaterialParam(const std::string& m, const std::string& p, glm::vec2 v)
{
    applyMaterialParam(m, p, [&](auto& params) {
        params->setNamedConstant(p, Ogre::Vector2(v.x, v.y));
    });
    mImpl->core.markPostChainDirty();
}
void Renderer::setMaterialParam(const std::string& m, const std::string& p, glm::vec3 v)
{
    applyMaterialParam(m, p, [&](auto& params) {
        params->setNamedConstant(p, Ogre::Vector3(v.x, v.y, v.z));
    });
    mImpl->core.markPostChainDirty();
}
void Renderer::setMaterialParam(const std::string& m, const std::string& p, glm::vec4 v)
{
    applyMaterialParam(m, p, [&](auto& params) {
        params->setNamedConstant(p, Ogre::Vector4(v.x, v.y, v.z, v.w));
    });
    mImpl->core.markPostChainDirty();
}

void Renderer::setAmbient(glm::vec3 colour)
{
    mImpl->env.ambient = colour;
    mImpl->core.sceneMgr()->setAmbientLight(toColour(colour));
}

void Renderer::setFog(glm::vec3 colour, float expDensity)
{
    mImpl->env.fogColour = colour;
    mImpl->env.fogDensity = expDensity;
    mImpl->core.sceneMgr()->setFog(Ogre::FOG_EXP, toColour(colour), expDensity);
}

void Renderer::setBackground(glm::vec3 colour)
{
    mImpl->env.background = colour;
    mImpl->core.viewport()->setBackgroundColour(toColour(colour));
}

void Renderer::setDitherEnabled(bool enabled)
{
    mImpl->env.dither = enabled;
    // The post chain hosts pixelation/bloom too, so it stays on;
    // "dither off" only bypasses the quantization inside the dither pass.
    mImpl->core.enablePostChain();
    setMaterialParam("PSX/DitherPost", "ditherEnabled", enabled ? 1.0f : 0.0f);
}

void Renderer::setPixelSize(int pixelSize)
{
    mImpl->env.pixelSize = std::clamp(pixelSize, 1, 16);
    mImpl->core.setPixelSize(mImpl->env.pixelSize);
}

void Renderer::setPerPixelLightingEnabled(bool enabled)
{
    mImpl->env.perPixelLighting = enabled;
    const float value = enabled ? 1.0f : 0.0f;
    auto it = Ogre::MaterialManager::getSingleton().getResourceIterator();
    while (it.hasMoreElements()) {
        auto mat = Ogre::static_pointer_cast<Ogre::Material>(it.getNext());
        if (!mat || !mat->isLoaded())
            continue; // unloaded materials keep the program default (on)
        for (Ogre::Technique* tech : mat->getTechniques()) {
            for (Ogre::Pass* pass : tech->getPasses()) {
                // perPixelLighting is an FS-only branch; vertex programs
                // never declare it, so skip them entirely.
                if (!pass->hasFragmentProgram())
                    continue;
                // Each loaded pass owns a clone of the program's default
                // params, so mutating here never leaks across materials.
                auto params = pass->getFragmentProgramParameters();
                if (params &&
                    params->_findNamedConstantDefinition("perPixelLighting", false))
                    params->setNamedConstant("perPixelLighting", value);
            }
        }
    }
}

void Renderer::setGlobalMaterialParam(const std::string& paramName, float value)
{
    auto it = Ogre::MaterialManager::getSingleton().getResourceIterator();
    while (it.hasMoreElements()) {
        auto mat = Ogre::static_pointer_cast<Ogre::Material>(it.getNext());
        if (!mat || !mat->isLoaded())
            continue;
        for (Ogre::Technique* tech : mat->getTechniques()) {
            for (Ogre::Pass* pass : tech->getPasses()) {
                Ogre::GpuProgramParametersSharedPtr sets[2];
                if (pass->hasVertexProgram())
                    sets[0] = pass->getVertexProgramParameters();
                if (pass->hasFragmentProgram())
                    sets[1] = pass->getFragmentProgramParameters();
                for (auto& params : sets)
                    if (params &&
                        params->_findNamedConstantDefinition(paramName, false))
                        params->setNamedConstant(paramName, value);
            }
        }
    }
    mImpl->core.markPostChainDirty();
}

void Renderer::setOmniAttenuation(float exponent)
{
    mImpl->env.omniAttenuation = exponent;
    setGlobalMaterialParam("omniAttenuation", exponent);
}

void Renderer::setLightSteps(float steps)
{
    mImpl->env.lightSteps = steps;
    setGlobalMaterialParam("lightSteps", steps);
}

void Renderer::setLightStepSoftness(float softness)
{
    mImpl->env.lightStepSoftness = softness;
    setGlobalMaterialParam("lightStepSoftness", softness);
}

void Renderer::setFogDesatBoost(float boost)
{
    mImpl->env.fogDesatBoost = boost;
    setGlobalMaterialParam("fogDesatBoost", boost);
}

void Renderer::setBloomEnabled(bool enabled)
{
    mImpl->env.bloom = enabled;
    setMaterialParam("PSX/BloomComposite", "bloomEnabled",
                     enabled ? 1.0f : 0.0f);
}

void Renderer::setBloomParams(float threshold, float intensity)
{
    mImpl->env.bloomThreshold = threshold;
    mImpl->env.bloomIntensity = intensity;
    setMaterialParam("PSX/BloomBright", "bloomThreshold", threshold);
    setMaterialParam("PSX/BloomComposite", "bloomIntensity", intensity);
}

void Renderer::setWireframeDebug(bool enabled)
{
    if (mImpl->env.wireframe == enabled)
        return;
    mImpl->env.wireframe = enabled;
    for (const auto& [name, mo] : mImpl->core.sceneMgr()->getMovableObjects("Entity")) {
        auto* e = static_cast<Ogre::Entity*>(mo);
        for (Ogre::SubEntity* se : e->getSubEntities()) {
            if (enabled) {
                mImpl->savedMaterials[se] = se->getMaterial()->getName();
                se->setMaterialName("PSX/DebugWireframe");
            } else {
                auto found = mImpl->savedMaterials.find(se);
                if (found != mImpl->savedMaterials.end())
                    se->setMaterialName(found->second);
            }
        }
    }
    if (!enabled)
        mImpl->savedMaterials.clear();
    // Static batches have no per-entity material swap: rebuild each built
    // batch from its records with the wire material (on) or the recorded
    // originals (off).
    for (auto& b : mImpl->staticBatches)
        if (b.built)
            mImpl->fillStaticBatch(b, enabled ? "PSX/DebugWireframe" : "");
    // The post chain smears the 1px lines, so bypass every post effect while
    // the view is up and restore it afterwards.
    // Bypass every post effect while the view is up, restore after.
    if (enabled) {
        mImpl->preWireframe = {mImpl->env.pixelSize, mImpl->env.dither,
                               mImpl->env.bloom,
                               mImpl->env.grade};
        setPixelSize(1);
        // Wireframe is a diagnostic view: no ink/highlight pass should alter
        // its lines or introduce false contour noise.
        setMaterialParam("PSX/PixelStylize", "stylizeEnabled", 0.0f);
        setDitherEnabled(false);
        setBloomEnabled(false);
        setGradeEnabled(false);
    } else {
        setPixelSize(mImpl->preWireframe.pixelSize);
        setMaterialParam("PSX/PixelStylize", "stylizeEnabled", 1.0f);
        setDitherEnabled(mImpl->preWireframe.dither);
        setBloomEnabled(mImpl->preWireframe.bloom);
        setGradeEnabled(mImpl->preWireframe.grade);
    }
}

void Renderer::setGradeEnabled(bool enabled)
{
    mImpl->env.grade = enabled;
    setMaterialParam("PSX/DitherPost", "gradeEnabled", enabled ? 1.0f : 0.0f);
}

void Renderer::setGradeParams(float desaturate, float contrast,
                              glm::vec3 shadowTint, glm::vec3 midTint)
{
    mImpl->env.gradeDesaturate = desaturate;
    mImpl->env.gradeContrast = contrast;
    mImpl->env.gradeShadowTint = shadowTint;
    mImpl->env.gradeMidTint = midTint;
    setMaterialParam("PSX/DitherPost", "gradeDesaturate", desaturate);
    setMaterialParam("PSX/DitherPost", "gradeContrast", contrast);
    setMaterialParam("PSX/DitherPost", "gradeShadowTint", shadowTint);
    setMaterialParam("PSX/DitherPost", "gradeMidTint", midTint);
}

const EnvState& Renderer::envState() const { return mImpl->env; }

void Renderer::writeScreenshot(const std::string& path)
{
    mImpl->core.writeScreenshot(path);
}

void Renderer::setDebugLines(const std::vector<DebugLine>& lines)
{
    Ogre::SceneManager* sm = mImpl->core.sceneMgr();
    // Lazy creation (or re-creation after clearScene).
    if (!mImpl->debugLines) {
        mImpl->debugLines = sm->createManualObject("__eng_debug_lines");
        mImpl->debugLines->setDynamic(true);
        sm->getRootSceneNode()->createChildSceneNode()->attachObject(
            mImpl->debugLines);
    }
    mImpl->debugLines->clear();
    if (lines.empty())
        return;
    // PSX/DebugLines: unlit, per-vertex colour, depth_write off (declared in
    // engine/assets/materials/psx.material + debug_lines.frag).
    const std::string matName = "PSX/DebugLines";
    mImpl->debugLines->begin(matName, Ogre::RenderOperation::OT_LINE_LIST);
    for (const auto& l : lines) {
        mImpl->debugLines->position(l.a.x, l.a.y, l.a.z);
        mImpl->debugLines->colour(Ogre::ColourValue(l.colour.r, l.colour.g, l.colour.b, 1.0f));
        mImpl->debugLines->position(l.b.x, l.b.y, l.b.z);
        mImpl->debugLines->colour(Ogre::ColourValue(l.colour.r, l.colour.g, l.colour.b, 1.0f));
    }
    mImpl->debugLines->end();
}

namespace detail {

RenderCore& coreOf(Renderer& r) { return r.mImpl->core; }

void registerRoot(Renderer& r)
{
    r.mImpl->nodes.push_back(r.mImpl->core.sceneMgr()->getRootSceneNode());
    r.mImpl->particles.init(r.mImpl->core.sceneMgr());
}

} // namespace detail

} // namespace eng
