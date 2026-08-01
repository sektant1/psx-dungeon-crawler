#!/usr/bin/env python3
"""Import the animated effect sheets under assets/sprites into the particle system.

The packs in assets/sprites/shaders are laid out the way effect packs always
are: one PNG holds a grid of cells, each ROW is one animation, and the row is
repeated horizontally once per colour variant.  Two things follow from that.

  * The colour variants are redundant here.  Particles are tinted at runtime by
    the effect's colour ramp, so the least saturated variant is the useful one
    and every other copy is dead weight.  This tool keeps exactly one block and
    drops the rest, which is most of why the imported sheets are a fraction of
    the source art rather than a duplicate of it.

  * Nothing needs slicing into one file per animation.  The engine's flipbook
    describes a *window* into a sheet (origin cell, frame count, frames per
    row), so a couple of hundred animations stay a couple of dozen textures and
    a couple of dozen texture bindings.

Output:
    assets/engine/particles/textures/sheets/*.png   the trimmed sheets
    assets/engine/particles/sprite_sheets.toml      one [texture.*] per animation

Both are generated: re-run the tool rather than hand-editing them.  Overrides
for individual entries belong in textures.toml, which is loaded from the same
directory and parsed after this file.

Usage:  python3 tools/import_sprite_sheets.py [--dry-run]
"""

from __future__ import annotations

import argparse
import colorsys
import os
import sys

import numpy as np
from PIL import Image

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO, "assets", "sprites", "shaders")
OUT_DIR = os.path.join(REPO, "assets", "engine", "particles", "textures", "sheets")
OUT_TOML = os.path.join(REPO, "assets", "engine", "particles", "sprite_sheets.toml")

# Row -> what the shape actually is, read off a contact sheet of the pack.  The
# names are the whole point of the import: "shade_crescent_b2" is something a
# designer can pick out of a combo box, "32x32 Shades Effect/48.png row 6" is
# not.
SHADES_GROUP_A = [
    "heart_a", "heart_b", "heart_c", "tile_a", "tile_b",
    "crescent_a", "crescent_b", "crescent_c", "heart_d", "heart_e",
    "starburst_a", "starburst_b", "cone_a", "cone_b", "arrow_a", "arrow_b",
]
SHADES_GROUP_B = [
    "dart_a", "dart_b", "trident", "streak_a", "streak_b", "streak_c",
    "flower", "spear_a", "spear_b", "droplet", "blade", "claw",
    "orb_a", "orb_b", "spiral", "star_a",
]
BULLET_ROWS = [
    "orb", "ring", "heart", "tile", "diamond", "ball",
    "cone", "lens", "hoop", "oval", "moon_a", "moon_b",
]


def load_rgba(path: str) -> np.ndarray:
    return np.array(Image.open(path).convert("RGBA"))


def block_saturation(a: np.ndarray, x0: int, x1: int) -> float:
    """Mean saturation of one colour block, weighted by alpha.

    Picking the flattest block automatically beats hardcoding an index: the
    packs do not agree on where the greyscale variant sits, and one of them has
    no greyscale variant at all, in which case this still returns the tamest
    colour rather than failing.
    """
    sl = a[:, x0:x1].astype(np.float32)
    w = sl[..., 3]
    total = w.sum()
    if total <= 0:
        return 1.0
    mean = (sl[..., :3] * w[..., None]).sum(axis=(0, 1)) / total
    _, _, sat = colorsys.rgb_to_hls(*(mean / 255.0))
    return sat


def pick_block(a: np.ndarray, starts: list[int], width_px: int) -> int:
    """Index into `starts` of the least saturated block."""
    return min(range(len(starts)),
               key=lambda i: block_saturation(a, starts[i], starts[i] + width_px))


def write_sheet(a: np.ndarray, path: str, dry: bool) -> None:
    # RGB under a fully transparent texel is arbitrary in the source art (one
    # pack stores a background tint there).  Bilinear filtering and additive
    # blending both read it, so zero it rather than trust it.
    a = a.copy()
    a[..., :3][a[..., 3] == 0] = 0
    if dry:
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    Image.fromarray(a).save(path, optimize=True)


class Entry:
    """One [texture.<stem>] block: an animation window into a trimmed sheet."""

    def __init__(self, stem, sheet, sheet_cols, sheet_rows, row, frames, fps,
                 blend="alpha"):
        self.stem = stem
        self.sheet = sheet
        self.sheet_cols = sheet_cols
        self.sheet_rows = sheet_rows
        self.row = row
        self.frames = frames
        self.fps = fps
        self.blend = blend

    def toml(self) -> str:
        return (
            f"[texture.{self.stem}]\n"
            f'sheet = "{self.sheet}"\n'
            f'blend = "{self.blend}"\n'
            f"flipbook = {{ sheet_cols = {self.sheet_cols}, "
            f"sheet_rows = {self.sheet_rows}, origin_col = 0, "
            f"origin_row = {self.row}, frames = {self.frames}, "
            f"per_row = {self.frames}, fps = {self.fps}, loop = true }}\n"
        )


def import_shades(dry: bool) -> list[Entry]:
    """32x32 Shades Effect: 56x16 cells, 7 colour blocks of 8 frames."""
    base = os.path.join(SRC, "32x32 Shades Effect")
    groups = [(["00", "16", "32", "48", "64"], SHADES_GROUP_A),
              (["320", "336", "352", "368", "384"], SHADES_GROUP_B)]
    cell, frames = 32, 8
    entries: list[Entry] = []
    for sheets, rows in groups:
        for variant, name in enumerate(sheets, start=1):
            src = os.path.join(base, f"{name}.png")
            a = load_rgba(src)
            cols = a.shape[1] // cell
            starts = [c * cell for c in range(0, cols, frames)]
            block = pick_block(a, starts, frames * cell)
            x0 = starts[block]
            trimmed = a[:, x0:x0 + frames * cell]
            sheet = f"shade32_{name}.png"
            write_sheet(trimmed, os.path.join(OUT_DIR, sheet), dry)
            sheet_rows = a.shape[0] // cell
            for row, shape in enumerate(rows[:sheet_rows]):
                entries.append(Entry(f"shade_{shape}{variant}", sheet,
                                     frames, sheet_rows, row, frames, 12))
    return entries


def import_bullet(dry: bool) -> list[Entry]:
    """16x16 Pixel Shade Bullet: 9 blocks of 8 frames, 1 blank column between.

    The blank column is why this pack cannot use the same block-start formula as
    the others: its blocks are 9 cells apart but 8 cells wide.
    """
    src = os.path.join(SRC, "16x16 Pixel Shade Bullet Free",
                       "16x16 Pixel Shade Bullet Free.png")
    a = load_rgba(src)
    cell, frames, stride = 16, 8, 9
    cols = a.shape[1] // cell
    starts = [c * cell for c in range(0, cols - frames + 1, stride)]
    block = pick_block(a, starts, frames * cell)
    x0 = starts[block]
    trimmed = a[:, x0:x0 + frames * cell]
    sheet = "bullet16.png"
    write_sheet(trimmed, os.path.join(OUT_DIR, sheet), dry)

    # Rows 0-11 and 13-24 are the same twelve shapes in two colour families;
    # row 12 is a blank separator and must not become an animation.
    sheet_rows = a.shape[0] // cell
    halves = [(0, "hot"), (13, "void")]
    entries: list[Entry] = []
    for first, half in halves:
        for row, shape in enumerate(BULLET_ROWS):
            if first + row >= sheet_rows:
                break
            entries.append(Entry(f"bullet_{shape}_{half}", sheet, frames,
                                 sheet_rows, first + row, frames, 14))
    return entries


def import_cylinder(dry: bool) -> list[Entry]:
    """Shader Cylinder 64x96: 4 colour blocks, 2 animation rows, per file."""
    root = os.path.join(SRC, "Shader Cylinder 64x96", "Free")
    cell_w, cell_h, blocks = 64, 96, 4
    entries: list[Entry] = []
    for part_dir in sorted(os.listdir(root)):
        full = os.path.join(root, part_dir)
        if not os.path.isdir(full):
            continue
        part = part_dir.split("Part ")[-1].split(" ")[0]
        for file in sorted(os.listdir(full)):
            if not file.endswith(".png") or not file[:-4].isdigit():
                continue
            a = load_rgba(os.path.join(full, file))
            cols = a.shape[1] // cell_w
            frames = cols // blocks
            starts = [i * frames * cell_w for i in range(blocks)]
            block = pick_block(a, starts, frames * cell_w)
            x0 = starts[block]
            trimmed = a[:, x0:x0 + frames * cell_w]
            sheet = f"coil_p{part}_{file[:-4]}.png"
            write_sheet(trimmed, os.path.join(OUT_DIR, sheet), dry)
            sheet_rows = a.shape[0] // cell_h
            for row in range(sheet_rows):
                suffix = chr(ord("a") + row)
                entries.append(Entry(f"coil_p{part}_{file[:-4]}{suffix}", sheet,
                                     frames, sheet_rows, row, frames, 15))
    return entries


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true",
                        help="report what would be written, touch nothing")
    args = parser.parse_args()

    if not os.path.isdir(SRC):
        print(f"import_sprite_sheets: no source pack at {SRC}", file=sys.stderr)
        return 1

    entries = (import_shades(args.dry_run)
               + import_bullet(args.dry_run)
               + import_cylinder(args.dry_run))
    entries.sort(key=lambda e: e.stem)

    sheets = sorted({e.sheet for e in entries})
    header = (
        "# GENERATED by tools/import_sprite_sheets.py -- do not hand edit.\n"
        "#\n"
        "# One [texture.*] per animation in the effect packs under\n"
        "# assets/sprites/shaders. Several entries share one sheet: the\n"
        "# flipbook names the window, not the file.\n"
        "#\n"
        "# To override a single entry (blend mode, rate, filtering) add it to\n"
        "# textures.toml instead. Both files are parsed, and re-running the\n"
        "# import rewrites only this one.\n"
        f"#\n# {len(entries)} animations across {len(sheets)} sheets.\n"
    )
    body = "\n".join(e.toml() for e in entries)

    if args.dry_run:
        print(header)
        print(f"{len(entries)} entries, {len(sheets)} sheets")
        return 0

    with open(OUT_TOML, "w", encoding="utf-8") as f:
        f.write(header + "\n" + body)
    print(f"import_sprite_sheets: {len(entries)} animations, "
          f"{len(sheets)} sheets -> {OUT_TOML}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
