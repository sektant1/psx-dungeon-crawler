#include <eng/SoundResource.h>
#include <eng/FileSystem.h>

namespace eng {

bool SoundResource::load() {
    mLoaded = FileSystem::exists(mPath);
    return mLoaded;
}

} // namespace eng
