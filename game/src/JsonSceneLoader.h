#pragma once

#include <eng/Handles.h>

#include <string>

namespace eng {
class Renderer;
class Physics;
}

// Minimal Godot-like scene JSON loader for editor/model scenes:
// {
//   "name": "Scene",
//   "default_floor": true,
//   "nodes": [{ "name": "Cube", "mesh": "...obj", "material": "...",
//               "position": [0,0,0], "rotation_degrees": [0,0,0],
//               "scale": [1,1,1], "children": [...] }]
// }
bool loadJsonScene(const std::string& path, eng::Renderer& r,
                   eng::Physics* physics, eng::NodeHandle parent,
                   const std::string& assetDir, std::string& error);
