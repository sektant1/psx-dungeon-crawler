// Play an authored .map: this game's ProjectApp. Reached from
// `game <file.map>`.
//
// The generic half of what this used to be -- load the scene, resolve meshes,
// build render and physics, wire the script host, walk around -- is
// eng::runtime::ProjectApp, and the game boots through exactly the code
// raven_player does. What is left here is what makes it *this* game: its
// component table, its collision layers, its particle collider, its exit
// portals and its spawn marker. Adding to it is a decision that the thing added
// is game content and not runtime.

#include "MapPlay.h"

#include "GameAssets.h"
#include "GameCollision.h"
#include "ParticleCollider.h"
#include "SceneFactory.h"
#include "scene/ComponentRegistry.h"
#include "scene/GameComponents.h"

#include <eng/Log.h>
#include <eng/Renderer.h>
#include <eng/ecs/ComponentRegistry.h>
#include <eng/ecs/Components.h>
#include <eng/render/GifRecorder.h>
#include <eng/runtime/Project.h>
#include <eng/runtime/ProjectApp.h>
#include <eng/runtime/SceneRuntime.h>

#include <entt/entt.hpp>

#include <glm/glm.hpp>

#include <optional>
#include <utility>

namespace game {
namespace {

// A portal at every authored Exit.
//
// The exit is the authored fact -- a position and the yaw the player leaves
// facing -- and the portal is how that fact is *presented*: a lit membrane
// inside a kit surround, assembled by createPortalProp. The campaign built one
// in LiveLevel and the authored-map path did not, so an exit placed in the
// editor played as a bare marker in an empty doorway. The editor is meant to be
// the way the game gets made; a thing you place there has to become the thing
// you see when you play.
//
// Built here rather than in the cooker on purpose: a portal is renderer state
// -- nodes, a light, a particle effect -- not entities, and baking it into the
// .map would freeze a presentation decision into level data.
//
// This is the *generated* path. An authored level does not need it: the kit now
// carries `portal_membrane` and the surround pieces, so a portal placed in the
// editor is ordinary entities -- a membrane with an eng::ecs::PortalParams on
// it, an arch, a light -- and every one of them is inspectable, tunable and
// different from the portal in the next room. An `exit` with no membrane beside
// it still gets this one, which is what keeps a bare marker playable.
void buildExitPortals(eng::Renderer& renderer, const entt::registry& registry)
{
    // An authored portal wins. A scene that placed a membrane has said what its
    // portal is; generating a second one on top of the marker gave two portals
    // in one doorway, one of them untouchable from the editor. The marker keeps
    // its meaning either way -- it is where the level exits -- and only stops
    // carrying a presentation decision the author already made.
    if (!registry.view<const eng::ecs::PortalParams>().empty()) {
        eng::log::info("Level: authored portal -- no generated one");
        return;
    }
    const std::string kitMeshDir = game::assetDir("meshes/kit");
    int built = 0;
    const auto view = registry.view<const game::Exit, const eng::ecs::Transform>();
    for (const entt::entity entity : view) {
        const game::Exit& exit = view.get<const game::Exit>(entity);
        const eng::ecs::Transform& transform =
            view.get<const eng::ecs::Transform>(entity);

        PortalPropStyle style;
        style.yawDegrees = exit.yawDegrees;
        style.kitMeshDir = kitMeshDir;
        // The same green the campaign's descending portal uses: two levels that
        // read differently because one was authored and one was generated is
        // exactly the inconsistency the editor exists to remove.
        style.lightColour = {0.22f, 1.05f, 0.10f};
        style.lightRange = 8.5f;
        if (!createPortalProp(renderer, transform.position, style).valid()) {
            eng::log::warn("playMap: portal at the exit could not be built");
            continue;
        }
        ++built;
    }
    if (built > 0)
        eng::log::info("Level: %d exit portal(s)", built);
}

class MapPlayApp : public eng::runtime::ProjectApp
{
public:
    using ProjectApp::ProjectApp;

protected:
    // This game's component table: the engine's plus its own markers. Passing
    // it here is what makes an Exit, an EnemySpawn and a ViewmodelRig readable
    // from the same .map raven_player would open with the engine's table and
    // simply skip.
    const eng::ecs::ComponentRegistry& components() const override
    {
        return mapio::coreRegistry();
    }

    eng::FpsGameConfig setup(eng::Engine& engine) override
    {
        eng::FpsGameConfig cfg = ProjectApp::setup(engine);
        cfg.physics = game::layer::physicsSetup();
        cfg.staticLayers = eng::layerMask(game::layer::Static);
        return cfg;
    }

    void onWorldAttached(eng::Engine& engine) override
    {
        mParticleCollider.emplace(physics(), eng::kAllLayers);
        engine.renderer().setParticleCollider(&*mParticleCollider);
    }

    // Authored Triggers become sensor colliders on this game's trigger layer.
    // What a Trigger means physically is the game's decision, which is why the
    // runtime asks rather than assumes.
    void onBeforeSync(entt::registry& reg) override
    {
        for (const entt::entity e : reg.view<Trigger>(entt::exclude<Collider>)) {
            const Trigger& t = reg.get<Trigger>(e);
            reg.emplace<Collider>(e, Collider{t.shape, t.size, layer::Trigger,
                                              /*sensor=*/true});
        }
    }

    void onSceneBuilt(eng::Engine& engine,
                      eng::runtime::SceneRuntime& scene) override
    {
        buildExitPortals(engine.renderer(), scene.registry());
    }

    // This game's authored marker wins over the engine's fallback.
    glm::vec3 playerStart(const eng::runtime::SceneRuntime& scene) const override
    {
        const entt::registry& reg = scene.registry();
        for (const entt::entity e : reg.view<const PlayerSpawn>()) {
            if (const auto* world = reg.try_get<eng::ecs::WorldTransform>(e))
                return glm::vec3(world->matrix[3]);
            if (const auto* t = reg.try_get<eng::ecs::Transform>(e))
                return t->position;
        }
        return ProjectApp::playerStart(scene);
    }

    void onWorldDetaching(eng::Engine& engine) override
    {
        engine.renderer().setParticleCollider(nullptr);
        mParticleCollider.reset();
    }

private:
    std::optional<game::JoltParticleCollider> mParticleCollider;
};

} // namespace

bool mapHasCamera(const std::string& mapPath)
{
    return eng::runtime::mapHasCamera(mapPath, mapio::coreRegistry());
}

int runMap(const std::string& mapPath, int argc, char** argv)
{
    // No project directory: a bare .map plays against the game's own content
    // and its own config, which is what `game <file.map>` has always meant.
    eng::runtime::Project project;
    project.name = "Raven";
    project.mainScene = mapPath;

    MapPlayApp app(std::move(project));
    app.setRecording(eng::GifRecorder::optionsFromArgs(argc, argv));
    return eng::runApplication(app, argc, argv);
}

} // namespace game
