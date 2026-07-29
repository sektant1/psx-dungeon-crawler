#include "SceneFactory.h"
#include "ParticleEffects.h"
#include "PortalGeometry.h"

#include <eng/Log.h>
#include <eng/Primitive.h>
#include <eng/Renderer.h>

#include <glm/gtc/quaternion.hpp>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <cmath>
#include <unordered_map>

PortalProp createPortalProp(eng::Renderer& r, glm::vec3 floorPosition,
                            const PortalPropStyle& style)
{
    PortalProp out;
    eng::PrimitiveMeshDesc membraneDesc;
    membraneDesc.kind = eng::PrimitiveKind::Disc;
    membraneDesc.radius = style.innerRadius;
    membraneDesc.segments = style.segments;
    const eng::MeshHandle membrane =
        r.createPrimitiveMesh(membraneDesc);

    eng::PrimitiveMeshDesc frameDesc;
    frameDesc.kind = eng::PrimitiveKind::BeveledBox;
    frameDesc.bevel = style.frameBevel;
    const eng::MeshHandle framePrimitive =
        r.createPrimitiveMesh(frameDesc);
    if (!membrane.valid() || !framePrimitive.valid()) {
        eng::log::error(
            "PortalProp: failed to create membrane or frame primitive");
        if (membrane.valid())
            r.releaseMesh(membrane);
        if (framePrimitive.valid())
            r.releaseMesh(framePrimitive);
        return {};
    }

    out.root = r.createNode(eng::kRootNode, floorPosition);
    r.setOrientation(out.root,
                     glm::angleAxis(glm::radians(style.yawDegrees),
                                    glm::vec3(0, 1, 0)));

    const eng::NodeHandle arch = r.createNode(
        out.root, {0.0f, style.height, 0.0f});
    const eng::NodeHandle frame = r.createNode(arch);
    const float openingHalfWidth =
        style.innerRadius * style.fieldScale.x;
    const float openingHalfHeight =
        style.innerRadius * style.fieldScale.y;
    const auto framePart = [&](const PortalBlock& block) {
        const eng::NodeHandle part = r.createNode(frame, block.position);
        r.setScale(part, block.scale * style.frameScale);
        r.attachMesh(part, framePrimitive, style.frameMaterial, false);
    };
    for (const PortalBlock& block : buildSteppedPortalBlocks({
             openingHalfWidth, openingHalfHeight, style.frameWidth,
             style.frameDepth}))
        framePart(block);

    out.field = r.createNode(arch, {0.0f, 0.0f, style.membraneInset});
    r.setScale(out.field,
               {style.fieldScale.x, 1.0f, style.fieldScale.y});
    r.setOrientation(out.field,
                     glm::angleAxis(glm::radians(90.0f),
                                    glm::vec3(1, 0, 0)));
    r.attachMesh(out.field, membrane, style.material);
    if (!style.particles.empty())
        r.spawnParticles(style.particles, arch);
    eng::LightDesc glow;
    glow.colour = style.lightColour;
    glow.range = style.lightRange;
    out.light = r.attachLight(arch, glow);
    out.labelAnchor = r.createNode(out.root, style.labelOffset);
    const float yaw = glm::radians(style.yawDegrees);
    const float c = std::cos(yaw), s = std::sin(yaw);
    const glm::vec3 local = style.labelOffset;
    out.labelWorldPosition = floorPosition + glm::vec3(
        c * local.x + s * local.z, local.y,
        -s * local.x + c * local.z);
    return out;
}

bool loadPrimitiveShowcase(eng::Renderer& r, const std::string& path,
                           std::vector<ShowcaseExhibit>& loadedExhibits,
                           const game::CombatVocabulary& vocabulary)
{
    const toml::parse_result parsed = toml::parse_file(path);
    if (!parsed) {
        eng::log::error("Showcase: failed to parse %s: %s", path.c_str(),
                        std::string(parsed.error().description()).c_str());
        return false;
    }
    const toml::array* exhibits = parsed.table()["exhibit"].as_array();
    if (!exhibits) {
        eng::log::error("Showcase: %s has no [[exhibit]] entries", path.c_str());
        return false;
    }
    std::unordered_map<std::string, eng::MeshHandle> meshes;
    const auto meshFor = [&](const std::string& shape) -> eng::MeshHandle {
        if (const auto it = meshes.find(shape); it != meshes.end()) return it->second;
        eng::PrimitiveMeshDesc desc;
        if (shape == "box") {
            desc.kind = eng::PrimitiveKind::BeveledBox;
            desc.bevel = 0.14f;
        } else if (shape == "cone") {
            desc.kind = eng::PrimitiveKind::Cone;
            desc.segments = 8;
        } else if (shape == "sphere") {
            desc.kind = eng::PrimitiveKind::Sphere;
            desc.rings = 10;
            desc.segments = 12;
        } else if (shape == "cylinder") {
            desc.kind = eng::PrimitiveKind::Cylinder;
            desc.segments = 12;
        } else if (shape == "plane") {
            desc.kind = eng::PrimitiveKind::Plane;
        } else if (shape == "disc") {
            desc.kind = eng::PrimitiveKind::Disc;
        } else {
            return {};
        }
        const eng::MeshHandle mesh = r.createPrimitiveMesh(desc);
        if (mesh.valid()) meshes.emplace(shape, mesh);
        return mesh;
    };
    const auto vec3 = [](const toml::table& t, const char* key,
                         glm::vec3 fallback) {
        const toml::array* a = t[key].as_array();
        if (!a || a->size() != 3) return fallback;
        return glm::vec3(float((*a)[0].value_or(double(fallback.x))),
                         float((*a)[1].value_or(double(fallback.y))),
                         float((*a)[2].value_or(double(fallback.z))));
    };
    const auto vec4 = [](const toml::table& t, const char* key,
                         glm::vec4 fallback) {
        const toml::array* a = t[key].as_array();
        if (!a || a->size() != 4) return fallback;
        return glm::vec4(float((*a)[0].value_or(double(fallback.x))),
                         float((*a)[1].value_or(double(fallback.y))),
                         float((*a)[2].value_or(double(fallback.z))),
                         float((*a)[3].value_or(double(fallback.w))));
    };
    size_t loaded = 0;
    for (const toml::node& node : *exhibits) {
        const toml::table* e = node.as_table();
        if (!e) continue;
        const std::string shape = (*e)["shape"].value_or(std::string());
        const std::string material = (*e)["material"].value_or(std::string());
        const std::string id = (*e)["id"].value_or(shape);
        const bool isSword = shape == "sword";
        const bool isStaff = shape == "staff";
        const bool isParticleAltar = shape == "particle_altar";
        const bool isComposite = isSword || isStaff || isParticleAltar;
        const eng::MeshHandle mesh = isComposite ? eng::MeshHandle{}
                                                  : meshFor(shape);
        if ((!isComposite && !mesh.valid()) || material.empty()) {
            eng::log::error("Showcase: skipping invalid '%s' exhibit",
                            shape.c_str());
            continue;
        }
        const glm::vec3 position = vec3(*e, "position", {});
        const glm::vec3 scale = vec3(*e, "scale", glm::vec3(1.0f));
        eng::NodeHandle placed = r.createNode(eng::kRootNode, position);
        r.setScale(placed, scale);
        const glm::vec3 degrees = vec3(*e, "rotation", {});
        const eng::NodeHandle display = isComposite
            ? r.createNode(placed) : placed;
        r.setOrientation(display,
            glm::angleAxis(glm::radians(degrees.y), glm::vec3(0,1,0)) *
            glm::angleAxis(glm::radians(degrees.x), glm::vec3(1,0,0)) *
            glm::angleAxis(glm::radians(degrees.z), glm::vec3(0,0,1)));

        // A school name, resolved through magic.toml. An unknown name lands
        // on the default school rather than dropping the enchantment.
        const std::string enchantment =
            (*e)["enchantment"].value_or(std::string());
        const eng::EnchantmentPalette enchantPalette =
            vocabulary.palette(enchantment);
        const float enchantStrength =
            float((*e)["enchantment_strength"].value_or(
                double(vocabulary.defaultEnchantStrength())));
        const auto part = [&](eng::NodeHandle parent, glm::vec3 offset,
                              glm::vec3 partScale, eng::MeshHandle partMesh,
                              const std::string& partMaterial,
                              bool enchanted = false) {
            const eng::NodeHandle child = r.createNode(parent, offset);
            r.setScale(child, partScale);
            r.attachMesh(child, partMesh, partMaterial, false);
            if (enchanted && !enchantment.empty())
                r.setNodeEnchantment(child, enchantPalette, enchantStrength);
            return child;
        };

        if (isComposite) {
            // A stable carved plinth keeps the prop readable even when the
            // displayed weapon is posed at an angle.
            part(placed, {0, 0.12f, 0}, {1.18f, 0.24f, 1.18f},
                 meshFor("box"), "Fantasy/CarvedStone");
        }
        if (isSword) {
            part(display, {0, 0.50f, 0}, {0.17f, 0.38f, 0.17f},
                 meshFor("box"), "Fantasy/AgedWood");
            part(display, {0, 0.18f, 0}, {0.23f, 0.18f, 0.23f},
                 meshFor("sphere"), "Fantasy/DarkIron");
            part(display, {0, 0.94f, 0}, {0.78f, 0.10f, 0.20f},
                 meshFor("box"), "Fantasy/DarkIron");
            part(display, {0, 2.02f, 0}, {0.24f, 1.02f, 0.075f},
                 meshFor("box"), material, true);
            const eng::NodeHandle tip =
                part(display, {0, 3.18f, 0}, {0.24f, 0.34f, 0.075f},
                     meshFor("cone"), material, true);
            r.setOrientation(tip, glm::angleAxis(
                glm::radians(45.0f), glm::vec3(0, 1, 0)));
        } else if (isStaff) {
            part(display, {0, 1.42f, 0}, {0.13f, 1.30f, 0.13f},
                 meshFor("box"), "Fantasy/AgedWood");
            part(display, {0, 2.80f, 0}, {0.42f, 0.18f, 0.42f},
                 meshFor("sphere"), "Fantasy/DarkIron");
            part(display, {0, 3.25f, 0}, {0.42f, 0.42f, 0.42f},
                 meshFor("sphere"), material, true);
            part(display, {0, 3.72f, 0}, {0.30f, 0.48f, 0.30f},
                 meshFor("cone"), material, true);
        } else if (isParticleAltar) {
            part(display, {0, 0.34f, 0}, {0.82f, 0.32f, 0.82f},
                 meshFor("cylinder"), material);
            part(display, {0, 0.55f, 0}, {0.62f, 0.08f, 0.62f},
                 meshFor("disc"), material);
            part(display, {0, 0.48f, 0}, {0.46f, 0.16f, 0.46f},
                 meshFor("sphere"), "Fantasy/DarkIron");
        } else {
            r.attachMesh(display, mesh, material, false);
        }
        if (const std::string enchantment =
                (*e)["enchantment"].value_or(std::string());
            !enchantment.empty() && !isComposite)
            r.setNodeEnchantment(display, enchantPalette, enchantStrength);
        if (const std::string particles =
                (*e)["particles"].value_or(std::string()); !particles.empty()) {
            eng::ParticleSpawnOptions particleOptions;
            particleOptions.localOffset =
                vec3(*e, "particle_offset", glm::vec3(0.0f));
            if (const toml::table* options =
                    (*e)["particle_options"].as_table()) {
                particleOptions.sizeScale =
                    float((*options)["size_scale"].value_or(1.0));
                particleOptions.amountScale =
                    float((*options)["amount_scale"].value_or(1.0));
                particleOptions.lifetimeScale =
                    float((*options)["lifetime_scale"].value_or(1.0));
                particleOptions.speedScale =
                    float((*options)["speed_scale"].value_or(1.0));
                particleOptions.radiusScale =
                    float((*options)["radius_scale"].value_or(1.0));
                particleOptions.emitterRadius =
                    float((*options)["emitter_radius"].value_or(0.0));
                particleOptions.colourTint =
                    vec4(*options, "colour_tint", glm::vec4(1.0f));
                particleOptions.localOffset = vec3(
                    *options, "local_offset", particleOptions.localOffset);
            }
            r.spawnParticles(particles, placed, particleOptions);
        }
        if (const toml::array* colour = (*e)["light_colour"].as_array();
            colour && colour->size() == 3) {
            eng::LightDesc light;
            light.colour = vec3(*e, "light_colour", glm::vec3(1.0f));
            light.range = float((*e)["light_range"].value_or(4.0));
            r.attachLight(placed, light);
        }
        ShowcaseExhibit info;
        info.root = placed;
        info.id = id;
        info.label = (*e)["label"].value_or(id);
        info.labelHighlightPattern =
            (*e)["label_highlight_pattern"].value_or(std::string());
        const glm::vec3 accent = vec3(*e, "label_accent", {0.88f, 0.58f, 0.12f});
        info.labelAccent = {accent, 1.0f};
        const glm::vec3 highlight = vec3(
            *e, "label_highlight_colour", {1.0f, 0.78f, 0.22f});
        info.labelHighlight = {highlight, 1.0f};
        info.labelOffset = vec3(*e, "label_offset", glm::vec3(0.0f));
        info.position = position;
        info.visibilityRange =
            float((*e)["visibility_range"].value_or(0.0));
        // Visual bounds drive label placement even when an exhibit does not
        // participate in collision. Plane height is intentionally zero.
        info.halfExtents = glm::abs(scale) * 0.5f;
        if (shape == "plane") info.halfExtents.y = 0.0f;
        if (isSword || isStaff)
            info.halfExtents = glm::abs(scale) * glm::vec3(1.2f, 3.7f, 1.2f);
        else if (isParticleAltar)
            info.halfExtents = glm::abs(scale) * glm::vec3(1.2f, 0.7f, 1.2f);
        info.blocksMovement = (*e)["collision"].value_or(
            shape == "box" || shape == "cone" || isComposite);
        if (info.blocksMovement) {
            // Thin stands remain comfortably collidable; vertical size is
            // irrelevant to the ground-plane FPS resolver.
            info.halfExtents.x = std::max(info.halfExtents.x, 0.15f);
            info.halfExtents.z = std::max(info.halfExtents.z, 0.15f);
        }
        loadedExhibits.push_back(std::move(info));
        ++loaded;
    }
    eng::log::info("Showcase: loaded %zu primitive/material exhibits", loaded);
    return loaded > 0;
}

TreasureShrine buildTreasureShrine(eng::Renderer& r, const std::string& props) {
    TreasureShrine sh;
    sh.chestGlowColour = glm::vec3(1.0f, 0.62f, 0.22f) * 1.6f;
    eng::MeshHandle vase0 = r.loadObj(props + "prop_vase_p0.obj");
    eng::MeshHandle vase1 = r.loadObj(props + "prop_vase_p1.obj");
    eng::MeshHandle sack = r.loadObj(props + "prop_jutesack.obj");
    for (int i = 0; i < 5; ++i) {
        const float a = glm::radians(72.0f * float(i) + 56.0f);
        const glm::vec3 pos{3.2f * std::sin(a), 0.0f, 3.2f * std::cos(a)};
        eng::NodeHandle n = r.createNode(eng::kRootNode, pos);
        r.setOrientation(n, glm::angleAxis(a, glm::vec3(0, 1, 0)));
        if (i % 2 == 0) {
            r.attachMesh(n, vase0, "Game/PropTerracotta", true);
            r.attachMesh(n, vase1, "Game/PropPlanks", true);
        } else {
            r.attachMesh(n, sack, "Game/PropJute", true);
        }
    }

    sh.chestBase = r.createNode(eng::kRootNode, {0.0f, 1.35f, 0.0f});
    sh.chestSpin = r.createNode(sh.chestBase);
    r.setScale(sh.chestSpin, glm::vec3(6.0f));
    r.attachMesh(sh.chestSpin, r.loadObj(props + "prop_chest.obj"),
                 "Game/PropChest", true);
    r.spawnParticles("sparkles", sh.chestBase);
    // The glow light is spawned by the caller as an ECS actor (see buildLevel);
    // sh.chestGlowColour / sh.glowRange carry the authored values.
    return sh;
}

void buildBraziers(eng::Renderer& r, const std::string& props,
                   eng::NodeHandle omniA, eng::NodeHandle omniB) {
    eng::MeshHandle brz0 = r.loadObj(props + "prop_barrel_open_p0.obj");
    eng::MeshHandle brz1 = r.loadObj(props + "prop_barrel_open_p1.obj");
    const eng::NodeHandle omnis[2] = {omniA, omniB};
    const float xs[2] = {-4.0f, 4.0f};
    for (size_t i = 0; i < 2; ++i) {
        eng::NodeHandle n = r.createNode(eng::kRootNode, {xs[i], 0.0f, 0.0f});
        r.setOrientation(n, glm::angleAxis(glm::radians(i == 0 ? 25.0f : -40.0f),
                                           glm::vec3(0, 1, 0)));
        r.attachMesh(n, brz0, "Game/PropPlanksTwoSided", true);
        r.attachMesh(n, brz1, "Game/PropBauerhausTwoSided", true);
        eng::NodeHandle flame =
            r.createNode(eng::kRootNode, {xs[i], 1.35f, 0.0f});
        particlefx::spawnFlame(r, flame);
        r.setPosition(omnis[i], {xs[i], 1.6f, 0.0f});
    }
}
