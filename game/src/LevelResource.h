#pragma once
#include "LevelDocument.h"
#include <eng/Resource.h>

// Bridges the engine's Content/Resource system to a game dungeon file. load()
// parses the TOML through the existing LevelDocument loader; the cached instance
// then hands out the validated gen::Layout the renderer consumes. This is the
// game-side concretion of the pattern Phase 3 proved engine-side with
// TextResource -- the engine never depends on game/, so it lives here.
class LevelResource : public eng::Resource {
public:
    LevelResource(std::string name, std::string path)
        : eng::Resource(std::move(name), std::move(path)) {}

    bool load() override;

    const LevelDocument& document() const { return mDoc; }
    gen::Layout layout(bool requireExit = true) const { return mDoc.validated(requireExit); }

private:
    LevelDocument mDoc;
};
