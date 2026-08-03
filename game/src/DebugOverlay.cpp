#include "DebugOverlay.h"

#include "GameContext.h"
#include "LiveLevel.h"
#include "PlayerSystem.h"
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

#include <cfloat>
#include <cmath>
#include <cstdio>

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
    console.addPanel(
        "Feel", [this] { drawFeelTab(); }, eng::PanelGroup::Gameplay);
    console.addPanel(
        "Enemies", [this] { drawEnemiesTab(); }, eng::PanelGroup::Gameplay);
    console.addPanel(
        "Audio", [this] { drawAudioTab(); }, eng::PanelGroup::Content);

    // Content, beside the shader panels: an effect is authored against the
    // bloom and grade settings that sit in the same dock group.
    mParticlePanel.install(console, eng::PanelGroup::Content);

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
