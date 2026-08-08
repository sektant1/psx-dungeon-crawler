#!/usr/bin/env python3
"""Remove one pack's imported output, so it can be re-imported cleanly.

`tools/import_asset_pack.py` overwrites what it writes, but it cannot remove
what it no longer writes -- a texture whose naming rule changed leaves the old
file behind, and a stale PNG in a flat resource group is exactly the kind of
thing that silently wins a basename lookup.

So re-importing after a rule change is: clean, then import. Only ever touches
the four generated locations for the named domain, all of which carry a header
saying they are generated:

    assets/meshes/<domain>/
    assets/textures/<domain>/
    assets/materials/<domain>.mat
    assets/prefabs/<domain>.prefab.toml

Usage:
  tools/clean_imported_pack.py <domain> [<domain> ...] [--dry-run]
"""

import argparse
import os
import shutil
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(REPO, "assets")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("domain", nargs="+")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    for domain in args.domain:
        targets = [
            os.path.join(ASSETS, "meshes", domain),
            os.path.join(ASSETS, "textures", domain),
            os.path.join(ASSETS, "materials", "%s.mat" % domain),
            os.path.join(ASSETS, "prefabs", "%s.prefab.toml" % domain),
        ]
        for target in targets:
            if not os.path.exists(target):
                continue
            rel = os.path.relpath(target, REPO)
            if args.dry_run:
                print("would remove", rel)
                continue
            if os.path.isdir(target):
                shutil.rmtree(target)
            else:
                os.remove(target)
            print("removed", rel)
    return 0


if __name__ == "__main__":
    sys.exit(main())
