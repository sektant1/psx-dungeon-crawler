#include <eng/TextResource.h>
#include <eng/FileSystem.h>

namespace eng {

bool TextResource::load() {
    if (!FileSystem::fileRead(mPath, mText)) { mLoaded = false; return false; }
    mLoaded = true;
    return true;
}

} // namespace eng
