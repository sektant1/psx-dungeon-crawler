#include "GameHud.h"

#include <eng/Config.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace game {
namespace {

struct Palette {
    ImVec4 ink;
    ImVec4 panel;
    ImVec4 iron;
    ImVec4 bone;
    ImVec4 muted;
    ImVec4 blood;
    ImVec4 ember;
    ImVec4 astral;
    ImVec4 quicksilver;
};

Palette palette(bool highContrast, float opacity)
{
    if (highContrast) {
        return {{0.0f, 0.0f, 0.0f, 1.0f},
                {0.0f, 0.0f, 0.0f, opacity},
                {0.66f, 0.66f, 0.70f, 1.0f},
                {1.0f, 1.0f, 1.0f, 1.0f},
                {0.82f, 0.82f, 0.82f, 1.0f},
                {1.0f, 0.18f, 0.24f, 1.0f},
                {1.0f, 0.72f, 0.18f, 1.0f},
                {0.48f, 0.72f, 1.0f, 1.0f},
                {0.72f, 1.0f, 0.92f, 1.0f}};
    }
    return {{0.025f, 0.025f, 0.035f, 1.0f},
            {0.035f, 0.032f, 0.045f, opacity},
            {0.25f, 0.24f, 0.29f, 0.96f},
            {0.84f, 0.78f, 0.62f, 1.0f},
            {0.54f, 0.52f, 0.50f, 1.0f},
            {0.58f, 0.12f, 0.18f, 1.0f},
            {0.86f, 0.49f, 0.12f, 1.0f},
            {0.36f, 0.43f, 0.70f, 1.0f},
            {0.58f, 0.72f, 0.68f, 1.0f}};
}

ImU32 colour(ImVec4 c, float alpha = 1.0f)
{
    c.w *= std::clamp(alpha, 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32(c);
}

ImVec2 textSize(float size, const char* value)
{
    return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, value);
}

void text(ImDrawList* draw, ImVec2 at, float size, ImU32 tint,
          const char* value)
{
    draw->AddText(ImGui::GetFont(), size, at, tint, value);
}

void rightText(ImDrawList* draw, float right, float y, float size,
               ImU32 tint, const char* value)
{
    const ImVec2 measured = textSize(size, value);
    text(draw, {right - measured.x, y}, size, tint, value);
}

void centredText(ImDrawList* draw, float centre, float y, float size,
                  ImU32 tint, const char* value)
{
    const ImVec2 measured = textSize(size, value);
    text(draw, {centre - measured.x * 0.5f, y}, size, tint, value);
}

void notchedPanel(ImDrawList* draw, ImVec2 min, ImVec2 max, float cut,
                  ImU32 fill, ImU32 border, float thickness = 1.0f)
{
    const ImVec2 points[] = {
        {min.x + cut, min.y}, {max.x - cut, min.y}, {max.x, min.y + cut},
        {max.x, max.y - cut}, {max.x - cut, max.y}, {min.x + cut, max.y},
        {min.x, max.y - cut}, {min.x, min.y + cut}};
    draw->AddConvexPolyFilled(points, IM_ARRAYSIZE(points), fill);
    draw->AddPolyline(points, IM_ARRAYSIZE(points), border,
                      ImDrawFlags_Closed, thickness);
}

void drawBar(ImDrawList* draw, ImVec2 min, ImVec2 max, float ratio,
             ImU32 empty, ImU32 fill)
{
    draw->AddRectFilled(min, max, empty);
    ratio = std::clamp(ratio, 0.0f, 1.0f);
    if (ratio <= 0.0f)
        return;
    const float right = min.x + (max.x - min.x) * ratio;
    const float cut = std::min(4.0f, std::max(0.0f, right - min.x));
    const ImVec2 shape[] = {{min.x, min.y}, {right, min.y},
                            {right - cut, max.y}, {min.x, max.y}};
    draw->AddConvexPolyFilled(shape, IM_ARRAYSIZE(shape), fill);
}

const char* roman(HudWeapon weapon)
{
    switch (weapon) {
        case HudWeapon::Blade: return "I";
        case HudWeapon::Staff: return "II";
        case HudWeapon::Torch: return "III";
        default: return "-";
    }
}

const char* actionName(ActionPhase action)
{
    switch (action) {
        case ActionPhase::Idle: return "READY";
        case ActionPhase::Windup: return "COMMITTED";
        case ActionPhase::Active: return "STRIKE";
        case ActionPhase::Recovery: return "RECOVER";
        case ActionPhase::Deflecting: return "DEFLECT";
        case ActionPhase::Dodging: return "EVADE";
        case ActionPhase::Staggered: return "STAGGERED";
    }
    return "READY";
}

ImVec4 actionColour(const Palette& p, ActionPhase action)
{
    switch (action) {
        case ActionPhase::Active: return p.ember;
        case ActionPhase::Deflecting: return p.quicksilver;
        case ActionPhase::Dodging: return p.astral;
        case ActionPhase::Staggered: return p.blood;
        case ActionPhase::Windup: return p.ember;
        case ActionPhase::Recovery: return p.muted;
        case ActionPhase::Idle: return p.bone;
    }
    return p.bone;
}

std::string firstBinding(const eng::Config& config, const char* action,
                         const char* fallback)
{
    const auto found = config.bindings().find(action);
    if (found == config.bindings().end() || found->second.empty())
        return fallback;
    return found->second.front();
}

void drawReticle(ImDrawList* draw, ImVec2 centre, float s, const Palette& p,
                 const HudSnapshot& snapshot)
{
    const ImVec4 state = actionColour(p, snapshot.action);
    const ImU32 shadow = colour(p.ink, 0.9f);
    const ImU32 line = colour(state);
    const float gap = 5.0f * s;
    const float reach = 11.0f * s;
    const float thick = std::max(1.0f, 1.25f * s);
    const std::array<std::pair<ImVec2, ImVec2>, 4> strokes{{
        {ImVec2(centre.x - reach, centre.y),
         ImVec2(centre.x - gap, centre.y)},
        {ImVec2(centre.x + gap, centre.y),
         ImVec2(centre.x + reach, centre.y)},
        {ImVec2(centre.x, centre.y - reach),
         ImVec2(centre.x, centre.y - gap)},
        {ImVec2(centre.x, centre.y + gap),
         ImVec2(centre.x, centre.y + reach)},
    }};
    for (const auto& stroke : strokes) {
        draw->AddLine(stroke.first, stroke.second, shadow, thick + 2.0f);
        draw->AddLine(stroke.first, stroke.second, line, thick);
    }

    if (snapshot.action == ActionPhase::Deflecting) {
        const float r = 4.0f * s;
        const ImVec2 diamond[] = {{centre.x, centre.y - r},
                                  {centre.x + r, centre.y},
                                  {centre.x, centre.y + r},
                                  {centre.x - r, centre.y}};
        draw->AddPolyline(diamond, IM_ARRAYSIZE(diamond), line,
                          ImDrawFlags_Closed, thick);
    } else if (snapshot.action == ActionPhase::Dodging) {
        draw->AddCircle(centre, 4.0f * s, line, 8, thick);
    } else if (snapshot.action == ActionPhase::Staggered) {
        const float r = 3.0f * s;
        draw->AddLine({centre.x - r, centre.y - r},
                      {centre.x + r, centre.y + r}, line, thick);
        draw->AddLine({centre.x + r, centre.y - r},
                      {centre.x - r, centre.y + r}, line, thick);
    } else if (snapshot.action == ActionPhase::Active) {
        draw->AddCircleFilled(centre, 2.0f * s, line, 8);
    }

    if (snapshot.interaction.available) {
        const float y = centre.y + 18.0f * s;
        const float r = 2.5f * s;
        const ImVec2 marker[] = {{centre.x, y - r}, {centre.x + r, y},
                                 {centre.x, y + r}, {centre.x - r, y}};
        draw->AddConvexPolyFilled(marker, IM_ARRAYSIZE(marker),
                                  colour(p.ember));
    }
}

void drawVitals(ImDrawList* draw, ImVec2 display, float s, const Palette& p,
                const HudSnapshot& snapshot, bool showNumbers)
{
    const float width = 242.0f * s;
    const float height = 108.0f * s;
    const ImVec2 min{22.0f * s, display.y - 22.0f * s - height};
    const ImVec2 max{min.x + width, min.y + height};
    notchedPanel(draw, min, max, 7.0f * s, colour(p.panel), colour(p.iron),
                 std::max(1.0f, s));
    draw->AddRectFilled({min.x + 6.0f * s, min.y + 8.0f * s},
                        {min.x + 8.0f * s, max.y - 8.0f * s},
                        colour(p.blood));

    const float labelSize = 10.0f * s;
    const float valueSize = 9.0f * s;
    text(draw, {min.x + 15.0f * s, min.y + 8.0f * s}, labelSize,
         colour(p.bone), "CONDITION");

    struct Row {
        const char* label;
        const HudResource* resource;
        ImVec4 tint;
        float y;
    };
    const Row rows[] = {{"HEALTH", &snapshot.health, p.blood, 29.0f},
                        {"STAMINA", &snapshot.stamina, p.quicksilver, 51.0f},
                        {"MANA", &snapshot.mana, p.astral, 73.0f}};
    for (const Row& row : rows) {
        const float y = min.y + row.y * s;
        text(draw, {min.x + 15.0f * s, y - 2.0f * s}, valueSize,
             colour(p.muted), row.label);
        drawBar(draw, {min.x + 70.0f * s, y},
                {min.x + 182.0f * s, y + 7.0f * s},
                hudResourceRatio(*row.resource), colour(p.iron, 0.9f),
                colour(row.tint));
        if (showNumbers && row.resource->available) {
            char value[24];
            std::snprintf(value, sizeof(value), "%d/%d",
                          int(std::ceil(std::max(0.0f, row.resource->current))),
                          int(std::ceil(std::max(0.0f, row.resource->maximum))));
            rightText(draw, max.x - 12.0f * s, y - 3.0f * s, valueSize,
                      colour(p.bone), value);
        }
    }

    const ImVec4 state = actionColour(p, snapshot.action);
    text(draw, {min.x + 15.0f * s, max.y - 17.0f * s}, valueSize,
         colour(state), actionName(snapshot.action));
    const float poise = hudResourceRatio(snapshot.poise);
    draw->AddRectFilled({min.x + 84.0f * s, max.y - 13.0f * s},
                        {max.x - 12.0f * s, max.y - 11.0f * s},
                        colour(p.iron));
    draw->AddRectFilled({min.x + 84.0f * s, max.y - 13.0f * s},
                        {min.x + (84.0f + 146.0f * poise) * s,
                         max.y - 11.0f * s},
                        colour(p.ember));

    float chipY = min.y - 7.0f * s;
    for (int i = snapshot.statusCount - 1; i >= 0; --i) {
        const char* status = hudStatusName(snapshot.statuses[size_t(i)].kind);
        const float chipW = textSize(valueSize, status).x + 18.0f * s;
        const ImVec2 chipMin{min.x, chipY - 17.0f * s};
        const ImVec2 chipMax{min.x + chipW, chipY};
        notchedPanel(draw, chipMin, chipMax, 3.0f * s, colour(p.panel),
                     colour(p.ember));
        text(draw, {chipMin.x + 9.0f * s, chipMin.y + 3.0f * s}, valueSize,
             colour(p.ember), status);
        chipY -= 20.0f * s;
    }
}

void drawWeapon(ImDrawList* draw, ImVec2 display, float s, const Palette& p,
                const HudSnapshot& snapshot, const std::string& swapKey,
                float flash)
{
    const float width = 218.0f * s;
    const float height = 80.0f * s;
    const ImVec2 min{display.x - 22.0f * s - width,
                     display.y - 22.0f * s - height};
    const ImVec2 max{min.x + width, min.y + height};
    const ImVec4 accent = flash > 0.0f ? p.ember : p.iron;
    notchedPanel(draw, min, max, 7.0f * s, colour(p.panel), colour(accent),
                 std::max(1.0f, s));
    draw->AddTriangleFilled({max.x - 28.0f * s, min.y},
                            {max.x, min.y}, {max.x, min.y + 28.0f * s},
                            colour(p.ember, 0.75f));
    text(draw, {min.x + 13.0f * s, min.y + 9.0f * s}, 9.0f * s,
         colour(p.muted), "ARMAMENT");
    rightText(draw, max.x - 13.0f * s, min.y + 9.0f * s, 10.0f * s,
              colour(p.ink), roman(snapshot.weapon));
    text(draw, {min.x + 13.0f * s, min.y + 27.0f * s}, 15.0f * s,
         colour(p.bone), hudWeaponName(snapshot.weapon));
    text(draw, {min.x + 13.0f * s, min.y + 49.0f * s}, 9.0f * s,
         colour(p.quicksilver), hudWeaponDiscipline(snapshot.weapon));
    std::string change = swapKey + "  CHANGE";
    rightText(draw, max.x - 13.0f * s, max.y - 18.0f * s, 9.0f * s,
              colour(p.muted), change.c_str());
}

void drawInteraction(ImDrawList* draw, ImVec2 display, float s,
                     const Palette& p, const InteractionFocus& focus,
                     const std::string& interactKey)
{
    if (!focus.available)
        return;
    const char* action = hudInteractionAction(focus);
    const float textPx = 11.0f * s;
    const float keyWidth = std::max(27.0f * s,
                                    textSize(textPx, interactKey.c_str()).x +
                                        13.0f * s);
    const float width = keyWidth + textSize(textPx, action).x + 35.0f * s;
    const float height = 32.0f * s;
    const float centre = display.x * 0.5f;
    const ImVec2 min{centre - width * 0.5f, display.y * 0.61f};
    const ImVec2 max{min.x + width, min.y + height};
    notchedPanel(draw, min, max, 5.0f * s, colour(p.panel), colour(p.iron),
                 std::max(1.0f, s));
    const ImVec2 keyMin{min.x + 5.0f * s, min.y + 5.0f * s};
    const ImVec2 keyMax{keyMin.x + keyWidth, max.y - 5.0f * s};
    notchedPanel(draw, keyMin, keyMax, 3.0f * s, colour(p.ember),
                 colour(p.bone));
    centredText(draw, (keyMin.x + keyMax.x) * 0.5f, keyMin.y + 4.0f * s,
                 textPx, colour(p.ink), interactKey.c_str());
    text(draw, {keyMax.x + 12.0f * s, min.y + 9.0f * s}, textPx,
         colour(p.bone), action);
}

void drawRegionNotice(ImDrawList* draw, ImVec2 display, float s,
                      const Palette& p, const std::string& title,
                      const std::string& subtitle, float alpha)
{
    if (title.empty() || alpha <= 0.0f)
        return;
    const float centre = display.x * 0.5f;
    const float width = std::max(textSize(18.0f * s, title.c_str()).x,
                                 textSize(10.0f * s, subtitle.c_str()).x) +
                        54.0f * s;
    const float y = 28.0f * s;
    draw->AddLine({centre - width * 0.5f, y + 17.0f * s},
                  {centre - width * 0.5f + 18.0f * s, y + 17.0f * s},
                  colour(p.ember, alpha), std::max(1.0f, s));
    draw->AddLine({centre + width * 0.5f - 18.0f * s, y + 17.0f * s},
                  {centre + width * 0.5f, y + 17.0f * s},
                  colour(p.ember, alpha), std::max(1.0f, s));
    centredText(draw, centre, y, 18.0f * s, colour(p.bone, alpha),
                 title.c_str());
    centredText(draw, centre, y + 23.0f * s, 10.0f * s,
                 colour(p.muted, alpha), subtitle.c_str());
}

} // namespace

void GameHud::configure(const eng::Config& config)
{
    mScale = std::clamp(float(config.getNumber("hud.scale", 1.0)), 0.75f,
                        2.0f);
    mOpacity = std::clamp(float(config.getNumber("hud.opacity", 0.92)),
                          0.35f, 1.0f);
    mHighContrast = config.getBool("hud.high_contrast", false);
    mReducedMotion = config.getBool("hud.reduced_motion", false);
    mShowCrosshair = config.getBool("hud.crosshair", true);
    mShowNumbers = config.getBool("hud.numeric_values", true);
    mInteractKey = firstBinding(config, "interact", "E");
    mSwapKey = firstBinding(config, "swap_weapon", "X");
}

void GameHud::notifyRegion(HudRegion region)
{
    if (region == HudRegion::Threshold) {
        mRegionTitle = "THE THRESHOLD";
        mRegionSubtitle = "A KNOWN WAY REMAINS BEHIND YOU";
    } else {
        mRegionTitle = "THE INTERIOR";
        mRegionSubtitle = "THE WAY FORWARD IS NOT YET KNOWN";
    }
    mRegionNotice = 4.0f;
}

void GameHud::draw(const HudSnapshot& snapshot, float dt, bool visible)
{
    if (!std::isfinite(dt) || dt < 0.0f)
        dt = 0.0f;
    mWeaponFlash = std::max(0.0f, mWeaponFlash - dt);
    mRegionNotice = std::max(0.0f, mRegionNotice - dt);
    if (snapshot.valid && snapshot.weapon != mLastWeapon) {
        if (mLastWeapon != HudWeapon::Unknown)
            mWeaponFlash = mReducedMotion ? 0.0f : 0.8f;
        mLastWeapon = snapshot.weapon;
    }
    if (!visible || !snapshot.valid)
        return;

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float fit = std::clamp(
        std::min(display.x / 960.0f, display.y / 720.0f), 0.80f, 1.50f);
    const float s = mScale * fit;
    const Palette p = palette(mHighContrast, mOpacity);

    drawVitals(draw, display, s, p, snapshot, mShowNumbers);
    drawWeapon(draw, display, s, p, snapshot, mSwapKey, mWeaponFlash);
    if (mShowCrosshair)
        drawReticle(draw, {display.x * 0.5f, display.y * 0.5f}, s, p,
                    snapshot);
    drawInteraction(draw, display, s, p, snapshot.interaction, mInteractKey);

    float noticeAlpha = 1.0f;
    if (!mReducedMotion && mRegionNotice < 0.6f)
        noticeAlpha = mRegionNotice / 0.6f;
    drawRegionNotice(draw, display, s, p, mRegionTitle, mRegionSubtitle,
                     mRegionNotice > 0.0f ? noticeAlpha : 0.0f);
}

} // namespace game
