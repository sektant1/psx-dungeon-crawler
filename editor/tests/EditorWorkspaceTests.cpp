#include <editor/ui/EditorWorkspace.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace ed;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "EditorWorkspaceTests: " << message << '\n';
        std::exit(1);
    }
}

static bool near(float value, float expected, float tolerance = 1.0f)
{
    return std::abs(value - expected) <= tolerance;
}

int main()
{
    {
        const WorkspacePlan plan = makeWorkspacePlan(1280.0f, 672.0f, 1.0f);
        require(near(plan.leftPixels, 248.0f),
                "720p keeps a usable asset rail");
        require(near(plan.rightPixels, 296.0f),
                "720p keeps a usable inspector");
        require(1280.0f - plan.leftPixels - plan.rightPixels >= 720.0f,
                "720p protects the scene workspace");
        require(plan.bottomPanelPixels <= 672.0f * 0.5f,
                "720p diagnostics do not consume the scene");
    }
    {
        const WorkspacePlan plan = makeWorkspacePlan(1600.0f, 902.0f, 1.0f);
        require(near(plan.leftPixels, 304.0f),
                "reference width gets a 304px browse rail");
        require(near(plan.rightPixels, 360.0f),
                "reference width gets a 360px inspector");
        require(near(1600.0f - plan.leftPixels - plan.rightPixels, 936.0f),
                "reference width leaves most space to the scene");
    }
    {
        const WorkspacePlan plan = makeWorkspacePlan(2560.0f, 1032.0f, 1.0f);
        require(near(plan.leftPixels, 320.0f) && near(plan.rightPixels, 440.0f),
                "ultrawide rails stop growing");
        require(near(2560.0f - plan.leftPixels - plan.rightPixels, 1800.0f),
                "ultrawide width belongs to the scene");
    }
    {
        const WorkspacePlan plan = makeWorkspacePlan(1280.0f, 672.0f, 2.0f);
        require(1280.0f - plan.leftPixels - plan.rightPixels >= 750.0f,
                "large text cannot erase the 720p workspace");
    }
    {
        // The bottom panel is not a dock split any more -- it lives outside the
        // dockspace and starts closed -- so this is only the height it opens
        // at. It still has to be a panel rather than a slot.
        const WorkspacePlan plan = makeWorkspacePlan(1600.0f, 950.0f, 1.0f);
        require(plan.bottomPanelPixels >= 160.0f &&
                    plan.bottomPanelPixels <= 320.0f,
                "the bottom panel opens at a readable height");
        require(plan.bottomPanelPixels < 950.0f * 0.5f,
                "and never at half the window");
    }
    {
        // The left column is split rather than tabbed, so the split has to
        // leave both lists usable. The file browser has a preview swatch and a
        // metadata block above its list that do not shrink, which is why the
        // share tips away from the tree on a short window.
        const WorkspacePlan tall = makeWorkspacePlan(1600.0f, 1400.0f, 1.0f);
        const WorkspacePlan short_ = makeWorkspacePlan(1600.0f, 600.0f, 1.0f);
        require(tall.sceneTreeFraction > short_.sceneTreeFraction,
                "a taller window gives more of the column to the tree");
        for (const WorkspacePlan& plan : {tall, short_}) {
            require(plan.sceneTreeFraction >= 0.40f &&
                        plan.sceneTreeFraction <= 0.65f,
                    "neither half of the left column is ever squeezed out");
        }
    }

    std::cout << "EditorWorkspaceTests: ok\n";
    return 0;
}
