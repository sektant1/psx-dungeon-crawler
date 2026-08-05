#include <editor/viewport/ViewportOverlay.h>

#include <editor/scene/Picker.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace ed {
namespace {

// Thousands separated by a thin space rather than a comma: a triangle count is
// read as a magnitude, not parsed, and "120 000" resolves at a glance where
// "120000" does not.
std::string grouped(std::size_t value)
{
    std::string digits = std::to_string(value);
    std::string out;
    out.reserve(digits.size() + digits.size() / 3);
    const std::size_t lead = digits.size() % 3 == 0 ? 3 : digits.size() % 3;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i == lead || (i > lead && (i - lead) % 3 == 0))
            out.push_back(' ');
        out.push_back(digits[i]);
    }
    return out;
}

std::string format(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
std::string format(const char* fmt, ...)
{
    char buffer[128];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    return buffer;
}

} // namespace

void FrameStatsSmoother::update(const FrameStats& sample, float alpha)
{
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    const auto blend = [alpha](float& value, float fresh) {
        value = value * (1.0f - alpha) + fresh * alpha;
    };
    blend(mSmoothed.fps, sample.fps);
    blend(mSmoothed.frameMs, sample.frameMs);
    // Counts are taken whole. A batch count that lags the placement that caused
    // it is not smoother, it is wrong -- and it is the number an author is
    // watching when they ask "what did that cost".
    mSmoothed.batches = sample.batches;
    mSmoothed.triangles = sample.triangles;
    mSmoothed.entities = sample.entities;
    mSmoothed.selected = sample.selected;
}

std::vector<StatsLine> frameStatsLines(const FrameStats& stats,
                                       const FrameBudget& budget)
{
    std::vector<StatsLine> lines;
    lines.push_back({format("%5.1f fps   %4.1f ms", double(stats.fps),
                            double(stats.frameMs)),
                     stats.frameMs > budget.frameMs});
    lines.push_back({format("%s batches", grouped(stats.batches).c_str()),
                     stats.batches > budget.batches});
    lines.push_back({format("%s tris", grouped(stats.triangles).c_str()),
                     stats.triangles > budget.triangles});
    lines.push_back({format("%s entities", grouped(stats.entities).c_str()),
                     false});
    if (stats.selected > 0)
        lines.push_back({format("%s selected", grouped(stats.selected).c_str()),
                         false});
    return lines;
}

void drawFrameStats(ImDrawList* list, const FrameStats& stats,
                    const FrameBudget& budget, float originX, float originY,
                    float sizeX, float sizeY)
{
    if (!list || sizeX < 64.0f || sizeY < 48.0f)
        return;

    const std::vector<StatsLine> lines = frameStatsLines(stats, budget);
    const float lineHeight = ImGui::GetTextLineHeight();
    const float padding = 6.0f;
    float width = 0.0f;
    for (const StatsLine& line : lines)
        width = std::max(width, ImGui::CalcTextSize(line.text.c_str()).x);

    const float boxWidth = width + padding * 2.0f;
    const float boxHeight = lineHeight * float(lines.size()) + padding * 2.0f;
    const ImVec2 topRight(originX + sizeX - 8.0f, originY + 8.0f);
    const ImVec2 min(topRight.x - boxWidth, topRight.y);
    const ImVec2 max(topRight.x, topRight.y + boxHeight);

    // Backed rather than shadowed: this sits over whatever the level happens to
    // be, and a dungeon is mostly the same value as the text.
    list->AddRectFilled(min, max, IM_COL32(12, 12, 16, 190), 4.0f);
    list->AddRect(min, max, IM_COL32(70, 66, 60, 200), 4.0f);

    float y = min.y + padding;
    for (const StatsLine& line : lines) {
        const ImU32 colour = line.overBudget ? IM_COL32(255, 130, 110, 255)
                                             : IM_COL32(196, 200, 210, 255);
        const float textWidth = ImGui::CalcTextSize(line.text.c_str()).x;
        // Right aligned: the numbers change width every frame, and a left edge
        // that jitters is what makes a readout unreadable.
        list->AddText(ImVec2(max.x - padding - textWidth, y), colour,
                      line.text.c_str());
        y += lineHeight;
    }
}

void drawSandboxGrid(ImDrawList* list, const glm::mat4& viewProjection,
                     glm::vec2 viewportOrigin, glm::vec2 viewportSize,
                     glm::vec3 centre, float level, float cell, int radius)
{
    if (!list || cell <= 0.0f || radius <= 0)
        return;

    // Snapped to the cell, so the lines stay put as the camera moves instead
    // of sliding under the object being edited.
    const float originX = std::floor(centre.x / cell) * cell;
    const float originZ = std::floor(centre.z / cell) * cell;
    const float far = float(radius) * cell;

    const auto segment = [&](glm::vec3 a, glm::vec3 b, ImU32 colour,
                             float thickness) {
        glm::vec2 pa, pb;
        if (!projectToViewport(a, viewProjection, viewportOrigin, viewportSize,
                               pa) ||
            !projectToViewport(b, viewProjection, viewportOrigin, viewportSize,
                               pb))
            return;
        list->AddLine(ImVec2(pa.x, pa.y), ImVec2(pb.x, pb.y), colour,
                      thickness);
    };

    for (int i = -radius; i <= radius; ++i) {
        const float x = originX + float(i) * cell;
        const float z = originZ + float(i) * cell;
        // Every fourth line brighter and the axes coloured, matching the
        // level grid's own scheme so the two read as the same surface.
        const bool axisX = std::fabs(x) < cell * 0.5f;
        const bool axisZ = std::fabs(z) < cell * 0.5f;
        const bool majorX = i % 4 == 0;
        const ImU32 colourX = axisX   ? IM_COL32(96, 130, 190, 200)
                              : majorX ? IM_COL32(104, 112, 134, 150)
                                       : IM_COL32(70, 76, 92, 110);
        const ImU32 colourZ = axisZ   ? IM_COL32(180, 92, 96, 200)
                              : majorX ? IM_COL32(104, 112, 134, 150)
                                       : IM_COL32(70, 76, 92, 110);
        segment({x, level, originZ - far}, {x, level, originZ + far}, colourX,
                axisX ? 1.6f : 1.0f);
        segment({originX - far, level, z}, {originX + far, level, z}, colourZ,
                axisZ ? 1.6f : 1.0f);
    }
}

bool drawIsolationBanner(ImDrawList* list, const std::string& objectLabel,
                         std::size_t partCount, float originX, float originY,
                         float sizeX, bool hovered)
{
    if (!list)
        return false;

    const std::string parts =
        partCount == 1 ? std::string("1 part")
                       : std::to_string(partCount) + " parts";
    const std::string left = "EDITING   " + objectLabel;
    const std::string right = parts + "      back to level  (Esc)";

    const float padding = 8.0f;
    const float height = ImGui::GetTextLineHeight() + padding * 2.0f;
    // Full width, at the top edge: this is the frame around the mode, not
    // another readout competing with the stats in the opposite corner.
    const ImVec2 min(originX, originY);
    const ImVec2 max(originX + sizeX, originY + height);

    // Warmer than the stats panel and brighter when the pointer is on it, so
    // "this strip is clickable" is legible without a button drawn inside it.
    const ImU32 fill = hovered ? IM_COL32(58, 46, 26, 230)
                               : IM_COL32(38, 31, 20, 205);
    list->AddRectFilled(min, max, fill);
    list->AddLine(ImVec2(min.x, max.y - 1.0f), ImVec2(max.x, max.y - 1.0f),
                  IM_COL32(214, 168, 74, 220), 1.0f);

    list->AddText(ImVec2(min.x + padding, min.y + padding),
                  IM_COL32(246, 214, 140, 255), left.c_str());
    const float rightWidth = ImGui::CalcTextSize(right.c_str()).x;
    list->AddText(ImVec2(max.x - padding - rightWidth, min.y + padding),
                  IM_COL32(190, 168, 130, 235), right.c_str());

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool inside = mouse.x >= min.x && mouse.x <= max.x &&
                        mouse.y >= min.y && mouse.y <= max.y;
    return hovered && inside && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}

} // namespace ed
