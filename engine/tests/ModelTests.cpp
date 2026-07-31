#include <eng/Model.h>

#include "render/MeshResources.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "ModelTests: " << message << '\n';
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

eng::ModelDesc modelDesc()
{
    eng::ModelDesc desc;
    desc.meshPath = "model.obj";
    desc.material = "Game/Surface";
    return desc;
}

void test_auto_box_uses_absolute_nonuniform_scale()
{
    eng::ModelDesc desc = modelDesc();
    desc.collider = eng::ColliderMode::AutoBox;
    desc.scale = {-2.0f, 3.0f, -0.5f};
    const eng::MeshBounds bounds{{-1.0f, -2.0f, -4.0f},
                                 {1.0f, 2.0f, 4.0f}};

    const auto resolved = eng::resolveModelCollider(desc, bounds);

    require(resolved.has_value(), "valid AutoBox was rejected");
    require(resolved->body.kind == eng::ShapeKind::Box,
            "AutoBox did not select a box body");
    require(near(resolved->body.halfExtents, {2.0f, 6.0f, 2.0f}),
            "AutoBox did not apply absolute nonuniform scale");
}

void test_auto_box_offsets_body_by_oriented_scaled_bounds_center()
{
    eng::ModelDesc desc = modelDesc();
    desc.collider = eng::ColliderMode::AutoBox;
    desc.position = {10.0f, 20.0f, 30.0f};
    desc.scale = {2.0f, 3.0f, 4.0f};
    desc.orientation =
        glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    const eng::MeshBounds bounds{{1.0f, 2.0f, 3.0f},
                                 {3.0f, 4.0f, 5.0f}};

    const auto resolved = eng::resolveModelCollider(desc, bounds);

    require(resolved.has_value(), "offset AutoBox was rejected");
    require(near(resolved->body.position, {1.0f, 24.0f, 46.0f}),
            "body position omitted the oriented/scaled bounds-center offset");
}

void test_negative_scale_mirrors_noncentered_bounds_offset()
{
    eng::ModelDesc desc = modelDesc();
    desc.collider = eng::ColliderMode::AutoBox;
    desc.position = {5.0f, 6.0f, 7.0f};
    desc.scale = {-2.0f, 3.0f, -4.0f};
    const eng::MeshBounds bounds{{1.0f, -1.0f, 2.0f},
                                 {3.0f, 1.0f, 4.0f}};

    const auto resolved = eng::resolveModelCollider(desc, bounds);

    require(resolved.has_value(), "negative-scale AutoBox was rejected");
    require(near(resolved->body.position, {1.0f, 6.0f, -5.0f}),
            "negative scale did not mirror the bounds-center offset");
}

void test_centered_bounds_do_not_offset_body()
{
    eng::ModelDesc desc = modelDesc();
    desc.collider = eng::ColliderMode::AutoBox;
    desc.position = {5.0f, 6.0f, 7.0f};
    desc.scale = {-2.0f, 3.0f, -4.0f};
    const eng::MeshBounds bounds{{-1.0f, -2.0f, -3.0f},
                                 {1.0f, 2.0f, 3.0f}};

    const auto resolved = eng::resolveModelCollider(desc, bounds);

    require(resolved.has_value(), "centered AutoBox was rejected");
    require(near(resolved->body.position, desc.position),
            "centered bounds unexpectedly offset the body");
}

void test_explicit_box_half_extents_override_derived_size()
{
    eng::ModelDesc desc = modelDesc();
    desc.collider = eng::ColliderMode::AutoBox;
    desc.scale = {10.0f, 20.0f, 30.0f};
    desc.colliderHalfExtents = glm::vec3{0.25f, 0.5f, 0.75f};
    const eng::MeshBounds bounds{glm::vec3(-2.0f), glm::vec3(2.0f)};

    const auto resolved = eng::resolveModelCollider(desc, bounds);

    require(resolved.has_value(), "explicit AutoBox was rejected");
    require(near(resolved->body.halfExtents, {0.25f, 0.5f, 0.75f}),
            "explicit box half extents were ignored or scaled");
}

void test_auto_sphere_uses_largest_scaled_bounds_extent()
{
    eng::ModelDesc desc = modelDesc();
    desc.collider = eng::ColliderMode::AutoSphere;
    desc.scale = {-2.0f, 3.0f, 0.5f};
    const eng::MeshBounds bounds{{-1.0f, -2.0f, -4.0f},
                                 {1.0f, 2.0f, 4.0f}};

    const auto resolved = eng::resolveModelCollider(desc, bounds);

    require(resolved.has_value(), "valid AutoSphere was rejected");
    require(resolved->body.kind == eng::ShapeKind::Sphere,
            "AutoSphere did not select a sphere body");
    require(near(resolved->body.radius, 6.0f),
            "AutoSphere did not use the largest scaled bounds extent");
}

void test_explicit_sphere_radius_overrides_derived_size()
{
    eng::ModelDesc desc = modelDesc();
    desc.collider = eng::ColliderMode::AutoSphere;
    desc.scale = {10.0f, 20.0f, 30.0f};
    desc.colliderRadius = 1.25f;
    const eng::MeshBounds bounds{glm::vec3(-2.0f), glm::vec3(2.0f)};

    const auto resolved = eng::resolveModelCollider(desc, bounds);

    require(resolved.has_value(), "explicit AutoSphere was rejected");
    require(near(resolved->body.radius, 1.25f),
            "explicit sphere radius was ignored or scaled");
}

void test_primitive_body_copies_physics_properties()
{
    eng::ModelDesc desc = modelDesc();
    desc.collider = eng::ColliderMode::AutoBox;
    desc.bodyLayer = eng::CollisionLayer{3};
    desc.dynamic = true;
    desc.sensor = true;
    desc.mass = 7.0f;
    desc.friction = 0.25f;
    desc.restitution = 0.75f;
    desc.orientation =
        glm::angleAxis(glm::radians(40.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const eng::MeshBounds bounds{glm::vec3(-1.0f), glm::vec3(1.0f)};

    const auto resolved = eng::resolveModelCollider(desc, bounds);

    require(resolved.has_value(), "primitive property test was rejected");
    require(resolved->body.layer == eng::CollisionLayer{3},
            "body layer was not copied");
    require(resolved->body.dynamic, "dynamic flag was not copied");
    require(resolved->body.sensor, "sensor flag was not copied");
    require(near(resolved->body.mass, 7.0f), "mass was not copied");
    require(near(resolved->body.friction, 0.25f), "friction was not copied");
    require(near(resolved->body.restitution, 0.75f),
            "restitution was not copied");
    require(near(resolved->body.orientation.x, desc.orientation.x) &&
                near(resolved->body.orientation.y, desc.orientation.y) &&
                near(resolved->body.orientation.z, desc.orientation.z) &&
                near(resolved->body.orientation.w, desc.orientation.w),
            "orientation was not copied");
}

void test_none_mode_is_render_only()
{
    eng::ModelDesc desc = modelDesc();
    desc.collider = eng::ColliderMode::None;

    const auto resolved =
        eng::resolveModelCollider(desc, eng::MeshBounds{});

    require(resolved.has_value(), "None collider mode was rejected");
    require(resolved->mode == eng::ColliderMode::None,
            "None collider mode changed");
    require(!resolved->createsBody(), "None collider mode creates a body");
}

void test_dynamic_static_mesh_is_rejected()
{
    eng::ModelDesc desc = modelDesc();
    desc.collider = eng::ColliderMode::StaticMesh;
    desc.dynamic = true;

    require(!eng::resolveModelCollider(desc, eng::MeshBounds{}).has_value(),
            "dynamic StaticMesh was accepted");
}

void test_invalid_descriptor_inputs_are_rejected()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const eng::MeshBounds bounds{glm::vec3(-1.0f), glm::vec3(1.0f)};

    eng::ModelDesc emptyMesh = modelDesc();
    emptyMesh.meshPath.clear();
    require(!eng::resolveModelCollider(emptyMesh, bounds).has_value(),
            "empty mesh path was accepted");

    eng::ModelDesc emptyMaterial = modelDesc();
    emptyMaterial.material.clear();
    require(eng::resolveModelCollider(emptyMaterial, bounds).has_value(),
            "blank requested material did not defer to fallback");

    eng::ModelDesc badTransform = modelDesc();
    badTransform.position.x = nan;
    require(!eng::resolveModelCollider(badTransform, bounds).has_value(),
            "non-finite transform was accepted");

    eng::ModelDesc badCollider = modelDesc();
    badCollider.collider = eng::ColliderMode::AutoSphere;
    badCollider.colliderRadius = nan;
    require(!eng::resolveModelCollider(badCollider, bounds).has_value(),
            "non-finite collider override was accepted");
}

void test_parent_transform_composes_model_world_transform()
{
    const eng::NodeTransform parent{
        {10.0f, 20.0f, 30.0f},
        glm::angleAxis(glm::radians(90.0f),
                       glm::vec3(0.0f, 0.0f, 1.0f)),
        {2.0f, 3.0f, 4.0f}};
    const eng::NodeTransform local{
        {1.0f, 2.0f, 3.0f},
        glm::angleAxis(glm::radians(90.0f),
                       glm::vec3(0.0f, 1.0f, 0.0f)),
        {-1.0f, 0.5f, 2.0f}};

    const eng::NodeTransform world =
        eng::composeNodeTransform(parent, local);

    require(near(world.position, {4.0f, 22.0f, 42.0f}),
            "parent translation/rotation/scale did not transform child position");
    require(near(world.scale, {-2.0f, 1.5f, 8.0f}),
            "parent and child scale were not composed component-wise");
    require(near(world.orientation.w, 0.5f) &&
                near(world.orientation.x, -0.5f) &&
                near(world.orientation.y, 0.5f) &&
                near(world.orientation.z, 0.5f),
            "parent and child orientation were not composed in scene order");
}

void test_parent_transform_is_applied_to_auto_collider()
{
    eng::ModelDesc desc = modelDesc();
    desc.collider = eng::ColliderMode::AutoBox;
    desc.position = {1.0f, 2.0f, 3.0f};
    desc.orientation =
        glm::angleAxis(glm::radians(90.0f),
                       glm::vec3(0.0f, 1.0f, 0.0f));
    desc.scale = {-1.0f, 0.5f, 2.0f};
    const eng::NodeTransform parent{
        {10.0f, 20.0f, 30.0f},
        glm::angleAxis(glm::radians(90.0f),
                       glm::vec3(0.0f, 0.0f, 1.0f)),
        {2.0f, 3.0f, 4.0f}};
    const eng::MeshBounds bounds{{0.0f, 0.0f, 0.0f},
                                 {2.0f, 2.0f, 2.0f}};

    const auto resolved =
        eng::resolveModelCollider(desc, bounds, parent);

    require(resolved.has_value(),
            "parented AutoBox collider was rejected");
    require(near(resolved->body.position, {2.5f, 30.0f, 44.0f}),
            "AutoBox body did not use composed world position/orientation/scale");
    require(near(resolved->body.halfExtents, {2.0f, 1.5f, 8.0f}),
            "AutoBox size did not include parent scale");
}

void test_mesh_resource_release_is_isolated_and_idempotent()
{
    eng::detail::MeshResources resources;
    eng::detail::MeshGeometry firstGeometry;
    firstGeometry.vertices = {{1.0f, 2.0f, 3.0f}};
    firstGeometry.indices = {0, 0, 0};
    eng::detail::MeshGeometry secondGeometry;
    secondGeometry.vertices = {{4.0f, 5.0f, 6.0f}};
    secondGeometry.indices = {0, 0, 0};
    const eng::MeshHandle first =
        resources.add("first", std::move(firstGeometry));
    const eng::MeshHandle second =
        resources.add("second", std::move(secondGeometry));

    const auto released = resources.release(first);

    require(released && *released == "first",
            "release did not return the owned mesh resource");
    require(resources.name(first) == nullptr &&
                resources.geometry(first) == nullptr,
            "release retained the name or collision cache");
    require(!resources.release(first),
            "repeated release was not safely ignored");
    require(!resources.release({}),
            "invalid-handle release was not safely ignored");
    require(resources.name(second) &&
                *resources.name(second) == "second" &&
                resources.geometry(second) &&
                near(resources.geometry(second)->vertices[0],
                     {4.0f, 5.0f, 6.0f}),
            "releasing one mesh damaged another mesh handle");
}

} // namespace

int main()
{
    test_auto_box_uses_absolute_nonuniform_scale();
    test_auto_box_offsets_body_by_oriented_scaled_bounds_center();
    test_negative_scale_mirrors_noncentered_bounds_offset();
    test_centered_bounds_do_not_offset_body();
    test_explicit_box_half_extents_override_derived_size();
    test_auto_sphere_uses_largest_scaled_bounds_extent();
    test_explicit_sphere_radius_overrides_derived_size();
    test_primitive_body_copies_physics_properties();
    test_none_mode_is_render_only();
    test_dynamic_static_mesh_is_rejected();
    test_invalid_descriptor_inputs_are_rejected();
    test_parent_transform_composes_model_world_transform();
    test_parent_transform_is_applied_to_auto_collider();
    test_mesh_resource_release_is_isolated_and_idempotent();
    std::cout << "ModelTests: OK\n";
    return 0;
}
