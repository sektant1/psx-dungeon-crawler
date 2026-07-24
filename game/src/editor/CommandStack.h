#pragma once

#include <functional>
#include <utility>
#include <vector>

namespace editor {

struct Command {
    std::function<void()> apply;
    std::function<void()> revert;
};

class CommandStack {
public:
    void run(Command c) {
        c.apply();
        mUndo.push_back(std::move(c));
        mRedo.clear();
    }
    bool undo() {
        if (mUndo.empty()) return false;
        Command c = std::move(mUndo.back());
        mUndo.pop_back();
        c.revert();
        mRedo.push_back(std::move(c));
        return true;
    }
    bool redo() {
        if (mRedo.empty()) return false;
        Command c = std::move(mRedo.back());
        mRedo.pop_back();
        c.apply();
        mUndo.push_back(std::move(c));
        return true;
    }
    bool canUndo() const { return !mUndo.empty(); }
    bool canRedo() const { return !mRedo.empty(); }
    void clear() { mUndo.clear(); mRedo.clear(); }

private:
    std::vector<Command> mUndo;
    std::vector<Command> mRedo;
};

} // namespace editor
