#pragma once
#include "SceneDocument.h"

#include <functional>
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

    // "Saved" is a position in the history, not a flag: undoing back to it means
    // the file on disk matches again, and the title bar should stop saying so.
    void markSaved() { mSavedDepth = mDone.size(); }
    bool savedStateReached() const { return mDone.size() == mSavedDepth; }

private:
    std::vector<Command> mDone;
    std::vector<Command> mUndone;
    std::size_t mSavedDepth = 0;
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
