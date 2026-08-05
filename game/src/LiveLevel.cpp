#include "LiveLevel.h"

#include "GameAssets.h"
#include "RenderPalette.h"
#include "ShowcaseVisibility.h"
#include "ShowroomMotion.h"

#include <eng/Log.h>
#include <eng/Renderer.h>
#include <eng/Physics.h>
#include <eng/ecs/Components.h>
#include <eng/ecs/Systems.h>

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

static eng::TextSpriteStyle showcaseLabelStyle(float worldHeight,
                                                glm::vec4 accent)
{
    eng::TextSpriteStyle style;
    style.worldHeight = worldHeight;
    style.accentColour = accent;
    return style;
}

// Build a complete level (dungeon + demo scene + props + chest + portals)
// into the (already-clear) scene. depth>0 adds an up-portal at the entry.
LiveLevel buildLevel(eng::ecs::World& world, eng::Renderer& r,
                      eng::Physics& physics,
                      const game::CombatVocabulary& vocabulary, uint32_t seed,
                      int depth, const gen::Layout* authored,
                      const std::string* authoredMapPath)
{
    LiveLevel lv;
    lv.world = &world;
    // Everything created from here on belongs to the level and dies with it;
    // the player and anything else persistent was created outside this window
    // and carries no group.
    world.setActiveGroup(kLevelGroup);
    const eng::NodeHandle levelRoot =
        r.createNode(eng::kRootNode, glm::vec3(0.0f),
                     "Level Scene depth " + std::to_string(depth));
    const bool sourceAuthored = authoredMapPath != nullptr;
    if (sourceAuthored) {
        lv.authoredMap = std::make_unique<game::MapRuntime>(world, kLevelGroup);
        if (!lv.authoredMap->load(*authoredMapPath)) {
            eng::log::error("buildLevel: authored map load failed: %s",
                            authoredMapPath->c_str());
            world.setActiveGroup(0);
            return lv;
        }
        eng::ModelImportOptions legacyImport;
        legacyImport.pivot = eng::PivotMode::Source;
        lv.authoredMap->resolveMeshes(
            [&, legacyImport](const std::string& path) -> eng::MeshHandle {
                std::error_code error;
                if (std::filesystem::exists(path, error))
                    return r.loadMesh(path, legacyImport);
                const std::filesystem::path resolved =
                    eng::assets::resolve(path);
                if (!resolved.empty())
                    return r.loadMesh(resolved.string(), legacyImport);
                eng::log::error("Authored scene mesh not found: %s",
                                path.c_str());
                return r.prototypeMesh(path);
            });
        lv.authoredMap->resolvePrimitives(r);
        lv.authoredMap->buildAll();
        lv.spawn = lv.authoredMap->playerSpawn();
        lv.exit = lv.authoredMap->levelExit();
        lv.exitYaw = lv.authoredMap->exitYawDegrees();
    } else {
        const eng::NodeHandle dungeonRoot =
            r.createNode(levelRoot, glm::vec3(0.0f), "Procedural Dungeon");
        gen::Layout layout = authored ? *authored : gen::generate(seed);
        if (!lv.map.loadFromRows(r, physics, std::move(layout),
                                 game::assetDir("meshes/kit"),
                                 game::assetDir("meshes/props"), dungeonRoot)) {
            eng::log::error("buildLevel: map load failed");
            world.setActiveGroup(0);
            return lv;
        }
        lv.spawn = lv.map.spawn();
        lv.exit = lv.map.exitPos();
        lv.exitYaw = lv.map.exitYawDegrees();
    }

    RenderPalette palette;
    if (sourceAuthored) {
        // The palette the level asks for, and the dungeon's when it asks for
        // nothing. This used to hardcode "boss_arena", which is not in
        // palettes.toml -- the branch was written for one specific authored
        // scene and had no caller, so the missing entry logged into a level
        // nobody loaded.
        const std::string authoredPalette = lv.authoredMap->palette();
        const std::string wanted =
            authoredPalette.empty() ? std::string("dungeon") : authoredPalette;
        if (!loadRenderPalette(game::assetPath("config/palettes.toml"), wanted,
                               palette) &&
            !authoredPalette.empty()) {
            // Reported rather than swallowed: a level whose palette was renamed
            // out from under it plays under the defaults and looks subtly wrong
            // everywhere, which is the hardest kind of bug to attribute.
            eng::log::error("Level: palette '%s' is not in palettes.toml",
                            wanted.c_str());
        }
        eng::NodeHandle sunNode{};
        eng::LightHandle sunLight{};
        auto& registry = lv.authoredMap->registry();
        for (const auto entity :
             registry.view<eng::ecs::LightRef, eng::ecs::NodeRef>()) {
            const auto& light = registry.get<eng::ecs::LightRef>(entity);
            if (light.desc.type == eng::LightDesc::Type::Directional) {
                sunNode = registry.get<eng::ecs::NodeRef>(entity).handle;
                sunLight = light.handle;
                break;
            }
        }
        applyRenderPalette(r, palette, sunNode, sunLight);

        // A level authored in the editor is exactly what its file says. This
        // used to also dress every authored map with the boss arena's feature
        // showcase, a crystal ring and a treasure shrine -- fixtures of the one
        // scene the branch was written for, pulled in by marker name and
        // silently added to anything else that came through here. Whatever a
        // level contains, the author placed; that is the whole point of having
        // an editor.
        //
        // `feature.<name>` markers still resolve, so a scene that wants the
        // showcase can be authored to carry it.
        std::unordered_map<std::string, glm::vec3> featurePlacements;
        for (const game::ScenePlacement& placement :
             lv.authoredMap->placements("feature."))
            featurePlacements.emplace(placement.type.substr(8),
                                      placement.position);
        if (!featurePlacements.empty()) {
            loadPrimitiveShowcase(
                r, game::assetPath("config/boss_arena_features.toml"),
                lv.exhibits, vocabulary, featurePlacements);
        }
    } else {
        const eng::NodeHandle sharedRoot =
            r.createNode(levelRoot, glm::vec3(0.0f), "Dungeon Lighting");
        DemoScene::Options sceneOptions;
        sceneOptions.crystals = false;
        sceneOptions.boxes = false;
        lv.scene.load(r, game::assetPath("config/demo_scene.toml"),
                      game::assetDir("meshes"), sharedRoot, sceneOptions);
        loadRenderPalette(game::assetPath("config/palettes.toml"), "dungeon", palette);
        applyRenderPalette(r, palette, lv.scene.sunNode(), lv.scene.sunLight());
    }

    // Portals: an opaque scrolling membrane inside a surround built from kit
    // pieces, deliberately larger than the kit's doorways so the threshold reads
    // as the room's landmark. It stays on the cell boundary, keeping
    // interaction/navigation deterministic.
    const std::string kitMeshDir = game::assetDir("meshes/kit");
    {
        PortalPropStyle down;
        // The portal is the brightest thing in the room and should light it:
        // this throws far enough to reach the arch's stone and the floor in
        // front of it, which is what makes the threshold read from the door.
        down.lightColour = {0.22f, 1.05f, 0.10f};
        down.lightRange = 8.5f;
        down.yawDegrees = lv.exitYaw;
        down.kitMeshDir = kitMeshDir;
        lv.downPortal = createPortalProp(r, lv.exit, down);
        if (sourceAuthored) {
            eng::TextSpriteStyle style = showcaseLabelStyle(
                0.48f, {0.22f, 0.82f, 0.18f, 1.0f});
            // Deliberately force a two-line plaque: it remains readable at
            // the low-resolution presentation target without spanning the
            // full arch width.
            style.maxWidthPixels = 72;
            style.colourRules.push_back(
                {"INTERIOR", {0.48f, 0.92f, 0.30f, 1.0f}});
            const eng::SpriteHandle sprite =
                r.attachTextSprite(lv.downPortal.labelAnchor,
                                   "THE INTERIOR", style);
            r.setSpriteVisible(sprite, false);
            lv.worldLabels.push_back({lv.downPortal.labelAnchor, sprite,
                                      lv.downPortal.labelWorldPosition});
        }
        if (depth > 0) {
            PortalPropStyle up;
            up.material = "Game/Vfx/PortalUp";
            up.lightColour = {0.42f, 1.60f, 2.35f};
            up.lightRange = 8.5f;
            up.kitMeshDir = kitMeshDir;
            lv.upPortal = createPortalProp(r, lv.spawn, up);
        }
    }
    if (sourceAuthored && !std::getenv("RAVEN_NO_SHOWCASE_LABELS")) {
        std::unordered_set<std::string> labelled;
        for (const ShowcaseExhibit& exhibit : lv.exhibits) {
            if (exhibit.label.empty() || !labelled.insert(exhibit.id).second)
                continue;
            const bool portal = exhibit.id.find("Portal") != std::string::npos;
            if (portal)
                continue; // portal labels are anchored to their rotated roots
            const glm::vec3 anchor = exhibit.position + glm::vec3(
                0.0f, std::max(0.7f, exhibit.halfExtents.y) + 0.40f, 0.0f) +
                exhibit.labelOffset;
            const eng::NodeHandle labelNode = r.createNode(eng::kRootNode, anchor);
            eng::TextSpriteStyle style = showcaseLabelStyle(
                0.36f, exhibit.labelAccent);
            if (!exhibit.labelHighlightPattern.empty())
                style.colourRules.push_back(
                    {exhibit.labelHighlightPattern, exhibit.labelHighlight});
            const eng::SpriteHandle sprite =
                r.attachTextSprite(labelNode, exhibit.label, style);
            r.setSpriteVisible(sprite, false);
            lv.worldLabels.push_back({labelNode, sprite, anchor});
        }
    }
    world.setActiveGroup(0);
    return lv;
}

bool LiveLevel::rebuild(eng::ecs::World& world, eng::Renderer& r,
                        eng::Physics& physics,
                        const game::CombatVocabulary& vocabulary,
                        uint32_t seed, int depth)
{
    // Free the outgoing level's collider bodies before overwriting the map.
    map.clearPhysics();
    r.clearScene();
    *this = buildLevel(world, r, physics, vocabulary, seed, depth);
    // The seed is the whole reproduction recipe for a generated level: without
    // it in the log, "the layout that trapped the player" is unrecoverable.
    eng::log::info("Level: generated depth %d, seed %u, %d rows, spawn "
                   "%.1f %.1f %.1f",
                   depth, seed, map.debugRows(), double(spawn.x),
                   double(spawn.y), double(spawn.z));
    return map.debugRows() > 0;
}

bool LiveLevel::rebuildLayout(eng::ecs::World& world, eng::Renderer& r,
                              eng::Physics& physics,
                              const game::CombatVocabulary& vocabulary,
                              gen::Layout layout, int depth)
{
    if (!layout.valid())
        return false;
    map.clearPhysics();
    r.clearScene();
    *this = buildLevel(world, r, physics, vocabulary, 0, depth, &layout);
    return map.debugRows() > 0;
}

void LiveLevel::update(eng::Renderer& r, float animationTime, float dt)
{
    scene.update(r, animationTime);
    map.update(r, animationTime);
    if (chestBase.valid()) {
        const game::showroom::TreasureMotion motion =
            game::showroom::treasureMotion(animationTime);
        r.setPosition(chestBase, motion.position);
        r.setOrientation(chestSpin, motion.orientation);
        // Drive the glow light actor through its ECS components; the world
        // sync (below) pushes them to the renderer this same frame.
        if (chestGlowEntity != entt::null && world &&
            world->registry().valid(chestGlowEntity)) {
            eng::ecs::Transform t;
            t.position = motion.position;
            world->setLocalTransform(chestGlowEntity, t);
            world->registry()
                .get<eng::ecs::LightColour>(chestGlowEntity)
                .value = chestGlowColour * motion.lightPulse;
        }
    }
    // Component-driven motion (Spin, LightAnimation, Lifetime) before the
    // reconcile that pushes it: an authored scene animates the same way in the
    // full game as it does in the stripped walk loop.
    if (world)
        eng::ecs::tickComponentSystems(*world, dt);
    // Reconcile the renderer and physics views with the registry AFTER every
    // actor mutation this frame (static props, animated glow light).
    if (world)
        world->sync();
}


void LiveLevel::updateVisibility(eng::Renderer& r, glm::vec3 cameraPos)
{
    map.updateVisibility(r, cameraPos, 30.0f);
    constexpr float exhibitHysteresis = 2.0f;
    for (ShowcaseExhibit& exhibit : exhibits) {
        if (!exhibit.root.valid() || exhibit.visibilityRange <= 0.0f)
            continue;
        const bool visible = showcaseVisibleAtDistance(
            exhibit.visibility, glm::length(exhibit.position - cameraPos),
            exhibit.visibilityRange, exhibitHysteresis);
        // Authored roots start visible in Ogre. Uninitialized evaluation must
        // nevertheless use the entry threshold; only issue a renderer call
        // when the evaluated state differs from the live starting state.
        const bool wasVisible =
            exhibit.visibility != ShowcaseVisibilityState::Hidden;
        if (visible != wasVisible)
            r.setNodeVisible(exhibit.root, visible);
        exhibit.visibility =
            visible ? ShowcaseVisibilityState::Visible
                    : ShowcaseVisibilityState::Hidden;
    }
    // Labels ease in over the final metre instead of popping at a hard range.
    // Scaling a billboard preserves its camera-facing orientation and the
    // fully hidden state avoids distant gallery clutter and draw cost.
    constexpr float revealStart = 5.5f;
    constexpr float fullSizeAt = 4.4f;
    for (const WorldLabel& label : worldLabels) {
        const float distance = glm::length(label.position - cameraPos);
        float t = glm::clamp((revealStart - distance) /
                                 (revealStart - fullSizeAt),
                             0.0f, 1.0f);
        t = t * t * (3.0f - 2.0f * t);
        r.setSpriteVisible(label.sprite, t > 0.015f);
        r.setScale(label.node, glm::vec3(t));
    }
}

void LiveLevel::appendTargets(std::vector<GameplayTarget>& targets,
                              int depth) const
{
    map.appendTorchTargets(targets);
    map.appendPropTargets(targets);
    // Aim at the membrane, not at the floor in front of it: the target used to
    // sit 0.4 m up, so from a metre away the player was looking a metre over the
    // top of it and "use" simply never armed. Centre and radius come from the
    // membrane as built, so the two cannot drift apart.
    const auto membraneTarget = [](TargetKind kind, glm::vec3 floorPosition,
                                   glm::vec2 opening) {
        return GameplayTarget{kind, 0,
                              floorPosition +
                                  glm::vec3(0.0f, opening.y * 0.5f, 0.0f),
                              3.0f, glm::max(opening.x, opening.y) * 0.5f};
    };
    if (downPortal.valid())
        targets.push_back(membraneTarget(TargetKind::PortalDown, exit,
                                         downPortal.opening));
    if (depth > 0 && upPortal.valid())
        targets.push_back(membraneTarget(TargetKind::PortalUp, spawn,
                                         upPortal.opening));
}

bool LiveLevel::rebuildAuthored(eng::ecs::World& world, eng::Renderer& r,
                                eng::Physics& physics,
                                const game::CombatVocabulary& vocabulary,
                                const std::string& cookedMap, int depth)
{
    map.clearPhysics();
    r.clearScene();
    *this = buildLevel(world, r, physics, vocabulary, 0, depth, nullptr,
                       &cookedMap);
    // A cooked map that fails to load leaves an empty level rather than an
    // error, so the load is reported either way -- an empty room and a missing
    // file used to look identical from inside the game.
    if (authoredMap)
        eng::log::info("Level: loaded '%s' at depth %d, %zu placements",
                       cookedMap.c_str(), depth,
                       authoredMap->placements().size());
    else
        eng::log::error("Level: '%s' did not load; the level is empty",
                        cookedMap.c_str());
    return authoredMap != nullptr;
}

// Authored placements live in the MapRuntime registry; the procedural path has
// no scene markers, so both marker queries fall back gracefully when there is
// no authored map.
glm::vec3 LiveLevel::markerPosition(const std::string& type,
                                    glm::vec3 fallback) const
{
    if (!authoredMap)
        return fallback;
    for (const game::ScenePlacement& placement : authoredMap->placements()) {
        if (placement.type == type)
            return placement.position;
    }
    return fallback;
}

std::vector<game::ScenePlacement>
LiveLevel::markerPlacements(const std::string& prefix) const
{
    if (!authoredMap)
        return {};
    return authoredMap->placements(prefix);
}

std::vector<game::ScenePlacement> LiveLevel::pickupPlacements() const
{
    if (!authoredMap)
        return {};
    std::vector<game::ScenePlacement> all = authoredMap->placements("pickup.");
    const std::vector<game::ScenePlacement> components =
        authoredMap->pickupPlacements();
    all.insert(all.end(), components.begin(), components.end());
    return all;
}

std::vector<game::ScenePlacement> LiveLevel::enemyPlacements() const
{
    if (!authoredMap)
        return {};
    // Markers first, so a hand-written "enemy.goblin.ambush" keeps its preset
    // suffix and its position in the list; the component form has no preset to
    // carry and is appended.
    std::vector<game::ScenePlacement> all = authoredMap->placements("enemy.");
    const std::vector<game::ScenePlacement> components =
        authoredMap->enemySpawnPlacements();
    all.insert(all.end(), components.begin(), components.end());
    return all;
}

void LiveLevel::clearPhysics()
{
    map.clearPhysics();
    // Exactly the entities this level added, by group -- the player and
    // anything else persistent stays. The sync that follows frees their nodes
    // and bodies.
    if (world) {
        world->destroyGroup(kLevelGroup);
        world->sync();
    }
}

// Dynamic physics prop: a render node driven by a Jolt rigid body each frame.
// renderOffset is subtracted from the body centre to place the mesh origin
