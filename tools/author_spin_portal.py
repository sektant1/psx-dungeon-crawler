import json, collections

E = []
def add(**kw):
    e = collections.OrderedDict()
    e["id"] = kw.pop("id")
    if "name" in kw: e["name"] = kw.pop("name")
    if "parent" in kw: e["parent"] = kw.pop("parent")
    if "prefab" in kw: e["prefab"] = kw.pop("prefab")
    if "material" in kw: e["material"] = kw.pop("material")
    t = collections.OrderedDict()
    if "pos" in kw: t["position"] = [float(v) for v in kw.pop("pos")]
    if "rot" in kw: t["rotation_degrees"] = [float(v) for v in kw.pop("rot")]
    if "scale" in kw: t["scale"] = [float(v) for v in kw.pop("scale")]
    if t: e["transform"] = t
    for k, v in kw.items(): e[k] = v
    E.append(e)

# --- the room -------------------------------------------------------------
# 3x3 cells = 12 x 12 m. Small on purpose: the subject is one turning prop, and
# a room the camera has to pull back 11 m to frame is a room in which the prop
# is four pixels tall. This was 7x7.
cells = [-4.0, 0.0, 4.0]
n = 0
for z in cells:
    for x in cells:
        n += 1
        add(id="floor_%04d" % n, prefab="kit.floor", pos=[x, 0.0, z])

# Walled on all four sides: the camera orbits, so a gap is a hole in the
# picture for a quarter of the clip.
for i, x in enumerate(cells):
    add(id="wall_north_%04d" % (i + 1), prefab="kit.wall", pos=[x, 0.0, -6.0])
    add(id="wall_south_%04d" % (i + 1), prefab="kit.wall", pos=[x, 0.0, 6.0])
for i, z in enumerate(cells):
    add(id="wall_west_%04d" % (i + 1), prefab="kit.wall", pos=[-6.0, 0.0, z],
        rot=[0.0, 90.0, 0.0])
    add(id="wall_east_%04d" % (i + 1), prefab="kit.wall", pos=[6.0, 0.0, z],
        rot=[0.0, 90.0, 0.0])

# --- the subjects ---------------------------------------------------------
# Each prop carries its OWN Spin. A prop turning in place needs no pivot: the
# component rotates the entity's own transform, and giving two props one shared
# parent only couples them -- change the rate and both change, hide one and the
# node stays. A pivot is for an *orbit*, where the offset from the centre is the
# radius, and that is what the camera below uses it for.
add(id="prop_crystal", name="Floating Crystal", prefab="kit.crystal_spire_tall",
    pos=[0.0, 0.85, 0.0], rot=[0.0, 0.0, 6.0],
    spin={"axis": [0.0, 1.0, 0.0], "degrees_per_second": 42.0},
    shader={"rim_colour": [0.45, 0.85, 1.0], "rim_strength": 1.8,
            "rim_power": 3.0})
add(id="prop_raccoon", name="Raccoon Head", prefab="kit.prop_raccoon_head",
    pos=[0.0, 2.85, 0.0], rot=[0.0, 180.0, 0.0],
    spin={"axis": [0.0, 1.0, 0.0], "degrees_per_second": -28.0},
    shader={"rim_colour": [0.35, 1.0, 0.85], "rim_strength": 2.2,
            "rim_power": 2.5})
add(id="prop_base", name="Crystal Base", prefab="kit.crystal_ground",
    pos=[0.0, 0.0, 0.0])
add(id="prop_shaft", name="God Ray", prefab="kit.light_shaft",
    pos=[0.0, 0.0, 0.0], scale=[0.75, 1.1, 0.75])

# --- the portal -----------------------------------------------------------
# Exit remains gameplay destination. Membrane is explicit presentation so
# scene editor shows and tunes same portal that play mode uses.
add(id="portal_exit", name="Portal", pos=[0.0, 0.0, -5.4],
    exit={"yaw_degrees": 0.0})
add(id="portal_membrane_0001", name="Portal Membrane",
    prefab="kit.portal_membrane", pos=[0.0, 0.0, -5.4],
    portal={"portalArms": 6.0, "portalFlowSpeed": -0.9,
            "portalTwist": 0.55, "surfaceDark": [0.22, 0.03, 0.3],
            "surfaceMid": [0.75, 0.1, 1.0],
            "surfaceBright": [1.4, 0.45, 1.9],
            "surfaceCore": [2.0, 1.3, 2.4],
            "surfaceGlowColour": [1.3, 0.2, 2.1]})

# --- light ----------------------------------------------------------------
add(id="light_key", name="Crystal Key", pos=[0.0, 3.6, 1.0],
    light={"type": "point", "colour": [0.42, 1.60, 2.35], "range": 8.0,
           "animation": {"mode": "pulse", "speed": 0.35, "amount": 0.22}})
for i, (x, z, phase) in enumerate([(-4.6, 4.6, 0.0), (4.6, 4.6, 3.7),
                                   (-4.6, -4.6, 6.3), (4.6, -4.6, 1.9)]):
    add(id="light_torch_%04d" % (i + 1), name="Torch %d" % (i + 1),
        pos=[x, 2.4, z],
        light={"type": "point", "colour": [1.00, 0.75, 0.45], "range": 6.5,
               "animation": {"mode": "flicker", "speed": 7.0, "amount": 0.35,
                             "phase": phase}})
add(id="light_fill", name="Fill", pos=[0.0, 5.0, 4.0], rot=[-48.0, 20.0, 0.0],
    light={"type": "directional", "colour": [0.42, 0.44, 0.55]})

# --- the shot -------------------------------------------------------------
# One entity, one component. The camera used to be a `camera_pivot` with a Spin
# and the camera parented to it, where the child's z offset *was* the orbit
# radius -- so changing the radius meant editing a transform, and the pivot was
# an entity in the outliner that stood for nothing.
#
# Orbit says what it means: circle this point, at this radius, looking at it.
add(id="camera_main", name="Main Camera", pos=[0.0, 3.1, 5.4],
    camera={"fov_degrees": 55.0, "far_clip": 60.0, "priority": 10},
    # The centre is what it circles AND what it looks at, so it sits at the
    # subject's height rather than on the floor; `height` then lifts the ring
    # above it. Centred at the floor with the ring raised, the camera orbits
    # correctly and spends the whole clip looking at tiles.
    orbit={"centre": [0.0, 1.9, 0.0], "radius": 5.4, "height": 1.2,
           "degrees_per_second": 7.0, "phase_degrees": 34.0,
           "facing": "centre"})
# A parked second framing: closer, wider, off to one side, and static. Inactive,
# so it is there to switch to rather than in the way.
add(id="camera_close", name="Close Framing", pos=[2.6, 1.5, 2.6],
    rot=[-8.0, 42.0, 0.0],
    camera={"fov_degrees": 68.0, "active": False})

add(id="player_start", name="Player Start", pos=[0.0, 0.0, 4.0],
    player_spawn=True)

doc = collections.OrderedDict()
doc["$schema"] = "../schemas/scene.schema.json"
doc["format"] = "psx-dungeon-scene"
doc["version"] = 2
doc["id"] = "scene.spin_portal"
doc["palette"] = "showroom"
doc["entities"] = E
with open("assets/scenes/spin_portal.scn", "w") as f:
    json.dump(doc, f, indent=2)
    f.write("\n")
print("entities:", len(E))
