#include "script/bind/Bindings.h"

#include <eng/Log.h>

#include <string>

namespace eng::script {
namespace {

// An unset hook binds this rather than leaving the global nil.
//
// A script that calls game.load_scene under a host with no scenes -- a
// headless test, a combat sim -- should be told the runtime does not offer it,
// not die on "attempt to call a nil value". The message names the call, so the
// line that has to change is obvious.
void unavailable(const char* what)
{
    log::warn("Script: %s is not available in this runtime", what);
}

} // namespace

void bindRuntime(sol::state& lua, const RuntimeHooks& hooks)
{
    sol::table g = lua.create_named_table("game");

    // Deferred by the runtime, never immediate: a script calling this is
    // inside the host's dispatch loop, and tearing the world down there would
    // destroy the very instance that asked. ProjectApp acts on it at the top of
    // the next frame.
    if (hooks.loadScene) {
        const auto load = hooks.loadScene;
        g["load_scene"] = [load](const std::string& path) { load(path); };
    } else {
        g["load_scene"] = [](const std::string&) {
            unavailable("game.load_scene");
        };
    }

    if (hooks.quit) {
        const auto quit = hooks.quit;
        g["quit"] = [quit]() { quit(); };
    } else {
        g["quit"] = []() { unavailable("game.quit"); };
    }

    // Game time, not wall time: it carries the clock's scale and pause, which
    // is what makes a timestamp taken here comparable with a timer's countdown.
    if (hooks.elapsed) {
        const auto elapsed = hooks.elapsed;
        g["time"] = [elapsed]() { return elapsed(); };
    } else {
        g["time"] = []() { return 0.0; };
    }

    if (hooks.setTimeScale) {
        const auto set = hooks.setTimeScale;
        g["set_time_scale"] = [set](float scale) { set(scale); };
    } else {
        g["set_time_scale"] = [](float) { unavailable("game.set_time_scale"); };
    }
    if (hooks.timeScale) {
        const auto get = hooks.timeScale;
        g["time_scale"] = [get]() { return get(); };
    } else {
        g["time_scale"] = []() { return 1.0f; };
    }

    // Where the view is. Read-only on purpose: the camera belongs to whatever
    // is driving it -- the player controller, an authored shot, a screen rig --
    // and a script that could move it would be fighting that thing every frame.
    // What scripts actually need is to aim at the player and to know what is in
    // front of them, which these two answer.
    sol::table c = lua.create_named_table("camera");
    if (hooks.cameraPosition) {
        const auto pos = hooks.cameraPosition;
        c["position"] = [pos]() { return pos(); };
    } else {
        c["position"] = []() { return glm::vec3(0.0f); };
    }
    if (hooks.cameraForward) {
        const auto fwd = hooks.cameraForward;
        c["forward"] = [fwd]() { return fwd(); };
    } else {
        c["forward"] = []() { return glm::vec3(0.0f, 0.0f, -1.0f); };
    }
}

} // namespace eng::script
