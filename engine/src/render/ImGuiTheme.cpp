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
    return ImVec4(float((argb >> 16) & 0xFF) / 255.0f,
                  float((argb >> 8) & 0xFF) / 255.0f,
                  float(argb & 0xFF) / 255.0f,
                  float((argb >> 24) & 0xFF) / 255.0f);
}

ImVec4 lerp(const ImVec4& a, const ImVec4& b, float t)
{
    return ImVec4(std::lerp(a.x, b.x, t), std::lerp(a.y, b.y, t),
                  std::lerp(a.z, b.z, t), std::lerp(a.w, b.w, t));
}

// One Dark: the Atom/OneDark palette, dark slate with muted blue accents.
void oneDark()
{
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowBorderSize = 3.0f;

    style.FrameRounding = 3.0f;
    style.PopupRounding = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.GrabRounding = 3.0f;

    style.DockingSeparatorSize = 3.0f;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = rgba(0xFFABB2BF);
    c[ImGuiCol_TextDisabled] = rgba(0xFF565656);
    c[ImGuiCol_WindowBg] = rgba(0xFF282C34);
    c[ImGuiCol_ChildBg] = rgba(0xFF21252B);
    c[ImGuiCol_PopupBg] = rgba(0xFF2E323A);
    c[ImGuiCol_Border] = rgba(0xFF2E323A);
    c[ImGuiCol_BorderShadow] = rgba(0x00000000);
    c[ImGuiCol_FrameBg] = c[ImGuiCol_ChildBg];
    c[ImGuiCol_FrameBgHovered] = rgba(0xFF484C52);
    c[ImGuiCol_FrameBgActive] = rgba(0xFF54575D);
    c[ImGuiCol_TitleBg] = c[ImGuiCol_WindowBg];
    c[ImGuiCol_TitleBgActive] = c[ImGuiCol_FrameBgActive];
    c[ImGuiCol_TitleBgCollapsed] = rgba(0x8221252B);
    c[ImGuiCol_MenuBarBg] = c[ImGuiCol_ChildBg];
    c[ImGuiCol_ScrollbarBg] = c[ImGuiCol_PopupBg];
    c[ImGuiCol_ScrollbarGrab] = rgba(0xFF3E4249);
    c[ImGuiCol_ScrollbarGrabHovered] = rgba(0xFF484C52);
    c[ImGuiCol_ScrollbarGrabActive] = rgba(0xFF54575D);
    c[ImGuiCol_CheckMark] = c[ImGuiCol_Text];
    c[ImGuiCol_SliderGrab] = rgba(0xFF353941);
    c[ImGuiCol_SliderGrabActive] = rgba(0xFF7A7A7A);
    c[ImGuiCol_Button] = c[ImGuiCol_SliderGrab];
    c[ImGuiCol_ButtonHovered] = c[ImGuiCol_FrameBgActive];
    c[ImGuiCol_ButtonActive] = c[ImGuiCol_ScrollbarGrabActive];
    c[ImGuiCol_Header] = c[ImGuiCol_ChildBg];
    c[ImGuiCol_HeaderHovered] = rgba(0xFF353941);
    c[ImGuiCol_HeaderActive] = c[ImGuiCol_FrameBgActive];
    c[ImGuiCol_Separator] = c[ImGuiCol_FrameBgActive];
    c[ImGuiCol_SeparatorHovered] = rgba(0xFF3E4452);
    c[ImGuiCol_SeparatorActive] = c[ImGuiCol_SeparatorHovered];
    c[ImGuiCol_ResizeGrip] = c[ImGuiCol_Separator];
    c[ImGuiCol_ResizeGripHovered] = c[ImGuiCol_SeparatorHovered];
    c[ImGuiCol_ResizeGripActive] = c[ImGuiCol_SeparatorActive];
    c[ImGuiCol_TabHovered] = c[ImGuiCol_HeaderHovered];
    c[ImGuiCol_Tab] = c[ImGuiCol_FrameBgActive];
    c[ImGuiCol_TabSelected] = c[ImGuiCol_HeaderHovered];
    c[ImGuiCol_TabSelectedOverline] = c[ImGuiCol_HeaderActive];
    c[ImGuiCol_TabDimmed] = lerp(c[ImGuiCol_Tab], c[ImGuiCol_TitleBg], 0.80f);
    c[ImGuiCol_TabDimmedSelected] =
        lerp(c[ImGuiCol_TabSelected], c[ImGuiCol_TitleBg], 0.40f);
    c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.50f, 0.50f, 0.50f, 0.00f);
    c[ImGuiCol_DockingPreview] = c[ImGuiCol_ChildBg];
    c[ImGuiCol_DockingEmptyBg] = c[ImGuiCol_WindowBg];
    c[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    c[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    c[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    c[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    c[ImGuiCol_TableHeaderBg] = c[ImGuiCol_ChildBg];
    c[ImGuiCol_TableBorderStrong] = c[ImGuiCol_SliderGrab];
    c[ImGuiCol_TableBorderLight] = c[ImGuiCol_FrameBgActive];
    c[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    c[ImGuiCol_TextLink] = rgba(0xFF3F94CE);
    c[ImGuiCol_TextSelectedBg] = rgba(0xFF243140);
    c[ImGuiCol_DragDropTarget] = c[ImGuiCol_Text];
    c[ImGuiCol_NavCursor] = c[ImGuiCol_TextLink];
    c[ImGuiCol_NavWindowingHighlight] = c[ImGuiCol_Text];
    c[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    c[ImGuiCol_ModalWindowDimBg] = rgba(0xC821252B);
    // ImGuiCol_InputTextCursor / ImGuiCol_TreeLines land after imgui 1.91.9b;
    // add them here when the vendored copy is bumped.
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

    const ImVec4 ink(0.071f, 0.055f, 0.039f, 0.941f);     // black iron
    const ImVec4 inkSoft(0.114f, 0.090f, 0.071f, 0.941f);
    const ImVec4 edge(0.420f, 0.329f, 0.200f, 1.0f);      // dim brass
    const ImVec4 edgeBright(0.722f, 0.576f, 0.333f, 1.0f);
    const ImVec4 text(0.910f, 0.863f, 0.753f, 1.0f);      // bone
    const ImVec4 textDim(0.557f, 0.506f, 0.408f, 1.0f);
    const ImVec4 accent(0.941f, 0.725f, 0.361f, 1.0f);    // brass focus
    const ImVec4 good(0.435f, 0.647f, 0.435f, 1.0f);
    const ImVec4 warn(0.851f, 0.643f, 0.255f, 1.0f);
    const ImVec4 bad(0.780f, 0.290f, 0.275f, 1.0f);       // blood
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
    registerTheme("hud_reliquary", &hudReliquary);
    registerTheme("dougbinks_dark", &dougBinksDark);
    registerTheme("one_dark", &oneDark);
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
    it->fn();
    currentId() = id;
    return true;
}

const std::string& current() { return currentId(); }

} // namespace eng::imguitheme
