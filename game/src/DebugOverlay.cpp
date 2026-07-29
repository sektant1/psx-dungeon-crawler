#include "DebugOverlay.h"

#include "CombatConfig.h"
#include "GameDiagnostics.h"
#include "combat/FeelComponents.h"

#include <eng/Renderer.h>

#include <imgui.h>

#include <cfloat>

namespace game {

namespace {

bool section(const char* label)
{
    return ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
}

eng::FrameStatsView frameStats(const ProfHud& prof)
{
    eng::FrameStatsView view;
    view.frameHist = prof.frameHist;
    view.histCount = ProfHud::kHist;
    view.histHead = prof.histHead;
    view.phaseNames = ProfHud::kNames;
    view.phaseMs = prof.ms;
    view.phaseCount = ProfHud::kCount;
    return view;
}

} // namespace

DebugOverlay::DebugOverlay()
{
    mTools.addPanel("Combat", [this] { drawCombatTab(); });
    mTools.addPanel("Feel", [this] { drawFeelTab(); });
}

void DebugOverlay::draw(const Deps& deps)
{
    mCur = deps;

    eng::FrameStatsView frame;
    if (deps.prof)
        frame = frameStats(*deps.prof);

    eng::DebugTools::Deps engineDeps;
    engineDeps.renderer = deps.renderer;
    engineDeps.fps = deps.fps;
    engineDeps.frame = deps.prof ? &frame : nullptr;
    engineDeps.colliders = deps.colliders;
    engineDeps.steps = deps.steps;
    mTools.draw(engineDeps);
}

void DebugOverlay::drawCombatTab()
{
    CombatConfig* c = mCur.combat;
    if (!c) {
        ImGui::TextDisabled("Combat config unavailable.");
        return;
    }

    if (section("Fireball")) {
        ImGui::PushID("fb");
        ImGui::SliderFloat("Speed", &c->fireball.speed, 1.0f, 60.0f);
        ImGui::SliderFloat("Radius", &c->fireball.radius, 0.02f, 1.0f);
        ImGui::SliderFloat("Mass", &c->fireball.mass, 0.05f, 5.0f);
        ImGui::SliderFloat("TTL", &c->fireball.ttl, 0.5f, 15.0f);
        ImGui::SliderFloat("Impact impulse", &c->fireball.impactImpulse, 0.0f,
                           20.0f);
        ImGui::ColorEdit3("Light colour", &c->fireball.lightColour.x);
        ImGui::SliderFloat("Light range", &c->fireball.lightRange, 0.5f, 12.0f);
        ImGui::PopID();
    }
    if (section("Beam")) {
        ImGui::PushID("bm");
        ImGui::SliderFloat("Range", &c->beam.range, 1.0f, 80.0f);
        ImGui::SliderFloat("Width", &c->beam.width, 0.01f, 0.5f);
        ImGui::SliderFloat("Impulse", &c->beam.impulse, 0.0f, 20.0f);
        ImGui::SliderFloat("Segment TTL", &c->beam.segmentTtl, 0.02f, 0.5f);
        ImGui::ColorEdit3("Light colour", &c->beam.lightColour.x);
        ImGui::SliderFloat("Light range", &c->beam.lightRange, 0.5f, 12.0f);
        ImGui::PopID();
    }
    if (section("Arrow")) {
        ImGui::PushID("ar");
        ImGui::SliderFloat("Speed", &c->arrow.speed, 5.0f, 120.0f);
        ImGui::SliderFloat("Radius", &c->arrow.radius, 0.01f, 0.2f);
        ImGui::SliderFloat("Half height", &c->arrow.halfHeight, 0.05f, 0.6f);
        ImGui::SliderFloat("Mass", &c->arrow.mass, 0.02f, 2.0f);
        ImGui::SliderFloat("TTL", &c->arrow.ttl, 1.0f, 20.0f);
        ImGui::PopID();
    }
    if (section("Melee")) {
        ImGui::PushID("ml");
        ImGui::SliderFloat("Reach", &c->melee.reach, 0.5f, 4.0f);
        ImGui::SliderFloat("Radius", &c->melee.radius, 0.1f, 1.5f);
        ImGui::SliderFloat("Impulse", &c->melee.impulse, 0.0f, 20.0f);
        ImGui::SliderFloat("Windup", &c->melee.windup, 0.0f, 0.5f);
        ImGui::SliderFloat("Active", &c->melee.active, 0.0f, 0.5f);
        ImGui::PopID();
    }
}

void DebugOverlay::drawFeelTab()
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
        ImGui::ProgressBar(stamina->max > 0.0f
                               ? stamina->current / stamina->max
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
        static const char* phases[] = {"Idle",    "Windup", "Active",  "Recovery",
                                       "Deflect", "Dodge",  "Staggered"};
        const int phase = int(action->phase);
        ImGui::Text("Phase: %s",
                    phase >= 0 && phase < IM_ARRAYSIZE(phases) ? phases[phase]
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

void PerfOverlay::draw(const ProfHud* prof, eng::Renderer* renderer)
{
    if (!prof) {
        mImpl.draw(nullptr, renderer);
        return;
    }

    const eng::FrameStatsView frame = frameStats(*prof);
    mImpl.draw(&frame, renderer);
}

} // namespace game
