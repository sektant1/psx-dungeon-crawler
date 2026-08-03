# Engine foundations

Three `eng_core` facilities that sit under everything else: the timelines the
engine runs on, the id type it names things with, and the profiler it measures
itself with. They follow *Game Engine Architecture* (Gregory, 4th ed.) vol. I —
§8.4–8.5, §6.4.3 and §10.8 respectively — and the sections below say where this
engine agrees with the book and where it does not.

For how the layers fit together see [ARCHITECTURE.md](../ARCHITECTURE.md); for
the console that drives all three see [debug-console.md](debug-console.md).

## Timelines: `eng::Clock`

Wall time and game time answer different questions. "How long did that frame
take" paces the loop; "how much game happened" advances the world. This engine
had only the first, so pause, slow motion and frame stepping each would have had
to be threaded through every system that integrates.

`eng::Clock` is an abstract timeline: it advances *from* the real frame delta
without being obliged to agree with it. The engine owns two.

| Clock | Scales | Pauses | Who reads it |
|---|---|---|---|
| `engine.gameClock()` | yes | yes | simulation, gameplay timers, AI, world VFX |
| `engine.realClock()` | no | no | debug camera, UI animation, frame limiter, capture hooks |

`runApplication()` drives the fixed-step loop from the game clock and hands both
deltas to every callback:

```cpp
void onUpdate(const FrameContext& f) override {
    f.dt;      // game time: 0 while paused, scaled by time.scale
    f.realDt;  // wall time: keeps running over a frozen world
}
```

That one substitution is the entire implementation of pause: `f.dt == 0` means
the accumulator never reaches `fixedDt`, so `onFixedStep` stops being called,
physics stops, and the renderer carries on painting the frozen result. Nothing
opted in.

Three uses, in rising order of how often they earn their keep:

- **Debugging.** `time.pause` then `time.step` walks a spawn, an impact or a
  particle burst one frame at a time while the camera stays free. Unlike a
  breakpoint, the engine keeps drawing — which is the only way to *look* at a
  visual bug.
- **Game feel.** Hit-stop and death cams are `setScale()` for a few frames.
- **Determinism.** Both clocks rebase when the loading phase ends, for the same
  reason `StepClock` does: a level that takes 40 frames to load must not shift a
  capture pinned to frame 300.

A single step is a *fixed* duration (1/60 s by default), not the frame it landed
on — the frame you are staring at is usually a slow one, and stepping through a
bug is only reproducible if every step is the same size. Negative deltas (the
book's warning about per-core timer drift, §8.5.3.1) are clamped, so nothing
downstream ever integrates backwards.

`Clock` is not `StepClock`. `StepClock` quantises *presentation* into stop-motion
snaps and is a look; `Clock` warps *simulation time* and is a control. They
compose: a stepped channel on a paused clock simply holds its pose.

## Names: `eng::StringId`

The engine names things with strings — materials, events, weapons, config keys,
profile bins. A `std::string` key costs an allocation to build, a `memcmp` to
compare and a pointer chase to hash. `eng::StringId` is a 64-bit FNV-1a hash:
one integer, compared in a cycle, and `constexpr`, so it can be a `case` label.

```cpp
constexpr StringId kFire = "fire_wand"_sid;   // hashed at compile time
StringId id = intern(tomlKey);                // hashed once, at load
if (id == kFire) ...                          // one integer compare

switch (id.value()) {
case "fire_wand"_sid.value(): ...
}
```

64-bit rather than the book's original 32: Naughty Dog moved for *The Last of Us
Part II* precisely because a 32-bit space starts colliding at content scale, and
a silent id collision costs a week. `intern()` reports one as an error if it ever
happens, and `stringIdCollisions()` exposes the count.

**Interning is the slow path by design.** `intern()` hashes *and* records the
text so `c_str()` can recover it; do it once at load and keep the id. A
compile-time `"x"_sid` never enters the table — that is the point — and prints
as `#<hex>` rather than as nothing.

Adoption is incremental. Nothing was rewritten to use it; the profiler is the
first consumer, and each subsequent one is a local change (`std::string` key →
`StringId` key) rather than a migration.

## Measurement: `eng::Profiler`

`make perf` answers "which function is hot" better than an in-engine profiler
ever will. What it cannot answer is "which phase of *this* frame is hot, right
now, while I fly the camera into the corner that stutters". That is what
`eng::Profiler` is for, and why its numbers are annotated by hand.

Timings are a tree, because code is a tree. Each node carries:

- **inclusive ms** — everything inside the scope, children included
- **self ms** — inclusive minus children: the work this scope does *itself*,
  which is the number that says whether to look here or one level down
- **calls** — 2 ms in one call and 2 ms across a thousand are different bugs

```cpp
Profiler& prof = engine.profiler();          // the loop begins/ends the frame
void onUpdate(const FrameContext& f) override {
    ENG_PROFILE(prof, "weapons");            // nests under the loop's "update"
    { ENG_PROFILE(prof, "viewmodel"); ... }
}
```

`runApplication()` already opens a scope per loop phase (`frame begin`,
`fixed step`, `update`, `gui`, `render`), so a game's own scopes land in the
right place without it arranging anything:

```
frame              16.482 ms  self  0.004  100.0%  x1
  frame begin       0.013 ms  self  0.013    0.1%  x1
  fixed step        1.163 ms  self  1.163    7.1%  x1
  update            0.045 ms  self  0.045    0.3%  x1
  render           15.197 ms  self 15.197   92.2%  x1
```

Scope names must be string literals: the tree stores the pointer and identity is
the name's `StringId`, so re-entering a scope accumulates into one node instead
of allocating. Once a frame's shape is stable nothing allocates, which is what
lets it stay on while you chase the spike.

Read it three ways: `profile` in the console, `RAVEN_PROFILE=<n>` in the log, or
`profile.csv <path>` for the spreadsheet pass the book describes in §10.8.2.

Two limits worth knowing:

- **One parent per name.** A scope called from two places accumulates under
  whichever parent is open, exactly as the book describes for statically-declared
  sample bins (§10.8.1.1). Fine for coarse phases; not a call-graph profiler.
- **Single-threaded.** An open scope stack is per-thread state and a lock in a
  profiler measures the lock. One instance per thread if it ever needs more.

`sample(name, ms)` reports a duration measured elsewhere and lands as a leaf
under the open scope. Its containment is only as true as the caller's
measurement, so `selfMs()` clamps at zero rather than printing a negative
remainder.

`eng::FrameStats` is unchanged and still the right tool for the perf HUD: a
fixed ring of frame times plus a flat phase breakdown, which is what a plot
wants. The profiler is the drill-down behind it.

## Tests

| Test | Covers |
|---|---|
| `clock` | scale, pause, single step, timeline mapping, negative-delta clamp, rebase |
| `string_id` | literal/runtime hash agreement, `case` labels, interning, collision-free over engine names |
| `profiler` | nesting, self time, call counts, pre-order, unbalanced scopes, CSV |
