// The design rule this test enforces: there is exactly ONE cooker. The editor
// calls game::content::cookToMap in-process and CI runs the scene_cook binary,
// and the two must produce identical bytes -- otherwise "it works in the
// editor" and "it works in the build" drift apart, which is the failure mode
// the shared-content-library boundary exists to prevent.
//
// It also pins determinism: cooking the same scene twice must not vary.

#include "SceneCook.h"
#include "SceneSource.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace game::content;

static void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "CookParityTests: " << message << '\n';
        std::exit(1);
    }
}

static std::vector<char> readAll(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    require(bool(input), ("cannot read " + path).c_str());
    return std::vector<char>((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
}

int main()
{
    const std::string outDir = COOK_PARITY_OUT_DIR;
    const std::string inProcess = outDir + "/parity_inprocess.map";
    const std::string viaCli = outDir + "/parity_cli.map";
    const std::string again = outDir + "/parity_again.map";

    SceneDocument document;
    KitCatalog catalog;
    std::string error;
    require(loadSceneSource(RITUAL_SCN, document, error), error.c_str());
    require(KitCatalog::load(KIT_TOML, catalog, error), error.c_str());

    require(cookToMap(document, catalog, inProcess, error), error.c_str());
    require(cookToMap(document, catalog, again, error), error.c_str());
    require(readAll(inProcess) == readAll(again),
            "cooking the same scene twice gives the same bytes");

    const std::string command = std::string(SCENE_COOK_EXE) + " " + RITUAL_SCN +
                                " --kit " + KIT_TOML + " --out " + viaCli +
                                " > /dev/null";
    require(std::system(command.c_str()) == 0, "scene_cook CLI succeeds");
    require(readAll(inProcess) == readAll(viaCli),
            "the CLI and the in-process cook produce identical maps");

    // A scene naming a prefab the kit does not have must be refused, not
    // silently cooked with a hole in it.
    SceneDocument broken = document;
    Entity ghost;
    ghost.id = "zzz_ghost";
    ghost.prefab = "kit.not_a_real_piece";
    broken.add(ghost);
    require(!cookToMap(broken, catalog, outDir + "/parity_broken.map", error),
            "an unresolved prefab blocks the cook");
    require(error.find("unresolved") != std::string::npos,
            "and the error names the problem");

    std::cout << "CookParityTests: ok\n";
    return 0;
}
