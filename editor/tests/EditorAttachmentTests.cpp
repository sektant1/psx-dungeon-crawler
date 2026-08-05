// Compound pieces, unpacked into the document.
//
// A model made of parts -- the boss placeholder and its sword, an imported
// model and its submeshes -- used to exist only inside the cooker: the parts
// were generated at build time, drawn in the viewport, and absent from the
// hierarchy, so the sword in the boss's hand could not be selected, moved or
// re-materialled. This is the expansion that puts them in the document, and the
// two properties it has to keep: the parts land where the cooker would have put
// them, and nothing is expanded twice.

#include <editor/scene/Attachments.h>
#include "TestAssets.h"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace ed;
using namespace game::content;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "EditorAttachmentTests: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    game::test::mountGameAssets();
    KitCatalog catalog;
    std::string error;
    require(KitCatalog::load(game::test::asset("config/kit.toml"), catalog,
                             error),
            error);

    const KitPiece* boss = catalog.find("kit.prop_boss_placeholder");
    require(boss && boss->attachments.size() == 1,
            "the boss placeholder is still the repository's compound piece");

    SceneDocument doc;
    Entity root;
    root.id = "boss_0001";
    root.prefab = "kit.prop_boss_placeholder";
    root.castShadows = true;
    doc.add(root);

    const std::vector<Entity> parts =
        buildAttachmentEntities(catalog, doc, doc.entities.front());
    require(parts.size() == 1, "the sword comes out as one entity");
    const Entity& sword = parts.front();
    require(sword.prefab == boss->attachments.front().prefab,
            "carrying the attachment's prefab");
    require(sword.parent == root.id,
            "parented to the piece it hangs off, so moving the boss moves it");
    require(sword.transform.position == boss->attachments.front().position,
            "at the attachment's own offset -- the cooker expresses it in the "
            "parent's frame, which is exactly what an authored child transform "
            "is, so this is a copy and not a conversion");
    require(sword.castShadows == root.castShadows,
            "and inherits the root's shadow choice");
    require(doc.find(sword.id) != nullptr,
            "the document holds it, so the outliner can draw a row for it");

    // --- ids do not collide ------------------------------------------------
    {
        Entity second;
        second.id = "boss_0002";
        second.prefab = "kit.prop_boss_placeholder";
        doc.add(second);
        const std::vector<Entity> more =
            buildAttachmentEntities(catalog, doc, *doc.find("boss_0002"));
        require(more.size() == 1, "the second boss also gets a sword");
        require(more.front().id != sword.id,
                "with an id of its own: ids are allocated against a document "
                "that already holds the ones handed out");
    }

    // --- nothing to unpack -------------------------------------------------
    {
        Entity wall;
        wall.id = "wall_0001";
        wall.prefab = "kit.wall";
        doc.add(wall);
        require(buildAttachmentEntities(catalog, doc, *doc.find("wall_0001"))
                    .empty(),
                "a piece with no parts expands to nothing");
        require(!hasPackedAttachments(catalog, *doc.find("wall_0001")),
                "and reports that there is nothing to unpack");
    }

    // --- expanded once, and only once --------------------------------------
    //
    // The flag is what stops the cooker generating a second copy underneath the
    // one the document now holds. A boss with two swords in the same hand is
    // the failure this guards.
    {
        Entity packed;
        packed.id = "boss_0003";
        packed.prefab = "kit.prop_boss_placeholder";
        require(hasPackedAttachments(catalog, packed),
                "a freshly placed compound piece has parts to unpack");
        packed.unpackedAttachments = true;
        require(!hasPackedAttachments(catalog, packed),
                "and none once it has been");
    }

    std::cout << "EditorAttachmentTests: ok\n";
    return 0;
}
