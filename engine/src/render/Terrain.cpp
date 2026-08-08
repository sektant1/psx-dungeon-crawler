#include <eng/Terrain.h>

#include <eng/Log.h>
#include <eng/assets/AssetRoot.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

// The renderer's own decoder. A heightfield needs samples on the CPU rather
// than a GPU upload, and `loadImage` gives exactly that -- so this reads a
// heightmap through the same code path, and same stb build, as every texture.
#include "rhi/Image.h"

namespace eng {
namespace {

// Value noise. Deliberately the cheap kind: this is a placeholder shape so a
// terrain is never flat, not a landscape generator. A real heightmap replaces
// it entirely, and the editor's sculpt tools overwrite it sample by sample.
uint32_t hash(uint32_t x, uint32_t y, uint32_t seed)
{
    uint32_t h = seed + x * 374761393u + y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

float valueAt(int x, int y, uint32_t seed)
{
    return float(hash(uint32_t(x), uint32_t(y), seed) & 0xFFFFFFu) /
           float(0xFFFFFF);
}

float smootherstep(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float lerp(float a, float b, float t) { return a + (b - a) * t; }

float clamp01(float v) { return std::min(1.0f, std::max(0.0f, v)); }

} // namespace

bool loadHeightmap(const std::string& path, int resolution,
                   std::vector<float>& out, std::string* error)
{
    const std::filesystem::path resolved = assets::resolve(path);
    if (resolved.empty()) {
        if (error)
            *error = "heightmap not found: " + path;
        return false;
    }

    rhi_renderer::Image image;
    if (!rhi_renderer::loadImage(resolved, image) || image.width <= 1 ||
        image.height <= 1) {
        if (error)
            *error = "heightmap could not be decoded: " + resolved.string();
        return false;
    }
    const int width = image.width;
    const int height = image.height;

    out.assign(std::size_t(resolution) * std::size_t(resolution), 0.0f);
    for (int row = 0; row < resolution; ++row) {
        for (int column = 0; column < resolution; ++column) {
            // Bilinear, so a 256px heightmap on a 129-vertex grid does not
            // stair-step -- point sampling a downscale is what makes terrain
            // look terraced for no reason anyone can find.
            const float u = float(column) / float(resolution - 1) *
                            float(width - 1);
            const float v = float(row) / float(resolution - 1) *
                            float(height - 1);
            const int x0 = int(u);
            const int y0 = int(v);
            const int x1 = std::min(x0 + 1, width - 1);
            const int y1 = std::min(y0 + 1, height - 1);
            const float fx = u - float(x0);
            const float fy = v - float(y0);
            // The red channel is the height. A heightmap authored as greyscale
            // has the same value in all three, and one authored in colour by
            // accident produces something visibly wrong rather than an average
            // that looks almost right.
            const auto texel = [&](int x, int y) {
                return float(image.rgba[(std::size_t(y) * std::size_t(width) +
                                         std::size_t(x)) * 4]) / 255.0f;
            };
            out[std::size_t(row) * std::size_t(resolution) +
                std::size_t(column)] =
                lerp(lerp(texel(x0, y0), texel(x1, y0), fx),
                     lerp(texel(x0, y1), texel(x1, y1), fx), fy);
        }
    }
    return true;
}

float Terrain::noiseAt(float u, float v) const
{
    float total = 0.0f;
    float amplitude = 1.0f;
    float normalisation = 0.0f;
    float frequency = mDesc.frequency;
    for (int octave = 0; octave < std::max(1, mDesc.octaves); ++octave) {
        const float x = u * frequency;
        const float y = v * frequency;
        const int xi = int(std::floor(x));
        const int yi = int(std::floor(y));
        const float fx = smootherstep(x - float(xi));
        const float fy = smootherstep(y - float(yi));
        const uint32_t seed = mDesc.seed + uint32_t(octave) * 7919u;
        const float top = lerp(valueAt(xi, yi, seed),
                               valueAt(xi + 1, yi, seed), fx);
        const float bottom = lerp(valueAt(xi, yi + 1, seed),
                                  valueAt(xi + 1, yi + 1, seed), fx);
        total += lerp(top, bottom, fy) * amplitude;
        normalisation += amplitude;
        amplitude *= mDesc.roughness;
        frequency *= 2.0f;
    }
    return normalisation > 0.0f ? total / normalisation : 0.0f;
}

void Terrain::applyFlatten()
{
    if (mDesc.flatten.empty())
        return;
    const float step = mDesc.size / float(mResolution - 1);
    for (const TerrainDesc::FlatSpot& spot : mDesc.flatten) {
        const float outer = spot.radius * 2.0f;
        for (int row = 0; row < mResolution; ++row) {
            for (int column = 0; column < mResolution; ++column) {
                const glm::vec2 at(-half() + float(column) * step,
                                   -half() + float(row) * step);
                const float distance = glm::length(at - spot.centre);
                if (distance >= outer)
                    continue;
                // Fully flat inside `radius`, easing to untouched at 2x. A hard
                // edge here is a cliff around every spawn pad.
                const float t = distance <= spot.radius
                                    ? 1.0f
                                    : 1.0f - smootherstep((distance - spot.radius) /
                                                          (outer - spot.radius));
                float& sample =
                    mHeights[std::size_t(row) * std::size_t(mResolution) +
                             std::size_t(column)];
                sample = lerp(sample, spot.y, t);
            }
        }
    }
}

bool Terrain::build(const TerrainDesc& desc, std::string* error)
{
    if (desc.resolution < 2 || desc.size <= 0.0f) {
        if (error)
            *error = "terrain needs resolution >= 2 and a positive size";
        return false;
    }
    // A 1025x1025 patch is a million vertices in one draw call; anything that
    // big wants chunking, which this does not do. Refusing is better than
    // shipping a stall nobody attributes to the terrain.
    if (desc.resolution > 513) {
        if (error)
            *error = "terrain resolution above 513 needs chunking, which the "
                     "terrain system does not do yet";
        return false;
    }

    mDesc = desc;
    mResolution = desc.resolution;
    const std::size_t count =
        std::size_t(mResolution) * std::size_t(mResolution);

    if (!desc.heightmap.empty()) {
        std::vector<float> samples;
        if (!loadHeightmap(desc.heightmap, mResolution, samples, error)) {
            mResolution = 0;
            return false;
        }
        mHeights.resize(count);
        for (std::size_t i = 0; i < count; ++i)
            mHeights[i] = samples[i] * desc.heightScale;
    } else {
        mHeights.assign(count, 0.0f);
        for (int row = 0; row < mResolution; ++row) {
            for (int column = 0; column < mResolution; ++column) {
                const float u = float(column) / float(mResolution - 1);
                const float v = float(row) / float(mResolution - 1);
                mHeights[std::size_t(row) * std::size_t(mResolution) +
                         std::size_t(column)] =
                    noiseAt(u, v) * desc.heightScale;
            }
        }
    }

    applyFlatten();
    return true;
}

float Terrain::sample(int column, int row) const
{
    if (!valid())
        return 0.0f;
    column = std::clamp(column, 0, mResolution - 1);
    row = std::clamp(row, 0, mResolution - 1);
    return mHeights[std::size_t(row) * std::size_t(mResolution) +
                    std::size_t(column)];
}

void Terrain::setSample(int column, int row, float height)
{
    if (!valid() || column < 0 || row < 0 || column >= mResolution ||
        row >= mResolution)
        return;
    mHeights[std::size_t(row) * std::size_t(mResolution) +
             std::size_t(column)] = height;
}

float Terrain::heightAt(float x, float z) const
{
    if (!valid())
        return 0.0f;
    const float step = mDesc.size / float(mResolution - 1);
    // Clamped rather than wrapped or zeroed: walking off the edge should feel
    // like the ground continuing, not like falling through it.
    const float u = std::clamp((x + half()) / step, 0.0f,
                               float(mResolution - 1));
    const float v = std::clamp((z + half()) / step, 0.0f,
                               float(mResolution - 1));
    const int x0 = std::min(int(u), mResolution - 2);
    const int y0 = std::min(int(v), mResolution - 2);
    const float fx = u - float(x0);
    const float fy = v - float(y0);
    return lerp(lerp(sample(x0, y0), sample(x0 + 1, y0), fx),
                lerp(sample(x0, y0 + 1), sample(x0 + 1, y0 + 1), fx), fy);
}

glm::vec3 Terrain::normalAt(float x, float z) const
{
    // Central differences on the interpolated field rather than on the grid, so
    // the normal a character slides down and the normal the mesh is shaded with
    // are the same function -- sampling them differently is how a slope looks
    // one way and behaves another.
    const float step = mDesc.size / float(mResolution - 1);
    const float left = heightAt(x - step, z);
    const float right = heightAt(x + step, z);
    const float back = heightAt(x, z - step);
    const float front = heightAt(x, z + step);
    return glm::normalize(
        glm::vec3(left - right, 2.0f * step, back - front));
}

bool Terrain::raise(glm::vec2 centre, float radius, float strength)
{
    if (!valid() || radius <= 0.0f || strength == 0.0f)
        return false;
    const float step = mDesc.size / float(mResolution - 1);
    bool changed = false;
    for (int row = 0; row < mResolution; ++row) {
        for (int column = 0; column < mResolution; ++column) {
            const glm::vec2 at(-half() + float(column) * step,
                               -half() + float(row) * step);
            const float distance = glm::length(at - centre);
            if (distance >= radius)
                continue;
            const float falloff = smootherstep(1.0f - distance / radius);
            mHeights[std::size_t(row) * std::size_t(mResolution) +
                     std::size_t(column)] += strength * falloff;
            changed = true;
        }
    }
    return changed;
}

bool Terrain::smooth(glm::vec2 centre, float radius, float strength)
{
    if (!valid() || radius <= 0.0f || strength == 0.0f)
        return false;
    const float step = mDesc.size / float(mResolution - 1);

    // The average is taken over the ORIGINAL field, not updated in place: a
    // running average sweeps height across the patch in whichever direction the
    // loop happens to run, which reads as the brush dragging the terrain.
    float total = 0.0f;
    int counted = 0;
    for (int row = 0; row < mResolution; ++row) {
        for (int column = 0; column < mResolution; ++column) {
            const glm::vec2 at(-half() + float(column) * step,
                               -half() + float(row) * step);
            if (glm::length(at - centre) >= radius)
                continue;
            total += sample(column, row);
            ++counted;
        }
    }
    if (counted == 0)
        return false;
    const float average = total / float(counted);

    const float amount = clamp01(strength);
    for (int row = 0; row < mResolution; ++row) {
        for (int column = 0; column < mResolution; ++column) {
            const glm::vec2 at(-half() + float(column) * step,
                               -half() + float(row) * step);
            const float distance = glm::length(at - centre);
            if (distance >= radius)
                continue;
            const float falloff = smootherstep(1.0f - distance / radius);
            float& value =
                mHeights[std::size_t(row) * std::size_t(mResolution) +
                         std::size_t(column)];
            value = lerp(value, average, amount * falloff);
        }
    }
    return true;
}

void Terrain::heightRange(float& low, float& high) const
{
    low = 0.0f;
    high = 0.0f;
    if (mHeights.empty())
        return;
    const auto minmax = std::minmax_element(mHeights.begin(), mHeights.end());
    low = *minmax.first;
    high = *minmax.second;
}

content::MeshData Terrain::geometry(const std::string& material) const
{
    content::MeshData data;
    if (!valid())
        return data;

    content::MeshSubmesh submesh;
    submesh.name = "terrain";
    submesh.sourceMaterial = material;
    const std::size_t count =
        std::size_t(mResolution) * std::size_t(mResolution);
    submesh.vertices.reserve(count);

    const float step = mDesc.size / float(mResolution - 1);
    for (int row = 0; row < mResolution; ++row) {
        for (int column = 0; column < mResolution; ++column) {
            const float x = -half() + float(column) * step;
            const float z = -half() + float(row) * step;
            content::MeshVertex vertex;
            vertex.position = glm::vec3(x, sample(column, row), z);
            vertex.normal = normalAt(x, z);
            vertex.texcoord =
                glm::vec2(float(column) / float(mResolution - 1) *
                              mDesc.uvScale,
                          float(row) / float(mResolution - 1) * mDesc.uvScale);
            vertex.colour = glm::vec4(1.0f);
            submesh.vertices.push_back(vertex);
        }
    }

    submesh.indices.reserve(std::size_t(mResolution - 1) *
                            std::size_t(mResolution - 1) * 6);
    for (int row = 0; row + 1 < mResolution; ++row) {
        for (int column = 0; column + 1 < mResolution; ++column) {
            const uint32_t a = uint32_t(row) * uint32_t(mResolution) +
                               uint32_t(column);
            const uint32_t b = a + 1;
            const uint32_t c = a + uint32_t(mResolution);
            const uint32_t d = c + 1;
            // Counter-clockwise seen from above, matching the winding every
            // other mesh in this engine uses; the opposite is a terrain that is
            // invisible from the only side anyone stands on.
            submesh.indices.insert(submesh.indices.end(),
                                   {a, c, b, b, c, d});
        }
    }

    data.collisionVertices.reserve(submesh.vertices.size());
    for (const content::MeshVertex& vertex : submesh.vertices)
        data.collisionVertices.push_back(vertex.position);
    data.collisionIndices = submesh.indices;
    data.submeshes.push_back(std::move(submesh));
    return data;
}

} // namespace eng
