#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Validated dungeon layout. ASCII is an adapter at the seam; callers consume
// derived markers and room/arch topology instead of re-interpreting chars.
namespace gen {
struct Cell {
    int col = -1;
    int row = -1;
    bool valid() const { return col >= 0 && row >= 0; }
};

struct Arch {
    int roomA = -1;
    int roomB = -1;
    bool northSouth = false;
};

class Layout {
public:
    static Layout fromRows(std::vector<std::string> rows,
                           bool requireExit = false);

    bool valid() const { return mError.empty(); }
    const std::string& error() const { return mError; }
    const std::vector<std::string>& rows() const { return mRows; }
    int rowCount() const { return int(mRows.size()); }
    int columnCount() const { return mStride; }
    char cellAt(int col, int row) const;
    bool walkable(int col, int row) const;
    int roomAt(int col, int row) const;
    int archAt(int col, int row) const;
    const Arch& arch(int index) const { return mArches[size_t(index)]; }
    int roomCount() const { return mRoomCount; }
    int archCount() const { return int(mArches.size()); }
    Cell spawn() const { return mSpawn; }
    Cell anchor() const { return mAnchor; }
    Cell exit() const { return mExit; }

private:
    std::vector<std::string> mRows;
    std::vector<int> mCellRoom;
    std::vector<int> mCellArch;
    std::vector<Arch> mArches;
    int mStride = 0;
    int mRoomCount = 0;
    Cell mSpawn;
    Cell mAnchor;
    Cell mExit;
    std::string mError;
};

// Deterministic: same seed -> identical validated layout.
Layout generate(uint32_t seed);
} // namespace gen
