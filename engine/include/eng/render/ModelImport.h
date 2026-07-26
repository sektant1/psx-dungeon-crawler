#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace eng {

// Engine model space is metres, +Y up, and -Z forward.
enum class PivotMode { Source, BoundsCenter, BottomCenter, Custom };

struct ModelImportOptions {
    PivotMode pivot = PivotMode::BottomCenter;
    // Multiplier converting one source-file unit to metres. Must be positive.
    float metresPerSourceUnit = 1.0f;
    glm::quat sourceOrientation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 customPivot{0.0f};
};

struct MeshBounds {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

struct ResolvedModelMaterial {
    std::string requested;
    std::string material;
    bool usedFallback = false;
};

// Small testable seam used by Renderer to emit one fallback diagnostic per
// distinct blank/missing request over its lifetime.
class ModelMaterialFallbackWarnings
{
public:
    bool shouldLog(const std::string& requested, bool usedFallback)
    {
        if (!usedFallback)
            return false;
        const std::string key =
            requested.empty() ? "<blank>" : requested;
        return mWarned.insert(key).second;
    }

private:
    std::unordered_set<std::string> mWarned;
};

namespace model_import_detail {

inline bool finite(glm::vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

inline bool finite(glm::quat value)
{
    return std::isfinite(value.w) && std::isfinite(value.x) &&
           std::isfinite(value.y) && std::isfinite(value.z);
}

inline bool validPivot(PivotMode value)
{
    switch (value) {
    case PivotMode::Source:
    case PivotMode::BoundsCenter:
    case PivotMode::BottomCenter:
    case PivotMode::Custom:
        return true;
    }
    return false;
}

inline uint32_t floatBits(float value)
{
    if (value == 0.0f)
        value = 0.0f; // canonicalize negative zero
    uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

inline std::string canonicalPath(const std::string& path)
{
    try {
        return std::filesystem::weakly_canonical(
                   std::filesystem::absolute(path))
            .generic_string();
    } catch (...) {
        return std::filesystem::path(path).lexically_normal().generic_string();
    }
}

} // namespace model_import_detail

// Pure field sanitizer. Malformed fields independently fall back to canonical
// defaults, so one bad authoring value cannot poison a transform or cache key.
inline ModelImportOptions
sanitizeModelImportOptions(const ModelImportOptions& input)
{
    ModelImportOptions result = input;
    if (!model_import_detail::validPivot(result.pivot))
        result.pivot = PivotMode::BottomCenter;
    if (!std::isfinite(result.metresPerSourceUnit) ||
        result.metresPerSourceUnit <= 0.0f)
        result.metresPerSourceUnit = 1.0f;

    const float orientationLength2 =
        glm::dot(result.sourceOrientation, result.sourceOrientation);
    if (!model_import_detail::finite(result.sourceOrientation) ||
        !std::isfinite(orientationLength2) ||
        orientationLength2 <= std::numeric_limits<float>::epsilon()) {
        result.sourceOrientation = glm::quat(1, 0, 0, 0);
    } else {
        result.sourceOrientation = glm::normalize(result.sourceOrientation);
        // q and -q encode the same rotation. Pick one representation so
        // semantically identical imports share a deterministic identity.
        const float epsilon = std::numeric_limits<float>::epsilon();
        bool negate = result.sourceOrientation.w < -epsilon;
        if (std::abs(result.sourceOrientation.w) <= epsilon) {
            const float components[] = {
                result.sourceOrientation.x,
                result.sourceOrientation.y,
                result.sourceOrientation.z};
            for (float component : components) {
                if (std::abs(component) <= epsilon)
                    continue;
                negate = component < 0.0f;
                break;
            }
        }
        if (negate)
            result.sourceOrientation = -result.sourceOrientation;
    }

    if (!model_import_detail::finite(result.customPivot))
        result.customPivot = glm::vec3(0.0f);
    return result;
}

inline glm::vec3 transformModelImportPosition(glm::vec3 position,
                                               const glm::mat4& bake)
{
    return glm::vec3(bake * glm::vec4(position, 1.0f));
}

inline MeshBounds transformModelImportBounds(const MeshBounds& bounds,
                                              const glm::mat4& bake)
{
    MeshBounds transformed{
        glm::vec3(std::numeric_limits<float>::infinity()),
        glm::vec3(-std::numeric_limits<float>::infinity())};
    for (int x = 0; x < 2; ++x)
        for (int y = 0; y < 2; ++y)
            for (int z = 0; z < 2; ++z) {
                const glm::vec3 corner{
                    x ? bounds.max.x : bounds.min.x,
                    y ? bounds.max.y : bounds.min.y,
                    z ? bounds.max.z : bounds.min.z};
                const glm::vec3 point =
                    transformModelImportPosition(corner, bake);
                transformed.min = glm::min(transformed.min, point);
                transformed.max = glm::max(transformed.max, point);
            }
    return transformed;
}

inline MeshBounds modelImportBounds(
    const std::vector<glm::vec3>& positions, const glm::mat4& transform)
{
    MeshBounds bounds{
        glm::vec3(std::numeric_limits<float>::infinity()),
        glm::vec3(-std::numeric_limits<float>::infinity())};
    for (glm::vec3 position : positions) {
        const glm::vec3 transformed =
            transformModelImportPosition(position, transform);
        bounds.min = glm::min(bounds.min, transformed);
        bounds.max = glm::max(bounds.max, transformed);
    }
    return bounds;
}

// Builds the one canonical matrix baked into render and collision positions.
// Bounds-based pivots are selected after source scale and orientation.
inline glm::mat4 modelImportBakeMatrix(
    const ModelImportOptions& input,
    const std::vector<glm::vec3>& sourcePositions)
{
    const ModelImportOptions options =
        sanitizeModelImportOptions(input);
    const glm::mat4 linear =
        glm::mat4_cast(options.sourceOrientation) *
        glm::scale(glm::mat4(1.0f),
                   glm::vec3(options.metresPerSourceUnit));
    const MeshBounds transformedBounds =
        modelImportBounds(sourcePositions, linear);

    glm::vec3 pivot(0.0f);
    switch (options.pivot) {
    case PivotMode::Source:
        break;
    case PivotMode::BoundsCenter:
        pivot = (transformedBounds.min + transformedBounds.max) * 0.5f;
        break;
    case PivotMode::BottomCenter:
        pivot = {(transformedBounds.min.x + transformedBounds.max.x) * 0.5f,
                 transformedBounds.min.y,
                 (transformedBounds.min.z + transformedBounds.max.z) * 0.5f};
        break;
    case PivotMode::Custom:
        pivot = transformModelImportPosition(options.customPivot, linear);
        break;
    }
    return glm::translate(glm::mat4(1.0f), -pivot) * linear;
}

// Box-geometry convenience overload. Importers with actual vertex streams
// must use the position overload above so sparse geometry is not overbounded.
inline glm::mat4 modelImportBakeMatrix(const ModelImportOptions& input,
                                        const MeshBounds& sourceBounds)
{
    std::vector<glm::vec3> corners;
    corners.reserve(8);
    for (int x = 0; x < 2; ++x)
        for (int y = 0; y < 2; ++y)
            for (int z = 0; z < 2; ++z)
                corners.push_back({
                    x ? sourceBounds.max.x : sourceBounds.min.x,
                    y ? sourceBounds.max.y : sourceBounds.min.y,
                    z ? sourceBounds.max.z : sourceBounds.min.z});
    return modelImportBakeMatrix(input, corners);
}

inline glm::mat3 modelImportNormalMatrix(const glm::mat4& bake)
{
    return glm::transpose(glm::inverse(glm::mat3(bake)));
}

inline glm::mat4 sourcePivotImportBake(const glm::mat4& bake)
{
    return bake;
}

inline std::string modelImportCacheKey(const std::string& path,
                                        const ModelImportOptions& input)
{
    const ModelImportOptions options =
        sanitizeModelImportOptions(input);
    std::ostringstream key;
    key << model_import_detail::canonicalPath(path) << '|'
        << static_cast<unsigned>(options.pivot) << '|'
        << std::hex << std::setfill('0');
    const auto appendFloat = [&](float value) {
        key << std::setw(8)
            << model_import_detail::floatBits(value) << '|';
    };
    appendFloat(options.metresPerSourceUnit);
    appendFloat(options.sourceOrientation.w);
    appendFloat(options.sourceOrientation.x);
    appendFloat(options.sourceOrientation.y);
    appendFloat(options.sourceOrientation.z);
    appendFloat(options.customPivot.x);
    appendFloat(options.customPivot.y);
    appendFloat(options.customPivot.z);
    return key.str();
}

} // namespace eng
