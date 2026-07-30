#include "Commands.h"

namespace ed {

void CommandStack::run(Doc& document, Command command)
{
    command.apply(document);
    mDone.push_back(std::move(command));
    // A new edit after undoing discards the redo branch, and with it any chance
    // of getting back to a saved state that lived on that branch.
    mUndone.clear();
    if (mSavedDepth > mDone.size())
        mSavedDepth = ~std::size_t(0);
}

bool CommandStack::undo(Doc& document)
{
    if (mDone.empty())
        return false;
    Command command = std::move(mDone.back());
    mDone.pop_back();
    command.revert(document);
    mUndone.push_back(std::move(command));
    return true;
}

bool CommandStack::redo(Doc& document)
{
    if (mUndone.empty())
        return false;
    Command command = std::move(mUndone.back());
    mUndone.pop_back();
    command.apply(document);
    mDone.push_back(std::move(command));
    return true;
}

std::string CommandStack::undoLabel() const
{
    return mDone.empty() ? std::string() : mDone.back().label;
}

std::string CommandStack::redoLabel() const
{
    return mUndone.empty() ? std::string() : mUndone.back().label;
}

void CommandStack::clear()
{
    mDone.clear();
    mUndone.clear();
    mSavedDepth = 0;
}

Command makeCreateEntity(Entity entity)
{
    const AuthorId id = entity.id;
    Command command;
    command.label = "create " + id;
    command.apply = [entity](Doc& document) { document.add(entity); };
    command.revert = [id](Doc& document) { document.remove(id); };
    return command;
}

Command makeDeleteEntity(const Doc& document, const AuthorId& id)
{
    Command command;
    command.label = "delete " + id;
    const Entity* existing = document.find(id);
    // Snapshot taken now, so undo restores the entity exactly as it was rather
    // than as a default-constructed shell.
    const Entity snapshot = existing ? *existing : Entity{};
    command.apply = [id](Doc& doc) { doc.remove(id); };
    command.revert = [snapshot](Doc& doc) { doc.add(snapshot); };
    return command;
}

Command makeSetTransform(const AuthorId& id, XformAuthor before,
                         XformAuthor after)
{
    Command command;
    command.label = "move " + id;
    command.apply = [id, after](Doc& doc) {
        if (Entity* entity = doc.find(id)) {
            entity->transform = after;
            doc.touch();
        }
    };
    command.revert = [id, before](Doc& doc) {
        if (Entity* entity = doc.find(id)) {
            entity->transform = before;
            doc.touch();
        }
    };
    return command;
}

Command makeEditEntity(std::string label, const AuthorId& id, Entity before,
                       Entity after)
{
    Command command;
    command.label = std::move(label);
    command.apply = [id, after](Doc& doc) {
        if (Entity* entity = doc.find(id)) {
            *entity = after;
            doc.touch();
        }
    };
    command.revert = [id, before](Doc& doc) {
        if (Entity* entity = doc.find(id)) {
            *entity = before;
            doc.touch();
        }
    };
    return command;
}

Command makeComposite(std::string label, std::vector<Command> parts)
{
    Command command;
    command.label = std::move(label);
    command.apply = [parts](Doc& doc) {
        for (const Command& part : parts)
            part.apply(doc);
    };
    // Reverse order: undoing a batch has to unwind it, or a create-then-move
    // pair reverts the move against an entity that no longer exists.
    command.revert = [parts](Doc& doc) {
        for (auto it = parts.rbegin(); it != parts.rend(); ++it)
            it->revert(doc);
    };
    return command;
}

} // namespace ed
