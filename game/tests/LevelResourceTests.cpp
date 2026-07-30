#include "../src/LevelResource.h"
#include <eng/Content.h>
#include <cstdlib>
#include <iostream>
static void require(bool c, const char* m){ if(!c){ std::cerr<<"LevelResourceTests: "<<m<<'\n'; std::exit(1);} }

int main(){
    const std::string showroom = std::string(APP_ASSET_DIR) + "/showroom.toml";

    // Direct: LevelResource loads the default showroom into a valid layout.
    LevelResource lr("showroom", showroom);
    require(!lr.loaded(), "not loaded before load()");
    require(lr.load() && lr.loaded(), "load parses showroom.toml");
    require(lr.document().rows() > 0, "document populated");
    require(lr.layout().valid(), "layout validates");
    require(lr.layout().spawn().valid() && lr.layout().anchor().valid() &&
                lr.layout().exit().valid(),
            "showroom preserves spawn, centre and portal markers");
    require(lr.layout().archCount() >= 2,
            "showroom exercises repeated doorway assembly");
    for (const char marker : std::string("HBRVL")) {
        bool found = false;
        for (int row = 0; row < lr.layout().rowCount() && !found; ++row)
            for (int col = 0; col < lr.layout().columnCount(); ++col)
                if (lr.layout().cellAt(col, row) == marker) {
                    found = true;
                    break;
                }
        require(found, "showroom retains every static marker feature");
    }
    for (int row = 0; row < lr.layout().rowCount(); ++row)
        for (int col = 0; col < lr.layout().columnCount(); ++col) {
            if (lr.layout().cellAt(col, row) != 'L')
                continue;
            const bool againstWall =
                !lr.layout().walkable(col - 1, row) ||
                !lr.layout().walkable(col + 1, row) ||
                !lr.layout().walkable(col, row - 1) ||
                !lr.layout().walkable(col, row + 1);
            require(againstWall,
                    "every showroom torch marker must touch a wall");
        }

    LevelResource missing("nope", std::string(APP_ASSET_DIR) + "/does_not_exist.toml");
    require(!missing.load(), "missing file fails load");

    // Through eng::Content: cached + deduped by name.
    eng::Content content;
    LevelResource* a = content.load<LevelResource>("showroom", showroom);
    require(a && a->layout().valid(), "content loads level resource");
    require(content.load<LevelResource>("showroom", showroom) == a,
            "content dedups by name");
    require(content.get<LevelResource>("showroom") == a,
            "content get returns cached");

    std::cout << "LevelResourceTests OK\n";
    return 0;
}
