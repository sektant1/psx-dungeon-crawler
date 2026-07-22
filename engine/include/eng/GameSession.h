#pragma once
#include <eng/Space.h>
#include <memory>
#include <string>
#include <unordered_map>

namespace eng {

// Owns the set of active spaces for a play session.
class GameSession : public Object {
public:
    explicit GameSession(std::string name) : Object(std::move(name)) {}
    ~GameSession() override;

    Space* createSpace(std::string name);
    Space* getSpace(const std::string& name) const;
    void destroySpace(const std::string& name);
    void clear();

private:
    std::unordered_map<std::string, std::unique_ptr<Space>> mSpaces;
};

} // namespace eng
