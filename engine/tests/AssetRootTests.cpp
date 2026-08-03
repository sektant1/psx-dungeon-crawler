// The content root, its manifest, and mount-order resolution.
//
// Everything here is pure logic over the filesystem, so the fixture is a
// throwaway tree under the temp dir rather than the repo's real assets: what
// is being tested is *which* pack answers a logical path, and that answer must
// not change the day somebody adds or deletes a real file.
#include <eng/assets/AssetRoot.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "AssetRootTests: " << message << '\n';
        std::exit(1);
    }
}

void writeFile(const fs::path& path, const std::string& body)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    out << body;
}

// Two packs with one colliding logical path ("config/shared.toml") and one
// unique path each, plus three mount sets: the two orderings and one naming a
// pack that does not exist.
fs::path buildFixture(const fs::path& base)
{
    std::error_code ec;
    fs::remove_all(base, ec);

    const fs::path root = base / "assets";
    writeFile(root / "assets.toml", R"(
schema = 1

[[pack]]
id = "engine"
dir = "../engine_tree"
resources = ["materials", "shaders"]

[[pack]]
id = "game"
dir = "../game_tree"
resources = ["materials"]

[mounts]
game        = ["game", "engine"]
engine_only = ["engine"]
inverted    = ["engine", "game"]
broken      = ["game", "nosuchpack"]
)");

    for (const char* tree : {"engine_tree", "game_tree"}) {
        writeFile(base / tree / "config/shared.toml",
                  std::string("owner = \"") + tree + "\"\n");
        fs::create_directories(base / tree / "materials");
    }
    fs::create_directories(base / "engine_tree" / "shaders");
    writeFile(base / "engine_tree" / "shaders/psx.frag", "// engine only\n");
    writeFile(base / "game_tree" / "config/enemies.toml", "# game only\n");
    return root;
}

std::string readOwner(const fs::path& path)
{
    std::ifstream in(path);
    std::string body((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    return body;
}

bool contains(const std::string& haystack, const char* needle)
{
    return haystack.find(needle) != std::string::npos;
}

} // namespace

int main()
{
    const fs::path base = fs::temp_directory_path() / "eng_asset_root_tests";
    const fs::path root = buildFixture(base);

    // --- root discovery --------------------------------------------------
    // A directory with no manifest is not a content root, and saying so must
    // be a return value: this runs before any window exists, and the fallback
    // chain depends on being able to try the next candidate.
    const fs::path empty = base / "empty";
    fs::create_directories(empty);
    require(!eng::assets::init(empty.string()),
            "a root without assets.toml must fail, not throw");

    setenv("RAVEN_ASSET_ROOT", root.c_str(), 1);
    require(eng::assets::init(), "RAVEN_ASSET_ROOT names the content root");
    require(eng::assets::root() == root, "root() is the env root");

    // project() is what replaces the assetRoot + "/../.." climb: in a dev
    // build it is the directory holding the content root.
    require(eng::assets::project() == base,
            "project() is the parent of a dev content root");

    // An explicit override outranks the environment -- tools pass one.
    const fs::path decoy = base / "decoy";
    fs::create_directories(decoy);
    require(!eng::assets::init(decoy.string()),
            "an override that is not a content root fails, env is not a "
            "silent fallback");

    require(eng::assets::init(root.string()), "explicit override");

    // --- mounting --------------------------------------------------------
    require(!eng::assets::mount("nosuchset"),
            "an unknown mount set fails cleanly");
    require(eng::assets::mounted().empty(),
            "a failed mount leaves nothing mounted");
    require(!eng::assets::mount("broken"),
            "a mount set naming an unknown pack id fails");
    require(eng::assets::mounted().empty(),
            "a partially resolvable mount set mounts nothing");

    require(eng::assets::mount("game"), "the game mount set");
    require(eng::assets::mounted().size() == 2, "game mounts two packs");
    require(eng::assets::mounted()[0].id == "game" &&
                eng::assets::mounted()[1].id == "engine",
            "mounted() is in priority order, highest first");

    // --- resolution ------------------------------------------------------
    require(contains(readOwner(eng::assets::resolve("config/shared.toml")),
                     "game_tree"),
            "the highest-priority mount wins a duplicated logical path");
    require(eng::assets::resolve("shaders/psx.frag") ==
                base / "engine_tree" / "shaders/psx.frag",
            "a lower mount answers what the higher one does not have");
    require(eng::assets::exists("config/enemies.toml"),
            "exists() agrees with resolve()");

    require(eng::assets::resolve("config/missing.toml").empty(),
            "an unresolved path is empty, not a guess");
    require(!eng::assets::exists("config/missing.toml"),
            "exists() is false for an unresolved path");

    // Pack-qualified: the first segment names a mounted pack and pins it,
    // which is how a caller reaches past a higher-priority override.
    require(
        contains(readOwner(eng::assets::resolve("engine/config/shared.toml")),
                 "engine_tree"),
        "a pack-qualified path pins that pack");
    require(eng::assets::resolve("engine/config/enemies.toml").empty(),
            "a pack-qualified path does not fall through to other packs");
    require(eng::assets::resolve("nosuchpack/config/shared.toml").empty(),
            "an unmounted first segment is not a pack qualifier");

    // No climbing out of the content root through a logical path.
    require(eng::assets::resolve("../assets.toml").empty(),
            "a logical path may not escape its pack");

    // --- resource dirs ---------------------------------------------------
    {
        const std::vector<fs::path> dirs = eng::assets::resourceDirs();
        require(dirs.size() == 3,
                "resourceDirs() lists every declared, existing dir");
        require(dirs[0] == base / "game_tree" / "materials",
                "resourceDirs() follows mount priority");
        require(dirs[1] == base / "engine_tree" / "materials",
                "then the next pack, in the pack's declared order");
        require(dirs[2] == base / "engine_tree" / "shaders",
                "a pack's own resources keep their manifest order");
    }

    // --- mount order is the only thing that decides a collision ----------
    require(eng::assets::mount("inverted"), "the inverted mount set");
    require(contains(readOwner(eng::assets::resolve("config/shared.toml")),
                     "engine_tree"),
            "reversing the mount order reverses which pack wins");
    require(eng::assets::resourceDirs()[0] ==
                base / "engine_tree" / "materials",
            "resourceDirs() follows the new order too");

    // mount() replaces, it does not accumulate.
    require(eng::assets::mount("engine_only"), "a single-pack mount set");
    require(eng::assets::mounted().size() == 1, "mount() replaces the set");
    require(eng::assets::resolve("config/enemies.toml").empty(),
            "an unmounted pack answers nothing");

    // --- a malformed manifest ---------------------------------------------
    // A pack with no id is a manifest bug; it must be reported at init rather
    // than becoming a pack that nothing can name.
    writeFile(root / "assets.toml", R"(
schema = 1

[[pack]]
dir = "../engine_tree"

[mounts]
game = ["engine"]
)");
    require(!eng::assets::init(root.string()),
            "a pack without an id fails init");
    require(eng::assets::mounted().empty(),
            "a failed init leaves no stale mounts");

    writeFile(root / "assets.toml", "schema = 1\nthis is not toml\n");
    require(!eng::assets::init(root.string()),
            "an unparseable manifest fails init");

    // A pack whose dir is missing is the same class of bug.
    writeFile(root / "assets.toml", R"(
schema = 1

[[pack]]
id = "engine"
dir = "../nowhere"

[mounts]
game = ["engine"]
)");
    require(!eng::assets::init(root.string()),
            "a pack pointing at a missing directory fails init");

    std::error_code ec;
    fs::remove_all(base, ec);
    std::cout << "AssetRootTests OK\n";
    return 0;
}
