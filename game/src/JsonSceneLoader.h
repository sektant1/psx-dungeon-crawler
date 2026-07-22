#pragma once

#include <eng/Handles.h>

#include <glm/glm.hpp>

#include <string>

namespace eng {
class Renderer;
class Physics;
}
class DungeonMap;

// How the editor should drive the Scene viewport camera for a loaded scene, so
// the preview matches how that scene's target runs:
//   Editor -- free-fly (RMB orbit + wheel dolly); the default.
//   Orbit  -- auto-rotating turntable around `target` (like the demo).
//   Fps    -- walk-through: WASD free-fly at eye height + mouse look (the game).
struct SceneCamera {
    enum class Mode { Editor, Orbit, Fps };
    Mode mode = Mode::Editor;
    glm::vec3 target{0.0f};        // orbit pivot
    float distance = 12.0f;        // orbit radius
    float orbitSpeed = 0.35f;      // rad/s (orbit)
    float orbitPitch = 0.35f;      // rad above horizon (orbit)
    glm::vec3 eye{0.0f, 1.6f, 6.0f}; // fps start eye position
    float yaw = 0.0f;              // fps start yaw (rad)
    float moveSpeed = 6.0f;        // fps m/s
    float fovDeg = 60.0f;
};

// Minimal Godot-like scene JSON loader for editor/model scenes:
// {
//   "name": "Scene",
//   "default_floor": true,
//   "nodes": [{ "name": "Cube", "mesh": "...obj", "material": "...",
//               "position": [0,0,0], "rotation_degrees": [0,0,0],
//               "scale": [1,1,1], "children": [...] }]
// }
//
// A scene may instead (or additionally) carry a `generator` directive that
// rebuilds a procedural scene 1:1 from the same code the game runs:
//   { "name": "Dungeon 1", "generator": { "type": "dungeon", "seed": 1 } }
// Generator dungeons need a caller-owned DungeonMap (it owns the tile/prop
// bodies + per-frame update state); pass it via `dungeon`. When a generator is
// present, `default_floor` defaults to false (the dungeon brings its own floor).
bool loadJsonScene(const std::string& path, eng::Renderer& r,
                   eng::Physics* physics, eng::NodeHandle parent,
                   const std::string& assetDir, std::string& error,
                   DungeonMap* dungeon = nullptr, SceneCamera* camera = nullptr);
