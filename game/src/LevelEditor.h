#pragma once

#include "LevelDocument.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

class DungeonMap;

class LevelEditor
{
public:
    LevelEditor(std::vector<std::string> rows, std::string defaultPath,
                bool workspace = false);

    // Draws the editor and returns true once when a valid layout should be
    // rebuilt into the playable scene.
    bool draw(const DungeonMap& map, glm::vec3 playerPosition);
    gen::Layout takeLayout() const { return mDocument.validated(true); }

private:
    void setStatus(std::string text, bool error = false);

    LevelDocument mDocument;
    std::string mPath;
    std::string mStatus;
    bool mStatusError = false;
    char mBrush = '.';
    uint32_t mSeed = 1;
    bool mShowRooms = false;
    bool mWorkspace = false;
};
