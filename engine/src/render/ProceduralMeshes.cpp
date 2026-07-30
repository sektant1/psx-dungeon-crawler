#include "ProceduralMeshes.h"

#include <Ogre.h>

namespace ProceduralMeshes {

void upload(const std::string& meshName,
            const eng::detail::PrimitiveGeometry& geometry)
{
    auto* object = new Ogre::ManualObject(meshName + "_mo");
    object->begin("BaseWhite", Ogre::RenderOperation::OT_TRIANGLE_LIST);
    for (const eng::detail::PrimitiveVertex& vertex : geometry.vertices) {
        object->position(vertex.position.x, vertex.position.y,
                         vertex.position.z);
        object->normal(vertex.normal.x, vertex.normal.y, vertex.normal.z);
        object->textureCoord(vertex.uv.x, vertex.uv.y);
        object->colour(vertex.colour.r, vertex.colour.g, vertex.colour.b,
                       vertex.colour.a);
    }
    for (size_t i = 0; i < geometry.indices.size(); i += 3)
        object->triangle(geometry.indices[i], geometry.indices[i + 1],
                         geometry.indices[i + 2]);
    object->end();
    object->convertToMesh(meshName);
    delete object;
}

} // namespace ProceduralMeshes
