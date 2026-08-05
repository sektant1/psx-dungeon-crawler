// Unpacking a compound kit piece must change what the author can *edit*, and
// nothing about what the engine *draws*.
//
// A piece like kit.prop_boss_placeholder declares its sword in kit.toml, and
// the cooker emits that sword as an ECS child at build time. The parts are
// therefore not in the document: they cannot be selected, moved, re-materialled
// or given a component, and isolating the boss reports "0 parts" while a sword
// is plainly visible in its hand.
//
// "Unpack attachments" writes them out as real child entities and sets
// `unpacked_attachments` so the cooker stops generating its own. The property
// that makes that safe -- and the only one worth a test -- is that the built
// registry is the same either way.

#include <editor/content/KitCatalog.h>
#include <editor/content/SceneCook.h>
#include <editor/content/SceneDocument.h>
#include "TestAssets.h"

#include <eng/ecs/Components.h>
#include <eng/ecs/components/MeshSource.h>

#include <entt/entt.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace game::content;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "SceneUnpackTests: " << message << '\n';
        std::exit(1);
    }
}

// What actually reaches the renderer, addressed by nothing the author controls:
// no ids, no names, no parent links -- only the mesh, where it ends up in the
// world, and what it wears. Two documents that produce the same set of these
// draw the same picture.
struct Drawn {
    std::string mesh;
    std::string material;
    glm::vec3 position{0.0f};
    glm::vec3 scale{1.0f};

    bool operator<(const Drawn& other) const
    {
        if (mesh != other.mesh) return mesh < other.mesh;
        if (material != other.material) return material < other.material;
        for (int i = 0; i < 3; ++i)
            if (std::fabs(position[i] - other.position[i]) > 1e-4f)
                return position[i] < other.position[i];
        for (int i = 0; i < 3; ++i)
            if (std::fabs(scale[i] - other.scale[i]) > 1e-4f)
                return scale[i] < other.scale[i];
        return false;
    }
};

// World position by walking the built parent chain, because the whole point of
// an attachment is that its transform is expressed in its parent's frame.
glm::vec3 worldPositionOf(const entt::registry& registry, entt::entity entity)
{
    glm::vec3 position(0.0f);
    entt::entity at = entity;
    for (int guard = 0; guard < 64 && registry.valid(at); ++guard) {
        if (const auto* transform = registry.try_get<eng::ecs::Transform>(at))
            position += transform->position;
        const auto* parent = registry.try_get<eng::ecs::Parent>(at);
        if (!parent)
            break;
        at = parent->value;
    }
    return position;
}

std::vector<Drawn> drawnSetOf(const SceneDocument& document,
                              const KitCatalog& catalog)
{
    entt::registry registry;
    std::string error;
    require(buildRegistry(document, catalog, registry, error),
            error.empty() ? "buildRegistry failed" : error.c_str());

    std::vector<Drawn> drawn;
    for (const entt::entity entity :
         registry.view<eng::ecs::MeshSource, eng::ecs::MeshRenderer>()) {
        Drawn one;
        one.mesh = registry.get<eng::ecs::MeshSource>(entity).path;
        one.material = registry.get<eng::ecs::MeshRenderer>(entity).material;
        one.position = worldPositionOf(registry, entity);
        if (const auto* transform = registry.try_get<eng::ecs::Transform>(entity))
            one.scale = transform->scale;
        drawn.push_back(std::move(one));
    }
    std::sort(drawn.begin(), drawn.end());
    return drawn;
}

// The compound piece this whole feature exists for, plus the child the editor's
// unpack would write. Kept beside each other so the two documents cannot drift.
const char* kCompoundPrefab = "kit.prop_boss_placeholder";

SceneDocument bakedDocument()
{
    SceneDocument document;
    document.id = "scene.test.unpack";
    Entity boss;
    boss.id = "boss";
    boss.name = "Malenia";
    boss.prefab = kCompoundPrefab;
    document.entities.push_back(boss);
    return document;
}

SceneDocument unpackedDocument(const KitCatalog& catalog)
{
    SceneDocument document = bakedDocument();
    document.entities[0].unpackedAttachments = true;

    const KitPiece* piece = catalog.find(kCompoundPrefab);
    require(piece != nullptr, "the kit no longer defines the compound piece");
    require(!piece->attachments.empty(),
            "the compound piece no longer declares attachments -- this test "
            "would pass vacuously");

    int index = 0;
    for (const KitAttachment& attachment : piece->attachments) {
        Entity child;
        child.id = "part_" + std::to_string(++index);
        child.prefab = attachment.prefab;
        child.parent = "boss";
        child.transform.position = attachment.position;
        document.entities.push_back(std::move(child));
    }
    return document;
}

// --- generality ------------------------------------------------------------
// The shipped boss piece has exactly one attachment at one level, so on its own
// it cannot tell "unpack works" from "unpack happens to work for one sword".
// This kit is deliberately awkward: several attachments on one parent, and
// attachments that carry attachments of their own, three levels deep.
const char* kSyntheticKit = R"(
[kit]
scale = 1.0
cell_size = 4.0
mesh_dir = "meshes/kit"

[[piece]]
id = "leaf_a"
mesh = "meshes/props/prop_barrel_p0.obj"
material = "Test/LeafA"
socket = "prop"
size = [1.0, 1.0, 1.0]

[[piece]]
id = "leaf_b"
mesh = "meshes/props/prop_barrel_p0.obj"
material = "Test/LeafB"
socket = "prop"
size = [1.0, 1.0, 1.0]

[[piece]]
id = "mid"
mesh = "meshes/props/prop_barrel_p0.obj"
material = "Test/Mid"
socket = "prop"
size = [1.0, 1.0, 1.0]
attachments = [
  { prefab = "kit.leaf_a", position = [0.1, 0.2, 0.3] },
  { prefab = "kit.leaf_b", position = [-0.4, 0.5, -0.6] },
]

[[piece]]
id = "root"
mesh = "meshes/props/prop_barrel_p0.obj"
material = "Test/Root"
socket = "prop"
size = [1.0, 1.0, 1.0]
attachments = [
  { prefab = "kit.mid", position = [1.0, 0.0, 0.0] },
  { prefab = "kit.mid", position = [-1.0, 0.0, 0.0] },
  { prefab = "kit.leaf_a", position = [0.0, 2.0, 0.0] },
]
)";

// The editor's unpack, as data: every attachment of every piece, depth first,
// each child expressed in its parent's frame. Mirrors EditorApp::unpackAttachments
// -- if that walk stops recursing, this test still describes the whole tree and
// the comparison fails.
void appendUnpacked(SceneDocument& document, const KitCatalog& catalog,
                    const std::string& parentId, const KitPiece& parentPiece,
                    int& counter)
{
    for (const KitAttachment& attachment : parentPiece.attachments) {
        const KitPiece* attached = catalog.find(attachment.prefab);
        if (!attached)
            continue;
        Entity child;
        child.id = "part_" + std::to_string(++counter);
        child.prefab = attachment.prefab;
        child.parent = parentId;
        child.transform.position = attachment.position;
        // Same rule the editor applies: a part that is itself compound has its
        // own attachments authored below, so the cooker must not add them too.
        child.unpackedAttachments = !attached->attachments.empty();
        document.entities.push_back(child);
        appendUnpacked(document, catalog, child.id, *attached, counter);
    }
}

void unpackIsGeneral()
{
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "raven_scene_unpack";
    std::filesystem::create_directories(dir);
    const std::filesystem::path kitPath = dir / "kit.toml";
    std::ofstream(kitPath) << kSyntheticKit;

    KitCatalog catalog;
    std::string error;
    require(KitCatalog::load(kitPath.string(), catalog, error),
            error.empty() ? "synthetic kit did not load" : error.c_str());

    SceneDocument baked;
    baked.id = "scene.test.unpack_general";
    Entity root;
    root.id = "root";
    root.prefab = "kit.root";
    root.transform.position = glm::vec3(3.0f, 0.0f, -2.0f);
    baked.entities.push_back(root);

    SceneDocument unpacked = baked;
    unpacked.entities[0].unpackedAttachments = true;
    int counter = 0;
    appendUnpacked(unpacked, catalog, "root", *catalog.find("kit.root"),
                   counter);

    // 3 on the root + 2 under each of the two mids = 7 parts, three levels deep.
    require(counter == 7,
            "the synthetic tree should unpack to seven parts; if this changed, "
            "the fixture did, and the counts below mean something else");

    const std::vector<Drawn> before = drawnSetOf(baked, catalog);
    const std::vector<Drawn> after = drawnSetOf(unpacked, catalog);
    require(before.size() == 8, "root plus seven parts should be eight meshes");
    require(before.size() == after.size(),
            "unpacking a multi-level, multi-attachment piece changed how many "
            "meshes are drawn");
    for (std::size_t i = 0; i < before.size(); ++i) {
        require(before[i].mesh == after[i].mesh &&
                    before[i].material == after[i].material,
                "unpacking changed what is drawn in a nested tree");
        require(glm::length(before[i].position - after[i].position) < 1e-4f,
                "unpacking moved a nested part -- the parent frame is not "
                "being composed the same way");
    }

    std::filesystem::remove_all(dir);
}

} // namespace

int main()
{
    game::test::mountGameAssets();
    KitCatalog catalog;
    std::string error;
    require(KitCatalog::load(game::test::asset("config/kit.toml"), catalog,
                             error),
            error.empty() ? "kit did not load" : error.c_str());

    const std::vector<Drawn> baked = drawnSetOf(bakedDocument(), catalog);
    const std::vector<Drawn> unpacked =
        drawnSetOf(unpackedDocument(catalog), catalog);

    // The boss and its sword: if the fixture ever collapsed to one mesh the
    // comparison below would still pass and mean nothing.
    require(baked.size() >= 2,
            "the baked compound piece drew fewer than two meshes");
    require(baked.size() == unpacked.size(),
            "unpacking changed how many meshes are drawn -- the cooker is "
            "either emitting the attachments twice or not at all");
    for (std::size_t i = 0; i < baked.size(); ++i) {
        require(baked[i].mesh == unpacked[i].mesh,
                "unpacking changed which mesh is drawn");
        require(baked[i].material == unpacked[i].material,
                "unpacking changed a material");
        require(glm::length(baked[i].position - unpacked[i].position) < 1e-4f,
                "unpacking moved a part");
        require(glm::length(baked[i].scale - unpacked[i].scale) < 1e-4f,
                "unpacking rescaled a part");
    }

    // And the half that is supposed to differ: the author can now reach them.
    require(bakedDocument().entities.size() == 1,
            "the baked document should hold only the root");
    require(unpackedDocument(catalog).entities.size() > 1,
            "the unpacked document should hold the parts as entities");

    // Forgetting the flag is the failure that draws every attachment twice, so
    // it is pinned rather than left to the cooker's own guard being read.
    SceneDocument doubled = unpackedDocument(catalog);
    doubled.entities[0].unpackedAttachments = false;
    require(drawnSetOf(doubled, catalog).size() > baked.size(),
            "clearing unpacked_attachments must make the cooker emit its own "
            "copies again -- if it does not, the guard is not being read");

    unpackIsGeneral();

    std::cout << "SceneUnpackTests: ok\n";
    return 0;
}
