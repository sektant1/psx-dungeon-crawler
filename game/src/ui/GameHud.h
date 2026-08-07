#pragma once

#include "../HudModel.h"
#include "GameHudStyle.h"

#include <eng/ui/TargetBanner.h>
#include <eng/ui/Tooltip.h>
#include <eng/ui/UiCanvas.h>

#include <glm/glm.hpp>

#include <string>
#include <span>

struct ImDrawList;

namespace eng { class Config; }

namespace game {

enum class HudRegion { Threshold, Interior };

// Player presentation, drawn on the engine's virtual pixel canvas.
//
// This replaced a hand-rolled HUD that measured everything in window pixels
// against the imgui font: it blurred as it scaled, every widget carried its
// own palette lookup, and the panel geometry was octagons drawn with float
// polylines. Nothing here knows a colour value or a font -- layout is virtual
// pixels, colour is the canvas palette, glyphs are the bitmap font.
//
// It still consumes only HudSnapshot values plus a prepared TooltipContent, so
// input, combat, interaction and level ownership stay with their systems.
class GameHud {
public:
    // Loads the canvas font. Returns false when the atlas is missing, in which
    // case draw() is a no-op rather than a crash.
    //
    // The definition is a parameter rather than the canvas default because the
    // *game's* typeface is an art decision and the engine's default is a
    // fallback: `ui.font` in game.toml picks it, and a project that ships its
    // own atlas names that instead.
    bool initialise(const std::string& fontDefinition = "ui_regular.toml");

    void configure(const eng::Config& config);
    void notifyRegion(HudRegion region);
    // Key cap the tooltip pairs with an interaction verb.
    const std::string& interactKey() const { return mInteractKey; }

    // The game's entry point: the whole window, on imgui's foreground list, at
    // the integer scale the window allows.
    void draw(const HudSnapshot& snapshot,
              const eng::ui::TooltipContent& tooltip, float dt,
              bool visible = true);

    // The same HUD inside somebody else's rectangle, at a virtual resolution
    // and scale the caller picks. This is what the editor's 2D viewport draws,
    // and it goes through the same layout as the game -- a HUD preview that
    // reimplements the HUD only tells you about the preview.
    void drawInto(const HudSnapshot& snapshot,
                   const eng::ui::TooltipContent& tooltip, float dt,
                   glm::vec2 originPixels, glm::ivec2 virtualSize, int scale,
                   ImDrawList* target, eng::ui::Insets safeArea = {});

private:
    // Per-frame animation timers, and whether there is anything to draw.
    bool beginFrame(const HudSnapshot& snapshot, float dt, bool visible);
    // The layout, against whatever surface the canvas is currently on.
    void paint(const HudSnapshot& snapshot,
               const eng::ui::TooltipContent& tooltip, float dt,
               eng::ui::Insets safeArea);
    void drawVitals(const HudSnapshot& snapshot, eng::ui::UiRect bounds,
                    bool compact) const;
    void drawArmament(const HudSnapshot& snapshot, eng::ui::UiRect bounds,
                      bool compact) const;
    void drawStatuses(const HudSnapshot& snapshot,
                      std::span<const eng::ui::UiRect> bounds,
                      bool compact) const;
    void drawReticle(const HudSnapshot& snapshot) const;
    void drawRegionNotice(const glm::ivec4& keepClear,
                          eng::ui::UiRect safe) const;

    eng::ui::UiCanvas mCanvas;
    // Two surfaces, one content model. Which one a look-target lands on is
    // decided by its TooltipEmphasis, not by what kind of thing it is: the HUD
    // owns the widget choice, gameplay owns the intent.
    eng::ui::TooltipView mTooltip;
    eng::ui::TargetBanner mBanner;
    GameHudStyleSheet mStyle;

    float mOpacity = 0.92f;
    bool mHighContrast = false;
    bool mReducedMotion = false;
    bool mShowCrosshair = true;
    bool mShowNumbers = true;
    float mUserScale = 1.0f; // multiplies the automatic integer fit
    float mSafeAreaPercent = 5.0f;
    std::string mInteractKey = "E";
    std::string mSwapKey = "X";
    HudWeapon mLastWeapon;
    float mWeaponFlash = 0.0f;
    float mRegionNotice = 0.0f;
    std::string mRegionTitle;
    std::string mRegionSubtitle;
};

} // namespace game
