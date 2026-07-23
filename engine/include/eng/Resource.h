#pragma once
#include <eng/Object.h>
#include <string>

namespace eng {

// Abstract cached asset: a named handle plus the source path it loads from.
// Subclasses implement load() (parse from mPath into memory). Cached and owned
// by ResourceCache<T> / Content.
class Resource : public Object {
public:
    Resource(std::string name, std::string path)
        : Object(std::move(name)), mPath(std::move(path)) {}
    ~Resource() override = default;

    const std::string& path() const { return mPath; }
    bool loaded() const { return mLoaded; }

    virtual bool load() = 0;                 // parse mPath; set mLoaded on success
    virtual void unload() { mLoaded = false; }

protected:
    std::string mPath;
    bool mLoaded = false;
};

} // namespace eng
