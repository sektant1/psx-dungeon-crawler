#include "LevelDocument.h"

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

LevelDocument::LevelDocument(std::vector<std::string> rows)
    : mRows(std::move(rows))
{
    normalize();
    mSavedRows = mRows;
}

int LevelDocument::columns() const
{
    size_t width = 0;
    for (const std::string& row : mRows)
        width = std::max(width, row.size());
    return int(width);
}

char LevelDocument::cell(int col, int row) const
{
    if (row < 0 || row >= rows() || col < 0 || col >= columns())
        return ' ';
    return mRows[size_t(row)][size_t(col)];
}

bool LevelDocument::isUniqueMarker(char tile)
{
    return tile == 'S' || tile == 'C' || tile == 'X';
}

void LevelDocument::normalize()
{
    if (mRows.empty())
        mRows.assign(15, std::string(15, '#'));
    const int width = std::max(1, columns());
    for (std::string& row : mRows)
        row.resize(size_t(width), '#');
}

void LevelDocument::checkpoint()
{
    mUndo.push_back(mRows);
    if (mUndo.size() > kHistoryLimit)
        mUndo.erase(mUndo.begin());
    mRedo.clear();
}

void LevelDocument::replace(std::vector<std::string> rows, bool remember)
{
    if (remember)
        checkpoint();
    mRows = std::move(rows);
    normalize();
}

bool LevelDocument::paint(int col, int row, char tile)
{
    static const std::string allowed = "#.ALSCXHBRV ";
    if (allowed.find(tile) == std::string::npos || row < 0 || row >= rows() ||
        col < 0 || col >= columns() || cell(col, row) == tile)
        return false;

    checkpoint();
    if (isUniqueMarker(tile)) {
        for (std::string& line : mRows)
            std::replace(line.begin(), line.end(), tile, '.');
    }
    mRows[size_t(row)][size_t(col)] = tile;
    return true;
}

bool LevelDocument::undo()
{
    if (mUndo.empty())
        return false;
    mRedo.push_back(mRows);
    mRows = std::move(mUndo.back());
    mUndo.pop_back();
    return true;
}

bool LevelDocument::redo()
{
    if (mRedo.empty())
        return false;
    mUndo.push_back(mRows);
    mRows = std::move(mRedo.back());
    mRedo.pop_back();
    return true;
}

gen::Layout LevelDocument::validated(bool requireExit) const
{
    return gen::Layout::fromRows(mRows, requireExit);
}

bool LevelDocument::loadToml(const std::string& path, std::string& error)
{
    {
        toml::parse_result result = toml::parse_file(path);
        if (!result) {
            error = std::string(result.error().description());
            return false;
        }
        const toml::table& config = result.table();
        const toml::array* values = config["dungeon"]["rows"].as_array();
        if (!values) {
            error = "missing [dungeon].rows array";
            return false;
        }
        std::vector<std::string> loaded;
        for (const auto& value : *values) {
            const auto row = value.value<std::string>();
            if (!row) {
                error = "dungeon.rows must contain only strings";
                return false;
            }
            loaded.push_back(*row);
        }
        const gen::Layout layout = gen::Layout::fromRows(loaded, true);
        if (!layout.valid()) {
            error = layout.error();
            return false;
        }
        checkpoint();
        mRows = layout.rows();
        normalize();
        mSavedRows = mRows;
        error.clear();
        return true;
    }
}

bool LevelDocument::saveToml(const std::string& path, std::string& error)
{
    const gen::Layout layout = validated(true);
    if (!layout.valid()) {
        error = layout.error();
        return false;
    }
    std::ofstream out(path);
    if (!out) {
        error = "could not open file for writing";
        return false;
    }
    out << "# Authored with the in-game dungeon editor.\n"
           "# # wall, . floor, A arch, L torch, S spawn, C anchor, X exit\n"
           "# H chest, B barrel, R crate, V urn\n\n"
           "[dungeon]\ncell_size = 4.0\nwall_height = 3.0\nrows = [\n";
    for (const std::string& row : mRows)
        out << "    " << std::quoted(row) << ",\n";
    out << "]\n\n[dungeon.light]\n"
           "colour_srgb = [1.0, 0.68, 0.34]\nenergy = 4.4\nrange = 6.5\ny = 1.9\n";
    if (!out) {
        error = "write failed";
        return false;
    }
    mSavedRows = mRows;
    error.clear();
    return true;
}
