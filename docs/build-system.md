# The build {#doc-build-system}

`make help` is the target reference. This document covers the parts that are
not self-evident from it: what the output is telling you, how the build stays
fast, and the two failures that look like a slow build rather than like a bug.

## `make doctor`

The first thing to run when the build behaves oddly.

```sh
make doctor          # report
make doctor FIX=1    # report, and repair what is safely repairable
```

It checks the toolchain, the state of the build tree, whether ccache is
actually caching, whether there is enough memory to compile in parallel, and
whether `compile_commands.json` is where clangd looks for it. Every check
exists because the failure it looks for cost real time here and none of them
announce themselves.

`FIX=1` is limited to caches and generated files -- a truncated dependency log,
a missing symlink, a re-run of `cmake`. It never touches object files or
source.

## Reading the output

A build prints a progress bar, the compiler diagnostics, and one summary line:

```
✔ forging the game · 27 steps · 1s · ccache 23/23 (100%)
```

Colour, the bar and the glyphs turn themselves off when stdout is not a
terminal, so a pipe, a log file and CI get plain text without configuring
anything. `PLAIN=1` (or `NO_COLOR=1`) forces that on a terminal too.

The renderer lives in `tools/build-ui.sh`, which hands over to
`tools/build_ui.py` when a python with `rich` is importable and otherwise draws
everything itself in bash. `make ui-deps` installs rich into `.cache/py`,
repo-locally. Bash is the floor on purpose: building the game must not depend
on a python package, and the machine missing it is usually the one where
something is already wrong. Both halves render the same information.

### Diagnostics are digested, not dumped

This tree emits around a hundred warnings, three to five lines each. Printed
verbatim that is four hundred lines per build, most of it from dependencies
nobody here can fix, and the one warning that is new is invisible in it. So:

- **Errors** print in full -- headline, include chain, instantiation stack,
  source echo and caret. They are the reason you are reading.
- **Warnings from this repository** collapse to one line each.
- **Warnings from dependencies** are counted and never printed.
- All of them are tallied into a table at the end, by flag, split into `ours`
  and `deps`, naming the file each comes from most.

```
warnings ──────────────────────────────────────────────────────────
warning                              ours    deps    mostly from
-Wswitch                               10            editor/src/content/SceneRepair.cpp  x10
-Wmissing-field-initializers            9      48    stb/stb_image_write.h  x48
```

The untouched transcript always goes to `build/last-build.log`, so nothing is
lost -- if a collapsed line is not enough, the full text is one file away.

## Never clean-build

Third-party sources are compiled from scratch. A full rebuild is many minutes,
and **killing a build mid-link corrupts the tree**. Build single targets, run
long builds in the background, and if the tree does break,
`cmake -S . -B build` regenerates the makefiles without discarding object
files. Never `rm -rf build`.

## The two silent slow builds

Both of these look exactly like the build simply being slow. Measure before
optimising anything.

### 1. A truncated ninja dependency log

Killing a build, or running two in one build directory, can truncate
`.ninja_deps`. Ninja meets a partial record on load, prints `premature end of
file; recovering`, and **discards every record after it**. The next build
recompiles all of it, writes its records past the same corruption, and loses
them again on the next load. An untouched tree then rebuilds from scratch,
forever, and never recovers on its own.

Diagnose it rather than guessing: `ninja -C build -d explain -n game` blames
`stored deps info out of date` / `deps are missing` instead of naming a changed
file, and `ninja -C build -t deps` opens with the `premature end of file`
warning.

`make` now runs that check itself before every build -- it costs about 60ms --
and resets the log when it finds the damage, so this should never again be
something you have to notice. `make build-reset` does it by hand;
`NO_DEPS_CHECK=1` skips the check. Either way it costs one rebuild and touches
no object files, so ccache makes the recovery cheap.

Concurrent invocations are what create the damage in the first place, so every
build rule takes an `flock` per build tree.

### 2. ccache disabled by a dependency

`ccache -s` read **96% uncacheable** on this tree, and no-op rebuilds cost
minutes, because a dependency turned ccache on for itself without scoping it to
itself. Assimp does:

```cmake
set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ${CCACHE_PATH})
```

`GLOBAL`, so the generator prepended it to every compile rule in the whole
project, ahead of the launcher `cmake/BuildOptions.cmake` had configured. Every
compile ran:

```
ccache  cmake -E env CCACHE_SLOPPINESS=... ccache  c++ -c foo.cpp
^ theirs                                   ^ ours
```

The outer ccache takes `cmake` for the compiler, sees several arguments that
look like input files, gives up with `multiple source files`, and execs the
line uncached. The inner one then finds itself nested inside another ccache and
reports `disabled`. Nothing was cached, and nothing said so. `RULE_LAUNCH_LINK`
got the same treatment, which also put a ccache in front of every `ar` and
`ranlib`.

The fix is in `cmake/Dependencies.cmake`: both projects are asked not to do it
(`ASSIMP_BUILD_USE_CCACHE OFF`, `SDL_CCACHE OFF`), and -- because those are
cache entries that an existing tree keeps, and because the next dependency will
try the same thing -- the `RULE_LAUNCH_*` global properties are cleared after
all dependencies are added. This tree's own launcher, which carries the
`CCACHE_SLOPPINESS` settings the precompiled headers need, is untouched.

After the fix, touching every file in `engine/src/core` and `engine/src/render`
and rebuilding: **27 steps, 1 second, 23/23 cache hits.**

`make doctor` checks for this directly, by looking for a second ccache in the
generated rules, rather than trusting the lifetime counters.

## Why `JOBS` is not `nproc`

The limit here is memory, not cores. Every heavy translation unit compiles
against a precompiled header -- ~620 MB across the targets, `eng_script`'s sol2
PCH alone at ~200 MB -- and a `cc1plus` optimising a renderer or binding TU on
top of that peaks around 1.5-2 GB resident. `nproc` jobs of that on a machine
without ~2 GB of headroom per core does not swap gracefully: it dies as
`internal compiler error: Bus error` or `cc1plus: out of memory`, on whichever
file happened to be unlucky.

That reads as a corrupt tree and is not one. `JOBS` therefore defaults to one
job per ~1.8 GB of `MemAvailable`, capped at the core count. Override it when
you know better: `make JOBS=8` on a machine with headroom, `make JOBS=1` when
already swapping or bisecting an ICE.

## Verify on screen, not just in the compiler

A change that compiles is not a change that works.

```sh
make screenshot SHOT=/tmp/x.png FRAME=200
```

Then actually read the PNG. Three real bugs here were invisible to the compiler
and obvious in a screenshot: a viewport showing the font atlas, a world
rendered upside down, and an entire scene stacked at the origin. A black
screenshot is usually the window being unfocused or offscreen, not a
regression -- confirm against a known-good binary before chasing it.

See `docs/debugging-renderdoc.md` before opening a GPU capture.

## The documentation site

`make docs` runs Doxygen over `engine/include`, `editor/`, `game/src`,
`samples/common` **and every page under `docs/`**, then opens the result. The
handbook and the generated C++ reference are one site: `docs/scenes.md` is a
page there exactly as `eng::Renderer` is.

Adding a page is one file. Give its H1 an anchor:

```markdown
# Occlusion culling {#doc-occlusion-culling}
```

and list it under a part in `docs/mainpage.md`:

```markdown
- @subpage doc-occlusion-culling — what the portal graph rejects before drawing.
```

A page with no anchor and no `@subpage` still ships; it just lands at the top
level of the tree instead of inside a part. Ordinary markdown links between
pages (`[the ECS](ecs.md)`, `[a section](#the-object-model)`) resolve on the
site and on GitHub both -- `MARKDOWN_ID_STYLE = GITHUB` is what keeps the
heading anchors identical in the two places.

`docs/` also holds imported material: a book conversion, vendored skill packs,
the survival kit's own built site. Those directories are listed in `EXCLUDE` in
`docs/Doxyfile.in`. Add a directory there if you drop another one in.

### The skin

Two stylesheets, in this order:

| Sheet | Owns |
|---|---|
| `docs/vulkan-impl-survival-kit/kit.css` | the palette and type scale, shared with the survival kit |
| `docs/doxygen-kit.css` | mapping Doxygen's markup onto them |

Change a colour in the first one. The second restates Doxygen's ~160 CSS custom
properties in terms of the kit's dozen tokens, which is why the whole generated
site follows the palette rather than only the parts the skin names. That bridge
only exists when `HTML_COLORSTYLE` is `TOGGLE` or an `AUTO_*` value -- under
`LIGHT` or `DARK`, Doxygen bakes literal hex into `doxygen.css` and every
unnamed corner goes back to being white. The light/dark control in the title
bar is the same setting.

Two traps live in that seam, both from generic rules in `kit.css` meeting
Doxygen's markup, and both are commented where they are fixed: `.entry` means
different things in the two sheets, and `* { box-sizing: border-box }` breaks
the navigation tree's indentation. If a listing suddenly renders one word per
line, look for a third one the same way -- compare `kit.css`'s class selectors
against the `class="..."` values in `build/docs/html`.
