# Connector {#doc-connector}

A browser window onto the engine's debug channels, after Naughty Dog's tool of
the same name (*Game Engine Architecture*, 4th ed., 3.4.4): the engine writes
text into channels named for the systems that produce it, a key-value store
collects them, and a browser filters the result.

```sh
make connector          # collector + web UI at http://127.0.0.1:8777
make run-connected      # the game, reporting into it
RAVEN_CONNECTOR=1 ./build/scene_editor assets/scenes/cozy_lair.scn
```

## Redis is optional

The engine only ever speaks Redis's wire protocol (RESP). Where that lands is
your choice:

- **Default.** `make connector` accepts RESP itself, implementing the four
  commands the sink uses. No Redis to install.
- **`make connector REDIS=1`.** Subscribes to a real Redis instead — the book's
  arrangement, and what you want when several machines or several game
  instances report into one place.

The engine cannot tell the difference. Requiring a Redis install to read a log
is friction on Linux, awkward on macOS and unpleasant on Windows, and a debug
channel is worth having precisely when you have not set anything up.

## What a channel is

Channels are **discovered, not declared** — the first write to a name registers
it. Adding one is a call site:

```cpp
ENG_TELEMETRY("ai", eng::telemetry::Level::Warn, "%s lost its path", name);
```

Nearly every line the engine already writes names its own system (`RHI
renderer:`, `EnemySpawner:`, `AudioCatalog:`), and `channelForLogLine` reads
that prefix, so thousands of existing `eng::log` call sites became real
channels without one of them being touched. An unrecognised prefix gets its own
channel rather than a catch-all, so a system added tomorrow appears by itself.

## Four kinds of record

The distinction that makes this more than a log tail:

| Kind | Call | In the browser |
|---|---|---|
| Log | `ENG_TELEMETRY` | a line; scrolls |
| Watch | `watch`, `watchf`, `watchValue` | a value that **updates in place**, with a sparkline |
| Sample | `sample` | a number over time |
| Event | `event` | a marker, pinned red; what you navigate by |

**Watches are the ones that change how a session feels.** A player position
written to a log sixty times a second is noise you filter out; the same value as
a watch is a number you glance at. Anything you would have put on a debug HUD
belongs here instead, where it costs no screen space in the game and can be read
on a second monitor.

The engine ships `frame.fps/frame_ms/game_ms`, `render.batches/triangles`; the
game adds `player.pos/speed/grounded/weapon` and a `combat` channel; the editor
adds `editor.scene/entities/selected/dirty/mode/camera`, an `edit` channel
carrying every command that runs (the undo stack, readable and searchable), and
`scene` events for loads and isolation.

### Reading the channel rail

A channel's count is its **log lines**. A channel that only ever emits watches
shows `~N` instead, dimmed — that is the number of *values* it publishes, and
they are in the panel above, not waiting in the log. `frame` and `player` are
watch-only; `render` has both.

The distinction matters because without it the rail lies: publishing
`render.batches` sixty times a second made the count read 900+, next to a pane
with five lines in it, and clicking the channel appeared to do nothing.
Toggling a channel now hides its watches as well as its lines, so the switch
means the same thing everywhere.

## In the browser

- **Filter** — plain text, or `/regex/`. Matches message and channel; hits are
  highlighted. `/` focuses it.
- **Channels** — click to toggle, **alt-click to solo**.
- **Levels** — trace/info/warn/error. Trace is off by default.
- **Click a frame number** to see everything that happened on that frame. Every
  record is stamped at write time with the frame that produced it, which is the
  question a flat log cannot answer.
- **pause** (space) holds the view while records keep arriving; resuming shows
  what you missed rather than skipping it.
- **export** downloads what is on screen, filters and all, for a bug report.
- **`?nostream=1`** loads the scrollback without a live connection — for reading
  a captured session without the tail moving under you.

## Cost when nobody is listening

Nothing. `RAVEN_CONNECTOR` is unset in a normal run, so no socket opens and no
thread starts; `enabled()` is an atomic load and `ENG_TELEMETRY` is a macro, so
a disabled channel never formats its arguments.

When it *is* listening, the game thread writes into a ring buffer and returns.
A publisher thread does the encoding and the blocking I/O. A full ring drops its
oldest records and counts them — a debug channel that applies back-pressure to
the simulation is a debug channel that changes the bug you opened it to find.
`telemetry_tests` pins that: twenty thousand writes into a sixteen-slot ring
with a sink that never succeeds must not block the caller.

## Platforms

`engine/src/telemetry/Socket.cpp` is the only file that knows which OS this is.
Winsock needs `WSAStartup` and `closesocket`; BSD sockets use `close` and
`errno`; macOS needs `SO_NOSIGPIPE` where Linux passes `MSG_NOSIGNAL` per send —
miss that one and a write to a closed collector kills the process. Connect uses
a non-blocking socket with an explicit timeout because the platform default is
over a minute, and a debug sink that hangs a thread at start-up because nothing
is listening is worse than one that never connects.

The server is standard-library Python, so it runs from a checkout on all three
with no `pip` step.

## Files

| What | Where |
|---|---|
| Channels, ring, publisher thread | `engine/include/eng/telemetry/Telemetry.h`, `engine/src/telemetry/Telemetry.cpp` |
| RESP encode/classify | `engine/src/telemetry/Resp.cpp` |
| Cross-platform TCP | `engine/src/telemetry/Socket.cpp` |
| Redis sink | `engine/src/telemetry/RedisSink.cpp` |
| Collector + web server | `tools/connector/server.py` |
| UI | `tools/connector/ui/` |
| Tests | `engine/tests/TelemetryTests.cpp` |
