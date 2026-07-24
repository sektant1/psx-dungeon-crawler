#pragma once
#include <string>
namespace eng { class Engine; class Physics; }
namespace game {
int playMap(eng::Engine& engine, eng::Physics& physics,
            const std::string& assetDir, const std::string& mapPath);
}
