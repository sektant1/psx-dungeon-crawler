#pragma once
#include "SceneDocument.h"

#include <string>

namespace game::content {

// Reads an authored .scn (JSON) into the IR. Structural validation only:
// shapes, types and finiteness of what the file says. Semantic checks that need
// the kit or the rest of the scene (unresolved prefab, missing spawn, two solid
// pieces in one cell) belong to SceneValidate, so that a broken scene still
// *opens* in the editor and shows its problems instead of refusing to load.
//
// `error` carries a `path:/entities/3/transform`-style location on failure.
bool loadSceneSource(const std::string& sourcePath, SceneDocument& out,
                     std::string& error);

// Same, from a string already in memory (tests, and eventually undo of a
// reload).
bool parseSceneSource(const std::string& json, const std::string& location,
                      SceneDocument& out, std::string& error);

} // namespace game::content
