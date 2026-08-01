#!/usr/bin/env python3
"""Generate the placeholder particle textures in assets/engine/particles/textures/.

    tools/gen_particle_textures.py

These are programmer art, not shipped art: hard-edged, few colours, sized for a
PSX-era look. They exist so the texture import path (drop a PNG -> the engine
generates `Particles/Auto/<stem>` at boot) is exercised by real files instead of
staying theoretical, and so blood decals stop rendering as flat quads.

Two properties are deliberate.

*Chunky pixels.* Every texture is authored at half its final resolution and
nearest-upscaled by 2. Nothing can accidentally acquire a smooth photographic
gradient: the smallest feature is a 2x2 block even before the runtime's point
filtering magnifies it further.

*Quantised alpha.* Coverage is snapped to a handful of levels per texture. A
particle sprite with a soft 8-bit falloff reads as a modern smoke puff; banding
it to four steps reads as a sprite.

Blending, per `assets/engine/materials/particles.material` and
`ParticleMaterials::buildMaterial`: alpha materials use SBT_TRANSPARENT_ALPHA
(src_alpha, 1-src_alpha), so alpha art is *straight*, never premultiplied.
Additive materials use SBT_ADD (one, one), which ignores alpha entirely, so
additive art must encode its falloff in RGB brightness and fade to pure black at
the edges. This script does both: alpha textures keep full-strength RGB under a
varying alpha, additive textures darken RGB and mirror the same curve into alpha
purely so the shader's `alphaScissor` can still discard dead pixels.

Re-running reproduces byte-identical files; all randomness comes from SEED.
"""

from __future__ import annotations

import math
import random
from pathlib import Path

from PIL import Image

SEED = 0x50535831  # "PSX1"

OUT_DIR = Path(__file__).resolve().parent.parent / "assets/engine/particles/textures"

UPSCALE = 2

Palette = list[tuple[int, int, int]]


# --------------------------------------------------------------------- util --

def quantise(value: float, levels: int) -> int:
    """Snap 0..1 coverage to a level index in 0..levels-1 (0 = empty)."""
    if value <= 0.0:
        return 0
    return max(0, min(levels - 1, int(value * levels)))


def new_frame(size: int) -> list[list[tuple[int, int, int, int]]]:
    return [[(0, 0, 0, 0)] * size for _ in range(size)]


def to_image(frames: list[list[list[tuple[int, int, int, int]]]],
             cols: int, rows: int, size: int) -> Image.Image:
    """Compose frames into an atlas and nearest-upscale it."""
    atlas = Image.new("RGBA", (cols * size, rows * size), (0, 0, 0, 0))
    for index, frame in enumerate(frames):
        tile = Image.new("RGBA", (size, size))
        tile.putdata([px for row in frame for px in row])
        atlas.paste(tile, ((index % cols) * size, (index // cols) * size))
    return atlas.resize((atlas.width * UPSCALE, atlas.height * UPSCALE),
                        Image.NEAREST)


def additive(colour: tuple[int, int, int], level: float
             ) -> tuple[int, int, int, int]:
    """Additive pixel: falloff lives in RGB, alpha only feeds the scissor."""
    if level <= 0.0:
        return (0, 0, 0, 0)
    r, g, b = colour
    return (int(r * level), int(g * level), int(b * level),
            max(1, int(255 * level)))


def straight(colour: tuple[int, int, int], alpha: float
             ) -> tuple[int, int, int, int]:
    """Straight-alpha pixel: RGB stays at full strength under a varying alpha."""
    if alpha <= 0.0:
        return (0, 0, 0, 0)
    return (colour[0], colour[1], colour[2], max(1, int(255 * alpha)))


# ------------------------------------------------------------------ spark ----

SPARK_PALETTE: Palette = [(255, 255, 236), (255, 226, 138), (255, 150, 46)]


def gen_spark(size: int = 16) -> Image.Image:
    """Four-pointed spark: two crossed tapering arms plus a hot core."""
    frame = new_frame(size)
    c = (size - 1) / 2.0
    for y in range(size):
        for x in range(size):
            dx, dy = abs(x - c), abs(y - c)
            # A star, not a disc: the product of the two axis distances is small
            # along either axis and large in the diagonals, which carves the
            # four points out without any explicit polygon.
            arm = 1.0 - (dx * dy) / (c * c * 0.30) - (dx + dy) / (c * 2.4)
            level = quantise(arm, 4) / 3.0
            if level <= 0.0:
                continue
            shade = SPARK_PALETTE[min(2, int((1.0 - level) * 3))]
            frame[y][x] = additive(shade, level)
    return to_image([frame], 1, 1, size)


# ------------------------------------------------------------------ ember ----

EMBER_PALETTE: Palette = [(255, 249, 214), (255, 196, 84), (240, 112, 30),
                          (150, 40, 12)]


def gen_ember(size: int = 16) -> Image.Image:
    """Round mote: concentric hard rings, hottest in the middle."""
    frame = new_frame(size)
    c = (size - 1) / 2.0
    radius = c + 0.5
    for y in range(size):
        for x in range(size):
            d = math.hypot(x - c, y - c) / radius
            if d > 1.0:
                continue
            band = min(3, int(d * 4.0))
            level = (1.0, 0.82, 0.55, 0.28)[band]
            frame[y][x] = additive(EMBER_PALETTE[band], level)
    return to_image([frame], 1, 1, size)


# ------------------------------------------------------------------ smoke ----

SMOKE_PALETTE: Palette = [(206, 204, 200), (168, 166, 163), (128, 127, 126),
                          (92, 92, 94)]


def gen_smoke(size: int = 32) -> Image.Image:
    """Puff built from overlapping lobes, then banded into four alpha steps."""
    rng = random.Random(SEED + 1)
    c = (size - 1) / 2.0
    lobes = [(c, c, c * 0.62)]
    for _ in range(6):
        angle = rng.uniform(0.0, math.tau)
        dist = rng.uniform(c * 0.18, c * 0.40)
        lobes.append((c + math.cos(angle) * dist,
                      c + math.sin(angle) * dist,
                      rng.uniform(c * 0.28, c * 0.46)))

    frame = new_frame(size)
    for y in range(size):
        for x in range(size):
            # Max of the lobes, not a sum: a sum would smooth the union back
            # into a disc and lose the lumpy silhouette.
            cover = 0.0
            for lx, ly, lr in lobes:
                d = math.hypot(x - lx, y - ly)
                cover = max(cover, 1.0 - d / lr)
            band = quantise(cover, 5)
            if band == 0:
                continue
            alpha = (0.0, 0.22, 0.42, 0.62, 0.80)[band]
            frame[y][x] = straight(SMOKE_PALETTE[4 - band], alpha)
    return to_image([frame], 1, 1, size)


# ------------------------------------------------------------- blood_drop ----

BLOOD_DARK = (58, 6, 6)
BLOOD_MID = (110, 12, 10)
BLOOD_LIT = (168, 30, 24)
BLOOD_HILITE = (222, 96, 82)


def gen_blood_drop(size: int = 16) -> Image.Image:
    """Teardrop: a disc low in the cell tapering to a point at the top."""
    frame = new_frame(size)
    cx = (size - 1) / 2.0
    bulb_y = size * 0.62
    bulb_r = size * 0.28
    for y in range(size):
        for x in range(size):
            inside = math.hypot(x - cx, y - bulb_y) <= bulb_r
            if not inside and y < bulb_y:
                # Tail: half-width shrinks linearly from the bulb to the tip.
                t = (bulb_y - y) / (bulb_y - size * 0.08)
                if 0.0 <= t <= 1.0 and abs(x - cx) <= bulb_r * (1.0 - t) + 0.2:
                    inside = True
            if not inside:
                continue
            # Shade by height so the drop reads as a volume, in three flat steps.
            up = (bulb_y - y) / size
            shade = BLOOD_LIT if up > 0.16 else (
                BLOOD_MID if y < bulb_y + bulb_r * 0.4 else BLOOD_DARK)
            frame[y][x] = straight(shade, 1.0)
    # A two-pixel specular, upper-left, the classic sprite-art cheat for "wet".
    hx, hy = int(cx - bulb_r * 0.45), int(bulb_y - bulb_r * 0.45)
    for px, py in ((hx, hy), (hx + 1, hy), (hx, hy + 1)):
        if 0 <= px < size and 0 <= py < size and frame[py][px][3]:
            frame[py][px] = straight(BLOOD_HILITE, 1.0)
    return to_image([frame], 1, 1, size)


# ------------------------------------------------------------ blood_splat ----

def gen_blood_splat(size: int = 32) -> Image.Image:
    """Irregular splatter: a wobbled central mass plus scattered satellites."""
    rng = random.Random(SEED + 2)
    c = (size - 1) / 2.0

    # Per-angle radius, so the core has a ragged but closed outline.
    spokes = 16
    radii = [c * rng.uniform(0.44, 0.74) for _ in range(spokes)]

    satellites = []
    for _ in range(9):
        angle = rng.uniform(0.0, math.tau)
        dist = rng.uniform(c * 0.62, c * 0.96)
        satellites.append((c + math.cos(angle) * dist,
                           c + math.sin(angle) * dist,
                           rng.uniform(0.9, 2.6)))

    frame = new_frame(size)
    for y in range(size):
        for x in range(size):
            dx, dy = x - c, y - c
            d = math.hypot(dx, dy)
            a = (math.atan2(dy, dx) % math.tau) / math.tau * spokes
            i0 = int(a) % spokes
            i1 = (i0 + 1) % spokes
            f = a - int(a)
            edge = radii[i0] * (1.0 - f) + radii[i1] * f

            inside = d <= edge
            if not inside:
                for sx, sy, sr in satellites:
                    if math.hypot(x - sx, y - sy) <= sr:
                        inside = True
                        break
            if not inside:
                continue
            # Darker rim, lighter middle: two flat tones, no gradient.
            shade = BLOOD_MID if d < edge * 0.55 else BLOOD_DARK
            frame[y][x] = straight(shade, 1.0 if d < edge * 0.85 else 0.72)
    return to_image([frame], 1, 1, size)


# ------------------------------------------------------------ flame_sheet ----

FLAME_PALETTE: Palette = [(255, 252, 226), (255, 214, 96), (247, 134, 32),
                          (176, 46, 14)]


def gen_flame_sheet(size: int = 16, cols: int = 4, rows: int = 4) -> Image.Image:
    """4x4 looping flame flipbook, frames left-to-right then top-to-bottom.

    The loop closes because every term is a function of `phase` in [0, 1) fed
    through whole-turn sines: frame 15 hands back to frame 0 with no pop.
    """
    rng = random.Random(SEED + 3)
    count = cols * rows
    # Fixed per-flame noise, shared by all frames, so the flame keeps an
    # identity while it flickers instead of reshuffling every frame.
    wobble = [rng.uniform(0.0, math.tau) for _ in range(4)]

    frames = []
    for index in range(count):
        phase = index / count
        frame = new_frame(size)
        cx = (size - 1) / 2.0
        for y in range(size):
            # 0 at the base, 1 at the tip.
            up = 1.0 - y / (size - 1)
            # Body profile: wide and stable at the base, pinched at the tip.
            width = (size * 0.30) * math.sin(math.pi * min(1.0, up * 0.92 + 0.08))
            width *= 1.0 + 0.18 * math.sin(math.tau * phase + wobble[0])
            # Lateral sway increases with height, like a real plume.
            sway = up * up * size * 0.22 * math.sin(
                math.tau * (phase + up * 0.5) + wobble[1])
            # Keep the silhouette off the cell border. A flipbook frame that
            # touches its edge reads as a flame sliced flat, and with point
            # sampling it also sits one texel from its neighbour in the atlas.
            margin = max(0.0, cx - 1.0 - width)
            sway = max(-margin, min(margin, sway))
            # Flicker in the tip height only; the base stays planted.
            tip = 0.86 + 0.14 * math.sin(math.tau * phase + wobble[2])
            if up > tip:
                continue
            for x in range(size):
                dx = abs(x - cx - sway)
                if width <= 0.0 or dx > width:
                    continue
                # Distance to the flame's own edge drives the colour bands:
                # white core, yellow, orange, red rim.
                inner = 1.0 - dx / width
                heat = inner * (1.0 - up * 0.72)
                heat *= 1.0 + 0.12 * math.sin(math.tau * (phase * 2.0) + wobble[3])
                band = 3 - quantise(heat, 4)
                level = (1.0, 0.90, 0.72, 0.45)[band]
                frame[y][x] = additive(FLAME_PALETTE[band], level)
        frames.append(frame)
    return to_image(frames, cols, rows, size)


# ---------------------------------------------------------------------- main --

GENERATORS = {
    "spark": gen_spark,
    "ember": gen_ember,
    "smoke": gen_smoke,
    "blood_drop": gen_blood_drop,
    "blood_splat": gen_blood_splat,
    "flame_sheet": gen_flame_sheet,
}


def main() -> int:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for stem, generate in sorted(GENERATORS.items()):
        image = generate()
        path = OUT_DIR / f"{stem}.png"
        # optimize + no ancillary chunks: PNG output must not carry a timestamp
        # or the files would differ byte-for-byte between runs.
        image.save(path, "PNG", optimize=True)
        print(f"{path.name:20s} {image.width}x{image.height}  "
              f"{path.stat().st_size} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
