// The undo stack, on the authored IR.
//
// The property that matters: a command must survive its entity being deleted
// and recreated. Commands address entities by AuthorId for exactly this reason
// -- an entt handle would be recycled and older entries would start pointing at
// something else.

#include "Commands.h"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace ed;
using game::content::Entity;
using game::content::XformAuthor;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "EditorCommandTests: " << message << '\n';
        std::exit(1);
    }
}

static Entity make(const std::string& id, glm::vec3 position = {})
{
    Entity entity;
    entity.id = id;
    entity.name = id;
    entity.transform.position = position;
    return entity;
}

int main()
{
    Doc document;
    CommandStack stack;

    // --- create / undo / redo ----------------------------------------------
    stack.run(document, makeCreateEntity(make("wall_0001", {4.0f, 0.0f, 0.0f})));
    require(document.contains("wall_0001"), "create adds the entity");
    require(stack.canUndo() && !stack.canRedo(), "and can be undone");

    require(stack.undo(document), "undo runs");
    require(!document.contains("wall_0001"), "undo removes it");
    require(stack.canRedo(), "redo becomes available");
    require(stack.redo(document), "redo runs");
    require(document.contains("wall_0001"), "redo puts it back");

    // --- transform ----------------------------------------------------------
    const XformAuthor before = document.find("wall_0001")->transform;
    XformAuthor after = before;
    after.position = {8.0f, 0.0f, 12.0f};
    stack.run(document, makeSetTransform("wall_0001", before, after));
    require(document.find("wall_0001")->transform.position.z == 12.0f,
            "set-transform applies");
    stack.undo(document);
    require(document.find("wall_0001")->transform.position.z == 0.0f,
            "and reverts");
    stack.redo(document);

    // --- delete keeps the whole entity, not a shell ------------------------
    document.find("wall_0001")->name = "North Wall";
    stack.run(document, makeDeleteEntity(document, "wall_0001"));
    require(!document.contains("wall_0001"), "delete removes it");
    stack.undo(document);
    require(document.find("wall_0001")->name == "North Wall",
            "undoing a delete restores the entity as it was");
    require(document.find("wall_0001")->transform.position.z == 12.0f,
            "including its transform");

    // --- the recycled-handle trap ------------------------------------------
    // Delete and recreate under the same id, then undo the OLD move command. It
    // has to still address the right entity.
    {
        Doc doc;
        CommandStack history;
        history.run(doc, makeCreateEntity(make("torch_0001")));
        XformAuthor moved;
        moved.position = {2.0f, 0.0f, 0.0f};
        history.run(doc, makeSetTransform("torch_0001", XformAuthor{}, moved));
        history.run(doc, makeDeleteEntity(doc, "torch_0001"));
        history.run(doc, makeCreateEntity(make("torch_0001", {9.0f, 9.0f, 9.0f})));
        require(history.undo(doc), "undo the recreate");
        require(history.undo(doc), "undo the delete");
        require(history.undo(doc), "undo the move");
        require(doc.find("torch_0001")->transform.position.x == 0.0f,
                "the move undo found the right entity after a delete cycle");
    }

    // --- composite unwinds in reverse --------------------------------------
    {
        Doc doc;
        CommandStack history;
        std::vector<Command> parts;
        parts.push_back(makeCreateEntity(make("a")));
        XformAuthor moved;
        moved.position = {5.0f, 0.0f, 0.0f};
        parts.push_back(makeSetTransform("a", XformAuthor{}, moved));
        history.run(doc, makeComposite("create and move", std::move(parts)));
        require(doc.find("a")->transform.position.x == 5.0f,
                "a composite applies in order");
        require(history.undo(doc), "and undoes");
        require(!doc.contains("a"),
                "unwinding in reverse leaves nothing behind");
    }

    // --- a new edit discards the redo branch -------------------------------
    {
        Doc doc;
        CommandStack history;
        history.run(doc, makeCreateEntity(make("a")));
        history.undo(doc);
        require(history.canRedo(), "redo is available after an undo");
        history.run(doc, makeCreateEntity(make("b")));
        require(!history.canRedo(), "a new edit drops the redo branch");
    }

    // --- saved marker -------------------------------------------------------
    {
        Doc doc;
        CommandStack history;
        history.run(doc, makeCreateEntity(make("a")));
        history.markSaved();
        require(history.savedStateReached(), "just saved: clean");
        history.run(doc, makeCreateEntity(make("b")));
        require(!history.savedStateReached(), "an edit makes it dirty");
        history.undo(doc);
        require(history.savedStateReached(),
                "undoing back to the saved point makes it clean again");
    }

    std::cout << "EditorCommandTests: ok\n";
    return 0;
}
