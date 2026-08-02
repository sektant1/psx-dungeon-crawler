// Picking and placement ask the same question -- what is under the cursor --
// and used to answer it with two copies of the same loop. These pin the shared
// one, including the tie-break and the face normal that placement needs and
// picking never did.

#include "DocumentRaycast.h"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace ed;
using namespace game::content;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "EditorDocumentRaycastTests: " << message << '\n';
        std::exit(1);
    }
}

static bool nearly(float a, float b, float tolerance = 1e-3f)
{
    return std::fabs(a - b) < tolerance;
}

// An entity with no prefab gets the one-metre fallback box centred on its
// position, which is all these tests need -- the catalogue is empty throughout,
// so every box has known bounds without loading kit.toml.
static Entity boxAt(const std::string& id, glm::vec3 position,
                    glm::vec3 scale = glm::vec3(1.0f))
{
    Entity entity;
    entity.id = id;
    entity.name = id;
    entity.transform.position = position;
    entity.transform.scale = scale;
    return entity;
}

static Ray downAt(float x, float z, float fromY = 20.0f)
{
    Ray ray;
    ray.origin = {x, fromY, z};
    ray.dir = {0.0f, -1.0f, 0.0f};
    return ray;
}

int main()
{
    const KitCatalog catalog; // empty: every entity uses the fallback box
    const EntityFilter all = [](const AuthorId&) { return true; };

    // --- a miss is a miss -------------------------------------------------
    {
        SceneDocument doc;
        doc.add(boxAt("a", {0.0f, 0.0f, 0.0f}));
        const DocumentHit hit = raycastDocument(doc, catalog, downAt(50.0f, 50.0f), all);
        require(!hit.valid, "a ray past everything reports no hit");
    }

    // --- the face the ray entered through ---------------------------------
    //
    // This is the whole reason the raycast was extracted: placement needs to
    // tell "on top of the crate" from "against its side".
    {
        SceneDocument doc;
        doc.add(boxAt("crate", {0.0f, 0.0f, 0.0f})); // box spans -0.5..0.5

        const DocumentHit top = raycastDocument(doc, catalog, downAt(0.0f, 0.0f), all);
        require(top.valid, "straight down onto the crate hits it");
        require(nearly(top.normal.y, 1.0f), "and enters through the top face");
        require(nearly(top.point.y, 0.5f), "at the top of the box");
        require(nearly(top.boundsMax.y, 0.5f), "reporting the box it hit");

        Ray east;
        east.origin = {10.0f, 0.0f, 0.0f};
        east.dir = {-1.0f, 0.0f, 0.0f};
        const DocumentHit side = raycastDocument(doc, catalog, east, all);
        require(side.valid, "and from the side too");
        require(nearly(side.normal.x, 1.0f),
                "entering through the +X face, whose outward normal is +X");
        require(nearly(side.point.x, 0.5f), "at the box's +X wall");
    }

    // --- nearest wins, and ties go to the smaller box ----------------------
    {
        SceneDocument doc;
        // A big room-sized box and a small barrel sharing a top face, so the
        // two hits arrive at the same t and only the volume separates them.
        doc.add(boxAt("room", {0.0f, 0.0f, 0.0f}, glm::vec3(10.0f, 1.0f, 10.0f)));
        doc.add(boxAt("barrel", {0.0f, 0.0f, 0.0f}, glm::vec3(1.0f, 1.0f, 1.0f)));
        const DocumentHit hit = raycastDocument(doc, catalog, downAt(0.0f, 0.0f), all);
        require(hit.valid, "the stack is hit");
        require(hit.id == "barrel",
                "and the smaller box wins the tie, so clicking a barrel inside "
                "a room does not select the room");
    }
    {
        SceneDocument doc;
        doc.add(boxAt("low", {0.0f, 0.0f, 0.0f}));
        doc.add(boxAt("high", {0.0f, 5.0f, 0.0f}));
        const DocumentHit hit = raycastDocument(doc, catalog, downAt(0.0f, 0.0f), all);
        require(hit.id == "high", "a ray from above hits the nearer box first");
        require(nearly(hit.point.y, 5.5f), "at that box's top");
    }

    // --- the filter is honoured -------------------------------------------
    //
    // Hidden, locked, and -- new with the erase stroke -- the entities the
    // running stroke has already laid down, all arrive through this one hook.
    {
        SceneDocument doc;
        doc.add(boxAt("low", {0.0f, 0.0f, 0.0f}));
        doc.add(boxAt("high", {0.0f, 5.0f, 0.0f}));
        const DocumentHit hit = raycastDocument(
            doc, catalog, downAt(0.0f, 0.0f),
            [](const AuthorId& id) { return id != "high"; });
        require(hit.valid && hit.id == "low",
                "excluding the nearer box falls through to the one behind it");
    }

    // --- a ray starting inside a box ---------------------------------------
    {
        SceneDocument doc;
        doc.add(boxAt("around", {0.0f, 0.0f, 0.0f}, glm::vec3(10.0f)));
        Ray inside;
        inside.origin = {0.0f, 0.0f, 0.0f};
        inside.dir = {0.0f, -1.0f, 0.0f};
        const DocumentHit hit = raycastDocument(doc, catalog, inside, all);
        require(hit.valid, "a ray inside a box still reports it");
        require(nearly(hit.normal.y, 1.0f),
                "with the +Y fallback normal, there being no entry face");
    }

    std::cout << "EditorDocumentRaycastTests: ok\n";
    return 0;
}
