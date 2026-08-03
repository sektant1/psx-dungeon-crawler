#include <eng/DebugTools.h>
#include <eng/render/ImGuiHint.h>

#include "render/RenderPresets.h" // eng::RenderPresetValues, the editable profile cache

#include <eng/Renderer.h>
#include <eng/render/MaterialPreview.h>
#include <eng/render/ImGuiLayout.h> // whether a saved dock layout was restored
#include <eng/controllers/FpsController.h>

// The math operators must be defined before imgui.h, not before
// imgui_internal.h -- internal asserts on it.
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h> // DockBuilder*, for the default layout

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace eng {

namespace {

constexpr float kPanelWidth = 360.0f;

bool section(const char* label)
{
    return ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
}

// Small coloured swatch + label, for the collider shape legend.
void legend(const glm::vec3& c, const char* label)
{
    ImGui::ColorButton(label, ImVec4(c.r, c.g, c.b, 1.0f),
                       ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                       ImVec2(14, 14));
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
}

// Index of `id` in the preset table, for the combo's selection. The table is
// the numbering now, so nothing here assumes ids are contiguous.
int presetIndexOf(int id)
{
    const std::vector<RenderPresetInfo>& presets = renderPresets();
    for (int i = 0; i < int(presets.size()); ++i)
        if (presets[size_t(i)].id == id) return i;
    return 0;
}


// Side of the material swatch, in pixels. Small on purpose: it is a reminder of
// what a material looks like, not a place to judge one -- the editor's material
// stage is where that happens.
constexpr int kPreviewSwatch = 128;

} // namespace

// ----------------------------------------------------------------- state ----

struct DebugTools::State {
    // The material swatch rig, shared with the editor's material stage. Held
    // here so it is built once and survives across frames; the thumbnail is
    // rendered into its own small target and never touches the live scene.
    MaterialPreview preview;
    // Snapshot of Renderer::materialNames(), so the swatch and the list agree
    // on what materialIdx refers to. Refreshed each time the list is drawn.
    std::vector<std::string> materialList;
    // Combo selection into renderPresets(). Seeded on the first draw from the
    // profile the engine actually applied (Deps::renderPresetId), so the panel
    // opens showing the look that is really live.
    int presetIdx = presetIndexOf(kDefaultRenderPreset);
    bool profileInit = false; // rp seeded from the initial profile yet?
    // Editable copy of the active render profile. Every Render/Shaders slider
    // edits a field here and pushes just that field to the renderer, so a full
    // profile can be tuned live and dumped back out (Copy as TOML) reproducibly.
    RenderPresetValues rp;

    // Frame-stats readout, resampled slowly: at frame rate the digits churn
    // too fast to read. The plot underneath stays live.
    float statsSince = 1e9f;
    float statsInterval = 0.25f;
    float statsAvg = 0.0f;
    std::vector<float> statsPhaseMs;

    int materialIdx = 0;                    // materials tab: selected material row
    char paramName[64] = "outlineOpacity";  // materials tab: global-param entry
    float paramValue = 0.26f;
};

DebugTools::DebugTools() : mState(std::make_unique<State>())
{
    // RAVEN_TUNE=1 opens the panel on the first frame, for the same reason
    // RAVEN_CONSOLE does for the console: a deterministic capture has no key to
    // press, so this is the only way a screenshot can prove the panel draws.
    mVisible = std::getenv("RAVEN_TUNE") != nullptr;
}
DebugTools::~DebugTools() = default;
DebugTools::DebugTools(DebugTools&&) noexcept = default;
DebugTools& DebugTools::operator=(DebugTools&&) noexcept = default;

void DebugTools::addPanel(std::string name, std::function<void()> draw,
                          PanelGroup group)
{
    if (draw)
        mPanels.push_back(Panel{std::move(name), std::move(draw), group});
}

// ---------------------------------------------------------------- shell -----

void DebugTools::draw(const Deps& d)
{
    if (!mVisible)
        return;

    State& s = *mState;
    if (!s.profileInit) {
        // Seed the editable cache from the live profile, not from the default:
        // every control edits one field of this struct and pushes just that
        // field, so a cache that does not match what is on screen makes each
        // slider jump the look to a value nobody asked for.
        const int live = d.renderPresetId > 0 ? d.renderPresetId
                                              : kDefaultRenderPreset;
        s.presetIdx = presetIndexOf(live);
        s.rp = renderPresetValues(live);
        s.profileInit = true;
    }

    // One dockspace over the whole viewport, with the centre left transparent
    // so the game keeps rendering through it. Every panel below is its own
    // window docked into that space, which is what lets a viewer put two
    // related controls side by side instead of paging a tab bar.
    const ImGuiID dockspace = ImGui::DockSpaceOverViewport(
        0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
    if (!mLayoutBuilt) {
        // A restored layout is the whole point of eng::imgui_layout, and
        // building over it would silently undo the arrangement on the first
        // frame the console opened, every run. resetLayout() still forces the
        // shipped one back, for a viewer who has made a mess.
        if (mForceLayout || !imgui_layout::restored())
            buildDefaultLayout(dockspace);
        else
            placeNewPanels(dockspace);
        mLayoutBuilt = true;
        mForceLayout = false;
    }

    // Engine panels, grouped: World on the left, Look on the right. Each dock
    // node ends up holding three or four tabs rather than all eleven.
    drawWindow("Player", [&] { drawPlayerTab(d); });
    drawWindow("Colliders", [&] { drawCollidersTab(d); });
    drawWindow("Animation", [&] { drawAnimationTab(d); });
    drawWindow("Render", [&] { drawRenderTab(d); });
    drawWindow("Shaders", [&] { drawShadersTab(d); });
    drawWindow("Materials", [&] { drawMaterialsTab(d); });
    // Application panels last, so the engine's own keep a stable position
    // however many the game registers.
    for (Panel& p : mPanels)
        drawWindow(p.name.c_str(), p.draw);
}

// A panel is a plain dockable window. It carries no position or size of its
// own: the dock node it belongs to owns both, and the viewer owns the node.
void DebugTools::drawWindow(const char* name,
                            const std::function<void()>& body)
{
    if (ImGui::Begin(name))
        body();
    ImGui::End();
}

// Every panel and the group it belongs to, in draw order. Both the default
// layout and the new-panel placement below walk it, so a panel cannot be
// grouped in one and forgotten in the other.
std::vector<std::pair<const char*, PanelGroup>> DebugTools::panelGroups() const
{
    std::vector<std::pair<const char*, PanelGroup>> all = {
        {"Player", PanelGroup::World},   {"Colliders", PanelGroup::World},
        {"Animation", PanelGroup::World}, {"Render", PanelGroup::Look},
        {"Shaders", PanelGroup::Look},   {"Materials", PanelGroup::Look},
    };
    for (const Panel& p : mPanels)
        all.emplace_back(p.name.c_str(), p.group);
    return all;
}

// A saved layout only knows about the panels that existed when it was saved.
// Add one afterwards and imgui has no settings for it, so it opens floating in
// the middle of the screen -- which reads as the new panel being broken.
//
// Docking it beside a panel of its own group that *is* placed puts it where the
// default layout would have, without touching anything the viewer arranged. It
// cannot use the default layout's node ids: those come from DockBuilderSplitNode
// and have nothing to do with the ids a restored tree happens to carry.
void DebugTools::placeNewPanels(ImGuiID dockspace)
{
    const auto savedDock = [](const char* name) -> ImGuiID {
        const ImGuiWindowSettings* ws =
            ImGui::FindWindowSettingsByID(ImHashStr(name));
        return ws ? ws->DockId : 0;
    };

    const std::vector<std::pair<const char*, PanelGroup>> all = panelGroups();
    // One representative dock node per group, taken from the saved file.
    ImGuiID home[4] = {0, 0, 0, 0};
    for (const auto& [name, group] : all)
        if (const ImGuiID dock = savedDock(name); dock && !home[int(group)])
            home[int(group)] = dock;

    bool placed = false;
    for (const auto& [name, group] : all) {
        if (savedDock(name))
            continue;
        // Its own group first; failing that, anywhere the layout already is.
        // The dockspace root is the last resort -- docked somewhere odd still
        // beats floating over the middle of the game.
        ImGuiID target = home[int(group)];
        for (int i = 0; !target && i < 4; ++i)
            target = home[i];
        ImGui::DockBuilderDockWindow(name, target ? target : dockspace);
        placed = true;
    }
    if (placed)
        ImGui::DockBuilderFinish(dockspace);
}

void DebugTools::buildDefaultLayout(ImGuiID dockspace)
{
    // Wipe whatever is there and lay the nodes out from scratch. This runs at
    // most once per session, and only when there is no saved layout to restore
    // (or resetLayout() asked for the shipped one back), so a headless capture
    // with persistence off is still reproducible.
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace |
                                             ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::DockBuilderSetNodeSize(dockspace,
                                  ImGui::GetMainViewport()->WorkSize);

    // Left a quarter, right a quarter, and the right column split again so the
    // authored content profiles get their own node instead of a fifth tab.
    // The centre is left empty on purpose: that is the game.
    ImGuiID centre = dockspace;
    ImGuiID left = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, 0.22f,
                                               nullptr, &centre);
    ImGuiID right = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.28f,
                                                nullptr, &centre);
    ImGuiID content = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.42f,
                                                  nullptr, &right);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.30f,
                                                 nullptr, &centre);

    const auto nodeFor = [&](PanelGroup group) {
        switch (group) {
            case PanelGroup::World: return left;
            case PanelGroup::Look: return right;
            case PanelGroup::Content: return content;
            case PanelGroup::Gameplay: break;
        }
        return bottom;
    };

    for (const auto& [name, group] : panelGroups())
        ImGui::DockBuilderDockWindow(name, nodeFor(group));

    ImGui::DockBuilderFinish(dockspace);
}

void DebugTools::loadProfile(const Deps& d, int id)
{
    State& s = *mState;
    s.rp = renderPresetValues(id);
    if (d.renderer)
        applyRenderPreset(*d.renderer, s.rp);
    s.profileInit = true;
}

// ---------------------------------------------------------------- Render ----
// Core framebuffer / lighting / bloom / grade / camera. The full stylize shader
// set lives in the Shaders tab; together they cover every field of the render
// profile so a look can be tuned live and exported reproducibly.

void DebugTools::drawRenderTab(const Deps& d)
{
    Renderer* r = d.renderer;
    if (!r) { ImGui::TextDisabled("Renderer unavailable."); return; }
    State& s = *mState;
    RenderPresetValues& v = s.rp;

    if (section("Render Profile")) {
        const std::vector<RenderPresetInfo>& presets = renderPresets();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##profile", presets[size_t(s.presetIdx)].name)) {
            for (int i = 0; i < int(presets.size()); ++i) {
                if (ImGui::Selectable(presets[size_t(i)].name, i == s.presetIdx)) {
                    s.presetIdx = i;
                    loadProfile(d, presets[size_t(i)].id);
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("Re-apply", ImVec2(-FLT_MIN, 0)))
            applyRenderPreset(*r, v);
        ImGui::TextDisabled("Edit below; Copy as TOML in Shaders tab.");
    }

    if (section("Pixelation")) {
        // Absolute resolution wins over the divisor when both are set, so show
        // which one is actually live rather than leaving the slider lying.
        bool absRes = v.targetWidth > 0 && v.targetHeight > 0;
        if (ImGui::Checkbox("Absolute resolution", &absRes)) {
            if (absRes) {
                v.targetWidth = 640; v.targetHeight = 448;
                r->setRenderResolution(v.targetWidth, v.targetHeight);
            } else {
                v.targetWidth = v.targetHeight = 0;
                r->setPixelSize(v.pixelSize); // also clears the absolute target
            }
        }
        if (absRes) {
            int wh[2] = {v.targetWidth, v.targetHeight};
            if (ImGui::InputInt2("Resolution", wh)) {
                v.targetWidth = std::clamp(wh[0], 64, 4096);
                v.targetHeight = std::clamp(wh[1], 64, 4096);
                r->setRenderResolution(v.targetWidth, v.targetHeight);
            }
        } else if (ImGui::SliderInt("Pixel size", &v.pixelSize, 1, 16)) {
            r->setPixelSize(v.pixelSize);
        }
        if (ImGui::Checkbox("Per-pixel lighting", &v.perPixel))
            r->setPerPixelLightingEnabled(v.perPixel);
        if (ImGui::SliderFloat("Precision mult", &v.precisionMultiplier, 0.25f, 4.0f))
            r->setGlobalMaterialParam("precisionMultiplier", v.precisionMultiplier);
        if (ImGui::SliderFloat("Affine warp", &v.affineAmount, 0.0f, 1.0f))
            r->setGlobalMaterialParam("affineAmount", v.affineAmount);
    }

    if (section("Lighting")) {
        bool ch = ImGui::Checkbox("Banded", &v.bandedLightingEnabled);
        ch |= ImGui::SliderFloat("Bands", &v.bandedLightSteps, 0.0f, 8.0f, "%.0f");
        if (ch)
            r->setLightSteps(v.bandedLightingEnabled ? v.bandedLightSteps : 0.0f);
        if (ImGui::SliderFloat("Band softness", &v.stepSoftness, 0.0f, 1.0f))
            r->setLightStepSoftness(v.stepSoftness);
    }

    if (section("Bloom")) {
        if (ImGui::Checkbox("Enabled##bloom", &v.bloom))
            r->setBloomEnabled(v.bloom);
        bool ch = ImGui::SliderFloat("Threshold", &v.bloomThreshold, 0.0f, 2.0f);
        ch |= ImGui::SliderFloat("Intensity", &v.bloomIntensity, 0.0f, 2.0f);
        if (ch)
            r->setBloomParams(v.bloomThreshold, v.bloomIntensity);
        // Off: bilinear upsample from the half-res blur, so the glow ramps
        // across a render pixel. On: the glow is built out of render pixels.
        bool snap = v.bloomPixelSnap >= 0.5f;
        if (ImGui::Checkbox("Snap to pixel grid##bloom", &snap)) {
            v.bloomPixelSnap = snap ? 1.0f : 0.0f;
            r->setMaterialParam("Engine/Psx/BloomComposite", "bloomPixelSnap",
                                v.bloomPixelSnap);
        }
    }

    if (section("Colour Grade")) {
        bool g = ImGui::SliderFloat("Desaturate", &v.gradeDesaturate, 0.0f, 1.0f);
        g |= ImGui::SliderFloat("Contrast", &v.gradeContrast, 0.5f, 2.0f);
        g |= ImGui::ColorEdit3("Shadow tint", &v.gradeShadow.x);
        g |= ImGui::ColorEdit3("Mid tint", &v.gradeMid.x);
        if (g)
            r->setGradeParams(v.gradeDesaturate, v.gradeContrast, v.gradeShadow,
                              v.gradeMid);
        if (ImGui::SliderFloat("Saturation", &v.gradeSaturation, 0.0f, 2.0f))
            r->setMaterialParam("Engine/Psx/DitherPost", "gradeSaturation", v.gradeSaturation);
        if (ImGui::SliderFloat("Tint strength", &v.gradeTintStrength, 0.0f, 1.0f))
            r->setMaterialParam("Engine/Psx/DitherPost", "gradeTintStrength", v.gradeTintStrength);
        if (ImGui::SliderFloat("Black lift", &v.gradeBlackLift, 0.0f, 0.5f))
            r->setMaterialParam("Engine/Psx/DitherPost", "gradeBlackLift", v.gradeBlackLift);
    }

    if (section("Quantize / Dither")) {
        if (ImGui::SliderFloat("Colour depth", &v.colDepth, 3.0f, 63.0f, "%.0f"))
            r->setMaterialParam("Engine/Psx/DitherPost", "colDepth", v.colDepth);
        if (ImGui::SliderFloat("Dither banding", &v.ditherBanding, 0.0f, 0.1f, "%.4f"))
            r->setMaterialParam("Engine/Psx/DitherPost", "ditherBanding", v.ditherBanding);
        if (ImGui::SliderFloat("Dither dark fade", &v.ditherDarkFade, 0.0f, 1.0f))
            r->setMaterialParam("Engine/Psx/DitherPost", "ditherDarkFade", v.ditherDarkFade);
    }

    if (section("Vignette")) {
        bool ch = ImGui::Checkbox("Enabled##vig", &v.vignetteEnabled);
        ch |= ImGui::SliderFloat("Strength##vig", &v.vignetteStrength, 0.0f, 1.0f);
        if (ch)
            r->setMaterialParam("Engine/Psx/DitherPost", "vignetteStrength",
                                v.vignetteEnabled ? v.vignetteStrength : 0.0f);
        if (ImGui::ColorEdit3("Colour##vig", &v.vignetteColor.x))
            r->setMaterialParam("Engine/Psx/DitherPost", "vignetteColor", v.vignetteColor);
    }

    // Environment lives outside the render profile (it comes from the scene
    // palette), so it reads/writes the live EnvState directly.
    if (section("Environment")) {
        const EnvState& e = r->envState();
        glm::vec3 amb = e.ambient;
        if (ImGui::ColorEdit3("Ambient", &amb.x)) r->setAmbient(amb);
        glm::vec3 fog = e.fogColour; float fogD = e.fogDensity;
        bool ch = ImGui::ColorEdit3("Fog colour", &fog.x);
        ch |= ImGui::SliderFloat("Fog density", &fogD, 0.0f, 0.3f, "%.4f");
        if (ch) r->setFog(fog, fogD);
        glm::vec3 bg = e.background;
        if (ImGui::ColorEdit3("Background", &bg.x)) r->setBackground(bg);
    }

    if (section("Camera")) {
        const EnvState& e = r->envState();
        float nearC = e.nearClip, farC = e.farClip;
        bool ch = ImGui::SliderFloat("Near", &nearC, 0.01f, 1.0f, "%.3f");
        ch |= ImGui::SliderFloat("Far", &farC, 100.0f, 8000.0f, "%.0f");
        if (ch) r->setCameraClip(nearC, farC);
        bool wire = e.wireframe;
        if (ImGui::Checkbox("Wireframe (mesh)", &wire)) r->setWireframeDebug(wire);
        ImGui::TextDisabled("FOV is on the Player tab (controller-owned).");
    }
}

// ------------------------------------------------------------- Animation ----
// Stop-motion / OSRS stepping rates, per channel. See eng/StepClock.h for what
// each channel covers and why the camera, player movement and UI are absent.

void DebugTools::drawAnimationTab(const Deps& d)
{
    StepClock* clk = d.steps;
    if (!clk) { ImGui::TextDisabled("Step clock unavailable."); return; }
    StepRates& sr = clk->rates();

    if (section("Master")) {
        ImGui::Checkbox("Stepping enabled", &sr.enabled);
        ImGui::SliderFloat("Rate scale", &sr.scale, 0.25f, 4.0f, "%.2fx");
        ImGui::TextDisabled("Scales every channel at once: <1 chunkier, >1 smoother.");
        ImGui::SliderFloat("Phase jitter", &sr.phaseJitter, 0.0f, 1.0f);
        ImGui::TextDisabled("0 = whole channel snaps on one frame.\n"
                            "1 = each object gets its own step phase, so a\n"
                            "crowd stops moving like a single puppet.");
    }

    if (section("Channels (Hz, 0 = smooth)")) {
        for (int i = 0; i < kStepChannelCount; ++i) {
            const StepChannel c = StepChannel(i);
            ImGui::PushID(i);
            ImGui::SliderFloat(stepChannelName(c), &sr.rate[i], 0.0f, 60.0f,
                               "%.0f");
            // Effective rate after scale/enable, so the slider never lies about
            // what is actually running.
            const float step = clk->stepDuration(c);
            ImGui::SameLine();
            if (step > 0.0f)
                ImGui::TextDisabled("= %.0f Hz (%.0f ms)", 1.0f / step,
                                    step * 1000.0f);
            else
                ImGui::TextDisabled("= smooth");
            ImGui::PopID();
        }
        ImGui::TextDisabled(
            "12 Hz is the classic stop-motion rate (24 fps shot on twos).\n"
            "Keep projectiles well above the rest or arrows teleport.");
    }

    if (section("Presets")) {
        // Whole-look starting points, since the interesting part is the ratio
        // between channels rather than any single number.
        struct Row { const char* name; float ch, vm, wo, pa, pr; const char* hint; };
        static constexpr Row kRows[] = {
            {"Off (smooth)",   0, 0, 0, 0, 0,  "Everything continuous."},
            {"Subtle (24/30)", 24, 24, 24, 24, 60, "Barely stepped; reads as low-budget anim."},
            {"OSRS-ish (15)",  15, 15, 15, 15, 30, "Middle ground; what this game shipped with."},
            {"Stop-motion (12)", 12, 12, 12, 12, 30, "Classic on-twos. The default."},
            {"Puppet (8)",     8, 8, 10, 8, 24, "Heavily stylised; hurts readability."},
        };
        for (const Row& r : kRows) {
            if (ImGui::Button(r.name, ImVec2(-FLT_MIN, 0))) {
                sr.enabled = r.ch > 0.0f || r.pr > 0.0f;
                sr.rate[int(StepChannel::Characters)] = r.ch;
                sr.rate[int(StepChannel::Viewmodel)] = r.vm;
                sr.rate[int(StepChannel::World)] = r.wo;
                sr.rate[int(StepChannel::Particles)] = r.pa;
                sr.rate[int(StepChannel::Projectiles)] = r.pr;
            }
            // The row's own hint stays the fallback, so the table keeps
            // working with no hints.toml present.
            imguihint::hover("debug.step_rate", r.hint);
        }
    }

    if (section("Export")) {
        if (ImGui::Button("Copy as TOML", ImVec2(-FLT_MIN, 0))) {
            char buf[512];
            std::snprintf(buf, sizeof(buf),
                "[animation]\nenabled = %s\nscale = %.3f\nphase_jitter = %.3f\n"
                "characters_fps = %.1f\nviewmodel_fps = %.1f\nworld_fps = %.1f\n"
                "particles_fps = %.1f\nprojectiles_fps = %.1f\n",
                sr.enabled ? "true" : "false", sr.scale, sr.phaseJitter,
                sr.rate[int(StepChannel::Characters)],
                sr.rate[int(StepChannel::Viewmodel)],
                sr.rate[int(StepChannel::World)],
                sr.rate[int(StepChannel::Particles)],
                sr.rate[int(StepChannel::Projectiles)]);
            ImGui::SetClipboardText(buf);
        }
        ImGui::TextDisabled("Paste into the app's config to make it the default.");
    }
}

// --------------------------------------------------------------- Shaders ----
// Every PSX stylize-shader parameter, so a render profile is fully reproducible.

void DebugTools::drawShadersTab(const Deps& d)
{
    Renderer* r = d.renderer;
    if (!r) { ImGui::TextDisabled("Renderer unavailable."); return; }
    RenderPresetValues& v = mState->rp;
    const char* kStylize = "Engine/Psx/PixelStylize";

    if (section("Master")) {
        if (ImGui::Checkbox("Stylize enabled", &v.stylizeEnabled))
            r->setMaterialParam(kStylize, "stylizeEnabled", v.stylizeEnabled ? 1.0f : 0.0f);
    }

    if (section("Ink / Shadow")) {
        if (ImGui::Checkbox("Enabled##ink", &v.inkEnabled))
            r->setMaterialParam(kStylize, "shadowsEnabled", v.inkEnabled ? 1.0f : 0.0f);
        if (ImGui::SliderFloat("Strength##ink", &v.inkStrength, 0.0f, 1.0f))
            r->setMaterialParam(kStylize, "shadowStrength", v.inkStrength);
        if (ImGui::SliderFloat("Threshold##ink", &v.inkThreshold, 0.0f, 1.0f))
            r->setMaterialParam(kStylize, "shadowThreshold", v.inkThreshold);
        if (ImGui::ColorEdit3("Colour##ink", &v.inkColor.x))
            r->setMaterialParam(kStylize, "shadowColor", v.inkColor);
    }

    if (section("Highlights")) {
        if (ImGui::Checkbox("Enabled##hl", &v.highlightsEnabled))
            r->setMaterialParam(kStylize, "highlightsEnabled", v.highlightsEnabled ? 1.0f : 0.0f);
        if (ImGui::SliderFloat("Strength##hl", &v.highlightStrength, 0.0f, 1.0f))
            r->setMaterialParam(kStylize, "highlightStrength", v.highlightStrength);
        if (ImGui::SliderFloat("Threshold##hl", &v.highlightThreshold, 0.0f, 1.0f))
            r->setMaterialParam(kStylize, "highlightThreshold", v.highlightThreshold);
        if (ImGui::SliderFloat("Dark fade##hl", &v.highlightDarkFade, 0.0f, 1.0f))
            r->setMaterialParam(kStylize, "highlightDarkFade", v.highlightDarkFade);
        if (ImGui::Checkbox("Override colour##hl", &v.highlightColorOverride))
            r->setMaterialParam(kStylize, "highlightColorOverride",
                                v.highlightColorOverride ? 1.0f : 0.0f);
        if (ImGui::ColorEdit3("Colour##hl", &v.highlightColor.x)) {
            v.highlightColorOverride = true;
            r->setMaterialParam(kStylize, "highlightColor", v.highlightColor);
            r->setMaterialParam(kStylize, "highlightColorOverride", 1.0f);
        }
    }

    if (section("Outlines")) {
        if (ImGui::Checkbox("Enabled##ol", &v.outlinesEnabled))
            r->setMaterialParam(kStylize, "outlineEnabled", v.outlinesEnabled ? 1.0f : 0.0f);
        if (ImGui::SliderFloat("Opacity##ol", &v.outlineOpacity, 0.0f, 1.0f))
            r->setMaterialParam(kStylize, "outlineOpacity", v.outlineOpacity);
        // Whole render pixels: the shader rounds the kernel offset to a texel,
        // so fractional widths only ever landed on one of these anyway.
        if (ImGui::SliderFloat("Thickness (px)##ol", &v.outlineThickness, 1.0f, 4.0f, "%.0f"))
            r->setMaterialParam(kStylize, "outlineThickness", v.outlineThickness);
        if (ImGui::SliderFloat("Depth sens##ol", &v.outlineDepthSens, 0.0f, 32.0f))
            r->setMaterialParam(kStylize, "outlineDepthSens", v.outlineDepthSens);
        if (ImGui::SliderFloat("Normal sens##ol", &v.outlineNormalSens, 0.0f, 1.0f))
            r->setMaterialParam(kStylize, "outlineNormalSens", v.outlineNormalSens);
        if (ImGui::SliderFloat("Sharpness##ol", &v.outlineSharpness, 0.0f, 1.0f))
            r->setMaterialParam(kStylize, "outlineSharpness", v.outlineSharpness);
        if (ImGui::SliderFloat("Dist fade##ol", &v.outlineDistFade, 0.0f, 1.0f))
            r->setMaterialParam(kStylize, "outlineDistFade", v.outlineDistFade);
        if (ImGui::SliderFloat("Dark fade##ol", &v.outlineDarkFade, 0.0f, 1.0f))
            r->setMaterialParam(kStylize, "outlineDarkFade", v.outlineDarkFade);
        if (ImGui::ColorEdit3("Colour##ol", &v.outlineColor.x))
            r->setMaterialParam(kStylize, "outlineColor", v.outlineColor);
    }

    // Splits interior normal edges between the highlight and the crease terms.
    // At 1.0 a convex fold can only ever be highlighted and a concave one can
    // only ever be inked, which is what makes low-res edges read as geometry.
    if (section("Edge classification")) {
        if (ImGui::SliderFloat("Convex split", &v.edgeConvexity, 0.0f, 1.0f))
            r->setMaterialParam(kStylize, "edgeConvexity", v.edgeConvexity);
        if (ImGui::SliderFloat("Convex bias", &v.edgeConvexBias, 0.001f, 0.5f))
            r->setMaterialParam(kStylize, "edgeConvexBias", v.edgeConvexBias);
        ImGui::TextDisabled("0 = every edge feeds both terms (legacy).");
    }

    if (section("Hardware Resolve")) {
        // 0..7 are every branch hardware_resolve.frag has; anything above 7
        // lands in the same final else, so a wider slider only looks like it
        // does something.
        if (ImGui::SliderFloat("Mode", &v.hardwareResolveMode, 0.0f, 7.0f, "%.0f"))
            r->setMaterialParam("Engine/Psx/HardwareResolve", "resolveMode", v.hardwareResolveMode);
        if (ImGui::SliderFloat("Strength##hr", &v.hardwareResolveStrength, 0.0f, 1.0f))
            r->setMaterialParam("Engine/Psx/HardwareResolve", "resolveStrength", v.hardwareResolveStrength);
    }

    ImGui::Separator();
    if (ImGui::Button("Copy profile as TOML", ImVec2(-FLT_MIN, 0))) {
        char buf[3072];
        std::snprintf(buf, sizeof(buf),
            "[render_profile]\n"
            "pixel_size = %d\ntarget_width = %d\ntarget_height = %d\n"
            "precision_multiplier = %.4f\naffine_amount = %.4f\n"
            "per_pixel = %s\nbanded_lighting = %s\nband_light_steps = %.3f\nstep_softness = %.4f\n"
            "bloom = %s\nbloom_threshold = %.4f\nbloom_intensity = %.4f\nbloom_pixel_snap = %.4f\n"
            "grade_desaturate = %.4f\ngrade_contrast = %.4f\ngrade_saturation = %.4f\n"
            "grade_tint_strength = %.4f\ngrade_black_lift = %.4f\n"
            "grade_shadow = [%.4f, %.4f, %.4f]\ngrade_mid = [%.4f, %.4f, %.4f]\n"
            "col_depth = %.3f\ndither_banding = %.4f\ndither_dark_fade = %.4f\n"
            "vignette = %s\nvignette_strength = %.4f\nvignette_color = [%.4f, %.4f, %.4f]\n"
            "stylize = %s\nink = %s\nink_strength = %.4f\nink_threshold = %.4f\nink_color = [%.4f, %.4f, %.4f]\n"
            "highlights = %s\nhighlight_strength = %.4f\nhighlight_threshold = %.4f\nhighlight_dark_fade = %.4f\nhighlight_color_override = %s\nhighlight_color = [%.4f, %.4f, %.4f]\n"
            "outlines = %s\noutline_opacity = %.4f\noutline_thickness = %.4f\noutline_depth_sens = %.4f\noutline_normal_sens = %.4f\noutline_sharpness = %.4f\noutline_dist_fade = %.4f\noutline_dark_fade = %.4f\noutline_color = [%.4f, %.4f, %.4f]\n"
            "edge_convexity = %.4f\nedge_convex_bias = %.4f\n"
            "hw_resolve_mode = %.3f\nhw_resolve_strength = %.4f\n",
            v.pixelSize, v.targetWidth, v.targetHeight,
            v.precisionMultiplier, v.affineAmount,
            v.perPixel?"true":"false", v.bandedLightingEnabled?"true":"false", v.bandedLightSteps, v.stepSoftness,
            v.bloom?"true":"false", v.bloomThreshold, v.bloomIntensity, v.bloomPixelSnap,
            v.gradeDesaturate, v.gradeContrast, v.gradeSaturation, v.gradeTintStrength, v.gradeBlackLift,
            v.gradeShadow.x, v.gradeShadow.y, v.gradeShadow.z, v.gradeMid.x, v.gradeMid.y, v.gradeMid.z,
            v.colDepth, v.ditherBanding, v.ditherDarkFade,
            v.vignetteEnabled?"true":"false", v.vignetteStrength, v.vignetteColor.x, v.vignetteColor.y, v.vignetteColor.z,
            v.stylizeEnabled?"true":"false", v.inkEnabled?"true":"false", v.inkStrength, v.inkThreshold, v.inkColor.x, v.inkColor.y, v.inkColor.z,
            v.highlightsEnabled?"true":"false", v.highlightStrength, v.highlightThreshold, v.highlightDarkFade, v.highlightColorOverride?"true":"false", v.highlightColor.x, v.highlightColor.y, v.highlightColor.z,
            v.outlinesEnabled?"true":"false", v.outlineOpacity, v.outlineThickness, v.outlineDepthSens, v.outlineNormalSens, v.outlineSharpness, v.outlineDistFade, v.outlineDarkFade, v.outlineColor.x, v.outlineColor.y, v.outlineColor.z,
            v.edgeConvexity, v.edgeConvexBias,
            v.hardwareResolveMode, v.hardwareResolveStrength);
        ImGui::SetClipboardText(buf);
    }
    ImGui::TextDisabled("Copies the live profile to the clipboard.");
}

// -------------------------------------------------------------- Colliders ---

void DebugTools::drawCollidersTab(const Deps& d)
{
    ColliderDebug* c = d.colliders;
    if (!c) { ImGui::TextDisabled("Collider view unavailable."); return; }

    ImGui::Checkbox("Show colliders", &c->enabled);
    ImGui::Separator();

    const char* modes[] = {"By shape", "By layer", "Uniform"};
    ImGui::Combo("Colour mode", &c->colorMode, modes, IM_ARRAYSIZE(modes));
    if (c->colorMode == 2)
        ImGui::ColorEdit3("Uniform colour", &c->uniformColor.x);
    ImGui::SliderFloat("Brightness", &c->brightness, 0.5f, 4.0f);
    ImGui::SliderFloat("Line thickness", &c->thickness, 1.0f, 4.0f);
    ImGui::Checkbox("Include static level", &c->includeStatic);
    ImGui::Checkbox("Characters + sweep", &c->drawCharacters);
    ImGui::Checkbox("Sensors", &c->drawSensors);
    ImGui::SliderFloat("Range (m, 0 = all)", &c->range, 0.0f, 100.0f, "%.0f");
    if (c->range > 0.0f) {
        c->fadeStart = std::min(c->fadeStart, c->range);
        ImGui::SliderFloat("Fade starts (m)", &c->fadeStart, 0.0f, c->range,
                           "%.0f");
    }
    ImGui::TextDisabled("Full-res overlay: crisp, ignores pixelation.");

    ImGui::Separator();
    if (c->colorMode == 0) {
        ImGui::TextDisabled("Shape legend");
        legend({0.30f, 1.00f, 0.45f}, "Box");
        legend({0.30f, 0.85f, 1.00f}, "Sphere");
        legend({1.00f, 0.90f, 0.30f}, "Capsule");
        legend({1.00f, 0.60f, 0.20f}, "Cylinder");
        legend({1.00f, 0.30f, 0.90f}, "Mesh / hull");
    } else if (c->colorMode == 1) {
        // Layer colours come from the application's layer table (the engine has
        // no taxonomy of its own), so the swatches show what the table says
        // rather than a fixed list of names.
        ImGui::TextDisabled("Layer legend: see the app's layer table.");
    }
}

// ---------------------------------------------------------- Player + Stats --

void DebugTools::drawPlayerTab(const Deps& d)
{
    if (section("Frame Stats") && d.frame && d.frame->frameHist) {
        const FrameStatsView& pr = *d.frame;
        float peak = 0.0f;
        for (int i = 0; i < pr.histCount; ++i)
            peak = std::max(peak, pr.frameHist[i]);
        State& st = *mState;
        st.statsSince += ImGui::GetIO().DeltaTime;
        if (st.statsSince >= st.statsInterval) {
            st.statsSince = 0.0f;
            float sum = 0.0f;
            for (int i = 0; i < pr.histCount; ++i)
                sum += pr.frameHist[i];
            st.statsAvg = pr.histCount > 0 ? sum / float(pr.histCount) : 0.0f;
            st.statsPhaseMs.assign(pr.phaseMs, pr.phaseMs + pr.phaseCount);
        }
        const float avg = st.statsAvg;
        ImGui::Text("Frame %.2f ms  (%.0f fps)", avg, avg > 0.0f ? 1000.0f / avg : 0.0f);
        ImGui::PlotLines("##ft", pr.frameHist, pr.histCount, pr.histHead, nullptr,
                         0.0f, std::max(16.7f, peak * 1.1f), ImVec2(-FLT_MIN, 60));
        ImGui::Separator();
        for (int i = 0; i < int(st.statsPhaseMs.size()) && i < pr.phaseCount; ++i)
            ImGui::Text("%-9s %6.2f ms", pr.phaseNames[i], st.statsPhaseMs[size_t(i)]);
        ImGui::SliderFloat("refresh (s)", &st.statsInterval, 0.05f, 2.0f, "%.2f");
    }

    FpsController* f = d.fps;
    if (!f) { ImGui::TextDisabled("FPS controller unavailable."); return; }
    if (section("Movement & Camera")) {
        ImGui::SliderFloat("Move speed", &f->speed(), 0.5f, 12.0f);
        ImGui::SliderFloat("Mouse sens", &f->sensitivity(), 0.0005f, 0.01f, "%.4f");
        float fov = f->baseFov();
        if (ImGui::SliderFloat("Base FOV", &fov, 40.0f, 110.0f, "%.0f"))
            f->setBaseFov(fov);
        // Near/far clip: read the live values off the renderer, write back on
        // change. Tight near planes reduce z-fighting; far drives cull distance.
        if (d.renderer) {
            const EnvState& env = d.renderer->envState();
            float nearC = env.nearClip, farC = env.farClip;
            bool clipChanged = false;
            clipChanged |= ImGui::SliderFloat("Near clip", &nearC, 0.01f, 1.0f, "%.3f");
            clipChanged |= ImGui::SliderFloat("Far clip", &farC, 10.0f, 4000.0f, "%.0f");
            if (clipChanged)
                d.renderer->setCameraClip(nearC, farC);
        }
        ImGui::SeparatorText("Feel");
        ImGui::SliderFloat("Sprint FOV kick", &f->sprintFovKick(), 0.0f, 20.0f, "%.1f");
        ImGui::SliderFloat("Head-bob amount", &f->bobAmount(), 0.0f, 0.1f, "%.3f");
        ImGui::SliderFloat("Head-bob speed", &f->bobSpeed(), 0.0f, 20.0f, "%.1f");
        ImGui::Separator();
        ImGui::Text("Horizontal speed: %.2f m/s", f->horizontalSpeed());
        ImGui::ProgressBar(f->sprintStamina(), ImVec2(-FLT_MIN, 0), "sprint stamina");
    }
}

// -------------------------------------------------------------- Materials ---

void DebugTools::drawMaterialsTab(const Deps& d)
{
    Renderer* r = d.renderer;
    if (!r) { ImGui::TextDisabled("Renderer unavailable."); return; }
    State& s = *mState;

    if (section("Custom Global Param")) {
        ImGui::InputText("Param", s.paramName, sizeof(s.paramName));
        ImGui::SliderFloat("Value", &s.paramValue, -2.0f, 4.0f);
        if (ImGui::Button("Set global", ImVec2(-FLT_MIN, 0)))
            r->setGlobalMaterialParam(s.paramName, s.paramValue);
        ImGui::TextDisabled("Sets a float on every material declaring it.");
    }

    // The staging swatch, the same rig the editor's material stage uses. It
    // lives here rather than only in the editor because "what does this
    // material actually look like" is a question you ask from the game and the
    // demo too, and the rig is a renderer concern (eng::MaterialPreview).
    if (section("Preview")) {
        if (!s.preview.thumbnailBuilt())
            s.preview.buildThumbnail(*r, kPreviewSwatch);
        if (s.preview.thumbnailBuilt()) {
            const std::vector<std::string>& names = s.materialList;
            const std::string wanted =
                s.materialIdx >= 0 && s.materialIdx < int(names.size())
                    ? names[size_t(s.materialIdx)]
                    : std::string();
            if (!wanted.empty() && wanted != s.preview.thumbnailMaterial())
                s.preview.setThumbnailMaterial(*r, wanted);
            // Turntable: a material's angular behaviour is most of what the
            // sphere rig exists to show, and a still swatch shows none of it.
            s.preview.spinThumbnail(*r, s.preview.thumbnailSpin() +
                                            ImGui::GetIO().DeltaTime * 0.6f);
            const uint64_t id = r->materialThumbnailTextureId();
            if (id != 0)
                ImGui::Image(ImTextureID(id),
                             ImVec2(float(kPreviewSwatch),
                                    float(kPreviewSwatch)));
            ImGui::TextDisabled("%s", s.preview.thumbnailMaterial().empty()
                                          ? "pick a material below"
                                          : s.preview.thumbnailMaterial().c_str());
        } else {
            ImGui::TextDisabled("Preview target unavailable.");
        }
    }

    if (section("Loaded Materials")) {
        s.materialList = r->materialNames();
        const std::vector<std::string>& names = s.materialList;
        ImGui::TextDisabled("%zu materials loaded", names.size());
        if (ImGui::BeginListBox("##mats", ImVec2(-FLT_MIN, 240))) {
            for (int i = 0; i < int(names.size()); ++i)
                if (ImGui::Selectable(names[size_t(i)].c_str(), i == s.materialIdx))
                    s.materialIdx = i;
            ImGui::EndListBox();
        }
    }
}

// -------------------------------------------------------------- PerfOverlay -

void drawColliderOverlay(Physics& physics, Renderer& renderer,
                         const ColliderDebug& view,
                         const ColliderOverlayOptions& options)
{
    if (!view.enabled)
        return;

    // Line buffer is static: this runs every frame while the overlay is on, and
    // the shape count is stable, so the allocation should happen once.
    static std::vector<Physics::DebugLine> lines;
    lines.clear();
    Physics::DebugDrawOptions draw;
    draw.palette = (view.colorMode == 1) ? Physics::ColliderPalette::ByLayer
                                         : Physics::ColliderPalette::ByShape;
    draw.include = view.includeStatic ? kAllLayers : ~options.staticLayers;
    draw.viewer = options.viewer;
    draw.range = view.range;
    draw.fadeStart = view.fadeStart;
    draw.drawCharacters = view.drawCharacters;
    draw.drawSensors = view.drawSensors;
    draw.sweepDt = options.sweepDt;
    physics.debugDraw(lines, draw);

    const glm::mat4 vp = renderer.cameraViewProj();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImDrawList* out = ImGui::GetBackgroundDrawList();
    const bool uniform = view.colorMode == 2;
    const auto project = [&](const glm::vec3& world, ImVec2& screen) {
        const glm::vec4 c = vp * glm::vec4(world, 1.0f);
        if (c.w <= 1e-4f)
            return false; // behind the camera
        screen = ImVec2((c.x / c.w * 0.5f + 0.5f) * ds.x,
                        (1.0f - (c.y / c.w * 0.5f + 0.5f)) * ds.y);
        return true;
    };
    for (const Physics::DebugLine& l : lines) {
        ImVec2 a, b;
        if (!project(l.a, a) || !project(l.b, b))
            continue; // skip segments crossing the near plane
        glm::vec3 col = uniform ? view.uniformColor : l.colour;
        col = glm::clamp(col * view.brightness, 0.0f, 1.0f);
        // 70% opacity so colliders read as an overlay, not solid geometry; the
        // rest of the frame stays fully opaque.
        const ImU32 packed = IM_COL32(int(col.r * 255), int(col.g * 255),
                                      int(col.b * 255), 179);
        out->AddLine(a, b, packed, view.thickness);
    }
}

void PerfOverlay::draw(const FrameStatsView* frame, Renderer* renderer)
{
    if (!mVisible || !frame || !frame->frameHist)
        return;

    // Frame-time stats over the rolling history ring, resampled on a slow
    // cadence so the digits are readable (see setRefreshInterval).
    mSince += ImGui::GetIO().DeltaTime;
    if (mSince >= mInterval) {
        mSince = 0.0f;
        float sum = 0.0f, peak = 0.0f, low = 1e9f;
        for (int i = 0; i < frame->histCount; ++i) {
            const float m = frame->frameHist[i];
            sum += m;
            peak = std::max(peak, m);
            if (m > 0.0f) low = std::min(low, m);
        }
        mAvg = frame->histCount > 0 ? sum / float(frame->histCount) : 0.0f;
        mFps = mAvg > 0.0f ? 1000.0f / mAvg : 0.0f;
        mPeak = peak;
        mLow = low >= 1e8f ? 0.0f : low;
        mPhaseMs.assign(frame->phaseMs, frame->phaseMs + frame->phaseCount);
        if (renderer) {
            size_t b = 0, t = 0;
            renderer->frameStats(b, t);
            mBatches = b;
            mTris = t;
        }
    }
    const float avg = mAvg, fps = mFps, peak = mPeak, low = mLow;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 8, vp->WorkPos.y + 8),
                            ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    if (!ImGui::Begin("##perf", nullptr, flags)) {
        ImGui::End();
        return;
    }

    // Colour the FPS by health (green >=58, yellow >=30, red below).
    const ImVec4 col = fps >= 58.0f ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                     : fps >= 30.0f ? ImVec4(1.0f, 0.9f, 0.3f, 1.0f)
                                    : ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
    ImGui::TextColored(col, "%.0f FPS", fps);
    ImGui::SameLine();
    ImGui::Text("  %.2f ms  (min %.2f / max %.2f)", avg, low, peak);

    ImGui::PlotLines("##perfft", frame->frameHist, frame->histCount,
                     frame->histHead, nullptr, 0.0f, std::max(33.3f, peak * 1.1f),
                     ImVec2(220, 40));

    ImGui::Separator();
    // CPU phase breakdown as labelled bars (fraction of the largest phase).
    float phaseMax = 1e-3f;
    for (float ms : mPhaseMs)
        phaseMax = std::max(phaseMax, ms);
    for (int i = 0; i < int(mPhaseMs.size()) && i < frame->phaseCount; ++i) {
        char lbl[32];
        std::snprintf(lbl, sizeof(lbl), "%.2f ms", mPhaseMs[size_t(i)]);
        ImGui::Text("%-8s", frame->phaseNames[i]);
        ImGui::SameLine(72);
        ImGui::ProgressBar(mPhaseMs[size_t(i)] / phaseMax, ImVec2(120, 12), lbl);
    }

    if (renderer) {
        ImGui::Separator();
        const unsigned long long batches = mBatches, tris = mTris;
        ImGui::Text("Draw calls: %llu", batches);
        ImGui::Text("Triangles : %s",
                    [&] {
                        static char b[32];
                        if (tris >= 1000000)
                            std::snprintf(b, sizeof(b), "%.2fM", double(tris) / 1e6);
                        else if (tris >= 1000)
                            std::snprintf(b, sizeof(b), "%.1fk", double(tris) / 1e3);
                        else
                            std::snprintf(b, sizeof(b), "%llu", tris);
                        return b;
                    }());
    }
    ImGui::Separator();
    ImGui::End();
}

} // namespace eng
