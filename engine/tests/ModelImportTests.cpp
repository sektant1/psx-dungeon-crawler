#include <eng/Model.h>
#include <eng/render/ModelImport.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "ModelImportTests: " << message << '\n';
        std::exit(1);
    }
}

bool near(float lhs, float rhs, float epsilon = 0.0001f)
{
    return std::abs(lhs - rhs) <= epsilon;
}

bool near(glm::vec3 lhs, glm::vec3 rhs, float epsilon = 0.0001f)
{
    return near(lhs.x, rhs.x, epsilon) &&
           near(lhs.y, rhs.y, epsilon) &&
           near(lhs.z, rhs.z, epsilon);
}

glm::vec3 point(const glm::mat4& matrix, glm::vec3 value)
{
    return glm::vec3(matrix * glm::vec4(value, 1.0f));
}

void test_all_pivots_use_scaled_oriented_asymmetric_bounds()
{
    const eng::MeshBounds bounds{{1.0f, -2.0f, 3.0f},
                                 {5.0f, 4.0f, 9.0f}};
    eng::ModelImportOptions options;
    options.metresPerSourceUnit = 2.0f;
    options.sourceOrientation =
        glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));

    options.pivot = eng::PivotMode::Source;
    require(near(point(eng::modelImportBakeMatrix(options, bounds),
                       {0.0f, 0.0f, 0.0f}),
                 {0.0f, 0.0f, 0.0f}),
            "Source pivot moved the source origin");

    options.pivot = eng::PivotMode::BoundsCenter;
    const glm::mat4 boundsCenter =
        eng::modelImportBakeMatrix(options, bounds);
    require(near(point(boundsCenter, {3.0f, 1.0f, 6.0f}),
                 {0.0f, 0.0f, 0.0f}),
            "BoundsCenter did not move the transformed center to origin");

    options.pivot = eng::PivotMode::BottomCenter;
    const glm::mat4 bottomCenter =
        eng::modelImportBakeMatrix(options, bounds);
    require(near(point(bottomCenter, {3.0f, -2.0f, 6.0f}),
                 {0.0f, 0.0f, 0.0f}),
            "BottomCenter did not move transformed bottom center to origin");

    options.pivot = eng::PivotMode::Custom;
    options.customPivot = {4.0f, 3.0f, 8.0f};
    require(near(point(eng::modelImportBakeMatrix(options, bounds),
                       options.customPivot),
                 {0.0f, 0.0f, 0.0f}),
            "Custom did not move the transformed custom pivot to origin");
}

void test_rotated_non_box_pivots_use_actual_vertices_not_aabb_corners()
{
    const std::vector<glm::vec3> vertices{
        {0.0f, 0.0f, 0.0f},
        {2.0f, 0.0f, 0.0f},
        {0.0f, 2.0f, 1.0f}};
    eng::ModelImportOptions options;
    options.sourceOrientation =
        glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 1, 0));

    options.pivot = eng::PivotMode::BoundsCenter;
    const glm::mat4 centered =
        eng::modelImportBakeMatrix(options, vertices);
    const eng::MeshBounds centeredBounds =
        eng::modelImportBounds(vertices, centered);
    require(near((centeredBounds.min + centeredBounds.max) * 0.5f,
                 {0.0f, 0.0f, 0.0f}),
            "BoundsCenter used rotated source-AABB corners");

    options.pivot = eng::PivotMode::BottomCenter;
    const glm::mat4 bottom =
        eng::modelImportBakeMatrix(options, vertices);
    const eng::MeshBounds bottomBounds =
        eng::modelImportBounds(vertices, bottom);
    require(near({(bottomBounds.min.x + bottomBounds.max.x) * 0.5f,
                  bottomBounds.min.y,
                  (bottomBounds.min.z + bottomBounds.max.z) * 0.5f},
                 {0.0f, 0.0f, 0.0f}),
            "BottomCenter used rotated source-AABB corners");
}

void test_default_is_metres_y_up_negative_z_forward_bottom_center()
{
    const eng::ModelImportOptions options;
    require(options.pivot == eng::PivotMode::BottomCenter,
            "default pivot is not BottomCenter");
    require(near(options.metresPerSourceUnit, 1.0f),
            "default source units are not metres");
    require(near(options.sourceOrientation * glm::vec3(0, 1, 0),
                 {0.0f, 1.0f, 0.0f}),
            "default orientation changed +Y up");
    require(near(options.sourceOrientation * glm::vec3(0, 0, -1),
                 {0.0f, 0.0f, -1.0f}),
            "default orientation changed -Z forward");
}

void test_normal_matrix_is_inverse_transpose()
{
    const glm::mat4 bake =
        glm::mat4_cast(glm::angleAxis(glm::radians(90.0f),
                                     glm::vec3(0, 0, 1))) *
        glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 3.0f, 4.0f));

    const glm::vec3 transformed = glm::normalize(
        eng::modelImportNormalMatrix(bake) * glm::vec3(1, 1, 0));
    require(near(transformed, {-0.5547002f, 0.8320503f, 0.0f}),
            "normal did not use inverse transpose under nonuniform scale");
}

void test_bounds_render_and_collision_positions_share_matrix()
{
    const eng::MeshBounds source{{1.0f, -2.0f, 3.0f},
                                 {5.0f, 4.0f, 9.0f}};
    eng::ModelImportOptions options;
    options.metresPerSourceUnit = 0.5f;
    options.sourceOrientation =
        glm::angleAxis(glm::radians(90.0f), glm::vec3(1, 0, 0));
    const glm::mat4 bake = eng::modelImportBakeMatrix(options, source);
    const eng::MeshBounds finalBounds =
        eng::transformModelImportBounds(source, bake);

    const glm::vec3 sourceVertex{5.0f, 4.0f, 9.0f};
    const glm::vec3 renderPosition =
        eng::transformModelImportPosition(sourceVertex, bake);
    const glm::vec3 collisionPosition =
        eng::transformModelImportPosition(sourceVertex, bake);
    require(near(renderPosition, collisionPosition),
            "render and collision transform streams disagree");
    require(glm::all(glm::greaterThanEqual(renderPosition,
                                            finalBounds.min)) &&
                glm::all(glm::lessThanEqual(renderPosition,
                                             finalBounds.max)),
            "transformed render/collision position escaped final bounds");
    require(near(finalBounds.min.y, 0.0f),
            "default BottomCenter bounds do not rest on y=0");
}

void test_invalid_options_sanitize_to_safe_defaults()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    eng::ModelImportOptions invalid;
    invalid.pivot = static_cast<eng::PivotMode>(99);
    invalid.metresPerSourceUnit = -1.0f;
    invalid.sourceOrientation = {nan, 0.0f, 0.0f, 0.0f};
    invalid.customPivot = {nan, 2.0f, 3.0f};

    const eng::ModelImportOptions sanitized =
        eng::sanitizeModelImportOptions(invalid);
    require(sanitized.pivot == eng::PivotMode::BottomCenter,
            "invalid pivot did not fall back to BottomCenter");
    require(near(sanitized.metresPerSourceUnit, 1.0f),
            "negative source scale did not fall back to metres");
    require(near(sanitized.sourceOrientation * glm::vec3(1, 0, 0),
                 {1.0f, 0.0f, 0.0f}),
            "non-normalizable orientation did not fall back to identity");
    require(near(sanitized.customPivot, {0.0f, 0.0f, 0.0f}),
            "invalid custom pivot did not fall back to source origin");

    invalid.metresPerSourceUnit = 0.0f;
    invalid.sourceOrientation = {0.0f, 0.0f, 0.0f, 0.0f};
    const auto zeroSanitized = eng::sanitizeModelImportOptions(invalid);
    require(near(zeroSanitized.metresPerSourceUnit, 1.0f),
            "zero source scale was accepted");
    require(near(zeroSanitized.sourceOrientation.w, 1.0f),
            "zero quaternion was accepted");
}

void test_cache_key_is_stable_and_covers_every_import_field()
{
    eng::ModelImportOptions base;
    const std::string first =
        eng::modelImportCacheKey("./models/../models/prop.obj", base);
    const std::string identical =
        eng::modelImportCacheKey("models/prop.obj", base);
    require(first == identical,
            "canonical-equivalent paths produced different cache keys");

    auto changed = base;
    changed.pivot = eng::PivotMode::Source;
    require(first != eng::modelImportCacheKey("models/prop.obj", changed),
            "pivot change aliased the cache key");
    changed = base;
    changed.metresPerSourceUnit = 0.01f;
    require(first != eng::modelImportCacheKey("models/prop.obj", changed),
            "scale change aliased the cache key");
    changed = base;
    changed.sourceOrientation =
        glm::angleAxis(glm::radians(30.0f), glm::vec3(0, 1, 0));
    require(first != eng::modelImportCacheKey("models/prop.obj", changed),
            "orientation change aliased the cache key");
    changed = base;
    changed.customPivot = {1.0f, 2.0f, 3.0f};
    require(first != eng::modelImportCacheKey("models/prop.obj", changed),
            "custom pivot change aliased the cache key");

    eng::ModelImportOptions halfTurn;
    halfTurn.sourceOrientation =
        glm::angleAxis(glm::radians(180.0f), glm::vec3(-1, 0, 0));
    eng::ModelImportOptions negated = halfTurn;
    negated.sourceOrientation = -negated.sourceOrientation;
    require(eng::modelImportCacheKey("models/prop.obj", halfTurn) ==
                eng::modelImportCacheKey("models/prop.obj", negated),
            "q/-q at w==0 produced distinct cache identities");
}

void test_single_submesh_material_selection_falls_back()
{
    eng::ModelDesc desc;
    desc.material = "Legacy/Material";
    auto resolved =
        eng::resolveModelMaterialForSubmesh(desc, 0, true);
    require(resolved.material == "Legacy/Material" &&
                !resolved.usedFallback &&
                resolved.requested == "Legacy/Material",
            "legacy one-material attachment changed");

    desc.submeshMaterials = {"Indexed/Material"};
    resolved = eng::resolveModelMaterialForSubmesh(desc, 0, true);
    require(resolved.material == "Indexed/Material" &&
                !resolved.usedFallback,
            "single submesh ignored indexed slot zero");

    desc.submeshMaterials[0].clear();
    resolved = eng::resolveModelMaterialForSubmesh(desc, 0, false);
    require(resolved.material == desc.fallbackMaterial &&
                resolved.usedFallback && resolved.requested.empty(),
            "blank indexed material did not use fallback");

    desc.submeshMaterials[0] = "Missing/Material";
    resolved = eng::resolveModelMaterialForSubmesh(desc, 0, false);
    require(resolved.material == desc.fallbackMaterial &&
                resolved.usedFallback &&
                resolved.requested == "Missing/Material",
            "missing indexed material did not expose one fallback warning");

    eng::ModelMaterialFallbackWarnings warnings;
    require(warnings.shouldLog(resolved.requested,
                               resolved.usedFallback),
            "first missing material did not request a warning");
    require(!warnings.shouldLog(resolved.requested,
                                resolved.usedFallback),
            "repeated missing material requested a duplicate warning");
    require(!warnings.shouldLog("Present/Material", false),
            "available material requested a fallback warning");
    require(warnings.shouldLog("", true) &&
                !warnings.shouldLog("", true),
            "blank material warning was not deduplicated");
}

void test_production_material_resolution_queries_requested_name()
{
    eng::ModelDesc desc;
    desc.material = "Missing/ProductionMaterial";
    std::string queried;
    const auto resolved = eng::resolveModelMaterialForSubmesh(
        desc, 0, [&](const std::string& requested) {
            queried = requested;
            return false;
        });

    require(queried == "Missing/ProductionMaterial",
            "production resolver did not query the requested material");
    require(resolved.requested == queried &&
                resolved.material == desc.fallbackMaterial &&
                resolved.usedFallback,
            "production resolver did not carry fallback warning state");
}

void test_source_pivot_compatibility_composes_existing_bake()
{
    const glm::mat4 oldBake =
        glm::translate(glm::mat4(1.0f), glm::vec3(2, 3, 4)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(2, 1, 0.5f));
    const glm::mat4 compatibility =
        eng::sourcePivotImportBake(oldBake);
    require(near(point(compatibility, {1.0f, 2.0f, 3.0f}),
                 {4.0f, 5.0f, 5.5f}),
            "legacy bake compatibility changed position semantics");
}

} // namespace

int main()
{
    test_all_pivots_use_scaled_oriented_asymmetric_bounds();
    test_rotated_non_box_pivots_use_actual_vertices_not_aabb_corners();
    test_default_is_metres_y_up_negative_z_forward_bottom_center();
    test_normal_matrix_is_inverse_transpose();
    test_bounds_render_and_collision_positions_share_matrix();
    test_invalid_options_sanitize_to_safe_defaults();
    test_cache_key_is_stable_and_covers_every_import_field();
    test_single_submesh_material_selection_falls_back();
    test_production_material_resolution_queries_requested_name();
    test_source_pivot_compatibility_composes_existing_bake();
    std::cout << "ModelImportTests: OK\n";
    return 0;
}
