#include "../src/LevelResource.h"
#include <eng/Content.h>
#include <cstdlib>
#include <iostream>
static void require(bool c, const char* m){ if(!c){ std::cerr<<"LevelResourceTests: "<<m<<'\n'; std::exit(1);} }

int main(){
    const std::string lobby = std::string(APP_ASSET_DIR) + "/lobby.toml";

    // Direct: LevelResource loads the lobby TOML into a valid layout.
    LevelResource lr("lobby", lobby);
    require(!lr.loaded(), "not loaded before load()");
    require(lr.load() && lr.loaded(), "load parses lobby.toml");
    require(lr.document().rows() > 0, "document populated");
    require(lr.layout().valid(), "layout validates");

    LevelResource missing("nope", std::string(APP_ASSET_DIR) + "/does_not_exist.toml");
    require(!missing.load(), "missing file fails load");

    // Through eng::Content: cached + deduped by name.
    eng::Content content;
    LevelResource* a = content.load<LevelResource>("lobby", lobby);
    require(a && a->layout().valid(), "content loads level resource");
    require(content.load<LevelResource>("lobby", lobby) == a, "content dedups by name");
    require(content.get<LevelResource>("lobby") == a, "content get returns cached");

    std::cout << "LevelResourceTests OK\n";
    return 0;
}
