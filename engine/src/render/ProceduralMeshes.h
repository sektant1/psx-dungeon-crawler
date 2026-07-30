#pragma once

#include "render/PrimitiveGeometry.h"

#include <string>

// Ogre upload stays private. Shape generation itself is pure CPU code in
// PrimitiveGeometry so headless tests inspect the exact production mesh.
namespace ProceduralMeshes {

void upload(const std::string& meshName,
            const eng::detail::PrimitiveGeometry& geometry);

} // namespace ProceduralMeshes
