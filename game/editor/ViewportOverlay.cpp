#include "ViewportOverlay.h"

#include <imgui.h>

#include <algorithm>
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

} // namespace ed
