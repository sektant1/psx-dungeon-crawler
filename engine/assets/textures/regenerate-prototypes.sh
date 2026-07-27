#!/usr/bin/env bash
# Regenerates the engine's prototype textures. They used to be built pixel by
# pixel at renderer startup; as files they resolve like any other texture, which
# is what the material scripts naming them actually want.
#
# Run from anywhere: ./engine/assets/textures/regenerate-prototypes.sh
set -euo pipefail
cd "$(dirname "$0")"

# 32x32, 4px cells -- large enough to read as a grid on a wall, small enough to
# stay obviously untextured. Colours match the loud "this asset is missing"
# convention used elsewhere in the project.

# Surface: magenta/black checker. The default stand-in for missing geometry.
magick -size 4x4 xc:'#FF00D2' xc:'#232323' +append \
       \( -clone 0 -flop \) -append -write mpr:cell +delete \
       -size 32x32 tile:mpr:cell -alpha set PNG32:EnginePrototypeSurface.png

# Sprite: cyan frame + diagonals, so a mis-sized billboard is obvious.
magick -size 32x32 xc:'#052D46' \
       -fill none -stroke '#14F0FF' -strokewidth 3 \
       -draw 'rectangle 1,1 30,30' -strokewidth 1 \
       -draw 'line 0,0 31,31' -draw 'line 31,0 0,31' \
       -alpha set PNG32:EnginePrototypeSprite.png

# Particle: orange diamond, transparent corners -- distinct from both above.
magick -size 32x32 xc:none \
       -fill '#FF5010' -draw 'polygon 15,0 31,15 15,31 0,15' \
       -fill '#FFF550' -draw 'polygon 15,7 23,15 15,23 7,15' \
       PNG32:EnginePrototypeParticle.png

echo "regenerated:"
ls -la EnginePrototype*.png
