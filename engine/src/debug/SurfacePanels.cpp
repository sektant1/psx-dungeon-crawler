#include <eng/debug/SurfacePanels.h>

#include <eng/Renderer.h>

#include <imgui.h>

#include <cfloat>
#include <cstdio>
#include <utility>

namespace eng {

namespace {

bool section(const char* label)
{
    return ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
}

// One material's controls. Every widget writes straight through to the live
// material, so an edit lands on the next frame with no apply step.
struct MatEdit {
    Renderer* r;
    std::string material;

    bool knob(const char* label, const char* param, float& value, float lo,
              float hi, const char* fmt = "%.3f") const
    {
        if (!ImGui::SliderFloat(label, &value, lo, hi, fmt))
            return false;
        r->setMaterialParam(material, param, value);
        return true;
    }
    // HDR on purpose: the hot tones are deliberately above 1.0, which is what
    // feeds them into the bloom pass. A clamped picker would quietly cap them.
    bool tone(const char* label, const char* param, glm::vec4& value) const
    {
        if (!ImGui::ColorEdit3(label, &value.x,
                               ImGuiColorEditFlags_HDR |
                                   ImGuiColorEditFlags_Float))
            return false;
        r->setMaterialParam(material, param, value);
        return true;
    }
    bool flow(const char* label, const char* param, glm::vec2& value,
              float range) const
    {
        if (!ImGui::SliderFloat2(label, &value.x, -range, range, "%.3f"))
            return false;
        r->setMaterialParam(material, param, value);
        return true;
    }
};

// The selector both tabs open with. Returns the (possibly changed) index.
int profileCombo(const char* id, int index, int count,
                 const std::function<const char*(int)>& label)
{
    if (count <= 1)
        return 0;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo(id, label(index))) {
        for (int i = 0; i < count; ++i)
            if (ImGui::Selectable(label(i), i == index))
                index = i;
        ImGui::EndCombo();
    }
    return index;
}

} // namespace

void SurfacePanels::addPortal(std::string label, std::string material,
                              PortalTuning tuning, std::string texture)
{
    mPortals.push_back(
        {std::move(label), std::move(material), std::move(texture), tuning});
}

void SurfacePanels::addLiquid(std::string label, std::string material,
                              LiquidTuning tuning)
{
    VfxProfile profile;
    profile.kind = VfxProfile::Kind::Liquid;
    profile.label = std::move(label);
    profile.material = std::move(material);
    profile.liquid = tuning;
    mVfx.push_back(std::move(profile));
}

void SurfacePanels::addLava(std::string label, std::string material,
                            LavaTuning tuning)
{
    VfxProfile profile;
    profile.kind = VfxProfile::Kind::Lava;
    profile.label = std::move(label);
    profile.material = std::move(material);
    profile.lava = tuning;
    mVfx.push_back(std::move(profile));
}

void SurfacePanels::setPortalDressing(std::function<void(int)> draw)
{
    mPortalDressing = std::move(draw);
}

void SurfacePanels::install(DebugTools& tools)
{
    // Both dock into the Content group, beside the render controls they
    // interact with: emission is only half an effect without the bloom pass.
    if (!mPortals.empty())
        tools.addPanel(
            "Portal", [this] { drawPortalTab(); }, PanelGroup::Content);
    if (!mVfx.empty())
        tools.addPanel("VFX", [this] { drawVfxTab(); }, PanelGroup::Content);
}

// ------------------------------------------------------------------ Portal --
// Live tuning for the portal membrane shader (engine/assets/shaders/
// portal.frag over surface_common.glsl). The swirl changes under the cursor;
// nothing is cached engine-side and nothing needs a rebuild. The Copy button
// emits the block to paste back into the app's .material file -- that file, not
// this panel, is where a tuned portal is *kept*.

void SurfacePanels::drawPortalTab()
{
    Renderer* r = mRenderer;
    if (!r) {
        ImGui::TextDisabled("Renderer unavailable.");
        return;
    }
    mPortalIdx = profileCombo(
        "##portalprofile", mPortalIdx, int(mPortals.size()),
        [this](int i) { return mPortals[size_t(i)].label.c_str(); });
    PortalProfile& profile = mPortals[size_t(mPortalIdx)];
    PortalTuning& t = profile.tuning;
    const std::string& material = profile.material;

    const MatEdit edit{r, material};
    const auto knob = [&](const char* label, const char* param, float& value,
                          float lo, float hi, const char* fmt = "%.3f") {
        edit.knob(label, param, value, lo, hi, fmt);
    };
    const auto tone = [&](const char* label, const char* param,
                          glm::vec4& value) { edit.tone(label, param, value); };

    if (section("Palette")) {
        ImGui::PushID("pal");
        tone("Void", "surfaceDark", t.dark);
        tone("Body", "surfaceMid", t.mid);
        tone("Arms", "surfaceBright", t.bright);
        tone("Core", "surfaceCore", t.core);
        knob("Brightness", "surfaceBrightness", t.brightness, 0.2f, 3.0f);
        knob("Dither", "surfaceDither", t.dither, 0.0f, 1.0f);
        ImGui::PopID();
    }

    if (section("Motion")) {
        ImGui::PushID("mot");
        knob("Step FPS", "surfaceStepFps", t.stepFps, 1.0f, 60.0f, "%.0f");
        knob("Flow (inward)", "portalFlowSpeed", t.flowSpeed, -1.5f, 1.5f);
        knob("Swirl", "portalSwirlSpeed", t.swirlSpeed, -1.0f, 1.0f);
        knob("Twist", "portalTwist", t.twist, -1.0f, 1.0f);
        knob("Arms", "portalArms", t.arms, 1.0f, 8.0f, "%.0f");
        knob("Arm width", "portalArmWidth", t.armWidth, 0.02f, 0.98f);
        ImGui::TextDisabled("Whole arm counts keep the swirl seamless.\n"
                            "Arm width 0.5 = even arm/gap; low = threads,\n"
                            "high = the gaps become the pattern.");
        ImGui::PopID();
    }

    if (section("Depth / resolution")) {
        ImGui::PushID("res");
        knob("Texel size (m)", "surfaceTexelSize", t.texelSize, 0.0f, 0.20f);
        ImGui::TextDisabled("0 falls back to the fixed grid below.");
        knob("Pixel grid", "surfacePixelGrid", t.pixelGrid, 8.0f, 128.0f,
             "%.0f");
        knob("Tunnel depth", "portalDepthScale", t.depthScale, 0.1f, 2.0f);
        knob("Parallax (m)", "portalParallax", t.parallax, 0.0f, 1.5f);
        knob("Field vs spiral", "portalFieldWeight", t.fieldWeight, 0.0f, 1.0f);
        ImGui::PopID();
    }

    if (section("Shape")) {
        ImGui::PushID("shape");
        knob("Core radius", "portalCoreRadius", t.coreRadius, 0.0f, 0.35f);
        knob("Core boost", "portalCoreBoost", t.coreBoost, 0.0f, 1.0f);
        knob("Ring radius", "portalRimRadius", t.rimRadius, 0.0f, 0.60f);
        knob("Ring width", "portalRimWidth", t.rimWidth, 0.005f, 0.20f);
        knob("Ring intensity", "portalRimIntensity", t.rimIntensity, 0.0f,
             1.5f);
        knob("Corner fade", "portalEdgeFade", t.edgeFade, 0.0f, 0.5f);
        ImGui::PopID();
    }

    // The knob the palette cannot express. Sits directly above the global bloom
    // section because the two are one workflow: this decides what colour and
    // how much crosses the threshold, that decides what the threshold is.
    if (section("Emission (what blooms)")) {
        ImGui::PushID("glow");
        tone("Glow colour", "surfaceGlowColour", t.glowColour);
        knob("Glow strength", "surfaceGlowStrength", t.glowStrength, 0.0f,
             3.0f);
        knob("Glow spread", "surfaceGlowThreshold", t.glowThreshold, 0.0f,
             0.99f);
        ImGui::TextDisabled("Added on top of the palette, so THIS is the\n"
                            "colour that blooms. Spread 1 = core only,\n"
                            "0 = the whole swirl glows.");
        ImGui::PopID();
    }

    // Bloom is global, not the portal's, but the portal's core and arms are
    // authored above 1.0 specifically to feed it -- so tuning the glow here,
    // next to the tones that cause it, beats bouncing to the Render tab.
    // Labelled so the scope is not a surprise.
    if (section("Bloom (global post)")) {
        ImGui::PushID("bloom");
        ImGui::TextDisabled("Whole frame, not just the portal.");
        if (ImGui::Checkbox("Enabled", &mBloom.enabled))
            r->setBloomEnabled(mBloom.enabled);
        bool changed =
            ImGui::SliderFloat("Threshold", &mBloom.threshold, 0.0f, 2.0f);
        changed |=
            ImGui::SliderFloat("Intensity", &mBloom.intensity, 0.0f, 2.0f);
        if (changed)
            r->setBloomParams(mBloom.threshold, mBloom.intensity);
        ImGui::TextDisabled("Lower threshold = more of the frame glows.");
        ImGui::PopID();
    }

    if (section("Slab rims")) {
        ImGui::PushID("rim");
        ImGui::TextDisabled("The four faces closing the membrane's thickness.");
        knob("Glow", "surfaceEdgeGlow", t.edgeGlow, 0.0f, 2.0f);
        knob("Band flow", "surfaceEdgeFlow", t.edgeFlow, -3.0f, 3.0f);
        static const char* kEdgeModes[] = {"Continue the face pattern",
                                           "Band of their own", "Hidden"};
        if (ImGui::Combo("Rim mode", &t.edgeMode, kEdgeModes, 3))
            r->setMaterialParam(material, "surfaceEdgeMode", float(t.edgeMode));
        ImGui::TextDisabled("Glow/flow above apply to \"band of their own\".\n"
                            "Hidden discards, so the rims stop writing depth\n"
                            "too -- dimming cannot hide them, the palette's\n"
                            "first tone is a colour and not black.");
        ImGui::PopID();
    }

    // Whatever the app owns that is not the shader: the game's portal prop
    // light and wisps. Drawn last, because it is the only part of this tab that
    // can be unavailable.
    if (mPortalDressing && section("Dressing")) {
        ImGui::PushID("dress");
        mPortalDressing(mPortalIdx);
        ImGui::PopID();
    }

    ImGui::Separator();
    if (ImGui::Button("Push all", ImVec2(-FLT_MIN, 0))) {
        r->setMaterialParam(material, "surfaceDark", t.dark);
        r->setMaterialParam(material, "surfaceMid", t.mid);
        r->setMaterialParam(material, "surfaceBright", t.bright);
        r->setMaterialParam(material, "surfaceCore", t.core);
        r->setMaterialParam(material, "surfaceStepFps", t.stepFps);
        r->setMaterialParam(material, "portalFlowSpeed", t.flowSpeed);
        r->setMaterialParam(material, "portalSwirlSpeed", t.swirlSpeed);
        r->setMaterialParam(material, "portalTwist", t.twist);
        r->setMaterialParam(material, "portalArms", t.arms);
        r->setMaterialParam(material, "portalArmWidth", t.armWidth);
        r->setMaterialParam(material, "surfaceTexelSize", t.texelSize);
        r->setMaterialParam(material, "surfacePixelGrid", t.pixelGrid);
        r->setMaterialParam(material, "portalDepthScale", t.depthScale);
        r->setMaterialParam(material, "portalParallax", t.parallax);
        r->setMaterialParam(material, "portalFieldWeight", t.fieldWeight);
        r->setMaterialParam(material, "portalCoreRadius", t.coreRadius);
        r->setMaterialParam(material, "portalCoreBoost", t.coreBoost);
        r->setMaterialParam(material, "portalRimRadius", t.rimRadius);
        r->setMaterialParam(material, "portalRimWidth", t.rimWidth);
        r->setMaterialParam(material, "portalRimIntensity", t.rimIntensity);
        r->setMaterialParam(material, "portalEdgeFade", t.edgeFade);
        r->setMaterialParam(material, "surfaceDither", t.dither);
        r->setMaterialParam(material, "surfaceEdgeGlow", t.edgeGlow);
        r->setMaterialParam(material, "surfaceEdgeFlow", t.edgeFlow);
        r->setMaterialParam(material, "surfaceEdgeMode", float(t.edgeMode));
        r->setMaterialParam(material, "surfaceGlowColour", t.glowColour);
        r->setMaterialParam(material, "surfaceGlowStrength", t.glowStrength);
        r->setMaterialParam(material, "surfaceGlowThreshold", t.glowThreshold);
        r->setMaterialParam(material, "surfaceBrightness", t.brightness);
    }
    // The WHOLE material, not just the param block: a bare fragment_program_ref
    // pasted over a material's opening lines silently eats `technique { pass {`
    // and its vertex_program_ref, and the result is a parse error at startup
    // that reads as a shader bug. Replacing the material entire cannot do that.
    if (ImGui::Button("Copy as material", ImVec2(-FLT_MIN, 0))) {
        char buf[2560];
        std::snprintf(
            buf, sizeof(buf),
            "material %s\n{\n    technique { pass {\n"
            "        cull_hardware none\n        depth_write on\n"
            "        vertex_program_ref PixelVfx/SurfaceVS { }\n"
            "        fragment_program_ref PixelVfx/PortalFS\n        {\n"
            "            param_named surfaceDark float4 %.3f %.3f %.3f 1.0\n"
            "            param_named surfaceMid float4 %.3f %.3f %.3f 1.0\n"
            "            param_named surfaceBright float4 %.3f %.3f %.3f 1.0\n"
            "            param_named surfaceCore float4 %.3f %.3f %.3f 1.0\n"
            "            param_named surfaceStepFps float %.1f\n"
            "            param_named portalFlowSpeed float %.3f\n"
            "            param_named portalSwirlSpeed float %.3f\n"
            "            param_named portalTwist float %.3f\n"
            "            param_named portalArms float %.0f\n"
            "            param_named portalArmWidth float %.3f\n"
            "            param_named surfaceTexelSize float %.4f\n"
            "            param_named surfacePixelGrid float %.0f\n"
            "            param_named portalDepthScale float %.3f\n"
            "            param_named portalParallax float %.3f\n"
            "            param_named portalFieldWeight float %.3f\n"
            "            param_named portalCoreRadius float %.3f\n"
            "            param_named portalCoreBoost float %.3f\n"
            "            param_named portalRimRadius float %.3f\n"
            "            param_named portalRimWidth float %.3f\n"
            "            param_named portalRimIntensity float %.3f\n"
            "            param_named portalEdgeFade float %.3f\n"
            "            param_named surfaceDither float %.3f\n"
            "            param_named surfaceEdgeGlow float %.3f\n"
            "            param_named surfaceEdgeFlow float %.3f\n"
            "            param_named surfaceEdgeMode float %.1f\n"
            "            param_named surfaceGlowColour float4 %.3f %.3f %.3f "
            "1.0\n"
            "            param_named surfaceGlowStrength float %.3f\n"
            "            param_named surfaceGlowThreshold float %.3f\n"
            "            param_named surfaceBrightness float %.3f\n"
            "        }\n"
            "        texture_unit\n        {\n            texture %s\n"
            "            filtering none\n            tex_address_mode wrap\n"
            "        }\n    } }\n}\n",
            material.c_str(), t.dark.x, t.dark.y, t.dark.z, t.mid.x, t.mid.y,
            t.mid.z, t.bright.x, t.bright.y, t.bright.z, t.core.x, t.core.y,
            t.core.z, t.stepFps, t.flowSpeed, t.swirlSpeed, t.twist, t.arms,
            t.armWidth, t.texelSize, t.pixelGrid, t.depthScale, t.parallax,
            t.fieldWeight, t.coreRadius, t.coreBoost, t.rimRadius, t.rimWidth,
            t.rimIntensity, t.edgeFade, t.dither, t.edgeGlow, t.edgeFlow,
            float(t.edgeMode), t.glowColour.x, t.glowColour.y, t.glowColour.z,
            t.glowStrength, t.glowThreshold, t.brightness,
            profile.texture.empty() ? "slime_stylized.png"
                                    : profile.texture.c_str());
        ImGui::SetClipboardText(buf);
    }
    ImGui::TextDisabled("Replaces the whole material block in the\n"
                        "app's vfx material file.");
}

// --------------------------------------------------------------------- VFX --
// Water, slime and lava: the scrolling family (scroll_common.glsl). Same
// contract as the Portal tab -- every widget writes straight through to the
// material, and Copy emits the block to paste back into the .material.

void SurfacePanels::drawVfxTab()
{
    Renderer* r = mRenderer;
    if (!r) {
        ImGui::TextDisabled("Renderer unavailable.");
        return;
    }
    mVfxIdx =
        profileCombo("##vfxprofile", mVfxIdx, int(mVfx.size()),
                     [this](int i) { return mVfx[size_t(i)].label.c_str(); });
    VfxProfile& profile = mVfx[size_t(mVfxIdx)];
    const MatEdit edit{r, profile.material};

    if (profile.kind == VfxProfile::Kind::Lava) {
        LavaTuning& t = profile.lava;
        if (section("Palette")) {
            ImGui::PushID("lp");
            edit.tone("Rock", "lavaDark", t.dark);
            edit.tone("Crust", "lavaCrust", t.crust);
            edit.tone("Hot", "lavaHot", t.hot);
            edit.tone("Core", "lavaCore", t.core);
            ImGui::TextDisabled("Hot and Core run above 1.0 so the bloom pass\n"
                                "catches them -- that is the molten read.");
            ImGui::PopID();
        }
        if (section("Motion")) {
            ImGui::PushID("lm");
            edit.knob("Step FPS", "lavaStepFps", t.stepFps, 1.0f, 60.0f,
                      "%.0f");
            edit.knob("Flow speed", "lavaFlowSpeed", t.flowSpeed, -1.0f, 1.0f);
            edit.knob("Pixel grid", "lavaPixelGrid", t.pixelGrid, 8.0f, 128.0f,
                      "%.0f");
            ImGui::TextDisabled("Lava warps its own domain rather than\n"
                                "crossing two scrolling layers.");
            ImGui::PopID();
        }
        ImGui::Separator();
        if (ImGui::Button("Copy as material params", ImVec2(-FLT_MIN, 0))) {
            char buf[1024];
            std::snprintf(
                buf, sizeof(buf),
                "            param_named lavaDark float4 %.3f %.3f %.3f 1.0\n"
                "            param_named lavaCrust float4 %.3f %.3f %.3f 1.0\n"
                "            param_named lavaHot float4 %.3f %.3f %.3f 1.0\n"
                "            param_named lavaCore float4 %.3f %.3f %.3f 1.0\n"
                "            param_named lavaStepFps float %.1f\n"
                "            param_named lavaPixelGrid float %.0f\n"
                "            param_named lavaFlowSpeed float %.3f\n",
                t.dark.x, t.dark.y, t.dark.z, t.crust.x, t.crust.y, t.crust.z,
                t.hot.x, t.hot.y, t.hot.z, t.core.x, t.core.y, t.core.z,
                t.stepFps, t.pixelGrid, t.flowSpeed);
            ImGui::SetClipboardText(buf);
        }
    }
    else {
        LiquidTuning& t = profile.liquid;
        if (section("Palette")) {
            ImGui::PushID("qp");
            edit.tone("Deep", "liquidDark", t.dark);
            edit.tone("Body", "liquidMid", t.mid);
            edit.tone("Highlight", "liquidBright", t.bright);
            edit.knob("Emission", "liquidEmission", t.emission, 0.0f, 1.5f);
            ImGui::TextDisabled("Emission boosts the brightest band only, in\n"
                                "the highlight colour -- that is what makes\n"
                                "slime glow and water not.");
            ImGui::PopID();
        }
        if (section("Flow")) {
            ImGui::PushID("qf");
            edit.flow("Layer A", "liquidFlowA", t.flowA, 0.4f);
            edit.flow("Layer B", "liquidFlowB", t.flowB, 0.4f);
            edit.knob("Step FPS", "liquidStepFps", t.stepFps, 1.0f, 60.0f,
                      "%.0f");
            edit.knob("Pixel grid", "liquidPixelGrid", t.pixelGrid, 8.0f,
                      128.0f, "%.0f");
            ImGui::TextDisabled(
                "Two tiling layers at different scales and\n"
                "directions: one sliding texture reads as a\n"
                "sliding texture, two crossing read as current.");
            ImGui::PopID();
        }
        ImGui::Separator();
        if (ImGui::Button("Copy as material params", ImVec2(-FLT_MIN, 0))) {
            char buf[1024];
            std::snprintf(
                buf, sizeof(buf),
                "            param_named liquidDark float4 %.3f %.3f %.3f 1.0\n"
                "            param_named liquidMid float4 %.3f %.3f %.3f 1.0\n"
                "            param_named liquidBright float4 %.3f %.3f %.3f "
                "1.0\n"
                "            param_named liquidFlowA float2 %.3f %.3f\n"
                "            param_named liquidFlowB float2 %.3f %.3f\n"
                "            param_named liquidStepFps float %.1f\n"
                "            param_named liquidPixelGrid float %.0f\n"
                "            param_named liquidEmission float %.3f\n",
                t.dark.x, t.dark.y, t.dark.z, t.mid.x, t.mid.y, t.mid.z,
                t.bright.x, t.bright.y, t.bright.z, t.flowA.x, t.flowA.y,
                t.flowB.x, t.flowB.y, t.stepFps, t.pixelGrid, t.emission);
            ImGui::SetClipboardText(buf);
        }
    }
    ImGui::TextDisabled("Paste into the app's vfx material file.");
}

} // namespace eng
