#pragma once

#include <eng/Handles.h>

#include <string>

namespace eng {
class Renderer;
class Physics;
}
class DungeonMap;

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
                   DungeonMap* dungeon = nullptr);
