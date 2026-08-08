#include "DebugOverlay.h"

#include "GameAssets.h"
#include "GameContext.h"
#include "LiveLevel.h"
#include "PlayerSystem.h"
#include "ViewmodelMotion.h"
#include "combat/CombatComponents.h"
#include "combat/FeelComponents.h"
#include "enemy/EnemyLibrary.h"
#include "enemy/EnemySave.h"
#include "enemy/EnemySpawner.h"
#include "enemy/EnemySystem.h"
#include "audio/GameAudio.h"

#include <eng/Renderer.h>
#include <eng/particles/ParticleLibrary.h>

#include <imgui.h>
#include <ImGuizmo.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>

namespace game {

namespace {

bool section(const char* label)
{
    return ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
}

} // namespace

void DebugPanels::install(eng::DebugTools& console)
{
    // Gameplay tuning docks together at the bottom. Portal and VFX are the
    // engine's own panels -- they tune engine shaders -- so this only tells
    // them which materials the game has and what its shipped values are.
    console.addPanel(
        "Combat", [this] { drawCombatTab(); }, eng::PanelGroup::Gameplay);
    // World group, so it docks beside the engine's Player tab: FOV, look
    // sensitivity and hand placement are one tuning session, and splitting
    // them across the window made every adjustment a hunt.
    console.addPanel(
        "Viewmodel", [this] { drawViewmodelTab(); }, eng::PanelGroup::World);
    // World group with the engine's Player tab and the Viewmodel one: where
    // the view sits, what it is framed at and where the hands are is one
    // tuning session.
    console.addPanel(
        "Camera", [this] { drawCameraTab(); }, eng::PanelGroup::World);
    console.addPanel(
        "Feel", [this] { drawFeelTab(); }, eng::PanelGroup::Gameplay);
    console.addPanel(
        "Enemies", [this] { drawEnemiesTab(); }, eng::PanelGroup::Gameplay);
    console.addPanel(
        "Audio", [this] { drawAudioTab(); }, eng::PanelGroup::Content);

    // Content, beside the shader panels: an effect is authored against the
    // bloom and grade settings that sit in the same dock group.
    mParticlePanel.install(console, eng::PanelGroup::Content);

    // World group: a clip drives an entity in the scene, so it is read beside
    // the player and camera tabs rather than beside the shader ones.
    mClipPanel.install(console, eng::PanelGroup::World);

    // The ascent profile differs from the descent one only in palette and in
    // running the flow the other way; everything else is the shared default.
    eng::PortalTuning up;
    up.dark = {0.015f, 0.04f, 0.15f, 1.0f};
    up.mid = {0.08f, 0.58f, 1.10f, 1.0f};
    up.bright = {0.32f, 1.15f, 1.80f, 1.0f};
    up.core = {1.05f, 1.85f, 2.60f, 1.0f};
    up.flowSpeed = -0.26f;
    up.swirlSpeed = -0.07f;
    up.glowColour = {0.45f, 1.55f, 2.40f, 1.0f};
    up.glowStrength = 1.00f;
    mDressing[1].lightColour = {0.42f, 1.60f, 2.35f};

    mSurfaces.addPortal("Descent  (Game/Vfx/PortalDown)", "Game/Vfx/PortalDown", {},
                        "slime_stylized.png");
    mSurfaces.addPortal("Ascent  (Game/Vfx/PortalUp)", "Game/Vfx/PortalUp", up,
                        "water_stylized.png");

    // Toxic slime differs from water in palette, in crossing its two layers the
    // other way, and in being the one liquid that glows.
    eng::LiquidTuning slime;
    slime.dark = {0.025f, 0.14f, 0.015f, 1.0f};
    slime.mid = {0.12f, 0.95f, 0.025f, 1.0f};
    slime.bright = {0.78f, 1.38f, 0.08f, 1.0f};
    slime.flowA = {-0.035f, 0.055f};
    slime.flowB = {0.045f, -0.020f};
    slime.emission = 0.20f;

    mSurfaces.addLiquid("Water  (Game/Vfx/Water)", "Game/Vfx/Water");
    mSurfaces.addLiquid("Toxic Slime  (Game/Vfx/ToxicSlime)",
                        "Game/Vfx/ToxicSlime", slime);
    mSurfaces.addLava("Lava  (Game/Vfx/Lava)", "Game/Vfx/Lava");

    // The bloom mirror on the Portal tab starts on the profile the game
    // actually boots in, read from the profile itself rather than copied by
    // hand -- a hand-copied mirror is one retune away from lying.
    const eng::RenderPresetBloom boot =
        eng::renderPresetBloom(eng::kDefaultRenderPreset);
    mSurfaces.setBloom({boot.enabled, boot.threshold, boot.intensity});
    mSurfaces.setPortalDressing([this](int idx) { drawPortalDressing(idx); });
    mSurfaces.install(console);
}

void DebugPanels::drawCombatTab()
{
    if (!mCur.playerSystem) {
        ImGui::TextDisabled("Weapon definitions unavailable.");
        return;
    }
    for (PlayerWeaponDef& weapon : mCur.playerSystem->weaponDefinitions()) {
        ImGui::PushID(weapon.id.c_str());
        if (section(weapon.displayName.c_str())) {
            ImGui::SliderFloat("Fire interval", &weapon.fireInterval, 0.04f,
                               1.5f);
            ImGui::SliderFloat("ARC cost", &weapon.arcCost, 0.0f, 40.0f);
            ImGui::SliderInt("Projectile count", &weapon.projectileCount, 1, 8);
            ImGui::SliderFloat("Spread degrees", &weapon.spreadDegrees, 0.0f,
                               90.0f);
            ImGui::SliderFloat("Projectile speed", &weapon.projectile.speed,
                               5.0f, 140.0f);
            ImGui::SliderFloat("Projectile radius", &weapon.projectile.radius,
                               0.01f, 0.3f);
        }
        ImGui::PopID();
    }
}

// ------------------------------------------------------------- Viewmodel --
// The tuning surface for first-person presentation: where the camera sits,
// where the hands sit in front of it, and how hard each procedural layer
// pushes them around. Everything here writes straight into live data --
// eng::FpsController for the camera, ViewmodelRig for the shared rig,
// the selected WeaponViewmodelDef for the per-weapon lean -- so there is no
// apply step, and no separate copy of the values to drift out of sync.
//
// The authored files stay the source of truth: a session ends by copying the
// TOML block out of here and pasting it back, exactly like the engine's
// Animation tab.
namespace {

// `[player.movement]`, formatted to paste straight back into game.toml.
//
// The same edit-live-then-paste loop the viewmodel rig uses (viewmodelRigToml),
// and deliberately the same shape: the panel does not write config files, it
// hands you the block. `move_speed` is not emitted because it is not in that
// section -- it is `[player] speed`, and printing it here would invite pasting
// a duplicate key that TOML rejects.
std::string movementTuningToml(const eng::MovementTuning& m)
{
    char buffer[768];
    std::snprintf(buffer, sizeof(buffer),
                  "[player.movement]\n"
                  "ground_acceleration = %.1f\n"
                  "ground_friction = %.1f\n"
                  "air_acceleration = %.1f\n"
                  "jump_velocity = %.2f\n"
                  "gravity = %.1f\n"
                  "sprint_multiplier = %.2f\n"
                  "walk_multiplier = %.2f\n"
                  "crouch_multiplier = %.2f\n"
                  "coyote_time = %.3f\n"
                  "jump_buffer_time = %.3f\n",
                  m.groundAcceleration, m.groundFriction, m.airAcceleration,
                  m.jumpVelocity, m.gravity, m.sprintMultiplier,
                  m.walkMultiplier, m.crouchMultiplier, m.coyoteTime,
                  m.jumpBufferTime);
    return buffer;
}

// A drag row labelled in the axes a first-person artist thinks in, rather than
// x/y/z, which nobody can map to "further right" without guessing.
bool axisDrag(const char* label, glm::vec3& value, float speed, float min,
              float max, const char* a, const char* b, const char* c)
{
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    const float width = (ImGui::GetContentRegionAvail().x - 2.0f * ImGui::GetStyle().ItemSpacing.x) / 3.0f;
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    changed |= ImGui::DragFloat(a, &value.x, speed, min, max, "%.3f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(width);
    changed |= ImGui::DragFloat(b, &value.y, speed, min, max, "%.3f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(width);
    changed |= ImGui::DragFloat(c, &value.z, speed, min, max, "%.3f");
    ImGui::PopID();
    return changed;
}

void copyButton(const char* label, const std::string& text, const char* hint)
{
    if (ImGui::Button(label))
        ImGui::SetClipboardText(text.c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", hint);
}

} // namespace

void DebugPanels::drawCameraTab()
{
    PlayerSystem* player = mCur.playerSystem;
    if (!player || !mCur.context) {
        ImGui::TextDisabled("Player camera unavailable.");
        return;
    }
    GameContext& ctx = *mCur.context;

    const bool third = player->cameraMode() == CameraMode::ThirdPerson;
    ImGui::Text("mode: %s", third ? "third person" : "first person");
    ImGui::SameLine();
    if (ImGui::SmallButton(third ? "switch to first" : "switch to third")) {
        player->setCameraMode(ctx, third ? CameraMode::FirstPerson
                                         : CameraMode::ThirdPerson);
    }
    ImGui::TextDisabled("F6 does the same thing in game.");

    if (section("Lock-on")) {
        const LockOnSystem& lock = player->lockOn();
        LockOnTuning& tuning = player->lockOn().tuning();
        if (lock.locked())
            ImGui::Text("holding target %d", lock.targetId());
        else
            ImGui::TextDisabled("no target  (Q / middle mouse)");
        ImGui::SliderFloat("acquire range", &tuning.acquireRange, 2.0f, 40.0f,
                           "%.1f m");
        ImGui::SliderFloat("break range", &tuning.breakRange, 2.0f, 60.0f,
                           "%.1f m");
        ImGui::SliderFloat("acquire cone", &tuning.acquireConeDegrees, 5.0f,
                           120.0f, "%.0f deg");
        ImGui::SliderFloat("switch flick", &tuning.switchThresholdPixels, 20.0f,
                           600.0f, "%.0f px");
        ImGui::SliderFloat("occlusion grace", &tuning.occlusionGrace, 0.0f, 3.0f,
                           "%.2f s");
        ImGui::TextDisabled("break range above acquire range is the hysteresis\n"
                            "that stops a lock flickering at the limit");
    }

    if (section("Third person")) {
        // Edited in place and pushed straight back at the rig, so a drag is
        // visible on the frame it happens -- which is the only way a camera can
        // be tuned at all.
        eng::ecs::ThirdPersonCamera tuning = player->cameraTuning();
        bool edited = false;
        edited |= ImGui::SliderFloat("distance", &tuning.distance, 0.5f, 12.0f,
                                     "%.2f m");
        edited |= ImGui::SliderFloat("pivot height", &tuning.pivotHeight, 0.0f,
                                     3.0f, "%.2f m");
        edited |= ImGui::SliderFloat("shoulder", &tuning.shoulderOffset, -2.0f,
                                     2.0f, "%.2f m");
        edited |= ImGui::SliderFloat("fov", &tuning.fovDegrees, 40.0f, 130.0f,
                                     "%.1f deg");
        ImGui::SeparatorText("follow");
        edited |= ImGui::SliderFloat("horizontal", &tuning.followRate, 1.0f,
                                     60.0f, "%.1f /s");
        edited |= ImGui::SliderFloat("vertical", &tuning.followRateVertical,
                                     1.0f, 60.0f, "%.1f /s");
        edited |= ImGui::SliderFloat("turn rate", &tuning.turnRateDegrees, 90.0f,
                                     2000.0f, "%.0f deg/s");
        ImGui::SeparatorText("spring arm");
        edited |= ImGui::SliderFloat("radius", &tuning.collisionRadius, 0.0f,
                                     1.0f, "%.2f m");
        edited |= ImGui::SliderFloat("push out", &tuning.pushOutSpeed, 0.5f,
                                     30.0f, "%.1f m/s");
        edited |= ImGui::SliderFloat("minimum", &tuning.minDistance, 0.1f, 4.0f,
                                     "%.2f m");
        ImGui::SeparatorText("lock framing");
        edited |= ImGui::SliderFloat("bias", &tuning.lockFramingBias, 0.0f, 1.0f,
                                     "%.2f");
        edited |= ImGui::SliderFloat("blend rate", &tuning.lockBlendRate, 1.0f,
                                     30.0f, "%.1f /s");
        edited |= ImGui::SliderFloat("lock pitch", &tuning.lockPitchDegrees,
                                     -45.0f, 15.0f, "%.1f deg");
        edited |= ImGui::SliderFloat("distance boost", &tuning.lockDistanceBoost,
                                     0.0f, 6.0f, "%.2f m");
        if (edited)
            player->setCameraTuning(tuning);
        if (!third)
            ImGui::TextDisabled("first person is running -- these apply the\n"
                                "moment you switch");
    }

    if (section("First person feel")) {
        eng::FirstPersonCameraRig::Tuning& feel =
            player->controller().firstPersonRig().tuning();
        ImGui::SliderFloat("step smoothing", &feel.stepSmoothRate, 1.0f, 40.0f,
                           "%.1f /s");
        ImGui::SliderFloat("max step", &feel.maxStepSmooth, 0.0f, 1.0f,
                           "%.2f m");
        ImGui::SliderFloat("landing dip", &feel.landingDipPerSpeed, 0.0f, 0.05f,
                           "%.4f m per m/s");
        ImGui::SliderFloat("dip cap", &feel.landingDipMax, 0.0f, 0.4f, "%.3f m");
        ImGui::SliderFloat("dip recovery", &feel.landingRecovery, 1.0f, 30.0f,
                           "%.1f /s");
        ImGui::SliderFloat("strafe lean", &feel.leanDegrees, 0.0f, 8.0f,
                           "%.2f deg");
        ImGui::SliderFloat("lean rate", &feel.leanRate, 1.0f, 30.0f, "%.1f /s");
        ImGui::TextDisabled("stair smoothing is the one that matters here:\n"
                            "the capsule steps instantly, the eye should not");
    }
}

void DebugPanels::drawViewmodelTab()
{
    PlayerSystem* player = mCur.playerSystem;
    if (!player || !mCur.renderer) {
        ImGui::TextDisabled("Player viewmodel unavailable.");
        return;
    }

    eng::FpsController& camera = player->controller();
    ViewmodelRig& rig = player->viewmodelRig();
    const ViewmodelMotion& motion = player->hands().motion();
    const PlayerWeaponDef* selected = player->selectedWeapon();

    // Live readout first: every slider below is judged against these, and
    // reading speed/grounded off the HUD while dragging is not possible.
    ImGui::Text("%s", selected ? selected->displayName.c_str() : "(no weapon)");
    ImGui::SameLine();
    ImGui::TextDisabled(player->hands().valid() ? "| rig: skinned hands"
                                                : "| rig: no cooked skeleton");
    ImGui::Text("Speed %.2f m/s   %s   recoil %.2f   sway %+.3f/%+.3f",
                camera.horizontalSpeed(), camera.grounded() ? "grounded" : "airborne",
                motion.recoil(), motion.swayOffset().x, motion.swayOffset().y);
    ImGui::Separator();

    bool feelChanged = false;

    if (section("Camera")) {
        ImGui::PushID("cam");
        float fov = camera.baseFov();
        if (ImGui::SliderFloat("Base FOV", &fov, 50.0f, 120.0f, "%.1f deg"))
            camera.setBaseFov(fov);
        ImGui::SliderFloat("Sprint FOV kick", &camera.sprintFovKick(), 0.0f,
                           20.0f, "%.1f deg");
        ImGui::SliderFloat("Mouse sensitivity", &camera.sensitivity(), 0.0002f,
                           0.01f, "%.4f rad/px");
        ImGui::SliderFloat("Head bob", &camera.bobAmount(), 0.0f, 0.12f,
                           "%.3f m");
        ImGui::SliderFloat("Head bob speed", &camera.bobSpeed(), 0.0f, 20.0f);
        ImGui::TextDisabled("Camera moves subtly; the viewmodel moves loudly.");
        ImGui::PopID();
    }

    if (section("Movement")) {
        ImGui::PushID("move");
        // Edited in place: FpsController reads its tuning every simulate(), so
        // a drag is felt on the next step with no respawn. The struct is
        // validated on the way IN from data (setMovementTuning); a slider
        // cannot produce a non-finite value, and clamping the ranges here is
        // what keeps it that way.
        eng::MovementTuning& move = camera.movementTuning();
        ImGui::SliderFloat("Move speed", &move.moveSpeed, 1.0f, 20.0f,
                           "%.2f m/s");
        ImGui::SliderFloat("Ground accel", &move.groundAcceleration, 5.0f,
                           200.0f, "%.0f m/s2");
        ImGui::SliderFloat("Ground friction", &move.groundFriction, 5.0f,
                           200.0f, "%.0f m/s2");
        ImGui::SliderFloat("Air accel", &move.airAcceleration, 0.0f, 80.0f,
                           "%.0f m/s2");
        ImGui::SliderFloat("Jump velocity", &move.jumpVelocity, 1.0f, 15.0f,
                           "%.2f m/s");
        ImGui::SliderFloat("Sprint x", &move.sprintMultiplier, 1.0f, 3.0f,
                           "%.2f");
        ImGui::SliderFloat("Crouch x", &move.crouchMultiplier, 0.1f, 1.0f,
                           "%.2f");
        ImGui::SliderFloat("Coyote time", &move.coyoteTime, 0.0f, 0.4f,
                           "%.3f s");
        ImGui::SliderFloat("Jump buffer", &move.jumpBufferTime, 0.0f, 0.4f,
                           "%.3f s");
        // The two numbers a designer actually reasons about, derived rather
        // than typed: how long to reach full speed, and how high the jump goes
        // against the world's gravity. Both change as the sliders move.
        const float toSpeed = move.groundAcceleration > 0.0f
                                  ? move.moveSpeed / move.groundAcceleration
                                  : 0.0f;
        // The world's gravity, not the tuning's: `gravity` in MovementTuning is
        // only the no-physics fallback, and quoting it here would print an
        // apex the live game does not have.
        const float gravity = mCur.context ? -mCur.context->physics.gravityY()
                                           : move.gravity;
        const float apex = gravity > 0.0f
                               ? (move.jumpVelocity * move.jumpVelocity) /
                                     (2.0f * gravity)
                               : 0.0f;
        ImGui::TextDisabled("%.0f ms to full speed   |   jump apex %.2f m",
                            toSpeed * 1000.0f, apex);
        ImGui::TextDisabled("Air is clamped, not Quake-like: no strafe-jump "
                            "acceleration.");
        if (ImGui::SmallButton("Copy [player.movement]"))
            ImGui::SetClipboardText(movementTuningToml(move).c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(paste into assets/config/game.toml)");
        ImGui::PopID();
    }

    if (section("Rig socket")) {
        ImGui::PushID("socket");
        ImGui::TextDisabled("Camera space: right / up / forward (-z is ahead).");
        axisDrag("Offset (m)", rig.offset, 0.005f, -3.0f, 3.0f, "R##ox", "U##oy",
                 "F##oz");
        axisDrag("Rotation (deg)", rig.rotation, 0.25f, -360.0f, 360.0f,
                 "P##rx", "Y##ry", "R##rz");
        ImGui::SliderFloat("Scale", &rig.scale, 0.05f, 2.0f, "%.3f");
        ImGui::Checkbox("Motion enabled", &rig.motionEnabled);
        ImGui::SameLine();
        ImGui::TextDisabled("(off = frozen pose for authoring)");

        // Framing presets: the three placements this rig is actually authored
        // against, so a session starts from a known pose instead of a drift.
        // The shipped framing, restated rather than read from the struct so
        // "Centered" keeps meaning the pose it names even while game.toml is
        // being edited live above it.
        if (ImGui::Button("Centered")) {
            rig.offset = {0.0f, -0.855f, -0.08f};
            rig.rotation = {0.0f, 180.0f, 0.0f};
            rig.scale = 0.50f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Right hand")) {
            rig.offset = {0.16f, -0.92f, -0.70f};
            rig.rotation = {0.0f, 172.0f, 0.0f};
            rig.scale = 0.52f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Low & wide")) {
            rig.offset = {0.0f, -1.10f, -0.62f};
            rig.rotation = {-4.0f, 180.0f, 0.0f};
            rig.scale = 0.60f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload game.toml")) {
            ViewmodelRig fromFile;
            if (loadViewmodelRig(assetPath("config/game.toml"), fromFile))
                rig = fromFile;
        }

        // Persist. The tuning loop was edit-live-then-paste-by-hand, which
        // means every session that ended without the paste threw the tuning
        // away -- and a placement dialled in over ten minutes of walking around
        // is exactly the thing you forget to copy out.
        //
        // It writes the section key by key, so the file's comments and
        // everything else in it survive.
        if (ImGui::Button("Save to game.toml")) {
            mViewmodelSaved =
                saveViewmodelRig(assetPath("config/game.toml"), rig);
            mViewmodelSaveNoted = 3.0f;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("write this rig into [player_viewmodel]; it "
                              "becomes the framing every launch starts at");
        if (mViewmodelSaveNoted > 0.0f) {
            mViewmodelSaveNoted -= ImGui::GetIO().DeltaTime;
            ImGui::SameLine();
            if (mViewmodelSaved)
                ImGui::TextDisabled("saved");
            else
                ImGui::TextColored(ImVec4(0.89f, 0.42f, 0.33f, 1.0f),
                                   "save failed -- see the log");
        }
        ImGui::PopID();
    }

    if (section("Motion layers")) {
        ImGui::PushID("layers");
        ImGui::TextDisabled("Multipliers over the per-weapon numbers below.");
        ImGui::SliderFloat("Bob scale", &rig.bobScale, 0.0f, 4.0f);
        ImGui::SliderFloat("Sway scale", &rig.swayScale, 0.0f, 4.0f);
        ImGui::SliderFloat("Recoil scale", &rig.recoilScale, 0.0f, 4.0f);
        ImGui::SeparatorText("Bob");
        ImGui::SliderFloat("Reference speed", &rig.bobReferenceSpeed, 1.0f,
                           14.0f, "%.1f m/s");
        ImGui::SliderFloat("Roll", &rig.bobRollDegrees, 0.0f, 12.0f, "%.2f deg");
        ImGui::SeparatorText("Look sway");
        ImGui::SliderFloat("Return speed", &rig.swayReturn, 0.0f, 30.0f);
        ImGui::SliderFloat("Max offset", &rig.swayMax, 0.0f, 0.25f, "%.3f m");
        ImGui::SliderFloat("Sway roll", &rig.swayRollDegrees, 0.0f, 15.0f,
                           "%.2f deg");
        ImGui::SeparatorText("Landing");
        ImGui::SliderFloat("Dip", &rig.landingDip, 0.0f, 0.3f, "%.3f m");
        ImGui::SliderFloat("Recovery", &rig.landingRecovery, 0.5f, 30.0f);
        ImGui::PopID();
    }

    // Per-weapon lean and kick. Edited on the definition itself, so switching
    // weapons and switching back keeps the edit for the rest of the session.
    if (section("Weapon presentation")) {
        ImGui::PushID("weapon");
        std::vector<PlayerWeaponDef>& definitions = player->weaponDefinitions();
        if (mViewmodelWeapon >= int(definitions.size()))
            mViewmodelWeapon = 0;
        if (definitions.empty()) {
            ImGui::TextDisabled("No weapon definitions loaded.");
            ImGui::PopID();
            return;
        }
        // Defaults to whatever is equipped, so the panel follows 1/2/3 unless
        // the viewer deliberately picks another row.
        if (mViewmodelFollowsEquipped)
            mViewmodelWeapon = player->weapon();
        if (ImGui::BeginCombo("Definition",
                              definitions[std::size_t(mViewmodelWeapon)]
                                  .displayName.c_str())) {
            for (int i = 0; i < int(definitions.size()); ++i)
                if (ImGui::Selectable(definitions[std::size_t(i)].displayName.c_str(),
                                      i == mViewmodelWeapon)) {
                    mViewmodelWeapon = i;
                    mViewmodelFollowsEquipped = false;
                }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Follow equipped", &mViewmodelFollowsEquipped);

        WeaponViewmodelDef& v =
            definitions[std::size_t(mViewmodelWeapon)].viewmodel;
        ImGui::SeparatorText("Lean (added to the rig socket)");
        feelChanged |= axisDrag("Offset (m)##w", v.handsOffset, 0.002f, -1.0f,
                                1.0f, "R##wox", "U##woy", "F##woz");
        feelChanged |= axisDrag("Rotation (deg)##w", v.handsRotationDegrees,
                                0.25f, -180.0f, 180.0f, "P##wrx", "Y##wry",
                                "R##wrz");
        feelChanged |= ImGui::SliderFloat("Scale##w", &v.handsScale, 0.25f,
                                          3.0f, "x%.3f");

        // A sprite weapon hangs off no socket -- it has no skeleton -- so the
        // whole attachment block below would be sliders that move nothing.
        // Showing what it *does* have instead is the honest readout, and the
        // muzzle is the one number here a sprite weapon still owns.
        if (v.presentation == ViewmodelPresentation::Sprite) {
            ImGui::SeparatorText("Sprite layers");
            ImGui::TextDisabled(
                "%zu weapon layer(s) over the hands', %zu drawn",
                v.spriteLayers.size(), player->hands().sprite().layerCount());
            for (const ViewmodelSpriteLayer& layer : v.spriteLayers)
                ImGui::BulletText("%s  %.2fx%.2f m @ %.2f m  grid %dx%d",
                                  layer.id.c_str(), layer.size.x, layer.size.y,
                                  layer.distance, layer.grid.x, layer.grid.y);
            ImGui::TextDisabled("Layers are authored in weapons.toml; they are "
                                "sorted farthest-first and drawn depth-off.");
            if (axisDrag("Muzzle (m)##sm", v.spriteMuzzle, 0.002f, -2.0f, 2.0f,
                         "R##smx", "U##smy", "F##smz") &&
                mCur.context)
                player->refreshViewmodel(*mCur.context);
            ImGui::TextDisabled("Camera space. Aim is still the camera ray -- "
                                "this only moves where the shot comes from.");
        } else {
            // Where the weapon sits *in the hand*, as opposed to where the
            // hands sit in front of the eye. These are the numbers that were
            // unreachable before sockets existed: the weapon had nowhere to
            // hang, so seating it was not a thing anyone could do.
            ImGui::SeparatorText("Attachment (socket on the hand rig)");
            const std::vector<std::string> socketNames =
                player->hands().sockets().names();
            if (socketNames.empty()) {
                ImGui::TextDisabled("no cooked rig, so no sockets to hang from");
            } else if (ImGui::BeginCombo("Socket", v.socket.c_str())) {
                for (const std::string& name : socketNames)
                    if (ImGui::Selectable(name.c_str(), name == v.socket)) {
                        v.socket = name;
                        // A different socket is a different parent, so this one
                        // rebuilds rather than re-seats.
                        if (mCur.context)
                            player->rebuildWeaponViewmodel(*mCur.context);
                    }
                ImGui::EndCombo();
            }
            bool attachChanged =
                axisDrag("Offset (m)##a", v.attachOffset, 0.002f, -1.0f, 1.0f,
                         "X##aox", "Y##aoy", "Z##aoz");
            attachChanged |=
                axisDrag("Rotation (deg)##a", v.attachRotationDegrees, 0.25f,
                         -180.0f, 180.0f, "P##arx", "Y##ary", "R##arz");
            attachChanged |= ImGui::SliderFloat("Scale##a", &v.attachScale,
                                                0.05f, 3.0f, "x%.3f");
            if (attachChanged && mCur.context)
                player->refreshViewmodel(*mCur.context);
            ImGui::TextDisabled("presenting: %s%s%s",
                                weaponPresentationName(
                                    player->hands().weapon().presentation()),
                                v.model.empty() ? "" : "  model ",
                                v.model.c_str());
        }

        ImGui::SeparatorText("Recoil");
        feelChanged |= ImGui::SliderFloat("Distance", &v.recoilDistance, 0.0f,
                                          0.5f, "%.3f m");
        feelChanged |= ImGui::SliderFloat("Pitch", &v.recoilPitchDegrees,
                                          -30.0f, 30.0f, "%.2f deg");
        feelChanged |= ImGui::SliderFloat("Yaw", &v.recoilYawDegrees, -30.0f,
                                          30.0f, "%.2f deg");
        feelChanged |= ImGui::SliderFloat("Recovery", &v.recoilRecovery, 1.0f,
                                          60.0f);
        feelChanged |= ImGui::SliderFloat("Fire animation", &v.fireDuration,
                                          0.03f, 1.0f, "%.3f s");

        ImGui::SeparatorText("Bob / sway");
        feelChanged |= ImGui::SliderFloat("Movement bob", &v.movementBob, 0.0f,
                                          0.1f, "%.4f m");
        feelChanged |= ImGui::SliderFloat("Bob speed", &v.movementBobSpeed,
                                          0.0f, 20.0f);
        feelChanged |= ImGui::SliderFloat("Idle sway", &v.idleSway, 0.0f, 0.05f,
                                          "%.4f m");
        feelChanged |= ImGui::SliderFloat("Look sway", &v.lookSway, 0.0f,
                                          0.01f, "%.5f m/px");

        ImGui::SeparatorText("Hands");
        ImGui::TextDisabled("idle '%s'  draw '%s'  fire '%s'",
                            v.handsIdleAnimation.c_str(),
                            v.handsDrawAnimation.c_str(),
                            v.handsFireAnimation.c_str());
        ImGui::TextDisabled("muzzle joint '%s'  offset [%.3f %.3f %.3f]",
                            v.handsMuzzleJoint.c_str(), v.handsMuzzleOffset.x,
                            v.handsMuzzleOffset.y, v.handsMuzzleOffset.z);

        // Kicks the live rig even while the sim is frozen, which is the only
        // way to judge recoil with the console open.
        if (ImGui::Button("Test fire"))
            player->hands().triggerFire(*mCur.renderer);
        ImGui::SameLine();
        copyButton("Copy weapon TOML",
                   viewmodelWeaponToml(
                       definitions[std::size_t(mViewmodelWeapon)].id, v),
                   "Paste into assets/config/weapons.toml");
        ImGui::PopID();
    }

    if (section("Gizmo")) {
        ImGui::PushID("giz");
        ImGui::Checkbox("Show handles in the view", &mGizmoEnabled);
        if (mGizmoEnabled) {
            static const char* targets[] = {"Rig socket", "Weapon lean",
                                            "Weapon attach", "Muzzle"};
            int target = int(mGizmoTarget);
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::Combo("Target", &target, targets,
                             IM_ARRAYSIZE(targets)))
                mGizmoTarget = ViewmodelGizmoTarget(target);
            ImGui::RadioButton("Move", &mGizmoOperation, 0);
            ImGui::SameLine();
            // The muzzle is a point on a joint: it has no orientation or size
            // of its own, so offering rotate/scale there would be a lie.
            const bool pointOnly = mGizmoTarget == ViewmodelGizmoTarget::Muzzle;
            ImGui::BeginDisabled(pointOnly);
            ImGui::RadioButton("Rotate", &mGizmoOperation, 1);
            ImGui::SameLine();
            ImGui::RadioButton("Scale", &mGizmoOperation, 2);
            ImGui::EndDisabled();
            if (pointOnly)
                mGizmoOperation = 0;
            ImGui::Checkbox("Local axes", &mGizmoLocal);
            ImGui::SameLine();
            ImGui::Checkbox("Snap", &mGizmoSnap);
            if (mGizmoSnap) {
                ImGui::SetNextItemWidth(110.0f);
                ImGui::DragFloat("Move step", &mGizmoTranslateSnap, 0.001f,
                                 0.001f, 0.5f, "%.3f m");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(110.0f);
                ImGui::DragFloat("Turn step", &mGizmoRotateSnap, 0.5f, 1.0f,
                                 90.0f, "%.0f deg");
            }
            ImGui::TextDisabled(
                "Handles sit on the authored pose, and motion is held still "
                "while you drag.");
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    copyButton("Copy [player_viewmodel]", viewmodelRigToml(rig),
               "Paste into assets/config/game.toml");
    // A rig with its own framing does not read [player_viewmodel]'s placement,
    // so pasting that block would tune something this rig ignores. It gets its
    // own three lines, for its own entry in viewmodel_hands.toml.
    if (const game::HandsDefinition& hands = player->hands().definitionRef();
        hands.hasFraming) {
        // The live values, not the ones loaded: the point of the panel is that
        // you drag until it looks right and then keep THAT.
        player->hands().captureFraming();
        char block[320];
        std::snprintf(block, sizeof(block),
                      "# in the [[rig]] entry for \"%s\"\n"
                      "offset = [%.4f, %.4f, %.4f]\n"
                      "rotation = [%.2f, %.2f, %.2f]\n"
                      "scale = %.4f\n",
                      hands.id.c_str(), rig.offset.x, rig.offset.y,
                      rig.offset.z, rig.rotation.x, rig.rotation.y,
                      rig.rotation.z, rig.scale);
        ImGui::SameLine();
        copyButton("Copy rig framing", block,
                   "Paste into this rig's [[rig]] entry in "
                   "assets/config/viewmodel_hands.toml");
    }
    ImGui::SameLine();
    if (!validViewmodelRig(rig))
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                           "rig values out of range");

    drawViewmodelGizmo();

    // A live edit to a definition only reaches the rig through the feel copy
    // the motion composer holds, and a frozen rig needs the pose re-applied.
    if (feelChanged && mCur.context)
        player->refreshViewmodel(*mCur.context);
}

// Handles over the game view for the three placements that are impossible to
// judge as numbers: where the rig sits, how a weapon leans out of it, and where
// the muzzle a projectile leaves from actually is.
//
// Two rules make this behave. The handles are anchored to the AUTHORED pose,
// not the animated node -- bob and sway would otherwise drag them out from
// under the cursor mid-drag -- and motion is frozen for the duration of a drag
// and restored after it, so what you place is what you get.
void DebugPanels::drawViewmodelGizmo()
{
    PlayerSystem* player = mCur.playerSystem;
    if (!mGizmoEnabled || !player || !mCur.renderer)
        return;

    eng::Renderer& renderer = *mCur.renderer;
    ViewmodelRig& rig = player->viewmodelRig();
    std::vector<PlayerWeaponDef>& definitions = player->weaponDefinitions();
    if (definitions.empty())
        return;
    const std::size_t index =
        std::min(std::size_t(std::max(mViewmodelWeapon, 0)),
                 definitions.size() - 1);
    WeaponViewmodelDef& weapon = definitions[index].viewmodel;

    // The frame every camera-space placement is expressed in.
    eng::NodeTransform head;
    if (!renderer.nodeWorldTransform(player->controller().headNode(), head))
        return;
    const glm::mat4 headWorld =
        glm::translate(glm::mat4(1.0f), head.position) *
        glm::mat4_cast(head.orientation) *
        glm::scale(glm::mat4(1.0f), head.scale);

    const auto compose = [](glm::vec3 position, glm::vec3 rotationDegrees,
                            float scale) {
        return glm::translate(glm::mat4(1.0f), position) *
               glm::mat4_cast(glm::quat(glm::radians(rotationDegrees))) *
               glm::scale(glm::mat4(1.0f), glm::vec3(scale));
    };
    // Same convention as the runtime pose: quat-from-euler, so what the gizmo
    // writes back reads identically in the sliders and in the TOML.
    const auto decompose = [](const glm::mat4& m, glm::vec3& position,
                              glm::vec3& rotationDegrees, float& scale) {
        position = glm::vec3(m[3]);
        glm::vec3 axes(glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])),
                       glm::length(glm::vec3(m[2])));
        axes = glm::max(axes, glm::vec3(1e-5f));
        const glm::mat3 rotation(glm::vec3(m[0]) / axes.x,
                                 glm::vec3(m[1]) / axes.y,
                                 glm::vec3(m[2]) / axes.z);
        rotationDegrees = glm::degrees(glm::eulerAngles(
            glm::normalize(glm::quat_cast(rotation))));
        scale = (axes.x + axes.y + axes.z) / 3.0f;
    };

    // The socket, and the weapon's lean expressed inside it.
    const glm::mat4 socket = compose(rig.offset, rig.rotation, rig.scale);
    const glm::mat4 lean = compose(weapon.handsOffset,
                                   weapon.handsRotationDegrees,
                                   weapon.handsScale);
    // Both joint-anchored targets are meaningless for a sprite weapon: it has
    // no skeleton, so there is no socket to seat it in and no joint to hang a
    // muzzle on. Its muzzle is a camera-space point, dragged in the Sprite
    // layers section above; anchoring a gizmo to a joint it does not use would
    // move handles that change nothing on screen.
    if (weapon.presentation == ViewmodelPresentation::Sprite &&
        (mGizmoTarget == ViewmodelGizmoTarget::WeaponAttach ||
         mGizmoTarget == ViewmodelGizmoTarget::Muzzle))
        return;
    const std::optional<glm::mat4> jointWorld =
        mGizmoTarget == ViewmodelGizmoTarget::Muzzle
            ? player->hands().muzzleJointWorld(renderer)
            : std::nullopt;
    if (mGizmoTarget == ViewmodelGizmoTarget::Muzzle && !jointWorld)
        return; // no cooked rig, or the weapon names a joint the rig lacks
    // The attach gizmo lives in the socket's frame, which is a live joint on
    // the animated skeleton -- not a camera-space offset like the two above.
    const std::optional<glm::mat4> socketWorld =
        mGizmoTarget == ViewmodelGizmoTarget::WeaponAttach
            ? player->hands().weaponSocketWorld(renderer)
            : std::nullopt;
    if (mGizmoTarget == ViewmodelGizmoTarget::WeaponAttach && !socketWorld)
        return; // the weapon names a socket this rig does not define

    glm::mat4 parent(1.0f);
    glm::mat4 matrix(1.0f);
    switch (mGizmoTarget) {
    case ViewmodelGizmoTarget::Socket:
        parent = headWorld;
        matrix = headWorld * socket;
        break;
    case ViewmodelGizmoTarget::WeaponLean:
        parent = headWorld * socket;
        matrix = parent * lean;
        break;
    case ViewmodelGizmoTarget::WeaponAttach:
        parent = *socketWorld;
        matrix = parent * compose(weapon.attachOffset,
                                  weapon.attachRotationDegrees,
                                  weapon.attachScale);
        break;
    case ViewmodelGizmoTarget::Muzzle:
        parent = *jointWorld;
        matrix = parent *
                 glm::translate(glm::mat4(1.0f), weapon.handsMuzzleOffset);
        break;
    }

    const ImGuiIO& io = ImGui::GetIO();
    ImGuizmo::SetOrthographic(false);
    // The game draws to the whole window, so the gizmo's rect is the window --
    // the editor's viewport-rect version of this is a panel, not a screen.
    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
    ImGuizmo::SetRect(0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y);

    const glm::mat4 view = renderer.cameraView();
    const glm::mat4 projection = renderer.cameraProjection();

    const ImGuizmo::OPERATION operation =
        mGizmoOperation == 1   ? ImGuizmo::ROTATE
        : mGizmoOperation == 2 ? ImGuizmo::SCALEU
                               : ImGuizmo::TRANSLATE;
    const glm::vec3 snapValue =
        mGizmoOperation == 1 ? glm::vec3(mGizmoRotateSnap)
                             : glm::vec3(mGizmoTranslateSnap);
    const float* snap = mGizmoSnap ? glm::value_ptr(snapValue) : nullptr;

    ImGuizmo::PushID("viewmodel-rig");
    const bool manipulated = ImGuizmo::Manipulate(
        glm::value_ptr(view), glm::value_ptr(projection), operation,
        mGizmoLocal ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
        glm::value_ptr(matrix), nullptr, snap);
    const bool using_ = ImGuizmo::IsUsing();
    ImGuizmo::PopID();

    // Freeze on the first frame of a drag, restore on release. Without this a
    // placement is made against a rig that is breathing under the handles.
    if (using_ && !mGizmoFroze) {
        mGizmoFroze = true;
        mGizmoMotionWas = rig.motionEnabled;
        rig.motionEnabled = false;
    }
    else if (!using_ && mGizmoFroze) {
        mGizmoFroze = false;
        rig.motionEnabled = mGizmoMotionWas;
    }

    if (manipulated) {
        const glm::mat4 local = glm::inverse(parent) * matrix;
        switch (mGizmoTarget) {
        case ViewmodelGizmoTarget::Socket:
            decompose(local, rig.offset, rig.rotation, rig.scale);
            rig.scale = std::max(rig.scale, 0.01f);
            break;
        case ViewmodelGizmoTarget::WeaponLean:
            decompose(local, weapon.handsOffset, weapon.handsRotationDegrees,
                      weapon.handsScale);
            weapon.handsScale = std::max(weapon.handsScale, 0.01f);
            break;
        case ViewmodelGizmoTarget::WeaponAttach:
            decompose(local, weapon.attachOffset, weapon.attachRotationDegrees,
                      weapon.attachScale);
            weapon.attachScale = std::max(weapon.attachScale, 0.01f);
            break;
        case ViewmodelGizmoTarget::Muzzle:
            weapon.handsMuzzleOffset = glm::vec3(local[3]);
            break;
        }
        if (mCur.context)
            player->refreshViewmodel(*mCur.context);
    }

    // Where the projectile actually leaves from, marked in the view. This is
    // the whole point of the muzzle being a joint rather than the camera: the
    // two are different places, and only one of them is visible without a mark.
    if (const std::optional<glm::vec3> muzzle =
            player->hands().muzzleWorldPosition(renderer)) {
        const glm::vec4 clip =
            projection * view * glm::vec4(*muzzle, 1.0f);
        if (clip.w > 1e-4f) {
            const ImVec2 at((clip.x / clip.w * 0.5f + 0.5f) * io.DisplaySize.x,
                            (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) *
                                io.DisplaySize.y);
            ImDrawList* draw = ImGui::GetBackgroundDrawList();
            draw->AddCircle(at, 7.0f, IM_COL32(255, 205, 70, 220), 12, 1.5f);
            draw->AddCircleFilled(at, 2.0f, IM_COL32(255, 235, 160, 255));
            draw->AddText(ImVec2(at.x + 10.0f, at.y - 7.0f),
                          IM_COL32(255, 220, 120, 220), "muzzle");
        }
    }
}

void DebugPanels::drawFeelTab()
{
    entt::registry* reg = mCur.registry;
    if (!reg || mCur.player == entt::null || !reg->valid(mCur.player)) {
        ImGui::TextDisabled("Player entity unavailable.");
        return;
    }
    const entt::entity player = mCur.player;

    if (auto* stamina = reg->try_get<Stamina>(player);
        stamina && section("Stamina")) {
        ImGui::PushID("st");
        ImGui::ProgressBar(stamina->max > 0.0f ? stamina->current / stamina->max
                                               : 0.0f,
                           ImVec2(-FLT_MIN, 0));
        ImGui::SliderFloat("Max", &stamina->max, 10.0f, 300.0f);
        ImGui::SliderFloat("Regen rate", &stamina->regenRate, 0.0f, 100.0f);
        ImGui::SliderFloat("Regen delay", &stamina->regenDelay, 0.0f, 3.0f);
        if (ImGui::Button("Refill"))
            stamina->current = stamina->max;
        ImGui::PopID();
    }
    if (auto* poise = reg->try_get<Poise>(player); poise && section("Poise")) {
        ImGui::PushID("po");
        ImGui::ProgressBar(poise->max > 0.0f ? poise->current / poise->max
                                             : 0.0f,
                           ImVec2(-FLT_MIN, 0));
        ImGui::SliderFloat("Max", &poise->max, 10.0f, 300.0f);
        ImGui::SliderFloat("Regen rate", &poise->regenRate, 0.0f, 100.0f);
        ImGui::SliderFloat("Regen delay", &poise->regenDelay, 0.0f, 3.0f);
        ImGui::Text("Stagger immunity: %.2fs", poise->staggerImmuneFor);
        ImGui::PopID();
    }
    if (auto* mana = reg->try_get<Mana>(player); mana && section("Mana")) {
        ImGui::PushID("mn");
        ImGui::ProgressBar(mana->max > 0.0f ? mana->current / mana->max : 0.0f,
                           ImVec2(-FLT_MIN, 0));
        ImGui::SliderFloat("Max", &mana->max, 10.0f, 300.0f);
        ImGui::SliderFloat("Regen rate", &mana->regenRate, 0.0f, 50.0f);
        if (ImGui::Button("Refill"))
            mana->current = mana->max;
        ImGui::PopID();
    }
    if (auto* action = reg->try_get<ActionState>(player);
        action && section("Action State")) {
        static const char* phases[] = {"Idle",     "Windup",  "Active",
                                       "Recovery", "Deflect", "Dodge",
                                       "Staggered"};
        const int phase = int(action->phase);
        ImGui::Text("Phase: %s", phase >= 0 && phase < IM_ARRAYSIZE(phases)
                                     ? phases[phase]
                                     : "?");
        ImGui::Text("Timer: %.3fs", action->timer);
        ImGui::Separator();
        ImGui::SliderFloat("Windup", &action->attack.windup, 0.0f, 1.0f);
        ImGui::SliderFloat("Active", &action->attack.active, 0.0f, 0.5f);
        ImGui::SliderFloat("Recovery", &action->attack.recovery, 0.0f, 1.0f);
        ImGui::SliderFloat("Stamina cost", &action->attack.staminaCost, 0.0f,
                           60.0f);
        ImGui::SliderFloat("Poise damage", &action->attack.poiseDamage, 0.0f,
                           80.0f);
    }
}

void DebugPanels::drawAudioTab()
{
    if (!mCur.audio || !mCur.audio->loaded()) {
        ImGui::TextDisabled("Audio system unavailable.");
        return;
    }
    const GameAudioStats stats = mCur.audio->stats();
    ImGui::Text("Voices: %zu / %zu", stats.backend.activeVoices,
                stats.backend.voiceLimit);
    ImGui::SameLine();
    ImGui::TextDisabled("%s backend",
                        stats.backend.nullBackend ? "null" : "device");
    ImGui::ProgressBar(stats.musicIntensity, ImVec2(-FLT_MIN, 0.0f));
    ImGui::Text("Music intensity %.2f  tier %d", stats.musicIntensity,
                stats.musicTier);

    if (ImGui::BeginTable("audio_buses", 2,
                          ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Bus");
        ImGui::TableSetupColumn("Voices");
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < eng::kAudioBusCount; ++i) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(
                eng::audioBusName(static_cast<eng::AudioBus>(i)));
            ImGui::TableNextColumn();
            ImGui::Text("%zu", stats.backend.voicesByBus[i]);
        }
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Session telemetry");
    ImGui::Text("Emitted: %llu",
                static_cast<unsigned long long>(stats.emitted));
    ImGui::Text("Cue concurrency rejects: %llu",
                static_cast<unsigned long long>(stats.concurrencyRejected));
    ImGui::Text("Cooldown rejects: %llu",
                static_cast<unsigned long long>(stats.cooldownRejected));
    ImGui::Text("Distance culled: %llu",
                static_cast<unsigned long long>(stats.distanceCulled));
    ImGui::Text("Unavailable cue requests: %llu",
                static_cast<unsigned long long>(stats.unavailableRejected));
    ImGui::Text("Backend stolen/rejected: %llu / %llu",
                static_cast<unsigned long long>(stats.backend.voicesStolen),
                static_cast<unsigned long long>(stats.backend.voicesRejected));
    ImGui::TextDisabled("%zu cues, %zu adaptive stems",
                        mCur.audio->catalog().cues.size(),
                        mCur.audio->catalog().music.stems.size());
}

// Live enemy tuning. Three things a designer does constantly and should never
// need a rebuild for: put one in front of me, watch what it is thinking, and
// change a number until it feels right. Reloading the table is the fourth.
void DebugPanels::drawEnemiesTab()
{
    EnemySystem* enemies = mCur.enemies;
    EnemyLibrary* library = mCur.enemyLibrary;
    if (!enemies || !library || !mCur.context) {
        ImGui::TextDisabled("Enemy system unavailable.");
        return;
    }

    const std::vector<std::string> ids = library->ids();
    if (ids.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f),
                           "enemies.toml defined no enemies.");
    }
    if (mSpawnId.empty() && !ids.empty())
        mSpawnId = ids.front();
    if (mTuneId.empty() && !ids.empty())
        mTuneId = ids.front();

    if (section("Spawn")) {
        ImGui::PushID("sp");
        if (ImGui::BeginCombo("Definition", mSpawnId.c_str())) {
            for (const std::string& id : ids)
                if (ImGui::Selectable(id.c_str(), id == mSpawnId))
                    mSpawnId = id;
            ImGui::EndCombo();
        }
        // Six metres ahead: far enough to watch the alert beat play out, close
        // enough that it engages immediately.
        const glm::vec3 at = mCur.playerFeet + mCur.playerForward * 6.0f;
        if (ImGui::Button("Spawn ahead") && !mSpawnId.empty())
            enemies->spawn(
                *mCur.context, mSpawnId, at,
                std::atan2(-mCur.playerForward.x, -mCur.playerForward.z));
        ImGui::SameLine();
        if (ImGui::Button("Spawn x5") && !mSpawnId.empty())
            for (int i = 0; i < 5; ++i)
                enemies->spawn(
                    *mCur.context, mSpawnId,
                    at + glm::vec3(float(i - 2) * 1.5f, 0.0f, 0.0f),
                    std::atan2(-mCur.playerForward.x, -mCur.playerForward.z));
        ImGui::SameLine();
        if (ImGui::Button("Clear all"))
            enemies->clear(*mCur.context);
        ImGui::Text("Alive: %d  (incl. corpses: %d)", enemies->aliveCount(),
                    enemies->liveCount());
        ImGui::PopID();
    }

    if (section("Live")) {
        ImGui::PushID("lv");
        const std::vector<EnemySystem::Snapshot> live =
            enemies->snapshot(mCur.playerFeet);
        if (live.empty()) {
            ImGui::TextDisabled("Nothing spawned.");
        }
        else if (ImGui::BeginTable("enemies", 5,
                                   ImGuiTableFlags_RowBg |
                                       ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("State");
            ImGui::TableSetupColumn("HP");
            ImGui::TableSetupColumn("Dist");
            ImGui::TableSetupColumn("");
            ImGui::TableHeadersRow();
            for (const EnemySystem::Snapshot& s : live) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(s.name.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(enemyStateName(s.state));
                ImGui::TableNextColumn();
                ImGui::Text("%.0f/%.0f", s.health, s.healthMax);
                ImGui::TableNextColumn();
                ImGui::Text("%.1fm", s.distance);
                ImGui::TableNextColumn();
                ImGui::PushID(int(entt::to_integral(s.entity)));
                if (ImGui::SmallButton("Kill"))
                    // Through the damage model, not by zeroing health: the
                    // corpse, the callbacks and the spawner bookkeeping all
                    // hang off the real death path.
                    enemies->onKilled(*mCur.context, s.entity,
                                      glm::vec3(0.0f, 0.0f, 1.0f));
                ImGui::SameLine();
                if (ImGui::SmallButton("Del"))
                    enemies->despawn(*mCur.context, s.entity);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
    }

    if (section("Tune definition")) {
        ImGui::PushID("tn");
        if (ImGui::BeginCombo("Definition##tune", mTuneId.c_str())) {
            for (const std::string& id : ids)
                if (ImGui::Selectable(id.c_str(), id == mTuneId))
                    mTuneId = id;
            ImGui::EndCombo();
        }
        if (EnemyDef* def = library->mutableDef(mTuneId)) {
            ImGui::TextDisabled(
                "Locomotion/perception/behaviour apply live. Stats apply to "
                "the next spawn (they are copied into components).");
            ImGui::SeparatorText("Locomotion");
            ImGui::SliderFloat("Walk", &def->locomotion.walkSpeed, 0.0f, 8.0f);
            ImGui::SliderFloat("Chase", &def->locomotion.chaseSpeed, 0.0f,
                               12.0f);
            ImGui::SliderFloat("Strafe", &def->locomotion.strafeSpeed, 0.0f,
                               10.0f);
            ImGui::SliderFloat("Accel", &def->locomotion.acceleration, 1.0f,
                               60.0f);
            ImGui::SliderFloat("Turn deg/s", &def->locomotion.turnRateDeg,
                               30.0f, 900.0f);
            ImGui::SliderFloat("Lunge", &def->locomotion.lungeSpeed, 0.0f,
                               12.0f);

            ImGui::SeparatorText("Perception");
            ImGui::SliderFloat("Sight", &def->perception.sightRange, 1.0f,
                               60.0f);
            ImGui::SliderFloat("FOV deg", &def->perception.sightFovDeg, 20.0f,
                               360.0f);
            ImGui::SliderFloat("Hearing", &def->perception.hearingRange, 0.0f,
                               20.0f);
            ImGui::SliderFloat("Lose sight", &def->perception.loseSightTime,
                               0.0f, 15.0f);
            ImGui::SliderFloat("Alert time", &def->perception.alertTime, 0.0f,
                               2.0f);
            ImGui::SliderFloat("Leash", &def->perception.leashRange, 0.0f,
                               80.0f);

            ImGui::SeparatorText("Behaviour");
            ImGui::SliderFloat("Aggression", &def->behaviour.aggression, 0.0f,
                               1.0f);
            ImGui::SliderFloat("Preferred range",
                               &def->behaviour.preferredRange, 0.0f, 25.0f);
            ImGui::SliderFloat("Backoff range", &def->behaviour.backoffRange,
                               0.0f, 20.0f);
            ImGui::SliderFloat("Circle chance", &def->behaviour.circleChance,
                               0.0f, 1.0f);
            ImGui::SliderFloat("Reposition time",
                               &def->behaviour.repositionTime, 0.1f, 5.0f);
            ImGui::SliderFloat("Flee below HP%", &def->behaviour.fleeHealthPct,
                               0.0f, 1.0f);
            ImGui::Checkbox("Stationary", &def->behaviour.stationary);
            ImGui::SameLine();
            ImGui::Checkbox("Starts dormant", &def->behaviour.startsDormant);

            ImGui::SeparatorText("Stats (next spawn)");
            ImGui::SliderFloat("Health", &def->stats.health, 1.0f, 1500.0f);
            ImGui::SliderFloat("Poise", &def->stats.poise, 1.0f, 400.0f);
            ImGui::SliderFloat("Corpse time", &def->stats.corpseTime, 0.0f,
                               60.0f);

            ImGui::SeparatorText("Attacks");
            for (size_t i = 0; i < def->attacks.size(); ++i) {
                EnemyAttack& a = def->attacks[i];
                ImGui::PushID(int(i));
                if (ImGui::TreeNode(a.id.c_str())) {
                    ImGui::Text("weapon: %s%s", a.weapon.c_str(),
                                a.ranged ? "  (ranged)" : "");
                    ImGui::SliderFloat("Min range", &a.minRange, 0.0f, 20.0f);
                    ImGui::SliderFloat("Max range", &a.maxRange, 0.5f, 40.0f);
                    ImGui::SliderFloat("Cooldown", &a.cooldown, 0.1f, 12.0f);
                    ImGui::SliderFloat("Aim cone", &a.aimConeDeg, 1.0f, 180.0f);
                    ImGui::SliderFloat("Weight", &a.weight, 0.0f, 4.0f);
                    ImGui::SliderFloat("Windup", &a.timing.windup, 0.0f, 2.0f);
                    ImGui::SliderFloat("Active", &a.timing.active, 0.01f, 0.5f);
                    ImGui::SliderFloat("Recovery", &a.timing.recovery, 0.0f,
                                       2.0f);
                    ImGui::SliderFloat("Poise dmg", &a.timing.poiseDamage, 0.0f,
                                       120.0f);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            ImGui::Separator();
            if (ImGui::Button("Reload enemies.toml") &&
                !library->sourcePath().empty()) {
                // Live enemies hold a share of their definition, so they are
                // safe across this: they keep fighting with the row they were
                // spawned from and the next spawn picks up the edited file.
                library->load(library->sourcePath());
                library->resolve(mCur.context->vocabulary);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(live enemies keep their current stats)");
        }
        ImGui::PopID();
    }

    if (mCur.spawner && section("Save / load")) {
        ImGui::PushID("sv");
        // Deliberately a whole round trip through the file, not an in-memory
        // snapshot: the thing worth exercising by hand is the format, and a
        // path that skips encode/decode would never catch a codec bug.
        const std::string path = "/tmp/raven_enemies.sav";
        if (ImGui::Button("Save encounter")) {
            enemysave::writeFile(path,
                                 enemysave::capture(*enemies, *mCur.spawner));
        }
        ImGui::SameLine();
        if (ImGui::Button("Load encounter")) {
            if (const auto data = enemysave::readFile(path))
                enemysave::restore(*mCur.context, *enemies, *mCur.spawner,
                                   *data);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", path.c_str());
        ImGui::PopID();
    }

    if (mCur.spawner && section("Spawn points")) {
        ImGui::PushID("sw");
        EnemySpawner& spawner = *mCur.spawner;
        for (int i = 0; i < spawner.size(); ++i) {
            const EnemySpawnPoint& p = spawner.point(i);
            const EnemySpawnState& s = spawner.state(i);
            ImGui::PushID(i);
            ImGui::Text("%s [%s] %s%s  waves %d  alive %d",
                        p.id.empty() ? p.enemy.c_str() : p.id.c_str(),
                        p.enemy.c_str(), s.armed ? "armed" : "idle",
                        s.exhausted ? "/spent" : "", s.wavesSpawned,
                        s.aliveFromHere);
            ImGui::SameLine();
            if (ImGui::SmallButton("Trigger"))
                spawner.trigger(*mCur.context, *enemies, i);
            ImGui::PopID();
        }
        ImGui::PopID();
    }
}

// ---------------------------------------------------------- Portal prop --
// The one section of the engine's Portal tab that is the game's: the membrane
// shader belongs to the engine (eng::SurfacePanels), but the light it throws
// into the room and the wisps drifting in front of it belong to the level. It
// edits the live prop rather than a material, so it needs the level, and it is
// only offered when the selected portal actually exists -- there is no ascent
// portal at depth 0.

void DebugPanels::drawPortalDressing(int profileIdx)
{
    eng::Renderer* r = mCur.renderer;
    if (!r) {
        ImGui::TextDisabled("Renderer unavailable.");
        return;
    }
    PortalDressing& t = mDressing[profileIdx == 1 ? 1 : 0];
    PortalProp* prop =
        mCur.level ? &mCur.level->portal(profileIdx == 1) : nullptr;
    if (!prop || !prop->valid()) {
        ImGui::TextDisabled(mCur.level ? "No such portal at this depth."
                                       : "Level unavailable.");
        return;
    }

    if (ImGui::ColorEdit3("Glow colour", &t.lightColour.x,
                          ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float))
        r->setLightColour(prop->light, t.lightColour);
    if (ImGui::SliderFloat("Glow range", &t.lightRange, 0.5f, 20.0f))
        r->setLightRange(prop->light, t.lightRange);

    // Respawn rather than retune: an effect is a definition, and the portal
    // holds one instance of it. Swapping the definition means dropping that
    // instance and making another.
    const auto respawn = [&] {
        if (prop->wisps.valid())
            r->despawnParticles(prop->wisps);
        prop->wisps = {};
        if (!t.wisps.empty())
            prop->wisps = r->spawnParticles(t.wisps, prop->arch);
    };
    if (!mCur.particles) {
        ImGui::TextDisabled("Particle library unavailable.");
        return;
    }
    const char* current = t.wisps.empty() ? "(none)" : t.wisps.c_str();
    if (ImGui::BeginCombo("Wisps", current)) {
        if (ImGui::Selectable("(none)", t.wisps.empty())) {
            t.wisps.clear();
            respawn();
        }
        for (const eng::ParticleEffectDesc& desc : mCur.particles->descs()) {
            if (ImGui::Selectable(desc.name.c_str(), desc.name == t.wisps)) {
                t.wisps = desc.name;
                respawn();
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button("Restart wisps", ImVec2(-FLT_MIN, 0)))
        respawn();
    ImGui::TextDisabled("Effects themselves live in the engine "
                        "console's particle tooling.");
}

} // namespace game
