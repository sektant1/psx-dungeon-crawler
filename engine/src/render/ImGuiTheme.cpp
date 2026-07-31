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

// Themes only exist once someone asks for one, so registration is lazy: no
// static-init ordering dependency on the imgui context existing.
void ensureBuiltins()
{
    if (!registry().empty())
        return;
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
