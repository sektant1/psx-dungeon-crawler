#pragma once
#include "DungeonGen.h"    // gen::Layout
#include "DungeonMap.h"
#include "GameScene.h"
#include "SceneFactory.h"  // PortalProp, ShowcaseExhibit, TreasureShrine
#include "Targeting.h"     // GameplayTarget

#include <DemoScene.h>
#include <eng/Handles.h>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace eng { class Renderer; class Physics; }

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
    // Game-side ECS scene: owns per-entity gameplay actors (static set-dressing
    // props migrated in R1; more actors follow). Pinned on the heap so the
    // Renderer/Scene refs inside it survive LiveLevel moves. Null until the
    // first buildLevel. The batched dungeon shell stays in `map`, not here.
    std::unique_ptr<game::GameScene> gameScene;
    eng::NodeHandle chestBase{}, chestSpin{};
    // Chest glow is an ECS light actor (R1b): animated position + colour are
    // written to its components each frame; SceneSync pushes them to the light.
    entt::entity chestGlowEntity{entt::null};
    glm::vec3 chestGlowColour{0.0f};
    glm::vec3 spawn{0.0f}, exit{0.0f};
    PortalProp downPortal{};
    PortalProp upPortal{}; // invalid at depth 0
    std::vector<ShowcaseExhibit> exhibits;
    struct WorldLabel {
        eng::NodeHandle node{};
        eng::SpriteHandle sprite{};
        glm::vec3 position{0.0f};
    };
    std::vector<WorldLabel> worldLabels;
};

// Build a complete level into the (already-clear) scene. Declared here so tests
// or tools could build a level directly; defined in LiveLevel.cpp. depth>0 adds
// an up-portal at the entry.
LiveLevel buildLevel(eng::Renderer& r, eng::Physics& physics,
                     const std::string& assets, uint32_t seed, int depth,
                     const gen::Layout* authored = nullptr);
