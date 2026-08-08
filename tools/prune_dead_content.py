#!/usr/bin/env python3
"""Remove catalogue entries and scenes whose meshes no longer exist.

The old prototype content — a fantasy dungeon kit plus a pile of `import_*`
models pulled in one at a time — was deleted from `assets/meshes/` without the
catalogue that names it being updated. `assets/config/kit.toml` is left claiming
106 pieces that resolve to nothing, and every scene placing one of them cooks
into a level with holes in it.

`assetlint` reports exactly this, which is how it was found. This tool is the
fix, run once: prune the pieces whose mesh is gone, then prune the scenes that
cannot survive losing them.

It is deliberately conservative about scenes. A scene that loses ONE torch is
still a scene and keeps its remaining entities; a scene where most of the
placements are dead was built out of content that no longer exists, and keeping a
shell of it helps nobody. `--scene-threshold` is where that line falls.

    tools/prune_dead_content.py --dry-run      # what would go
    tools/prune_dead_content.py                # do it
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import tomllib

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(REPO, "assets")


def mesh_exists(mesh_dir: str, mesh: str) -> bool:
    for candidate in (os.path.join(ASSETS, mesh_dir, mesh),
                      os.path.join(ASSETS, mesh)):
        if os.path.isfile(candidate):
            return True
    return False


def dead_pieces(path: str) -> set:
    with open(path, "rb") as stream:
        document = tomllib.load(stream)
    mesh_dir = document.get("kit", {}).get("mesh_dir", "")
    dead = set()
    for piece in document.get("piece", []):
        mesh = piece.get("mesh")
        # A group piece has no mesh of its own; it dies only if every part it
        # attaches is dead, which the second pass below works out.
        if mesh and not mesh_exists(mesh_dir, mesh):
            dead.add(piece["id"])

    # A group whose every attachment is dead is itself dead.
    for piece in document.get("piece", []):
        attachments = piece.get("attachment", [])
        if not piece.get("mesh") and attachments:
            names = {a.get("prefab", "") for a in attachments}
            stripped = {n.split(".", 1)[-1] for n in names}
            if stripped and stripped <= dead:
                dead.add(piece["id"])
    return dead


def prune_kit(path: str, dead: set, dry: bool) -> int:
    """Drop the dead `[[piece]]` blocks, preserving everything around them."""
    with open(path) as stream:
        text = stream.read()

    # Split on block starts, keeping the leading comment block with its piece:
    # the comments in this file explain the piece that follows, so carrying them
    # along is what keeps the survivors readable.
    parts = re.split(r"\n(?=\[\[piece\]\])", text)
    kept, removed = [parts[0]], 0
    for block in parts[1:]:
        match = re.search(r'id\s*=\s*"([^"]+)"', block)
        if match and match.group(1) in dead:
            removed += 1
            continue
        kept.append(block)

    if not dry:
        with open(path, "w") as f:
            f.write("\n".join(kept))
    return removed


def scene_dead_ratio(path: str, dead: set) -> tuple:
    with open(path) as stream:
        try:
            document = json.load(stream)
        except json.JSONDecodeError:
            return 0, 0
    entities = document.get("entities", [])
    total = sum(1 for e in entities if e.get("prefab"))
    gone = sum(1 for e in entities
               if e.get("prefab", "").split(".", 1)[-1] in dead)
    return gone, total


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--scene-threshold", type=float, default=0.5,
                        help="drop a scene when this fraction of its "
                             "prefab placements are dead (default 0.5)")
    args = parser.parse_args()

    kit = os.path.join(ASSETS, "config", "kit.toml")
    dead = dead_pieces(kit)
    print("%d catalogue pieces name a mesh that no longer exists" % len(dead))

    scenes_dir = os.path.join(ASSETS, "scenes")
    doomed = []
    for name in sorted(os.listdir(scenes_dir)):
        if not name.endswith(".scn"):
            continue
        path = os.path.join(scenes_dir, name)
        gone, total = scene_dead_ratio(path, dead)
        if total and gone / total >= args.scene_threshold:
            doomed.append((name, gone, total))
        elif gone:
            print("  %-28s keeps %d of %d placements" %
                  (name, total - gone, total))

    for name, gone, total in doomed:
        print("  %-28s DROP (%d of %d placements dead)" % (name, gone, total))

    if args.dry_run:
        print("\n--dry-run: nothing written")
        return 0

    removed = prune_kit(kit, dead, dry=False)
    print("\nremoved %d pieces from config/kit.toml" % removed)

    for name, _, _ in doomed:
        for suffix in (".scn", ".map", ".autosave.scn"):
            path = os.path.join(scenes_dir, name[:-4] + suffix)
            if os.path.isfile(path):
                os.remove(path)
                print("removed", os.path.relpath(path, REPO))
    return 0


if __name__ == "__main__":
    sys.exit(main())
