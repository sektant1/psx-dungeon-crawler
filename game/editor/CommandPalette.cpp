#include "CommandPalette.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>

namespace ed {
namespace {

char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
}

bool wordStart(const std::string& text, std::size_t at)
{
    if (at == 0)
        return true;
    const char prev = text[at - 1];
    return prev == ' ' || prev == '_' || prev == '.' || prev == '-' ||
           prev == '/';
}

// Subsequence match of `needle` (already lowercase) inside `text`. Returns a
// score, or -1 when the letters do not appear in order at all.
int fuzzyScore(const std::string& text, const std::string& needle)
{
    if (needle.empty())
        return 0;

    int score = 0;
    std::size_t at = 0;
    std::size_t previous = std::string::npos;
    for (const char want : needle) {
        while (at < text.size() && lower(text[at]) != want)
            ++at;
        if (at >= text.size())
            return -1;

        if (wordStart(text, at))
            score += 10; // "cs" should find "Cook scene", not "pieces"
        else if (previous != std::string::npos && at == previous + 1)
            score += 8; // a run of letters is a stronger signal than a scatter
        else
            score += 1;

        // Distance from the previous hit, mildly discouraged: it separates two
        // otherwise equal matches without letting one long gap sink a name.
        if (previous != std::string::npos)
            score -= int(std::min<std::size_t>(at - previous - 1, 4));

        previous = at;
        ++at;
    }
    return score;
}

} // namespace

std::vector<PaletteMatch> matchPalette(const std::vector<PaletteAction>& actions,
                                       const std::string& query)
{
    std::string needle;
    needle.reserve(query.size());
    for (const char c : query)
        if (c != ' ')
            needle.push_back(lower(c));

    std::vector<PaletteMatch> matches;
    matches.reserve(actions.size());
    for (std::size_t i = 0; i < actions.size(); ++i) {
        const PaletteAction& action = actions[i];
        if (needle.empty()) {
            matches.push_back({i, 0});
            continue;
        }

        int score = fuzzyScore(action.label, needle);
        if (score >= 0) {
            // A hit on the name outranks a hit on the group: typing "place"
            // means the verb, not every action filed under the place tool.
            score += 40;
            // Shorter names first among equals -- "Save scene" over "Save
            // scene as...". Deliberately a whisper next to the per-letter
            // scores: a name being short is a tiebreak, not evidence, and at
            // full weight it floats every short unrelated action to the top.
            score += std::max(0, 8 - int(action.label.size() / 8));
        }
        else {
            score = fuzzyScore(action.group, needle);
            if (score < 0)
                continue;
        }
        if (!action.enabled)
            score -= 12;
        matches.push_back({i, score});
    }

    // Stable, so equal scores keep registration order and the list does not
    // reshuffle as the author types a character that changes nothing.
    std::stable_sort(matches.begin(), matches.end(),
                     [](const PaletteMatch& a, const PaletteMatch& b) {
                         return a.score > b.score;
                     });
    return matches;
}

void openPalette(PaletteState& state)
{
    state.open = true;
    state.query[0] = '\0';
    state.highlighted = 0;
    state.focusInput = true;
}

bool drawCommandPalette(PaletteState& state,
                        const std::vector<PaletteAction>& actions)
{
    if (!state.open)
        return false;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float width = std::min(720.0f, viewport->WorkSize.x * 0.6f);
    // Near the top, where a palette is expected and where it cannot cover the
    // thing the author is about to act on.
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
               viewport->WorkPos.y + viewport->WorkSize.y * 0.18f),
        ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);

    if (!ImGui::IsPopupOpen("##palette"))
        ImGui::OpenPopup("##palette");

    bool ran = false;
    const std::vector<PaletteMatch> matches = matchPalette(actions, state.query);
    if (ImGui::BeginPopup("##palette",
                          ImGuiWindowFlags_NoMove |
                              ImGuiWindowFlags_NoSavedSettings |
                              ImGuiWindowFlags_NoResize)) {
        if (state.focusInput) {
            ImGui::SetKeyboardFocusHere();
            state.focusInput = false;
        }
        ImGui::SetNextItemWidth(-1.0f);
        // Enter is handled below rather than through the input's return flag:
        // the highlight can be moved with the arrows, so the committed action
        // is the highlighted row, not whatever the text happens to spell.
        ImGui::InputTextWithHint("##query", "type a command", state.query,
                                 sizeof(state.query));

        const int count = int(matches.size());
        // The arrows move the highlight while the text field keeps the caret,
        // which is why the input box does not consume them.
        if (count > 0) {
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))
                state.highlighted = (state.highlighted + 1) % count;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))
                state.highlighted = (state.highlighted + count - 1) % count;
        }
        state.highlighted = std::clamp(state.highlighted, 0, std::max(0, count - 1));

        ImGui::Separator();
        int chosen = -1;
        if (matches.empty())
            ImGui::TextDisabled("nothing matches");

        const float rows = std::min(10.0f, float(std::max(count, 1)));
        const float rowHeight = ImGui::GetTextLineHeightWithSpacing() * 2.05f;
        if (ImGui::BeginChild("##results", ImVec2(0.0f, rows * rowHeight))) {
            for (int i = 0; i < count; ++i) {
                const PaletteAction& action = actions[matches[std::size_t(i)].index];
                ImGui::PushID(i);
                const bool highlighted = i == state.highlighted;
                if (ImGui::Selectable("##row", highlighted,
                                      ImGuiSelectableFlags_AllowDoubleClick,
                                      ImVec2(0.0f, rowHeight - 4.0f)))
                    chosen = i;
                if (ImGui::IsItemHovered())
                    state.highlighted = i;
                // Keeps the highlight visible when the arrows walk it past the
                // bottom of the list.
                if (highlighted && ImGui::IsWindowAppearing())
                    ImGui::SetScrollHereY(0.5f);

                // Both corners are read here, while the row's own item is still
                // the last one submitted: the label and the group drawn below
                // become "the last item", and asking them for the row's right
                // edge put the shortcut on top of the label instead of at the
                // end of the row.
                const ImVec2 rowMin = ImGui::GetItemRectMin();
                const ImVec2 rowMax = ImGui::GetItemRectMax();
                // Where the next row belongs. The label and the group below are
                // drawn *over* this row at absolute positions, which leaves the
                // cursor wherever the last of them ended -- and the next row
                // then starts there, so the list walked itself into overlapping
                // text the further down it went.
                const ImVec2 nextRow = ImGui::GetCursorScreenPos();
                ImGui::SetCursorScreenPos(
                    ImVec2(rowMin.x + 4.0f, rowMin.y + 2.0f));
                ImGui::BeginGroup();
                if (action.enabled)
                    ImGui::TextUnformatted(action.label.c_str());
                else
                    ImGui::TextDisabled("%s", action.label.c_str());
                ImGui::TextDisabled("%s%s%s", action.group.c_str(),
                                    action.detail.empty() ? "" : "  -  ",
                                    action.detail.c_str());
                ImGui::EndGroup();

                if (!action.shortcut.empty()) {
                    const float w = ImGui::CalcTextSize(action.shortcut.c_str()).x;
                    ImGui::SetCursorScreenPos(
                        ImVec2(rowMax.x - w - 12.0f, rowMin.y + 2.0f));
                    ImGui::TextDisabled("%s", action.shortcut.c_str());
                }
                ImGui::SetCursorScreenPos(nextRow);
                ImGui::PopID();
            }
        }
        ImGui::EndChild();

        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
            ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
            chosen = state.highlighted;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            state.open = false;
            ImGui::CloseCurrentPopup();
        }

        if (chosen >= 0 && chosen < count) {
            const PaletteAction& action =
                actions[matches[std::size_t(chosen)].index];
            if (action.enabled && action.run) {
                state.open = false;
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                // Outside the popup: the action may open one of its own.
                action.run();
                return true;
            }
        }
        ImGui::EndPopup();
    }
    else {
        // Clicked away.
        state.open = false;
    }
    return ran;
}

} // namespace ed
