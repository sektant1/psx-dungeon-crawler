#include "EditorApp.h"

#include "Commands.h"
#include "InspectorRegistry.h"
#include "Picker.h"

#include "../scene/ByteStream.h"
#include "../scene/ComponentRegistry.h"
#include "../scene/GameComponents.h"
#include "../scene/LayoutToScene.h"
#include "../scene/MapSerializer.h"
#include "../scene/MeshSource.h"

#include "../DungeonGen.h"

#include <memory>

#include <eng/Engine.h>
#include <eng/Input.h>
#include <eng/Renderer.h>
#include <eng/ecs/Components.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <limits>

#include <sys/wait.h> // waitpid (POSIX; editor is Linux-only, uses /proc/self/exe)
#include <unistd.h>   // fork/execl/setenv/_exit

namespace editor {

namespace {
constexpr float kFovDeg = 60.0f;
} // namespace

EditorApp::EditorApp(eng::Engine& engine, std::string assetDir)
    : mEngine(engine),
      mRenderer(engine.renderer()),
      mAssetDir(std::move(assetDir)),
      mBackend(engine.renderer()),
      mScene(mBackend),
      mDoc(mScene.registry(),
           game::SceneGenOptions{4.0f, 3.0f, mAssetDir + "/meshes/tiles/",
                                 mAssetDir + "/meshes/props/"})
{
    mRenderer.enableEditorViewport(mVpW, mVpH);
    mRenderer.setAmbient(glm::vec3(0.35f, 0.36f, 0.40f));
    mRenderer.setBackground({0.09f, 0.10f, 0.12f});
    mRenderer.setCameraClip(0.05f, 200.0f);

    mEngine.debugUi().setMainWindowVisible(false);
    mEngine.debugUi().setVisible(true);
    mEngine.input().setMouseGrab(false);

    mCam.setFlyPosition(glm::vec3(0.0f, 4.0f, 8.0f));
    mCam.setYawPitch(0.0f, -0.4f);

    mPalette.discover(mAssetDir);
    buildMaterialCatalog();

    // Seed a starter scene so the viewport isn't empty: a floor + a light.
    if (!mPalette.meshes().empty()) {
        const std::string& seedMesh = mPalette.meshes().front();
        entt::entity floor =
            mScene.spawnMesh(seedMesh, materialForMesh(seedMesh), glm::vec3(0.0f));
        ensureMeshHandles();    // resolve the .obj before the sync below
        mScene.sync();          // build the node so its gizmo/bounds resolve now
        mSel.set(floor);        // select it so its gizmo is visible immediately
    } else {
        mScene.spawnMarker(glm::vec3(0.0f), "Floor");
    }
    {
        eng::LightDesc key;
        key.type = eng::LightDesc::Type::Directional;
        key.colour = glm::vec3(0.95f, 0.93f, 0.88f);
        mScene.spawnLight(key, glm::vec3(0.0f, 6.0f, 0.0f));
    }

    mEngine.debugUi().addWindow([this] { drawPanels(); });
}

// --------------------------------------------------------------------------

glm::vec3 EditorApp::spawnPosInFrontOfCamera() const
{
    const glm::vec3 fwd = mCam.flyOrientation() * glm::vec3(0.0f, 0.0f, -1.0f);
    return snapToGrid(mCam.flyEye() + fwd * 4.0f);
}

glm::vec3 EditorApp::snapToGrid(glm::vec3 p) const
{
    if (!mGridSnap || mGridSize <= 0.0f) return p;
    return {snap(p.x, mGridSize), snap(p.y, mGridSize), snap(p.z, mGridSize)};
}

bool EditorApp::selectionCentroid(glm::vec3& out)
{
    if (mSel.empty()) return false;
    auto& reg = mScene.registry();
    glm::vec3 acc(0.0f);
    int n = 0;
    for (entt::entity e : mSel.items()) {
        if (reg.valid(e) && reg.all_of<eng::ecs::Transform>(e)) {
            acc += reg.get<eng::ecs::Transform>(e).position;
            ++n;
        }
    }
    if (n == 0) return false;
    out = acc / float(n);
    return true;
}

// --------------------------------------------------------------------------

void EditorApp::buildMaterialCatalog()
{
    // Mesh->material pairing mirrored from game/assets/dungeon_props.toml. Kept
    // as a static table (rather than parsing TOML here) because pulling the
    // tomlplusplus header into this translation unit clashes with how the
    // engine's eng_toml library is compiled and corrupts the heap at startup.
    static const std::pair<const char*, const char*> kProps[] = {
        {"prop_chest.obj", "Game/PropChest"},
        {"prop_barrel_p0.obj", "Game/PropPlanks"},
        {"prop_barrel_p1.obj", "Game/PropBauerhaus"},
        {"prop_crate.obj", "Game/PropMarket"},
        {"prop_vase_p0.obj", "Game/PropTerracotta"},
        {"prop_vase_p1.obj", "Game/PropPlanks"},
        {"prop_jutesack.obj", "Game/PropJute"},
        {"prop_pumpkin.obj", "Game/PropMarketMisc"},
        {"prop_haybale.obj", "Game/PropHay"},
    };
    for (const auto& [mesh, mat] : kProps)
        mMatByMesh[mesh] = mat;
}

std::string EditorApp::materialForMesh(const std::string& objPath) const
{
    const std::string file = std::filesystem::path(objPath).filename().string();
    const auto it = mMatByMesh.find(file);
    if (it != mMatByMesh.end()) return it->second;
    // Tile heuristics for the modular kit meshes not listed as props.
    const auto has = [&](const char* k) { return file.find(k) != std::string::npos; };
    if (has("floor")) return "Game/DungeonFloor";
    if (has("ceiling")) return "Game/DungeonCeiling";
    if (has("wall")) return "Game/DungeonWall";
    if (has("arch") || has("pillar")) return "Game/DungeonTile";
    return "Game/DungeonTile";
}

void EditorApp::ensureMeshHandles()
{
    // Any MeshRenderer whose runtime handle is unresolved (freshly spawned,
    // deserialized on open/duplicate/undo-delete, or generated) gets its .obj
    // loaded from MeshSource and is marked dirty so SceneSync attaches it. The
    // MeshHandle is a runtime id and is never serialized, so this is the single
    // place that turns a MeshSource path into a live mesh -- callers never have
    // to remember to resolve.
    entt::registry& reg = mScene.registry();
    for (auto e : reg.view<eng::ecs::MeshRenderer, mapio::MeshSource>()) {
        auto& mr = reg.get<eng::ecs::MeshRenderer>(e);
        if (mr.mesh.valid()) continue;
        const auto& src = reg.get<mapio::MeshSource>(e);
        if (src.path.empty()) continue;
        mr.mesh = mRenderer.loadObj(src.path);
        reg.emplace_or_replace<eng::ecs::Dirty>(e);
    }
}

void EditorApp::frame(float dt)
{
    handleViewportInput(dt);
    handleShortcuts();
    mRenderer.setEditorCameraPose(mCam.flyEye(), mCam.flyOrientation(), kFovDeg);

    updateGizmoDrag();

    ensureMeshHandles();
    mScene.sync();

    // Overlays: ground grid + selection AABB wire-box + gizmo axis lines.
    // Retained buffer, refilled each frame (no per-frame allocation).
    std::vector<eng::Renderer::DebugLine>& lines = mDebugLines;
    lines.clear();
    // Ground grid: brighter when grid-snap is on, spaced to the snap cell so the
    // snap increment is visible.
    const bool snapping = mGridSnap && mGridSize > 0.0f;
    const glm::vec3 gridCol = snapping ? glm::vec3(0.35f, 0.45f, 0.55f)
                                       : glm::vec3(0.25f, 0.27f, 0.30f);
    const float step = snapping ? mGridSize : 1.0f;
    const int half = 10;
    const float ext = half * step;
    for (int i = -half; i <= half; ++i) {
        const float p = i * step;
        lines.push_back({{p, 0.0f, -ext}, {p, 0.0f, ext}, gridCol});
        lines.push_back({{-ext, 0.0f, p}, {ext, 0.0f, p}, gridCol});
    }

    const auto& reg = mScene.registry();
    entt::entity sel = mSel.primary();
    if (sel != entt::null && reg.valid(sel)) {
        glm::vec3 mn, mx;
        if (pickBounds(sel, mn, mx)) {
            const glm::vec3 col(0.55f, 0.8f, 1.0f);
            const glm::vec3 v[8] = {
                {mn.x,mn.y,mn.z},{mx.x,mn.y,mn.z},{mx.x,mn.y,mx.z},{mn.x,mn.y,mx.z},
                {mn.x,mx.y,mn.z},{mx.x,mx.y,mn.z},{mx.x,mx.y,mx.z},{mn.x,mx.y,mx.z}};
            const int edges[12][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},
                                    {0,4},{1,5},{2,6},{3,7}};
            for (auto& ed : edges) lines.push_back({v[ed[0]], v[ed[1]], col});
        }
    }
    glm::vec3 centroid;
    if (selectionCentroid(centroid)) {
        const float L = 1.5f;
        lines.push_back({centroid, centroid + glm::vec3(L,0,0), {1.0f,0.2f,0.2f}});
        lines.push_back({centroid, centroid + glm::vec3(0,L,0), {0.2f,1.0f,0.2f}});
        lines.push_back({centroid, centroid + glm::vec3(0,0,L), {0.3f,0.4f,1.0f}});
    }

    // Terrain brush cursor: outline the ground cell under the pointer so the
    // painter can see the target tile before clicking.
    if (mTool == Tool::Terrain && mVpHovered && !mLooking &&
        mVpSize.x >= 1.0f && mVpSize.y >= 1.0f) {
        const ImGuiIO& io = ImGui::GetIO();
        const glm::vec2 rel =
            (glm::vec2(io.MousePos.x, io.MousePos.y) - mVpMin) / mVpSize;
        const glm::vec2 ndc(rel.x * 2.0f - 1.0f, 1.0f - rel.y * 2.0f);
        int col, row;
        if (pointerCell(ndc, col, row)) {
            const float c = mDoc.cellSize();
            const float h = c * 0.5f;
            const glm::vec3 ctr(col * c, 0.02f, row * c); // just above the floor
            const glm::vec3 brush(0.95f, 0.85f, 0.25f);
            const glm::vec3 q[4] = {
                {ctr.x - h, ctr.y, ctr.z - h}, {ctr.x + h, ctr.y, ctr.z - h},
                {ctr.x + h, ctr.y, ctr.z + h}, {ctr.x - h, ctr.y, ctr.z + h}};
            for (int i = 0; i < 4; ++i)
                lines.push_back({q[i], q[(i + 1) % 4], brush});
        }
    }

    mRenderer.setDebugLines(lines);
}

void EditorApp::handleViewportInput(float dt)
{
    if (mVpSize.x < 1.0f || mVpSize.y < 1.0f)
        return;
    const ImGuiIO& io = ImGui::GetIO();

    // RMB free-look latch: begin only when pressing over the viewport, then keep
    // capturing look + WASD until RMB releases even if the cursor leaves the
    // image rect (avoids drops from hover flicker while turning).
    if (mLooking && !io.MouseDown[1])
        mLooking = false;
    if (!mLooking && mVpHovered && io.MouseClicked[1])
        mLooking = true;

    if (mLooking) {
        mCam.addYawPitch(-io.MouseDelta.x * 0.005f, -io.MouseDelta.y * 0.005f);
        const float speed = 8.0f * dt;
        glm::vec3 move(0.0f);
        if (ImGui::IsKeyDown(ImGuiKey_W)) move.z -= 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_S)) move.z += 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_A)) move.x -= 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_D)) move.x += 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_E)) move.y += 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_Q)) move.y -= 1.0f;
        if (glm::dot(move, move) > 0.0f)
            mCam.moveLocal(move * speed);
        return; // don't pick/drag while flying
    }

    if (!mVpHovered)
        return; // picking/gizmo/paint only when the viewport is under the cursor

    const glm::vec2 mouse(io.MousePos.x, io.MousePos.y);
    const glm::vec2 rel = (mouse - mVpMin) / mVpSize; // [0,1]
    glm::vec2 ndc(rel.x * 2.0f - 1.0f, 1.0f - rel.y * 2.0f); // y up

    // Terrain tool: LMB paints the tile under the ground-plane cursor (WC3-style
    // brush), held-drag paints across cells. No picking/gizmo in this mode.
    if (mTool == Tool::Terrain) {
        if (io.MouseDown[0])
            paintUnderPointer(ndc);
        else
            mLastPaintCol = mLastPaintRow = -999999; // reset drag de-dupe
        return;
    }

    // LMB click: begin gizmo drag if over an axis, else pick.
    if (io.MouseClicked[0]) {
        // Gizmo hit test: check the three axes from the selection centroid.
        glm::vec3 centroid;
        bool startedDrag = false;
        if (selectionCentroid(centroid)) {
            const Ray ray = screenRay(ndc, mCam.flyEye(), mCam.flyOrientation(),
                                      glm::radians(kFovDeg),
                                      mVpSize.x / mVpSize.y);
            const glm::vec3 axes[3] = {{1,0,0},{0,1,0},{0,0,1}};
            float bestDist = 0.35f; // world-space pick radius near axis
            int bestAxis = -1;
            for (int a = 0; a < 3; ++a) {
                float t = 0.0f;
                if (!closestPointOnAxis(centroid, axes[a], ray, t)) continue;
                if (t < 0.0f || t > 1.6f) continue;
                const glm::vec3 pOnAxis = centroid + axes[a] * t;
                // distance from ray to that point
                const glm::vec3 w = pOnAxis - ray.origin;
                const float proj = glm::dot(w, ray.dir);
                const glm::vec3 closest = ray.origin + ray.dir * proj;
                const float d = glm::length(closest - pOnAxis);
                if (d < bestDist) { bestDist = d; bestAxis = a; }
            }
            if (bestAxis >= 0) {
                entt::entity e = mSel.primary();
                if (e != entt::null && mScene.registry().valid(e)) {
                    mDragging = true;
                    mDragAxis = bestAxis;
                    mPreDrag = mScene.registry().get<eng::ecs::Transform>(e);
                    // Snapshot every selected entity's transform so the drag
                    // moves the whole selection about the gizmo pivot.
                    mDragPre.clear();
                    for (entt::entity s : mSel.items())
                        if (mScene.registry().valid(s) &&
                            mScene.registry().all_of<eng::ecs::Transform>(s))
                            mDragPre.push_back(
                                {s, mScene.registry().get<eng::ecs::Transform>(s)});
                    mDragCentroid = centroid;
                    float t = 0.0f;
                    closestPointOnAxis(centroid, axes[bestAxis], ray, t);
                    mDragStartHit = centroid + axes[bestAxis] * t;
                    mDragStartT = t;
                    // Rotate reference vector: pointer on the plane perpendicular
                    // to the axis through the pivot.
                    glm::vec3 hit;
                    if (rayPlane(ray, centroid, axes[bestAxis], hit))
                        mDragStartVec = hit - centroid;
                    else
                        mDragStartVec = glm::vec3(0.0f);
                    startedDrag = true;
                }
            }
        }
        if (!startedDrag)
            pickAt(ndc, io.KeyCtrl);
    }
}

bool EditorApp::pointerCell(glm::vec2 ndc, int& col, int& row) const
{
    const Ray ray = screenRay(ndc, mCam.flyEye(), mCam.flyOrientation(),
                              glm::radians(kFovDeg), mVpSize.x / mVpSize.y);
    glm::vec3 hit;
    if (!rayPlane(ray, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), hit))
        return false; // looking at/above the horizon
    const float cell = mDoc.cellSize();
    col = int(std::lround(hit.x / cell));
    row = int(std::lround(hit.z / cell));
    return true;
}

void EditorApp::paintUnderPointer(glm::vec2 ndc)
{
    int col, row;
    if (!pointerCell(ndc, col, row))
        return;
    if (col == mLastPaintCol && row == mLastPaintRow)
        return; // still on the same cell within a held drag
    mLastPaintCol = col;
    mLastPaintRow = row;
    // Paint + re-extrude; new terrain meshes resolve on the next
    // ensureMeshHandles() (called from frame() below this).
    mDoc.paintTile(col, row, mBrush);
}

bool EditorApp::pickBounds(entt::entity e, glm::vec3& mn, glm::vec3& mx)
{
    // Prefer the real rendered mesh AABB so large/offset models are clickable;
    // fall back to a fixed box for markers/lights that have no renderable node.
    entt::registry& reg = mScene.registry();
    if (const auto* nr = reg.try_get<eng::ecs::NodeRef>(e)) {
        glm::vec3 c(0.0f);
        float r = 0.0f;
        if (nr->handle.valid() && mRenderer.nodeWorldBounds(nr->handle, c, r) &&
            r > 0.0f) {
            mn = c - glm::vec3(r);
            mx = c + glm::vec3(r);
            return true;
        }
    }
    return mScene.entityBounds(e, mn, mx);
}

void EditorApp::pickAt(glm::vec2 ndc, bool additive)
{
    const Ray ray = screenRay(ndc, mCam.flyEye(), mCam.flyOrientation(),
                              glm::radians(kFovDeg), mVpSize.x / mVpSize.y);
    const auto& reg = mScene.registry();
    entt::entity best = entt::null;
    float bestT = std::numeric_limits<float>::max();
    for (auto e : reg.view<eng::ecs::Transform>()) {
        glm::vec3 mn, mx;
        if (!pickBounds(e, mn, mx)) continue;
        float t = 0.0f;
        if (rayAabb(ray, mn, mx, t) && t < bestT) { bestT = t; best = e; }
    }
    if (additive) mSel.add(best);
    else mSel.set(best);
}

void EditorApp::updateGizmoDrag()
{
    if (!mDragging) return;
    const ImGuiIO& io = ImGui::GetIO();
    entt::registry& reg = mScene.registry();

    if (!io.MouseDown[0]) {
        // Release: one undoable step for the whole selection. Restore each
        // entity's pre-drag transform, then a composite command re-applies the
        // dragged finals (capturing pre-drag as its revert target).
        std::vector<Command> cmds;
        for (auto& [ent, pre] : mDragPre) {
            if (!reg.valid(ent) || !reg.all_of<eng::ecs::Transform>(ent)) continue;
            eng::ecs::Transform finalT = reg.get<eng::ecs::Transform>(ent);
            reg.replace<eng::ecs::Transform>(ent, pre);
            reg.emplace_or_replace<eng::ecs::Dirty>(ent);
            cmds.push_back(makeSetTransform(reg, ent, finalT));
        }
        if (!cmds.empty()) mStack.run(makeComposite(std::move(cmds)));
        mDragging = false;
        mDragPre.clear();
        return;
    }

    // Drag: compute the axis delta once, then map it over every selected
    // entity's pre-drag transform, pivoting on the gizmo centroid.
    const glm::vec2 mouse(io.MousePos.x, io.MousePos.y);
    const glm::vec2 rel = (mouse - mVpMin) / mVpSize;
    const glm::vec2 ndc(rel.x * 2.0f - 1.0f, 1.0f - rel.y * 2.0f);
    const Ray ray = screenRay(ndc, mCam.flyEye(), mCam.flyOrientation(),
                              glm::radians(kFovDeg), mVpSize.x / mVpSize.y);
    const glm::vec3 axes[3] = {{1,0,0},{0,1,0},{0,0,1}};
    const glm::vec3 axis = axes[mDragAxis];

    // Build a per-entity transform map for the active mode.
    std::function<eng::ecs::Transform(const eng::ecs::Transform&)> xform;

    if (mGizmoMode == GizmoMode::Translate) {
        float t = 0.0f;
        if (!closestPointOnAxis(mDragStartHit, axis, ray, t)) return;
        // Snap the PRIMARY's resulting position, then move the group by the
        // same offset so the selection stays rigid and grid-aligned.
        glm::vec3 primPos = mPreDrag.position + axis * t;
        if (mSnapStep > 0.0f) {
            primPos.x = snap(primPos.x, mSnapStep);
            primPos.y = snap(primPos.y, mSnapStep);
            primPos.z = snap(primPos.z, mSnapStep);
        }
        primPos = snapToGrid(primPos); // grid snap overrides when on
        const glm::vec3 offset = primPos - mPreDrag.position;
        xform = [offset](const eng::ecs::Transform& pre) {
            eng::ecs::Transform out = pre;
            out.position = pre.position + offset;
            return out;
        };
    } else if (mGizmoMode == GizmoMode::Rotate) {
        glm::vec3 hit;
        if (!rayPlane(ray, mDragCentroid, axis, hit)) return;
        float ang = signedAngleAround(mDragStartVec, hit - mDragCentroid, axis);
        if (mSnapStep > 0.0f) // treat snap as degrees in Rotate mode
            ang = glm::radians(snap(glm::degrees(ang), mSnapStep));
        const glm::quat q = glm::angleAxis(ang, axis);
        const glm::vec3 pivot = mDragCentroid;
        xform = [q, pivot](const eng::ecs::Transform& pre) {
            eng::ecs::Transform out = pre;
            out.rotation = glm::normalize(q * pre.rotation);
            out.position = pivot + q * (pre.position - pivot);
            return out;
        };
    } else { // Scale
        float t = 0.0f;
        if (!closestPointOnAxis(mDragCentroid, axis, ray, t)) return;
        if (std::abs(mDragStartT) < 1e-3f) return;
        float factor = std::clamp(t / mDragStartT, 0.01f, 100.0f);
        const glm::vec3 pivot = mDragCentroid;
        const bool uniform = mUniformScale;
        const int dragAxis = mDragAxis;
        const float snapStep = mSnapStep;
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
                out.scale[dragAxis] =
                    std::max(0.01f, pre.scale[dragAxis] * factor);
                if (snapStep > 0.0f)
                    out.scale[dragAxis] =
                        std::max(0.01f, snap(out.scale[dragAxis], snapStep));
            }
            return out;
        };
    }

    for (auto& [ent, pre] : mDragPre) {
        if (!reg.valid(ent) || !reg.all_of<eng::ecs::Transform>(ent)) continue;
        reg.replace<eng::ecs::Transform>(ent, xform(pre));
        reg.emplace_or_replace<eng::ecs::Dirty>(ent);
    }
}

// --------------------------------------------------------------------------
// Keyboard shortcuts + selection ops
// --------------------------------------------------------------------------

void EditorApp::handleShortcuts()
{
    const ImGuiIO& io = ImGui::GetIO();
    // Don't steal keys from text fields or while RMB-flying (WASD is movement).
    if (io.WantTextInput || mLooking) return;

    if (ImGui::IsKeyPressed(ImGuiKey_W, false)) mGizmoMode = GizmoMode::Translate;
    if (ImGui::IsKeyPressed(ImGuiKey_E, false)) mGizmoMode = GizmoMode::Rotate;
    if (ImGui::IsKeyPressed(ImGuiKey_R, false)) mGizmoMode = GizmoMode::Scale;
    if (ImGui::IsKeyPressed(ImGuiKey_F, false)) frameSelection();
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) deleteSelection();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) mSel.clear();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) duplicateSelection();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) mStack.undo();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) mStack.redo();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
        saveMap(mMapPath.empty() ? "level.map" : mMapPath);
}

void EditorApp::frameSelection()
{
    glm::vec3 c;
    if (!selectionCentroid(c)) return;
    // Pull the camera back along its current forward so the pivot is centred.
    const glm::vec3 fwd = mCam.flyOrientation() * glm::vec3(0, 0, -1);
    mCam.setFlyPosition(c - fwd * 6.0f);
}

void EditorApp::deleteSelection()
{
    entt::registry& reg = mScene.registry();
    for (entt::entity e : mSel.items())
        if (reg.valid(e)) mStack.run(makeDeleteEntity(reg, e));
    mSel.clear();
}

void EditorApp::duplicateSelection()
{
    entt::registry& reg = mScene.registry();
    const entt::entity src = mSel.primary();
    if (src == entt::null || !reg.valid(src)) return;

    // Snapshot the source entity's components into a byte blob.
    auto blob = std::make_shared<std::vector<uint8_t>>();
    auto pool = std::make_shared<std::vector<std::string>>();
    {
        mapio::ByteWriter w;
        std::vector<const mapio::ComponentType*> present;
        for (const mapio::ComponentType& t : mapio::coreRegistry().types())
            if (t.has(reg, src)) present.push_back(&t);
        w.u16(uint16_t(present.size()));
        for (const mapio::ComponentType* t : present) {
            w.u16(t->stableTypeId);
            t->serialize(reg, src, w);
        }
        *blob = w.bytes();
        *pool = w.pool();
    }

    auto slot = std::make_shared<entt::entity>(entt::null);
    Command c;
    c.apply = [this, blob, pool, slot] {
        entt::registry& r = mScene.registry();
        entt::entity e = r.create();
        mapio::ByteReader rd(blob->data(), blob->size(), *pool);
        const uint16_t n = rd.u16();
        for (uint16_t i = 0; i < n && rd.ok(); ++i) {
            const uint16_t id = rd.u16();
            if (const mapio::ComponentType* t = mapio::coreRegistry().find(id))
                t->deserialize(r, e, rd);
        }
        if (auto* tr = r.try_get<eng::ecs::Transform>(e))
            tr->position = snapToGrid(tr->position + glm::vec3(1.0f, 0.0f, 1.0f));
        r.emplace_or_replace<eng::ecs::Dirty>(e);
        // Mesh handle (if any) is resolved by ensureMeshHandles() next frame.
        *slot = e;
        mSel.set(e);
    };
    c.revert = [this, slot] {
        entt::registry& r = mScene.registry();
        if (r.valid(*slot)) r.destroy(*slot);
    };
    mStack.run(c);
}

// --------------------------------------------------------------------------
// Panels
// --------------------------------------------------------------------------

void EditorApp::drawPanels()
{
    // Full-window dock host so the panels tile instead of overlapping. Pattern
    // mirrors engine EditorUi: a borderless host window carrying the DockSpace,
    // with a one-time default layout built once the real window size arrives.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const ImGuiWindowFlags host =
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;
    ImGui::Begin("##EditorHost", nullptr, host);
    ImGui::PopStyleVar(3);
    const ImGuiID dockId = ImGui::GetID("EditorDockSpace");
    ImGui::DockSpace(dockId, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
    if (!mBuiltLayout && vp->WorkSize.x > 200.0f && vp->WorkSize.y > 200.0f) {
        mBuiltLayout = true;
        ImGui::DockBuilderRemoveNode(dockId);
        ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockId, vp->WorkSize);
        ImGuiID center = dockId, top, left, right, leftBottom;
        top = ImGui::DockBuilderSplitNode(center, ImGuiDir_Up, 0.14f, nullptr, &center);
        left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.16f, nullptr, &center);
        right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.22f, nullptr, &center);
        leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.5f, nullptr, &left);
        ImGui::DockBuilderDockWindow("Editor", top);
        ImGui::DockBuilderDockWindow("Outliner", left);
        ImGui::DockBuilderDockWindow("Palette", leftBottom);
        ImGui::DockBuilderDockWindow("Inspector", right);
        ImGui::DockBuilderDockWindow("Viewport", center);
        ImGui::DockBuilderFinish(dockId);
    }
    ImGui::End();

    drawToolbar();
    drawViewport();
    drawOutliner();
    drawInspector();
    drawPalette();
}

void EditorApp::drawToolbar()
{
    ImGui::Begin("Editor");
    if (ImGui::Button("New")) {
        mPendingDestructive = 1;
        ImGui::OpenPopup("Confirm##editor");
    }
    ImGui::SameLine();
    if (mPathBuf[0] == 0 && !mMapPath.empty()) {
        std::snprintf(mPathBuf, sizeof(mPathBuf), "%s", mMapPath.c_str());
    }
    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputText("path", mPathBuf, sizeof(mPathBuf));
    ImGui::SameLine();
    if (ImGui::Button("Open")) {
        mPendingDestructive = 3;
        ImGui::OpenPopup("Confirm##editor");
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) saveMap(mPathBuf[0] ? mPathBuf : "level.map");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("##seed", &mGenSeed);
    ImGui::SameLine();
    if (ImGui::Button("Generate")) {
        mPendingDestructive = 2;
        ImGui::OpenPopup("Confirm##editor");
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Replace the scene with a BSP dungeon from this seed");

    // Confirm gate for the three scene-destroying actions (they also clear undo).
    if (ImGui::BeginPopupModal("Confirm##editor", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        const char* msg =
            mPendingDestructive == 1 ? "Discard the current scene and start new?"
            : mPendingDestructive == 2 ? "Replace the scene with a generated dungeon?"
                                       : "Discard the current scene and open the file?";
        ImGui::TextUnformatted(msg);
        ImGui::TextDisabled("This clears the undo history and cannot be undone.");
        if (ImGui::Button("OK")) {
            switch (mPendingDestructive) {
                case 1: newScene(); break;
                case 2: generateDungeon(); break;
                case 3: openMap(mPathBuf); break;
                default: break;
            }
            mPendingDestructive = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            mPendingDestructive = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Tool tabs (WC3-style): Doodad places/gizmos entities, Terrain paints tiles.
    ImGui::Separator();
    int tool = int(mTool);
    ImGui::TextUnformatted("Tool:");
    ImGui::SameLine(); ImGui::RadioButton("Doodad", &tool, 0);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Place + gizmo entities (LMB pick/drag)");
    ImGui::SameLine(); ImGui::RadioButton("Terrain", &tool, 1);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Paint the tile under the cursor (LMB, hold to drag)");
    mTool = Tool(tool);

    if (mTool == Tool::Terrain) {
        // Brush palette: one button per paintable tile glyph.
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::TextUnformatted("Brush:");
        struct Tile { char glyph; const char* label; };
        static const Tile kTiles[] = {
            {'.', "Floor"}, {'#', "Wall"}, {'A', "Arch"}, {'L', "Torch"},
            {'S', "Spawn"}, {'X', "Exit"}, {'H', "Chest"}, {'B', "Barrel"},
            {'R', "Crate"}, {'V', "Urn"}, {' ', "Void"},
        };
        for (const Tile& t : kTiles) {
            ImGui::SameLine();
            const bool active = mBrush == t.glyph;
            if (active) ImGui::PushStyleColor(ImGuiCol_Button,
                                              ImVec4(0.20f, 0.45f, 0.65f, 1.0f));
            if (ImGui::Button(t.label)) mBrush = t.glyph;
            if (active) ImGui::PopStyleColor();
        }
    }

    // Gizmo/snap controls only apply to the Doodad tool.
    if (mTool == Tool::Doodad) {
        ImGui::Separator();
        int mode = int(mGizmoMode);
        ImGui::TextUnformatted("Gizmo:");
        ImGui::SameLine(); ImGui::RadioButton("Move (W)", &mode, 0);
        ImGui::SameLine(); ImGui::RadioButton("Rotate (E)", &mode, 1);
        ImGui::SameLine(); ImGui::RadioButton("Scale (R)", &mode, 2);
        mGizmoMode = GizmoMode(mode);
        ImGui::SameLine();
        ImGui::Checkbox("Proportional", &mUniformScale);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Scale drag affects all axes uniformly");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::DragFloat("Snap", &mSnapStep, 0.05f, 0.0f, 90.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Gizmo increment. 0 = free. Units for Move/Scale, "
                              "degrees for Rotate. For Move, the Grid below "
                              "overrides this when enabled.");
        ImGui::SameLine();
        ImGui::Checkbox("Grid", &mGridSnap);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Snap placement + Move to a fixed world grid");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        ImGui::DragFloat("Cell", &mGridSize, 0.05f, 0.05f, 32.0f, "%.2f");
    }

    ImGui::Separator();
    if (!mStack.canUndo()) ImGui::BeginDisabled();
    if (ImGui::Button("Undo")) mStack.undo();
    if (!mStack.canUndo()) ImGui::EndDisabled();
    ImGui::SameLine();
    if (!mStack.canRedo()) ImGui::BeginDisabled();
    if (ImGui::Button("Redo")) mStack.redo();
    if (!mStack.canRedo()) ImGui::EndDisabled();
    ImGui::SameLine();
    const bool hasSel = mSel.primary() != entt::null;
    if (!hasSel) ImGui::BeginDisabled();
    if (ImGui::Button("Duplicate")) duplicateSelection();
    ImGui::SameLine();
    if (ImGui::Button("Delete")) deleteSelection();
    ImGui::SameLine();
    if (ImGui::Button("Frame")) frameSelection();
    if (!hasSel) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Play")) launchGame();

    // Status line: selection + control hints.
    const entt::registry& reg = mScene.registry();
    const entt::entity sel = mSel.primary();
    std::string selName = "none";
    if (sel != entt::null && reg.valid(sel) && reg.all_of<eng::ecs::Name>(sel))
        selName = reg.get<eng::ecs::Name>(sel).value;
    ImGui::TextDisabled(
        "Selected: %s  |  RMB+WASD fly, LMB pick / drag axis, F frame, "
        "Del delete, Ctrl+D dup, Ctrl+Z/Y undo",
        selName.c_str());

    ImGui::End();
}

void EditorApp::drawViewport()
{
    ImGui::Begin("Viewport");
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const int w = std::max(32, int(avail.x));
    const int h = std::max(32, int(avail.y));
    if (w != mVpW || h != mVpH) {
        mVpW = w; mVpH = h;
        mRenderer.resizeEditorViewport(mVpW, mVpH);
    }
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Image((ImTextureID)mRenderer.editorViewportTextureId(),
                 ImVec2(float(mVpW), float(mVpH)));
    mVpMin = glm::vec2(pos.x, pos.y);
    mVpSize = glm::vec2(float(mVpW), float(mVpH));
    mVpHovered = ImGui::IsItemHovered();
    ImGui::End();
}

void EditorApp::drawOutliner()
{
    ImGui::Begin("Outliner");
    auto& reg = mScene.registry();
    // List every scene entity (anything with a Transform), not only named ones,
    // so lights/markers/generated geometry are always selectable here.
    for (auto e : reg.view<eng::ecs::Transform>()) {
        const auto* nm = reg.try_get<eng::ecs::Name>(e);
        const bool selected = (mSel.primary() == e);
        char label[256];
        std::snprintf(label, sizeof(label), "%s##%u",
                      (nm && !nm->value.empty())
                          ? nm->value.c_str()
                          : "(entity)",
                      unsigned(entt::to_integral(e)));
        if (ImGui::Selectable(label, selected))
            mSel.set(e);
    }
    ImGui::End();
}

void EditorApp::drawInspector()
{
    ImGui::Begin("Inspector");
    entt::entity e = mSel.primary();
    auto& reg = mScene.registry();
    if (e == entt::null || !reg.valid(e)) {
        ImGui::TextDisabled("(no selection)");
        ImGui::End();
        return;
    }
    const auto& core = mapio::coreRegistry();
    const auto& insp = inspectorRegistry();
    for (const auto& t : core.types()) {
        if (!t.has || !t.has(reg, e)) continue;
        ImGui::PushID(t.stableTypeId);
        const bool open = ImGui::CollapsingHeader(t.name, ImGuiTreeNodeFlags_DefaultOpen);
        // Remove button, right-aligned. Transform is required (pick/gizmo/sync
        // all depend on it), so it can't be removed.
        const bool removable = t.remove && std::string(t.name) != "Transform";
        if (removable) {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 4.0f);
            if (ImGui::SmallButton("x")) {
                t.remove(reg, e);
                reg.emplace_or_replace<eng::ecs::Dirty>(e);
                ImGui::PopID();
                continue;
            }
        }
        if (open) {
            const auto* entry = insp.find(t.stableTypeId);
            if (entry && entry->draw)
                entry->draw(reg, e);
            else
                ImGui::TextDisabled("(no inspector)");
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    if (ImGui::BeginCombo("Add Component", "...")) {
        for (const auto& t : core.types()) {
            if (t.has && t.has(reg, e)) continue;
            if (!t.addDefault) continue;
            if (ImGui::Selectable(t.name)) {
                t.addDefault(reg, e);
                reg.emplace_or_replace<eng::ecs::Dirty>(e);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::End();
}

void EditorApp::drawPalette()
{
    Palette::Callbacks cbs;
    cbs.spawnMesh = [this](const std::string& path) {
        entt::entity e = mScene.spawnMesh(path, materialForMesh(path),
                                          spawnPosInFrontOfCamera());
        mSel.set(e); // ensureMeshHandles() resolves the .obj next frame
    };
    cbs.spawnLight = [this] {
        eng::LightDesc d;
        d.type = eng::LightDesc::Type::Point;
        d.colour = glm::vec3(1.0f);
        d.range = 12.0f;
        mSel.set(mScene.spawnLight(d, spawnPosInFrontOfCamera()));
    };
    cbs.spawnPlayerSpawn = [this] {
        entt::entity e = mScene.spawnMarker(spawnPosInFrontOfCamera(), "PlayerSpawn");
        mScene.registry().emplace<game::PlayerSpawn>(e);
        mSel.set(e);
    };
    cbs.spawnExit = [this] {
        entt::entity e = mScene.spawnMarker(spawnPosInFrontOfCamera(), "Exit");
        mScene.registry().emplace<game::Exit>(e);
        mSel.set(e);
    };
    cbs.spawnEnemy = [this] {
        entt::entity e = mScene.spawnMarker(spawnPosInFrontOfCamera(), "Enemy");
        mScene.registry().emplace<game::EnemySpawn>(e);
        mSel.set(e);
    };
    cbs.spawnPickup = [this] {
        entt::entity e = mScene.spawnMarker(spawnPosInFrontOfCamera(), "Pickup");
        mScene.registry().emplace<game::Pickup>(e);
        mSel.set(e);
    };
    cbs.spawnTrigger = [this] {
        entt::entity e = mScene.spawnMarker(spawnPosInFrontOfCamera(), "Trigger");
        mScene.registry().emplace<game::Trigger>(e);
        mSel.set(e);
    };
    mPalette.draw(cbs);
}

// --------------------------------------------------------------------------

void EditorApp::newScene()
{
    mScene.registry().clear();
    mDoc.replaceLayout({}); // clear the terrain grid too (empty = no terrain)
    mStack.clear();
    mSel.clear();
}

void EditorApp::generateDungeon()
{
    mSel.clear();
    mStack.clear();

    gen::Layout layout = gen::generate(uint32_t(mGenSeed < 0 ? 0 : mGenSeed));
    if (!layout.valid()) return;

    // Load the generated grid into the terrain layer; EditorDocument extrudes it
    // (re-extrude destroys only prior terrain, so any doodads survive). This is
    // also the seed a subsequent brush edit paints on top of.
    mDoc.replaceLayout(layout.rows());
    ensureMeshHandles(); // resolve the extruded terrain meshes, then sync
    mScene.sync();
}

void EditorApp::saveMap(const std::string& path)
{
    if (path.empty()) return;
    mapio::writeMap(path, mScene.registry(), mapio::coreRegistry());
    mMapPath = path;
}

void EditorApp::openMap(const std::string& path)
{
    if (path.empty()) return;
    mScene.registry().clear();
    mapio::readMap(path, mScene.registry(), mapio::coreRegistry());
    // Mark everything dirty so SceneSync attaches nodes/lights; mesh handles are
    // resolved by ensureMeshHandles() on the next frame.
    auto& reg = mScene.registry();
    for (auto e : reg.view<eng::ecs::Transform>())
        reg.emplace_or_replace<eng::ecs::Dirty>(e);
    mStack.clear();
    mSel.clear();
    mMapPath = path;
}

void EditorApp::launchGame()
{
    // Save the current scene first so Play always runs what's on screen, not a
    // stale or empty last-saved path.
    const std::string path =
        !mMapPath.empty() ? mMapPath
                          : (mPathBuf[0] ? std::string(mPathBuf) : "level.map");
    saveMap(path); // sets mMapPath
    if (mMapPath.empty()) return;

    std::string exeDir = ".";
    std::error_code ec;
    const auto self = std::filesystem::canonical("/proc/self/exe", ec);
    if (!ec) exeDir = self.parent_path().string();
    const std::string gameExe = exeDir + "/game";

    // Spawn via fork+exec with an argv array -- no shell, so the map path (from
    // the editable text field) can never be interpreted as a command. The
    // grandchild is reparented to init and auto-reaped (double fork).
    const pid_t pid = ::fork();
    if (pid == 0) {
        if (::fork() == 0) {
            ::setenv("SDL_VIDEODRIVER", "x11", 1);
            ::execl(gameExe.c_str(), gameExe.c_str(), mMapPath.c_str(),
                    static_cast<char*>(nullptr));
            ::_exit(127); // exec failed
        }
        ::_exit(0); // intermediate child exits immediately
    }
    if (pid > 0) {
        int status = 0;
        ::waitpid(pid, &status, 0); // reap the intermediate child
    }
}

} // namespace editor
