#pragma once
#include "combat/CombatVocabulary.h"
#include "DungeonGen.h"    // gen::Layout
#include "DungeonMap.h"
#include "GameScene.h"
#include "SceneFactory.h"  // PortalProp, ShowcaseExhibit, TreasureShrine
#include "Targeting.h"     // GameplayTarget
#include "scene/MapRuntime.h"

#include <ecs/RendererSceneBackend.h>

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
                 const game::CombatVocabulary& vocabulary, uint32_t seed,
                 int depth);
    bool rebuildLayout(eng::Renderer& r, eng::Physics& physics,
                       const game::CombatVocabulary& vocabulary,
                       gen::Layout layout, int depth);
    bool rebuildAuthored(eng::Renderer& r, eng::Physics& physics,
                         const game::CombatVocabulary& vocabulary,
                         const std::string& cookedMap, int depth);
    void update(eng::Renderer& r, float animationTime);
    void updateVisibility(eng::Renderer& r, glm::vec3 cameraPos);
    void appendTargets(std::vector<GameplayTarget>& targets, int depth) const;
    glm::vec3 spawnPosition() const { return spawn; }
    glm::vec3 exitPosition() const { return exit; }
    float exitYawDegrees() const { return exitYaw; }
    glm::vec3 markerPosition(const std::string& type,
                             glm::vec3 fallback = glm::vec3(0.0f)) const;
    std::vector<game::ScenePlacement> markerPlacements(
        const std::string& prefix) const;
    bool torchIsLit(int index) const { return map.torchLit(index); }
    void toggleTorch(eng::Renderer& r, int index) { map.toggleTorch(r, index); }
    const DungeonMap& dungeon() const { return map; }
    // The two portal props, for tooling that re-dresses them live (the debug
    // console's Portal tab). Mutable: switching the wisp effect replaces the
    // handle the prop is holding. The ascent one is invalid at depth 0.
    PortalProp& portal(bool ascent) { return ascent ? upPortal : downPortal; }
    void clearPhysics();

private:
    friend LiveLevel buildLevel(eng::Renderer&, eng::Physics&,
                                 const game::CombatVocabulary&, uint32_t, int,
                                 const gen::Layout*, const std::string*);
    DungeonMap map;
    std::unique_ptr<eng::ecs::RendererSceneBackend> authoredBackend;
    std::unique_ptr<game::MapRuntime> authoredMap;
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
    glm::vec3 chestOrigin{0.0f};
    glm::vec3 spawn{0.0f}, exit{0.0f};
    float exitYaw = 0.0f;
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
                     const game::CombatVocabulary& vocabulary, uint32_t seed,
                     int depth, const gen::Layout* authored = nullptr,
                     const std::string* authoredMap = nullptr);
