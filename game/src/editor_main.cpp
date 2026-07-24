// 3D level editor entry point. Owns only the engine lifecycle + main loop;
// all editor behaviour lives in editor::EditorApp.

#include "editor/EditorApp.h"

#include <eng/Engine.h>

#include <string>

int main(int, char**)
{
    eng::Engine engine;
    const std::string assets = APP_ASSET_DIR;
    if (!engine.init(assets + "/editor.toml", assets))
        return 1;

    editor::EditorApp app(engine, assets);

    while (!engine.shouldClose()) {
        const float dt = engine.tick();
        if (engine.input().wasPressed("quit") || app.wantsQuit())
            engine.requestClose();
        app.frame(dt);
        engine.renderFrame(dt);
    }
    engine.shutdown();
    return 0;
}
