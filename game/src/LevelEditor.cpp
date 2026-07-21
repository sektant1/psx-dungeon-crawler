#include "LevelEditor.h"

#include "DungeonMap.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>

namespace {
struct Brush { char tile; const char* label; const char* hint; ImU32 colour; };
constexpr std::array<Brush, 11> kBrushes{{
    {'#', "Wall", "Solid rock", IM_COL32(35, 39, 43, 255)},
    {'.', "Floor", "Walkable floor", IM_COL32(67, 91, 92, 255)},
    {'A', "Arch", "Straight doorway", IM_COL32(202, 153, 61, 255)},
    {'L', "Torch", "Floor with wall torch", IM_COL32(210, 106, 43, 255)},
    {'S', "Spawn", "Unique player start", IM_COL32(89, 190, 236, 255)},
    {'C', "Anchor", "Unique scene origin", IM_COL32(188, 102, 220, 255)},
    {'X', "Exit", "Unique down portal", IM_COL32(95, 210, 143, 255)},
    {'H', "Chest", "Loot chest and blocker", IM_COL32(211, 172, 70, 255)},
    {'B', "Barrel", "Wooden barrel and blocker", IM_COL32(139, 91, 55, 255)},
    {'R', "Crate", "Storage crate and blocker", IM_COL32(164, 119, 67, 255)},
    {'V', "Urn", "Breakable-style ceramic urn", IM_COL32(178, 99, 72, 255)},
}};

ImU32 tileColour(char tile)
{
    for (const Brush& brush : kBrushes)
        if (brush.tile == tile) return brush.colour;
    return IM_COL32(12, 15, 17, 255);
}
}

LevelEditor::LevelEditor(std::vector<std::string> rows, std::string defaultPath,
                         bool workspace)
    : mDocument(std::move(rows)), mPath(std::move(defaultPath)),
      mWorkspace(workspace)
{}

void LevelEditor::setStatus(std::string text, bool error)
{
    mStatus = std::move(text);
    mStatusError = error;
}

bool LevelEditor::draw(const DungeonMap& map, glm::vec3 playerPosition)
{
    bool apply = false;
    if (mWorkspace) {
        static bool styled = false;
        if (!styled) {
            ImGuiStyle& style = ImGui::GetStyle();
            style.WindowRounding = 0.0f;
            style.ChildRounding = 3.0f;
            style.FrameRounding = 2.0f;
            style.FramePadding = ImVec2(8.0f, 5.0f);
            style.ItemSpacing = ImVec2(7.0f, 7.0f);
            style.WindowPadding = ImVec2(14.0f, 12.0f);
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.035f, 0.043f, 0.047f, 1.0f);
            style.Colors[ImGuiCol_ChildBg] = ImVec4(0.050f, 0.061f, 0.065f, 1.0f);
            style.Colors[ImGuiCol_FrameBg] = ImVec4(0.075f, 0.090f, 0.096f, 1.0f);
            style.Colors[ImGuiCol_Button] = ImVec4(0.11f, 0.17f, 0.20f, 1.0f);
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.20f, 0.30f, 0.32f, 1.0f);
            style.Colors[ImGuiCol_Header] = ImVec4(0.16f, 0.24f, 0.26f, 1.0f);
            style.Colors[ImGuiCol_Separator] = ImVec4(0.31f, 0.39f, 0.38f, 0.65f);
            style.Colors[ImGuiCol_CheckMark] = ImVec4(0.89f, 0.60f, 0.24f, 1.0f);
            styled = true;
        }
    }
    ImGuiWindowFlags flags = 0;
    if (mWorkspace) {
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    } else {
        ImGui::SetNextWindowSize(ImVec2(550.0f, 700.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(400.0f, 10.0f), ImGuiCond_FirstUseEver);
    }
    if (!ImGui::Begin("Dungeon Level Editor", nullptr, flags)) {
        ImGui::End();
        return false;
    }

    const gen::Layout validated = mDocument.validated(true);
    const bool valid = validated.valid();
    ImGui::TextColored(ImVec4(0.91f, 0.67f, 0.31f, 1.0f),
                       "DUNGEON WORKSHOP");
    ImGui::SameLine();
    ImGui::TextDisabled("  %d x %d cells  /  %s%s", mDocument.columns(),
                        mDocument.rows(), valid ? "ready" : "needs attention",
                        mDocument.dirty() ? "  |  unsaved" : "");
    ImGui::Separator();

    if (!valid) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.28f, 0.08f, 0.07f, 0.82f));
        ImGui::BeginChild("##validation", ImVec2(0, 38), ImGuiChildFlags_Borders);
        ImGui::TextColored(ImVec4(1.0f, 0.60f, 0.48f, 1.0f), "Validation: %s",
                           validated.error().c_str());
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    if (!mDocument.canUndo()) ImGui::BeginDisabled();
    if (ImGui::Button("Undo  Ctrl+Z")) mDocument.undo();
    if (!mDocument.canUndo()) ImGui::EndDisabled();
    ImGui::SameLine();
    if (!mDocument.canRedo()) ImGui::BeginDisabled();
    if (ImGui::Button("Redo  Ctrl+Y")) mDocument.redo();
    if (!mDocument.canRedo()) ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Checkbox("Room IDs", &mShowRooms);

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) mDocument.undo();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) mDocument.redo();
    }

    ImGui::SeparatorText("Palette");
    for (size_t i = 0; i < kBrushes.size(); ++i) {
        const Brush& brush = kBrushes[i];
        if (i && i != 4 && i != 7) ImGui::SameLine();
        const bool selected = mBrush == brush.tile;
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.40f, 0.16f, 1.0f));
        char label[40];
        std::snprintf(label, sizeof(label), "%c  %s", brush.tile, brush.label);
        if (ImGui::Button(label)) mBrush = brush.tile;
        if (selected) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", brush.hint);
    }

    ImGui::SeparatorText("Layout");
    const int cols = mDocument.columns(), rows = mDocument.rows();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float byWidth = (available.x - 12.0f) / float(cols);
    const float byHeight = (available.y - (mWorkspace ? 185.0f : 0.0f)) /
                           float(rows);
    const float cellSize = std::clamp(mWorkspace ? std::min(byWidth, byHeight)
                                                  : byWidth,
                                      9.0f, 22.0f);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 extent(cellSize * float(cols), cellSize * float(rows));
    if (mWorkspace && available.x > extent.x) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                             (available.x - extent.x) * 0.5f);
        origin = ImGui::GetCursorScreenPos();
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    int playerCol = -1, playerRow = -1;
    map.debugCellOf(playerPosition, playerCol, playerRow);

    ImGui::InvisibleButton("##level_canvas", extent,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                    ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const int col = int((mouse.x - origin.x) / cellSize);
        const int row = int((mouse.y - origin.y) / cellSize);
        mDocument.paint(col, row, ImGui::IsMouseClicked(ImGuiMouseButton_Right) ? '#' : mBrush);
    }
    for (int row = 0; row < rows; ++row) for (int col = 0; col < cols; ++col) {
        const char tile = mDocument.cell(col, row);
        const ImVec2 p0(origin.x + col * cellSize, origin.y + row * cellSize);
        const ImVec2 p1(p0.x + cellSize, p0.y + cellSize);
        dl->AddRectFilled(p0, p1, tileColour(tile));
        dl->AddRect(p0, p1, IM_COL32(9, 12, 14, 190));
        if (tile != '#' && tile != '.' && cellSize >= 12.0f) {
            char glyph[2]{tile, 0};
            const ImVec2 text = ImGui::CalcTextSize(glyph);
            dl->AddText({p0.x + (cellSize - text.x) * 0.5f,
                         p0.y + (cellSize - text.y) * 0.5f},
                        IM_COL32(245, 245, 232, 255), glyph);
        }
        if (mShowRooms && valid) {
            const int room = validated.roomAt(col, row);
            if (room >= 0 && cellSize >= 16.0f) {
                const std::string id = std::to_string(room);
                dl->AddText({p0.x + 2, p0.y + 1}, IM_COL32(230, 245, 238, 180), id.c_str());
            }
        }
    }
    dl->AddRect(origin, {origin.x + extent.x, origin.y + extent.y},
                IM_COL32(201, 214, 207, 220), 0, 0, 1.5f);
    if (playerCol >= 0 && playerRow >= 0 && playerCol < cols && playerRow < rows) {
        const ImVec2 p(origin.x + (playerCol + 0.5f) * cellSize,
                       origin.y + (playerRow + 0.5f) * cellSize);
        dl->AddCircleFilled(p, std::max(3.0f, cellSize * 0.20f), IM_COL32_WHITE);
    }
    ImGui::TextDisabled("Left click paints selected tile. Right click paints wall.");

    ImGui::SeparatorText("Build & file");
    ImGui::SetNextItemWidth(110.0f);
    ImGui::InputScalar("Seed", ImGuiDataType_U32, &mSeed);
    ImGui::SameLine();
    if (ImGui::Button("Generate")) {
        mDocument.replace(gen::generate(mSeed).rows());
        setStatus("Generated a new editable layout.");
    }
    ImGui::SameLine();
    if (!valid) ImGui::BeginDisabled();
    if (ImGui::Button("Play changes")) apply = true;
    if (!valid) ImGui::EndDisabled();
    ImGui::SetNextItemWidth(-160.0f);
    char path[512];
    std::snprintf(path, sizeof(path), "%s", mPath.c_str());
    if (ImGui::InputText("##path", path, sizeof(path))) mPath = path;
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        std::string error;
        setStatus(mDocument.loadToml(mPath, error) ? "Loaded level." : error,
                  !error.empty());
    }
    ImGui::SameLine();
    if (!valid) ImGui::BeginDisabled();
    if (ImGui::Button("Save")) {
        std::string error;
        setStatus(mDocument.saveToml(mPath, error) ? "Saved level." : error,
                  !error.empty());
    }
    if (!valid) ImGui::EndDisabled();
    if (!mStatus.empty())
        ImGui::TextColored(mStatusError ? ImVec4(1, 0.45f, 0.35f, 1)
                                        : ImVec4(0.45f, 0.85f, 0.58f, 1),
                           "%s", mStatus.c_str());
    ImGui::End();
    return apply;
}
