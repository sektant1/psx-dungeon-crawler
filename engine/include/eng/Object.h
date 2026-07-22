#pragma once
#include <string>

namespace eng {

// Base of every engine object: a stable unique id + a mutable name.
// Ported from SPEngine's Object; modernized to eng conventions.
class Object {
public:
    explicit Object(std::string name);
    virtual ~Object();

    unsigned id() const { return mId; }
    const std::string& name() const { return mName; }
    void setName(std::string name) { mName = std::move(name); }

private:
    std::string mName;
    unsigned mId;
    static unsigned sCreated;
};

} // namespace eng
