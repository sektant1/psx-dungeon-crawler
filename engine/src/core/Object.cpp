#include <eng/Object.h>

namespace eng {

unsigned Object::sCreated = 0;

Object::Object(std::string name) : mName(std::move(name)), mId(sCreated++) {}
Object::~Object() = default;

} // namespace eng
