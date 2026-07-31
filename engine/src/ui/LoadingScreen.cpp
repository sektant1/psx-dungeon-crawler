#include <eng/ui/LoadingScreen.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace eng::ui {
namespace {

// Blend two 0xAABBGGRR colours. Used for the rune glow so the ring fades
// through the palette instead of popping between two fixed colours.
unsigned int mix(unsigned int a, unsigned int b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    unsigned int out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const float ca = float((a >> shift) & 0xFFu);
        const float cb = float((b >> shift) & 0xFFu);
        const auto c = static_cast<unsigned int>(
            std::lround(ca + (cb - ca) * t));
        out |= (c & 0xFFu) << shift;
    }
    return out;
}

unsigned int withAlpha(unsigned int colour, float alpha)
{
    const auto a = static_cast<unsigned int>(
        std::lround(std::clamp(alpha, 0.0f, 1.0f) * 255.0f));
    return (colour & 0x00FFFFFFu) | (a << 24);
}

constexpr int kRuneCount = 12;

} // namespace

LoadingRune loadingRune(int index, int count, float time, int radius)
{
    count = std::max(1, count);
    const float step = 6.2831853f / float(count);
    const float angle = step * float(index) - 1.5707963f;
    LoadingRune rune;
    rune.x = int(std::lround(std::cos(angle) * float(radius)));
    rune.y = int(std::lround(std::sin(angle) * float(radius)));
    // A comet head travelling the ring: brightness is the angular distance
    // behind the head, so one rune is hot and the trail decays.
    const float head = std::fmod(time * 1.6f, 1.0f) * float(count);
    float behind = head - float(index);
    if (behind < 0.0f)
        behind += float(count);
    rune.glow = std::max(0.0f, 1.0f - behind / float(count) * 2.0f);
    return rune;
}

float loadingFlicker(float time)
{
    const float a = std::sin(time * 11.0f);
    const float b = std::sin(time * 4.3f + 1.7f);
    return std::clamp(0.72f + 0.18f * a + 0.10f * b, 0.0f, 1.0f);
}

void drawLoadingScreen(const UiCanvas& canvas, const LoadingView& view)
{
    const UiPalette& pal = canvas.palette();
    const glm::ivec2 size = canvas.size();
    const glm::ivec2 centre = size / 2;
    const float flicker = loadingFlicker(view.time);

    // Backdrop: opaque, because behind it is whatever the renderer last drew
    // (usually nothing at all) and a half-lit loading screen reads as a bug.
    canvas.rect({0, 0}, size, 0xFF05070AU);
    // Torch glow behind the ring. Drawn as concentric squares, so the bands
    // have to stay faint: any more alpha and the corners read as the boxes
    // they are instead of as light. The flicker is what sells it, not the
    // shape.
    for (int band = 5; band >= 1; --band) {
        const glm::ivec2 half = {size.x * band / 16, size.y * band / 16};
        canvas.rect(centre - half, half * 2, withAlpha(pal.inkSoft, 0.05f * flicker));
    }

    const int ringRadius = std::max(18, std::min(size.x, size.y) / 8);
    const int runeSize = std::max(3, ringRadius / 7);
    for (int i = 0; i < kRuneCount; ++i) {
        const LoadingRune rune = loadingRune(i, kRuneCount, view.time, ringRadius);
        const unsigned int colour =
            withAlpha(mix(pal.edge, pal.accent, rune.glow),
                      0.35f + 0.65f * rune.glow * flicker);
        const int grow = int(std::lround(rune.glow * float(runeSize) * 0.5f));
        canvas.rect({centre.x + rune.x - (runeSize + grow) / 2,
                     centre.y + rune.y - (runeSize + grow) / 2},
                    {runeSize + grow, runeSize + grow}, colour);
    }

    const int line = canvas.lineHeight();
    if (!view.title.empty())
        canvas.text({centre.x, centre.y - ringRadius - line * 3}, view.title,
                    withAlpha(pal.accent, 0.75f + 0.25f * flicker),
                    Align::Centre);

    // Progress bar sits under the ring, wide but capped so it does not stretch
    // across an ultrawide window.
    const int barW = std::min(size.x - 40, 240);
    const int barH = std::max(7, line / 2);
    const glm::ivec2 barAt{centre.x - barW / 2, centre.y + ringRadius + line * 2};
    canvas.bar(barAt, {barW, barH}, view.progress, pal.accent, pal.inkSoft);

    if (!view.step.empty())
        canvas.text({centre.x, barAt.y + barH + 4}, view.step, pal.text,
                    Align::Centre);

    if (view.total > 0) {
        char counter[64];
        std::snprintf(counter, sizeof(counter), "%d / %d  %d%%", view.completed,
                      view.total,
                      int(std::lround(std::clamp(view.progress, 0.0f, 1.0f) *
                                      100.0f)));
        canvas.text({centre.x, barAt.y - line - 2}, counter, pal.textDim,
                    Align::Centre);
    }

    if (!view.hint.empty())
        canvas.text({centre.x, size.y - line * 2}, view.hint, pal.textDim,
                    Align::Centre);
}

} // namespace eng::ui
