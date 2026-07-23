#pragma once
#include <eng/Resource.h>
#include <string>

namespace eng {

// A resource whose payload is the raw text of its file. The simplest concrete
// Resource; also the bridge pattern game-side resources follow (call an existing
// loader inside load()).
class TextResource : public Resource {
public:
    TextResource(std::string name, std::string path)
        : Resource(std::move(name), std::move(path)) {}

    bool load() override;
    const std::string& text() const { return mText; }

private:
    std::string mText;
};

} // namespace eng
