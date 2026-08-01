#pragma once

#include <eng/Log.h>
#include <eng/assets/AssetRoot.h>

#include <string>
#include <string_view>

// Logical asset paths, for the game.
//
// Before this, every game system carried an asset root around -- GameContext
// held one, LiveLevel took one as a parameter, Dummy read APP_ASSET_DIR
// directly -- and each of them concatenated its own strings on top
// (`assets + "/weapons.toml"`, `assets + "/meshes/props/"`). The root was a
// compile-time absolute source path, so the answer to "where is this file"
// was baked into the binary in ~15 places.
//
// Now a caller names the content and eng::assets walks the mount list. The
// paths are flat -- "config/enemies.toml", "meshes/props" -- because that is the
// shape of the game pack today; regrouping them under config/ is the file
// move's business, not this header's.
namespace game {

// The file, or a fatal error. An unresolved asset used to become an empty
// string handed to loadObj, which produces a silent empty mesh a long way from
// the cause; a missing shipped asset is a content bug and should say so.
inline std::string assetPath(std::string_view logical)
{
    const std::filesystem::path path = eng::assets::resolve(logical);
    if (path.empty())
        eng::log::fatal("assets: unresolved '%s'",
                        std::string(logical).c_str());
    return path.string();
}

// The same, for a directory, with a trailing separator so the callers that
// hand a prefix to a loader (`dir + "prop_crate.obj"`) keep reading the way
// they did.
inline std::string assetDir(std::string_view logical)
{
    return assetPath(logical) + "/";
}

} // namespace game
