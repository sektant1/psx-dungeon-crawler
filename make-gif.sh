#!/usr/bin/env bash

set -euo pipefail

INPUT="${1:-input.mp4}"
OUTPUT="${2:-showcase.gif}"
PALETTE="$(mktemp --suffix=.png)"

cleanup() {
    rm -f "$PALETTE"
}

trap cleanup EXIT

if [[ ! -f "$INPUT" ]]; then
    echo "Error: input file not found: $INPUT" >&2
    exit 1
fi

ffmpeg -i "$INPUT" \
    -vf "fps=15,scale=960:-1:flags=lanczos,palettegen=stats_mode=diff" \
    -y "$PALETTE"

ffmpeg -i "$INPUT" -i "$PALETTE" \
    -lavfi "fps=15,scale=960:-1:flags=lanczos[x];[x][1:v]paletteuse=dither=sierra2_4a:diff_mode=rectangle" \
    -loop 0 \
    -y "$OUTPUT"

echo "Created: $OUTPUT"
du -h "$OUTPUT"
