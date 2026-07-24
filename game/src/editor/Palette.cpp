#include "Palette.h"

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace editor {

void Palette::discover(const std::string& assetDir)
{
    mMeshes.clear();
    const auto scan = [&](const std::string& dir) {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) return;
        for (const auto& e : std::filesystem::directory_iterator(dir, ec))
            if (e.path().extension() == ".obj")
                mMeshes.push_back(e.path().string());
    };
    scan(assetDir + "/meshes/props");
    scan(assetDir + "/meshes/tiles");
    std::sort(mMeshes.begin(), mMeshes.end());
}

void Palette::draw(const Callbacks& cb) const
{
    ImGui::Begin("Palette");

    ImGui::TextUnformatted("Meshes");
    for (const auto& path : mMeshes) {
        const std::string label = std::filesystem::path(path).stem().string();
        if (ImGui::Button(label.c_str()) && cb.spawnMesh)
            cb.spawnMesh(path);
    }
    if (mMeshes.empty())
        ImGui::TextDisabled("(no .obj meshes found)");

    ImGui::Separator();
    ImGui::TextUnformatted("Markers");
    if (ImGui::Button("Light") && cb.spawnLight) cb.spawnLight();
    if (ImGui::Button("Player Spawn") && cb.spawnPlayerSpawn) cb.spawnPlayerSpawn();
    if (ImGui::Button("Exit") && cb.spawnExit) cb.spawnExit();
    if (ImGui::Button("Enemy") && cb.spawnEnemy) cb.spawnEnemy();
    if (ImGui::Button("Pickup") && cb.spawnPickup) cb.spawnPickup();
    if (ImGui::Button("Trigger") && cb.spawnTrigger) cb.spawnTrigger();

    ImGui::End();
}

} // namespace editor
