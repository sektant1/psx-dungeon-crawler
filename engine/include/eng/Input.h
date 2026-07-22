#pragma once
#include <glm/vec2.hpp>

#include <memory>
#include <string>

namespace eng {

enum class MouseButton { Left, Right, Middle };

class Config;

// Action-mapped input. Bindings come from the [bindings] TOML table:
// action name -> SDL key name string (or array of them).
class Input
{
public:
    Input();
    ~Input();

    bool loadBindings(const Config& cfg); // false on unknown key name

    bool isDown(const std::string& action) const;
    bool wasPressed(const std::string& action) const; // edge, cleared each tick
    bool wasMouseClicked() const;                     // left-button edge
    bool isMouseDown(MouseButton button) const;
    bool wasMousePressed(MouseButton button) const;
    glm::vec2 mouseDelta() const;                     // relative, this tick

    void setMouseGrab(bool grab); // relative mouse mode + cursor capture
    bool mouseGrabbed() const;

private:
    friend class Engine; // feeds SDL events, begins ticks
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace eng
