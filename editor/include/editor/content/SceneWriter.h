#pragma once
#include <editor/content/SceneDocument.h>

#include <string>

namespace game::content {

// Serializes the IR back to .scn JSON, canonically: fixed key order, entities
// sorted by author id, default-valued fields omitted, floats rounded to 4
// decimals with -0 normalised.
//
// Canonical is not cosmetic. This file is committed to git and reviewed, so a
// gizmo drag must produce a three-line diff, and re-saving an untouched scene
// must produce no diff at all. Both are tested.
std::string serializeSceneSource(const SceneDocument& document);

// Writes atomically (temp file + rename), like eng::ecs::writeMap, so an
// interrupted save cannot leave a half-written scene on disk. Rejects absolute
// paths inside the document -- those are what made the old editor's maps
// non-portable.
bool writeSceneSource(const std::string& path, const SceneDocument& document,
                      std::string& error);

} // namespace game::content
