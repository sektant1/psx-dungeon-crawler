#include "GameDiagnostics.h"

#include "DungeonMap.h"

#include <eng/Physics.h>

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <string>

namespace game {

void drawDungeonMap(const DungeonMap& map, glm::vec3 playerPos)
{
    ImGui::SetNextWindowSize(ImVec2(540.0f, 600.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(400.0f, 10.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Generated Dungeon Map")) {
        ImGui::End();
        return;
    }

    const int rows = map.debugRows();
    const int cols = map.debugColumns();
    if (rows == 0 || cols == 0) {
        ImGui::TextUnformatted("No generated level is loaded.");
        ImGui::End();
        return;
    }

    int playerCol, playerRow;
    map.debugCellOf(playerPos, playerCol, playerRow);
    ImGui::Text("%d x %d cells  |  player: (%d, %d)", cols, rows,
                playerCol, playerRow);
    static bool roomLabels = false;
    ImGui::SameLine();
    ImGui::Checkbox("room IDs", &roomLabels);

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float cell = std::clamp((available.x - 4.0f) / float(cols), 7.0f, 18.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 extent(cell * float(cols), cell * float(rows));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + extent.x, origin.y + extent.y),
                        IM_COL32(8, 12, 11, 255));

    static constexpr ImU32 kRooms[] = {
        IM_COL32(46, 92, 104, 255), IM_COL32(89, 69, 118, 255),
        IM_COL32(73, 104, 68, 255), IM_COL32(120, 78, 57, 255),
        IM_COL32(50, 109, 97, 255), IM_COL32(105, 91, 53, 255),
        IM_COL32(72, 76, 125, 255), IM_COL32(117, 61, 99, 255),
    };
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const char tile = map.debugCellAt(col, row);
            const int room = map.debugRoomAt(col, row);
            ImU32 colour = IM_COL32(16, 20, 21, 255); // solid/void
            if (room >= 0)
                colour = kRooms[size_t(room) % (sizeof(kRooms) / sizeof(kRooms[0]))];
            if (tile == 'A') colour = IM_COL32(202, 153, 61, 255);
            if (tile == 'L') colour = IM_COL32(210, 106, 43, 255);
            if (tile == 'S') colour = IM_COL32(89, 190, 236, 255);
            if (tile == 'C') colour = IM_COL32(188, 102, 220, 255);
            if (tile == 'X') colour = IM_COL32(95, 210, 143, 255);

            const ImVec2 p0(origin.x + cell * float(col),
                             origin.y + cell * float(row));
            const ImVec2 p1(p0.x + cell, p0.y + cell);
            draw->AddRectFilled(p0, p1, colour);
            draw->AddRect(p0, p1, IM_COL32(4, 7, 7, 210));
            if (tile == 'A') {
                const bool ns = map.debugArchNorthSouth(col, row);
                if (ns) {
                    draw->AddLine({p0.x + cell * 0.25f, p0.y},
                                  {p0.x + cell * 0.25f, p1.y}, IM_COL32(38, 28, 13, 255));
                    draw->AddLine({p0.x + cell * 0.75f, p0.y},
                                  {p0.x + cell * 0.75f, p1.y}, IM_COL32(38, 28, 13, 255));
                } else {
                    draw->AddLine({p0.x, p0.y + cell * 0.25f},
                                  {p1.x, p0.y + cell * 0.25f}, IM_COL32(38, 28, 13, 255));
                    draw->AddLine({p0.x, p0.y + cell * 0.75f},
                                  {p1.x, p0.y + cell * 0.75f}, IM_COL32(38, 28, 13, 255));
                }
            }
            if (roomLabels && room >= 0 && cell >= 13.0f) {
                const std::string label = std::to_string(room);
                draw->AddText({p0.x + 2.0f, p0.y + 1.0f}, IM_COL32(230, 245, 238, 235),
                              label.c_str());
            }
        }
    }
    draw->AddRect(origin, ImVec2(origin.x + extent.x, origin.y + extent.y),
                  IM_COL32(205, 225, 210, 220), 0.0f, 0, 1.5f);
    if (playerCol >= 0 && playerRow >= 0 && playerCol < cols && playerRow < rows) {
        const ImVec2 centre(origin.x + (float(playerCol) + 0.5f) * cell,
                            origin.y + (float(playerRow) + 0.5f) * cell);
        draw->AddCircleFilled(centre, std::max(2.5f, cell * 0.24f),
                              IM_COL32(245, 249, 236, 255));
        draw->AddCircle(centre, std::max(3.5f, cell * 0.30f),
                        IM_COL32(7, 10, 8, 255), 12, 1.5f);
    }
    ImGui::Dummy(extent); // reserve the draw-list rectangle in window layout
    ImGui::TextColored(ImVec4(0.35f, 0.75f, 0.92f, 1.0f), "S player spawn");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.79f, 0.60f, 0.24f, 1.0f), "A arch");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.83f, 0.42f, 0.17f, 1.0f), "L torch");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.74f, 0.40f, 0.86f, 1.0f), "C anchor");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.37f, 0.82f, 0.56f, 1.0f), "X exit");
    ImGui::End();
}

void drawDiagnostics(const ProfHud& prof, eng::Physics& physics)
{
    ImGui::SetNextWindowSize(ImVec2(380.0f, 320.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Diagnostics")) { ImGui::End(); return; }

    float total = 0.0f;
    for (float m : prof.ms) total += m;
    char overlay[32];
    std::snprintf(overlay, sizeof(overlay), "%.2f ms  (%.0f fps)",
                  total, total > 0.0f ? 1000.0f / total : 0.0f);
    ImGui::PlotLines("##frame", prof.frameHist, ProfHud::kHist, prof.histHead,
                     overlay, 0.0f, 33.3f, ImVec2(-1.0f, 48.0f));

    if (ImGui::CollapsingHeader("Systems (ms)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PlotHistogram("##sys", prof.ms, ProfHud::kCount, 0, nullptr,
                             0.0f, FLT_MAX, ImVec2(150.0f, 90.0f));
        ImGui::SameLine();
        ImGui::BeginGroup();
        for (int i = 0; i < ProfHud::kCount; ++i)
            ImGui::Text("%d: %-8s %5.2f ms", i, ProfHud::kNames[i], prof.ms[i]);
        ImGui::EndGroup();
    }

    if (ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Bodies active: %d", physics.activeBodyCount());
        ImGui::Text("Bodies total:  %d", physics.bodyCount());
    }

    ImGui::End();
}

} // namespace game
