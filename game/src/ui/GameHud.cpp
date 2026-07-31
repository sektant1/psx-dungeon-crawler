#include "GameHud.h"

#include <eng/Config.h>
#include <eng/ui/UiLayout.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>

namespace game {
namespace {

using eng::ui::Align;

constexpr int kChip = 6;

std::string firstBinding(const eng::Config& config, const char* action,
                         const char* fallback) {
    const auto found = config.bindings().find(action);
    if (found == config.bindings().end() || found->second.empty())
        return fallback;
    return found->second.front();
}

std::string upper(std::string value) {
    for (char& c : value)
        c = char(std::toupper(static_cast<unsigned char>(c)));
    return value;
}

std::string number(float current, float maximum) {
    const auto displayInteger = [](float value) {
        if (!std::isfinite(value))
            return 0;
        const double clamped = std::clamp(
            double(value), 0.0, double(std::numeric_limits<int>::max()));
        return int(std::lround(clamped));
    };
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%d/%d", displayInteger(current),
                  displayInteger(maximum));
    return buffer;
}

unsigned int withAlpha(unsigned int colour, float alpha) {
    const unsigned int a = (colour >> 24) & 0xFFu;
    const unsigned int scaled =
        (unsigned int)std::lround(float(a) * std::clamp(alpha, 0.0f, 1.0f));
    return (colour & 0x00FFFFFFu) | (scaled << 24);
}

int visibleResourceRows(const HudSnapshot& snapshot)
{
    return int(snapshot.health.available) + int(snapshot.stamina.available) +
           int(snapshot.mana.available);
}

eng::ui::UiTone statusTone(CrowdControl status)
{
    switch (status) {
    case CrowdControl::Burn: return eng::ui::UiTone::Danger;
    case CrowdControl::Chill: return eng::ui::UiTone::Mystic;
    case CrowdControl::Slow: return eng::ui::UiTone::Warning;
    case CrowdControl::Stun:
    case CrowdControl::Root:
    case CrowdControl::Silence: return eng::ui::UiTone::Warning;
    case CrowdControl::Count: break;
    }
    return eng::ui::UiTone::Muted;
}

std::string statusText(const HudStatus& status, bool compact)
{
    std::string label = hudStatusName(status.kind);
    if (compact && label.size() > 5)
        label.resize(5);
    if (std::isfinite(status.remaining) && status.remaining > 0.0f)
        label += " " + std::to_string(int(std::min(
                           std::ceil(double(status.remaining)),
                           double(std::numeric_limits<int>::max()))));
    return label;
}

} // namespace

bool GameHud::initialise() {
    mStyle = makeGameHudStyleSheet(mOpacity, mHighContrast, mReducedMotion);
    mCanvas.style() = mStyle.ui;
    mTooltip.configure(mStyle.tooltip);
    mBanner.configure(mStyle.banner);
    return mCanvas.initialise();
}

void GameHud::configure(const eng::Config& config) {
    const float requestedScale = float(config.getNumber("hud.scale", 1.0));
    mUserScale = std::isfinite(requestedScale)
                     ? std::clamp(requestedScale, 0.5f, 3.0f)
                     : 1.0f;
    const float requestedOpacity =
        float(config.getNumber("hud.opacity", 0.92));
    mOpacity = std::isfinite(requestedOpacity)
                   ? std::clamp(requestedOpacity, 0.35f, 1.0f)
                   : 0.92f;
    mHighContrast = config.getBool("hud.high_contrast", false);
    mReducedMotion = config.getBool("hud.reduced_motion", false);
    mShowCrosshair = config.getBool("hud.crosshair", true);
    mShowNumbers = config.getBool("hud.numeric_values", true);
    mInteractKey = upper(firstBinding(config, "interact", "E"));
    mSwapKey = upper(firstBinding(config, "swap_weapon", "X"));

    // Resolve from immutable defaults every time. Reconfiguration therefore
    // cannot compound alpha or leave high-contrast/reduced-motion values stuck.
    mStyle = makeGameHudStyleSheet(mOpacity, mHighContrast, mReducedMotion);
    mCanvas.style() = mStyle.ui;
    mTooltip.configure(mStyle.tooltip);
    mBanner.configure(mStyle.banner);
}

void GameHud::notifyRegion(HudRegion region) {
    mRegionNotice = mReducedMotion ? 1.6f : 3.2f;
    if (region == HudRegion::Threshold) {
        mRegionTitle = "THE THRESHOLD";
        mRegionSubtitle = "the gate holds behind you";
    } else {
        mRegionTitle = "DEEPER HALLS";
        mRegionSubtitle = "nothing here was built for visitors";
    }
}

void GameHud::drawVitals(const HudSnapshot& snapshot, eng::ui::UiRect bounds,
                         bool compact) const {
    const eng::ui::UiPalette& pal = mCanvas.palette();
    struct Row {
        const char* label;
        const HudResource& resource;
        eng::ui::UiTone tone;
    };
    const Row rows[] = {
        {"VIT", snapshot.health, eng::ui::UiTone::Danger},
        {"STA", snapshot.stamina, eng::ui::UiTone::Positive},
        {"ARC", snapshot.mana, eng::ui::UiTone::Mystic},
    };

    if (visibleResourceRows(snapshot) == 0 || bounds.empty())
        return;

    const int row = mCanvas.lineHeight();
    const glm::ivec2 at = bounds.position;
    const int width = bounds.size.x;
    const int height = bounds.size.y;
    mCanvas.panel(at, bounds.size, mStyle.vitals, eng::ui::UiTone::Danger);

    int y = at.y + 4;
    for (const Row& entry : rows) {
        if (!entry.resource.available)
            continue;
        mCanvas.text({at.x + 6, y}, entry.label, pal.textDim, Align::Left,
                     false);
        // The numeric column is measured, not guessed: it used to be
        // right-aligned over a full-width bar, which read as two overlapping
        // widgets as soon as a value reached three digits.
        const std::string value =
            number(entry.resource.current, entry.resource.maximum);
        const bool showNumbers = mShowNumbers && width >= (compact ? 94 : 112);
        const int numbersW = showNumbers ? mCanvas.measure(value).x + 4 : 0;
        const int barX = at.x + 6 + mCanvas.measure("ARC").x + 4;
        const int barW = at.x + width - 5 - numbersW - barX;
        mCanvas.bar({barX, y + 2}, {barW, row - 5},
                    hudResourceRatio(entry.resource), mCanvas.colour(entry.tone),
                    pal.inkSoft);
        if (showNumbers)
            mCanvas.text({at.x + width - 5, y}, value, pal.text,
                         Align::Right, false);
        y += row;
    }

    // Poise is not a resource the player spends, so it reads as a thin gauge
    // under the block rather than as a fourth bar competing with them.
    if (snapshot.poise.available) {
        const float ratio = hudResourceRatio(snapshot.poise);
        const int poiseWidth = int(std::lround(ratio * float(width - 4)));
        mCanvas.rect({at.x + 2, at.y + height - 2}, {width - 4, 1},
                      pal.inkSoft);
        mCanvas.rect({at.x + 2, at.y + height - 2}, {poiseWidth, 1},
                      ratio < 0.34f ? pal.bad : pal.edgeBright);
    }
}

void GameHud::drawStatuses(const HudSnapshot& snapshot,
                           std::span<const eng::ui::UiRect> bounds,
                           bool compact) const {
    const int count = std::clamp(snapshot.statusCount, 0,
                                 std::min(int(bounds.size()),
                                          HudSnapshot::kMaxStatuses));
    if (count <= 0)
        return;
    const eng::ui::UiPalette& pal = mCanvas.palette();
    for (int i = 0; i < count; ++i) {
        const HudStatus& status = snapshot.statuses[size_t(i)];
        const eng::ui::UiRect& box = bounds[size_t(i)];
        const eng::ui::UiTone tone = statusTone(status.kind);
        mCanvas.panel(box.position, box.size, mStyle.status, tone);
        mCanvas.icon({box.position.x + 4,
                      box.position.y + (box.size.y - kChip) / 2},
                     {kChip, kChip}, mCanvas.colour(tone), compact ? 1 : 0);
        const std::string label = statusText(status, compact);
        mCanvas.text({box.position.x + kChip + 7, box.position.y + 1}, label,
                     pal.text, Align::Left, false);
    }
}

void GameHud::drawArmament(const HudSnapshot& snapshot,
                           eng::ui::UiRect bounds, bool compact) const {
    const eng::ui::UiPalette& pal = mCanvas.palette();
    const std::string& name = snapshot.weapon.name;
    const std::string& discipline = snapshot.weapon.discipline;
    const std::string keyCap = "[" + mSwapKey + "]";
    // Row two carries the key cap and the discipline side by side, so the
    // plate is as wide as the wider of the two rows -- not as wide as the
    // longest single string, which let them overlap.
    const int keyW = mCanvas.measure(keyCap).x;
    const int width = bounds.size.x;
    const int row = mCanvas.lineHeight();
    const int height = bounds.size.y;
    const glm::ivec2 at = bounds.position;

    mCanvas.panel(at, bounds.size, mStyle.armament, eng::ui::UiTone::Focus);
    // The swap flash lives on the plate's rail, not on the text: a colour
    // pulse under a name is readable at a glance; a pulsing name is not.
    const unsigned int rail =
        mWeaponFlash > 0.0f
            ? withAlpha(pal.edgeBright, 0.35f + 0.65f * mWeaponFlash)
            : pal.edge;
    mCanvas.rect({at.x + width - 3, at.y + 2}, {2, height - 4}, rail);
    const std::string fittedName = mCanvas.font().ellipsize(name, width - 14);
    mCanvas.text({at.x + width - 8, at.y + 3}, fittedName, pal.text,
                 Align::Right, false);
    if (!compact) {
        const int disciplineRoom = std::max(1, width - keyW - 19);
        const std::string fittedDiscipline =
            mCanvas.font().ellipsize(discipline, disciplineRoom);
        mCanvas.text({at.x + width - 8, at.y + 3 + row}, fittedDiscipline,
                     pal.textDim, Align::Right, false);
        mCanvas.text({at.x + 5, at.y + 3 + row}, keyCap, pal.textDim,
                     Align::Left, false);
    }
}

void GameHud::drawReticle(const HudSnapshot& snapshot) const {
    if (!mShowCrosshair)
        return;
    const eng::ui::UiPalette& pal = mCanvas.palette();
    const glm::ivec2 centre = mCanvas.size() / 2;
    const bool focused = snapshot.interaction.available;
    // Four ticks around a gap, opening by one pixel when a target resolves.
    // The gap is what changes, because a reticle that changes size is easier
    // to read at the edge of vision than one that changes colour.
    const int gap = focused ? 4 : 3;
    const int len = focused ? 3 : 2;
    const unsigned int tint = focused ? pal.accent : pal.text;
    mCanvas.rect({centre.x - gap - len, centre.y}, {len, 1}, tint);
    mCanvas.rect({centre.x + gap + 1, centre.y}, {len, 1}, tint);
    mCanvas.rect({centre.x, centre.y - gap - len}, {1, len}, tint);
    mCanvas.rect({centre.x, centre.y + gap + 1}, {1, len}, tint);
    if (snapshot.action != ActionPhase::Idle)
        mCanvas.rect(centre, {1, 1}, pal.edgeBright);
}

void GameHud::drawRegionNotice(const glm::ivec4& keepClear,
                               eng::ui::UiRect safe) const {
    if (mRegionNotice <= 0.0f || mRegionTitle.empty())
        return;
    const eng::ui::UiPalette& pal = mCanvas.palette();
    // Ease the last 0.8 s out; hold full opacity before that.
    const float alpha = mReducedMotion
                            ? 1.0f
                            : std::clamp(mRegionNotice / 0.8f, 0.0f, 1.0f);
    const int x = safe.position.x + safe.size.x / 2;
    // Upper middle reads as an arrival inscription without fighting either
    // bottom instrument cluster or the weapon viewmodel.
    int y = safe.position.y + safe.size.y / 3;
    // Step below the tooltip when the two want the same band. The tooltip
    // reports its rect for exactly this, and not using it is why a level-entry
    // banner used to print straight through an open tooltip.
    if (keepClear.z > 0 && keepClear.w > 0) {
        const int tipBottom = keepClear.y + keepClear.w;
        if (y < tipBottom + 6 && y + mCanvas.lineHeight() * 2 > keepClear.y)
            y = tipBottom + 8;
    }
    const int blockHeight = mCanvas.lineHeight() * 2;
    y = std::clamp(y, safe.position.y,
                   std::max(safe.position.y,
                            safe.position.y + safe.size.y - blockHeight));
    mCanvas.text({x, y}, mRegionTitle, withAlpha(pal.text, alpha),
                 Align::Centre);
    mCanvas.text({x, y + mCanvas.lineHeight()}, mRegionSubtitle,
                 withAlpha(pal.textDim, alpha), Align::Centre);
    const int width = mCanvas.measure(mRegionTitle).x + 20;
    mCanvas.rect({x - width / 2, y - 4}, {width, 1},
                 withAlpha(pal.edge, alpha));
}

void GameHud::draw(const HudSnapshot& snapshot,
                   const eng::ui::TooltipContent& tooltip, float dt,
                   bool visible) {
    // Timers decay even while hidden: coming back from a menu should not
    // replay a weapon flash the player already saw.
    mWeaponFlash = std::max(0.0f, mWeaponFlash - dt * 2.5f);
    mRegionNotice = std::max(0.0f, mRegionNotice - dt);
    if (snapshot.weapon != mLastWeapon) {
        mLastWeapon = snapshot.weapon;
        mWeaponFlash = mReducedMotion ? 0.0f : 1.0f;
    }

    if (!mCanvas.ready() || !visible || !snapshot.valid) {
        mTooltip.update({}, dt);
        mBanner.update({}, dt);
        return;
    }

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    // hud.scale is honoured by re-basing the *preferred* resolution, not by
    // multiplying coordinates: the canvas magnification stays an integer, so
    // a user scale can never reintroduce half pixels. A bigger scale asks for
    // fewer virtual pixels, which is what makes the widgets larger.
    // 640x480 rather than 320x240: at a 960x720 window that is a 1:1 canvas,
    // so the bitmap font draws at its authored size instead of being magnified
    // into blocks. Bigger windows still step up by whole factors.
    const glm::ivec2 preferred{int(std::lround(640.0f / mUserScale)),
                               int(std::lround(480.0f / mUserScale))};
    mCanvas.begin({display.x, display.y}, preferred);

    const GameHudViewportStyle viewport =
        resolveGameHudViewportStyle(mStyle, mCanvas.size());
    const int line = mCanvas.lineHeight();

    const int vitalsRows = visibleResourceRows(snapshot);
    const int vitalsHeight = vitalsRows > 0
                                 ? vitalsRows * line + 7 +
                                       (snapshot.poise.available ? 2 : 0)
                                 : 0;
    const std::string& weaponName = snapshot.weapon.name;
    const std::string& discipline = snapshot.weapon.discipline;
    const std::string keyCap = "[" + mSwapKey + "]";
    const int naturalArmamentWidth =
        viewport.compact
            ? mCanvas.measure(weaponName).x + 16
            : std::max(mCanvas.measure(weaponName).x,
                       mCanvas.measure(keyCap).x + 6 +
                           mCanvas.measure(discipline).x) +
                  16;
    const int armamentHeight = (viewport.compact ? line : line * 2) + 7;
    const int availableArmament = std::max(
        1, viewport.safe.size.x - viewport.vitalsWidth - viewport.gap);
    const int armamentWidth = std::min(naturalArmamentWidth, availableArmament);
    const GameHudBottomLayout bottom = layoutGameHudBottom(
        viewport, {viewport.vitalsWidth, vitalsHeight},
        {armamentWidth, armamentHeight});

    drawVitals(snapshot, bottom.vitals, viewport.compact);
    drawArmament(snapshot, bottom.armament, viewport.compact);

    const int statusCount = std::clamp(snapshot.statusCount, 0,
                                       HudSnapshot::kMaxStatuses);
    std::array<eng::ui::FlexItem, HudSnapshot::kMaxStatuses> statusItems{};
    std::array<eng::ui::UiRect, HudSnapshot::kMaxStatuses> statusRects{};
    const int chipHeight = line + 2;
    for (int i = 0; i < statusCount; ++i) {
        const std::string label =
            statusText(snapshot.statuses[size_t(i)], viewport.compact);
        const int width = mCanvas.measure(label).x + kChip + 12;
        statusItems[size_t(i)] = {{width, chipHeight},
                                  {std::min(width, 48), chipHeight}, 0, 1};
    }
    eng::ui::FlexLayout statusLayout;
    statusLayout.wrap = true;
    statusLayout.gap = 3;
    statusLayout.lineGap = 3;
    const eng::ui::FlexLayoutResult statusMeasure = eng::ui::layoutFlex(
        {{viewport.safe.position.x, 0}, {viewport.safe.size.x, 1024}},
        statusLayout,
        std::span<const eng::ui::FlexItem>(statusItems.data(),
                                            size_t(statusCount)),
        std::span<eng::ui::UiRect>(statusRects.data(), size_t(statusCount)));
    const int statusY = std::max(
        viewport.safe.position.y,
        std::min(bottom.vitals.position.y, bottom.armament.position.y) -
            viewport.gap - statusMeasure.usedExtent.y);
    (void)eng::ui::layoutFlex(
        {{viewport.safe.position.x, statusY},
         {viewport.safe.size.x, statusMeasure.usedExtent.y}},
        statusLayout,
        std::span<const eng::ui::FlexItem>(statusItems.data(),
                                            size_t(statusCount)),
        std::span<eng::ui::UiRect>(statusRects.data(), size_t(statusCount)));
    drawStatuses(snapshot,
                 std::span<const eng::ui::UiRect>(statusRects.data(),
                                                   size_t(statusCount)),
                 viewport.compact);
    drawReticle(snapshot);

    // Route by emphasis: a combatant goes to the top banner, everything else
    // to the crosshair tooltip. Exactly one is fed real content each frame, so
    // the other fades itself out rather than lingering.
    const bool focus =
        !tooltip.empty() &&
        tooltip.emphasis == eng::ui::TooltipEmphasis::Focus;
    mBanner.update(focus ? tooltip : eng::ui::TooltipContent{}, dt);
    mTooltip.update(focus ? eng::ui::TooltipContent{} : tooltip, dt);
    eng::ui::TooltipStyle tooltipStyle = mStyle.tooltip;
    tooltipStyle.maxWidth = viewport.tooltipWidth;
    tooltipStyle.minWidth = std::min(tooltipStyle.minWidth,
                                     tooltipStyle.maxWidth);
    mTooltip.configure(tooltipStyle);
    eng::ui::TargetBannerStyle bannerStyle = mStyle.banner;
    bannerStyle.maxWidth = std::min(bannerStyle.maxWidth,
                                    viewport.safe.size.x);
    bannerStyle.minWidth = std::min(bannerStyle.minWidth,
                                    bannerStyle.maxWidth);
    mBanner.configure(bannerStyle);
    mBanner.draw(mCanvas, viewport.safe);

    eng::ui::UiRect tooltipSafe = viewport.safe;
    tooltipSafe.size.y = std::max(
        1, statusY - viewport.gap - tooltipSafe.position.y);
    // The region notice steps clear of whatever rect the tooltip claimed; the
    // banner is at the top edge and never contends for that band.
    drawRegionNotice(mTooltip.draw(mCanvas, {}, tooltipSafe), viewport.safe);
}

} // namespace game
