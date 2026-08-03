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
        require(near(plan.diagnosticsPixels, 132.0f),
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
        require(plan.commandBarPixels <= 96.0f,
                "large text keeps the command bar bounded");
    }

    std::cout << "EditorWorkspaceTests: ok\n";
    return 0;
}
