# Profiling memory, time and code size

*Game Engine Architecture, 4th ed., §2.3 (profilers) and §2.4 (leaks and
corruption), built into this engine and read through Connector.*

A frame spends three budgets, and only one of them usually has a tool pointed at
it:

| budget | measured by | read in Connector |
| --- | --- | --- |
| time | `eng::Profiler` — hierarchical, per-scope, per frame | `cpu: ms per phase` |
| heap | `eng::memprof` — every `operator new`, counted | `heap: *` |
| code size | `eng::codesize` — this binary's own symbol table | `code: bytes per module` |

All three publish to the telemetry channel `mem`, and all three are drawn as a
**treemap**: area is the quantity, so the big rectangle *is* the problem and you
do not have to read and compare fourteen numbers to find it.

---

## Why this, and not a standalone memory profiler

The book describes two kinds of profiler, and a memory profiler wants both:

- **Instrumenting** — hook everything, get exact numbers, accept the cost.
  `eng::memprof` replaces global `operator new`/`delete`, so *every* allocation
  is counted exactly: live bytes, live blocks, peak, and bytes allocated **this
  frame**. That last one is the number that matters in a game — a steady-state
  frame should allocate approximately nothing, and watching that number is how
  you find out it doesn't. Cost is two relaxed atomic adds and a 16-byte header.

- **Statistical** — sample periodically, stay fast enough to keep playing. One
  allocation in *N* also captures a call stack (`backtrace`), and the stacks are
  aggregated into a fixed table by hash. This is §2.3's "which 20% of the code"
  question, asked about bytes instead of milliseconds. Sampling is what makes it
  affordable: `backtrace()` costs microseconds, which is ruinous per allocation
  and free at 1-in-128.

The third aggregation is the one heaptrack, Massif and `make valgrind` **cannot**
do, and it is why this lives in-engine at all: allocations are attributed to the
**frame phase** that made them. `eng::Profiler::push` already brackets every
phase by name, so entering a profiled scope also tags the heap — "physics
allocated 400 KB this frame" falls out with no extra annotation at any call
site. A call stack tells you which function; a phase tells you which *part of
the frame*, which is what you can schedule, budget and pool away.

**Why not an ImGui HUD overlay?** Because this renderer draws the world into a
framebuffer a third of the window's size, and the image is frozen — pixels spent
on a heap overlay are pixels taken from the thing being debugged. `Telemetry.h`
already made this call for watches: *"anything you would have put on a debug HUD
belongs here instead, where it costs no screen space in the game and can be read
on a second monitor."* The browser also already has the plotting, the history and
the second monitor.

---

## Running it

`RelWithDebInfo` is the default build, and it turns the tools on by itself:

```sh
make                       # RelWithDebInfo; ENG_MEMPROF=1, Connector by default
python3 tools/connector/server.py --open      # the browser view
make run                   # the game; the "memory" tab appears on its own
```

**Not plain `Debug`.** Debug is `-O0`, which costs roughly half the frame rate
on this engine — Jolt, Ogre and the RHI all spend their time in small functions
that only inlining makes cheap — and buys nothing this needs. RelWithDebInfo
keeps full symbols, so gdb backtraces, `backtrace_symbols` and every call-stack
name below work exactly the same. Use `make BUILD_TYPE=Debug` only when you are
stepping through code the optimiser has rearranged.

| variable | effect |
| --- | --- |
| `RAVEN_MEMPROF=N` | call-stack sampling rate, 1-in-N. `0` keeps the exact counters and stops capturing stacks. Default 128. |
| `RAVEN_CONNECTOR=0` | don't attach, even in Debug. |
| `RAVEN_CONNECTOR=host:port` | attach somewhere else. |

`Release` is opt-in for Connector and can drop the heap instrumentation
entirely:

```sh
make BUILD_TYPE=Release CMAKE_ARGS=-DENG_MEMPROF=OFF
```

With `ENG_MEMPROF=OFF` the `operator new` overrides are not compiled, every
function in the header still exists and reports zero, and no call site needs an
`#ifdef`.

### The profiler must not become the stutter

Everything expensive here is computed once and memoised, because all of it feeds
a view a human reads a few times a second:

| work | cost | how often it is paid |
| --- | --- | --- |
| demangling the symbol table | ~290 ms | **once**, during load (`codesize::warm()`) |
| resolving a call stack to a name | ~ms each | once per distinct stack, ever; ≤4 new ones per second |
| the per-phase tile breakdown | trivial | 10 Hz |
| the churn graph | two atomic loads | every frame |

Two real regressions came out of getting this wrong, and both are worth knowing
because they are the failure mode of this kind of tool: re-reading the symbol
table on a timer produced a **290 ms freeze every ten seconds**, and
re-symbolising sixteen call stacks every second produced a **100 ms p99**. A
profiler that is the largest thing in its own report is measuring itself.

### From the console (F1)

```
mem              live bytes by frame phase, then the top allocation sites
mem.reset        zero the peak and the churn totals; live bytes stay
mem.sample N     change the sampling rate, or 0 to stop capturing stacks
mem.csv [path]   live/frame/total bytes per phase, for a spreadsheet
```

---

## The five treemaps

Each tab is the same drawing code over a different set of numbers. The engine
publishes `<group>.<name>` watches on channel `mem`; the browser groups by
prefix and sizes tiles by value.

| group | tab | what it means |
| --- | --- | --- |
| **heap** | live | live bytes, **exact**, by frame phase. Look here when memory is growing. |
| **heap** | per frame | bytes allocated last frame, **exact**, by phase. Look here when the game hitches. |
| **heap** | call site | live bytes by call stack, **estimated**. "Which code." |
| **cpu** | per phase | self ms per profiler scope. Self, not inclusive, so the parts sum to the frame. |
| **code** | per module | machine code per namespace/class, read from `/proc/self/exe`. |

Keyboard: `m` toggles the memory pane, `1`–`5` pick a view while it is up.
`?pane=memory&view=site` deep-links straight to one.

Tiles are a **monochrome red ramp**: the larger the tile, the darker it is.
Area already encodes the quantity, so colour doubles it rather than spending the
strongest visual channel on an arbitrary hash of the name — and lightness is the
one channel that survives being printed, squinted at, or seen by anyone
colourblind. The ramp is `share^0.4`, because a linear share leaves everything
but the biggest tile in the same pale wash and a ratio-of-logs compresses the
whole map into a few percent of lightness.

Tiles are laid out with a **squarified treemap** (Bruls, Huizing & van Wijk)
whose badness function targets the **golden ratio** rather than 1.0. Aiming at
squares gives a field of near-squares where only area distinguishes tiles; aiming
at φ gives a consistent grain, so the eye reads area rather than shape.

Adding a sixth view is a row in the `VIEWS` table in `app.js` and a
`telemetry::watchValue` in the engine. There is no new drawing code.

---

## What it costs

Per allocation, with `ENG_MEMPROF=1`:

- 16 bytes of header (which is why the header is 16 and not a more comfortable
  32 — a `malloc` block that was 16-aligned stays 16-aligned behind it);
- ~6 relaxed atomic adds;
- one `backtrace()` per *N* allocations, plus every allocation ≥ 64 KB — a
  single 8 MB allocation is not something to learn about one time in 128.

Nothing on the allocation path takes a lock or allocates. Every table is
fixed-capacity, because a profiler that allocates to record an allocation is a
profiler that recurses.

The publisher itself allocates (formatting telemetry records does), and it is
tagged `telemetry` so those bytes are charged honestly instead of landing on
`untagged` and looking like a leak in something else.

---

## Corruption detection (§2.4)

Every header carries a cookie derived from its size. On free, a header that does
not validate means something wrote past the end of the block *in front of* this
one. The block is still released — leaking on top of a corruption helps nobody —
but the event is counted, logged once, and surfaced as a `CORRUPT_HEADERS` watch.
Non-zero is a real bug, and it points at the neighbour, not the victim.

Freeing also clears the cookie, so a double free reports as a corruption rather
than silently decrementing the counters twice.

This does not replace `make valgrind` or `-DENABLE_ASAN=ON`, which see reads as
well as writes and know about stack and static memory too. It is the check that
is always on.

---

## Adding a tag by hand

Almost never needed — every `ENG_PROFILE` scope is already a tag, because
`Profiler::push` drives `memprof::pushTag`. For work outside a profiled scope:

```cpp
{
    ENG_MEM_TAG("level cook");   // string literal; the table stores the pointer
    cookLevel(...);              // everything allocated in here is charged here
}
```

Tags nest, follow the thread that opened them, and travel in the block's header
— so freeing a block on a different thread, under a different tag, still credits
the tag that allocated it.

---

## Reading the numbers honestly

- **`phase.`, `churn.` and `load.` are exact.** Every allocation is counted;
  self-ms comes from the same timers the profiler prints.
- **`site.` is an estimate**, and a carefully-scaled one. Blocks caught by the
  1-in-N sample stand for N blocks each; blocks caught for being large (≥64 KB)
  were *all* seen, so they are counted whole. Conflating the two made this
  report 1.6 GB live against an exact 38 MB. Treat the ordering as reliable and
  the magnitudes as approximate, and lower `mem.sample` if a site matters enough
  to measure properly.
- **`size.` counts function symbols only** — not data, headers, relocations or
  debug info — so it is smaller than the file on disk, and it needs a symbol
  table. A stripped binary reports nothing rather than guessing.
- **Live bytes are what was *requested*.** The allocator's own rounding and the
  16-byte headers are not in them; the header cost is reported separately as
  `header_overhead_kb`.
