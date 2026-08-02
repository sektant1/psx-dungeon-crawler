#pragma once
#include <editor/content/KitCatalog.h>
#include <editor/content/SceneDocument.h>

#include <string>

namespace game::content {

struct GridConfig;

// Starting points for a new scene.
//
// These are built by the same RoomBuilder the editor's Room tool calls, on
// purpose: the shipped demo cannot drift away from what the tool produces,
// because it IS what the tool produces. A template that was hand-authored
// instead would slowly stop matching the thing it is supposed to demonstrate.
enum class SceneTemplate {
    Empty,    // a spawn and an exit, nothing else
    Room,     // one lit room, the smallest playable thing
    TechDemo, // two rooms and a corridor, exercising every authored feature
};

const char* sceneTemplateName(SceneTemplate which);

// Replaces `out` with a fresh scene. `id` becomes the document id.
bool buildTemplate(SceneTemplate which, const GridConfig& grid,
                   const KitCatalog& catalog, const std::string& id,
                   SceneDocument& out, std::string& error);

} // namespace game::content
