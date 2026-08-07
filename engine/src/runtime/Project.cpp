#include <eng/runtime/Project.h>

#include <eng/Log.h>

// Error returns rather than throws, like every other TOML reader in the tree:
// a malformed project.toml is a diagnosis to report, not an exception to
// unwind through.
#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace eng::runtime {
namespace {

// The starter content. Written as literal text rather than assembled through
// the scene writer, because eng_runtime must not link the editor's authoring
// half -- and because a template somebody can read, copy and hand-edit is
// worth more here than one produced by code.
//
// Deliberately no camera. A scene that authors one is a *shot*: the runtime
// plays it through that camera and stands the player controller down (see
// ProjectApp). A new project should be something you can walk around in, so
// the starter scene leaves the camera to the player -- and says where the
// player stands with a `first_person` rig instead.
//
// That rig is not optional decoration. The scene contract's one hard rule is
// that a scene with nothing to look through loads, cooks, plays and shows
// nothing; a starter scene that tripped it would make every new project's
// first F5 a refusal. It is also what SceneRuntime::playerSpawn reads, so the
// same component answers "where is the player" for the runtime and "is this
// scene playable" for the cooker.
constexpr const char* kStarterScene = R"({
  "format": "raven-scene",
  "version": 2,
  "id": "scene.main",
  "entities": [
    {
      "id": "player",
      "name": "Player",
      "transform": {
        "position": [0.0, 1.7, 4.0]
      },
      "first_person": {
        "moveSpeed": 6.0,
        "baseFovDegrees": 70.0
      }
    },
    {
      "id": "key_light",
      "name": "Key Light",
      "transform": {
        "rotation_degrees": [-55.0, -30.0, 0.0]
      },
      "light": {
        "type": "directional",
        "colour": [1.0, 0.96, 0.88]
      }
    },
    {
      "id": "floor",
      "name": "Floor",
      "transform": {
        "position": [0.0, -0.1, 0.0]
      },
      "primitive": {
        "kind": "box",
        "size": [24.0, 0.2, 24.0]
      },
      "collider": {
        "shape": "box",
        "half_extents": [12.0, 0.1, 12.0]
      }
    },
    {
      "id": "cube",
      "name": "Scripted Cube",
      "transform": {
        "position": [0.0, 0.6, -5.0]
      },
      "primitive": {
        "kind": "box",
        "size": [1.2, 1.2, 1.2]
      },
      "scripts": [
        {
          "path": "scripts/cube.lua",
          "props": { "speed": 5.0 }
        }
      ]
    }
  ]
}
)";

// The starter script. Moves the cube the player is looking at, so pressing F5
// on a brand new project produces something that visibly responds to input --
// which is the only way to tell "the runtime works" from "the window opened".
constexpr const char* kStarterScript = R"(-- The cube in the starter scene.
--
-- Props:
--   speed  number  metres per second (default 5)
--
-- Action names, not keys: input.down takes an action from [bindings] in
-- project.toml, so remapping never touches a line of Lua.
local Cube = {}

function Cube:start()
  self.speed = self.props.speed or 5
end

function Cube:update(dt)
  local x, z = 0, 0
  if input.down("move_forward") then z = z - 1 end
  if input.down("move_back")    then z = z + 1 end
  if input.down("move_left")    then x = x - 1 end
  if input.down("move_right")   then x = x + 1 end
  if x == 0 and z == 0 then return end

  self.entity.position = self.entity.position + vec3(x, 0, z) * (self.speed * dt)
end

return Cube
)";

// A project's content manifest. One pack, rooted at the project itself, which
// is what mountProject overlays on the engine's own content.
//
// The resource directories are listed even though a new project's are empty:
// they are the ones the renderer scans by name, and a project that grows a
// texture should not also have to learn that this file exists.
constexpr const char* kStarterManifest = R"(# This project's content.
#
# Mounted OVER the engine's own pack, so anything here shadows the builtin of
# the same logical path -- which is how a project overrides a default material
# without editing anything else.
schema = 1

[[pack]]
id = "project"
dir = "."
resources = ["materials", "textures"]

[mounts]
game = ["project"]
)";

std::string starterConfig(const std::string& name)
{
    std::string out;
    out += "# " + name + "\n";
    out += R"(#
# This file is both the project's identity and its configuration: the engine
# reads [window] and [bindings] straight out of it, so there is one place to
# change what the window is called and what W does.

[project]
name = ")";
    out += name;
    out += R"("
# Relative to this file. The editor cooks it; the player plays the result.
main_scene = "scenes/main.scn"

[window]
title = ")";
    out += name;
    out += R"("
width = 1280
height = 720
vsync = false

# Which look the renderer starts in. RAVEN_RENDER_PRESET overrides it.
[render]
preset = "psx"

# Where scripts are resolved from and watched for hot reload.
[scripts]
root = "scripts"

# Action -> key. Scripts and the player controller both name actions, never
# keys, so this table is the only place a rebind happens.
#
# The locomotion block is not optional: eng::FpsController reads every one of
# these by name and treats an unbound one as fatal, so a project missing any of
# them would start and then die on the first frame that asked. Anything below
# the blank line is this project's to change freely.
[bindings]
move_forward = "W"
move_back = "S"
move_left = "A"
move_right = "D"
jump = "Space"
sprint = "Left Shift"
walk = "Left Alt"
crouch = "Left Ctrl"
slide = "C"

interact = "E"
quit = "Escape"
debug_ui = "F1"
dev_console = "`"
show_colliders = "F3"
show_perf = "F4"
)";
    return out;
}

bool writeFile(const fs::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        log::error("project: could not write %s", path.c_str());
        return false;
    }
    out << text;
    if (!out) {
        log::error("project: failed writing %s", path.c_str());
        return false;
    }
    return true;
}

} // namespace

fs::path Project::workDir() const
{
    return dir / ".raven";
}

fs::path Project::cookedMainScene() const
{
    // Same stem, cooked extension, under the work directory: the editor writes
    // it there and the player reads it from there, and neither has to be told
    // where by the other.
    fs::path scene(mainScene);
    return workDir() / "cooked" / (scene.stem().string() + ".map");
}

bool isProjectDir(const fs::path& dir)
{
    std::error_code ec;
    return fs::is_regular_file(dir / kProjectFile, ec);
}

bool loadProject(const fs::path& dir, Project& out)
{
    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(dir, ec);
    if (ec)
        canonical = dir.lexically_normal();

    const fs::path file = canonical / kProjectFile;
    if (!fs::is_regular_file(file, ec)) {
        log::error("project: no %s in %s", kProjectFile, canonical.c_str());
        return false;
    }

    // toml++ directly rather than eng::Config, which lives in eng_platform and
    // would drag SDL in behind it. Reading a project must work in a headless
    // tool -- the editor's project dialog, a future export CLI -- and none of
    // those want a window. The [bindings] table is deliberately not read here:
    // it belongs to the Config the engine loads from this same file, and a
    // second copy would be a second answer to "what is bound to W".
    toml::parse_result parsed = toml::parse_file(file.string());
    if (!parsed) {
        log::error("project: could not parse %s: %s", file.c_str(),
                   std::string(parsed.error().description()).c_str());
        return false;
    }
    const toml::table& root = parsed.table();

    const auto str = [&root](std::string_view table, std::string_view key,
                             std::string fallback) {
        if (const toml::value<std::string>* v =
                root[table][key].as_string())
            return v->get();
        return fallback;
    };
    const auto num = [&root](std::string_view table, std::string_view key,
                             int fallback) {
        if (const toml::value<int64_t>* v = root[table][key].as_integer())
            return int(v->get());
        if (const toml::value<double>* v = root[table][key].as_floating_point())
            return int(v->get());
        return fallback;
    };

    Project project;
    project.dir = canonical;
    // The directory's name is a better default than "Untitled": somebody who
    // omits the key almost certainly meant the thing the folder is called.
    project.name = str("project", "name", canonical.filename().string());
    project.mainScene = str("project", "main_scene", {});
    if (project.mainScene.empty()) {
        log::error("project: %s has no [project] main_scene", file.c_str());
        return false;
    }

    project.windowTitle = str("window", "title", project.name);
    project.windowWidth = num("window", "width", 1280);
    project.windowHeight = num("window", "height", 720);
    project.renderPreset = str("render", "preset", {});
    project.scriptRoot = str("scripts", "root", "scripts");

    out = std::move(project);
    return true;
}

bool createProject(const fs::path& dir, const std::string& name)
{
    std::error_code ec;
    if (isProjectDir(dir)) {
        log::error("project: %s is already a project", dir.c_str());
        return false;
    }

    for (const char* sub : {"", "scenes", "scripts", "materials", "textures",
                            "meshes"}) {
        fs::create_directories(dir / sub, ec);
        if (ec) {
            log::error("project: could not create %s: %s",
                       (dir / sub).c_str(), ec.message().c_str());
            return false;
        }
    }

    const std::string projectName = name.empty()
                                        ? dir.filename().string()
                                        : name;
    if (!writeFile(dir / kProjectFile, starterConfig(projectName)) ||
        !writeFile(dir / "assets.toml", kStarterManifest) ||
        !writeFile(dir / "scenes" / "main.scn", kStarterScene) ||
        !writeFile(dir / "scripts" / "cube.lua", kStarterScript))
        return false;

    log::info("project: created '%s' at %s", projectName.c_str(), dir.c_str());
    return true;
}

} // namespace eng::runtime
