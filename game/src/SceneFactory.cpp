#include "SceneFactory.h"

#include <eng/Renderer.h>
#include <eng/Log.h>

#include <glm/gtc/quaternion.hpp>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <unordered_map>

PortalVisual createPortal(eng::Renderer& r, glm::vec3 floorPosition,
                          const PortalStyle& style)
{
    PortalVisual out;
    const bool authoredFrame = !style.frameMesh.empty();
    const eng::MeshHandle membrane = authoredFrame
        ? r.createPlane(style.innerRadius * 2.0f)
        : r.createPortalDisc(style.innerRadius, style.segments);
    out.root = r.createNode(eng::kRootNode, floorPosition);
    r.setOrientation(out.root,
                     glm::angleAxis(glm::radians(style.yawDegrees),
                                    glm::vec3(0, 1, 0)));
    if (!style.frameMesh.empty()) {
        // The authored arch occupies x[0,4], y[0,3], z[-4,0]. Offset it so
        // its opening is centered on the 4 m portal cell and its front face
        // points toward the approaching player (+Z).
        eng::NodeHandle frame = r.createNode(out.root, {-2.0f, 0.0f, 2.0f});
        // The source kit piece is a four-metre-deep passage module. Compress
        // only its depth so it reads as a monumental portal surround rather
        // than a short tunnel, without distorting the authored front arch.
        r.setScale(frame, {1.0f, 1.0f, 0.24f});
        r.attachMesh(frame, r.loadObj(style.frameMesh), style.frameMaterial,
                     false);
    }
    // The compressed frame spans local z [1.04, 2.0]. Put the opaque
    // membrane just behind it so it never intersects the masonry and cannot
    // z-fight or reorder as the view angle changes.
    const eng::NodeHandle arch = r.createNode(
        out.root, {0.0f, style.height, style.frameMesh.empty() ? 0.0f : 1.0f});
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
        r.attachParticles(out.root, style.particles);
    eng::LightDesc glow;
    glow.colour = style.lightColour;
    glow.range = style.lightRange;
    out.light = r.attachLight(arch, glow);
    return out;
}

void animatePortal(eng::Renderer& r, const PortalVisual& portal, float time,
                   float direction)
{
    // Portal motion is entirely UV-driven by the material. Keeping the mesh
    // fixed avoids the cheap spinning-disc look and leaves bloom intensity
    // stable; direction is retained for API compatibility with return gates.
    (void)r;
    (void)portal;
    (void)time;
    (void)direction;
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
            r.attachParticles(placed, particles);
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
        info.position = position;
        info.blocksMovement = (*e)["collision"].value_or(
            shape == "box" || shape == "cone");
        if (info.blocksMovement) {
            info.halfExtents = glm::abs(scale) * 0.5f;
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
