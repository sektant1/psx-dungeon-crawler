#include <editor/ui/EditorShell.h>

#include <editor/content/SceneContract.h>

#include <algorithm>

namespace ed {

const char* mainScreenName(MainScreen screen)
{
    switch (screen) {
    case MainScreen::Scene3D:
        return "3D";
    case MainScreen::Screen2D:
        return "2D";
    case MainScreen::Material:
        return "Material";
    }
    return "3D";
}

const char* mainScreenSummary(MainScreen screen)
{
    switch (screen) {
    case MainScreen::Scene3D:
        return "The level: place kit pieces, move things, walk the room.";
    case MainScreen::Screen2D:
        return "The page: a screen scene authored in pixels, with the game's "
               "own HUD drawn over it.";
    case MainScreen::Material:
        return "The staging scene: one sphere, one material, nothing else in "
               "the frame.";
    }
    return "";
}

MainScreen mainScreenForKind(game::content::SceneKind kind)
{
    // Only the flat page wants the 2D editor. Everything else -- including an
    // Empty scene, which is usually a level somebody has not finished yet -- is
    // a world, and opening a world in the page editor would show it edge-on.
    return kind == game::content::SceneKind::Screen ? MainScreen::Screen2D
                                                    : MainScreen::Scene3D;
}

TopBarLayout layoutTopBar(float barWidth, float menusEndX, float switcherWidth,
                          float playWidth, float pad)
{
    TopBarLayout out;
    barWidth = std::max(barWidth, 1.0f);
    pad = std::max(pad, 0.0f);

    out.playX = barWidth - playWidth - pad;

    // The centred position, and the two walls it must stay between.
    const float centred = (barWidth - switcherWidth) * 0.5f;
    const float leftWall = menusEndX + pad;
    const float rightWall = out.playX - pad - switcherWidth;

    out.switcherFits = rightWall >= leftWall;
    if (!out.switcherFits) {
        // Nothing legible is possible on this width. Report it and let the
        // caller drop the zone rather than drawing two things on top of each
        // other, which is what a naive clamp would produce.
        out.switcherCentred = false;
        out.switcherX = leftWall;
        return out;
    }

    out.switcherX = std::clamp(centred, leftWall, rightWall);
    // Only genuinely centred when nothing pushed it. Half a pixel of rounding
    // is not a layout change, so the comparison is loose.
    out.switcherCentred = std::abs(out.switcherX - centred) <= 0.5f;
    return out;
}

const char* bottomTabName(BottomTab tab)
{
    switch (tab) {
    case BottomTab::Output:
        return "Output";
    case BottomTab::Problems:
        return "Problems";
    case BottomTab::Timeline:
        return "Timeline";
    case BottomTab::Scripts:
        return "Scripts";
    }
    return "";
}

float bottomPanelHeight(const BottomPanel& panel, float windowHeight,
                        float minimumPixels)
{
    if (!panel.isOpen())
        return 0.0f;
    windowHeight = std::max(windowHeight, 1.0f);
    minimumPixels = std::max(minimumPixels, 1.0f);
    // Never more than half the space it shares with the workspace: a bottom
    // panel bigger than the viewport it is read against has stopped being a
    // panel. The floor wins over the ceiling on a very short window, because a
    // panel too small to show a row of text is worse than a cramped viewport --
    // the author can always close it.
    const float ceiling = std::max(windowHeight * 0.5f, minimumPixels);
    return std::clamp(panel.height, minimumPixels, ceiling);
}

} // namespace ed
