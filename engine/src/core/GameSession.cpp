#include <eng/GameSession.h>

namespace eng {

GameSession::~GameSession() = default;

Space* GameSession::createSpace(std::string name) {
    auto space = std::make_unique<Space>(name);
    Space* raw = space.get();
    mSpaces[std::move(name)] = std::move(space);
    return raw;
}

Space* GameSession::getSpace(const std::string& name) const {
    auto it = mSpaces.find(name);
    return it == mSpaces.end() ? nullptr : it->second.get();
}

void GameSession::destroySpace(const std::string& name) { mSpaces.erase(name); }
void GameSession::clear() { mSpaces.clear(); }

} // namespace eng
