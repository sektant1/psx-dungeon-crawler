#include <editor/ui/EditorShell.h>

#include <editor/content/SceneContract.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace ed;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "EditorShellTests: " << message << '\n';
        std::exit(1);
    }
}

static bool near(float value, float expected, float tolerance = 0.5f)
{
    return std::abs(value - expected) <= tolerance;
}

int main()
{
    // --- the top bar's three zones -------------------------------------------
    {
        // A wide window: the switcher sits dead centre and the play controls
        // are hard right, which is the arrangement the whole layout exists for.
        const TopBarLayout layout =
            layoutTopBar(1600.0f, 320.0f, 200.0f, 140.0f, 12.0f);
        require(layout.switcherFits, "a 1600px bar fits all three zones");
        require(layout.switcherCentred, "a wide bar centres the switcher");
        require(near(layout.switcherX, (1600.0f - 200.0f) * 0.5f),
                "centred means centred on the bar, not on what is left of it");
        require(near(layout.playX, 1600.0f - 140.0f - 12.0f),
                "play controls are right-aligned");
    }
    {
        // The centred position would overlap the menus, so the switcher gives
        // way rather than being drawn over them. This is the case a naive
        // "(width - w) / 2" gets wrong, and it is the common one on a laptop.
        const TopBarLayout layout =
            layoutTopBar(760.0f, 330.0f, 200.0f, 140.0f, 12.0f);
        require(layout.switcherFits, "760px still fits, packed left");
        require(!layout.switcherCentred,
                "a narrow bar reports that the switcher was pushed");
        require(near(layout.switcherX, 342.0f),
                "pushed means resting against the menus");
        require(layout.switcherX + 200.0f <= layout.playX,
                "the switcher never overlaps the play controls");
    }
    {
        // Narrower than the three zones can ever be. The caller drops the
        // switcher; reporting a clamped position instead would draw it under
        // the play controls.
        const TopBarLayout layout =
            layoutTopBar(500.0f, 330.0f, 200.0f, 140.0f, 12.0f);
        require(!layout.switcherFits,
                "a bar too narrow for all three says so rather than overlapping");
    }
    {
        // The zones stay separated as the menus grow, right up to the point
        // where they cannot.
        for (float menus = 100.0f; menus < 900.0f; menus += 37.0f) {
            const TopBarLayout layout =
                layoutTopBar(1280.0f, menus, 200.0f, 140.0f, 12.0f);
            if (!layout.switcherFits)
                continue;
            require(layout.switcherX >= menus,
                    "the switcher never runs back over the menus");
            require(layout.switcherX + 200.0f <= layout.playX,
                    "the switcher never runs into the play controls");
        }
    }

    // --- the bottom panel ----------------------------------------------------
    {
        BottomPanel panel;
        require(!panel.isOpen(), "the bottom panel starts collapsed");
        require(bottomPanelHeight(panel, 900.0f, 100.0f) == 0.0f,
                "a collapsed panel takes no height at all");

        panel.toggle(BottomTab::Output);
        require(panel.isOpen(BottomTab::Output), "clicking a button opens it");
        require(bottomPanelHeight(panel, 900.0f, 100.0f) > 0.0f,
                "an open panel has height");

        panel.toggle(BottomTab::Output);
        require(!panel.isOpen(), "clicking the same button closes it");

        panel.toggle(BottomTab::Problems);
        panel.toggle(BottomTab::Timeline);
        require(panel.isOpen(BottomTab::Timeline) &&
                    !panel.isOpen(BottomTab::Problems),
                "only one tab is open at a time");
    }
    {
        BottomPanel panel;
        panel.show(BottomTab::Output);
        panel.height = 4000.0f; // dragged past the top of the window
        require(bottomPanelHeight(panel, 900.0f, 100.0f) <= 450.0f,
                "the panel never takes more than half the workspace");
        panel.height = 1.0f; // dragged shut
        require(bottomPanelHeight(panel, 900.0f, 100.0f) >= 100.0f,
                "the panel never shrinks below something readable");
        // A window shorter than the minimum: the floor wins. A panel too small
        // to show a row of text is worse than a cramped viewport, and the
        // author can always close it.
        require(bottomPanelHeight(panel, 120.0f, 100.0f) >= 100.0f,
                "the floor beats the ceiling on a very short window");
    }
    {
        // The height survives a collapse. A panel that reopens at somebody
        // else's size is a panel not worth collapsing.
        BottomPanel panel;
        panel.show(BottomTab::Problems);
        panel.height = 300.0f;
        panel.close();
        panel.show(BottomTab::Problems);
        require(near(bottomPanelHeight(panel, 1200.0f, 100.0f), 300.0f),
                "reopening restores the height it was dragged to");
    }

    // --- the contract drives the main screen ---------------------------------
    {
        using game::content::SceneKind;
        require(mainScreenForKind(SceneKind::Screen) == MainScreen::Screen2D,
                "a flat page opens in the 2D editor");
        for (const SceneKind kind :
             {SceneKind::Empty, SceneKind::GameDriven, SceneKind::Shot,
              SceneKind::FirstPerson, SceneKind::ThirdPerson}) {
            require(mainScreenForKind(kind) == MainScreen::Scene3D,
                    "every world scene opens in the 3D editor");
        }
    }

    // --- names ---------------------------------------------------------------
    {
        for (int i = 0; i < kMainScreenCount; ++i) {
            require(mainScreenName(MainScreen(i))[0] != '\0',
                    "every main screen is named");
            require(mainScreenSummary(MainScreen(i))[0] != '\0',
                    "every main screen says what it is for");
        }
        for (int i = 0; i < kBottomTabCount; ++i)
            require(bottomTabName(BottomTab(i))[0] != '\0',
                    "every bottom tab is named");
    }

    std::cout << "EditorShellTests: ok\n";
    return 0;
}
