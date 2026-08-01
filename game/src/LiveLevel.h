#pragma once
#include "combat/CombatVocabulary.h"
#include "DungeonGen.h"    // gen::Layout
#include "DungeonMap.h"
#include "SceneFactory.h"  // PortalProp, ShowcaseExhibit, TreasureShrine
#include "Targeting.h"     // GameplayTarget
#include "scene/MapRuntime.h"

#include <ecs/RendererSceneBackend.h>
#include <eng/ecs/World.h>

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
    bool rebuild(eng::ecs::World& world, eng::Renderer& r,
                 eng::Physics& physics,
                 const game::CombatVocabulary& vocabulary, uint32_t seed,
                 int depth);
    bool rebuildLayout(eng::ecs::World& world, eng::Renderer& r,
                       eng::Physics& physics,
                       const game::CombatVocabulary& vocabulary,
                       gen::Layout layout, int depth);
    bool rebuildAuthored(eng::ecs::World& world, eng::Renderer& r,
                         eng::Physics& physics,
                         const game::CombatVocabulary& vocabulary,
                         const std::string& cookedMap, int depth);
    // `animationTime` is the quantised world clock (pose-from-time dressing);
    // `dt` is the step it advanced by, which is what the component systems
    // integrate. Both, because the two kinds of animation in this level are
    // genuinely different: a torch's pose is a function of the clock, and a
    // spinning prop accumulates.
    void update(eng::Renderer& r, float animationTime, float dt);
    void updateVisibility(eng::Renderer& r, glm::vec3 cameraPos);
    void appendTargets(std::vector<GameplayTarget>& targets, int depth) const;
    glm::vec3 spawnPosition() const { return spawn; }
    glm::vec3 exitPosition() const { return exit; }
    float exitYawDegrees() const { return exitYaw; }
    glm::vec3 markerPosition(const std::string& type,
                             glm::vec3 fallback = glm::vec3(0.0f)) const;
    std::vector<game::ScenePlacement> markerPlacements(
        const std::string& prefix) const;
    // Encounters an authored level asks for, whether they were written as
    // markers ("enemy.goblin") or as the editor's own EnemySpawn component.
    // Both mean the same thing, so both arrive here.
    std::vector<game::ScenePlacement> enemyPlacements() const;
    bool torchIsLit(int index) const { return map.torchLit(index); }
    void toggleTorch(eng::Renderer& r, int index) { map.toggleTorch(r, index); }
    const DungeonMap& dungeon() const { return map; }
    // The two portal props, for tooling that re-dresses them live (the debug
    // console's Portal tab). Mutable: switching the wisp effect replaces the
    // handle the prop is holding. The ascent one is invalid at depth 0.
    PortalProp& portal(bool ascent) { return ascent ? upPortal : downPortal; }
    void clearPhysics();

private:
    friend LiveLevel buildLevel(eng::ecs::World&, eng::Renderer&, eng::Physics&,
                                 const game::CombatVocabulary&, uint32_t, int,
                                 const gen::Layout*, const std::string*);
    DungeonMap map;
    // The game's world, which OUTLIVES this level: the player and everything
    // else persistent lives on it, so a transition must not take it down. What
    // the level added carries kLevelGroup and is destroyed by group on
    // teardown. Non-owning, and a pointer rather than a reference because
    // buildLevel returns by value and LiveLevel is assigned over.
    //
    // Not everything is on it yet -- the batched dungeon shell stays in `map`
    // (batched static geometry is a different path from per-entity actors, as
    // in any shipping engine) and combatants stay on CombatDirector's registry
    // until that system is moved across.
    eng::ecs::World* world = nullptr;
    std::unique_ptr<game::MapRuntime> authoredMap;
    DemoScene scene;
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

// Entities a level owns carry this group, so a transition destroys exactly
// them. Anything persistent (the player) is created with no group at all.
inline constexpr uint32_t kLevelGroup = 1;

// Build a complete level into the (already-clear) scene. Declared here so tests
// or tools could build a level directly; defined in LiveLevel.cpp. depth>0 adds
// an up-portal at the entry.
LiveLevel buildLevel(eng::ecs::World& world, eng::Renderer& r,
                     eng::Physics& physics,
                     const game::CombatVocabulary& vocabulary, uint32_t seed,
                     int depth, const gen::Layout* authored = nullptr,
                     const std::string* authoredMap = nullptr);
