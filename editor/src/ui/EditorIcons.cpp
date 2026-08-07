#include <editor/ui/EditorIcons.h>

#include <imgui.h>

#include <cmath>
#include <cstring>

namespace ed {
namespace {

// Icons are authored in a 0..1 box and scaled, so one set of coordinates works
// at every size a panel asks for.
ImVec2 at(ImVec2 origin, float size, float x, float y)
{
    return ImVec2(origin.x + x * size, origin.y + y * size);
}

void poly(ImDrawList* list, ImVec2 origin, float size, const float* points,
          int count, unsigned rgba, float thickness, bool closed)
{
    ImVec2 screen[16];
    const int n = count > 16 ? 16 : count;
    for (int i = 0; i < n; ++i)
        screen[i] = at(origin, size, points[i * 2], points[i * 2 + 1]);
    list->AddPolyline(screen, n, rgba,
                      closed ? ImDrawFlags_Closed : ImDrawFlags_None, thickness);
}

void fillPoly(ImDrawList* list, ImVec2 origin, float size, const float* points,
              int count, unsigned rgba)
{
    ImVec2 screen[16];
    const int n = count > 16 ? 16 : count;
    for (int i = 0; i < n; ++i)
        screen[i] = at(origin, size, points[i * 2], points[i * 2 + 1]);
    list->AddConvexPolyFilled(screen, n, rgba);
}

unsigned dim(unsigned rgba, float alpha)
{
    const unsigned a = unsigned(float((rgba >> 24) & 0xFF) * alpha);
    return (rgba & 0x00FFFFFFu) | (a << 24);
}

} // namespace

void drawIcon(ImDrawList* list, Icon icon, ImVec2 origin, float size,
              unsigned rgba)
{
    if (!list || size < 4.0f)
        return;
    const float line = size < 14.0f ? 1.0f : 1.5f;
    const ImVec2 centre = at(origin, size, 0.5f, 0.5f);

    switch (icon) {
    case Icon::Select: {
        static const float cursor[] = {0.18f, 0.10f, 0.76f, 0.56f,
                                       0.49f, 0.60f, 0.64f, 0.88f,
                                       0.49f, 0.94f, 0.34f, 0.64f,
                                       0.16f, 0.82f};
        fillPoly(list, origin, size, cursor, 7, rgba);
        break;
    }
    case Icon::Place: {
        static const float diamond[] = {0.48f, 0.12f, 0.82f, 0.30f,
                                        0.48f, 0.49f, 0.14f, 0.30f};
        fillPoly(list, origin, size, diamond, 4, dim(rgba, 0.65f));
        list->AddLine(at(origin, size, 0.48f, 0.49f),
                      at(origin, size, 0.48f, 0.84f), rgba, line);
        list->AddLine(at(origin, size, 0.30f, 0.68f),
                      at(origin, size, 0.66f, 0.68f), rgba, line);
        break;
    }
    case Icon::Move: {
        list->AddLine(at(origin, size, 0.12f, 0.50f),
                      at(origin, size, 0.88f, 0.50f), rgba, line + 0.5f);
        list->AddLine(at(origin, size, 0.50f, 0.12f),
                      at(origin, size, 0.50f, 0.88f), rgba, line + 0.5f);
        static const float left[] = {0.12f, 0.50f, 0.30f, 0.37f, 0.30f, 0.63f};
        static const float right[] = {0.88f, 0.50f, 0.70f, 0.37f, 0.70f, 0.63f};
        static const float up[] = {0.50f, 0.12f, 0.37f, 0.30f, 0.63f, 0.30f};
        static const float down[] = {0.50f, 0.88f, 0.37f, 0.70f, 0.63f, 0.70f};
        fillPoly(list, origin, size, left, 3, rgba);
        fillPoly(list, origin, size, right, 3, rgba);
        fillPoly(list, origin, size, up, 3, rgba);
        fillPoly(list, origin, size, down, 3, rgba);
        break;
    }
    case Icon::Rotate: {
        list->PathArcTo(centre, size * 0.31f, -2.65f, 2.05f, 20);
        list->PathStroke(rgba, 0, line + 0.7f);
        static const float head[] = {0.78f, 0.68f, 0.91f, 0.78f, 0.72f, 0.86f};
        fillPoly(list, origin, size, head, 3, rgba);
        list->AddCircleFilled(centre, size * 0.07f, dim(rgba, 0.75f), 8);
        break;
    }
    case Icon::Scale: {
        list->AddLine(at(origin, size, 0.24f, 0.76f),
                      at(origin, size, 0.76f, 0.24f), rgba, line + 0.5f);
        static const float out[] = {0.76f, 0.24f, 0.52f, 0.22f, 0.78f, 0.48f};
        static const float in[] = {0.24f, 0.76f, 0.48f, 0.78f, 0.22f, 0.52f};
        fillPoly(list, origin, size, out, 3, rgba);
        fillPoly(list, origin, size, in, 3, rgba);
        break;
    }
    case Icon::Room: {
        list->AddRect(at(origin, size, 0.14f, 0.18f),
                      at(origin, size, 0.86f, 0.82f), rgba, size * 0.04f, 0,
                      line + 0.5f);
        list->AddLine(at(origin, size, 0.50f, 0.18f),
                      at(origin, size, 0.50f, 0.82f), dim(rgba, 0.55f), line);
        list->AddLine(at(origin, size, 0.14f, 0.50f),
                      at(origin, size, 0.86f, 0.50f), dim(rgba, 0.55f), line);
        break;
    }
    case Icon::Cube: {
        // An isometric box: three faces, which reads as geometry at 12 px where
        // a wireframe cube reads as noise.
        static const float top[] = {0.5f, 0.14f, 0.86f, 0.32f,
                                    0.5f, 0.50f, 0.14f, 0.32f};
        static const float left[] = {0.14f, 0.32f, 0.5f, 0.50f,
                                     0.5f,  0.86f, 0.14f, 0.68f};
        static const float right[] = {0.86f, 0.32f, 0.86f, 0.68f,
                                      0.5f,  0.86f, 0.5f,  0.50f};
        fillPoly(list, origin, size, top, 4, rgba);
        fillPoly(list, origin, size, left, 4, dim(rgba, 0.55f));
        fillPoly(list, origin, size, right, 4, dim(rgba, 0.75f));
        break;
    }
    case Icon::Light: {
        // A bulb with rays: the shape everyone reads as a point light.
        list->AddCircleFilled(at(origin, size, 0.5f, 0.44f), size * 0.20f, rgba,
                              12);
        for (int i = 0; i < 8; ++i) {
            const float angle = float(i) * 0.7853982f;
            const ImVec2 from(centre.x + std::cos(angle) * size * 0.30f,
                              at(origin, size, 0.5f, 0.44f).y +
                                  std::sin(angle) * size * 0.30f);
            const ImVec2 to(centre.x + std::cos(angle) * size * 0.44f,
                            at(origin, size, 0.5f, 0.44f).y +
                                std::sin(angle) * size * 0.44f);
            list->AddLine(from, to, dim(rgba, 0.8f), line);
        }
        break;
    }
    case Icon::Sun: {
        // A disc with a single direction: a directional light is aimed, and the
        // arrow is the part that matters.
        list->AddCircle(at(origin, size, 0.36f, 0.36f), size * 0.20f, rgba, 12,
                        line);
        list->AddLine(at(origin, size, 0.50f, 0.50f),
                      at(origin, size, 0.86f, 0.86f), rgba, line);
        static const float head[] = {0.86f, 0.86f, 0.86f, 0.60f, 0.60f, 0.86f};
        fillPoly(list, origin, size, head, 3, rgba);
        break;
    }
    case Icon::Camera: {
        // A body with a lens cone off the front. The cone is the half that
        // matters: a camera's job is a direction, and a plain box would read as
        // a crate at 12 px.
        static const float body[] = {0.10f, 0.30f, 0.58f, 0.30f,
                                     0.58f, 0.72f, 0.10f, 0.72f};
        fillPoly(list, origin, size, body, 4, dim(rgba, 0.85f));
        static const float lens[] = {0.58f, 0.40f, 0.90f, 0.22f,
                                     0.90f, 0.80f, 0.58f, 0.62f};
        fillPoly(list, origin, size, lens, 4, rgba);
        break;
    }
    case Icon::Spawn: {
        // A figure on a base: where the player stands.
        list->AddCircleFilled(at(origin, size, 0.5f, 0.26f), size * 0.13f, rgba,
                              10);
        list->AddLine(at(origin, size, 0.5f, 0.40f),
                      at(origin, size, 0.5f, 0.68f), rgba, line);
        list->AddLine(at(origin, size, 0.30f, 0.50f),
                      at(origin, size, 0.70f, 0.50f), rgba, line);
        list->AddLine(at(origin, size, 0.22f, 0.86f),
                      at(origin, size, 0.78f, 0.86f), dim(rgba, 0.7f), line);
        break;
    }
    case Icon::Exit: {
        // A doorway with an arrow leaving it.
        static const float frame[] = {0.18f, 0.86f, 0.18f, 0.16f,
                                      0.56f, 0.16f, 0.56f, 0.86f};
        poly(list, origin, size, frame, 4, rgba, line, false);
        list->AddLine(at(origin, size, 0.60f, 0.50f),
                      at(origin, size, 0.88f, 0.50f), rgba, line);
        static const float head[] = {0.88f, 0.50f, 0.74f, 0.38f, 0.74f, 0.62f};
        fillPoly(list, origin, size, head, 3, rgba);
        break;
    }
    case Icon::Enemy: {
        // A horned head. Angular on purpose: it has to be told apart from the
        // spawn's round one at a glance down a long list.
        static const float head[] = {0.20f, 0.34f, 0.36f, 0.16f, 0.64f, 0.16f,
                                     0.80f, 0.34f, 0.66f, 0.80f, 0.34f, 0.80f};
        fillPoly(list, origin, size, head, 6, rgba);
        list->AddLine(at(origin, size, 0.20f, 0.34f),
                      at(origin, size, 0.08f, 0.14f), rgba, line);
        list->AddLine(at(origin, size, 0.80f, 0.34f),
                      at(origin, size, 0.92f, 0.14f), rgba, line);
        break;
    }
    case Icon::Pickup: {
        // A diamond: loot, in every game that has ever drawn loot.
        static const float gem[] = {0.5f, 0.12f, 0.88f, 0.46f,
                                    0.5f, 0.88f, 0.12f, 0.46f};
        fillPoly(list, origin, size, gem, 4, dim(rgba, 0.55f));
        poly(list, origin, size, gem, 4, rgba, line, true);
        break;
    }
    case Icon::Trigger: {
        // A dashed box: a volume you pass through rather than collide with.
        const ImVec2 min = at(origin, size, 0.14f, 0.18f);
        const ImVec2 max = at(origin, size, 0.86f, 0.82f);
        const float step = (max.x - min.x) / 5.0f;
        for (int i = 0; i < 5; i += 2) {
            list->AddLine(ImVec2(min.x + step * float(i), min.y),
                          ImVec2(min.x + step * float(i + 1), min.y), rgba, line);
            list->AddLine(ImVec2(min.x + step * float(i), max.y),
                          ImVec2(min.x + step * float(i + 1), max.y), rgba, line);
        }
        const float vstep = (max.y - min.y) / 5.0f;
        for (int i = 0; i < 5; i += 2) {
            list->AddLine(ImVec2(min.x, min.y + vstep * float(i)),
                          ImVec2(min.x, min.y + vstep * float(i + 1)), rgba, line);
            list->AddLine(ImVec2(max.x, min.y + vstep * float(i)),
                          ImVec2(max.x, min.y + vstep * float(i + 1)), rgba, line);
        }
        break;
    }
    case Icon::Marker: {
        // A map pin.
        list->AddCircleFilled(at(origin, size, 0.5f, 0.36f), size * 0.22f, rgba,
                              12);
        static const float tip[] = {0.32f, 0.50f, 0.68f, 0.50f, 0.5f, 0.88f};
        fillPoly(list, origin, size, tip, 3, rgba);
        break;
    }
    case Icon::Group: {
        // Three dots joined to one: a parent and what hangs off it.
        list->AddCircleFilled(at(origin, size, 0.5f, 0.20f), size * 0.13f, rgba,
                              10);
        list->AddCircleFilled(at(origin, size, 0.22f, 0.78f), size * 0.11f,
                              dim(rgba, 0.8f), 10);
        list->AddCircleFilled(at(origin, size, 0.78f, 0.78f), size * 0.11f,
                              dim(rgba, 0.8f), 10);
        list->AddLine(at(origin, size, 0.5f, 0.33f),
                      at(origin, size, 0.22f, 0.67f), dim(rgba, 0.6f), line);
        list->AddLine(at(origin, size, 0.5f, 0.33f),
                      at(origin, size, 0.78f, 0.67f), dim(rgba, 0.6f), line);
        break;
    }
    case Icon::Stack: {
        // Three offset slabs seen edge-on: copies of one thing, piled up.
        // Deliberately unlike Group's parent-and-children, because a bucket row
        // is not a parent and the tree used to draw them identically.
        for (int i = 0; i < 3; ++i) {
            const float y = 0.28f + float(i) * 0.22f;
            list->AddRectFilled(at(origin, size, 0.16f, y - 0.07f),
                                at(origin, size, 0.84f, y + 0.07f),
                                dim(rgba, i == 0 ? 1.0f : 0.62f));
        }
        break;
    }
    case Icon::Collider: {
        // A wire box: bounds, not geometry.
        list->AddRect(at(origin, size, 0.14f, 0.18f),
                      at(origin, size, 0.86f, 0.82f), rgba, 0.0f, 0, line);
        list->AddLine(at(origin, size, 0.14f, 0.18f),
                      at(origin, size, 0.86f, 0.82f), dim(rgba, 0.4f), line);
        break;
    }
    case Icon::Eye: {
        static const float lid[] = {0.08f, 0.50f, 0.30f, 0.24f, 0.70f, 0.24f,
                                    0.92f, 0.50f, 0.70f, 0.76f, 0.30f, 0.76f};
        poly(list, origin, size, lid, 6, rgba, line, true);
        list->AddCircleFilled(centre, size * 0.14f, rgba, 10);
        break;
    }
    case Icon::EyeClosed: {
        static const float lid[] = {0.08f, 0.50f, 0.30f, 0.24f, 0.70f, 0.24f,
                                    0.92f, 0.50f, 0.70f, 0.76f, 0.30f, 0.76f};
        poly(list, origin, size, lid, 6, dim(rgba, 0.55f), line, true);
        // Struck through, which is the one thing that makes "hidden" read
        // instantly rather than as a slightly darker eye.
        list->AddLine(at(origin, size, 0.14f, 0.84f),
                      at(origin, size, 0.86f, 0.16f), rgba, line + 0.5f);
        break;
    }
    case Icon::Lock: {
        list->AddRectFilled(at(origin, size, 0.24f, 0.46f),
                            at(origin, size, 0.76f, 0.84f), rgba, size * 0.08f);
        list->AddCircle(at(origin, size, 0.5f, 0.40f), size * 0.18f, rgba, 12,
                        line + 0.5f);
        break;
    }
    case Icon::Unlock: {
        list->AddRectFilled(at(origin, size, 0.24f, 0.46f),
                            at(origin, size, 0.76f, 0.84f), dim(rgba, 0.55f),
                            size * 0.08f);
        // The shackle swung open: an unlocked padlock that only differs by
        // shade is not a different icon.
        list->AddLine(at(origin, size, 0.34f, 0.46f),
                      at(origin, size, 0.34f, 0.30f), dim(rgba, 0.8f), line);
        list->AddLine(at(origin, size, 0.34f, 0.26f),
                      at(origin, size, 0.62f, 0.20f), dim(rgba, 0.8f), line);
        break;
    }
    case Icon::Missing: {
        // A question mark's job, without a font: a ring with a gap and a dot.
        list->AddCircle(centre, size * 0.34f, rgba, 12, line);
        list->AddLine(at(origin, size, 0.30f, 0.30f),
                      at(origin, size, 0.70f, 0.70f), rgba, line);
        break;
    }
    }
}

Icon iconForKind(const char* kind)
{
    if (!kind)
        return Icon::Cube;
    const auto is = [kind](const char* name) {
        return std::strcmp(kind, name) == 0;
    };
    if (is("spawn")) return Icon::Spawn;
    if (is("exit")) return Icon::Exit;
    if (is("enemy")) return Icon::Enemy;
    if (is("pickup")) return Icon::Pickup;
    if (is("trigger")) return Icon::Trigger;
    if (is("marker")) return Icon::Marker;
    if (is("light")) return Icon::Light;
    if (is("sun")) return Icon::Sun;
    if (is("camera")) return Icon::Camera;
    if (is("node") || is("group")) return Icon::Group;
    if (is("volume")) return Icon::Collider;
    if (is("MISSING")) return Icon::Missing;
    return Icon::Cube;
}

bool iconButton(Icon icon, const char* id, const char* tooltip, bool active,
                 float size)
{
    if (size <= 0.0f)
        size = ImGui::GetFontSize();
    const float pad = ImGui::GetStyle().FramePadding.y;
    const ImVec2 box(size + pad * 2.0f, size + pad * 2.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    if (active) {
        const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImVec4(accent.x, accent.y, accent.z, 0.24f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(accent.x, accent.y, accent.z, 0.36f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              ImVec4(accent.x, accent.y, accent.z, 0.48f));
    }
    const bool pressed = ImGui::Button(id, box);
    if (active)
        ImGui::PopStyleColor(3);
    const ImU32 colour = ImGui::GetColorU32(
        active ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    drawIcon(ImGui::GetWindowDrawList(), icon,
             ImVec2(origin.x + pad, origin.y + pad), size, colour);
    if (tooltip && (ImGui::IsItemHovered() || ImGui::IsItemFocused()))
        ImGui::SetTooltip("%s", tooltip);
    return pressed;
}

bool iconToggle(Icon icon, const char* id, bool active, const char* tooltip,
                float size)
{
    if (size <= 0.0f)
        size = ImGui::GetFontSize();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(
        id, ImVec2(size, size), ImGuiButtonFlags_EnableNav);
    // Lit when on, and only faintly visible when off *and* not hovered: a
    // column of hard grey icons down the side of a list competes with the list.
    const bool hovered = ImGui::IsItemHovered();
    const ImU32 colour = ImGui::GetColorU32(
        active ? ImGuiCol_Text : ImGuiCol_TextDisabled,
        active || hovered ? 1.0f : 0.35f);
    drawIcon(ImGui::GetWindowDrawList(), icon, origin, size, colour);
    if (tooltip && (hovered || ImGui::IsItemFocused()))
        ImGui::SetTooltip("%s", tooltip);
    return pressed;
}

} // namespace ed
