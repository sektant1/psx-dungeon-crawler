#!/usr/bin/env python3
"""raven_player must contain no game code.

That is the whole claim the target exists to make: a game authored in the
editor and scripted in Lua runs on a binary that knows nothing about the
dungeon crawler this repository also ships. A claim like that rots the first
time somebody adds a convenient #include and a convenient link line -- the
build still works, the test suite still passes, and the player has quietly
grown a dependency on combat, dungeon generation or the enemy library.

So it is enforced the way the layering is (tools/check_layering.py): read the
binary and fail on the symbols that must not be there.

Usage: check_player_purity.py <path-to-raven_player>
"""

import re
import subprocess
import sys

# Symbols in these namespaces mean game code was linked in. `game::` is the
# dungeon crawler itself; `mapio::` is now only its component table -- the .map
# codec that used to share the namespace moved into eng::ecs precisely because
# this check could not tell the two apart, and a codec every runtime needs
# should not be wearing a name that reads as the game's.
#
# The table is the subtler way in: it looks generic and drags the game's
# vocabulary -- Exit, EnemySpawn, Pickup, ViewmodelRig -- along behind it.
FORBIDDEN = [
    ("game::", "the dungeon crawler"),
    ("mapio::", "the game's component table"),
]

# Namespaced C++ symbols are mangled as N<len>name...; `game` is 4 characters,
# `mapio` is 5. Matching the mangling rather than demangling every symbol keeps
# this working without a demangler on the box.
MANGLED = {
    "game::": re.compile(r"_ZN[KV]*4game[0-9]"),
    "mapio::": re.compile(r"_ZN[KV]*5mapio[0-9]"),
}


def symbols(binary: str) -> list[str]:
    """Every symbol name in the binary, however we can get at them."""
    for argv in (["nm", "--defined-only", binary], ["nm", binary]):
        try:
            out = subprocess.run(argv, capture_output=True, text=True, check=True)
        except (OSError, subprocess.CalledProcessError):
            continue
        return out.stdout.splitlines()
    return []


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    binary = sys.argv[1]

    lines = symbols(binary)
    if not lines:
        # A stripped binary or no nm. Refuse rather than pass: a check that
        # cannot see anything must not report that it saw nothing wrong.
        print(f"check_player_purity: could not read symbols from {binary}")
        return 1

    found: dict[str, list[str]] = {}
    for line in lines:
        for prefix, pattern in MANGLED.items():
            if pattern.search(line) or prefix in line:
                found.setdefault(prefix, []).append(line.strip())

    if not found:
        print(f"check_player_purity: {binary} is clean")
        return 0

    print(f"check_player_purity: {binary} links game code\n")
    for prefix, what in FORBIDDEN:
        hits = found.get(prefix)
        if not hits:
            continue
        print(f"  {prefix} ({what}): {len(hits)} symbol(s), first few:")
        for hit in hits[:5]:
            print(f"    {hit}")
        print()
    print("raven_player links eng_runtime and nothing under game/.")
    print("If a runtime genuinely needs what was added, it belongs in")
    print("eng_runtime -- not in the player's link line.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
