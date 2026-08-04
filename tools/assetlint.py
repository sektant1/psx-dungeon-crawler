#!/usr/bin/env python3
"""Validate that every asset reference in the tree resolves to a real asset.

A missing mesh or material does not crash this engine: the prototype fallback
draws a box or a checkered surface instead (see eng/render/PrototypeAssets.h).
That is the right runtime behaviour and the wrong build behaviour -- it turns a
typo into a scene that silently looks wrong, buried in a log line. This script
makes the same mistake fail the build.

Checks:
  1. every `texture = "file"` in a .mat resolves to a file in a registered
     texture directory;
  2. every `.obj` named in a .toml exists under the owning tree's meshes/;
  3. every material name referenced from a .toml is defined by some .mat;
  4. no material name is defined twice (Ogre rejects the duplicate, and the
     scene silently keeps the first definition);
  5. no two texture files share a basename, and no two .mat files do
     either (Ogre's file index is as flat as its resource namespace: the
     duplicate is skipped and openResource() resolves it arbitrarily).

Usage: assetlint.py [asset-root ...]   (defaults to every mount set declared in
assets/assets.toml). Run standalone or as the `assetlint` ctest.
"""

from __future__ import annotations

import json
import re
import sys
import tomllib
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "assets" / "assets.toml"


def mount_sets() -> dict[str, list[Path]]:
    """The manifest's [mounts], resolved to directories.

    Uniqueness is per *mount set*, not global: Ogre's namespaces are flat, but
    only one set is ever registered at a time, so two packs that are never
    mounted together may reuse a name. Two packs in the SAME set may not --
    ResourceManager::add throws rather than warns -- which is what makes this
    the check that lets `demo` sit on top of `game`.

    Every root comes from the manifest now. assets/game used to be
    appended here by hand because DEMO_SCENE_TOML reached it outside the
    resolver; it is the declared `common` pack since P2.
    """
    with MANIFEST.open("rb") as f:
        manifest = tomllib.load(f)
    dirs = {p["id"]: (MANIFEST.parent / p["dir"]).resolve()
            for p in manifest.get("pack", [])}
    return {
        name: [dirs[pack] for pack in packs if pack in dirs]
        for name, packs in manifest.get("mounts", {}).items()
    }

# Engine material files are TOML: `[material."Pack/Name"]`, one table per
# material. The bare name form is accepted too, for names that need no quoting.
MATERIAL_DEF_RE = re.compile(r'^\s*\[material\.(?:"([^"]+)"|([^\]\s]+))\]', re.M)
TEXTURE_RE = re.compile(r'^\s*texture\s*=\s*"([^"]+)"', re.M)
OBJ_RE = re.compile(r'"([^"]*\.obj)"')
# Material references in level/catalog TOML: `material = "X"`, `materials =
# ["X", "Y"]`. Deliberately not every string -- only the keys that name one.
TOML_MATERIAL_RE = re.compile(r'materials?\s*=\s*(\[[^\]]*\]|"[^"]*")')
QUOTED_RE = re.compile(r'"([^"]*)"')

IMAGE_SUFFIXES = {".png", ".jpg", ".jpeg", ".tga", ".dds", ".bmp"}

# Pipeline INPUTS, not content: archives, .blend files and the raw effect/model
# packs they extract to. They sit under assets/ so the artist finds them next to
# the thing they produce, but nothing loads them and they are not registered
# with Ogre -- so their basenames cannot collide with anything, and linting them
# only produces noise (the dungeon pack ships the same texture names the game
# already committed, extracted, under textures/).
SKIP_DIRS = {"source"}

# The manifest describes the tree, it is not content in it. It also declares
# `[formats] mesh = [..., ".obj"]`, which the mesh-reference regex would
# otherwise read as a reference to a file literally named ".obj".
SKIP_FILES = {"assets.toml"}


def skipped(path: Path, root: Path) -> bool:
    try:
        parts = path.relative_to(root).parts
    except ValueError:
        return False
    if not parts:
        return False
    return parts[0] in SKIP_DIRS or (len(parts) == 1 and parts[0] in SKIP_FILES)


def collect(roots: list[Path]):
    textures: dict[str, list[Path]] = defaultdict(list)
    meshes: set[str] = set()
    materials: dict[str, list[Path]] = defaultdict(list)
    scripts: dict[str, list[Path]] = defaultdict(list)
    # Lua scripts, by path relative to the pack root -- which is exactly how a
    # scene names them ("scripts/door.lua"), so the check below is a lookup
    # rather than a search.
    lua: set[str] = set()

    for root in roots:
        if not root.is_dir():
            continue
        for path in root.rglob("*"):
            if not path.is_file() or skipped(path, root):
                continue
            if path.suffix.lower() in IMAGE_SUFFIXES:
                textures[path.name].append(path)
            elif path.suffix == ".obj":
                meshes.add(path.name)
            elif path.suffix == ".lua":
                lua.add(path.relative_to(root).as_posix())
            elif path.suffix == ".mat":
                scripts[path.name].append(path)
                for quoted, bare in MATERIAL_DEF_RE.findall(
                        path.read_text(errors="replace")):
                    materials[quoted or bare].append(path)
    return textures, meshes, materials, scripts, lua


def lint(roots: list[Path]) -> tuple[list[str], str]:
    textures, meshes, materials, scripts, lua = collect(roots)
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
    for name, files in sorted(scripts.items()):
        if len(files) > 1:
            where = ", ".join(str(f.relative_to(ROOT)) for f in files)
            errors.append(f"script basename '{name}' used {len(files)}x: {where}")

    # 1: texture units.
    for root in roots:
        if not root.is_dir():
            continue
        for mat in sorted(root.rglob("*.material")):
            if skipped(mat, root):
                continue
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
            if skipped(doc, root):
                continue
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

    # 6: scripts named by .scn scenes. A dangling script path is exactly as
    # broken as a dangling mesh, and the runtime cannot tell you: an entity
    # whose script failed to load simply sits there doing nothing.
    for root in roots:
        if not root.is_dir():
            continue
        for doc in sorted(root.rglob("*.scn")):
            if skipped(doc, root):
                continue
            try:
                scene = json.loads(doc.read_text(errors="replace"))
            except json.JSONDecodeError:
                continue  # the schema check owns malformed scenes
            for entity in scene.get("entities", []):
                for script in entity.get("scripts", []):
                    path = script.get("path", "")
                    if path and path not in lua:
                        errors.append(
                            f"{doc.relative_to(ROOT)}: script '{path}' not found"
                        )

    summary = (
        f"{len(materials)} materials, {len(textures)} textures, "
        f"{len(meshes)} meshes, {len(lua)} scripts"
    )
    return errors, summary


def main(argv: list[str]) -> int:
    explicit = [Path(a).resolve() for a in argv[1:]]
    jobs = {"(explicit)": explicit} if explicit else mount_sets()

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
