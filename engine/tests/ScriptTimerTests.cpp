// timer.after / timer.every, and the scheduling rules they are meant to make
// unnecessary to get right by hand.
//
// Headless: a TimerSet needs a clock's worth of dt and nothing else, which is
// the point of it living in the host rather than in each script.

#include <eng/ecs/ComponentRegistry.h>
#include <eng/ecs/World.h>
#include <eng/ecs/components/Scripts.h>
#include <eng/script/ScriptHost.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace eng;
using namespace eng::script;

static void require(bool c, const char* m)
{
    if (!c) {
        std::cerr << "ScriptTimerTests: " << m << '\n';
        std::exit(1);
    }
}

namespace fs = std::filesystem;

static fs::path scriptDir()
{
    const fs::path dir = fs::temp_directory_path() / "raven_timer_tests";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

static std::string writeScript(const char* name, const char* body)
{
    const fs::path path = scriptDir() / name;
    std::ofstream out(path, std::ios::trunc);
    out << body;
    return path.string();
}

static void attach(ecs::World& world, entt::entity e, const std::string& path)
{
    ecs::Scripts s;
    s.items.push_back(ecs::ScriptRef{path, {}, true});
    world.registry().emplace<ecs::Scripts>(e, std::move(s));
}

// Fires once, at the right time, and does not fire again.
static void testAfter()
{
    const std::string path = writeScript("after.lua", R"(
local T = {}
function T:start()
  fired = 0
  timer.after(0.5, function() fired = fired + 1 end)
end
return T
)");

    ecs::World world;
    ScriptHost host(world, ScriptConfig{}, ecs::engineRegistry());
    attach(world, world.create("thing"), path);

    host.tick(0.1f); // start() runs, the timer is scheduled
    require(host.luaGlobalNumber("fired") == 0,
            "a timer scheduled this frame does not fire this frame");
    require(host.timerCount() == 1, "the timer is live");

    for (int i = 0; i < 4; ++i)
        host.tick(0.1f); // 0.5s total since scheduling
    require(host.luaGlobalNumber("fired") == 1, "it fires once, on time");

    for (int i = 0; i < 20; ++i)
        host.tick(0.1f);
    require(host.luaGlobalNumber("fired") == 1, "and never again");
    require(host.timerCount() == 0, "a spent one-shot retires itself");
}

// Repeats on its interval, and carries the overshoot so the rate does not
// depend on the frame rate.
static void testEvery()
{
    const std::string path = writeScript("every.lua", R"(
local T = {}
function T:start()
  ticks = 0
  handle = timer.every(0.25, function() ticks = ticks + 1 end)
end
return T
)");

    ecs::World world;
    ScriptHost host(world, ScriptConfig{}, ecs::engineRegistry());
    attach(world, world.create("thing"), path);

    host.tick(0.0f);
    // One second in steps that do not divide the interval evenly: a timer that
    // reset to the full interval on each firing would drift and produce three.
    for (int i = 0; i < 10; ++i)
        host.tick(0.1f);
    require(host.luaGlobalNumber("ticks") == 4,
            "four firings in one second at 0.25s, whatever the step size");
    require(host.timerCount() == 1, "a repeating timer stays live");
}

// Cancel, including from inside the callback -- the case a hand-rolled timer
// always gets wrong.
static void testCancel()
{
    const std::string path = writeScript("cancel.lua", R"(
local T = {}
function T:start()
  ticks = 0
  local id
  id = timer.every(0.1, function()
    ticks = ticks + 1
    if ticks == 3 then timer.cancel(id) end
  end)
  other = timer.after(10.0, function() end)
end
return T
)");

    ecs::World world;
    ScriptHost host(world, ScriptConfig{}, ecs::engineRegistry());
    attach(world, world.create("thing"), path);

    host.tick(0.0f);
    for (int i = 0; i < 20; ++i)
        host.tick(0.1f);
    require(host.luaGlobalNumber("ticks") == 3,
            "a callback can cancel the timer it is running on");
    require(host.timerCount() == 1, "and only that one -- the other is live");
}

// A callback that throws is reported and dropped, not fatal, and does not stop
// the timers around it.
static void testFailingCallback()
{
    const std::string path = writeScript("boom.lua", R"(
local T = {}
function T:start()
  survived = 0
  timer.after(0.1, function() error("boom") end)
  timer.after(0.2, function() survived = survived + 1 end)
end
return T
)");

    ecs::World world;
    ScriptHost host(world, ScriptConfig{}, ecs::engineRegistry());
    attach(world, world.create("thing"), path);

    host.tick(0.0f);
    for (int i = 0; i < 5; ++i)
        host.tick(0.1f);
    require(host.luaGlobalNumber("survived") == 1,
            "one bad callback does not stop the next timer");
}

// A timer outliving the entity that scheduled it is safe: the closure keeps its
// upvalues, and a LuaEntity re-checks validity before touching anything.
static void testOutlivesItsEntity()
{
    const std::string path = writeScript("outlive.lua", R"(
local T = {}
function T:start()
  after_destroy = "not yet"
  local me = self.entity
  timer.after(0.3, function()
    after_destroy = me.valid and "alive" or "gone"
  end)
  world.destroy(self.entity)
end
return T
)");

    ecs::World world;
    ScriptHost host(world, ScriptConfig{}, ecs::engineRegistry());
    attach(world, world.create("doomed"), path);

    host.tick(0.0f); // start() schedules, then destroys its own entity
    for (int i = 0; i < 5; ++i)
        host.tick(0.1f);
    require(host.luaGlobalString("after_destroy") == "gone",
            "the callback runs and sees a dead entity rather than crashing");
}

int main()
{
    testAfter();
    testEvery();
    testCancel();
    testFailingCallback();
    testOutlivesItsEntity();
    std::puts("ScriptTimerTests: ok");
    return 0;
}
