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
# An Exit, not a prop: the runtime builds the membrane, its green key light and
# its wisps from the authored fact, exactly as it does in a played level.
add(id="portal_exit", name="Portal", pos=[0.0, 0.0, -5.4],
    exit={"yaw_degrees": 0.0})

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
# The camera DOES hang off a pivot, and this is the case that needs one: the
# camera stands 5.4 m out from the centre -- inside a 12 m room, which is
# the constraint an orbit radius has that a static framing does not, so the pivot's rotation becomes an
# orbit. A Spin on the camera itself would only make it pirouette on the spot.
add(id="camera_pivot", name="Camera Orbit", pos=[0.0, 0.0, 0.0],
    rot=[0.0, -34.0, 0.0],
    spin={"axis": [0.0, 1.0, 0.0], "degrees_per_second": 7.0})
add(id="camera_main", name="Main Camera", parent="camera_pivot",
    pos=[0.0, 3.10, 5.4], rot=[-13.0, 0.0, 0.0],
    camera={"fov_degrees": 55.0, "far_clip": 60.0, "priority": 10})
# A parked second framing: closer, wider, off to one side. Inactive, so it is
# there to switch to rather than in the way.
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
