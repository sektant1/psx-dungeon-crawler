#include <eng/Primitive.h>
#include <eng/Renderer.h>

#include "render/PrimitiveGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "PrimitiveTests: " << message << '\n';
        std::exit(1);
    }
}

bool near(float actual, float expected)
{
    return std::fabs(actual - expected) < 0.0001f;
}

bool near(glm::vec3 actual, glm::vec3 expected)
{
    return near(actual.x, expected.x) &&
           near(actual.y, expected.y) &&
           near(actual.z, expected.z);
}

bool finite(glm::vec2 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}

bool finite(glm::vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

struct Bounds {
    glm::vec3 min{std::numeric_limits<float>::infinity()};
    glm::vec3 max{-std::numeric_limits<float>::infinity()};
};

Bounds boundsOf(const eng::detail::PrimitiveGeometry& geometry)
{
    Bounds bounds;
    for (const auto& vertex : geometry.vertices) {
        bounds.min = glm::min(bounds.min, vertex.position);
        bounds.max = glm::max(bounds.max, vertex.position);
    }
    return bounds;
}

void requireFiniteVertices(const eng::detail::PrimitiveGeometry& geometry)
{
    require(!geometry.vertices.empty(), "primitive emitted no vertices");
    require(!geometry.indices.empty(), "primitive emitted no triangles");
    require(geometry.indices.size() % 3 == 0,
            "primitive index count is not triangular");
    for (const auto& vertex : geometry.vertices) {
        require(finite(vertex.position), "primitive position is non-finite");
        require(finite(vertex.normal), "primitive normal is non-finite");
        require(finite(vertex.uv), "primitive UV is non-finite");
        require(glm::dot(vertex.normal, vertex.normal) > 0.99f,
                "primitive normal is not unit length");
    }
}

void requireOutwardWinding(const eng::detail::PrimitiveGeometry& geometry)
{
    size_t checked = 0;
    for (size_t i = 0; i < geometry.indices.size(); i += 3) {
        const auto& a = geometry.vertices[geometry.indices[i]];
        const auto& b = geometry.vertices[geometry.indices[i + 1]];
        const auto& c = geometry.vertices[geometry.indices[i + 2]];
        const glm::vec3 face =
            glm::cross(b.position - a.position, c.position - a.position);
        if (glm::dot(face, face) < 0.00000001f)
            continue;
        require(glm::dot(face, a.normal + b.normal + c.normal) > 0.0f,
                "primitive triangle winding opposes its vertex normals");
        ++checked;
    }
    require(checked > 0, "primitive emitted no nondegenerate triangles");
}

void test_mesh_validation_rejects_invalid_dimensions_and_tessellation()
{
    eng::PrimitiveMeshDesc desc;
    require(eng::validPrimitiveMeshDesc(desc),
            "default primitive mesh descriptor was rejected");

    desc.size.x = 0.0f;
    require(!eng::validPrimitiveMeshDesc(desc),
            "zero box size was accepted");

    desc = {};
    desc.radius = -0.1f;
    require(!eng::validPrimitiveMeshDesc(desc),
            "negative radius was accepted");

    desc = {};
    desc.height = std::numeric_limits<float>::infinity();
    require(!eng::validPrimitiveMeshDesc(desc),
            "non-finite height was accepted");

    desc = {};
    desc.rings = 2;
    require(!eng::validPrimitiveMeshDesc(desc),
            "unsafe sphere ring count was accepted");

    desc = {};
    desc.segments = 2;
    require(!eng::validPrimitiveMeshDesc(desc),
            "unsafe segment count was accepted");

    desc = {};
    desc.kind = eng::PrimitiveKind::BeveledBox;
    desc.bevel = 0.5f;
    require(!eng::validPrimitiveMeshDesc(desc),
            "bevel consuming the full half-extent was accepted");
}

void test_renderer_dispatch_selects_every_generator()
{
    using G = eng::detail::PrimitiveMeshGenerator;
    const struct {
        eng::PrimitiveKind kind;
        G expected;
    } cases[] = {
        {eng::PrimitiveKind::Box, G::Box},
        {eng::PrimitiveKind::BeveledBox, G::BeveledBox},
        {eng::PrimitiveKind::Sphere, G::Sphere},
        {eng::PrimitiveKind::Capsule, G::Capsule},
        {eng::PrimitiveKind::Cylinder, G::Cylinder},
        {eng::PrimitiveKind::Cone, G::Cone},
        {eng::PrimitiveKind::Plane, G::Plane},
        {eng::PrimitiveKind::Disc, G::Disc},
    };

    for (const auto& test : cases) {
        const auto generator = eng::detail::primitiveMeshGenerator(test.kind);
        require(generator && *generator == test.expected,
                "primitive kind selected the wrong mesh generator");
    }
    require(!eng::detail::primitiveMeshGenerator(
                 static_cast<eng::PrimitiveKind>(999)),
            "unknown primitive kind selected a generator");
}

void test_renderer_exposes_one_descriptor_driven_primitive_entry_point()
{
    using Method =
        eng::MeshHandle (eng::Renderer::*)(const eng::PrimitiveMeshDesc&);
    const Method method = &eng::Renderer::createPrimitiveMesh;
    require(method != nullptr,
            "renderer primitive entry point is missing");
}

eng::PrimitiveDesc collidable(eng::PrimitiveKind kind)
{
    eng::PrimitiveDesc desc;
    desc.mesh.kind = kind;
    desc.material = "Game/Test";
    desc.collision = true;
    desc.position = {2.0f, 3.0f, 4.0f};
    desc.orientation =
        glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    desc.scale = {-2.0f, 3.0f, -0.5f};
    desc.bodyLayer = eng::BodyLayer::Projectile;
    desc.dynamic = true;
    desc.sensor = true;
    desc.mass = 7.0f;
    desc.friction = 0.25f;
    desc.restitution = 0.75f;
    return desc;
}

void test_box_plane_and_sphere_collider_dimensions_follow_render_scale()
{
    eng::PrimitiveDesc box = collidable(eng::PrimitiveKind::Box);
    box.mesh.size = {2.0f, 4.0f, 6.0f};
    auto resolved = eng::resolvePrimitiveCollider(box);
    require(resolved && resolved->createsBody(),
            "box collider was not resolved");
    require(resolved->body.kind == eng::ShapeKind::Box,
            "box primitive did not map to box collision");
    require(near(resolved->body.halfExtents, {2.0f, 6.0f, 1.5f}),
            "box collider did not use mesh size and absolute node scale");

    eng::PrimitiveDesc plane = collidable(eng::PrimitiveKind::Plane);
    plane.mesh.size = {2.0f, 9.0f, 4.0f};
    plane.mesh.thickness = 0.2f;
    resolved = eng::resolvePrimitiveCollider(plane);
    require(resolved && resolved->body.kind == eng::ShapeKind::Box,
            "plane primitive did not map to thin box collision");
    require(near(resolved->body.halfExtents, {2.0f, 0.3f, 1.0f}),
            "plane collider did not use XZ size and scaled thickness");

    eng::PrimitiveDesc sphere = collidable(eng::PrimitiveKind::Sphere);
    sphere.mesh.radius = 0.5f;
    resolved = eng::resolvePrimitiveCollider(sphere);
    require(resolved && resolved->body.kind == eng::ShapeKind::Sphere,
            "sphere primitive did not map to sphere collision");
    require(near(resolved->body.radius, 1.5f),
            "sphere collider did not contain nonuniform render scale");
}

void test_round_primitive_collider_mapping_and_scaled_dimensions()
{
    const struct {
        eng::PrimitiveKind kind;
        eng::ShapeKind expected;
    } cases[] = {
        {eng::PrimitiveKind::Capsule, eng::ShapeKind::Capsule},
        {eng::PrimitiveKind::Cylinder, eng::ShapeKind::Cylinder},
        // A cone has no exact convex primitive in Physics; use a conservative
        // cylinder with the same rendered radius and height.
        {eng::PrimitiveKind::Cone, eng::ShapeKind::Cylinder},
        {eng::PrimitiveKind::Disc, eng::ShapeKind::Cylinder},
    };

    for (const auto& test : cases) {
        eng::PrimitiveDesc desc = collidable(test.kind);
        desc.mesh.radius = 0.5f;
        desc.mesh.height = 2.0f;
        desc.mesh.thickness = 0.2f;
        const auto resolved = eng::resolvePrimitiveCollider(desc);
        require(resolved && resolved->body.kind == test.expected,
                "round primitive selected the wrong collider kind");
        const float expectedRadius =
            test.kind == eng::PrimitiveKind::Capsule ? 1.5f : 1.0f;
        require(near(resolved->body.radius, expectedRadius),
                "round collider radius omitted radial scale");
        const float expectedHalfHeight =
            test.kind == eng::PrimitiveKind::Disc ? 0.3f : 3.0f;
        require(near(resolved->body.halfHeight, expectedHalfHeight),
                "round collider half-height omitted rendered height/thickness");
    }
}

void test_parent_transform_drives_conservative_capsule_collider()
{
    eng::PrimitiveDesc desc = collidable(eng::PrimitiveKind::Capsule);
    desc.position = {1.0f, 2.0f, 3.0f};
    desc.scale = {-2.0f, 1.0f, -0.5f};
    desc.mesh.radius = 0.5f;
    desc.mesh.height = 2.0f;
    const eng::NodeTransform parent{
        {10.0f, 20.0f, 30.0f},
        glm::angleAxis(glm::radians(90.0f),
                       glm::vec3(0.0f, 0.0f, 1.0f)),
        {1.0f, 4.0f, 1.0f},
    };

    const auto resolved =
        eng::resolvePrimitiveCollider(desc, parent);

    require(resolved && resolved->body.kind == eng::ShapeKind::Capsule,
            "parented capsule collider was rejected");
    require(near(resolved->body.position, {2.0f, 21.0f, 33.0f}),
            "parent transform did not compose capsule position");
    require(near(resolved->body.radius, 2.0f),
            "capsule radius did not conservatively include Y scale");
    require(near(resolved->body.halfHeight, 4.0f),
            "capsule straight half-height omitted composed Y scale");
}

void test_sphere_geometry_extents_finite_attributes_and_outward_winding()
{
    eng::PrimitiveMeshDesc desc;
    desc.kind = eng::PrimitiveKind::Sphere;
    desc.radius = 0.75f;
    desc.rings = 8;
    desc.segments = 12;
    const auto geometry = eng::detail::buildPrimitiveGeometry(desc);

    require(geometry.has_value(), "valid sphere geometry was rejected");
    requireFiniteVertices(*geometry);
    requireOutwardWinding(*geometry);
    const Bounds bounds = boundsOf(*geometry);
    require(near(bounds.min, glm::vec3(-0.75f)) &&
                near(bounds.max, glm::vec3(0.75f)),
            "sphere geometry does not match descriptor radius");
}

void test_every_outward_primitive_has_finite_aligned_geometry()
{
    const eng::PrimitiveKind kinds[] = {
        eng::PrimitiveKind::Box,
        eng::PrimitiveKind::BeveledBox,
        eng::PrimitiveKind::Sphere,
        eng::PrimitiveKind::Capsule,
        eng::PrimitiveKind::Cylinder,
        eng::PrimitiveKind::Cone,
        eng::PrimitiveKind::Plane,
        eng::PrimitiveKind::Disc,
    };
    for (eng::PrimitiveKind kind : kinds) {
        eng::PrimitiveMeshDesc desc;
        desc.kind = kind;
        const auto geometry =
            eng::detail::buildPrimitiveGeometry(desc);
        require(geometry.has_value(),
                "valid primitive geometry was rejected");
        requireFiniteVertices(*geometry);
        requireOutwardWinding(*geometry);
    }
}

void test_capsule_geometry_has_two_hemispheres_and_a_straight_strip()
{
    eng::PrimitiveMeshDesc desc;
    desc.kind = eng::PrimitiveKind::Capsule;
    desc.radius = 0.5f;
    desc.height = 2.0f;
    desc.rings = 8;
    desc.segments = 12;
    const auto geometry = eng::detail::buildPrimitiveGeometry(desc);

    require(geometry.has_value(), "valid capsule geometry was rejected");
    requireFiniteVertices(*geometry);
    requireOutwardWinding(*geometry);
    const Bounds bounds = boundsOf(*geometry);
    require(near(bounds.min, {-0.5f, -1.5f, -0.5f}) &&
                near(bounds.max, {0.5f, 1.5f, 0.5f}),
            "capsule silhouette omitted radius or straight height");

    size_t bottomEquator = 0;
    size_t topEquator = 0;
    for (const auto& vertex : geometry->vertices) {
        if (std::fabs(vertex.normal.y) < 0.0001f &&
            near(vertex.position.y, -1.0f))
            ++bottomEquator;
        if (std::fabs(vertex.normal.y) < 0.0001f &&
            near(vertex.position.y, 1.0f))
            ++topEquator;
    }
    require(bottomEquator >= size_t((desc.segments + 1) * 2) &&
                topEquator >= size_t((desc.segments + 1) * 2),
            "capsule did not duplicate hemisphere/cylinder equators");

    bool straightStrip = false;
    for (size_t i = 0; i < geometry->indices.size(); i += 3) {
        float minY = std::numeric_limits<float>::infinity();
        float maxY = -std::numeric_limits<float>::infinity();
        bool radialNormals = true;
        for (size_t corner = 0; corner < 3; ++corner) {
            const auto& vertex =
                geometry->vertices[geometry->indices[i + corner]];
            minY = std::min(minY, vertex.position.y);
            maxY = std::max(maxY, vertex.position.y);
            radialNormals &=
                std::fabs(vertex.normal.y) < 0.0001f;
        }
        straightStrip |= radialNormals && near(minY, -1.0f) &&
                         near(maxY, 1.0f);
    }
    require(straightStrip,
            "capsule has no indexed straight cylindrical strip");
}

void test_cone_triangles_have_nonzero_uv_area()
{
    eng::PrimitiveMeshDesc desc;
    desc.kind = eng::PrimitiveKind::Cone;
    desc.radius = 0.5f;
    desc.height = 1.5f;
    desc.segments = 8;
    const auto geometry = eng::detail::buildPrimitiveGeometry(desc);

    require(geometry.has_value(), "valid cone geometry was rejected");
    requireFiniteVertices(*geometry);
    requireOutwardWinding(*geometry);
    for (size_t i = 0; i < geometry->indices.size(); i += 3) {
        const glm::vec2 a =
            geometry->vertices[geometry->indices[i]].uv;
        const glm::vec2 b =
            geometry->vertices[geometry->indices[i + 1]].uv;
        const glm::vec2 c =
            geometry->vertices[geometry->indices[i + 2]].uv;
        const glm::vec2 ab = b - a;
        const glm::vec2 ac = c - a;
        require(std::fabs(ab.x * ac.y - ab.y * ac.x) > 0.000001f,
                "cone triangle has zero UV area");
    }
}

void test_inward_subdivided_box_uses_descriptor_geometry()
{
    eng::PrimitiveMeshDesc desc;
    desc.kind = eng::PrimitiveKind::Box;
    desc.size = {2.0f, 4.0f, 6.0f};
    desc.inwardFacing = true;
    desc.subdivisions = 2;
    const auto geometry = eng::detail::buildPrimitiveGeometry(desc);

    require(geometry.has_value(), "valid inward box geometry was rejected");
    requireFiniteVertices(*geometry);
    require(geometry->vertices.size() == 6u * 16u,
            "box subdivision count was not routed through descriptor");
    const Bounds bounds = boundsOf(*geometry);
    require(near(bounds.min, {-1.0f, -2.0f, -3.0f}) &&
                near(bounds.max, {1.0f, 2.0f, 3.0f}),
            "inward box extents do not match descriptor size");
    for (const auto& vertex : geometry->vertices)
        require(glm::dot(vertex.position, vertex.normal) < 0.0f,
                "inward box emitted an outward normal");
}

void test_collider_copies_transform_and_physics_properties()
{
    const eng::PrimitiveDesc desc = collidable(eng::PrimitiveKind::Cylinder);
    const auto resolved = eng::resolvePrimitiveCollider(desc);

    require(resolved.has_value(), "valid collider descriptor was rejected");
    require(near(resolved->body.position, desc.position),
            "collider position was not copied");
    require(near(resolved->body.orientation.w, desc.orientation.w) &&
                near(resolved->body.orientation.x, desc.orientation.x) &&
                near(resolved->body.orientation.y, desc.orientation.y) &&
                near(resolved->body.orientation.z, desc.orientation.z),
            "collider orientation was not copied");
    require(resolved->body.layer == eng::BodyLayer::Projectile,
            "body layer was not copied");
    require(resolved->body.dynamic && resolved->body.sensor,
            "dynamic or sensor flag was not copied");
    require(near(resolved->body.mass, 7.0f) &&
                near(resolved->body.friction, 0.25f) &&
                near(resolved->body.restitution, 0.75f),
            "physical properties were not copied");
}

void test_render_only_and_invalid_primitive_descriptors()
{
    eng::PrimitiveDesc renderOnly;
    renderOnly.material = "Game/Test";
    const auto resolved = eng::resolvePrimitiveCollider(renderOnly);
    require(resolved && !resolved->createsBody(),
            "render-only primitive unexpectedly creates a body");

    eng::PrimitiveDesc invalid = renderOnly;
    invalid.material.clear();
    require(!eng::resolvePrimitiveCollider(invalid),
            "blank material was accepted");

    invalid = renderOnly;
    invalid.scale.y = 0.0f;
    require(!eng::resolvePrimitiveCollider(invalid),
            "zero transform scale was accepted");

    invalid = renderOnly;
    invalid.orientation = {};
    require(!eng::resolvePrimitiveCollider(invalid),
            "zero-length orientation was accepted");
}

void test_cleanup_releases_owned_handles_once_and_resets_instance()
{
    eng::PrimitiveInstance instance{
        eng::NodeHandle{2}, eng::MeshHandle{3}, eng::BodyHandle{4}};
    std::string calls;

    eng::detail::releasePrimitiveOwnership(
        instance,
        [&](eng::BodyHandle body) {
            require(body.id == 4, "cleanup released the wrong body");
            calls += 'b';
        },
        [&](eng::NodeHandle node) {
            require(node.id == 2, "cleanup destroyed the wrong node");
            calls += 'n';
        },
        [&](eng::MeshHandle mesh) {
            require(mesh.id == 3, "cleanup released the wrong mesh");
            calls += 'm';
        });

    require(calls == "bnm",
            "cleanup did not release body, node, then mesh");
    require(!instance.node.valid() && !instance.mesh.valid() &&
                !instance.body.valid(),
            "cleanup did not reset the primitive instance");

    eng::detail::releasePrimitiveOwnership(
        instance,
        [&](eng::BodyHandle) { calls += 'B'; },
        [&](eng::NodeHandle) { calls += 'N'; },
        [&](eng::MeshHandle) { calls += 'M'; });
    require(calls == "bnm",
            "repeated cleanup released an already-owned handle");
}

std::string readProjectFile(const std::string& relativePath)
{
    std::ifstream file(
        std::string(PROJECT_SOURCE_DIR) + "/" + relativePath);
    require(file.good(), "repository contract file could not be read");
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

void test_repository_has_no_obsolete_round_frame_or_authored_arch()
{
    const std::string obsoleteRenderer =
        std::string("createPortal") + "Ring";
    const std::string obsoleteGenerator =
        std::string("createPortal") + "Disc";
    const std::string authoredAsset =
        std::string("portal_stone") + "_arch.obj";
    const std::string authoredField =
        std::string("frame") + "Mesh";
    const std::string oldShape =
        std::string("shape == ") + '"' + "ring" + '"';
    const char* files[] = {
        "engine/include/eng/Renderer.h",
        "engine/src/Renderer.cpp",
        "engine/src/ProceduralMeshes.h",
        "engine/src/ProceduralMeshes.cpp",
        "game/src/SceneFactory.h",
        "game/src/SceneFactory.cpp",
        "game/src/LiveLevel.cpp",
    };
    for (const char* file : files) {
        const std::string source = readProjectFile(file);
        require(source.find(obsoleteRenderer) == std::string::npos,
                "obsolete round-frame renderer/generator API remains");
        require(source.find(obsoleteGenerator) == std::string::npos,
                "obsolete dedicated portal-disc wrapper remains");
        require(source.find(authoredAsset) == std::string::npos,
                "authored portal arch reference remains");
        require(source.find(authoredField) == std::string::npos,
                "authored portal frame branch remains");
        require(source.find(oldShape) == std::string::npos,
                "obsolete showcase shape branch remains");
    }
    require(!std::filesystem::exists(
                std::string(PROJECT_SOURCE_DIR) +
                "/game/assets/meshes/props/" + authoredAsset),
            "authored portal arch asset still exists");
}

} // namespace

int main()
{
    test_mesh_validation_rejects_invalid_dimensions_and_tessellation();
    test_renderer_dispatch_selects_every_generator();
    test_renderer_exposes_one_descriptor_driven_primitive_entry_point();
    test_box_plane_and_sphere_collider_dimensions_follow_render_scale();
    test_round_primitive_collider_mapping_and_scaled_dimensions();
    test_parent_transform_drives_conservative_capsule_collider();
    test_sphere_geometry_extents_finite_attributes_and_outward_winding();
    test_every_outward_primitive_has_finite_aligned_geometry();
    test_capsule_geometry_has_two_hemispheres_and_a_straight_strip();
    test_cone_triangles_have_nonzero_uv_area();
    test_inward_subdivided_box_uses_descriptor_geometry();
    test_collider_copies_transform_and_physics_properties();
    test_render_only_and_invalid_primitive_descriptors();
    test_cleanup_releases_owned_handles_once_and_resets_instance();
    test_repository_has_no_obsolete_round_frame_or_authored_arch();
    std::cout << "PrimitiveTests: OK\n";
    return 0;
}
