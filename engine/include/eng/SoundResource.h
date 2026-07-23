#pragma once
#include <eng/Resource.h>

namespace eng {

// A named handle to an on-disk audio file. load() validates the path exists;
// the actual PCM decode is owned by miniaudio's resource manager and happens
// lazily when Audio::play() first references the file. Thin on purpose.
class SoundResource : public Resource {
public:
    SoundResource(std::string name, std::string path)
        : Resource(std::move(name), std::move(path)) {}
    bool load() override;
};

} // namespace eng
