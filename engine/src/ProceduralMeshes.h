#pragma once
#include <string>

// Procedural meshes replacing Godot primitive meshes from world.tscn /
// shadow.tscn. Both emit an explicit white vertex-colour stream so the GLSL
// `colour` attribute is always bound.
namespace ProceduralMeshes {

// Godot BoxMesh: size 40x40x40, subdivide 25 (=> 26 segments per edge),
// flip_faces=true (normals point inward, winding reversed), UVs in Godot's
// 3x2 per-face atlas layout.
void createInteriorBox(const std::string& meshName, float size, int subdivide);

// Godot PlaneMesh: default 2x2 on XZ, facing +Y, UV 0..1.
void createPlane(const std::string& meshName, float size);
void createBeveledBox(const std::string& meshName, float bevel);
void createCone(const std::string& meshName, float radius, float height,
                int segments);

// Thick annulus and matching vertical membrane, centred on XY and facing Z.
void createPortalRing(const std::string& meshName, float outerRadius,
                      float innerRadius, float depth, int segments);
void createPortalDisc(const std::string& meshName, float radius, int segments);

} // namespace ProceduralMeshes
