#!/usr/bin/env python3
"""One-shot: Ogre .material scripts -> engine .mat.

Run once to produce assets/materials/*.mat from the Ogre scripts this
project inherited, then deleted along with this script's reason to exist. Kept
in tools/ so the conversion is auditable rather than a mystery diff.

The important translation is the shader. Ogre named a compiled program per
look, and the RHI had been recovering its own shader family by substring-
matching those names ("...Portal..." -> the portal profile). That is fragile in
both directions: renaming a program silently changed rendering, and a new
material had to be named to match. The engine format states the family
outright.
"""
import re
import sys
from pathlib import Path

# Ogre fragment program -> (engine shader id, highlight)
#
# highlight=False is the old PSX_FS_Dungeon variant: keeps outlines and
# creases, refuses the stylize highlight wash over stone.
SHADERS = {
    "PSX_FS_Lit": ("lit", True),
    "PSX_FS_LitAlphaScissor": ("lit", True),
    "PSX_FS_LitMetal": ("lit.metal", True),
    "PSX_FS_LitNoTex": ("lit.untextured", True),
    "PSX_FS_LitNoTexRim": ("lit.untextured", True),
    "PSX_FS_LitPerspective": ("lit", True),
    "PSX_FS_LitTransparent": ("lit", True),
    "PSX_FS_Dungeon": ("lit", False),
    "PSX_FS_Unlit": ("unlit", True),
    "PSX_FS_UnlitAlphaScissor": ("unlit", True),
    "PSX_FS_UnlitMetal": ("unlit.metal", True),
    "PSX_FS_UnlitTransparent": ("unlit", True),
    "PSX_FS_LightVolume": ("unlit.light_volume", True),

    "PixelVfx/LiquidFS": ("surface.liquid", False),
    "PixelVfx/LavaFS": ("surface.lava", False),
    "PixelVfx/PortalFS": ("surface.portal", False),
    "PixelVfx/PrototypeLiquidFS": ("surface.liquid", False),
    "PixelVfx/PrototypePortalFS": ("surface.portal", False),

    "AtlasParticle_FS": ("particle.atlas", True),
    "FireParticle_FS": ("particle.flame", True),
    "SmokeParticle_FS": ("particle.smoke", True),
    "RainParticle_FS": ("particle.rain", True),
    "BlockParticle_FS": ("particle.block", True),
    "MoteParticle_FS": ("particle.mote", True),
    "ShardParticle_FS": ("particle.shard", True),
    "BubbleParticle_FS": ("particle.bubble", True),
    "WispParticle_FS": ("particle.wisp", True),
    "AshParticle_FS": ("particle.textured", True),
    "Particles/SpriteFS": ("particle.textured", True),
    "Particles/VoxelFS": ("particle.voxel", True),

    # Compositor passes. The RHI implements these as fixed passes and reads
    # only their tuning out of the material, but the values still live here.
    "Dither_FS": ("post.grade", True),
    "Stylize_FS": ("post.stylize", True),
    "BloomBright_FS": ("post.bloom_bright", True),
    "BloomBlur_FS": ("post.bloom_blur", True),
    "BloomComposite_FS": ("post.bloom_composite", True),
    "HardwareResolve_FS": ("post.resolve", True),

    "Sprite/FS": ("sprite", True),
    "Wire_FS": ("wire", True),
    "DebugLines_FS": ("debug_lines", True),
    "Decals/QuadFS": ("decal", True),
    "Editor_FS_Checkerboard": ("editor.checkerboard", True),
    "Editor_FS_FireIcon": ("editor.icon", True),
    "Editor_FS_PlacementGhost": ("editor.ghost", True),
}

BLEND = {"alpha_blend": "alpha", "add": "additive", "src_alpha": "additive"}
CULL = {"none": "none", "anticlockwise": "front", "clockwise": "back"}
FILTER = {"none": "nearest", "point": "nearest"}
ADDRESS = {"clamp": "clamp", "mirror": "mirror", "wrap": "repeat"}


def tokenize(text):
    text = re.sub(r"//[^\n]*", " ", text)
    return text.replace("{", " { ").replace("}", " } ").split()


def toml_scalar(value):
    return "true" if value is True else "false" if value is False else value


def parse(path):
    """Ogre's material grammar is brace-nested tokens; walk it flat."""
    tokens = tokenize(path.read_text())
    materials, i = [], 0
    while i < len(tokens):
        if tokens[i] != "material" or i + 1 >= len(tokens):
            i += 1
            continue
        name = tokens[i + 1]
        # Match braces from the material's own opening one and stop the moment
        # depth returns to zero; anything looser runs on into the next block.
        depth, j = 0, i + 2
        start = j + 1
        while j < len(tokens):
            if tokens[j] == "{":
                depth += 1
            elif tokens[j] == "}":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        body = tokens[start:j]
        mat = {"name": name, "params": []}
        k = 0
        while k < len(body):
            t = body[k]
            if t == "texture" and k + 1 < len(body):
                mat["texture"] = body[k + 1]
                k += 1
            elif t == "filtering" and k + 1 < len(body):
                mat["filter"] = FILTER.get(body[k + 1], "linear")
                k += 1
            elif t == "tex_address_mode" and k + 1 < len(body):
                mat["address"] = ADDRESS.get(body[k + 1], "repeat")
                k += 1
            elif t == "cull_hardware" and k + 1 < len(body):
                mat["cull"] = CULL.get(body[k + 1], "back")
                k += 1
            elif t == "scene_blend" and k + 1 < len(body):
                if body[k + 1] == "src_alpha" and k + 2 < len(body):
                    mat["blend"] = "additive"
                    k += 2
                else:
                    mat["blend"] = BLEND.get(body[k + 1], "opaque")
                    k += 1
            elif t == "depth_check" and k + 1 < len(body):
                mat["depth_test"] = body[k + 1] != "off"
                k += 1
            elif t == "depth_write" and k + 1 < len(body):
                mat["depth_write"] = body[k + 1] != "off"
                k += 1
            elif t == "polygon_mode" and k + 1 < len(body):
                mat["polygon"] = body[k + 1]
                k += 1
            elif t == "fragment_program_ref" and k + 1 < len(body):
                mat["fs"] = body[k + 1]
                k += 1
            elif t == "param_named" and k + 2 < len(body):
                pname, ptype = body[k + 1], body[k + 2]
                count = {"float": 1, "float2": 2, "float3": 3, "float4": 4,
                         "int": 1}.get(ptype, 0)
                values = body[k + 3:k + 3 + count]
                if count and len(values) == count and ptype != "int":
                    mat["params"].append((pname, values))
                k += 2 + count
            k += 1
        materials.append(mat)
        i = j
    return materials


def emit(materials, out_path, source_name):
    lines = [
        "# Engine material definitions. Generated once from the Ogre script",
        f"# this file replaced ({source_name}); hand-edited since.",
        "#",
        "# `shader` names the engine shader family outright. It used to be",
        "# recovered by substring-matching an Ogre program name, which meant a",
        "# rename silently changed how a material drew.",
        "",
    ]
    for m in materials:
        shader, highlight = SHADERS.get(m.get("fs", ""), ("lit", True))
        lines.append(f'[material."{m["name"]}"]')
        lines.append(f'shader = "{shader}"')
        # filter and address are emitted even at their defaults: nearest
        # sampling and the wrap mode ARE this project's look, and a material
        # that leaves them implicit reads as one nobody thought about.
        for key, default in (("texture", None), ("filter", None),
                             ("address", None), ("cull", "back"),
                             ("blend", "opaque")):
            if key in m and m[key] != default:
                lines.append(f'{key} = "{m[key]}"')
        for key in ("depth_test", "depth_write"):
            if key in m:
                lines.append(f"{key} = {toml_scalar(m[key])}")
        if m.get("polygon") == "wireframe":
            lines.append("polygon = \"line\"")
        if not highlight:
            lines.append("# Stone: keeps outlines and creases, refuses the "
                         "stylize highlight wash.")
            lines.append("highlight = false")
        if m["params"]:
            lines.append("")
            lines.append(f'[material."{m["name"]}".params]')
            for pname, values in m["params"]:
                if len(values) == 1:
                    lines.append(f"{pname} = {values[0]}")
                else:
                    lines.append(f"{pname} = [{', '.join(values)}]")
        lines.append("")
    out_path.write_text("\n".join(lines))


def main():
    root = Path(sys.argv[1] if len(sys.argv) > 1 else "assets/materials")
    total = 0
    for script in sorted(root.glob("*.material")):
        materials = parse(script)
        emit(materials, script.with_suffix(".mat"), script.name)
        total += len(materials)
        print(f"{script.name}: {len(materials)}")
    print(f"total {total}")


if __name__ == "__main__":
    main()
