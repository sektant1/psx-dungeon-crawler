#!/usr/bin/env python3
"""Generate the placeholder first-person sprite sheets in assets/textures/viewmodels/.

    tools/gen_viewmodel_sprites.py

These are programmer art for the sprite viewmodel path (docs/fps-viewmodel.md).
They exist so that path is exercised by real files rather than staying
theoretical, and so an author can see the layer composition -- hands under
weapon under muzzle flash -- before any real art exists.

Three properties are deliberate, and they match tools/gen_particle_textures.py
so the two sets look like they belong to one game.

*Chunky pixels.* Everything is authored at half its final resolution and
nearest-upscaled by 2, so the smallest feature is a 2x2 block before the
runtime's point filtering magnifies it further. Nothing can accidentally
acquire a smooth gradient.

*Few colours, hard edges.* A shape reads by silhouette, which is what the
reference art (docs/references/fps_viewmodel_reference.png) does and what
survives being 200 pixels tall on screen.

*Straight alpha, quantised.* The sprite materials blend src_alpha/1-src_alpha,
so alpha is straight and never premultiplied. Coverage is binary here: a sprite
with a soft 8-bit edge reads as a modern cutout, a hard one reads as a sprite.

Sheets are laid out as a grid of cells, row-major, which is what
ViewmodelSpriteLayer's `grid` / `idle_frame` / `fire_frame` describe.

Re-running reproduces byte-identical files -- there is no randomness.

Usage:  python3 tools/gen_viewmodel_sprites.py [--dry-run]
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = PROJECT_ROOT / "assets" / "textures" / "viewmodels"

# --- the palette ------------------------------------------------------------
# Deliberately tiny. Every sheet draws from it, so the hands and the weapon
# already look like they came from the same hand before anyone paints anything.
CLEAR = (0, 0, 0, 0)
GLOVE_DARK = (38, 30, 52, 255)
GLOVE_MID = (68, 55, 88, 255)
GLOVE_LIT = (104, 88, 128, 255)
SKIN = (196, 148, 112, 255)
SKIN_SHADE = (140, 100, 76, 255)
METAL_DARK = (44, 46, 58, 255)
METAL_MID = (92, 98, 116, 255)
METAL_LIT = (150, 158, 178, 255)
EMBER_DEEP = (150, 40, 12, 255)
EMBER_MID = (232, 108, 24, 255)
EMBER_HOT = (255, 208, 96, 255)
ARC_DEEP = (28, 92, 148, 255)
ARC_MID = (72, 168, 232, 255)
ARC_HOT = (196, 240, 255, 255)


class Canvas:
    """A tiny indexed-free RGBA raster with the handful of ops these sheets need."""

    def __init__(self, width: int, height: int):
        self.w = width
        self.h = height
        self.px = [[CLEAR] * width for _ in range(height)]

    def put(self, x: int, y: int, colour) -> None:
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = colour

    def rect(self, x0: int, y0: int, x1: int, y1: int, colour) -> None:
        for y in range(max(0, y0), min(self.h, y1)):
            for x in range(max(0, x0), min(self.w, x1)):
                self.px[y][x] = colour

    def disc(self, cx: float, cy: float, radius: float, colour) -> None:
        r2 = radius * radius
        for y in range(max(0, int(cy - radius)), min(self.h, int(cy + radius) + 1)):
            for x in range(max(0, int(cx - radius)), min(self.w, int(cx + radius) + 1)):
                if (x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2 <= r2:
                    self.px[y][x] = colour

    def ring(self, cx: float, cy: float, outer: float, inner: float, colour) -> None:
        o2, i2 = outer * outer, inner * inner
        for y in range(max(0, int(cy - outer)), min(self.h, int(cy + outer) + 1)):
            for x in range(max(0, int(cx - outer)), min(self.w, int(cx + outer) + 1)):
                d = (x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2
                if i2 <= d <= o2:
                    self.px[y][x] = colour

    def taper(self, x0: int, y0: int, x1: int, y1: int, w0: float, w1: float,
              colour) -> None:
        """A quad that narrows from w0 to w1 along a line -- a finger, a claw, a barrel."""
        steps = max(abs(x1 - x0), abs(y1 - y0), 1)
        for i in range(steps + 1):
            t = i / steps
            cx = x0 + (x1 - x0) * t
            cy = y0 + (y1 - y0) * t
            half = (w0 + (w1 - w0) * t) * 0.5
            for y in range(int(cy - half), int(cy + half) + 1):
                for x in range(int(cx - half), int(cx + half) + 1):
                    self.put(x, y, colour)

    def blit(self, other: "Canvas", ox: int, oy: int) -> None:
        for y in range(other.h):
            for x in range(other.w):
                colour = other.px[y][x]
                if colour[3]:
                    self.put(ox + x, oy + y, colour)

    def upscale(self, factor: int) -> "Canvas":
        out = Canvas(self.w * factor, self.h * factor)
        for y in range(self.h):
            for x in range(self.w):
                colour = self.px[y][x]
                if not colour[3]:
                    continue
                out.rect(x * factor, y * factor, (x + 1) * factor,
                         (y + 1) * factor, colour)
        return out

    def to_png(self) -> bytes:
        raw = bytearray()
        for row in self.px:
            raw.append(0)  # filter type 0 (None): these are tiny and flat
            for r, g, b, a in row:
                raw += bytes((r, g, b, a))

        def chunk(tag: bytes, data: bytes) -> bytes:
            body = tag + data
            return (struct.pack(">I", len(data)) + body +
                    struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF))

        header = struct.pack(">IIBBBBB", self.w, self.h, 8, 6, 0, 0, 0)
        return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
                chunk(b"IDAT", zlib.compress(bytes(raw), 9)) +
                chunk(b"IEND", b""))


# --- the sheets -------------------------------------------------------------
# Every cell is authored at CELL px and upscaled 2x on the way out.
CELL = 64


def gloved_hand(flip: bool) -> Canvas:
    """One gauntleted hand entering from the bottom of frame, thumb inward."""
    c = Canvas(CELL, CELL)
    # Forearm wrap: a broad wedge climbing from the bottom edge.
    c.taper(14 if not flip else CELL - 14, CELL + 2,
            24 if not flip else CELL - 24, 34, 26, 20, GLOVE_DARK)
    c.taper(16 if not flip else CELL - 16, CELL, 25 if not flip else CELL - 25,
            36, 16, 12, GLOVE_MID)
    # Knuckle block.
    cx = 26 if not flip else CELL - 26
    c.rect(cx - 11, 24, cx + 11, 38, GLOVE_MID)
    c.rect(cx - 11, 24, cx + 11, 28, GLOVE_LIT)
    # Fingers, curled toward the grip.
    for i in range(3):
        fx = cx - 8 + i * 8 if not flip else cx + 8 - i * 8
        c.taper(fx, 26, fx + (3 if not flip else -3), 16, 7, 5, GLOVE_MID)
        c.put(fx, 17, GLOVE_LIT)
    # Thumb, and the one bit of skin that keeps this reading as a hand.
    tx = cx + 11 if not flip else cx - 11
    c.taper(tx, 32, tx + (6 if not flip else -6), 24, 8, 6, SKIN)
    c.taper(tx, 33, tx + (5 if not flip else -5), 27, 4, 3, SKIN_SHADE)
    return c


def hands_sheet() -> Canvas:
    """1x1: the shared hands, framing the bottom corners with the centre free."""
    c = Canvas(CELL * 2, CELL)
    c.blit(gloved_hand(flip=False), 0, 0)
    c.blit(gloved_hand(flip=True), CELL, 0)
    return c


def talon_cell(charge: float) -> Canvas:
    """The claw: three metal talons over a fist, embers rising with `charge` 0..1."""
    c = Canvas(CELL, CELL)
    # Fist block.
    c.rect(20, 34, 44, 54, METAL_DARK)
    c.rect(20, 34, 44, 40, METAL_MID)
    c.rect(22, 36, 30, 39, METAL_LIT)
    # Three talons fanning up and out.
    for dx, tip in ((-13, 6), (0, 2), (13, 6)):
        c.taper(32 + dx // 2, 36, 32 + dx, tip, 9, 3, METAL_MID)
        c.taper(32 + dx // 2, 36, 32 + dx, tip + 2, 4, 2, METAL_LIT)
    if charge <= 0.0:
        return c
    # Ember bloom between the talons. Three quantised steps, never a gradient.
    hot = EMBER_HOT if charge > 0.66 else EMBER_MID
    mid = EMBER_MID if charge > 0.33 else EMBER_DEEP
    c.disc(32, 22, 5 + 7 * charge, mid)
    c.disc(32, 22, 2 + 4 * charge, hot)
    if charge > 0.5:
        c.ring(32, 22, 13 + 6 * charge, 11 + 6 * charge, EMBER_DEEP)
    return c


def talon_sheet() -> Canvas:
    """2x2: idle, then a three-cell firing bloom."""
    c = Canvas(CELL * 2, CELL * 2)
    for i, charge in enumerate((0.0, 1.0, 0.62, 0.28)):
        c.blit(talon_cell(charge), (i % 2) * CELL, (i // 2) * CELL)
    return c


def flash_cell(scale: float) -> Canvas:
    """A four-point arcane star, sized by `scale`; scale 0 is an empty cell."""
    c = Canvas(CELL, CELL)
    if scale <= 0.0:
        return c
    cx = cy = CELL / 2
    arm = 8 + 20 * scale
    c.taper(int(cx), int(cy - arm), int(cx), int(cy + arm), 7 * scale, 7 * scale,
            ARC_DEEP)
    c.taper(int(cx - arm), int(cy), int(cx + arm), int(cy), 7 * scale, 7 * scale,
            ARC_DEEP)
    c.disc(cx, cy, 5 + 8 * scale, ARC_MID)
    c.disc(cx, cy, 2 + 4 * scale, ARC_HOT)
    return c


def flash_sheet() -> Canvas:
    """2x2: an empty idle cell, then the flash collapsing over three cells."""
    c = Canvas(CELL * 2, CELL * 2)
    for i, scale in enumerate((0.0, 1.0, 0.55, 0.22)):
        c.blit(flash_cell(scale), (i % 2) * CELL, (i // 2) * CELL)
    return c


SHEETS = {
    "vm_hands_default.png": hands_sheet,
    "vm_weapon_talon.png": talon_sheet,
    "vm_muzzle_arcane.png": flash_sheet,
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true",
                        help="report what would be written and write nothing")
    args = parser.parse_args()

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for name, build in SHEETS.items():
        data = build().upscale(2).to_png()
        target = OUT_DIR / name
        rel = target.relative_to(PROJECT_ROOT)
        if args.dry_run:
            print(f"would write {rel} ({len(data)} bytes)")
            continue
        target.write_bytes(data)
        print(f"wrote {rel} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
