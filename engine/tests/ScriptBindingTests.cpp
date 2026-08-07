#include <eng/ecs/ComponentRegistry.h>
#include <eng/ecs/World.h>
#include <eng/ecs/components/Dirty.h>
#include <eng/ecs/components/Name.h>
#include <eng/ecs/components/Scripts.h>
#include <eng/ecs/components/Spin.h>
#include <eng/ecs/components/Transform.h>
#include <eng/ecs/components/WorldTransform.h>
#include <eng/script/ScriptConfig.h>
#include <eng/script/ScriptHost.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace eng;
using namespace eng::ecs;
using namespace eng::script;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "ScriptBindingTests: " << m << '\n'; std::exit(1); }
}

static std::filesystem::path gDir;

static std::string writeScript(const std::string& name, const std::string& body)
{
    std::filesystem::create_directories(gDir);
    const std::filesystem::path file = gDir / name;
    std::ofstream(file) << body;
    return file.string();
}

static entt::entity scripted(World& w, const std::string& name,
                             const std::string& path)
{
    const entt::entity e = w.create(name);
    w.registry().get_or_emplace<Scripts>(e).items.push_back({path, {}, true});
    return e;
}

int main()
{
    gDir = std::filesystem::temp_directory_path() / "eng_script_binding_tests";
    std::filesystem::remove_all(gDir);

    // --- vec3 arithmetic ---------------------------------------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = writeScript(
            "vec.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  local a = vec3(1, 2, 3)\n"
            "  local b = vec3(0, 1, 0)\n"
            "  sum_y = (a + b).y\n"
            "  diff_x = (a - b).x\n"
            "  scaled = (a * 2).z\n"
            "  len = vec3(3, 4, 0):length()\n"
            "  norm = vec3(0, 5, 0):normalized().y\n"
            "  zero_norm = vec3(0, 0, 0):normalized().x\n"
            "  dotted = a:dot(b)\n"
            "  crossed = vec3(1, 0, 0):cross(vec3(0, 1, 0)).z\n"
            "end\n"
            "return M\n");
        scripted(world, "v", path);
        host.tick(0.016f);
        require(host.luaGlobalNumber("sum_y") == 3.0, "vec3 addition");
        require(host.luaGlobalNumber("diff_x") == 1.0, "vec3 subtraction");
        require(host.luaGlobalNumber("scaled") == 6.0, "vec3 scalar multiply");
        require(host.luaGlobalNumber("len") == 5.0, "vec3 length");
        require(host.luaGlobalNumber("norm") == 1.0, "vec3 normalized");
        require(host.luaGlobalNumber("zero_norm") == 0.0,
                "normalising a zero vector yields zero, not NaN -- a NaN here "
                "propagates into a transform and puts the entity nowhere");
        require(host.luaGlobalNumber("dotted") == 2.0, "vec3 dot");
        require(host.luaGlobalNumber("crossed") == 1.0, "vec3 cross");
    }

    // --- self.entity reads and writes the local Transform ------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = writeScript(
            "move.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  read_y = self.entity.position.y\n"
            "  self.entity.position = vec3(1, 5, 2)\n"
            "  who = self.entity.name\n"
            "  ok = self.entity.valid\n"
            "end\n"
            "return M\n");
        const entt::entity e = scripted(world, "mover", path);
        world.setLocalTransform(e, Transform{glm::vec3(0.0f, 7.0f, 0.0f)});
        world.updateWorldTransforms(); // clears Dirty, so the next write is ours
        host.tick(0.016f);

        require(host.luaGlobalNumber("read_y") == 7.0,
                "position reads the authored local transform");
        require(host.luaGlobalString("who") == "mover", "name reads Name");
        require(host.luaGlobalBool("ok"), "a live entity is valid");
        require(world.registry().get<Transform>(e).position.y == 5.0f,
                "writing position writes the local Transform");
        require(world.registry().all_of<Dirty>(e),
                "and marks the subtree dirty -- a write that bypassed "
                "setLocalTransform would draw at the old pose until something "
                "unrelated happened to move the entity");
    }

    // --- rotation is Euler degrees, and sets the right orientation ---------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = writeScript(
            "pose.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  self.entity.rotation = vec3(0, 120, 0)\n"
            "  self.entity.scale = vec3(2, 2, 2)\n"
            "  read_back = self.entity.rotation.y\n"
            "end\n"
            "return M\n");
        const entt::entity e = scripted(world, "poser", path);
        host.tick(0.016f);

        // The contract is the ORIENTATION, not the triple. Euler angles are
        // not unique: glm reads 120 degrees of yaw back as (180, 60, 180),
        // which is the same rotation spelled differently. Asserting the
        // quaternion is asserting what the entity actually does; asserting the
        // triple would be asserting a glm implementation detail.
        const glm::quat expected(glm::radians(glm::vec3(0.0f, 120.0f, 0.0f)));
        const glm::quat got = world.registry().get<Transform>(e).rotation;
        require(std::abs(glm::dot(expected, got)) > 0.9999f,
                "setting rotation in degrees produces that orientation");
        require(world.registry().get<Transform>(e).scale.x == 2.0f,
                "scale writes through");

        // And the round trip is exact where Euler angles are unambiguous, which
        // is the case a script author actually hits.
        World w2;
        ScriptHost h2(w2, ScriptConfig{}, engineRegistry());
        scripted(w2, "poser2", writeScript("pose45.lua",
                                           "local M = {}\n"
                                           "function M:start()\n"
                                           "  self.entity.rotation = vec3(0, 45, 0)\n"
                                           "  read_back = self.entity.rotation.y\n"
                                           "end\n"
                                           "return M\n"));
        h2.tick(0.016f);
        require(std::abs(h2.luaGlobalNumber("read_back") - 45.0) < 0.01,
                "an unambiguous angle round-trips exactly");
    }

    // --- world_position is derived and read-only ---------------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = writeScript(
            "wp.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  wp = self.entity.world_position.y\n"
            "  local ok = pcall(function()\n"
            "    self.entity.world_position = vec3(0, 0, 0)\n"
            "  end)\n"
            "  refused = not ok\n"
            "end\n"
            "return M\n");
        const entt::entity parent = world.create("rig");
        world.setLocalTransform(parent, Transform{glm::vec3(0.0f, 10.0f, 0.0f)});
        const entt::entity e = scripted(world, "child", path);
        world.setParent(e, parent);
        world.setLocalTransform(e, Transform{glm::vec3(0.0f, 2.0f, 0.0f)});
        world.updateWorldTransforms();
        host.tick(0.016f);

        require(std::abs(host.luaGlobalNumber("wp") - 12.0) < 1e-4,
                "world_position is the composed pose, not the local one");
        require(host.luaGlobalBool("refused"),
                "assigning it is an error -- WorldTransform is derived, and a "
                "silent write would be overwritten on the next resolve");
    }

    // --- a destroyed entity reports invalid rather than crashing -----------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string hold = writeScript(
            "hold.lua",
            "local M = {}\n"
            "function M:start() held = self.entity end\n"
            "return M\n");
        const entt::entity e = scripted(world, "temp", hold);
        host.tick(0.016f);
        world.destroyHierarchy(e);

        const std::string probe = writeScript(
            "probe.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  stale_valid = held.valid\n"
            "  stale_name = held.name\n"
            "  stale_pos = held.position.y\n"
            "end\n"
            "return M\n");
        scripted(world, "prober", probe);
        host.tick(0.016f);
        require(!host.luaGlobalBool("stale_valid"),
                "a handle to a destroyed entity reports invalid");
        require(host.luaGlobalString("stale_name").empty(),
                "and reading through it yields a default, not a crash");
        require(host.luaGlobalNumber("stale_pos") == 0.0,
                "including its transform");
    }

    // --- the proxy reads and writes any reflected component ----------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = writeScript(
            "reflect.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  has_before = self.entity:has('Spin')\n"
            "  self.entity:add('Spin')\n"
            "  has_after = self.entity:has('Spin')\n"
            "  local s = self.entity:get('Spin')\n"
            "  read_default = s.degrees_per_second\n"
            "  s.degrees_per_second = 45\n"
            "  after_field_write = self.entity:get('Spin').degrees_per_second\n"
            "  self.entity:set('Spin', { degrees_per_second = 180 })\n"
            "  missing = self.entity:get('Orbit')\n"
            "end\n"
            "return M\n");
        const entt::entity e = scripted(world, "spinner", path);
        host.tick(0.016f);

        require(!host.luaGlobalBool("has_before"), "has() is false before add");
        require(host.luaGlobalBool("has_after"), "add() emplaces the component");
        require(host.luaGlobalNumber("read_default") == 90.0,
                "a field reads the component's own default");
        require(host.luaGlobalNumber("after_field_write") == 45.0,
                "assigning a field writes through to the live component");
        require(world.registry().get<Spin>(e).degreesPerSecond == 180.0f,
                "set() with a table writes named fields");
        require(host.luaGlobalNil("missing"),
                "get() on an absent component is nil, not an error -- a script "
                "should be able to probe");
    }

    // --- vec3 fields go through the proxy too ------------------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = writeScript(
            "reflect_vec.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  self.entity:set('Spin', { axis = vec3(1, 0, 0) })\n"
            "  axis_x = self.entity:get('Spin').axis.x\n"
            "end\n"
            "return M\n");
        const entt::entity e = scripted(world, "axis", path);
        host.tick(0.016f);
        require(host.luaGlobalNumber("axis_x") == 1.0, "vec3 field round-trips");
        require(world.registry().get<Spin>(e).axis.x == 1.0f,
                "and lands on the component");
    }

    // --- remove, and the proxy after it ------------------------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = writeScript(
            "remove.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  self.entity:add('Spin')\n"
            "  held = self.entity:get('Spin')\n"
            "  self.entity:remove('Spin')\n"
            "  gone = self.entity:has('Spin')\n"
            "  after = held.degrees_per_second\n"
            "end\n"
            "return M\n");
        scripted(world, "r", path);
        host.tick(0.016f);
        require(!host.luaGlobalBool("gone"), "remove() removes it");
        require(host.luaGlobalNumber("after") == 0.0,
                "a proxy to a removed component reads a default rather than "
                "through a dangling pointer");
    }

    // --- THE invalidation case: a proxy held across a pool-moving emplace ---
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = writeScript(
            "invalidate.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  self.entity:add('Spin')\n"
            "  local s = self.entity:get('Spin')\n"
            "  s.degrees_per_second = 10\n"
            "  -- Emplacing Spin on 512 other entities reallocates the pool. A\n"
            "  -- proxy caching a component pointer is now dangling.\n"
            "  for i = 1, 512 do fillers[i]:add('Spin') end\n"
            "  s.degrees_per_second = 20\n"
            "  readback = s.degrees_per_second\n"
            "end\n"
            "return M\n");
        const entt::entity e = scripted(world, "survivor", path);

        // Built from C++ rather than Lua: world.spawn does not exist yet, and
        // this case is about the proxy, not about spawning.
        host.luaSetGlobalEntityArray("fillers", [&] {
            std::vector<entt::entity> made;
            made.reserve(512);
            for (int i = 0; i < 512; ++i) made.push_back(world.create("filler"));
            return made;
        }());

        host.tick(0.016f);
        require(host.luaGlobalNumber("readback") == 20.0,
                "the proxy re-resolved after the pool moved");
        require(world.registry().get<Spin>(e).degreesPerSecond == 20.0f,
                "and it wrote the RIGHT entity's component, not whatever "
                "occupies the address the old one used to have");
    }

    // --- an unknown component name is an error, not a silent no-op ---------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = writeScript(
            "typo.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  local ok = pcall(function() self.entity:add('Spinn') end)\n"
            "  refused = not ok\n"
            "end\n"
            "return M\n");
        scripted(world, "t", path);
        host.tick(0.016f);
        require(host.luaGlobalBool("refused"),
                "a misspelled component name fails loudly -- silently doing "
                "nothing is how a typo becomes an afternoon");
    }

    // --- props arrive typed, before start ----------------------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = writeScript(
            "props.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  p_bool = self.props.open\n"
            "  p_num = self.props.speed\n"
            "  p_str = self.props.label\n"
            "  p_vec = self.props.tint.y\n"
            "  p_ent_name = self.props.target.name\n"
            "  p_missing = self.props.nope\n"
            "end\n"
            "return M\n");
        world.create("lever_a");
        const entt::entity e = world.create("door");
        auto& s = world.registry().get_or_emplace<Scripts>(e);
        ScriptRef ref;
        ref.path = path;
        ref.props.push_back({"open", ScriptProp::Type::Bool, true, 0.0f, {}, ""});
        ref.props.push_back(
            {"speed", ScriptProp::Type::Number, false, 2.5f, {}, ""});
        ref.props.push_back({"label", ScriptProp::Type::String, false, 0.0f, {},
                             "north"});
        ref.props.push_back({"tint", ScriptProp::Type::Vec3, false, 0.0f,
                             glm::vec3(0.0f, 0.5f, 0.0f), ""});
        ref.props.push_back({"target", ScriptProp::Type::Entity, false, 0.0f, {},
                             "lever_a"});
        s.items.push_back(ref);

        host.tick(0.016f);
        require(host.luaGlobalBool("p_bool"), "bool prop");
        require(host.luaGlobalNumber("p_num") == 2.5, "number prop");
        require(host.luaGlobalString("p_str") == "north", "string prop");
        require(host.luaGlobalNumber("p_vec") == 0.5, "vec3 prop");
        require(host.luaGlobalString("p_ent_name") == "lever_a",
                "an Entity prop arrives already resolved to a handle");
        require(host.luaGlobalNil("p_missing"),
                "an unauthored prop is nil, not an error");
    }

    // --- an Entity prop naming nothing is nil, not a crash -----------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = writeScript(
            "dangling.lua",
            "local M = {}\n"
            "function M:start() dangled = (self.props.target == nil) end\n"
            "return M\n");
        const entt::entity e = world.create("orphan");
        auto& s = world.registry().get_or_emplace<Scripts>(e);
        ScriptRef ref;
        ref.path = path;
        ref.props.push_back({"target", ScriptProp::Type::Entity, false, 0.0f, {},
                             "no_such_entity"});
        s.items.push_back(ref);
        host.tick(0.016f);
        require(host.luaGlobalBool("dangled"),
                "an unresolvable Entity prop is nil -- a level may legitimately "
                "ship without the collaborator");
    }

    // --- world.spawn / find / destroy --------------------------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = writeScript(
            "worldapi.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  local made = world.spawn('spawned')\n"
            "  made.position = vec3(1, 1, 1)\n"
            "  spawned_ok = made.valid\n"
            "  found_ok = world.find('spawned').valid\n"
            "  missing_nil = (world.find('nothing_here') == nil)\n"
            "  world.destroy(made)\n"
            "  -- Still valid THIS frame: destroys are queued, so the loop we\n"
            "  -- are inside cannot have its views invalidated under it.\n"
            "  immediate = made.valid\n"
            "end\n"
            "return M\n");
        scripted(world, "spawner", path);
        host.tick(0.016f);
        require(host.luaGlobalBool("spawned_ok"), "spawn returns a live handle");
        require(host.luaGlobalBool("found_ok"), "find locates it by name");
        require(host.luaGlobalBool("missing_nil"), "find returns nil when absent");
        require(host.luaGlobalBool("immediate"),
                "destroy is deferred within the tick");

        const std::string probe = writeScript(
            "probe2.lua",
            "local M = {}\n"
            "function M:start() gone = (world.find('spawned') == nil) end\n"
            "return M\n");
        scripted(world, "probe", probe);
        host.tick(0.016f);
        require(host.luaGlobalBool("gone"),
                "and the queued destroy was flushed after the tick");
    }

    // --- set_parent composes transforms ------------------------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string path = writeScript(
            "parent.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  self.entity:set_parent(world.find('anchor'))\n"
            "end\n"
            "return M\n");
        const entt::entity anchor = world.create("anchor");
        world.setLocalTransform(anchor, Transform{glm::vec3(4.0f, 0.0f, 0.0f)});
        const entt::entity e = scripted(world, "hanger", path);
        host.tick(0.016f);
        world.updateWorldTransforms();
        require(world.registry().get<WorldTransform>(e).matrix[3].x == 4.0f,
                "set_parent puts the entity in its parent's frame");
    }

    // --- events reach another entity's script ------------------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string listener = writeScript(
            "listener.lua",
            "local M = {}\n"
            "function M:on_event(name, data)\n"
            "  heard = name\n"
            "  payload = data and data.amount or 0\n"
            "end\n"
            "return M\n");
        const std::string sender = writeScript(
            "sender.lua",
            "local M = {}\n"
            "function M:start()\n"
            "  world.find('ear'):send('open', { amount = 7 })\n"
            "end\n"
            "return M\n");
        scripted(world, "ear", listener);
        scripted(world, "mouth", sender);
        host.tick(0.016f);
        require(host.luaGlobalString("heard") == "open",
                "send reaches the target's on_event");
        require(host.luaGlobalNumber("payload") == 7.0,
                "and carries its data table");
    }

    // --- broadcast reaches everything --------------------------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string ear = writeScript(
            "ear2.lua",
            "local M = {}\n"
            "function M:on_event(name, data) count = (count or 0) + 1 end\n"
            "return M\n");
        scripted(world, "a", ear);
        scripted(world, "b", ear);
        host.tick(0.016f);
        host.broadcast("wake");
        require(host.luaGlobalNumber("count") == 2.0,
                "a broadcast reaches every live instance");
    }

    // --- e:script() reaches another entity's instance ----------------------
    {
        World world;
        ScriptHost host(world, ScriptConfig{}, engineRegistry());
        const std::string doorPath = writeScript(
            "door_api.lua",
            "local M = {}\n"
            "function M:start() self.open = false end\n"
            "function M:toggle() self.open = not self.open; door_open = self.open end\n"
            "return M\n");
        const std::string leverPath = writeScript(
            "lever_api.lua",
            "local M = {}\n"
            "function M:update(dt)\n"
            "  if pulled then return end\n"
            "  local d = world.find('door'):script('" + doorPath + "')\n"
            "  if d then d:toggle(); pulled = true end\n"
            "end\n"
            "return M\n");
        scripted(world, "door", doorPath);
        scripted(world, "lever", leverPath);
        host.tick(0.016f);
        require(host.luaGlobalBool("door_open"),
                "a lever can call a method on the door's instance directly, "
                "without routing every interaction through an event");
    }

    std::cout << "ScriptBindingTests: ok\n";
    return 0;
}
