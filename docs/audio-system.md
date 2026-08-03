# Audio System

Runtime audio has two layers:

- `eng::Audio` owns miniaudio, output device/null fallback, mixer buses, 3D
  listener state, voice scheduling, fades, voice limits and backend telemetry.
- `game::GameAudioSystem` owns authored cue policy, sample variation,
  concurrency/cooldowns, distance culling, gameplay emissions, ducking, player
  foley and adaptive music.

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
