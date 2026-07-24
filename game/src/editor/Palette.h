#pragma once

#include <functional>
#include <string>
#include <vector>

namespace editor {

// Asset palette: discovers .obj meshes under the asset tree and draws a
// spawn-button window. All spawning is delegated to caller-supplied callbacks
// so the palette holds no scene/registry state.
class Palette {
public:
    struct Callbacks {
        std::function<void(const std::string&)> spawnMesh;
        std::function<void()> spawnLight;
        std::function<void()> spawnPlayerSpawn;
        std::function<void()> spawnExit;
        std::function<void()> spawnEnemy;
        std::function<void()> spawnPickup;
        std::function<void()> spawnTrigger;
    };

    // Scan <assetDir>/meshes/props and <assetDir>/meshes/tiles for .obj files.
    void discover(const std::string& assetDir);

    // ImGui window "Palette": one button per mesh (label = filename stem) then
    // a separator and the six marker buttons.
    void draw(const Callbacks& cb) const;

    const std::vector<std::string>& meshes() const { return mMeshes; }

private:
    std::vector<std::string> mMeshes; // absolute .obj paths, sorted
};

} // namespace editor
