// scene_cook -- the one and only .scn -> .map cooker.
//
// The editor calls game::content::cookToMap directly; this CLI is the same call
// with argument parsing around it, which is what lets CI and the editor produce
// byte-identical maps (the cook_parity test asserts exactly that).

#include <editor/content/SceneCook.h>
#include <editor/content/SceneSource.h>
#include <editor/content/SceneRepair.h>
#include <editor/content/SceneTemplates.h>
#include <editor/content/SceneValidate.h>
#include <editor/content/SceneWriter.h>

#include <eng/assets/AssetRoot.h>

#include <cstdio>
#include <filesystem>
#include <cstring>
#include <string>

namespace {

int usage()
{
    std::fprintf(stderr,
                 "usage: scene_cook <source.scn> --kit <kit.toml>\n"
                 "         [--out <output.map>]     cook to a runtime map\n"
                 "         [--validate-only]        parse and resolve, write nothing\n"
                 "         [--repair]               --repair-cells, plus "
                 "every quick fix that is safe unattended\n"
                 "         [--repair-cells]         rebase drifted `cell` "
                 "records onto where pieces actually are (never moves one)\n"
                 "         [--rewrite <out.scn>]    re-emit canonical .scn "
                 "(formatting, v1 -> v2)\n"
                 "         [--kit/--assets]         both default to the game "
                 "content pack; pass them to cook a tree that is not it\n"
                 "\n"
                 "  scene_cook --template <empty|room|techdemo> --kit <kit.toml>\n"
                 "             --rewrite <out.scn> [--id <scene.id>]\n"
                 "         generate a starting scene (the editor's New menu, "
                 "from the shell)\n");
    return 2;
}

} // namespace

int main(int argc, char** argv)
{
    // The cooker mounts the game content set like every other consumer, so
    // --kit and --assets become overrides rather than requirements. They used
    // to be neither: --kit was mandatory and --assets was *inferred from it*
    // by taking the kit file's parent directory, which silently encoded "the
    // kit lives at the root of the asset tree".
    const bool mounted = eng::assets::init() && eng::assets::mount("game");

    std::string source, kit, out, rewrite, assets, templateName, sceneId;
    bool validateOnly = false;
    bool repairCells = false;
    bool repairIssues = false;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        const auto value = [&](std::string& target) {
            if (i + 1 >= argc) return false;
            target = argv[++i];
            return true;
        };
        if (std::strcmp(arg, "--kit") == 0) {
            if (!value(kit)) return usage();
        } else if (std::strcmp(arg, "--out") == 0) {
            if (!value(out)) return usage();
        } else if (std::strcmp(arg, "--template") == 0) {
            if (!value(templateName)) return usage();
        } else if (std::strcmp(arg, "--id") == 0) {
            if (!value(sceneId)) return usage();
        } else if (std::strcmp(arg, "--assets") == 0) {
            if (!value(assets)) return usage();
        } else if (std::strcmp(arg, "--rewrite") == 0) {
            if (!value(rewrite)) return usage();
        } else if (std::strcmp(arg, "--repair-cells") == 0) {
            repairCells = true;
        } else if (std::strcmp(arg, "--repair") == 0) {
            repairCells = true;
            repairIssues = true;
        } else if (std::strcmp(arg, "--validate-only") == 0) {
            validateOnly = true;
        } else if (arg[0] == '-') {
            return usage();
        } else if (source.empty()) {
            source = arg;
        } else {
            return usage();
        }
    }
    if (kit.empty() && mounted)
        kit = eng::assets::resolve("config/kit.toml").string();
    // The mesh-on-disk check needs a directory to join pack-relative mesh
    // paths onto. That is the game pack itself; the old heuristic only
    // happened to agree because kit.toml sits at the tree's root.
    if (assets.empty() && mounted)
        assets = eng::assets::packDir("content").string();

    if (kit.empty() || (source.empty() && templateName.empty()) ||
        (out.empty() && rewrite.empty() && !validateOnly))
        return usage();

    std::string error;
    game::content::KitCatalog catalog;
    if (!game::content::KitCatalog::load(kit, catalog, error)) {
        std::fprintf(stderr, "scene_cook: %s\n", error.c_str());
        return 1;
    }

    game::content::SceneDocument document;
    if (!templateName.empty()) {
        // Generating rather than loading: this is how the shipped starter
        // scenes are produced, so they cannot drift from what the editor's own
        // New menu builds.
        game::content::SceneTemplate which = game::content::SceneTemplate::Empty;
        if (templateName == "room")
            which = game::content::SceneTemplate::Room;
        else if (templateName == "techdemo")
            which = game::content::SceneTemplate::TechDemo;
        else if (templateName != "empty")
            return usage();
        const game::content::GridConfig grid =
            game::content::GridConfig::fromCatalog(catalog);
        if (!game::content::buildTemplate(which, grid, catalog,
                                          sceneId.empty() ? "scene.untitled"
                                                          : sceneId,
                                          document, error)) {
            std::fprintf(stderr, "scene_cook: %s\n", error.c_str());
            return 1;
        }
        source = templateName;
    } else if (!game::content::loadSceneSource(source, document, error)) {
        std::fprintf(stderr, "scene_cook: %s\n", error.c_str());
        return 1;
    }

    // Before the rewrite and before validation, so one invocation can repair a
    // scene, write it back and report what is left.
    if (repairCells) {
        const game::content::CellRepairReport repaired =
            game::content::repairCellRecords(
                document, catalog,
                game::content::GridConfig::fromCatalog(catalog));
        std::printf("scene_cook: cells -- %zu rebased, %zu detached, "
                    "%zu already correct\n",
                    repaired.rebased, repaired.detached, repaired.untouched);
    }

    if (repairIssues) {
        const game::content::SafeFixReport fixed =
            game::content::applySafeQuickFixes(document, catalog, assets);
        std::printf("scene_cook: issues -- %zu fixed, %zu left for a human\n",
                    fixed.applied, fixed.remaining + fixed.skipped);
    }

    if (!rewrite.empty()) {
        if (!game::content::writeSceneSource(rewrite, document, error)) {
            std::fprintf(stderr, "scene_cook: %s\n", error.c_str());
            return 1;
        }
        std::printf("scene_cook: rewrote %s -> %s\n", source.c_str(),
                    rewrite.c_str());
        if (out.empty() && !validateOnly)
            return 0;
    }

    // One validation pass for both modes: the CLI must refuse exactly what the
    // editor refuses, or "it cooked on my machine" starts happening.
    const std::vector<game::content::Issue> issues =
        game::content::validate(document, catalog, assets);
    for (const game::content::Issue& issue : issues) {
        std::fprintf(issue.severity == game::content::Severity::Error ? stderr
                                                                      : stdout,
                     "%s: %s: %s%s%s\n",
                     game::content::severityName(issue.severity),
                     issue.code.c_str(), issue.message.c_str(),
                     issue.entity.empty() ? "" : " -- entity ",
                     issue.entity.c_str());
    }
    if (game::content::blocksCook(issues)) {
        std::fprintf(stderr, "scene_cook: %s has blocking issues\n",
                     source.c_str());
        return 1;
    }

    if (validateOnly) {
        std::printf("scene_cook: %s is valid (%zu entities, %zu warnings)\n",
                    source.c_str(), document.entities.size(), issues.size());
        return 0;
    }

    if (!game::content::cookToMap(document, catalog, out, error)) {
        std::fprintf(stderr, "scene_cook: %s\n", error.c_str());
        return 1;
    }
    std::printf("scene_cook: %s -> %s (%zu entities)\n", source.c_str(),
                out.c_str(), document.entities.size());
    return 0;
}
