#pragma once
#include <ecs/RendererSceneBackend.h>

#include <eng/Handles.h>
#include <eng/LightDesc.h>
#include <eng/ecs/Scene.h>
#include <eng/ecs/SceneSync.h>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>

namespace eng { class Renderer; }

namespace game {

// Game-side ECS scene: the registry is the source of truth for gameplay actors
// (props, pickups, dynamic set dressing) and eng::Renderer is driven as a pure
// view through SceneSync. This is the R1 seam that replaces direct
// r.createNode/attachMesh calls for per-entity content.
//
// NOT represented here: the static dungeon shell (walls, floors, arches). That
// stays in DungeonMap's region-batched StaticGeometry -- batched static
// geometry is a separate path from per-entity actors, exactly as in shipping
// engines (level geometry vs actors).
//
// One GameScene is built per live level and destroyed with it. It is pinned on
// the heap (held via unique_ptr in LiveLevel) so the Renderer/Scene references
// captured by its backend and sync stay valid across LiveLevel moves.
class GameScene {
public:
    explicit GameScene(eng::Renderer& r);
    GameScene(const GameScene&) = delete;
    GameScene& operator=(const GameScene&) = delete;

    // Spawn a static (non-animated, unparented) renderable actor. Equivalent to
    // a direct createNode + attachMesh: SceneSync allocates the backing node and
    // attaches the mesh on the next sync().
    entt::entity spawnStatic(eng::MeshHandle mesh, const std::string& material,
                             glm::vec3 position,
                             glm::quat orientation = glm::quat(1, 0, 0, 0),
                             glm::vec3 scale = glm::vec3(1.0f),
                             bool castShadows = false,
                             const std::string& name = {});

    // Spawn a point/omni light actor at a world position. The light is attached
    // by SceneSync on the next sync(). The returned entity carries a LightColour
    // component seeded from desc.colour; write it (and the entity's transform)
    // each frame to animate flicker/pulse and motion.
    entt::entity spawnLight(const eng::LightDesc& desc, glm::vec3 position,
                            const std::string& name = {});

    // Reconcile the renderer with the registry. Call once per frame after
    // gameplay has mutated components.
    void sync() { mSync.sync(); }

    eng::ecs::Scene& scene() { return mScene; }

private:
    eng::ecs::Scene mScene;
    eng::ecs::RendererSceneBackend mBackend;
    eng::ecs::SceneSync mSync;
};

} // namespace game
