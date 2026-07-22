#pragma once
#include <OgreMatrix4.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

// Minimal Wavefront OBJ loader -> Ogre::Mesh via ManualObject.
//
// Supports the vertex-colour extension used by tools/gltf_to_obj.py
// ("v x y z r g b a"). Every produced mesh carries an explicit white vertex
// colour stream when the file has none, so the GLSL `colour` attribute is
// always bound (matching Godot's COLOR default of 1,1,1,1).
//
// `bake` is multiplied into positions (normals get its inverse-transpose):
// used to bake the non-uniform spire transforms from crystal_mesh.tscn /
// crystal.gltf, which cannot be represented by Ogre's TRS scene nodes.
//
// v texture coordinates are flipped (1 - v), matching Godot's OBJ importer.
namespace ObjLoader {
void load(const std::string& filePath, const std::string& meshName,
          const Ogre::Matrix4& bake = Ogre::Matrix4::IDENTITY);

// CPU-side geometry read for physics/analysis (no Ogre mesh created). Fills
// triangulated positions + indices in the OBJ's own space, with the same
// `bake` transform applied as load(). Returns false if the file is unreadable.
// UVs/normals/colours are ignored. Polygons are fan-triangulated.
bool loadGeometry(const std::string& filePath,
                  std::vector<glm::vec3>& outVerts,
                  std::vector<uint32_t>& outIndices,
                  const Ogre::Matrix4& bake = Ogre::Matrix4::IDENTITY);
}
