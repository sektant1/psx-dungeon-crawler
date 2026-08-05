#pragma once
#include <eng/ecs/Components.h>

#include <entt/entt.hpp>

#include <memory>
#include <string>

namespace eng {
class Audio;
class Physics;
}

namespace eng::ecs {

class SceneBackend;

// One view driven from the registry: renderer, physics, or audio. World knows
// only this contract and the order to call it in, so
// a build that links neither reconciler still gets a working headless World --
// which is what the combat sim and the map tests run on.
class WorldReconciler
{
public:
    virtual ~WorldReconciler() = default;
    // Components -> view: materialise what is new, push what changed.
    virtual void sync() = 0;
    // View -> components, run before every sync(). The direction only physics
    // needs: a dynamic body's pose is decided by the simulation, and until it
    // lands back on the Transform the registry is not the source of truth about
    // it. Default no-op, because a view that only reads has nothing to give
    // back.
    virtual void readback() {}
    virtual void clear() = 0;
};

// One simulated world: the single registry every entity in it lives on, the
// transform hierarchy over those entities, and the reconcilers that drive the
// renderer and physics from them.
//
// *Game Engine Architecture* (4th ed.) §1.5.15.1 poses the questions a game
// object model has to answer. This engine answers them once, here, rather than
// once per subsystem:
//
//   What is an object?     One `entt::entity` in one World. Never two entities
//                          in two registries standing for the same thing.
//   How is it identified?  By entity id within its World; by `Name` for humans
//                          and by a `StringId` for anything that keys off a
//                          name in a hot path.
//   How is it referenced?  By entity id, checked with registry().valid(). Never
//                          by a raw component pointer held across a frame --
//                          any emplace can move a pool.
//   What owns its lifetime? The World. destroy() unlinks it from the hierarchy;
//                          the next sync() frees whatever renderer node or
//                          physics body was materialised for it.
//   How is it simulated?   By systems that take (World&, dt) and iterate views.
//                          Not by virtual update() on an object.
//
// **One World per simulated world.** This is the invariant the type exists to
// enforce. Before it, a level had three registries -- scene actors, authored map
// entities and combatants -- so an enemy was one entity in the combat registry
// and (at best) another in the scene one, and no view could see both halves.
// Anything that needed the whole object (a tooltip, a save file, a debug
// inspector) had to join them by hand, and every new system had to pick a side.
//
// A *different* world is still fine: the editor's material preview owns its own
// World because it is a different simulation, not half of this one.
//
// Attachments are optional, and that is what makes a headless World real: the
// combat sim and the map tests build one with no backend at all and run the
// same systems the game runs.
class World
{
public:
    World();
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // --- attachments -----------------------------------------------------
    // Materialise renderer nodes, physics bodies, and authored audio voices.
    // All are optional, must outlive this World, and
    // detachAll() exists for the case where they do not.
    // `drivesCamera` is false for a world that is being *looked at* rather
    // than looked *through*: the editor previews documents whose cameras are
    // content, and a preview that took the viewport would fight the author.
    void attachRenderer(SceneBackend& backend, bool drivesCamera = true);
    // The same choice, after the fact. What the scene *is* is only known once
    // it has been loaded -- a scene carrying a ScreenCamera is a 2D screen and
    // its camera belongs to the rig that fits the page, not to the entity
    // transform -- and the renderer is attached before the load.
    void setDrivesCamera(bool drives);
    void attachPhysics(Physics& physics);
    // `drivesListener=false` lets a player/editor camera keep listener control
    // while entity emitters still follow their WorldTransforms.
    void attachAudio(Audio& audio, bool drivesListener = true);
    // Drop every materialised node and body and forget both attachments. Call
    // before the renderer or physics world dies; the registry survives.
    void detachAll();

    // --- entities --------------------------------------------------------
    // Entities created while a group is active are stamped with it, so a level
    // transition can destroy exactly what the level added. 0 (the default)
    // means "lives as long as this World": the player, their inventory, and
    // anything else that must survive a level swap.
    void setActiveGroup(uint32_t group) { mActiveGroup = group; }
    uint32_t activeGroup() const { return mActiveGroup; }

    entt::entity create(std::string name = {});
    // Unlinks from the hierarchy and destroys. Children survive as roots, with
    // their world transforms re-derived on the next resolve. The node/body the
    // entity had is released by the next sync().
    void destroy(entt::entity e);
    // Destroys the entity and everything parented under it. What "delete this
    // object" means when the object is a rig: a projectile and its trail, a
    // torch and its light, a prop and its attachment points are one thing, and
    // orphaning half of it leaves entities alive that nothing can reach.
    void destroyHierarchy(entt::entity e);
    // Destroys every entity stamped with `group`. Returns how many. Group 0 is
    // rejected: it would take the ungrouped survivors with it, which is
    // precisely the bug this mechanism exists to prevent.
    std::size_t destroyGroup(uint32_t group);

    void setLocalTransform(entt::entity e, const Transform& t);
    void setParent(entt::entity e, entt::entity parent);
    void updateWorldTransforms();

    entt::registry& registry() { return mReg; }
    const entt::registry& registry() const { return mReg; }

    // --- frame -----------------------------------------------------------
    // Reconcile the views with the registry, in the one order that is correct:
    //
    //   1. physics readback -- simulated bodies write their pose onto their
    //      Transform, so the registry is current before anything reads it;
    //   2. world transforms, then the renderer, which needs them;
    //   3. physics push -- new bodies, and the authored poses of the ones
    //      gameplay steers.
    //
    // Readback before the renderer rather than after is what keeps a falling
    // prop from drawing a frame behind the body it is falling with.
    //
    // Call once per frame after gameplay has finished mutating components.
    void sync();

    // Destroy every entity and every node/body materialised for one, keeping
    // the attachments. This is what a level teardown wants.
    void clear();

private:
    void markSubtreeDirty(entt::entity e);
    void resolveWorld(entt::entity e);

    entt::registry mReg;
    // unique_ptr rather than optional: both reconcilers hold references and are
    // non-movable, and a World is built before its backends exist. Held as the
    // interface so this TU does not pull in the renderer or Jolt -- each
    // attach() is defined next to the reconciler it constructs.
    std::unique_ptr<WorldReconciler> mRender;
    std::unique_ptr<WorldReconciler> mPhysics;
    std::unique_ptr<WorldReconciler> mAudio;
    uint32_t mActiveGroup = 0;
};

// --- spawn helpers -------------------------------------------------------
// The two archetypes every caller was hand-assembling. Free functions, not
// methods: an archetype is a choice of components, and putting each one on the
// World would make it the place every new archetype gets added.

entt::entity spawnMesh(World& world, MeshHandle mesh, const std::string& material,
                       glm::vec3 position,
                       glm::quat orientation = glm::quat(1, 0, 0, 0),
                       glm::vec3 scale = glm::vec3(1.0f),
                       bool castShadows = false, const std::string& name = {});

// The light starts at desc.colour and carries a LightColour component, so
// gameplay animates flicker or pulse by writing that component.
entt::entity spawnLight(World& world, const LightDesc& desc, glm::vec3 position,
                        const std::string& name = {});

} // namespace eng::ecs
