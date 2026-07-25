#include "GizmoTool.h"

#include "Commands.h" // makeSetTransform, makeComposite

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <functional>

namespace editor {

namespace {
constexpr glm::vec3 kAxes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
} // namespace

bool GizmoTool::begin(entt::registry& reg, const Selection& sel, const Ray& ray,
                      glm::vec3 centroid, const GizmoConfig& cfg)
{
    (void)cfg; // begin only hit-tests; the mode is read live in drag()
    // Axis hit-test: the closest of the three axes within a world-space radius.
    float bestDist = 0.35f;
    int bestAxis = -1;
    for (int a = 0; a < 3; ++a) {
        float t = 0.0f;
        if (!closestPointOnAxis(centroid, kAxes[a], ray, t)) continue;
        if (t < 0.0f || t > 1.6f) continue;
        const glm::vec3 pOnAxis = centroid + kAxes[a] * t;
        const glm::vec3 w = pOnAxis - ray.origin;
        const float proj = glm::dot(w, ray.dir);
        const glm::vec3 closest = ray.origin + ray.dir * proj;
        const float d = glm::length(closest - pOnAxis);
        if (d < bestDist) { bestDist = d; bestAxis = a; }
    }
    if (bestAxis < 0) return false;

    const entt::entity e = sel.primary();
    if (e == entt::null || !reg.valid(e) ||
        !reg.all_of<eng::ecs::Transform>(e))
        return false;

    mDragging = true;
    mAxis = bestAxis;
    mPreDrag = reg.get<eng::ecs::Transform>(e);
    // Snapshot every selected entity so the drag moves the whole selection.
    mPre.clear();
    for (entt::entity s : sel.items())
        if (reg.valid(s) && reg.all_of<eng::ecs::Transform>(s))
            mPre.push_back({s, reg.get<eng::ecs::Transform>(s)});
    mCentroid = centroid;
    float t = 0.0f;
    closestPointOnAxis(centroid, kAxes[bestAxis], ray, t);
    mStartHit = centroid + kAxes[bestAxis] * t;
    mStartT = t;
    // Rotate reference: pointer on the plane perpendicular to the axis.
    glm::vec3 hit;
    mStartVec = rayPlane(ray, centroid, kAxes[bestAxis], hit) ? hit - centroid
                                                              : glm::vec3(0.0f);
    return true;
}

void GizmoTool::drag(entt::registry& reg, const Ray& ray, const GizmoConfig& cfg)
{
    if (!mDragging) return;
    const glm::vec3 axis = kAxes[mAxis];

    // Build a per-entity transform map for the active mode, applied over each
    // entity's pre-drag transform.
    std::function<eng::ecs::Transform(const eng::ecs::Transform&)> xform;

    if (cfg.mode == GizmoMode::Translate) {
        float t = 0.0f;
        if (!closestPointOnAxis(mStartHit, axis, ray, t)) return;
        // Snap the primary's resulting position, then translate the group by the
        // same offset so the selection stays rigid and grid-aligned.
        glm::vec3 primPos = mPreDrag.position + axis * t;
        if (cfg.snapStep > 0.0f) {
            primPos.x = snap(primPos.x, cfg.snapStep);
            primPos.y = snap(primPos.y, cfg.snapStep);
            primPos.z = snap(primPos.z, cfg.snapStep);
        }
        if (cfg.gridSize > 0.0f) { // world-grid snap overrides snapStep for Move
            primPos.x = snap(primPos.x, cfg.gridSize);
            primPos.y = snap(primPos.y, cfg.gridSize);
            primPos.z = snap(primPos.z, cfg.gridSize);
        }
        const glm::vec3 offset = primPos - mPreDrag.position;
        xform = [offset](const eng::ecs::Transform& pre) {
            eng::ecs::Transform out = pre;
            out.position = pre.position + offset;
            return out;
        };
    } else if (cfg.mode == GizmoMode::Rotate) {
        glm::vec3 hit;
        if (!rayPlane(ray, mCentroid, axis, hit)) return;
        float ang = signedAngleAround(mStartVec, hit - mCentroid, axis);
        if (cfg.snapStep > 0.0f) // snap is degrees in Rotate mode
            ang = glm::radians(snap(glm::degrees(ang), cfg.snapStep));
        const glm::quat q = glm::angleAxis(ang, axis);
        const glm::vec3 pivot = mCentroid;
        xform = [q, pivot](const eng::ecs::Transform& pre) {
            eng::ecs::Transform out = pre;
            out.rotation = glm::normalize(q * pre.rotation);
            out.position = pivot + q * (pre.position - pivot);
            return out;
        };
    } else { // Scale
        float t = 0.0f;
        if (!closestPointOnAxis(mCentroid, axis, ray, t)) return;
        if (std::abs(mStartT) < 1e-3f) return;
        const float factor = std::clamp(t / mStartT, 0.01f, 100.0f);
        const glm::vec3 pivot = mCentroid;
        const bool uniform = cfg.uniformScale;
        const int dragAxis = mAxis;
        const float snapStep = cfg.snapStep;
        xform = [factor, pivot, uniform, dragAxis, snapStep](
                    const eng::ecs::Transform& pre) {
            eng::ecs::Transform out = pre;
            if (uniform) {
                out.scale = glm::max(glm::vec3(0.01f), pre.scale * factor);
                if (snapStep > 0.0f)
                    for (int i = 0; i < 3; ++i)
                        out.scale[i] = std::max(0.01f, snap(out.scale[i], snapStep));
                out.position = pivot + (pre.position - pivot) * factor;
            } else {
                out.scale[dragAxis] = std::max(0.01f, pre.scale[dragAxis] * factor);
                if (snapStep > 0.0f)
                    out.scale[dragAxis] =
                        std::max(0.01f, snap(out.scale[dragAxis], snapStep));
            }
            return out;
        };
    }

    for (auto& [ent, pre] : mPre) {
        if (!reg.valid(ent) || !reg.all_of<eng::ecs::Transform>(ent)) continue;
        reg.replace<eng::ecs::Transform>(ent, xform(pre));
        reg.emplace_or_replace<eng::ecs::Dirty>(ent);
    }
}

Command GizmoTool::release(entt::registry& reg)
{
    Command out; // null (.apply == nullptr) unless we build a composite
    if (mDragging) {
        std::vector<Command> cmds;
        for (auto& [ent, pre] : mPre) {
            if (!reg.valid(ent) || !reg.all_of<eng::ecs::Transform>(ent)) continue;
            eng::ecs::Transform finalT = reg.get<eng::ecs::Transform>(ent);
            reg.replace<eng::ecs::Transform>(ent, pre); // restore; command re-applies
            reg.emplace_or_replace<eng::ecs::Dirty>(ent);
            cmds.push_back(makeSetTransform(reg, ent, finalT));
        }
        if (!cmds.empty()) out = makeComposite(std::move(cmds));
    }
    mDragging = false;
    mPre.clear();
    return out;
}

} // namespace editor
