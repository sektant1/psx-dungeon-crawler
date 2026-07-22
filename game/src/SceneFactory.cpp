#include "SceneFactory.h"
#include "ParticleLibrary.h"

#include <eng/Renderer.h>
#include <eng/Log.h>

#include <glm/gtc/quaternion.hpp>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <cmath>
#include <unordered_map>

PortalProp createPortalProp(eng::Renderer& r, glm::vec3 floorPosition,
                            const PortalPropStyle& style)
{
    PortalProp out;
    const bool authoredFrame = !style.frameMesh.empty();
    const eng::MeshHandle membrane = authoredFrame
        ? r.createPlane(style.innerRadius * 2.0f)
        : r.createPortalDisc(style.innerRadius, style.segments);
    out.root = r.createNode(eng::kRootNode, floorPosition);
    r.setOrientation(out.root,
                     glm::angleAxis(glm::radians(style.yawDegrees),
                                    glm::vec3(0, 1, 0)));
    if (!style.frameMesh.empty()) {
        // The authored arch occupies x[0,4], y[0,3], z[-4,0]. Centre its
        // facade directly on the wall plane instead of offsetting the whole
        // four-metre source tunnel into the room.
        eng::NodeHandle frame = r.createNode(out.root, style.frameOffset);
        // The source kit piece is a four-metre-deep passage module. Compress
        // only its depth so it reads as a monumental portal surround rather
        // than a short tunnel, without distorting the authored front arch.
        r.setScale(frame, style.frameScale);
        r.attachMesh(frame, r.loadObj(style.frameMesh), style.frameMaterial,
                     false);
    }
    // Overscan the opaque field behind the opening and keep it only a few
    // centimetres behind the facade. The frame masks its edges from every
    // playable angle without exposing an unlit recess.
    const eng::NodeHandle arch = r.createNode(
        out.root, {0.0f, style.height,
                   style.frameMesh.empty() ? 0.0f : style.membraneInset});
    out.field = r.createNode(arch);
    if (authoredFrame) {
        r.setScale(out.field, {style.fieldScale.x, 1.0f, style.fieldScale.y});
        r.setOrientation(out.field,
                         glm::angleAxis(glm::radians(90.0f),
                                        glm::vec3(1, 0, 0)));
    } else {
        r.setScale(out.field, {style.fieldScale.x, style.fieldScale.y, 1.0f});
    }
    r.attachMesh(out.field, membrane, style.material);
    if (!style.particles.empty())
        r.spawnParticles(style.particles, out.root);
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
                           std::vector<ShowcaseExhibit>& loadedExhibits)
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
        eng::MeshHandle mesh;
        if (shape == "box") mesh = r.createBeveledBox(0.14f);
        else if (shape == "cone") mesh = r.createCone(0.5f, 1.0f, 8);
        else if (shape == "plane") mesh = r.createPlane(1.0f);
        else if (shape == "disc") mesh = r.createPortalDisc(0.5f, 16);
        else if (shape == "ring") mesh = r.createPortalRing(0.5f, 0.32f, 0.12f, 16);
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
    size_t loaded = 0;
    for (const toml::node& node : *exhibits) {
        const toml::table* e = node.as_table();
        if (!e) continue;
        const std::string shape = (*e)["shape"].value_or(std::string());
        const std::string material = (*e)["material"].value_or(std::string());
        const std::string id = (*e)["id"].value_or(shape);
        const eng::MeshHandle mesh = meshFor(shape);
        if (!mesh.valid() || material.empty()) {
            eng::log::error("Showcase: skipping invalid '%s' exhibit",
                            shape.c_str());
            continue;
        }
        const glm::vec3 position = vec3(*e, "position", {});
        const glm::vec3 scale = vec3(*e, "scale", glm::vec3(1.0f));
        eng::NodeHandle placed = r.createNode(eng::kRootNode, position);
        r.setScale(placed, scale);
        const glm::vec3 degrees = vec3(*e, "rotation", {});
        r.setOrientation(placed,
            glm::angleAxis(glm::radians(degrees.y), glm::vec3(0,1,0)) *
            glm::angleAxis(glm::radians(degrees.x), glm::vec3(1,0,0)) *
            glm::angleAxis(glm::radians(degrees.z), glm::vec3(0,0,1)));
        r.attachMesh(placed, mesh, material, false);
        if (const std::string particles =
                (*e)["particles"].value_or(std::string()); !particles.empty())
            r.spawnParticles(particles, placed);
        if (const toml::array* colour = (*e)["light_colour"].as_array();
            colour && colour->size() == 3) {
            eng::LightDesc light;
            light.colour = vec3(*e, "light_colour", glm::vec3(1.0f));
            light.range = float((*e)["light_range"].value_or(4.0));
            r.attachLight(placed, light);
        }
        ShowcaseExhibit info;
        info.id = id;
        info.label = (*e)["label"].value_or(id);
        info.labelHighlightPattern =
            (*e)["label_highlight_pattern"].value_or(std::string());
        const glm::vec3 accent = vec3(*e, "label_accent", {0.88f, 0.58f, 0.12f});
        info.labelAccent = {accent, 1.0f};
        const glm::vec3 highlight = vec3(
            *e, "label_highlight_colour", {1.0f, 0.78f, 0.22f});
        info.labelHighlight = {highlight, 1.0f};
        info.position = position;
        // Visual bounds drive label placement even when an exhibit does not
        // participate in collision. Plane height is intentionally zero.
        info.halfExtents = glm::abs(scale) * 0.5f;
        if (shape == "plane") info.halfExtents.y = 0.0f;
        info.blocksMovement = (*e)["collision"].value_or(
            shape == "box" || shape == "cone");
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
    eng::LightDesc glow;
    glow.colour = sh.chestGlowColour;
    glow.range = 6.0f;
    sh.chestGlow = r.attachLight(sh.chestBase, glow);
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
