#pragma once
#include <editor/content/SceneDocument.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ed {

using Doc = game::content::SceneDocument;
using game::content::AuthorId;
using game::content::Entity;
using game::content::XformAuthor;

// One reversible edit.
//
// Commands address entities by AuthorId and resolve them at apply time -- never
// by pointer, index or entt handle. That is the rule that keeps a history valid
// across delete + undo: a handle would be recycled and every older entry in the
// stack would silently start pointing at a different thing.
struct Command {
    std::string label; // shown in the Edit menu and the undo tooltip
    std::function<void(Doc&)> apply;
    std::function<void(Doc&)> revert;
};

class CommandStack
{
public:
    void run(Doc& document, Command command);
    bool undo(Doc& document);
    bool redo(Doc& document);

    bool canUndo() const { return !mDone.empty(); }
    bool canRedo() const { return !mUndone.empty(); }
    std::string undoLabel() const;
    std::string redoLabel() const;
    void clear();

    // --- history, as a list --------------------------------------------------
    //
    // The History panel draws the stack: what has been done, oldest first, then
    // what has been undone and could come back. Clicking a row walks to it.
    //
    // Exposed as labels rather than as the commands themselves. A caller that
    // could reach a Command could run its apply() out of order, and the whole
    // reason this class exists is that the order is the invariant.
    std::vector<std::string> doneLabels() const;
    // Newest first: the next redo is row 0, which is the order the panel draws
    // them in above the "current" marker.
    std::vector<std::string> undoneLabels() const;
    std::size_t doneCount() const { return mDone.size(); }
    std::size_t undoneCount() const { return mUndone.size(); }

    // Undoes or redoes until exactly `depth` commands are on the done stack.
    // What a click on a history row means. Returns how many steps it took, so
    // the caller can skip the work that follows a no-op.
    std::size_t walkTo(Doc& document, std::size_t depth);

    // "Saved" is a position in the history, not a flag: undoing back to it means
    // the file on disk matches again, and the title bar should stop saying so.
    void markSaved() { mSavedDepth = mDone.size(); }
    bool savedStateReached() const { return mDone.size() == mSavedDepth; }

private:
    std::vector<Command> mDone;
    std::vector<Command> mUndone;
    std::size_t mSavedDepth = 0;
};

// A live inspector edit spans several ImGui frames: the document changes while
// the pointer moves, but history gets one command when the interaction closes.
// Keeping the activation-time entity here prevents the final drag frame from
// becoming the undo snapshot.
class EntityEditTransaction
{
public:
    bool active() const { return mBefore.has_value(); }
    const AuthorId& id() const { return mId; }
    // The entity as it was when the interaction opened, or nothing when no
    // edit is in progress.
    //
    // This is what multi-object editing is diffed against (ed::multiedit): the
    // inspector draws one entity, and comparing its pre-edit state against the
    // committed one is how the editor knows which FIELD the author moved and
    // therefore what to fan out across the rest of the selection. It used to be
    // a single `beforeMaterial()`, which is why only the material could.
    const std::optional<Entity>& before() const { return mBefore; }
    void begin(const Entity& before);
    std::optional<Command> commit(const Doc& document, std::string label);
    bool cancel(Doc& document);
    void clear();

private:
    AuthorId mId;
    std::optional<Entity> mBefore;
};

Command makeCreateEntity(Entity entity);
Command makeDeleteEntity(const Doc& document, const AuthorId& id);
Command makeSetTransform(const AuthorId& id, XformAuthor before,
                         XformAuthor after);
// Whole-entity swap, for the inspector: simpler and safer than one command per
// field, and an Entity is small enough to copy.
Command makeEditEntity(std::string label, const AuthorId& id, Entity before,
                       Entity after);
Command makeComposite(std::string label, std::vector<Command> parts);

} // namespace ed
