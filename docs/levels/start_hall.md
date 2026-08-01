# Level: start_hall — "The Reliquary Antechamber"

`assets/scenes/start_hall.scn` · 133 entities · re-authored by
`tools/author_start_hall.py` · the scene the editor opens with no `SCENE=`.

## Intent

**Player fantasy**: you wake somewhere that was safe once, and the only way on
is through the room where it stopped being safe.

**Pacing arc**: arrival (low) → read the room (low) → the pinch (rising) →
guards at the arch (high) → the way down (release).

**The one moment**: standing in the cold shrine light and seeing, straight
ahead through a lit arch ten metres away, the thing that is standing in it.

**Narrative beat**: people camped here waiting for something, the west wall came
down, and they never left. The arms at the shrine are theirs.

**Teaches, spatially**: cover exists (the pinch is chest height), lanes are a
choice (tight/covered against wide/exposed), and cold light is the way out.

## Layout

**Shape language**: linear with a two-lane middle. Hub-and-spoke would hide the
objective; this level's whole argument is that you can see it the entire time.

**Footprint**: hall 12 × 12 m (3 × 3 cells) + shrine alcove 4 × 4 m + exit
vestibule 4 × 4 m. Critical path ≈ 17 m.

**Estimated playtime**: 60–90 s direct, 2–3 min with both optional rewards.

```
                     [ VESTIBULE ]        cold light, exit
                          |  arch
        ..................|..................
        :  E01: two guards + a shooter      :   z 0..4   fight floor
        :   cover: barrels, crate stack     :
        :...................................:
        :  CAMP  ]   [ PINCH ]   [ EAST     :   z 4..8   the choice
        :  potion     scrap       open lane :
        :...................................:
        :  bedding, containers, rubble      :   z 8..12  arrival read
        ..................|..................
                          |  arch
                     [ SHRINE ]             cold light, spawn, refuge
```

Two lanes at the pinch, deliberately asymmetric:

| Lane | Width | Cover | Reward | Cost |
|---|---|---|---|---|
| West (through the collapse) | ~3 m | continuous, chest height | `scrap` in the rubble | slow, funnels you |
| East (past the vases) | ~5 m | two chest-high clusters | none | exposed to the shooter |

## Encounters

| ID | Type | Enemies | Read distance | Tactical options | Fallback |
|---|---|---|---|---|---|
| E01 | Held position | 2× `hollow`, 1× `crossbow_hollow` | ~10 m from the pinch, lit from behind | west lane in cover; east lane fast and wide; fight from the pinch itself | the arch to the shrine alcove — the player started there and knows it |

Placement rules honoured: no enemy is closer than the read distance, all three
stand in the arch's cold light (silhouetted against the one bright thing in the
room), and none of them is behind the player at any point on the path.

## Environmental story

| Cluster | Props | What it says |
|---|---|---|
| Shrine (alcove) | crystals, shaft, chest, a sword and shield propped at the walls, candles | someone armed came here, put their weapons down, and did not pick them up |
| Camp (west) | table with bread and a pumpkin, sacks, crates, hay bedding, a hanging lamp, candles | people waited here long enough to sleep and eat |
| Collapse (west→centre) | ruined wall, timber props, debris, a fallen and a broken pillar, a door off its frame | the waiting ended when the wall came down |

Nothing is spread evenly along a wall. The version this replaced was: props
hugged all four walls and the middle 6 × 6 m was empty, which reads as dressing,
not as history.

## Light

Warm where you are, cold where you are going — one rule, no markers.

| Light | Colour | Job |
|---|---|---|
| Chandelier | warm 1.0/0.72/0.42, r 9 | fills the hall, so the room is legible at all |
| 4× torch, camp lamp | warm firelight, r 4.5–5.5 | mark the walls and the camp cluster |
| Exit Arch + Vestibule | **cold 0.62/0.82/1.20**, r 6–6.5 | the objective is the only cold thing forward |
| Shrine | cold blue 0.45/0.75/1.25, r 7 | the landmark you can always orient by, behind you |
| Key light | dim neutral directional, shadows | shape, not illumination |

## What the review changed

Every one of these came from looking at the scene, not from theory.

| Finding | Fix |
|---|---|
| No encounters, pickups or triggers: a diorama | E01, two pickups, a threshold trigger |
| Props evenly along all four walls; empty middle | three story clusters + cover on the fight floor |
| Market stall with fresh vegetables in a sealed crypt | reframed as a camp: bedding, lamp, candles, the table as its mess |
| Flat 16 m walk from spawn to exit | the collapse pinches the crossing to ~3 m and splits it into two lanes |
| Exit had no accent light | cold light at the arch, everything else warm |
| **Spawn faced the back wall of the alcove** | yaw 0: the player wakes facing the arch |
| Exit arch read as a black hole (nothing behind it to light) | a vestibule cell beyond the arch, with its own cold light |
| Multi-part props were loose pairs | parented: barrel + hoops, table + boards, vase + lid are one row and one drag |

The last two were found by capturing the level **from the player's eye**
(`PSX_EDITOR_WALK=1`), which is the only view that answers a level-design
question. The top-down capture had looked fine.

## Readability review

Verified from captures:

- [x] Exit visible from the spawn, framed by the arch and its sconces
- [x] Exit is the only cold light forward; interior is warm
- [x] Exit has depth behind it — a lit vestibule, not a shadow
- [x] Shrine is a landmark visible from anywhere in the hall (orientation)
- [x] Both lanes at the pinch are open and read as different
- [x] Enemies stand in front of the objective, silhouetted, ~10 m away
- [x] No dead end that looks like an exit (the alcove is lit as a room, not a way on)

Not verified — no playtest was run, and this cannot be claimed from screenshots:

- [ ] Both lanes are viable in play (cover heights are eyeballed against the
      1.7 m eye height, not measured against the character controller)
- [ ] Time-to-cross and encounter difficulty
- [ ] Whether the camp/collapse story is actually inferred by a fresh player

## Rebuilding it

```sh
python3 tools/author_start_hall.py assets/scenes/start_hall.scn
./build/scene_cook assets/scenes/start_hall.scn --kit assets/config/kit.toml --validate-only
PSX_EDITOR_WALK=1 PSX_SCREENSHOT=/tmp/eye.png PSX_SCREENSHOT_FRAME=260 \
  xvfb-run -a ./build/scene_editor          # the player's view, headless
```

The `.scn` is the artifact; edit it in the editor. The script exists because the
grid arithmetic (cell centres, the half-thickness wall inset, the `y_offset`
exceptions) mirrors `game/content/GridMath.cpp`, and the cooker's validator is
what proves the two still agree.
