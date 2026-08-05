#pragma once

#include <eng/acp/Exporter.h>

#include <memory>

// The diagram's "World Editor -> Game World" row, as an ACP exporter.
//
// The cooker itself is not new -- game::content::cookToMap has been the one and
// only .scn -> .map path since the editor and scene_cook were split. What is new
// is that it is now a stage of the pipeline rather than a thing a human
// remembers to run: a scene whose kit or prefab meshes changed is stale by
// build key, and `raven_acp build` cooks it without being told which scene.
//
// It lives in the editor tree because the scene format does. eng_acp must not
// link the .scn parser to condition a mesh, so the CLI registers this row on
// top of the built-ins instead.
namespace ed {

std::unique_ptr<eng::acp::Exporter> makeSceneWorldExporter();

} // namespace ed
