#include <editor/scene/MultiEdit.h>

#include <cmath>

namespace ed::multiedit {
namespace {

using game::content::Entity;

// The same tolerance the writer canonicalises at (4 decimals). Comparing raw
// floats would call a drag that ended where it began a change, and fan a
// no-op out across the whole selection.
bool same(float a, float b)
{
    return std::round(a * 10000.0f) == std::round(b * 10000.0f);
}

// One scalar of the transform, addressed by axis so a drag on X does not carry
// Y and Z with it. That granularity is the point: an author nudging one axis
// across forty pillars means one axis.
bool applyAxis(float before, float after, float& target)
{
    if (same(before, after))
        return false;
    target = after;
    return true;
}

} // namespace

bool TransformAgreement::allAgree() const
{
    for (int axis = 0; axis < 3; ++axis)
        if (!position[axis] || !rotation[axis] || !scale[axis])
            return false;
    return true;
}

bool applyDelta(const Entity& before, const Entity& after, Entity& target)
{
    bool moved = false;

    for (int axis = 0; axis < 3; ++axis) {
        moved |= applyAxis(before.transform.position[axis],
                           after.transform.position[axis],
                           target.transform.position[axis]);
        moved |= applyAxis(before.transform.rotationDegrees[axis],
                           after.transform.rotationDegrees[axis],
                           target.transform.rotationDegrees[axis]);
        moved |= applyAxis(before.transform.scale[axis],
                           after.transform.scale[axis],
                           target.transform.scale[axis]);
    }

    // A material override needs a surface to override. Same rule the material
    // fan-out has always used: an entity with no prefab wears the material
    // outright, and stamping one across a mixed selection would silently
    // re-skin things the author was not looking at.
    if (before.material != after.material && !target.prefab.empty()) {
        target.material = after.material;
        moved = true;
    }
    if (before.castShadows != after.castShadows) {
        target.castShadows = after.castShadows;
        moved = true;
    }
    if (before.layer != after.layer) {
        target.layer = after.layer;
        moved = true;
    }

    // Lights, field by field. The one non-transform component worth fanning
    // out: "make these thirty torches dimmer" is a lighting pass, and doing it
    // one torch at a time is why the chapter calls multi-object editing an
    // advanced feature rather than a nicety.
    if (before.light && after.light && target.light) {
        const game::content::LightAuthor& b = *before.light;
        const game::content::LightAuthor& a = *after.light;
        game::content::LightAuthor& t = *target.light;
        if (b.type != a.type) {
            t.type = a.type;
            moved = true;
        }
        for (int channel = 0; channel < 3; ++channel)
            moved |= applyAxis(b.colour[channel], a.colour[channel],
                               t.colour[channel]);
        moved |= applyAxis(b.range, a.range, t.range);
        if (b.castShadows != a.castShadows) {
            t.castShadows = a.castShadows;
            moved = true;
        }
        // The animation block moves as a unit: its three numbers only mean
        // anything together, and a phase without a mode is not a state the
        // format has.
        if (b.animation.has_value() != a.animation.has_value()) {
            t.animation = a.animation;
            moved = true;
        } else if (a.animation && b.animation &&
                   (b.animation->mode != a.animation->mode ||
                    !same(b.animation->speed, a.animation->speed) ||
                    !same(b.animation->amount, a.animation->amount))) {
            // Phase is deliberately not copied: it is the per-instance offset
            // that stops a wall of torches guttering in lockstep, and fanning
            // it out would undo exactly what it is for.
            t.animation->mode = a.animation->mode;
            t.animation->speed = a.animation->speed;
            t.animation->amount = a.animation->amount;
            moved = true;
        }
    }

    return moved;
}

std::vector<std::string> changedFields(const Entity& before, const Entity& after)
{
    std::vector<std::string> fields;
    static const char* kAxes[] = {"x", "y", "z"};

    for (int axis = 0; axis < 3; ++axis) {
        if (!same(before.transform.position[axis], after.transform.position[axis]))
            fields.push_back(std::string("position.") + kAxes[axis]);
        if (!same(before.transform.rotationDegrees[axis],
                  after.transform.rotationDegrees[axis]))
            fields.push_back(std::string("rotation.") + kAxes[axis]);
        if (!same(before.transform.scale[axis], after.transform.scale[axis]))
            fields.push_back(std::string("scale.") + kAxes[axis]);
    }
    if (before.material != after.material)
        fields.emplace_back("material");
    if (before.castShadows != after.castShadows)
        fields.emplace_back("cast shadows");
    if (before.layer != after.layer)
        fields.emplace_back("layer");

    if (before.light && after.light) {
        const game::content::LightAuthor& b = *before.light;
        const game::content::LightAuthor& a = *after.light;
        if (b.type != a.type)
            fields.emplace_back("light type");
        if (!same(b.colour.x, a.colour.x) || !same(b.colour.y, a.colour.y) ||
            !same(b.colour.z, a.colour.z))
            fields.emplace_back("light colour");
        if (!same(b.range, a.range))
            fields.emplace_back("light range");
        if (b.castShadows != a.castShadows)
            fields.emplace_back("light shadows");
        const bool animMoved =
            b.animation.has_value() != a.animation.has_value() ||
            (a.animation && b.animation &&
             (b.animation->mode != a.animation->mode ||
              !same(b.animation->speed, a.animation->speed) ||
              !same(b.animation->amount, a.animation->amount)));
        if (animMoved)
            fields.emplace_back("light animation");
    }
    return fields;
}

TransformAgreement agreementOf(const std::vector<const Entity*>& entities)
{
    TransformAgreement out;
    const Entity* first = nullptr;
    for (const Entity* entity : entities) {
        if (!entity)
            continue;
        if (!first) {
            first = entity;
            continue;
        }
        for (int axis = 0; axis < 3; ++axis) {
            out.position[axis] =
                out.position[axis] && same(first->transform.position[axis],
                                           entity->transform.position[axis]);
            out.rotation[axis] =
                out.rotation[axis] &&
                same(first->transform.rotationDegrees[axis],
                     entity->transform.rotationDegrees[axis]);
            out.scale[axis] = out.scale[axis] &&
                              same(first->transform.scale[axis],
                                   entity->transform.scale[axis]);
        }
    }
    return out;
}

} // namespace ed::multiedit
