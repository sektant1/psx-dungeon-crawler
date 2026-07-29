#!/usr/bin/env python3
"""Enforce the engine's layer rules at the #include level.

The CMake split (eng_core -> eng_platform -> eng_systems -> eng_framework ->
eng) already makes a *symbol* dependency that points upward a link error. It
cannot catch a header-level violation, because every layer still shares one
public include root (engine/include). This script closes that gap: it maps each
translation unit and each public header to a layer, then fails if it includes
anything from a layer above it.

Run standalone or as the `layering` ctest.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Layers, lowest first. A file may include its own layer and anything below.
# "platform" holds both the window/input layer and the RHI: both sit directly
# on core, neither may see the other, and systems is the first layer allowed to
# use either.
LAYERS = ["core", "platform", "systems", "framework", "app"]

# Layer of a source file, by path relative to engine/. First match wins, so
# specific paths precede the directory defaults.
SOURCE_RULES: list[tuple[str, str]] = [
    ("src/Engine.cpp", "app"),
    ("src/Platform.cpp", "platform"),
    ("src/Input.cpp", "platform"),
    ("src/Config.cpp", "platform"),
    ("src/Log.cpp", "core"),
    ("src/StepClock.cpp", "core"),
    ("src/core/", "core"),
    ("src/io/", "core"),
    ("src/rhi/", "platform"),
    ("src/content/", "core"),
    ("src/diagnostics/", "core"),
    ("src/systems/", "core"),  # Ease/Actions/Events are core utilities
    ("src/render/PrimitiveGeometry.cpp", "core"),
    ("src/ecs/", "framework"),
    ("src/controllers/", "framework"),
    ("src/DebugTools.cpp", "framework"),  # imgui debug console, drives fps/ecs
    ("src/", "systems"),  # renderer, physics, audio, particles
]

# Layer of a public header, by path relative to engine/include/eng/.
HEADER_RULES: list[tuple[str, str]] = [
    ("Engine.h", "app"),
    ("Platform.h", "platform"),
    ("Input.h", "platform"),
    ("Config.h", "platform"),
    ("Log.h", "core"),
    ("Math.h", "core"),
    ("Handles.h", "core"),
    ("Object.h", "core"),
    ("Resource.h", "core"),
    ("ResourceCache.h", "core"),
    ("TextResource.h", "core"),
    ("Content.h", "core"),
    ("FileSystem.h", "core"),
    ("DirectoryWatcher.h", "core"),
    ("Trace.h", "core"),
    ("Profiler.h", "core"),
    ("StepClock.h", "core"),
    ("systems/", "core"),
    ("io/", "core"),
    ("rhi/", "platform"),
    ("ecs/", "framework"),
    ("controllers/", "framework"),
    ("DebugTools.h", "framework"),  # imgui debug console over the fps/ecs layer
    ("", "systems"),  # everything else: renderer/physics/audio/particles API
]

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"](eng/[^">]+)[">]', re.M)


def classify(path: str, rules: list[tuple[str, str]]) -> str:
    for prefix, layer in rules:
        if path.startswith(prefix):
            return layer
    return "systems"


def header_layer(include: str) -> str:
    return classify(include[len("eng/") :], HEADER_RULES)


def main() -> int:
    violations: list[str] = []

    targets: list[tuple[Path, str]] = []
    for src in sorted((ROOT / "engine" / "src").rglob("*.cpp")):
        rel = src.relative_to(ROOT / "engine").as_posix()
        targets.append((src, classify(rel, SOURCE_RULES)))
    for hdr in sorted((ROOT / "engine" / "include" / "eng").rglob("*.h")):
        rel = hdr.relative_to(ROOT / "engine" / "include" / "eng").as_posix()
        targets.append((hdr, classify(rel, HEADER_RULES)))

    for path, layer in targets:
        own = LAYERS.index(layer)
        text = path.read_text(encoding="utf-8", errors="replace")
        for include in INCLUDE_RE.findall(text):
            dep = header_layer(include)
            if LAYERS.index(dep) > own:
                violations.append(
                    f"{path.relative_to(ROOT)} [{layer}] includes "
                    f"<{include}> [{dep}] -- dependencies must point downward"
                )

    if violations:
        print("Layering violations:\n", file=sys.stderr)
        for v in violations:
            print(f"  {v}", file=sys.stderr)
        print(
            f"\n{len(violations)} violation(s). Move the code down a layer or "
            "invert the dependency (handles/callbacks, not upward includes).",
            file=sys.stderr,
        )
        return 1

    print(f"layering: {len(targets)} files, no upward includes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
