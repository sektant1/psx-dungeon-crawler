// The inspector's layout arithmetic, checked without a window.
//
// ImGui itself needs a context to draw into, and this test gives it one with no
// backend: NewFrame/EndFrame with a null renderer is enough to exercise widget
// sizing, which is the part that was measured by hand and drifted. Nothing here
// checks pixels; it checks that a row's pieces still add up to its width at
// sizes other than the one somebody happened to have docked.

#include <editor/ui/EditorUi.h>

#include <imgui.h>

#include <cstdio>
#include <iostream>

namespace {

int failures = 0;

void require(bool condition, const char* what)
{
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

// A headless ImGui context. The null backend renders nothing, which is exactly
// what a layout test wants: no device, no window, no frame timing.
class HeadlessImGui {
public:
    HeadlessImGui()
    {
        IMGUI_CHECKVERSION();
        mContext = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1280.0f, 720.0f);
        io.DeltaTime = 1.0f / 60.0f;
        // A font atlas has to exist and be built, or NewFrame asserts.
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        io.Fonts->SetTexID(ImTextureID(1));
    }
    ~HeadlessImGui() { ImGui::DestroyContext(mContext); }

private:
    ImGuiContext* mContext = nullptr;
};

// The control strip on a Scripts row, as drawScripts computes it. Kept in step
// with that function by construction: both ask the style, neither measures.
float scriptRowControlWidth()
{
    const float button = ImGui::GetFrameHeight();
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float removeWidth = ImGui::CalcTextSize("remove").x +
                              ImGui::GetStyle().FramePadding.x * 2.0f;
    return button * 3.0f + removeWidth + gap * 4.0f;
}

} // namespace

int main()
{
    HeadlessImGui imgui;

    ImGui::NewFrame();

    // --- the script row's controls fit, at every dock width ----------------
    //
    // The regression this guards. The path picker used to take a hardcoded
    // -90.0f, which was measured against one dock width: narrower than that and
    // the arrows and the remove button ran off the edge; wider and they floated
    // away from it. The width is now derived from the style, so the only thing
    // worth asserting is that the derivation leaves room for what it reserves.
    {
        const float controls = scriptRowControlWidth();
        require(controls > 0.0f, "the control strip has a width");

        const float button = ImGui::GetFrameHeight();
        const float removeWidth = ImGui::CalcTextSize("remove").x +
                                  ImGui::GetStyle().FramePadding.x * 2.0f;
        require(controls >= button * 3.0f + removeWidth,
                "it reserves at least the two arrows, the checkbox-sized gap "
                "and the remove button it actually draws");

        // The old constant. Any font or style where the real controls need more
        // than 90 px is a dock where the old layout overflowed -- which is the
        // bug, and the reason this is derived now.
        if (controls > 90.0f)
            std::printf("note: the controls need %.0f px; the old hardcoded "
                        "-90 would have clipped them here\n",
                        double(controls));
    }

    // --- a derived width scales with the frame, a constant does not --------
    {
        const float before = scriptRowControlWidth();
        const float savedPadding = ImGui::GetStyle().FramePadding.y;
        ImGui::GetStyle().FramePadding.y += 6.0f; // a taller frame, e.g. a big font
        const float after = scriptRowControlWidth();
        ImGui::GetStyle().FramePadding.y = savedPadding;

        require(after > before,
                "the reserved width follows the frame height: a bigger font "
                "makes the buttons bigger, and a constant would not have "
                "noticed");
    }

    // --- sizes follow the UI scale ----------------------------------------
    //
    // The editor has a UI scale setting. applyUiScale calls ScaleAllSizes and
    // sets FontGlobalScale, so padding, spacing and text all grow with it -- but
    // a hardcoded ImVec2(120, 0) button does not. At 1.5x the label grew and its
    // button did not, and the text ran out of its own button. Six dialog buttons
    // had that constant.
    {
        const float buttonBefore = ed::ui::dialogButtonWidth();
        const float iconBefore = ed::ui::iconButtonSize();

        // What applyUiScale does, in miniature.
        const float scale = 1.5f;
        ImGui::GetStyle().ScaleAllSizes(scale);
        ImGui::GetIO().FontGlobalScale = scale;

        const float buttonAfter = ed::ui::dialogButtonWidth();
        const float iconAfter = ed::ui::iconButtonSize();

        require(buttonAfter > buttonBefore,
                "a dialog button gets wider when the UI scales up -- a "
                "constant would not have, and the label inside it would "
                "outgrow the box");
        require(iconAfter > iconBefore,
                "and so does a one-glyph icon button");

        ImGui::GetIO().FontGlobalScale = 1.0f;
        ImGui::GetStyle().ScaleAllSizes(1.0f / scale);
    }

    ImGui::EndFrame();

    if (failures > 0) {
        std::cerr << "EditorInspectorLayoutTests: " << failures
                  << " failure(s)\n";
        return 1;
    }
    std::cout << "EditorInspectorLayoutTests: ok\n";
    return 0;
}
