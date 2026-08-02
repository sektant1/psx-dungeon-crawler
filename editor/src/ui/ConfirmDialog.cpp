#include <editor/ui/ConfirmDialog.h>

#include <imgui.h>

#include <algorithm>

namespace ed {
namespace {

constexpr const char* kTitle = "Confirm";

struct State {
    bool open = false;
    bool requested = false; // opened this frame, popup not yet told
    std::string question;
    std::string detail;
    std::string confirmLabel;
    std::function<void()> onConfirm;
};

State& state()
{
    static State instance;
    return instance;
}

} // namespace

void ConfirmDialog::open(std::string question, std::string detail,
                         std::function<void()> onConfirm,
                         std::string confirmLabel)
{
    State& s = state();
    s.open = true;
    s.requested = true;
    s.question = std::move(question);
    s.detail = std::move(detail);
    s.confirmLabel = std::move(confirmLabel);
    s.onConfirm = std::move(onConfirm);
}

bool ConfirmDialog::isOpen()
{
    return state().open;
}

void ConfirmDialog::cancel()
{
    State& s = state();
    s.open = false;
    s.requested = false;
    s.onConfirm = nullptr;
}

bool ConfirmDialog::draw()
{
    State& s = state();
    if (!s.open)
        return false;
    if (s.requested) {
        ImGui::OpenPopup(kTitle);
        s.requested = false;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float maxWidth = std::max(260.0f, viewport->WorkSize.x - 32.0f);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(std::min(280.0f, maxWidth), 0.0f),
        ImVec2(std::min(560.0f, maxWidth),
               std::max(180.0f, viewport->WorkSize.y - 32.0f)));
    if (!ImGui::BeginPopupModal(kTitle, nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoSavedSettings))
        return false;

    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() +
                           std::min(520.0f, maxWidth - 32.0f));
    ImGui::TextWrapped("%s", s.question.c_str());
    if (!s.detail.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", s.detail.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::PopTextWrapPos();
    ImGui::Separator();

    // Cancel takes the leftmost position and Escape, because it is the choice
    // that cannot lose anything. The confirm button is named for what it does
    // -- "Delete", "Discard" -- rather than "OK": a button labelled OK is one
    // people press without reading the sentence above it.
    const float cancelWidth = ImGui::CalcTextSize("Cancel").x +
                              ImGui::GetStyle().FramePadding.x * 2.0f + 24.0f;
    const float confirmWidth = ImGui::CalcTextSize(s.confirmLabel.c_str()).x +
                               ImGui::GetStyle().FramePadding.x * 2.0f + 24.0f;
    const float buttonWidth = std::max({120.0f, cancelWidth, confirmWidth});
    if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0.0f)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        s.open = false;
        s.onConfirm = nullptr;
        ImGui::CloseCurrentPopup();
    }
    if (ImGui::IsWindowAppearing())
        ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
    if (ImGui::Button(s.confirmLabel.c_str(), ImVec2(buttonWidth, 0.0f))) {
        std::function<void()> action = std::move(s.onConfirm);
        s.open = false;
        s.onConfirm = nullptr;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        // Outside the popup: the action may open one of its own, and it must
        // not be fighting this one for the modal stack.
        if (action)
            action();
        return true;
    }
    ImGui::EndPopup();
    return false;
}

} // namespace ed
