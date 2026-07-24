#include "LiveLevel.h"

#include "LobbyDressing.h"
#include "RenderPalette.h"

#include <eng/Log.h>
#include <eng/Renderer.h>
#include <eng/Physics.h>
#include <eng/ecs/Components.h>

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdlib>
#include <string>
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
LiveLevel buildLevel(eng::Renderer& r, eng::Physics& physics,
                     const std::string& assets, uint32_t seed, int depth,
                     const gen::Layout* authored)
{
    LiveLevel lv;
    lv.gameScene = std::make_unique<game::GameScene>(r);
    const eng::NodeHandle levelRoot =
        r.createNode(eng::kRootNode, glm::vec3(0.0f),
                     "Level Scene depth " + std::to_string(depth));
    const eng::NodeHandle dungeonRoot =
        r.createNode(levelRoot, glm::vec3(0.0f), "Procedural Dungeon");
    const eng::NodeHandle showcaseRoot =
        r.createNode(levelRoot, glm::vec3(0.0f), "Shared Showcase");

    // --------------------------------------------------------- dungeon ---
    // Procedurally generated level; the anchor 'C' room lands at the world
    // origin so the shared DemoScene sits centred inside it.
    gen::Layout layout = authored ? *authored : gen::generate(seed);
    if (!lv.map.loadFromRows(r, physics, std::move(layout),
                             assets + "/meshes/tiles/",
                             assets + "/meshes/props/", dungeonRoot)) {
        eng::log::error("buildLevel: map load failed");
        return lv;
    }
    lv.spawn = lv.map.spawn();
    lv.exit = lv.map.exitPos();

    // ------------------------------------------------------ shared scene ---
    DemoScene::Options sceneOpts;
    sceneOpts.crystals = depth == 0; // lobby-only crystal feature gallery
    sceneOpts.boxes = false;       // movers replaced by the treasure chest
    lv.scene.load(r, DEMO_SCENE_TOML, assets + "/meshes/", showcaseRoot,
                  sceneOpts);

    RenderPalette palette;
    loadRenderPalette(assets + "/palettes.toml", depth == 0 ? "lobby" : "dungeon",
                      palette);
    applyRenderPalette(r, palette, lv.scene.sunNode(), lv.scene.sunLight());

    // ------------------------------------------------- lobby showcase ---
    // Depth zero is a deliberately authored, non-combat exhibition hall.
    // Keep this dense staging out of procedural dungeon floors: those are
    // dressed by DungeonMap's data-driven marker and ambient prop catalogs.
    if (depth == 0) {
    loadPrimitiveShowcase(r, assets + "/lobby_showcase.toml", lv.exhibits);
    // ------------------------------------------------- set dressing ---
    // Medieval props placed around the anchor room (positions authored for
    // the shared centrepiece layout at the world origin).
    {
        loadLobbyDressing(r, assets + "/lobby_dressing.toml",
                          assets + "/meshes/props/");

        const std::string props = assets + "/meshes/props/";
        const auto mesh = [&](const char* f) { return r.loadObj(props + f); };
        const glm::vec3 noScale{1.0f};

        // ECS path (R1): spawn a static prop as a registry actor; SceneSync
        // allocates the renderer node and attaches the mesh. yawDeg rotates
        // about Y (the only rotation the migrated crates need).
        const auto placeEcs = [&](eng::MeshHandle m, const char* mat,
                                  glm::vec3 pos, float yawDeg,
                                  glm::vec3 scale = glm::vec3(1.0f),
                                  bool cast = true) {
            const glm::quat q =
                yawDeg != 0.0f
                    ? glm::angleAxis(glm::radians(yawDeg), glm::vec3(0, 1, 0))
                    : glm::quat(1, 0, 0, 0);
            return lv.gameScene->spawnStatic(m, mat, pos, q, scale, cast);
        };

        eng::MeshHandle crate = mesh("prop_crate.obj");
        eng::MeshHandle pumpkin = mesh("prop_pumpkin.obj");

        // --- entry hall (z ~ +24): child props on the market table. The table
        // body itself is authored in lobby_dressing.toml; here we mirror its
        // transform on a bare node so the bread/pumpkin ride along.
        eng::NodeHandle table = r.createNode(eng::kRootNode, {7.0f, 0.88f, 24.5f});
        r.setOrientation(table, glm::angleAxis(glm::radians(-120.0f),
                                               glm::vec3(0, 1, 0)));
        r.attachMesh(r.createNode(table, {0.3f, 0.53f, -0.2f}),
                     mesh("prop_bread.obj"), "Game/PropMarketMisc");
        r.attachMesh(r.createNode(table, {-0.35f, 0.59f, 0.25f}), pumpkin,
                     "Game/PropMarketMisc");

        // --- great hall corner crate stack (per-crate Y offsets, kept in code)
        if (depth == 0) {
            const glm::vec3 c{-9.0f, 0.0f, -4.5f};
            placeEcs(crate, "Game/PropMarket", c, 10.0f, noScale, false);
            placeEcs(crate, "Game/PropMarket", c + glm::vec3(0, 0.24f, 0), -25.0f,
                     noScale, false);
            placeEcs(crate, "Game/PropMarket", c + glm::vec3(0, 0.48f, 0), 40.0f,
                     noScale, false);
        }

        // --- vault (z ~ -18..-26): sword stabbed into the floor, shield on a
        // barrel. The weapons-pack meshes are authored huge; scale down.
        {
            const glm::quat swordRot =
                glm::angleAxis(glm::radians(168.0f), glm::vec3(0, 0, 1)) *
                glm::angleAxis(glm::radians(8.0f), glm::vec3(1, 0, 0));
            lv.gameScene->spawnStatic(mesh("prop_sword.obj"), "Game/PropWeapon",
                                      {0.0f, 1.15f, -24.0f}, swordRot,
                                      glm::vec3(0.06f), false);

            // The barrel the shield rests on is authored in lobby_dressing.toml.
            const glm::vec3 b{-4.0f, 0.0f, -24.2f};
            const glm::quat shieldRot =
                glm::angleAxis(glm::radians(180.0f), glm::vec3(0, 1, 0)) *
                glm::angleAxis(glm::radians(-20.0f), glm::vec3(1, 0, 0));
            lv.gameScene->spawnStatic(mesh("prop_shield.obj"), "Game/PropWeapon",
                                      b + glm::vec3(0.0f, 0.55f, -0.75f),
                                      shieldRot, glm::vec3(0.08f), false);
        }

        // --- braziers: ground the demo's two omni lamps in open barrels with
        // a fire on the rim and lift the light just above the flames.
        const auto& omnis = lv.scene.omniNodes();
        if (omnis.size() >= 2)
            buildBraziers(r, assets + "/meshes/props/", omnis[0], omnis[1]);
        {
            const glm::vec3 c{-4.5f, 0.0f, -20.0f};
            placeEcs(crate, "Game/PropMarket", c, -20.0f, noScale, false);
            placeEcs(crate, "Game/PropMarket", c + glm::vec3(0, 0.24f, 0), 15.0f,
                     noScale, false);
        }
    }

    // ------------------------------------------- hall centrepiece ---
    // Treasure shrine: a low-poly chest levitating over the origin (anchor
    // room centre), ringed by the demo's crystal spires + offering clutter,
    // with a warm gold spill that pulses like banked coals.
    TreasureShrine shrine = buildTreasureShrine(r, assets + "/meshes/props/");
    lv.chestBase = shrine.chestBase;
    lv.chestSpin = shrine.chestSpin;
    lv.chestGlowColour = shrine.chestGlowColour;
    // Glow light as an ECS actor at the chest's rest height; LiveLevel::update
    // animates its position (to ride the hovering chest) and colour (pulse).
    eng::LightDesc glow;
    glow.colour = shrine.chestGlowColour;
    glow.range = shrine.glowRange;
    lv.chestGlowEntity =
        lv.gameScene->spawnLight(glow, {0.0f, 1.35f, 0.0f}, "Chest Glow");

    }

    // Portals: generated low-poly arch + opaque scrolling sprite membrane. The
    // threshold remains on the cell centre so interaction/navigation stays
    // deterministic while the tall silhouette reads across a whole room.
    {
        PortalPropStyle down;
        down.frameMesh = assets + "/meshes/props/portal_stone_arch.obj";
        down.lightColour = {0.06f, 0.42f, 0.025f};
        down.yawDegrees = lv.map.exitYawDegrees();
        lv.downPortal = createPortalProp(r, lv.exit, down);
        if (depth == 0) {
            eng::TextSpriteStyle style = showcaseLabelStyle(
                0.48f, {0.22f, 0.82f, 0.18f, 1.0f});
            // Deliberately force a two-line plaque: it remains readable at
            // the low-resolution presentation target without spanning the
            // full arch width.
            style.maxWidthPixels = 72;
            style.colourRules.push_back(
                {"PORTAL", {0.48f, 0.92f, 0.30f, 1.0f}});
            const eng::SpriteHandle sprite =
                r.attachTextSprite(lv.downPortal.labelAnchor,
                                   "DUNGEON PORTAL", style);
            r.setSpriteVisible(sprite, false);
            lv.worldLabels.push_back({lv.downPortal.labelAnchor, sprite,
                                      lv.downPortal.labelWorldPosition});
        }
        if (depth > 0) {
            PortalPropStyle up;
            up.frameMesh = assets + "/meshes/props/portal_stone_arch.obj";
            up.material = "Game/PortalUp";
            up.lightColour = {0.18f, 0.90f, 1.35f};
            lv.upPortal = createPortalProp(r, lv.spawn, up);
        }
    }
    if (depth == 0 && !std::getenv("PSX_NO_SHOWCASE_LABELS")) {
        std::unordered_set<std::string> labelled;
        for (const ShowcaseExhibit& exhibit : lv.exhibits) {
            if (exhibit.label.empty() || !labelled.insert(exhibit.id).second)
                continue;
            const bool portal = exhibit.id.find("Portal") != std::string::npos;
            if (portal)
                continue; // portal labels are anchored to their rotated roots
            const glm::vec3 anchor = exhibit.position + glm::vec3(
                0.0f, std::max(0.7f, exhibit.halfExtents.y) + 0.40f, 0.0f);
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
    return lv;
}

bool LiveLevel::rebuild(eng::Renderer& r, eng::Physics& physics,
                        const std::string& assets, uint32_t seed, int depth)
{
    // Free the outgoing level's collider bodies before overwriting the map.
    map.clearPhysics();
    r.clearScene();
    *this = buildLevel(r, physics, assets, seed, depth);
    return map.debugRows() > 0;
}

bool LiveLevel::rebuildLayout(eng::Renderer& r, eng::Physics& physics,
                              const std::string& assets, gen::Layout layout,
                              int depth)
{
    if (!layout.valid())
        return false;
    map.clearPhysics();
    r.clearScene();
    *this = buildLevel(r, physics, assets, 0, depth, &layout);
    return map.debugRows() > 0;
}

void LiveLevel::update(eng::Renderer& r, float animationTime)
{
    scene.update(r, animationTime);
    map.update(r, animationTime);
    if (chestBase.valid()) {
        const glm::vec3 hover{
            0.0f, 1.35f + 0.25f * std::sin(animationTime * 0.9f), 0.0f};
        r.setPosition(chestBase, hover);
        r.setOrientation(
            chestSpin,
            glm::angleAxis(animationTime * 0.8f, glm::vec3(0, 1, 0)));
        const float pulse = 0.9f + 0.1f * std::sin(animationTime * 1.7f) +
                            0.05f * std::sin(animationTime * 4.3f);
        // Drive the glow light actor through its ECS components; SceneSync
        // (below) pushes them to the renderer this same frame.
        if (chestGlowEntity != entt::null && gameScene) {
            eng::ecs::Transform t;
            t.position = hover;
            gameScene->scene().setLocalTransform(chestGlowEntity, t);
            gameScene->scene()
                .registry()
                .get<eng::ecs::LightColour>(chestGlowEntity)
                .value = chestGlowColour * pulse;
        }
    }
    // Reconcile the renderer view with the ECS registry AFTER all actor
    // mutations this frame (static props, animated glow light).
    if (gameScene)
        gameScene->sync();
}


void LiveLevel::updateVisibility(eng::Renderer& r, glm::vec3 cameraPos)
{
    map.updateVisibility(r, cameraPos, 30.0f);
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
    targets.push_back({TargetKind::PortalDown, 0,
                       exit + glm::vec3(0.0f, 0.4f, 0.0f), 3.0f});
    if (depth > 0)
        targets.push_back({TargetKind::PortalUp, 0,
                           spawn + glm::vec3(0.0f, 0.4f, 0.0f), 3.0f});
}

// Dynamic physics prop: a render node driven by a Jolt rigid body each frame.
// renderOffset is subtracted from the body centre to place the mesh origin
