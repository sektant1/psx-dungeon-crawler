#!/usr/bin/env bash
# Install the launcher entry + icon into the user's XDG dirs (no root).
# Exec= must resolve to the built binary; pass its path as $1, else uses build/game.
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
bin="${1:-$repo/build/game}"
icon_src="$repo/assets/logo1.png"

apps_dir="$HOME/.local/share/applications"
icon_dir="$HOME/.local/share/icons/hicolor/256x256/apps"
mkdir -p "$apps_dir" "$icon_dir"

# Icon: name must match Icon= in the .desktop (psx-dungeon).
cp "$icon_src" "$icon_dir/psx-dungeon.png"

# Desktop entry with a concrete absolute Exec path.
sed "s|^Exec=.*|Exec=$bin|" "$repo/packaging/psx-dungeon.desktop" \
    > "$apps_dir/psx-dungeon.desktop"

update-desktop-database "$apps_dir" 2>/dev/null || true
gtk-update-icon-cache "$HOME/.local/share/icons/hicolor" 2>/dev/null || true

echo "Installed: $apps_dir/psx-dungeon.desktop"
echo "Icon:      $icon_dir/psx-dungeon.png"
