#include <editor/content/SceneInstancing.h>

#include <editor/content/SceneSource.h>

#include <algorithm>
#include <filesystem>
#include <unordered_set>

namespace fs = std::filesystem;

namespace game::content {
namespace {

// Where a scene path resolves to on disk.
//
// An absolute path is taken as-is, which is what makes a test able to point at
// a temporary directory. Anything else is relative to the asset root, which is
// the project when one is open and the content pack otherwise -- the same rule
// every other authored path in this format follows.
fs::path resolveScene(const std::string& scene, const std::string& assetRoot)
{
    const fs::path named(scene);
    if (named.is_absolute())
        return named;
    if (assetRoot.empty())
        return named;
    return fs::path(assetRoot) / named;
}

// Canonical, so two spellings of one file are recognised as the same file.
// That is what makes cycle detection work at all: `scenes/a.scn` and
// `scenes/../scenes/a.scn` must not look like two different scenes.
std::string sceneKey(const fs::path& path)
{
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(path, ec);
    return ec ? path.lexically_normal().string() : canonical.string();
}

// "torch_a" + "flame" -> "torch_a/flame".
AuthorId namespaced(const AuthorId& placement, const AuthorId& inner)
{
    return placement + "/" + inner;
}

struct Expansion {
    const std::string& assetRoot;
    std::string& error;
    // The scenes currently being expanded, innermost last. Both the cycle test
    // and the message it produces come from this.
    std::vector<std::string> stack;

    bool cycleAt(const std::string& key, const std::string& scene,
                 const AuthorId& placement)
    {
        if (std::find(stack.begin(), stack.end(), key) == stack.end())
            return false;
        // Name the path, not just the fact. A cycle two scenes deep is
        // otherwise something an author has to reproduce to understand.
        std::string chain;
        for (const std::string& entry : stack)
            chain += fs::path(entry).filename().string() + " -> ";
        chain += fs::path(key).filename().string();
        error = "entity '" + placement + "' instances '" + scene +
                "', which instances itself: " + chain;
        return true;
    }

    // Expands `document` in place. `depth` counts nesting, not recursion.
    bool run(SceneDocument& document, int depth)
    {
        if (depth > kMaxInstanceDepth) {
            error = "scenes are instanced more than " +
                    std::to_string(kMaxInstanceDepth) +
                    " deep; this is almost always a mistake";
            return false;
        }

        // Collected first: expanding appends to document.entities, and walking
        // a vector while appending to it is undefined.
        std::vector<AuthorId> placements;
        for (const Entity& entity : document.entities)
            if (entity.instance)
                placements.push_back(entity.id);
        if (placements.empty())
            return true;

        for (const AuthorId& id : placements) {
            if (!expandOne(document, id, depth))
                return false;
        }
        return true;
    }

    bool expandOne(SceneDocument& document, const AuthorId& placementId,
                   int depth)
    {
        // Re-found by id each time: the previous expansion may have reallocated
        // the vector, so a pointer taken before the loop would dangle.
        Entity* placement = nullptr;
        for (Entity& entity : document.entities) {
            if (entity.id == placementId) {
                placement = &entity;
                break;
            }
        }
        if (!placement || !placement->instance)
            return true;

        const std::string scene = placement->instance->scene;
        const fs::path path = resolveScene(scene, assetRoot);
        const std::string key = sceneKey(path);

        if (cycleAt(key, scene, placementId))
            return false;

        std::error_code ec;
        if (!fs::is_regular_file(path, ec)) {
            error = "entity '" + placementId + "' instances '" + scene +
                    "', which is not a file (" + path.string() + ")";
            return false;
        }

        SceneDocument inner;
        std::string innerError;
        if (!loadSceneSource(path.string(), inner, innerError)) {
            error = "entity '" + placementId + "' instances '" + scene +
                    "': " + innerError;
            return false;
        }

        // Depth-first: the inner scene's own instances are expanded before its
        // entities are merged, so what arrives here is already flat.
        stack.push_back(key);
        const bool ok = run(inner, depth + 1);
        stack.pop_back();
        if (!ok)
            return false;

        merge(document, placementId, inner);
        return true;
    }

    // Copies `inner`'s entities in under the placement.
    static void merge(SceneDocument& document, const AuthorId& placementId,
                      const SceneDocument& inner)
    {
        // The placement stops being an instance and becomes an ordinary entity:
        // the node the contents hang from. Its transform is untouched, which is
        // what makes moving the placement move the whole thing -- the cooker
        // already resolves child transforms through the parent chain, so there
        // is no transform arithmetic to do here at all.
        for (Entity& entity : document.entities) {
            if (entity.id == placementId) {
                entity.instance.reset();
                break;
            }
        }

        // The layer the placement sits on, inherited by everything it brings
        // in: an author who hides the layer a torch is on means the flame too.
        std::string layer;
        for (const Entity& entity : document.entities)
            if (entity.id == placementId) { layer = entity.layer; break; }

        document.entities.reserve(document.entities.size() +
                                  inner.entities.size());
        for (const Entity& source : inner.entities) {
            Entity copy = source;
            copy.id = namespaced(placementId, source.id);
            // An inner root hangs from the placement; an inner child keeps its
            // own parent, namespaced to match. This is what turns two flat
            // documents into one tree.
            copy.parent = source.parent.empty()
                              ? placementId
                              : namespaced(placementId, source.parent);
            copy.layer = layer;
            document.entities.push_back(std::move(copy));
        }
    }
};

void collect(const SceneDocument& document, const std::string& assetRoot,
             std::unordered_set<std::string>& seen,
             std::vector<std::string>& out, int depth)
{
    if (depth > kMaxInstanceDepth)
        return;
    for (const Entity& entity : document.entities) {
        if (!entity.instance)
            continue;
        const std::string scene = entity.instance->scene;
        const fs::path path = resolveScene(scene, assetRoot);
        const std::string key = sceneKey(path);
        if (!seen.insert(key).second)
            continue; // already reported, or a cycle -- either way, stop
        out.push_back(scene);

        SceneDocument inner;
        std::string ignored;
        if (loadSceneSource(path.string(), inner, ignored))
            collect(inner, assetRoot, seen, out, depth + 1);
    }
}

} // namespace

bool expandInstances(SceneDocument& document, const std::string& assetRoot,
                     std::string& error)
{
    error.clear();

    // Expanded into a copy and swapped in only on success. A half-expanded
    // document is worse than an unexpanded one: the editor would show a scene
    // that is neither what is on disk nor what would ship.
    SceneDocument working = document;
    Expansion expansion{assetRoot, error, {}};
    if (!expansion.run(working, 0))
        return false;

    document = std::move(working);
    return true;
}

std::vector<std::string> instancedScenes(const SceneDocument& document,
                                         const std::string& assetRoot)
{
    std::unordered_set<std::string> seen;
    std::vector<std::string> out;
    collect(document, assetRoot, seen, out, 0);
    return out;
}

} // namespace game::content
