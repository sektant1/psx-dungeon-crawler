#pragma once

#include "HudModel.h"

#include <string>

namespace eng { class Config; }

namespace game {

enum class HudRegion { Threshold, Interior };

// Full-resolution player presentation. It consumes only HudSnapshot values;
// input, combat, interaction, and level ownership remain with their systems.
class GameHud {
public:
    void configure(const eng::Config& config);
    void notifyRegion(HudRegion region);
    void draw(const HudSnapshot& snapshot, float dt, bool visible = true);

private:
    float mScale = 1.0f;
    float mOpacity = 0.92f;
    bool mHighContrast = false;
    bool mReducedMotion = false;
    bool mShowCrosshair = true;
    bool mShowNumbers = true;
    std::string mInteractKey = "E";
    std::string mSwapKey = "X";
    HudWeapon mLastWeapon = HudWeapon::Unknown;
    float mWeaponFlash = 0.0f;
    float mRegionNotice = 0.0f;
    std::string mRegionTitle;
    std::string mRegionSubtitle;
};

} // namespace game
