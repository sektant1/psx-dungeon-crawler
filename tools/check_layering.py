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
# One rule per directory. This used to carry seven per-file exceptions
# (Engine.cpp, Platform.cpp, Input.cpp, Config.cpp, Log.cpp, StepClock.cpp,
# DebugTools.cpp) because those sources sat loose in src/; they now live in the
# directory that names their layer, so the path states the layer.
SOURCE_RULES: list[tuple[str, str]] = [
    ("src/app/", "app"),
    ("src/platform/", "platform"),
    ("src/rhi/", "platform"),
    ("src/core/", "core"),
    ("src/io/", "core"),
    ("src/content/", "core"),
    ("src/diagnostics/", "core"),
    ("src/systems/", "core"),  # Ease/Actions/Events are core utilities
    ("src/render/PrimitiveGeometry.cpp", "core"),  # pure geometry, no renderer
    ("src/ecs/", "framework"),
    ("src/script/", "framework"),  # lua host + bindings; sees World/Physics/Input
    ("src/controllers/", "framework"),
    ("src/debug/", "framework"),  # imgui debug console, drives fps/ecs
    ("src/", "systems"),  # renderer, physics, audio, particles
]

# Layer of a public header, by path relative to engine/include/eng/.
HEADER_RULES: list[tuple[str, str]] = [
    ("Engine.h", "app"),
    ("app/", "app"),
    ("Platform.h", "platform"),
    ("Input.h", "platform"),
    ("Config.h", "platform"),
    ("FrameStats.h", "core"),
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
    # A 64-bit hash of a string_view and an intern table: <cstdint>,
    # <string_view> and <functional>, no renderer and no window. It is included
    # by src/core, which the catch-all below would make an upward include.
    ("StringId.h", "core"),
    ("Clock.h", "core"),  # <cstdint> and a tick count
    # Plain data plus callbacks: std::function and strings, no renderer, which
    # is what lets it be unit-tested without a window. Only the catch-all below
    # would otherwise file it under systems.
    ("Loading.h", "core"),
    ("systems/", "core"),
    ("io/", "core"),
    # The content root: <filesystem> and the TOML reader, no renderer and no
    # window, which is what lets every layer, every tool and every test resolve
    # an asset path. Only the catch-all below would otherwise file it under
    # systems and make src/core/AssetRoot.cpp an upward include.
    ("assets/", "core"),
    ("rhi/", "platform"),
    ("ecs/", "framework"),
    ("script/", "framework"),
    ("controllers/", "framework"),
    ("DebugTools.h", "framework"),  # imgui debug console over the fps/ecs layer
    # The panels that dock into it. Same layer for the same reason: they are
    # imgui tooling, they are built into eng_framework beside DebugTools.cpp,
    # and a panel that could not name the host it registers with would have to
    # invert the dependency for no benefit to anything below it.
    ("debug/", "framework"),
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
