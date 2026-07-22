// dungeon-crawler: FPS walk through a procedurally generated PSX dungeon
// (DungeonGen -> DungeonMap), with the shared demo scene (crystals, chest,
// sparkles, light shaft) sitting in the generated level's anchor room.
// A whole level is built by buildLevel() into a Level bundle; level
// transitions (portals) clear the scene and rebuild.

#include "DungeonGen.h"
#include "DungeonMap.h"
#include "FpsController.h"
#include "LevelEditor.h"
#include "Projectiles.h"
#include "SceneFactory.h"
#include "Melee.h"
#include "Dummy.h"
#include "Targeting.h"
#include "ViewModel.h"

#include <DemoScene.h>

#include <imgui.h>

#include <eng/Engine.h>
#include <eng/Log.h>
#include <eng/Physics.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_set>
#include <vector>

// Per-scene moonlit dark-fantasy palette. Must re-run after every
// scene rebuild, so it lives in its own function called from buildLevel.
static void applyPalette(eng::Renderer& r, const DemoScene& scene)
{
    const auto lin = [](float srgb) { return std::pow(srgb, 2.2f); };
    // Slate-blue ambient keeps unlit planes legible without painting the
    // whole level purple. Local warm lights provide the fantasy colour.
    r.setAmbient({lin(0.46f) * 0.25f, lin(0.47f) * 0.25f,
                  lin(0.53f) * 0.25f});
    // A dim moon-blue spill separates silhouettes and sells illustrated
    // planes while the steep angle keeps the dungeon oppressive.
    r.setOrientation(scene.sunNode(),
                     glm::angleAxis(glm::radians(30.0f), glm::vec3(0, 1, 0)) *
                         glm::angleAxis(glm::radians(-75.0f), glm::vec3(1, 0, 0)));
    r.setLightColour(scene.sunLight(),
                     {lin(0.62f) * 0.42f, lin(0.54f) * 0.42f,
                      lin(0.76f) * 0.42f});
    // Navy-violet fog keeps distant geometry dreamlike instead of erasing it.
    r.setFog({lin(0.12f), lin(0.115f), lin(0.15f)}, 0.050f);
    r.setBackground({0.038f, 0.035f, 0.050f});
    // The shaft becomes pale moon-magic rather than green ghost-light.
    r.setMaterialParam("PSX/LightShaft", "modulateColor",
                       glm::vec4(0.68f, 0.76f, 1.0f, 0.19f));
    // Broad but softly joined bands retain the illustrated read without
    // turning every light pool into a high-contrast target.
    r.setLightSteps(4.0f);
    r.setLightStepSoftness(0.30f);
    r.setFogDesatBoost(0.08f);
    r.setGradeEnabled(true);
    r.setGradeParams(0.015f, 0.98f, {0.12f, 0.12f, 0.18f},
                     {0.72f, 0.65f, 0.60f});
    r.setMaterialParam("PSX/DitherPost", "gradeSaturation", 1.0f);
    r.setMaterialParam("PSX/DitherPost", "gradeTintStrength", 0.035f);
    r.setMaterialParam("PSX/DitherPost", "gradeBlackLift", 0.060f);
    r.setMaterialParam("PSX/DitherPost", "vignetteStrength", 0.08f);
    r.setMaterialParam("PSX/DitherPost", "vignetteColor",
                       glm::vec3(0.24f, 0.20f, 0.38f));
    // Retain PSX colour precision and ordered dithering, but keep the pattern
    // subordinate to texture detail and lighting rather than coating the view.
    r.setMaterialParam("PSX/DitherPost", "ditherEnabled", 1.0f);
    r.setMaterialParam("PSX/DitherPost", "colDepth", 31.0f);
    r.setMaterialParam("PSX/DitherPost", "ditherBanding", 0.018f);
    r.setMaterialParam("PSX/DitherPost", "ditherDarkFade", 0.20f);
    r.setMaterialParam("PSX/PixelStylize", "shadowColor",
                       glm::vec3(0.035f, 0.025f, 0.09f));
    r.setMaterialParam("PSX/PixelStylize", "shadowStrength", 0.16f);
    r.setMaterialParam("PSX/PixelStylize", "highlightColor",
                       glm::vec3(1.0f, 0.72f, 0.42f));
    r.setMaterialParam("PSX/PixelStylize", "highlightStrength", 0.10f);
    r.setMaterialParam("PSX/PixelStylize", "outlineColor",
                       glm::vec3(0.025f, 0.018f, 0.065f));
    r.setMaterialParam("PSX/PixelStylize", "outlineOpacity", 0.26f);
    r.setMaterialParam("PSX/PixelStylize", "outlineDepthSens", 8.0f);
    r.setMaterialParam("PSX/PixelStylize", "outlineNormalSens", 0.20f);
    // Warm magic blooms softly, without washing the inked silhouettes.
    r.setBloomParams(0.72f, 0.72f);
}

static eng::TextSpriteStyle showcaseLabelStyle(float worldHeight,
                                                glm::vec4 accent)
{
    eng::TextSpriteStyle style;
    style.worldHeight = worldHeight;
    style.accentColour = accent;
    return style;
}

// Everything a built level owns that the main loop animates or references.
// Swapped atomically on a transition (clearScene + buildLevel).
class LiveLevel {
public:
    bool rebuild(eng::Renderer& r, eng::Physics& physics,
                 const std::string& assets, uint32_t seed, int depth);
    bool rebuildLayout(eng::Renderer& r, eng::Physics& physics,
                       const std::string& assets, gen::Layout layout, int depth);
    void update(eng::Renderer& r, float animationTime);
    void updateVisibility(eng::Renderer& r, glm::vec3 cameraPos);
    void appendTargets(std::vector<GameplayTarget>& targets, int depth) const;
    glm::vec3 spawnPosition() const { return spawn; }
    glm::vec3 exitPosition() const { return exit; }
    bool torchIsLit(int index) const { return map.torchLit(index); }
    void toggleTorch(eng::Renderer& r, int index) { map.toggleTorch(r, index); }
    const DungeonMap& dungeon() const { return map; }
    void clearPhysics() { map.clearPhysics(); }

private:
    friend LiveLevel buildLevel(eng::Renderer&, eng::Physics&, const std::string&,
                                uint32_t, int, const gen::Layout*);
    DungeonMap map;
    DemoScene scene;
    eng::NodeHandle chestBase{}, chestSpin{};
    eng::LightHandle chestGlow{};
    glm::vec3 chestGlowColour{0.0f};
    glm::vec3 spawn{0.0f}, exit{0.0f};
    PortalVisual downPortal{};
    PortalVisual upPortal{}; // invalid at depth 0
    std::vector<ShowcaseExhibit> exhibits;
    struct WorldLabel {
        eng::NodeHandle node{};
        eng::SpriteHandle sprite{};
        glm::vec3 position{0.0f};
    };
    std::vector<WorldLabel> worldLabels;
};

// Read-only generated-grid inspector. The dungeon owns the data; this only
// projects it into an ImGui draw list for debugging layout/room segmentation.
static void drawDungeonMap(const DungeonMap& map, glm::vec3 playerPos)
{
    ImGui::SetNextWindowSize(ImVec2(540.0f, 600.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(400.0f, 10.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Generated Dungeon Map")) {
        ImGui::End();
        return;
    }

    const int rows = map.debugRows();
    const int cols = map.debugColumns();
    if (rows == 0 || cols == 0) {
        ImGui::TextUnformatted("No generated level is loaded.");
        ImGui::End();
        return;
    }

    int playerCol, playerRow;
    map.debugCellOf(playerPos, playerCol, playerRow);
    ImGui::Text("%d x %d cells  |  player: (%d, %d)", cols, rows,
                playerCol, playerRow);
    static bool roomLabels = false;
    ImGui::SameLine();
    ImGui::Checkbox("room IDs", &roomLabels);

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float cell = std::clamp((available.x - 4.0f) / float(cols), 7.0f, 18.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 extent(cell * float(cols), cell * float(rows));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + extent.x, origin.y + extent.y),
                        IM_COL32(8, 12, 11, 255));

    static constexpr ImU32 kRooms[] = {
        IM_COL32(46, 92, 104, 255), IM_COL32(89, 69, 118, 255),
        IM_COL32(73, 104, 68, 255), IM_COL32(120, 78, 57, 255),
        IM_COL32(50, 109, 97, 255), IM_COL32(105, 91, 53, 255),
        IM_COL32(72, 76, 125, 255), IM_COL32(117, 61, 99, 255),
    };
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const char tile = map.debugCellAt(col, row);
            const int room = map.debugRoomAt(col, row);
            ImU32 colour = IM_COL32(16, 20, 21, 255); // solid/void
            if (room >= 0)
                colour = kRooms[size_t(room) % (sizeof(kRooms) / sizeof(kRooms[0]))];
            if (tile == 'A') colour = IM_COL32(202, 153, 61, 255);
            if (tile == 'L') colour = IM_COL32(210, 106, 43, 255);
            if (tile == 'S') colour = IM_COL32(89, 190, 236, 255);
            if (tile == 'C') colour = IM_COL32(188, 102, 220, 255);
            if (tile == 'X') colour = IM_COL32(95, 210, 143, 255);

            const ImVec2 p0(origin.x + cell * float(col),
                             origin.y + cell * float(row));
            const ImVec2 p1(p0.x + cell, p0.y + cell);
            draw->AddRectFilled(p0, p1, colour);
            draw->AddRect(p0, p1, IM_COL32(4, 7, 7, 210));
            if (tile == 'A') {
                const bool ns = map.debugArchNorthSouth(col, row);
                if (ns) {
                    draw->AddLine({p0.x + cell * 0.25f, p0.y},
                                  {p0.x + cell * 0.25f, p1.y}, IM_COL32(38, 28, 13, 255));
                    draw->AddLine({p0.x + cell * 0.75f, p0.y},
                                  {p0.x + cell * 0.75f, p1.y}, IM_COL32(38, 28, 13, 255));
                } else {
                    draw->AddLine({p0.x, p0.y + cell * 0.25f},
                                  {p1.x, p0.y + cell * 0.25f}, IM_COL32(38, 28, 13, 255));
                    draw->AddLine({p0.x, p0.y + cell * 0.75f},
                                  {p1.x, p0.y + cell * 0.75f}, IM_COL32(38, 28, 13, 255));
                }
            }
            if (roomLabels && room >= 0 && cell >= 13.0f) {
                const std::string label = std::to_string(room);
                draw->AddText({p0.x + 2.0f, p0.y + 1.0f}, IM_COL32(230, 245, 238, 235),
                              label.c_str());
            }
        }
    }
    draw->AddRect(origin, ImVec2(origin.x + extent.x, origin.y + extent.y),
                  IM_COL32(205, 225, 210, 220), 0.0f, 0, 1.5f);
    if (playerCol >= 0 && playerRow >= 0 && playerCol < cols && playerRow < rows) {
        const ImVec2 centre(origin.x + (float(playerCol) + 0.5f) * cell,
                            origin.y + (float(playerRow) + 0.5f) * cell);
        draw->AddCircleFilled(centre, std::max(2.5f, cell * 0.24f),
                              IM_COL32(245, 249, 236, 255));
        draw->AddCircle(centre, std::max(3.5f, cell * 0.30f),
                        IM_COL32(7, 10, 8, 255), 12, 1.5f);
    }
    ImGui::Dummy(extent); // reserve the draw-list rectangle in window layout
    ImGui::TextColored(ImVec4(0.35f, 0.75f, 0.92f, 1.0f), "S player spawn");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.79f, 0.60f, 0.24f, 1.0f), "A arch");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.83f, 0.42f, 0.17f, 1.0f), "L torch");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.74f, 0.40f, 0.86f, 1.0f), "C anchor");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.37f, 0.82f, 0.56f, 1.0f), "X exit");
    ImGui::End();
}

// Build a complete level (dungeon + demo scene + props + chest + portals)
// into the (already-clear) scene. depth>0 adds an up-portal at the entry.
LiveLevel buildLevel(eng::Renderer& r, eng::Physics& physics,
                     const std::string& assets, uint32_t seed, int depth,
                     const gen::Layout* authored = nullptr)
{
    LiveLevel lv;

    // --------------------------------------------------------- dungeon ---
    // Procedurally generated level; the anchor 'C' room lands at the world
    // origin so the shared DemoScene sits centred inside it.
    gen::Layout layout = authored ? *authored : gen::generate(seed);
    if (!lv.map.loadFromRows(r, physics, std::move(layout),
                             assets + "/meshes/tiles/",
                             assets + "/meshes/props/")) {
        eng::log::error("buildLevel: map load failed");
        return lv;
    }
    lv.spawn = lv.map.spawn();
    lv.exit = lv.map.exitPos();

    // ------------------------------------------------------ shared scene ---
    DemoScene::Options sceneOpts;
    sceneOpts.crystals = depth == 0; // lobby-only crystal feature gallery
    sceneOpts.boxes = false;       // movers replaced by the treasure chest
    lv.scene.load(r, DEMO_SCENE_TOML, assets + "/meshes/", eng::kRootNode,
                  sceneOpts);

    applyPalette(r, lv.scene);
    if (depth == 0) {
        // The lobby is a teaching/showcase vista: preserve atmosphere while
        // keeping the far portal landmark readable from the arrival frame.
        const auto lin = [](float c) { return std::pow(c, 2.2f); };
        r.setFog({lin(0.12f), lin(0.115f), lin(0.15f)}, 0.026f);
    }

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
        const std::string props = assets + "/meshes/props/";
        const auto mesh = [&](const char* f) { return r.loadObj(props + f); };
        const auto place = [&](eng::MeshHandle m, const char* mat,
                               glm::vec3 pos, float yawDeg,
                               glm::vec3 scale = glm::vec3(1.0f),
                               bool cast = true) {
            eng::NodeHandle n = r.createNode(eng::kRootNode, pos);
            if (yawDeg != 0.0f)
                r.setOrientation(n, glm::angleAxis(glm::radians(yawDeg),
                                                   glm::vec3(0, 1, 0)));
            if (scale != glm::vec3(1.0f))
                r.setScale(n, scale);
            r.attachMesh(n, m, mat, cast);
            return n;
        };
        const auto place2 = [&](eng::MeshHandle m0, const char* mat0,
                                eng::MeshHandle m1, const char* mat1,
                                glm::vec3 pos, float yawDeg,
                                bool cast = true) {
            eng::NodeHandle n = place(m0, mat0, pos, yawDeg,
                                      glm::vec3(1.0f), cast);
            r.attachMesh(n, m1, mat1, cast);
            return n;
        };
        const glm::vec3 noScale{1.0f};

        eng::MeshHandle barrel0 = mesh("prop_barrel_p0.obj");
        eng::MeshHandle barrel1 = mesh("prop_barrel_p1.obj");
        eng::MeshHandle crate = mesh("prop_crate.obj");
        eng::MeshHandle sack = mesh("prop_jutesack.obj");
        eng::MeshHandle hay = mesh("prop_haybale.obj");
        eng::MeshHandle pumpkin = mesh("prop_pumpkin.obj");
        eng::MeshHandle vase0 = mesh("prop_vase_p0.obj");
        eng::MeshHandle vase1 = mesh("prop_vase_p1.obj");

        // --- entry hall (z ~ +24): a market table greets the player.
        eng::NodeHandle table = place2(
            mesh("prop_table_p0.obj"), "Game/PropWood",
            mesh("prop_table_p1.obj"), "Game/PropMarket",
            {7.0f, 0.88f, 24.5f}, -120.0f, false);
        r.attachMesh(r.createNode(table, {0.3f, 0.53f, -0.2f}),
                     mesh("prop_bread.obj"), "Game/PropMarketMisc");
        r.attachMesh(r.createNode(table, {-0.35f, 0.59f, 0.25f}), pumpkin,
                     "Game/PropMarketMisc");
        place2(vase0, "Game/PropTerracotta", vase1, "Game/PropPlanks",
               {-8.5f, 0.0f, 25.0f}, 30.0f);
        place(sack, "Game/PropJute", {-5.0f, 0.0f, 22.8f}, 100.0f);

        // --- great hall corners (x +-10, z +-6 interior)
        place2(barrel0, "Game/PropPlanks", barrel1, "Game/PropBauerhaus",
               {-8.8f, 0.0f, 4.5f}, 15.0f);
        place2(barrel0, "Game/PropPlanks", barrel1, "Game/PropBauerhaus",
               {-9.0f, 0.0f, 3.4f}, 80.0f);
        if (depth == 0) {
            const glm::vec3 c{-9.0f, 0.0f, -4.5f};
            place(crate, "Game/PropMarket", c, 10.0f, noScale, false);
            place(crate, "Game/PropMarket", c + glm::vec3(0, 0.24f, 0), -25.0f,
                  noScale, false);
            place(crate, "Game/PropMarket", c + glm::vec3(0, 0.48f, 0), 40.0f,
                  noScale, false);
        }
        place(hay, "Game/PropHay", {8.7f, 0.0f, 4.4f}, 20.0f, noScale, false);
        place(hay, "Game/PropHay", {7.6f, 0.0f, 5.0f}, -60.0f, noScale, false);
        place(pumpkin, "Game/PropMarketMisc", {9.2f, 0.08f, 3.4f}, 0.0f);
        place2(vase0, "Game/PropTerracotta", vase1, "Game/PropPlanks",
               {8.8f, 0.0f, -4.5f}, 0.0f);
        place(sack, "Game/PropJute", {9.1f, 0.0f, -3.4f}, -15.0f);

        // --- vault (z ~ -18..-26): sword stabbed into the floor, shield on a
        // barrel. The weapons-pack meshes are authored huge; scale down.
        {
            eng::NodeHandle sword =
                r.createNode(eng::kRootNode, {0.0f, 1.15f, -24.0f});
            r.setScale(sword, glm::vec3(0.06f));
            r.setOrientation(sword,
                             glm::angleAxis(glm::radians(168.0f),
                                            glm::vec3(0, 0, 1)) *
                                 glm::angleAxis(glm::radians(8.0f),
                                                glm::vec3(1, 0, 0)));
            r.attachMesh(sword, mesh("prop_sword.obj"), "Game/PropWeapon");

            const glm::vec3 b{-4.0f, 0.0f, -24.2f};
            place2(barrel0, "Game/PropPlanks", barrel1, "Game/PropBauerhaus",
                   b, 0.0f);
            eng::NodeHandle shield =
                r.createNode(eng::kRootNode, b + glm::vec3(0.0f, 0.55f, -0.75f));
            r.setScale(shield, glm::vec3(0.08f));
            r.setOrientation(shield,
                             glm::angleAxis(glm::radians(180.0f),
                                            glm::vec3(0, 1, 0)) *
                                 glm::angleAxis(glm::radians(-20.0f),
                                                glm::vec3(1, 0, 0)));
            r.attachMesh(shield, mesh("prop_shield.obj"), "Game/PropWeapon");
        }
        place2(mesh("prop_barrel_open_p0.obj"), "Game/PropPlanksTwoSided",
               mesh("prop_barrel_open_p1.obj"), "Game/PropBauerhausTwoSided",
               {4.2f, 0.0f, -24.0f}, -30.0f);

        // --- braziers: ground the demo's two omni lamps in open barrels with
        // a fire on the rim and lift the light just above the flames.
        {
            eng::MeshHandle brz0 = mesh("prop_barrel_open_p0.obj");
            eng::MeshHandle brz1 = mesh("prop_barrel_open_p1.obj");
            const auto& omnis = lv.scene.omniNodes();
            const float xs[2] = {-4.0f, 4.0f};
            for (size_t i = 0; i < omnis.size() && i < 2; ++i) {
                place2(brz0, "Game/PropPlanksTwoSided", brz1,
                       "Game/PropBauerhausTwoSided",
                       {xs[i], 0.0f, 0.0f}, i == 0 ? 25.0f : -40.0f);
                eng::NodeHandle flame =
                    r.createNode(eng::kRootNode, {xs[i], 1.35f, 0.0f});
                r.attachParticles(flame, "Game/TorchGlow");
                r.attachParticles(flame, "Game/TorchFire");
                r.attachParticles(flame, "Game/TorchAsh");
                eng::NodeHandle smoke =
                    r.createNode(flame, {0.0f, 0.12f, 0.0f});
                r.attachParticles(smoke, "Game/FireSmoke");
                r.setPosition(omnis[i], {xs[i], 1.6f, 0.0f});
            }
        }
        {
            const glm::vec3 c{-4.5f, 0.0f, -20.0f};
            place(crate, "Game/PropMarket", c, -20.0f, noScale, false);
            place(crate, "Game/PropMarket", c + glm::vec3(0, 0.24f, 0), 15.0f,
                  noScale, false);
        }
    }

    // ------------------------------------------- hall centrepiece ---
    // Treasure shrine: a low-poly chest levitating over the origin (anchor
    // room centre), ringed by the demo's crystal spires + offering clutter,
    // with a warm gold spill that pulses like banked coals.
    lv.chestGlowColour = glm::vec3(1.0f, 0.62f, 0.22f) * 1.6f;
    {
        const std::string props = assets + "/meshes/props/";
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

        lv.chestBase = r.createNode(eng::kRootNode, {0.0f, 1.35f, 0.0f});
        lv.chestSpin = r.createNode(lv.chestBase);
        r.setScale(lv.chestSpin, glm::vec3(6.0f));
        r.attachMesh(lv.chestSpin, r.loadObj(props + "prop_chest.obj"),
                     "Game/PropChest", true);
        r.attachParticles(lv.chestBase, "PSX/Sparkles");
        eng::LightDesc glow;
        glow.colour = lv.chestGlowColour;
        glow.range = 6.0f;
        lv.chestGlow = r.attachLight(lv.chestBase, glow);
    }

    }

    // Portals: generated low-poly arch + opaque scrolling sprite membrane. The
    // threshold remains on the cell centre so interaction/navigation stays
    // deterministic while the tall silhouette reads across a whole room.
    {
        const auto portalBlockers = [&](glm::vec3 at, const char* label) {
            for (float side : {-1.0f, 1.0f}) {
                ShowcaseExhibit pillar;
                pillar.id = label;
                pillar.label = label;
                pillar.position = at + glm::vec3(side * 1.25f, 1.2f, 0.0f);
                pillar.halfExtents = {0.46f, 1.2f, 0.42f};
                pillar.blocksMovement = true;
                lv.exhibits.push_back(std::move(pillar));
            }
        };
        PortalStyle down;
        down.frameMesh = assets + "/meshes/props/portal_stone_arch.obj";
        down.lightColour = {0.06f, 0.42f, 0.025f};
        down.yawDegrees = lv.map.exitYawDegrees();
        lv.downPortal = createPortal(r, lv.exit, down);
        if (depth == 0) {
            const glm::vec3 local{0.0f, 2.82f, 0.10f};
            const eng::NodeHandle label = r.createNode(
                lv.downPortal.root, local);
            eng::TextSpriteStyle style = showcaseLabelStyle(
                0.48f, {0.22f, 0.82f, 0.18f, 1.0f});
            // Deliberately force a two-line plaque: it remains readable at
            // the low-resolution presentation target without spanning the
            // full arch width.
            style.maxWidthPixels = 72;
            style.colourRules.push_back(
                {"PORTAL", {0.48f, 0.92f, 0.30f, 1.0f}});
            const eng::SpriteHandle sprite =
                r.attachTextSprite(label, "DUNGEON PORTAL", style);
            r.setSpriteVisible(sprite, false);
            const float yaw = glm::radians(down.yawDegrees);
            lv.worldLabels.push_back({label, sprite, lv.exit + glm::vec3(
                std::sin(yaw) * local.z, local.y, std::cos(yaw) * local.z)});
        }
        portalBlockers(lv.exit, "Dungeon Portal — animated fel gate");
        if (depth > 0) {
            PortalStyle up;
            up.frameMesh = assets + "/meshes/props/portal_stone_arch.obj";
            up.material = "Game/PortalUp";
            up.lightColour = {0.18f, 0.90f, 1.35f};
            lv.upPortal = createPortal(r, lv.spawn, up);
            portalBlockers(lv.spawn, "Return Portal — animated arcane gate");
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
    animatePortal(r, downPortal, animationTime);
    animatePortal(r, upPortal, animationTime, -1.0f);
    if (chestBase.valid()) {
        r.setPosition(chestBase,
                      {0.0f, 1.35f + 0.25f * std::sin(animationTime * 0.9f),
                       0.0f});
        r.setOrientation(
            chestSpin,
            glm::angleAxis(animationTime * 0.8f, glm::vec3(0, 1, 0)));
        const float pulse = 0.9f + 0.1f * std::sin(animationTime * 1.7f) +
                            0.05f * std::sin(animationTime * 4.3f);
        r.setLightColour(chestGlow, chestGlowColour * pulse);
    }
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
// (which sits at the model's base) at the correct world position.
struct DynamicProp {
    eng::NodeHandle node;
    eng::BodyHandle body;
    glm::vec3 renderOffset{0.0f}; // body centre - mesh base (vertical half-height)
};

int main(int, char**)
{
    // Dev self-test: PSX_GEN_DUMP=<seed> prints a generated grid and exits,
    // no window/Ogre. Eyeball connectivity + room shapes across seeds.
    if (const char* dump = std::getenv("PSX_GEN_DUMP")) {
        const auto grid = gen::generate(uint32_t(std::strtoul(dump, nullptr, 10)));
        for (const std::string& row : grid.rows())
            std::printf("%s\n", row.c_str());
        return 0;
    }

    eng::Engine engine;
    const std::string assets = APP_ASSET_DIR;
    if (!engine.init(assets + "/game.toml", assets))
        return 1;
    eng::Renderer& r = engine.renderer();

    r.setCameraFov(70.0f);
    // With the current 0.05 exponential fog, a 90 m far plane retains only
    // ~1.1% scene colour. The cut is hidden without visibly popping long
    // corridors; the 0.08 m near plane also improves depth precision.
    r.setCameraClip(0.08f, 90.0f);

    uint32_t baseSeed = 1;
    if (const char* s = std::getenv("PSX_GEN_SEED"))
        baseSeed = uint32_t(std::strtoul(s, nullptr, 10));

    // Persistent level stack: seeds[d] is depth d's seed (stored so revisits
    // reuse it -> identical layout). Live scenes are never cached; one level
    // is live at a time and rebuilt on every transition.
    std::vector<uint32_t> seeds{baseSeed};
    int depth = 0;
    const float speed = float(engine.config().getNumber("player.speed", 3.0));
    const float sens =
        float(engine.config().getNumber("player.mouse_sensitivity", 0.002));

    bool showColliders = false;

    eng::Physics physics;
    physics.init();

    ProjectileSystem projectiles;
    MeleeSystem melee;

    // Dynamic prop table: bodies spawned once for the depth-0 lobby and
    // synced to render nodes every frame while propsAlive is true.
    // Known limitation: props are not re-spawned on level transition; their
    // bodies are removed and propsAlive set false before any rebuild.
    std::vector<DynamicProp> dynamicProps;
    bool propsAlive = false;
    Dummy dummy;
    bool dummyAlive = false;
    melee.setHitCallback([&dummy, &dummyAlive, &physics](
                             eng::BodyHandle body, glm::vec3 point,
                             glm::vec3 normal) {
        if (dummyAlive && dummy.alive() && body == dummy.body())
            dummy.kill(physics, -normal * 8.0f + glm::vec3(0.0f, 3.0f, 0.0f),
                       point);
    });

    LiveLevel level;
    FpsController player;
    ViewModel viewModel;
    const bool portalPreviewMode =
        std::getenv("PSX_SHOWCASE_PORTAL") != nullptr;

    // Wipe the scene, build the level at `depth`, and (re)spawn the player.
    // atExit spawns at the down-portal (arrived by ascending); else at entry.
    const auto enterLevel = [&](bool atExit) {
        // Destroy dynamic prop bodies before clearScene wipes their nodes.
        if (propsAlive) {
            for (auto& dp : dynamicProps)
                physics.removeBody(dp.body);
            dynamicProps.clear();
            propsAlive = false;
        }
        if (dummyAlive) {
            dummy.clear(physics, r);
            dummyAlive = false;
        }
        bool loaded = false;
        if (depth == 0) {
            LevelDocument lobby;
            std::string error;
            if (lobby.loadToml(assets + "/lobby.toml", error)) {
                loaded = level.rebuildLayout(r, physics, assets,
                                             lobby.validated(), depth);
            } else {
                eng::log::error("Lobby: %s", error.c_str());
            }
        } else {
            loaded = level.rebuild(r, physics, assets, seeds[size_t(depth)],
                                   depth);
        }
        if (!loaded) {
            eng::log::error("Level %d failed to load", depth);
            return;
        }
        const bool portalPreview = depth == 0 && portalPreviewMode;
        const float portalYaw = glm::radians(level.dungeon().exitYawDegrees());
        const glm::vec3 portalFront(std::sin(portalYaw), 0.0f,
                                    std::cos(portalYaw));
        const glm::vec3 p = portalPreview
            ? level.exitPosition() + portalFront * 4.0f
            : (atExit ? level.exitPosition() : level.spawnPosition());
        player.init(r, physics, p, speed, sens, glm::vec3(-1000.0f), glm::vec3(1000.0f));
        if (portalPreview) {
            player.setViewAngles(portalYaw);
            player.present(r);
        }
        // Carried light rides the fresh head node (the old one was destroyed).
        eng::LightDesc carry;
        carry.colour = glm::vec3(std::pow(1.0f, 2.2f), std::pow(0.80f, 2.2f),
                                 std::pow(0.58f, 2.2f)) * 0.95f;
        carry.range = 7.0f;
        r.attachLight(player.headNode(), carry);
        viewModel.init(r, player.headNode(), assets + "/meshes/props");
        engine.input().setMouseGrab(!portalPreview);
    };
    enterLevel(false); // depth 0, spawn at entry

    // Initialise the projectile system (builds procedural meshes) and register
    // the contact seam so arrows stick and bolts despawn on impact.
    projectiles.init(r);
    physics.setContactCallback([&projectiles, &physics, &dummy, &dummyAlive](const eng::HitEvent& e) {
        projectiles.onHit(physics, e);
        if (dummyAlive && dummy.alive() &&
            (e.self == dummy.body() || e.other == dummy.body())) {
            // Arrow hit the dummy: knock it forward and upward
            dummy.kill(physics, glm::vec3(0.0f, 3.0f, 6.0f), e.point);
        }
    });

    // Spawn dynamic crates and barrels in the lobby entry hall.
    // Props sit a few metres in front of the spawn (toward the anchor room).
    // Mesh origins are at the base; body centres are offset up by halfHeight.
    // Crate: 0.8 m cube -> halfExtents {0.4, 0.4, 0.4}, body centre y = 0.4.
    // Barrel: r=0.28, h=0.9 -> halfHeight 0.45, body centre y = 0.45.
    {
        const std::string props = assets + "/meshes/props/";
        eng::MeshHandle mCrate   = r.loadObj(props + "prop_crate.obj");
        eng::MeshHandle mBarrel0 = r.loadObj(props + "prop_barrel_p0.obj");
        eng::MeshHandle mBarrel1 = r.loadObj(props + "prop_barrel_p1.obj");

        const auto spawnCrate = [&](glm::vec3 bodyPos, float yawDeg) {
            constexpr float hh = 0.4f;
            eng::BodyDesc bd;
            bd.kind = eng::ShapeKind::Box;
            bd.halfExtents = {0.4f, hh, 0.4f};
            bd.position = bodyPos;
            bd.layer = eng::BodyLayer::Prop;
            bd.dynamic = true;
            bd.mass = 5.0f;
            bd.friction = 0.6f;
            eng::BodyHandle bh = physics.createBody(bd);
            // Node origin at mesh base = body centre lowered by halfHeight
            glm::vec3 nodePos = bodyPos - glm::vec3(0.0f, hh, 0.0f);
            eng::NodeHandle nh = r.createNode(eng::kRootNode, nodePos);
            if (yawDeg != 0.0f)
                r.setOrientation(nh, glm::angleAxis(glm::radians(yawDeg),
                                                    glm::vec3(0.0f, 1.0f, 0.0f)));
            r.attachMesh(nh, mCrate, "Game/PropMarket");
            DynamicProp dp;
            dp.node = nh;
            dp.body = bh;
            dp.renderOffset = glm::vec3(0.0f, hh, 0.0f);
            dynamicProps.push_back(dp);
        };

        const auto spawnBarrel = [&](glm::vec3 bodyPos, float yawDeg) {
            constexpr float halfH = 0.45f;
            constexpr float radius = 0.28f;
            eng::BodyDesc bd;
            bd.kind = eng::ShapeKind::Cylinder;
            bd.halfHeight = halfH;
            bd.radius = radius;
            bd.position = bodyPos;
            bd.layer = eng::BodyLayer::Prop;
            bd.dynamic = true;
            bd.mass = 8.0f;
            bd.friction = 0.6f;
            eng::BodyHandle bh = physics.createBody(bd);
            glm::vec3 nodePos = bodyPos - glm::vec3(0.0f, halfH, 0.0f);
            eng::NodeHandle nh = r.createNode(eng::kRootNode, nodePos);
            if (yawDeg != 0.0f)
                r.setOrientation(nh, glm::angleAxis(glm::radians(yawDeg),
                                                    glm::vec3(0.0f, 1.0f, 0.0f)));
            r.attachMesh(nh, mBarrel0, "Game/PropPlanks");
            r.attachMesh(nh, mBarrel1, "Game/PropBauerhaus");
            DynamicProp dp;
            dp.node = nh;
            dp.body = bh;
            dp.renderOffset = glm::vec3(0.0f, halfH, 0.0f);
            dynamicProps.push_back(dp);
        };

        // Two crates stacked near the entry hall (spawn side of the anchor room)
        spawnCrate({3.0f, 0.4f, 18.0f},   10.0f);   // ground crate
        spawnCrate({3.0f, 1.2f, 18.0f},  -15.0f);   // stacked on top
        // A third crate to the side
        spawnCrate({1.5f, 0.4f, 17.0f},   30.0f);
        // Two barrels next to them
        spawnBarrel({4.5f, 0.45f, 17.5f},   0.0f);
        spawnBarrel({5.2f, 0.45f, 18.5f},  20.0f);
        // One more loose crate for variety
        spawnCrate({2.2f, 0.4f, 19.5f},  -20.0f);

        propsAlive = true;
    }

    // Spawn a topple dummy alongside the lobby props (entry hall area).
    // Placed 3 m further toward the anchor room from the crate cluster.
    dummy.init(physics, r, glm::vec3(3.0f, 0.0f, 15.0f));
    dummyAlive = true;

    engine.debugUi().addPanel("Player", [&player, &r] {
        ImGui::SliderFloat("move speed", &player.speed(), 0.5f, 15.0f);
        ImGui::SliderFloat("mouse sensitivity", &player.sensitivity(), 0.0005f,
                           0.01f, "%.4f");
        float baseFov = player.baseFov();
        if (ImGui::SliderFloat("locomotion base FOV", &baseFov, 30.0f, 120.0f,
                               "%.0f"))
            player.setBaseFov(baseFov);
        ImGui::Text("stance: %s", player.crouched() ? "crouched" : "standing");
        ImGui::Text("sprint: %s  stamina: %3.0f%%",
                    player.sprinting() ? "active" : "ready",
                    player.sprintStamina() * 100.0f);
        ImGui::Text("movement: %s", player.sliding() ? "sliding"
                                                       : (player.grounded() ? "grounded"
                                                                            : "airborne"));
    });
    engine.debugUi().addPanel("Physics", [&physics, &player, &showColliders] {
        ImGui::Text("active bodies: %d", physics.activeBodyCount());
        float g = physics.gravityY();
        if (ImGui::SliderFloat("gravity Y", &g, -40.0f, 0.0f, "%.1f"))
            physics.setGravity(g);
        ImGui::Checkbox("show colliders", &showColliders);
        ImGui::Separator();
        ImGui::Text("grounded: %s", player.grounded() ? "yes" : "no");
        const glm::vec3 n = player.groundNormal();
        ImGui::Text("ground normal: %.2f %.2f %.2f", n.x, n.y, n.z);
        ImGui::Text("horizontal speed: %.2f m/s", player.horizontalSpeed());
        ImGui::Text("stance: %s", player.crouched() ? "crouched"
                    : (player.sliding() ? "sliding" : "standing"));
    });
    LevelEditor editor(level.dungeon().debugLayoutRows(),
                       assets + "/editor_level.toml");
    engine.debugUi().addWindow([&level, &player, &viewModel, &editor, &r, &physics,
                                &assets, &depth, speed, sens, &engine] {
        if (!editor.draw(level.dungeon(), player.eyePosition()))
            return;
        const gen::Layout layout = editor.takeLayout();
        if (!level.rebuildLayout(r, physics, assets, layout, depth))
            return;
        player.init(r, physics, level.spawnPosition(), speed, sens,
                    glm::vec3(-1000.0f), glm::vec3(1000.0f));
        eng::LightDesc carry;
        carry.colour = glm::vec3(std::pow(1.0f, 2.2f), std::pow(0.80f, 2.2f),
                                 std::pow(0.58f, 2.2f)) * 0.95f;
        carry.range = 7.0f;
        r.attachLight(player.headNode(), carry);
        viewModel.init(r, player.headNode(), assets + "/meshes/props");
        engine.input().setMouseGrab(false);
    });

    // ---------------------------------------------------------------- loop ---
    constexpr float kFixedDt = 1.0f / 60.0f;
    float accumulator = 0.0f;
    float animTime = 0.0f;
    std::vector<GameplayTarget> targets;
    targets.reserve(64);
    while (!engine.shouldClose()) {
        const float dt = engine.tick();
        eng::Input& in = engine.input();
        // First Esc releases the mouse, second quits; click re-grabs.
        // Suspended while the debug panel is open (F1 owns grab then).
        if (!engine.debugUi().visible()) {
            if (in.wasPressed("quit")) {
                if (in.mouseGrabbed())
                    in.setMouseGrab(false);
                else
                    engine.requestClose();
            }
            if (!in.mouseGrabbed() && in.wasMouseClicked())
                in.setMouseGrab(true);
        }

        // Fixed-step physics. Cap at 5 steps to prevent spiral of death.
        accumulator += dt;
        int guard = 0;
        while (accumulator >= kFixedDt && guard++ < 5) {
            physics.update(kFixedDt);
            projectiles.fixedUpdate(physics, r, kFixedDt);
            melee.fixedUpdate(physics, player.eyePosition(), player.forward(), kFixedDt);
            accumulator -= kFixedDt;
        }
        physics.setInterpolationAlpha(accumulator / kFixedDt);

        // Sync dynamic prop render nodes from the interpolated physics transform.
        if (propsAlive) {
            for (auto& dp : dynamicProps) {
                glm::vec3 p; glm::quat q;
                physics.getRenderTransform(dp.body, p, q);
                r.setPosition(dp.node, p - dp.renderOffset);
                r.setOrientation(dp.node, q);
            }
        }
        projectiles.syncRender(physics, r);
        if (dummyAlive)
            dummy.syncRender(physics, r);

        animTime += dt;
        level.update(r, animTime);
        level.updateVisibility(r, player.eyePosition());

        if (!portalPreviewMode)
            player.update(in, r, dt);

        targets.clear();
        level.appendTargets(targets, depth);
        const GameplayTarget* target = aimedTarget(
            targets, player.eyePosition(), player.forward());
        if (!target) {
            engine.debugUi().setHudPrompt({});
        } else if (target->kind == TargetKind::Torch) {
            engine.debugUi().setHudPrompt(level.torchIsLit(target->id)
                                              ? "Press [E] to snuff the torch"
                                              : "Press [E] to light the torch");
            if (in.wasPressed("interact"))
                level.toggleTorch(r, target->id);
        } else if (target->kind == TargetKind::PortalDown) {
            engine.debugUi().setHudPrompt("Press [E] to descend");
            if (in.wasPressed("interact")) {
                if (depth + 1 == int(seeds.size()))
                    seeds.push_back(baseSeed +
                                    uint32_t(depth + 1) * 0x9E3779B9u);
                ++depth;
                enterLevel(false);
            }
        } else {
            engine.debugUi().setHudPrompt("Press [E] to ascend");
            if (in.wasPressed("interact")) {
                --depth;
                enterLevel(true);
            }
        }

        // Projectile firing — only when mouse is grabbed (not in debug UI).
        bool swordAttack = false;
        if (in.mouseGrabbed()) {
            if (in.wasPressed("fire_arrow"))
                projectiles.fireArrow(physics, r, player.eyePosition(), player.forward());
            if (in.wasPressed("cast_spell"))
                projectiles.fireBolt(physics, r, player.eyePosition(), player.forward());
            if (in.wasMouseClicked()) {
                melee.startSwing();
                swordAttack = true;
            }
        }
        viewModel.update(r, dt, swordAttack,
                         in.mouseGrabbed() &&
                             in.isMouseDown(eng::MouseButton::Right));

        // Physics collider wireframe overlay
        if (showColliders) {
            static std::vector<eng::Physics::DebugLine> pl;
            pl.clear();
            physics.debugDraw(pl);
            static std::vector<eng::Renderer::DebugLine> dl;
            dl.clear();
            for (const auto& l : pl)
                dl.push_back({l.a, l.b, l.colour});
            r.setDebugLines(dl);
        } else {
            r.setDebugLines({});
        }

        engine.renderFrame(dt);
    }
    // Remove dynamic prop bodies before shutdown (nodes are owned by Ogre/scene).
    if (propsAlive) {
        for (auto& dp : dynamicProps)
            physics.removeBody(dp.body);
        dynamicProps.clear();
        propsAlive = false;
    }
    if (dummyAlive) {
        dummy.clear(physics, r);
        dummyAlive = false;
    }
    level.clearPhysics();
    projectiles.clear(physics, r);
    physics.shutdown();
    engine.shutdown();
    return 0;
}
