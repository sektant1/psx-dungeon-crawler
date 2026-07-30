#!/usr/bin/env python3
"""Validate that every asset reference in the tree resolves to a real asset.

A missing mesh or material does not crash this engine: the prototype fallback
draws a box or a checkered surface instead (see eng/render/PrototypeAssets.h).
That is the right runtime behaviour and the wrong build behaviour -- it turns a
typo into a scene that silently looks wrong, buried in a log line. This script
makes the same mistake fail the build.

Checks:
  1. every `texture <file>` in a .material resolves to a file in a registered
     texture directory;
  2. every `.obj` named in a .toml exists under the owning tree's meshes/;
  3. every material name referenced from a .toml is defined by some .material;
  4. no material name is defined twice (Ogre rejects the duplicate, and the
     scene silently keeps the first definition);
  5. no two texture files share a basename (Ogre's lookup is flat, so the
     collision is resolved arbitrarily).

Usage: assetlint.py [asset-root ...]   (defaults to the engine + game + samples
trees). Run standalone or as the `assetlint` ctest.
"""

from __future__ import annotations

import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Uniqueness is per *application*, not global: Ogre only ever has one app's
# asset roots registered at a time, so `game` and `psx-demo` are allowed to
# define the same material name. Each app is linted as engine + shared + its
# own tree, which is exactly what RenderCore registers at startup.
SHARED_ROOTS = [
    ROOT / "engine" / "assets",
    ROOT / "samples" / "common" / "assets",
]
APPS = {
    "game": ROOT / "game" / "assets",
    "psx-demo": ROOT / "samples" / "psx-demo" / "assets",
}

MATERIAL_DEF_RE = re.compile(r"^\s*material\s+(\S+)", re.M)
TEXTURE_RE = re.compile(r"^\s*texture\s+(\S+)", re.M)
OBJ_RE = re.compile(r'"([^"]*\.obj)"')
# Material references in level/catalog TOML: `material = "X"`, `materials =
# ["X", "Y"]`. Deliberately not every string -- only the keys that name one.
TOML_MATERIAL_RE = re.compile(r'materials?\s*=\s*(\[[^\]]*\]|"[^"]*")')
QUOTED_RE = re.compile(r'"([^"]*)"')

IMAGE_SUFFIXES = {".png", ".jpg", ".jpeg", ".tga", ".dds", ".bmp"}


def collect(roots: list[Path]):
    textures: dict[str, list[Path]] = defaultdict(list)
    meshes: set[str] = set()
    materials: dict[str, list[Path]] = defaultdict(list)

    for root in roots:
        if not root.is_dir():
            continue
        for path in root.rglob("*"):
            if not path.is_file():
                continue
            if path.suffix.lower() in IMAGE_SUFFIXES:
                textures[path.name].append(path)
            elif path.suffix == ".obj":
                meshes.add(path.name)
            elif path.suffix == ".material":
                for name in MATERIAL_DEF_RE.findall(path.read_text(errors="replace")):
                    materials[name].append(path)
    return textures, meshes, materials


def lint(roots: list[Path]) -> tuple[list[str], str]:
    textures, meshes, materials = collect(roots)
    errors: list[str] = []

    # 4/5: ambiguity in the flat namespaces Ogre actually resolves against.
    for name, files in sorted(materials.items()):
        if len(files) > 1:
            where = ", ".join(str(f.relative_to(ROOT)) for f in files)
            errors.append(f"material '{name}' defined {len(files)}x: {where}")
    for name, files in sorted(textures.items()):
        if len(files) > 1:
            where = ", ".join(str(f.relative_to(ROOT)) for f in files)
            errors.append(f"texture basename '{name}' used {len(files)}x: {where}")

    # 1: texture units.
    for root in roots:
        if not root.is_dir():
            continue
        for mat in sorted(root.rglob("*.material")):
            text = mat.read_text(errors="replace")
            for tex in TEXTURE_RE.findall(text):
                if tex not in textures:
                    errors.append(
                        f"{mat.relative_to(ROOT)}: texture '{tex}' not found "
                        "in any asset tree"
                    )

    # 2/3: TOML references.
    for root in roots:
        if not root.is_dir():
            continue
        for doc in sorted(root.rglob("*.toml")):
            text = doc.read_text(errors="replace")
            for obj in OBJ_RE.findall(text):
                if Path(obj).name not in meshes:
                    errors.append(f"{doc.relative_to(ROOT)}: mesh '{obj}' not found")
            for match in TOML_MATERIAL_RE.findall(text):
                for name in QUOTED_RE.findall(match):
                    if name and name not in materials:
                        errors.append(
                            f"{doc.relative_to(ROOT)}: material '{name}' is not "
                            "defined by any .material"
                        )

    summary = (
        f"{len(materials)} materials, {len(textures)} textures, "
        f"{len(meshes)} meshes"
    )
    return errors, summary


def main(argv: list[str]) -> int:
    explicit = [Path(a).resolve() for a in argv[1:]]
    jobs = (
        {"(explicit)": explicit}
        if explicit
        else {name: [*SHARED_ROOTS, root] for name, root in APPS.items()}
    )

    failed = 0
    for name, roots in jobs.items():
        errors, summary = lint(roots)
        if errors:
            failed += len(errors)
            print(f"Asset reference errors in {name}:\n", file=sys.stderr)
            for e in errors:
                print(f"  {e}", file=sys.stderr)
            print("", file=sys.stderr)
        else:
            print(f"assetlint [{name}]: {summary}, all references resolve")

    if failed:
        print(f"{failed} error(s).", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
