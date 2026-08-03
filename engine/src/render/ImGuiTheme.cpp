#include "eng/render/ImGuiTheme.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace eng::imguitheme {
namespace {

struct Entry {
    std::string id;
    std::function<void()> fn;
};

// Vector, not map: registration order is the order the debug UI lists them in.
std::vector<Entry>& registry()
{
    static std::vector<Entry> r;
    return r;
}

std::string& currentId()
{
    static std::string id;
    return id;
}

constexpr ImVec4 rgba(unsigned argb)
{
    return ImVec4(
        float((argb >> 16) & 0xFF) / 255.0f, float((argb >> 8) & 0xFF) / 255.0f,
        float(argb & 0xFF) / 255.0f, float((argb >> 24) & 0xFF) / 255.0f);
}

ImVec4 lerp(const ImVec4& a, const ImVec4& b, float t)
{
    return ImVec4(std::lerp(a.x, b.x, t), std::lerp(a.y, b.y, t),
                  std::lerp(a.z, b.z, t), std::lerp(a.w, b.w, t));
}

// Raven editor chrome.
//
// Palette sampled from docs/references logo art: black void, layered gunmetal,
// cold chrome and one red optic. Red is scarce by contract: active, selected,
// keyboard-focused and destructive states only. Ordinary hover remains grey.
void ravenEditor()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);

    style.Alpha = 1.0f;
    style.DisabledAlpha = 0.55f;

    style.WindowPadding = ImVec2(9.0f, 8.0f);
    style.FramePadding = ImVec2(7.0f, 4.0f);
    style.CellPadding = ImVec2(7.0f, 4.0f);
    style.ItemSpacing = ImVec2(7.0f, 5.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 13.0f;
    style.GrabMinSize = 10.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.TabBorderSize = 1.0f;
    style.TabBarBorderSize = 1.0f;
    style.DockingSeparatorSize = 2.0f;

    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.PopupRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.TabRounding = 0.0f;

    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.5f);

    const ImVec4 kVoid = rgba(0xFF090B0E);
    const ImVec4 kCanvas = rgba(0xFF0D1014);
    const ImVec4 kPanel = rgba(0xFF14191F);
    const ImVec4 kRaised = rgba(0xFF1C2229);
    const ImVec4 kControl = rgba(0xFF20272F);
    const ImVec4 kHover = rgba(0xFF2B343E);
    const ImVec4 kSelected = rgba(0xFF3A1A1E);
    const ImVec4 kEdge = rgba(0xFF3E4853);
    const ImVec4 kEdgeBright = rgba(0xFF64707C);
    const ImVec4 kText = rgba(0xFFD9DEE3);
    const ImVec4 kTextDim = rgba(0xFF98A1AA);
    const ImVec4 kChrome = rgba(0xFFBAC2CA);
    const ImVec4 kAccent = rgba(0xFFC63A40);
    const ImVec4 kAccentHover = rgba(0xFFE15258);
    const ImVec4 kAccentPressed = rgba(0xFF91292F);
    const ImVec4 kAccentDark = rgba(0xFF451B20);
    const ImVec4 kFocus = rgba(0xFFF06A70);
    const ImVec4 kInfo = rgba(0xFF7296B3);
    const ImVec4 kWarning = rgba(0xFFC79A50);
    const ImVec4 kDanger = rgba(0xFFE36A55);

    const auto alpha = [](const ImVec4& color, float value) {
        return ImVec4(color.x, color.y, color.z, value);
    };

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = kText;
    c[ImGuiCol_TextDisabled] = kTextDim;

    c[ImGuiCol_WindowBg] = kPanel;
    c[ImGuiCol_ChildBg] = kVoid;
    c[ImGuiCol_PopupBg] = alpha(kRaised, 0.98f);

    c[ImGuiCol_Border] = kEdge;
    c[ImGuiCol_BorderShadow] = alpha(kVoid, 0.0f);

    c[ImGuiCol_FrameBg] = kControl;
    c[ImGuiCol_FrameBgHovered] = kHover;
    c[ImGuiCol_FrameBgActive] = kAccentDark;

    c[ImGuiCol_TitleBg] = kVoid;
    c[ImGuiCol_TitleBgActive] = kRaised;
    c[ImGuiCol_TitleBgCollapsed] = alpha(kVoid, 0.86f);
    c[ImGuiCol_MenuBarBg] = kCanvas;

    c[ImGuiCol_ScrollbarBg] = kVoid;
    c[ImGuiCol_ScrollbarGrab] = kControl;
    c[ImGuiCol_ScrollbarGrabHovered] = kEdge;
    c[ImGuiCol_ScrollbarGrabActive] = kAccentPressed;

    c[ImGuiCol_CheckMark] = kAccent;
    c[ImGuiCol_SliderGrab] = kChrome;
    c[ImGuiCol_SliderGrabActive] = kAccentHover;

    c[ImGuiCol_Button] = kControl;
    c[ImGuiCol_ButtonHovered] = kHover;
    c[ImGuiCol_ButtonActive] = kAccentDark;

    c[ImGuiCol_Header] = kControl;
    c[ImGuiCol_HeaderHovered] = kHover;
    c[ImGuiCol_HeaderActive] = kSelected;

    c[ImGuiCol_Separator] = kEdge;
    c[ImGuiCol_SeparatorHovered] = kEdgeBright;
    c[ImGuiCol_SeparatorActive] = kAccent;

    c[ImGuiCol_ResizeGrip] = alpha(kEdge, 0.45f);
    c[ImGuiCol_ResizeGripHovered] = kEdgeBright;
    c[ImGuiCol_ResizeGripActive] = kAccent;

    // A tab is the front edge of the panel it opens, so the selected one is the
    // panel's own colour and the rest sit *below* it. The values were the other
    // way round -- the unselected tab (kControl) was lighter than the selected
    // one (kRaised) and neither matched WindowBg -- which reads as the inactive
    // tab being the active one. With two tabs in a rail (Hierarchy beside Asset
    // Browser) that is a rail where you cannot tell what you are looking at.
    c[ImGuiCol_TabSelected] = kPanel;
    c[ImGuiCol_Tab] = lerp(kPanel, kVoid, 0.55f);
    c[ImGuiCol_TabHovered] = lerp(kPanel, kHover, 0.75f);
    c[ImGuiCol_TabSelectedOverline] = kAccent;
    // Unfocused: the same order, flattened toward the void so a background dock
    // recedes as a group rather than each tab dimming on its own.
    c[ImGuiCol_TabDimmedSelected] = lerp(kPanel, kVoid, 0.45f);
    c[ImGuiCol_TabDimmed] = lerp(kPanel, kVoid, 0.80f);
    c[ImGuiCol_TabDimmedSelectedOverline] = alpha(kAccentPressed, 0.35f);

    c[ImGuiCol_DockingPreview] = alpha(kAccent, 0.45f);
    c[ImGuiCol_DockingEmptyBg] = kVoid;

    c[ImGuiCol_PlotLines] = kInfo;
    c[ImGuiCol_PlotLinesHovered] = kAccentHover;
    c[ImGuiCol_PlotHistogram] = kWarning;
    c[ImGuiCol_PlotHistogramHovered] = kDanger;

    c[ImGuiCol_TableHeaderBg] = kRaised;
    c[ImGuiCol_TableBorderStrong] = kEdge;
    c[ImGuiCol_TableBorderLight] = alpha(kEdge, 0.55f);
    c[ImGuiCol_TableRowBg] = alpha(kVoid, 0.0f);
    c[ImGuiCol_TableRowBgAlt] = alpha(kText, 0.035f);

    c[ImGuiCol_TextLink] = kInfo;
    c[ImGuiCol_TextSelectedBg] = alpha(kAccent, 0.32f);
    c[ImGuiCol_DragDropTarget] = kAccentHover;
    c[ImGuiCol_NavCursor] = kFocus;
    c[ImGuiCol_NavWindowingHighlight] = kChrome;
    c[ImGuiCol_NavWindowingDimBg] = alpha(kVoid, 0.72f);
    c[ImGuiCol_ModalWindowDimBg] = alpha(kVoid, 0.78f);

    // ImGuiCol_InputTextCursor and ImGuiCol_TreeLines are newer than the
    // currently vendored ImGui. Assign them here after upgrading.
}

// Pacome Danhiez's light palette with Doug Binks's dark-value conversion.
// Ported from the legacy ImGui color names used by:
// https://gist.github.com/dougbinks/8089b4bbaccaaf6fa204236978d165a9
void dougBinksDark()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsLight(&style);
    style.Alpha = 1.0f;
    style.FrameRounding = 3.0f;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
    c[ImGuiCol_WindowBg] = ImVec4(0.94f, 0.94f, 0.94f, 0.94f);
    c[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_PopupBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.94f);
    c[ImGuiCol_Border] = ImVec4(0.00f, 0.00f, 0.00f, 0.39f);
    c[ImGuiCol_BorderShadow] = ImVec4(1.00f, 1.00f, 1.00f, 0.10f);
    c[ImGuiCol_FrameBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.94f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
    c[ImGuiCol_TitleBg] = ImVec4(0.96f, 0.96f, 0.96f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(1.00f, 1.00f, 1.00f, 0.51f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.82f, 0.82f, 0.82f, 1.00f);
    c[ImGuiCol_MenuBarBg] = ImVec4(0.86f, 0.86f, 0.86f, 1.00f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.98f, 0.98f, 0.98f, 0.53f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.69f, 0.69f, 0.69f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.59f, 0.59f, 0.59f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.49f, 0.49f, 0.49f, 1.00f);
    c[ImGuiCol_CheckMark] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.24f, 0.52f, 0.88f, 1.00f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    c[ImGuiCol_Button] = ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);
    c[ImGuiCol_Header] = ImVec4(0.26f, 0.59f, 0.98f, 0.31f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    // Legacy ImGuiCol_Column* became separator colors.
    c[ImGuiCol_Separator] = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
    c[ImGuiCol_SeparatorHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.78f);
    c[ImGuiCol_SeparatorActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    c[ImGuiCol_ResizeGrip] = ImVec4(1.00f, 1.00f, 1.00f, 0.50f);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
    c[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
    c[ImGuiCol_PlotLines] = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
    c[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    c[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    c[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);

    constexpr float alpha = 1.0f;
    for (int i = 0; i < ImGuiCol_COUNT; ++i) {
        ImVec4& color = c[i];
        float hue = 0.0f;
        float saturation = 0.0f;
        float value = 0.0f;
        ImGui::ColorConvertRGBtoHSV(color.x, color.y, color.z, hue, saturation,
                                    value);
        if (saturation < 0.1f)
            value = 1.0f - value;
        ImGui::ColorConvertHSVtoRGB(hue, saturation, value, color.x, color.y,
                                    color.z);
        if (color.w < 1.0f)
            color.w *= alpha;
    }
}

// The game HUD's own look, lifted onto the tool UI so the debug panels read as
// part of the same product instead of as a default imgui window floating over
// it.
//
// Palette is "Ash Reliquary" -- black iron, bone ink, brass focus, blood
// danger, restrained ritual violet. The source of truth is
// makeGameHudStyleSheet() in game/src/ui/GameHudStyle.cpp, which packs the same
// colours ABGR for the UiCanvas; they are restated here as floats because the
// engine cannot include a game header. Keep the two in step by hand -- there
// are eleven of them and they change rarely.
//
// Nothing is rounded. The HUD is drawn as hard rectangles on a pixel grid, and
// a rounded tool window beside it looks like a different program.
void hudReliquary()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);

    // Square, everywhere. FrameRounding is the one that shows most (every
    // slider, checkbox and input), but a single rounded scrollbar or tab is
    // enough to break the read.
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.PopupRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.TabRounding = 0.0f;

    // Hairline borders and tight, even padding: the HUD's panels are outlined
    // rectangles, not shaded slabs.
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.TabBorderSize = 1.0f;
    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.FramePadding = ImVec2(6.0f, 3.0f);
    style.ItemSpacing = ImVec2(6.0f, 5.0f);
    style.ItemInnerSpacing = ImVec2(5.0f, 4.0f);
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 9.0f;
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);

    const ImVec4 ink(0.071f, 0.055f, 0.039f, 0.941f); // black iron
    const ImVec4 inkSoft(0.114f, 0.090f, 0.071f, 0.941f);
    const ImVec4 edge(0.420f, 0.329f, 0.200f, 1.0f); // dim brass
    const ImVec4 edgeBright(0.722f, 0.576f, 0.333f, 1.0f);
    const ImVec4 text(0.910f, 0.863f, 0.753f, 1.0f); // bone
    const ImVec4 textDim(0.557f, 0.506f, 0.408f, 1.0f);
    const ImVec4 accent(0.941f, 0.725f, 0.361f, 1.0f); // brass focus
    const ImVec4 good(0.435f, 0.647f, 0.435f, 1.0f);
    const ImVec4 warn(0.851f, 0.643f, 0.255f, 1.0f);
    const ImVec4 bad(0.780f, 0.290f, 0.275f, 1.0f); // blood
    const ImVec4 mystic(0.561f, 0.514f, 0.788f, 1.0f);

    const auto tint = [](const ImVec4& c, float alpha) {
        return ImVec4(c.x, c.y, c.z, alpha);
    };

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = textDim;
    c[ImGuiCol_WindowBg] = ink;
    c[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_PopupBg] = ImVec4(ink.x, ink.y, ink.z, 0.98f);
    c[ImGuiCol_Border] = tint(edge, 0.85f);
    c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_FrameBg] = inkSoft;
    c[ImGuiCol_FrameBgHovered] = tint(edge, 0.45f);
    c[ImGuiCol_FrameBgActive] = tint(edge, 0.70f);
    c[ImGuiCol_TitleBg] = inkSoft;
    c[ImGuiCol_TitleBgActive] = tint(edge, 0.55f);
    c[ImGuiCol_TitleBgCollapsed] = tint(ink, 0.75f);
    c[ImGuiCol_MenuBarBg] = inkSoft;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.25f);
    c[ImGuiCol_ScrollbarGrab] = tint(edge, 0.75f);
    c[ImGuiCol_ScrollbarGrabHovered] = edgeBright;
    c[ImGuiCol_ScrollbarGrabActive] = accent;
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = tint(edgeBright, 0.90f);
    c[ImGuiCol_SliderGrabActive] = accent;
    c[ImGuiCol_Button] = tint(edge, 0.35f);
    c[ImGuiCol_ButtonHovered] = tint(edgeBright, 0.65f);
    c[ImGuiCol_ButtonActive] = tint(accent, 0.85f);
    c[ImGuiCol_Header] = tint(edge, 0.40f);
    c[ImGuiCol_HeaderHovered] = tint(edgeBright, 0.55f);
    c[ImGuiCol_HeaderActive] = tint(accent, 0.70f);
    c[ImGuiCol_Separator] = tint(edge, 0.60f);
    c[ImGuiCol_SeparatorHovered] = edgeBright;
    c[ImGuiCol_SeparatorActive] = accent;
    c[ImGuiCol_ResizeGrip] = tint(edge, 0.45f);
    c[ImGuiCol_ResizeGripHovered] = tint(edgeBright, 0.70f);
    c[ImGuiCol_ResizeGripActive] = accent;
    c[ImGuiCol_Tab] = tint(edge, 0.30f);
    c[ImGuiCol_TabHovered] = tint(edgeBright, 0.60f);
    c[ImGuiCol_TabSelected] = tint(edge, 0.75f);
    c[ImGuiCol_TabDimmed] = tint(ink, 0.90f);
    c[ImGuiCol_TabDimmedSelected] = tint(edge, 0.50f);
    // The status colours the HUD reserves for meaning, kept meaningful here:
    // plots read danger-to-good the same way the vitals rail does.
    c[ImGuiCol_PlotLines] = mystic;
    c[ImGuiCol_PlotLinesHovered] = accent;
    c[ImGuiCol_PlotHistogram] = warn;
    c[ImGuiCol_PlotHistogramHovered] = bad;
    c[ImGuiCol_TextSelectedBg] = tint(accent, 0.35f);
    c[ImGuiCol_NavCursor] = accent;
    c[ImGuiCol_DragDropTarget] = good;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
}

// Themes only exist once someone asks for one, so registration is lazy: no
// static-init ordering dependency on the imgui context existing.
void ensureBuiltins()
{
    if (!registry().empty())
        return;
    registerTheme("raven_editor", &ravenEditor);
    // Persisted ids from the previous editor theme keep working, but resolve to
    // current Raven chrome rather than preserving obsolete magenta branding.
    registerTheme("ac_vixen", &ravenEditor);
    registerTheme("hud_reliquary", &hudReliquary);
    // Backward-compatible alias for projects that persisted the old id.
    registerTheme("one_dark", &ravenEditor);
    registerTheme("dougbinks_dark", &dougBinksDark);
    registerTheme("dark", [] { ImGui::StyleColorsDark(); });
    registerTheme("light", [] { ImGui::StyleColorsLight(); });
    registerTheme("classic", [] { ImGui::StyleColorsClassic(); });
}

} // namespace

void registerTheme(const std::string& id, std::function<void()> fn)
{
    auto& r = registry();
    auto it = std::find_if(r.begin(), r.end(),
                           [&](const Entry& e) { return e.id == id; });
    if (it != r.end())
        it->fn = std::move(fn);
    else
        r.push_back({id, std::move(fn)});
}

std::vector<std::string> ids()
{
    ensureBuiltins();
    std::vector<std::string> out;
    out.reserve(registry().size());
    for (const Entry& e : registry())
        out.push_back(e.id);
    return out;
}

bool apply(const std::string& id)
{
    ensureBuiltins();
    auto& r = registry();
    auto it = std::find_if(r.begin(), r.end(),
                           [&](const Entry& e) { return e.id == id; });
    if (it == r.end() || !it->fn)
        return false;
    // A theme describes the complete style, not a delta from whichever theme
    // happened to run before it. Resetting geometry as well as colours makes
    // hot-swapping deterministic (raven_editor after hud_reliquary must not
    // inherit another theme's spacing).
    ImGui::GetStyle() = ImGuiStyle{};
    it->fn();
    currentId() = id;
    return true;
}

const std::string& current()
{
    return currentId();
}

} // namespace eng::imguitheme
