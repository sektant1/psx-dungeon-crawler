#pragma once

#include "DungeonGen.h"

#include <cstddef>
#include <string>
#include <vector>

// Editor-owned, renderer-independent dungeon document. Keeping commands and
// validation here makes the authoring workflow testable without booting Ogre.
class LevelDocument
{
public:
    explicit LevelDocument(std::vector<std::string> rows = {});

    void replace(std::vector<std::string> rows, bool remember = true);
    bool paint(int col, int row, char tile);
    bool undo();
    bool redo();

    int rows() const { return int(mRows.size()); }
    int columns() const;
    char cell(int col, int row) const;
    const std::vector<std::string>& data() const { return mRows; }
    gen::Layout validated(bool requireExit = true) const;
    bool dirty() const { return mRows != mSavedRows; }
    bool canUndo() const { return !mUndo.empty(); }
    bool canRedo() const { return !mRedo.empty(); }

    bool loadToml(const std::string& path, std::string& error);
    bool saveToml(const std::string& path, std::string& error);

private:
    void checkpoint();
    void normalize();
    static bool isUniqueMarker(char tile);

    std::vector<std::string> mRows;
    std::vector<std::string> mSavedRows;
    std::vector<std::vector<std::string>> mUndo;
    std::vector<std::vector<std::string>> mRedo;
    static constexpr size_t kHistoryLimit = 100;
};
