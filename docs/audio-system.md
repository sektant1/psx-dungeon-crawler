# Audio System

Runtime audio has two layers:

- `eng::Audio` owns miniaudio, output device/null fallback, mixer buses, 3D
  listener state, voice scheduling, fades, voice limits and backend telemetry.
- `game::GameAudioSystem` owns authored cue policy, sample variation,
  concurrency/cooldowns, distance culling, gameplay emissions, ducking, player
  foley timing and adaptive music.
- `game::ActorAudio` owns *which cue an actor's action plays* — the override
  chain from placed entity to actor type to convention (see Actor Sounds).

Engine code never names weapons, enemies or music sections. Game code never
touches `ma_sound`, `ma_engine` or the miniaudio node graph.

## Mixer Graph

Every voice routes through one of eight stable buses:

```text
Master
  Music
  Ambience
  Dialogue
  Weapons
  SFX
  UI
  Warnings
```

`Master` defaults to `-4 dB` for summing headroom. Authoring balance lives in
`assets/config/audio.toml`; user overrides live under `[audio]` in
`assets/config/game.toml`. Gains stay in decibels until `eng::Audio` converts at
the miniaudio boundary. Never write a `0..1` slider value directly as dB; use
`eng::linearToDecibels` or `Audio::setBusVolume`.

Dialogue and critical cues carry `duck_music_db`. Active cue requests combine by
taking the strongest reduction. Music uses a fast attack and slow release;
ambience receives a lighter version of the same duck. Overlapping dialogue
therefore cannot release the duck early.

## Cue Authoring

Each `[[cue]]` row defines semantic event, not clip:

```toml
[[cue]]
id = "enemy.telegraph"
files = ["audio/enemies/telegraph_01.wav", "audio/enemies/telegraph_02.wav"]
bus = "warnings"
gain_db = -1.5
pitch_min = 0.96
pitch_max = 1.04
spatial = true
min_distance = 2.0
max_distance = 35.0
max_instances = 12
cooldown_seconds = 0.04
priority = "critical"
```

Short cues decode through miniaudio's shared resource manager. Long ambience and
music set `stream = true`. Repeated cues should provide multiple independently
edited takes. Runtime selection prevents immediate repeats and applies subtle,
seeded pitch/gain variation; variation is not a substitute for source takes.

Missing clip files make that cue a no-op. Catalog load reports one aggregate
`awaiting assets` count rather than flooding logs during combat. This lets code,
event bindings and mix policy land before final recordings without crashing or
repeated disk probes.

## Emission

One-shot gameplay event:

```cpp
game::AudioEmission sound;
sound.position = impactPoint;
audio.emit("weapon.eidolon.impact", sound);
```

First-person/UI sounds force 2D so head rotation cannot pan player-owned foley:

```cpp
sound.spatial = game::SpatialOverride::Force2D;
audio.emit("player.hit", sound);
```

Persistent object:

```cpp
game::AudioEmitter emitter(audio, game::audioCueId("ambience.ritual_fire"));
emitter.play(position);
emitter.setTransform(position, velocity);
emitter.stop(eng::StopMode::AllowFadeOut);
```

Cue cooldown and concurrency run before backend voice allocation. Inaudible 3D
cues are culled beyond `max_distance * 1.1`. If global budget is full,
`eng::Audio` steals oldest voice in lowest eligible priority; critical voices
are non-stealable. Default budget is 96 and clamps to `8..512`.

## Actor Sounds

An actor is an entity the game treats as somebody: a player, an NPC or an enemy.
`game::Actor` says which, and it is what gates everything below — scenery,
props and projectiles carry no `Actor` and therefore make no actor noise.

A level rarely states the kind. It is implied by what is already authored:

| authored | kind |
|---|---|
| `player_spawn` | player |
| `enemy_spawn`, or a marker named `enemy.<id>` | enemy |
| an explicit `actor` component | whatever it says |

Only an NPC needs the component, because nothing else in the format implies one.
The cooker resolves the kind once (`game::content::actorKindOf`) and writes
`Actor`, so the runtime asks one question instead of the three the format grew.

### Actions

`game::ActorAction` is a closed list. Every entry has a call site — the `hint`
in `ActorSounds.cpp` names the moment it plays — so an author choosing a cue
gets a sound rather than a field nothing reads.

| action | when | kinds |
|---|---|---|
| `spawn` | it appears in the level | all |
| `idle` | breathing/growl with no target | npc, enemy |
| `alert` | the beat it notices the player | npc, enemy |
| `footstep` | per stride on the ground | all |
| `jump` | leaving the ground under its own power | player |
| `land` | touching down | all |
| `telegraph` | the readable wind-up before a swing | npc, enemy |
| `attack` | the swing or shot leaving the body | all |
| `impact` | its attack landing on something | all |
| `hurt` | damage taken and survived | all |
| `block` | a deflect negating a hit | all |
| `dodge` | a dodge/dash starting | all |
| `death` | the killing blow | all |
| `interact` | the player interacting with it | npc |

A kind only sees the actions it performs: the inspector hides `telegraph` on the
player and `jump` on an enemy, and `SceneValidate` warns about a row authored
for an action the kind never takes.

### The override chain

A cue resolves three deep, most specific first:

```text
the placed entity's table   ActorSounds component, authored per placement
    -> the actor type's     EnemyDef::sounds, [enemy.<id>.sounds]
        -> the convention   "<kind>.<action>", e.g. "enemy.death"
```

An empty row means *not stated*, never *silent* — so adding a sound table to an
entity changes nothing until a row is filled in. A cue the catalog does not
define is a counted no-op, which is what makes the convention safe to lean on
before the takes are recorded.

`game::ActorAudio` walks that chain. Call sites name what happened, not what it
sounds like:

```cpp
mActorAudio->play(victim, game::ActorAction::Hurt, point);
```

A more specific cue the caller already holds — an attack's own
`telegraph_sound`, a weapon's `fire_sound` — is passed as an override and wins
over the actor's row; an empty one falls straight through, so no call site
branches on whether it was authored.

Emissions are 3D except when the sound comes from the player's own body
(`ActorActionInfo::ownBody`). `impact` is the exception that proves it: the
shot lands across the room, and forcing that 2D would put it inside the
player's head.

### Authoring

Per enemy type, in `enemies.toml`, inherited from the archetype row by row:

```toml
[enemy.knight.sounds]
death = "enemy.knight.death"   # everything else stays the archetype's
```

Per placement, in a `.scn` — the Sounds component in the inspector, offered only
on entities that are actors:

```json
{ "id": "boss_0001", "enemy_spawn": "knight",
  "sounds": { "death": "boss.knight.death", "alert": "boss.knight.roar" } }
```

The placement's table rides through the spawner (`EnemySpawnPoint::sounds`) onto
the live enemy, because the authored entity and the enemy it produces are
entities on two different registries.

### Adding an action

1. A row in `kActions` (`game/src/audio/ActorSounds.cpp`), with its `hint` and
   the kinds that perform it.
2. The call site that plays it — for an enemy, an `mOnAction` emission in
   `EnemySystem`; nothing else has to change.
3. A `[[cue]]` for `<kind>.<action>` in `audio.toml`, so the default is real.

The inspector row, the `.scn` key, the cooked payload and the validator all come
from that one table entry.

## Adaptive Music

Music combines vertical layering and horizontal re-sequencing:

- `[[music_stem]]` rows map smoothed `0..1` intensity to aligned stem gains.
- `exploration`, `combat`, `boss` and later `extraction` sections switch on next
  authored bar boundary using miniaudio's output-frame clock.
- Section voices receive one absolute start frame, keeping stems sample-aligned.
- Transition fades straddle the same scheduled boundary.
- Combat and boss stingers overlay state changes without restarting score.

All stems in a section must have identical sample rate, length, downbeat and
loop points. Sections must begin and end on the BPM/bar grid declared in
`[music]`. Do not trim encoder delay independently per stem.

Intensity rises quickly and falls slowly. Combat section uses hysteresis
(`0.42` enter, `0.25` leave), preventing bar requests from oscillating around a
threshold. Inputs currently include nearby weighted enemy pressure, player
health and boss presence; extraction/significant-object channels are ready for
expedition systems.

## Diagnostics

F1 opens `Audio` panel:

- Active/global voices and per-bus counts.
- Device versus null backend.
- Smoothed music intensity/tier.
- Emitted, cooldown-rejected, concurrency-rejected, distance-culled and missing
  cue counts.
- Backend steal/reject counts.

Null backend is automatic when no output device exists, so tests and headless
captures exercise timing, cue policy and music state safely.

## Asset Delivery

Runtime paths expected by `audio.toml` are under `assets/audio/`. Raw multitrack
sessions and masters belong under `assets/source/audio/` and are not runtime
inputs. Current repository contains system and cue manifest but no final audio
clips; every authored path is therefore reported as awaiting assets.

Delivery checks:

1. Normalize categories against mix, not every file to same peak.
2. Keep true peaks below `0 dBFS`; target integrated game mix around `-14` to
   `-16 LUFS` after real play measurement, not isolated-file normalization.
3. Export one-shot PCM as WAV; stream long beds/stems as OGG or FLAC.
4. Verify mono spatial sources and stereo non-spatial beds intentionally.
5. Audition headphones, speakers and low-volume playback.
6. Stress maximum bullet patterns while watching Audio panel voice steals.
7. Verify dialogue duck recovery does not pump.
8. Verify every section transition and loop across at least ten repetitions.
