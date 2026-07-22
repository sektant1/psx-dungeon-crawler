#pragma once

#include <string>

namespace eng {
class Engine;

// Full-window ImGui progress bar. Single-threaded: present() draws one
// synchronous frame between loading chunks.
class LoadingScreen {
public:
    explicit LoadingScreen(Engine& e);
    void begin(const std::string& title);
    void step(const std::string& label, float t01);
    void present();
    void finish();

private:
    Engine* mEngine = nullptr;
    std::string mTitle;
    std::string mLabel;
    float mProgress = 0.0f;
    bool mActive = false;
};
} // namespace eng
