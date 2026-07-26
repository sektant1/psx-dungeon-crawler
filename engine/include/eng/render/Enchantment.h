#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eng {

enum class EnchantmentStyle { Arcane, Fire, Poison, Frost };

struct EnchantmentDesc {
    EnchantmentStyle style = EnchantmentStyle::Arcane;
    float strength = 1.0f;
    float runeScale = 3.5f;
    glm::vec3 scroll{0.12f, 0.20f, 0.08f};
    float pulseSpeed = 3.1f;
    float pulseDepth = 0.18f;
    float edgeIntensity = 0.35f;
    float bandCount = 4.0f;
    float pixelScale = 18.0f;
    float coreBoost = 1.35f;
    bool recursive = true;
};

struct EnchantmentPalette {
    glm::vec4 colour;
    glm::vec3 scrollDirection;
};

// Pure ownership/material bookkeeping. Renderer resource operations remain
// outside this type; replace/take return exactly what the caller must restore
// and release, which also makes reapplication and scene teardown testable.
template <typename Attachment, typename Owner>
class EnchantmentBookkeeping
{
public:
    struct State {
        std::string baseMaterial;
        std::string generatedMaterial;
        Owner owner{};
    };

    const State* find(const Attachment& attachment) const
    {
        const auto found = mStates.find(attachment);
        return found == mStates.end() ? nullptr : &found->second;
    }

    std::optional<State> replace(Attachment attachment, State state)
    {
        auto found = mStates.find(attachment);
        if (found == mStates.end()) {
            mStates.emplace(std::move(attachment), std::move(state));
            return std::nullopt;
        }
        State previous = std::move(found->second);
        found->second = std::move(state);
        return previous;
    }

    std::optional<State> take(const Attachment& attachment)
    {
        auto found = mStates.find(attachment);
        if (found == mStates.end())
            return std::nullopt;
        State state = std::move(found->second);
        mStates.erase(found);
        return state;
    }

    std::vector<State> takeAll()
    {
        std::vector<State> states;
        states.reserve(mStates.size());
        for (auto& [attachment, state] : mStates)
            states.push_back(std::move(state));
        mStates.clear();
        return states;
    }

    bool shouldClear(const Attachment& attachment, const Owner& current,
                     const Owner& root) const
    {
        const State* state = find(attachment);
        return state && (current == root || state->owner == root);
    }

    bool containsGeneratedMaterial(std::string_view material) const
    {
        for (const auto& [attachment, state] : mStates)
            if (state.generatedMaterial == material)
                return true;
        return false;
    }

    std::size_t size() const noexcept { return mStates.size(); }

private:
    std::unordered_map<Attachment, State> mStates;
};

inline EnchantmentStyle sanitizeEnchantmentStyle(EnchantmentStyle style)
{
    switch (style) {
    case EnchantmentStyle::Arcane:
    case EnchantmentStyle::Fire:
    case EnchantmentStyle::Poison:
    case EnchantmentStyle::Frost:
        return style;
    }
    return EnchantmentStyle::Arcane;
}

inline EnchantmentDesc sanitizeEnchantmentDesc(const EnchantmentDesc& desc)
{
    const EnchantmentDesc defaults;
    EnchantmentDesc clean = desc;
    clean.style = sanitizeEnchantmentStyle(clean.style);
    clean.strength = std::clamp(
        std::isfinite(clean.strength) ? clean.strength : defaults.strength,
        0.0f, 2.0f);
    clean.runeScale = std::clamp(
        std::isfinite(clean.runeScale) ? clean.runeScale : defaults.runeScale,
        0.1f, 64.0f);
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(clean.scroll[axis]))
            clean.scroll[axis] = defaults.scroll[axis];
    clean.pulseSpeed = std::clamp(
        std::isfinite(clean.pulseSpeed) ? clean.pulseSpeed
                                        : defaults.pulseSpeed,
        0.0f, 20.0f);
    clean.pulseDepth = std::clamp(
        std::isfinite(clean.pulseDepth) ? clean.pulseDepth
                                        : defaults.pulseDepth,
        0.0f, 1.0f);
    clean.edgeIntensity = std::clamp(
        std::isfinite(clean.edgeIntensity) ? clean.edgeIntensity
                                           : defaults.edgeIntensity,
        0.0f, 4.0f);
    clean.bandCount = std::clamp(
        std::isfinite(clean.bandCount) ? clean.bandCount
                                       : defaults.bandCount,
        2.0f, 8.0f);
    clean.pixelScale = std::clamp(
        std::isfinite(clean.pixelScale) ? clean.pixelScale
                                        : defaults.pixelScale,
        4.0f, 64.0f);
    clean.coreBoost = std::clamp(
        std::isfinite(clean.coreBoost) ? clean.coreBoost
                                       : defaults.coreBoost,
        0.0f, 3.0f);
    return clean;
}

inline EnchantmentPalette enchantmentPalette(EnchantmentStyle style)
{
    switch (sanitizeEnchantmentStyle(style)) {
    case EnchantmentStyle::Fire:
        return {{1.0f, 0.24f, 0.03f, 0.75f},
                {1.0f, -0.65f, 0.35f}};
    case EnchantmentStyle::Poison:
        return {{0.32f, 1.0f, 0.08f, 0.68f},
                {-0.55f, 0.85f, -1.0f}};
    case EnchantmentStyle::Frost:
        return {{0.22f, 0.72f, 1.0f, 0.68f},
                {0.35f, 1.0f, -0.70f}};
    case EnchantmentStyle::Arcane:
        return {{0.62f, 0.32f, 1.0f, 0.68f},
                {1.0f, 1.0f, 1.0f}};
    }
    return {{0.62f, 0.32f, 1.0f, 0.68f}, {1.0f, 1.0f, 1.0f}};
}

// Pure scene-tree traversal shared by the renderer and its unit tests. The
// root is always included; descendants are visited breadth-first only when
// recursive is requested.
template <typename Node, typename Children>
std::vector<Node> collectEnchantmentTargets(Node root, bool recursive,
                                            Children&& children)
{
    std::vector<Node> targets{std::move(root)};
    if (!recursive)
        return targets;
    for (std::size_t index = 0; index < targets.size(); ++index)
        for (Node child : children(targets[index]))
            targets.push_back(std::move(child));
    return targets;
}

} // namespace eng
