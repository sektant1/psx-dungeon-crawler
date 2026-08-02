// Copying entities: fresh ids, and an offset that is visible in the game and
// not only in the viewport.

#include <editor/commands/Clipboard.h>

#include <cstdlib>
#include <iostream>
#include <string>

using namespace ed;
using game::content::CellPlacement;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "EditorClipboardTests: " << message << '\n';
        std::exit(1);
    }
}

static Entity wall(const AuthorId& id, int col, int row)
{
    Entity entity;
    entity.id = id;
    entity.prefab = "kit.wall";
    entity.transform.position = glm::vec3(float(col) * 4.0f, 0.0f, float(row) * 4.0f);
    CellPlacement cell;
    cell.col = col;
    cell.row = row;
    entity.cell = cell;
    return entity;
}

int main()
{
    Doc document;
    document.add(wall("kit.wall_0001", 0, 0));
    document.add(wall("kit.wall_0002", 1, 0));

    Entity torch;
    torch.id = "torch_0001";
    torch.transform.position = glm::vec3(2.0f, 1.5f, 0.0f);
    document.add(torch);

    // --- what is collected is what was asked for ----------------------------
    {
        const std::vector<Entity> picked =
            collectEntities(document, {"kit.wall_0002", "torch_0001", "gone"});
        require(picked.size() == 2, "an id the document lost is skipped");
        require(picked[0].id == "kit.wall_0002" && picked[1].id == "torch_0001",
                "and the order asked for is kept");
    }

    // --- copies never reuse an id -------------------------------------------
    {
        const std::vector<Entity> source = collectEntities(
            document, {"kit.wall_0001", "kit.wall_0002", "torch_0001"});
        const std::vector<Entity> copies =
            offsetCopies(document, source, 4.0f, 1);
        require(copies.size() == 3, "every entity is copied");
        for (const Entity& copy : copies)
            require(!document.contains(copy.id),
                    "a copy's id is free in the document it will join");
        require(copies[0].id != copies[1].id,
                "and free of the other copies in the same paste -- allocating "
                "against the untouched document would hand out one id twice");
    }

    // --- the offset moves the piece in the game, not only on screen ---------
    {
        const std::vector<Entity> source =
            collectEntities(document, {"kit.wall_0001"});
        const Entity copy = offsetCopies(document, source, 4.0f, 1).front();
        require(copy.cell.has_value(), "the placement survives the copy");
        require(copy.cell->col == 1 && copy.cell->row == 0,
                "the cell moved -- the cooker reads the cell, so a copy that "
                "shares one is two walls in a single slot");
        require(copy.transform.position.x > source.front().transform.position.x,
                "and the transform moved with it, so the viewport agrees");
    }

    // --- a free entity has no cell to shift ---------------------------------
    {
        const std::vector<Entity> source =
            collectEntities(document, {"torch_0001"});
        const Entity copy = offsetCopies(document, source, 4.0f, 1).front();
        require(!copy.cell.has_value(), "a prop stays free");
        require(copy.transform.position.x == 6.0f,
                "and is offset by the metres asked for");
        require(copy.transform.position.y == 1.5f &&
                    copy.transform.position.z == 0.0f,
                "in one axis only -- a paste that also drifts vertically is a "
                "paste the author has to undo");
    }

    // --- a composed object copies as an object, not as loose parts ----------
    {
        Doc composed;
        Entity chandelier = wall("chandelier_0001", 0, 0);
        chandelier.prefab = "kit.chandelier";
        chandelier.cell.reset();
        composed.add(chandelier);
        for (int i = 1; i <= 3; ++i) {
            Entity candle;
            candle.id = "candle_000" + std::to_string(i);
            candle.prefab = "kit.candle";
            candle.parent = "chandelier_0001";
            candle.transform.position = glm::vec3(float(i), 0.0f, 0.0f);
            composed.add(candle);
        }

        const std::vector<AuthorId> whole =
            withDescendants(composed, {"chandelier_0001"});
        require(whole.size() == 4, "the object is the root plus its children");
        require(whole.front() == "chandelier_0001",
                "root first, so the copy can re-point the links as it goes");

        const std::vector<Entity> copies =
            offsetCopies(composed, collectEntities(composed, whole), 4.0f, 1);
        require(copies.size() == 4, "all four are copied");

        const AuthorId newRoot = copies.front().id;
        require(newRoot != "chandelier_0001", "with a fresh root id");
        for (std::size_t i = 1; i < copies.size(); ++i) {
            require(copies[i].parent == newRoot,
                    "and every copied child hangs off the COPIED root -- "
                    "pointing at the original would leave the candles behind "
                    "the moment the new chandelier is moved");
        }
        for (std::size_t i = 1; i < copies.size(); ++i) {
            require(copies[i].transform.position.x == float(i),
                    "a child keeps its offset: it moves with the parent it was "
                    "copied alongside, and offsetting it too would move it "
                    "twice");
        }
        require(copies.front().transform.position.x == 4.0f,
                "only the root takes the offset");
    }

    // --- copying one child keeps it on the original parent ------------------
    {
        Doc composed;
        composed.add(wall("rack_0001", 0, 0));
        Entity torch;
        torch.id = "torch_0001";
        torch.parent = "rack_0001";
        composed.add(torch);

        const std::vector<Entity> copies = offsetCopies(
            composed, collectEntities(composed, {"torch_0001"}), 4.0f, 1);
        require(copies.size() == 1 && copies.front().parent == "rack_0001",
                "a parent outside the copied set is left alone -- duplicating "
                "one torch on a rack should give a second torch on that rack");
        require(copies.front().transform.position.x == 4.0f,
                "and it does move, because nothing it was copied with moved it");
    }

    // --- degenerate ----------------------------------------------------------
    {
        require(offsetCopies(document, {}, 4.0f, 1).empty(),
                "copying nothing yields nothing");
        require(collectEntities(document, {}).empty(), "and collects nothing");
        require(withDescendants(document, {}).empty(), "and expands nothing");
        require(withDescendants(document, {"gone"}).size() == 1,
                "an id the document lost still expands to itself, so a delete "
                "of a stale selection is a no-op rather than a crash");
    }

    std::cout << "EditorClipboardTests: ok\n";
    return 0;
}
