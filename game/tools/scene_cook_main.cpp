// scene_cook -- the one and only .scn -> .map cooker.
//
// The editor calls game::content::cookToMap directly; this CLI is the same call
// with argument parsing around it, which is what lets CI and the editor produce
// byte-identical maps (the cook_parity test asserts exactly that).

#include "SceneCook.h"
#include "SceneSource.h"
#include "SceneTemplates.h"
#include "SceneValidate.h"
#include "SceneWriter.h"

#include <cstdio>
#include <filesystem>
#include <cstring>
#include <string>

namespace {

// Where kit mesh paths resolve from. Defaults to the kit.toml's own directory,
// which is where mesh_dir is relative to -- deriving it from the scene path
// instead breaks the moment a scene lives anywhere but assets/scenes.
std::string assetRootFor(const std::string& kitPath)
{
    return std::filesystem::path(kitPath).parent_path().string();
}

int usage()
{
    std::fprintf(stderr,
                 "usage: scene_cook <source.scn> --kit <kit.toml>\n"
                 "         [--out <output.map>]     cook to a runtime map\n"
                 "         [--validate-only]        parse and resolve, write nothing\n"
                 "         [--rewrite <out.scn>]    re-emit canonical .scn "
                 "(formatting, v1 -> v2)\n"
                 "         [--assets <dir>]         asset root for the "
                 "mesh-on-disk check (default: the kit's directory)\n"
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
    std::string source, kit, out, rewrite, assets, templateName, sceneId;
    bool validateOnly = false;
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
        game::content::validate(document, catalog,
                                assets.empty() ? assetRootFor(kit) : assets);
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
