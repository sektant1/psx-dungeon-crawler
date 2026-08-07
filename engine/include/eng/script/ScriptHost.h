#pragma once
#include <eng/script/ScriptConfig.h>

#include <entt/entity/fwd.hpp>
#include <glm/glm.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace eng {
class Audio;
class Input;
class Physics;
class DebugConsole;
}

namespace eng::ecs {
class World;
class ComponentRegistry;
}

namespace eng::script {

// What the runtime does that the script layer cannot do for itself.
//
// Loading a scene, quitting, and where the camera is are all answered by the
// application that owns the frame loop -- eng::runtime::ProjectApp, or a game
// that arranges its own. They arrive as callbacks rather than as a pointer to
// that application: the host must not be able to reach into the runtime, and
// the runtime is above this library and cannot be named from inside it.
//
// Every field is optional. An unset one binds a call that logs "not available
// in this runtime" and does nothing, which is friendlier than a nil global -- a
// script calling game.load_scene under a headless test should say so, not die
// on "attempt to call a nil value".
struct RuntimeHooks {
    // Logical scene path, as authored ("scenes/level2.scn"). The runtime is
    // what knows where its cooked form lives, and what deferring the switch to
    // a frame boundary means.
    std::function<void(const std::string&)> loadScene;
    std::function<void()> quit;
    // Merge a cooked scene into the world at a position; returns the group that
    // despawns it, or 0 on failure. The runtime half of scene instancing: the
    // same .scn an author places is what a script spawns.
    std::function<uint32_t(const std::string&, const glm::vec3&)> spawnScene;
    // Destroy everything in a group -- what spawnScene handed back.
    std::function<void(uint32_t)> despawn;
    std::function<glm::vec3()> cameraPosition;
    std::function<glm::vec3()> cameraForward;
    // Game-time controls, over eng::Clock -- so slowing time from Lua slows
    // every timer and every fixed step with it, rather than only what the
    // calling script remembers to scale.
    std::function<double()> elapsed;
    std::function<void(float)> setTimeScale;
    std::function<float()> timeScale;
};

// --- application modules ----------------------------------------------------
//
// How a game publishes its *own* vocabulary to Lua without this library
// learning any of it.
//
// RuntimeHooks works because loading a scene and asking where the camera is are
// things every runtime has. A game's gameplay surface is not like that: the
// dungeon crawler wants `rpg.give`, `rpg.standing` and `player.health`, and
// none of those words belong in an engine library that a different game links.
// The alternative -- a `GameplayHooks` struct with a field per verb -- would put
// quests and raid phases in this header, which is precisely the coupling the
// project layering exists to prevent (see docs/projects.md: raven_player links
// no game code, and a `player_purity` test enforces it).
//
// So the seam is generic: a table name, and named callbacks over a small value
// type. The game supplies the words. The cost is that arity and types are
// checked at call time rather than at compile time -- a script calling
// `rpg.give("potion")` when two arguments are required gets a Lua error naming
// the function, which is the same failure mode a misspelled component name
// already has here.
using ScriptValue =
    std::variant<std::monostate, bool, double, std::string, glm::vec3>;
using ScriptArgs = std::vector<ScriptValue>;
// Returning monostate is how a void function reports; the binding turns it into
// Lua nil.
using ScriptFn = std::function<ScriptValue(const ScriptArgs&)>;

struct ScriptModule {
    // The global table the functions land in. Created if absent, extended if
    // the name is already a table, so two calls can build one module.
    std::string table;
    std::vector<std::pair<std::string, ScriptFn>> functions;
};

// What a native event carries into Lua's `on_event(name, data)`.
//
// Deliberately the shape of game::rpg::GameEvent -- a subject id and a number --
// because that is what a typed gameplay event actually is once the enum has
// been turned into a name. A richer payload would need a table marshaller here
// and would tempt callers to push structures Lua then has to validate.
struct EventData {
    std::string subject;
    double value = 0.0;
};

// Reading a module call's arguments.
//
// Public because the application writing the callbacks is the one that needs
// them, and it lives outside this library. Out-of-range and wrong-typed
// arguments both yield the fallback: a dynamically bound function has no arity,
// so "absent" and "not what I wanted" are the same case at every call site, and
// distinguishing them would put a type switch in each one.
std::string argString(const ScriptArgs&, std::size_t index,
                      const std::string& fallback = {});
double argNumber(const ScriptArgs&, std::size_t index, double fallback = 0.0);
bool argBool(const ScriptArgs&, std::size_t index, bool fallback = false);
glm::vec3 argVec3(const ScriptArgs&, std::size_t index,
                  glm::vec3 fallback = glm::vec3(0.0f));

// Owns the Lua state, the loaded-chunk cache and the live script instances for
// one World.
//
// Not an eng::System. A System's contract is a single update(dt), and this
// host's whole point is that its callbacks land at three different places in
// the frame -- fixed_update before a physics step, contacts after it, update
// with the rest of presentation. A generic update(dt) would hide the one thing
// a reader needs to know about it.
//
// PIMPL'd because sol2 is template-heavy: <sol/sol.hpp> stays behind this
// header so including it does not cost every consumer a second of compile time.
class ScriptHost
{
public:
    // `world` and `registry` must both outlive the host. The registry is what
    // the reflection bindings walk, so an application that registers its own
    // components gets them in Lua for free -- which is the whole reason the
    // generic component path exists.
    ScriptHost(ecs::World& world, const ScriptConfig& config,
               const ecs::ComponentRegistry& registry);
    ~ScriptHost();
    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    // --- optional subsystems ---------------------------------------------
    // Both references must outlive the host. A host given neither still runs
    // every script that only touches the World -- which is what makes the
    // headless tests real, and lets a combat sim run scripted behaviour with
    // no window and no physics world.
    void bindInput(Input& input);
    void bindPhysics(Physics& physics);
    // `sound.play/play_at/loop/bus_volume`.
    void bindAudio(Audio& audio);
    // `game.*` and `camera.*`. See RuntimeHooks above; an unset hook binds a
    // call that logs and does nothing rather than leaving a nil global.
    void bindRuntime(const RuntimeHooks& hooks);
    // `save.*`, persisted to `path`. The runtime chooses where that is -- for a
    // project it is inside the project's own working directory, so two games on
    // one machine cannot overwrite each other's saves.
    void bindSave(const std::string& path);

    // --- frame -----------------------------------------------------------
    // Creates instances for entities whose Scripts have none, runs start() on
    // any that have not started, then update(dt) on the rest.
    //
    // Call once per frame after gameplay has mutated components and BEFORE
    // World::sync() -- the same slot tickComponentSystems() occupies.
    void tick(float dt);

    // Runs fixed_update(dt) on every started instance.
    //
    // Defined as "immediately before a physics step", not "on the fixed clock".
    // That keeps the contract true in a mode with no fixed loop -- MapPlay steps
    // physics from onPresent -- where the caller simply calls this first.
    void fixedTick(float dt);

    // Dispatches on_collision / on_trigger for the contacts the last physics
    // step produced. Call immediately AFTER Physics::update(), before tick(),
    // so a script reacts in the same frame the collision happened. A no-op
    // unless bindPhysics() was called.
    //
    // Collider::sensor decides which callback fires: a sensor body reports
    // through on_trigger, a solid one through on_collision. That is a component
    // read, not a second mechanism, so a trigger volume and a wall are the same
    // kind of object with one flag between them.
    void drainContacts();

    // Publish a table of named callbacks (see ScriptModule). Call before the
    // first tick; a module bound after a script's start() has already run is
    // nil for the line that wanted it, which is a confusing way to fail.
    void bindModule(const ScriptModule& module);

    // --- messaging -------------------------------------------------------
    // Delivers on_event(name, nil) to every live instance. The C++ side of what
    // Lua reaches through event.broadcast, for a game that wants to announce
    // something from native code -- a level transition, a boss phase.
    void broadcast(const std::string& name);
    // The same, carrying a payload: `on_event(name, { subject = ..., value =
    // ... })`. This is what turns a typed native event stream into something a
    // script can act on -- "an enemy died" is not useful without knowing which.
    void broadcast(const std::string& name, const EventData& data);

    // --- errors ----------------------------------------------------------
    // Un-quarantines every instance that errored. Returns how many. Called by
    // the console and by a successful hot reload.
    std::size_t revive();

    // Whether this entity's instance of `path` is currently quarantined.
    bool isQuarantined(entt::entity e, const std::string& path) const;

    // --- hot reload ------------------------------------------------------
    // Re-runs `path`'s chunk and swaps the class table under every live
    // instance of it. Instance state -- everything on self -- survives, start()
    // is NOT re-run, on_reload() fires if the class defines it, and any
    // quarantined instance of that script is revived.
    //
    // Returns false and changes nothing when the new source fails to load: a
    // half-typed save must not kill a running level. An empty path reloads
    // every script currently in use.
    bool reload(const std::string& path = {});

    // Polls the script root for changes and reloads what moved. A no-op unless
    // ScriptConfig::hotReload. Call once per frame.
    void pollReload();

    // --- console ---------------------------------------------------------
    // Evaluates one line in the script state, through the same traceback
    // handler every callback uses. Returns false on failure; `out` carries the
    // result or the error message either way.
    bool executeConsole(const std::string& line, std::string& out);

    // Logs every live instance: entity, path, and whether it is quarantined.
    void listInstances() const;

    // --- test and tooling seams ------------------------------------------
    std::size_t instanceCount() const;
    std::size_t timerCount() const;
    bool luaGlobalBool(const char* name) const;
    double luaGlobalNumber(const char* name) const;
    std::string luaGlobalString(const char* name) const;
    bool luaGlobalNil(const char* name) const;
    void luaSetGlobalNil(const char* name);
    // Publishes entity handles as a 1-based Lua array. Exists so a test can
    // hand the VM entities it built in C++ before world.spawn is bound.
    void luaSetGlobalEntityArray(const char* name,
                                 const std::vector<entt::entity>& entities);

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

// Registers `lua`, `script.list`, `script.reload` and `script.revive`.
//
// A free function taking both rather than a method on either: gameplay must not
// depend on ImGui, and DebugConsole is a debug-layer type. This is the one
// place that knows about both, and a build without a console simply does not
// link it.
void registerScriptCommands(eng::DebugConsole& console, ScriptHost& host);

} // namespace eng::script
