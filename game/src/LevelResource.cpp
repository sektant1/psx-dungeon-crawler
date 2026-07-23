#include "LevelResource.h"
#include <eng/Log.h>

bool LevelResource::load() {
    std::string error;
    mLoaded = mDoc.loadToml(mPath, error);
    if (!mLoaded)
        eng::log::error("LevelResource '%s': %s", name().c_str(), error.c_str());
    return mLoaded;
}
