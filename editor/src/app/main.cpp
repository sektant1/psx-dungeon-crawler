// scene_editor -- the placement editor, the engine's second consumer.
//
//   scene_editor [scene.scn]
//
// Opens the shipped showroom scene when given no argument.

#include <editor/app/EditorApp.h>

int main(int argc, char** argv)
{
    ed::EditorApp app;
    return eng::runApplication(app, argc, argv);
}
