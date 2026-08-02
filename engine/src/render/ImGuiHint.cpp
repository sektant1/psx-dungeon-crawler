#include <eng/render/ImGuiHint.h>

#include <eng/Log.h>

#include <imgui.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <map>

namespace {

std::map<std::string, eng::imguihint::Hint>& table() {
    static std::map<std::string, eng::imguihint::Hint> instance;
    return instance;
}

// One layout for every tool tooltip: bold-ish title, dimmed wrapped body.
// Wrapping is capped so a long paragraph does not stretch to the window edge.
void draw(const char* title, const char* body) {
    if (!ImGui::BeginTooltip())
        return;
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
    if (title && title[0]) {
        ImGui::TextUnformatted(title);
        if (body && body[0])
            ImGui::Separator();
    }
    if (body && body[0])
        ImGui::TextDisabled("%s", body);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

} // namespace

namespace eng::imguihint {

bool load(const std::string& tomlPath) {
    toml::parse_result parsed = toml::parse_file(tomlPath);
    if (!parsed) {
        log::warn("imguihint: parse failed: %s", tomlPath.c_str());
        return false;
    }
    const toml::array* hints = parsed.table()["hint"].as_array();
    if (!hints) {
        log::warn("imguihint: %s has no [[hint]] array", tomlPath.c_str());
        return false;
    }
    std::map<std::string, Hint> loaded;
    for (const toml::node& node : *hints) {
        const toml::table* h = node.as_table();
        if (!h)
            continue;
        const std::string id = (*h)["id"].value_or(std::string());
        if (id.empty())
            continue;
        loaded[id] = Hint{(*h)["title"].value_or(std::string()),
                          (*h)["body"].value_or(std::string())};
    }
    table().swap(loaded);
    return true;
}

const Hint* find(const std::string& id) {
    const auto it = table().find(id);
    return it == table().end() ? nullptr : &it->second;
}

void set(const std::string& id, Hint hint) {
    table()[id] = std::move(hint);
}

std::vector<std::string> ids() {
    std::vector<std::string> out;
    out.reserve(table().size());
    for (const auto& [id, _] : table())
        out.push_back(id);
    return out;
}

bool hover(const std::string& id, const char* fallback) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort |
                              ImGuiHoveredFlags_AllowWhenDisabled) &&
        !ImGui::IsItemFocused())
        return false;
    if (const Hint* hint = id.empty() ? nullptr : find(id)) {
        draw(hint->title.c_str(), hint->body.c_str());
        return true;
    }
    if (fallback && fallback[0]) {
        draw(nullptr, fallback);
        return true;
    }
    return false;
}

void marker(const std::string& id, const char* fallback) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
    ImGui::SmallButton("?");
    ImGui::PopStyleColor(2);
    hover(id, fallback);
}

void showText(const char* title, const char* body) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) ||
        ImGui::IsItemFocused())
        draw(title, body);
}

} // namespace eng::imguihint
