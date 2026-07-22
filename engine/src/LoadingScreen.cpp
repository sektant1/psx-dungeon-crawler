#include <eng/LoadingScreen.h>

#include <eng/Engine.h>

#include <algorithm>

namespace eng {

LoadingScreen::LoadingScreen(Engine& e) : mEngine(&e) {}

void LoadingScreen::begin(const std::string& title)
{
    mTitle = title;
    mLabel.clear();
    mProgress = 0.0f;
    mActive = true;
}

void LoadingScreen::step(const std::string& label, float t01)
{
    mLabel = label;
    mProgress = std::clamp(t01, 0.0f, 1.0f);
}

void LoadingScreen::present()
{
    if (mEngine && mActive)
        mEngine->presentLoadingFrame(mTitle, mLabel, mProgress);
}

void LoadingScreen::finish()
{
    if (!mEngine)
        return;
    mActive = false;
    mEngine->presentLoadingFrame("", "", 1.0f);
}

} // namespace eng
