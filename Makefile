# Convenience wrapper around the CMake build + run/test CLI.
#
# Quick start:
#   make            configure (if needed) + build the game
#   make run        build + run the game
#   make help       full target + option reference
#
# Run options are plain make variables mapped to the game's RAVEN_* env vars, e.g.
#   make run SEED=42 PRESET=ps1            # seed + render preset
#   make run MAP=level.map                 # play an authored .map
#   make run SHOWROOM=assets/config/showroom.toml
#   make run COLLIDERS=1 WIREFRAME=1       # debug overlays
#   make screenshot SHOT=/tmp/x.png FRAME=200
#   make prefab-viewer PRESET=modern-ps1   # compact turntable scene
#   make sim SCRIPT=game/sim/scripts/smoke.txt
#
# Every app target (game, editor, demo) shares the same run/debug options, so
# `make gdb APP=scene_editor` and `make renderdoc APP=scene_editor` work exactly
# like they do for the game.
#
# Output is coloured and carries a progress bar when a terminal is attached, and
# is plain text when it is not -- so a pipe, a log file and CI all get the same
# text they always did. Force plain with PLAIN=1 (or NO_COLOR=1).
#
#   make doctor      diagnose the build tree, the toolchain and ccache
#   make doctor FIX=1  and repair what is safely repairable

# ---- configuration ---------------------------------------------------------
BUILD_DIR   ?= build
# RelWithDebInfo by default: a development build that is still a *game*.
#
# It keeps everything that makes this tree debuggable -- full symbols, usable
# gdb backtraces, the heap profiler's call-stack names, Connector attached by
# default -- while compiling -O2. Plain Debug is -O0, which on this engine costs
# roughly half the frame rate for no debugging benefit that RelWithDebInfo does
# not already give: Jolt, Ogre and the RHI all spend their time in small
# functions that only inlining makes cheap.
#
#   make BUILD_TYPE=Debug ...            -O0, for stepping through optimised-out
#                                        locals or chasing an inlining bug
#   make BUILD_TYPE=Release ...          to measure or to ship
#
# Switching type rebuilds the whole tree, third party included.
BUILD_TYPE  ?= RelWithDebInfo
# Parallelism, capped by MEMORY rather than by core count.
#
# This tree's limit is not the CPU. Every heavy translation unit is compiled
# against a precompiled header -- they total ~620 MB across the targets, with
# eng_script's sol2 PCH alone at ~200 MB -- and a cc1plus optimising one of the
# renderer or binding TUs on top of that peaks around 1.5-2 GB resident. `nproc`
# jobs of that on a machine with less than 2 GB of headroom per core does not
# swap gracefully: it dies, as `internal compiler error: Bus error` or
# `cc1plus: out of memory`, on whichever file happened to be unlucky. That reads
# as a corrupt tree and is not one, which is the trap -- see the note in
# CLAUDE.md.
#
# So: one job per ~1.8 GB currently available, never more than there are cores,
# never fewer than one. MemAvailable rather than MemTotal because the browser
# and the language server on a development machine are real memory that the
# compiler cannot have. Override explicitly when you know better:
#
#   make JOBS=8            # a machine with headroom
#   make JOBS=1            # already swapping, or bisecting an ICE
_mem_avail_mb := $(shell awk '/MemAvailable/{printf "%d", $$2/1024}' /proc/meminfo 2>/dev/null)
_mem_jobs     := $(shell echo $$(( $(or $(_mem_avail_mb),4096) / 1800 )))
JOBS        ?= $(shell n=$$(nproc); m=$(_mem_jobs); [ $$m -lt 1 ] && m=1; \
                       [ $$m -lt $$n ] && echo $$m || echo $$n)
# One build at a time per tree. Two ninja runs in the same build directory
# interleave their appends to .ninja_deps; ninja then meets a partial record on
# the next load, prints "premature end of file; recovering", and TRUNCATES the
# log at that point -- silently discarding the dependency info for everything
# recorded after it. The tree then rebuilds from scratch on every invocation and
# never recovers on its own, because each new build's records land after the
# corruption and are dropped again on the next load. That is a full rebuild for
# an untouched tree, forever. Serialising invocations is what prevents it.
#
# If a tree is already in that state, `make build-reset` clears the log.
FLOCK       := $(shell command -v flock 2>/dev/null)
BUILD_LOCK   = $(if $(FLOCK),$(FLOCK) $(BUILD_DIR)/.build.lock,)
# Ninja owns fresh project build trees by default. Existing trees keep their
# cached generator because CMake cannot switch one in place; an explicit
# GENERATOR override still reports a mismatch instead of deleting user data.
GENERATOR   ?= Ninja
GENERATOR_ORIGIN := $(origin GENERATOR)
# Extra cache entries, e.g. CMAKE_ARGS='-DENABLE_UNITY=ON -DENABLE_LTO=ON'.
CMAKE_ARGS  ?=
# The visual-test harness. Distributions that dropped the unversioned `python`
# (Debian/Ubuntu, Fedora without python-unversioned-command) made every
# visual-test target fail with "python: No such file or directory", which reads
# as a broken harness rather than as a missing alias.
PYTHON      ?= $(shell command -v python3 2>/dev/null || echo python)
# Force X11 on Wayland (XWayland). Override with SDL_VIDEODRIVER=... on the
# command line if needed.
SDL_VIDEODRIVER ?= x11

# ---- build UI --------------------------------------------------------------
# Colour, log lines and the progress bar live in one script rather than in the
# recipes, for two reasons: the recipes stay readable, and there is exactly one
# place that decides whether this invocation has a terminal. Without one -- a
# pipe, a log, CI -- the script emits the same plain text the recipes used to,
# so nothing downstream has to learn about escape codes.
#
#   make PLAIN=1 ...     plain output on a terminal too (also NO_COLOR=1)
#
# The script is decoration only: it runs the command it is given and passes the
# exit status through, so deleting it costs the colour and nothing else.
UI          := $(CURDIR)/tools/build-ui.sh
DOCTOR      := $(CURDIR)/tools/build-doctor.sh
export RAVEN_BUILD_PLAIN := $(if $(PLAIN),1,)
# Where `run` keeps the raw, uncoloured transcript of the last build, so a
# compiler error that scrolled past the progress bar is still readable.
export RAVEN_BUILD_LOG_DIR := $(abspath $(BUILD_DIR))

# Every build rule goes through this: one lock, one progress bar, one summary.
#   $(call forge,<target>,<label shown while it builds>)
forge = @$(UI) run "$(2)" -- $(BUILD_LOCK) cmake --build $(BUILD_DIR) --target $(1) -j$(JOBS)

# ---- run-option -> RAVEN_* env mapping -------------------------------------
# Each variable is only exported when the user sets it, so unset options keep
# the game's own defaults. Add a mapping here to expose a new env var.
RUN_ENV := SDL_VIDEODRIVER=$(SDL_VIDEODRIVER)
ifdef SEED
RUN_ENV += RAVEN_GEN_SEED=$(SEED)
endif
ifdef PRESET
RUN_ENV += RAVEN_RENDER_PRESET=$(PRESET)
endif
ifdef SHOT
RUN_ENV += RAVEN_SCREENSHOT=$(SHOT)
endif
ifdef FRAME
RUN_ENV += RAVEN_SCREENSHOT_FRAME=$(FRAME)
endif
ifdef FIXED_DT
RUN_ENV += RAVEN_FIXED_DT=$(FIXED_DT)
endif
ifdef BENCH
RUN_ENV += RAVEN_BENCH_FRAMES=$(BENCH)
endif
ifdef PROFILE
RUN_ENV += RAVEN_PROFILE=$(PROFILE)
endif
ifdef COLLIDERS
RUN_ENV += RAVEN_SHOW_COLLIDERS=$(COLLIDERS)
endif
ifdef WIREFRAME
RUN_ENV += RAVEN_WIREFRAME=$(WIREFRAME)
endif
ifdef PORTAL
RUN_ENV += RAVEN_SHOWCASE_PORTAL=$(PORTAL)
endif
ifdef SHOWROOM
RUN_ENV += RAVEN_SHOWROOM_MAP=$(abspath $(SHOWROOM))
endif
ifdef MATERIAL
RUN_ENV += RAVEN_EDITOR_MATERIAL=$(MATERIAL)
endif
ifdef RENDERDOC_FRAME
RUN_ENV += RAVEN_RENDERDOC_FRAME=$(RENDERDOC_FRAME)
endif
ifdef RENDERDOC_OUT
RUN_ENV += RAVEN_RENDERDOC_CAPTURE=$(abspath $(RENDERDOC_OUT))
endif
# Positional game argument (e.g. a .map file to play).
RUN_ARGS := $(MAP)

# Which executable the generic app targets drive. Every target below that takes
# APP= works for any of these, because they are all eng::Application consumers.
APP        ?= game
APP_TARGET := $(if $(filter scene_editor,$(APP)),scene_editor,\
              $(if $(filter psx_demo,$(APP)),psx_demo,game))

.PHONY: all configure build build-all build-app build-game build-demo build-mapgen build-sim \
        build-editor build-cook build-acp build-player build-export \
        build-reset doctor ui-deps play export \
        acp acp-check acp-clean assetdb \
        editor cook scene material prefab-viewer \
        run game demo psx-demo mapgen sim test asan bench screenshot visual-test \
        editor-selftest clip clip-mp4 look new-clip assetformats \
        visual-bench renderdoc-capture renderdoc gdb valgrind perf deps docs \
        vulkan-kit asset debug debug-run clean help help-text deps-guard \
        connector run-connected

all: build

# ---- build -----------------------------------------------------------------
# Avoid re-running CMake on every invocation. CMake's generated build system
# still performs its own dependency check, so edits to CMake inputs regenerate
# normally when cmake --build runs.
configure:
	@$(UI) banner "$(BUILD_TYPE) $(GENERATOR) $(JOBS) jobs -> $(BUILD_DIR)/"
	@if [ ! -f "$(BUILD_DIR)/CMakeCache.txt" ] && [ "$(GENERATOR)" = "Ninja" ] && ! command -v ninja >/dev/null 2>&1; then \
		$(UI) err "ninja not found -- run 'make deps' or install Ninja"; \
		exit 2; \
	fi
	@if [ -f "$(BUILD_DIR)/CMakeCache.txt" ]; then \
		actual=$$(grep '^CMAKE_GENERATOR:INTERNAL=' "$(BUILD_DIR)/CMakeCache.txt" | cut -d= -f2-); \
		if [ -n "$$actual" ] && [ "$$actual" != "$(GENERATOR)" ] && [ "$(GENERATOR_ORIGIN)" != "file" ]; then \
			$(UI) err "$(BUILD_DIR) uses '$$actual', requested '$(GENERATOR)'"; \
			$(UI) note "choose another BUILD_DIR or remove that build tree before switching generators"; \
			exit 2; \
		fi; \
	fi
	@if [ ! -f "$(BUILD_DIR)/CMakeCache.txt" ]; then \
		$(UI) run "configuring" -- cmake -B "$(BUILD_DIR)" -G "$(GENERATOR)" \
		      -DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" \
		      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON $(CMAKE_ARGS); \
	elif [ -n "$(strip $(CMAKE_ARGS))" ] || ! grep -Fqx "CMAKE_BUILD_TYPE:STRING=$(BUILD_TYPE)" "$(BUILD_DIR)/CMakeCache.txt"; then \
		$(UI) run "reconfiguring" -- cmake -B "$(BUILD_DIR)" -DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" \
		      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON $(CMAKE_ARGS); \
	fi
	@$(MAKE) --no-print-directory deps-guard

# A truncated .ninja_deps is invisible: the tree simply rebuilds itself in full,
# every time, forever, and never recovers on its own (see build-reset). The
# check costs ~60ms -- ninja prints its warning on stderr and we read only that
# -- which is cheap enough to run in front of every build and so turn a
# permanent, silent full rebuild into one automatic recovery.
#
#   make NO_DEPS_CHECK=1 ...     skip it
deps-guard:
ifndef NO_DEPS_CHECK
	@if [ -f "$(BUILD_DIR)/.ninja_deps" ] && \
	    ninja -C "$(BUILD_DIR)" -t deps 2>&1 >/dev/null | grep -q "premature end of file"; then \
		$(UI) warn "ninja dependency log is truncated -- resetting it"; \
		$(UI) note "this build is a full one; the next is incremental again"; \
		rm -f "$(BUILD_DIR)/.ninja_deps" "$(BUILD_DIR)/.ninja_log"; \
	fi
endif

# Recover a tree whose dependency log has been truncated. Symptom: every build
# recompiles everything even with no edits, and
#   ninja -C build -d explain -n game
# blames "stored deps info out of date" / "deps are missing" rather than naming
# a changed file. Confirm with `ninja -C build -t deps` -- a corrupt log opens
# with "premature end of file; recovering" and marks entries STALE.
#
# .ninja_deps is a cache, not build output: deleting it costs one full rebuild
# and nothing else. It is deliberately not part of `clean`, and it does NOT
# touch object files, so ccache makes the recovery build cheap.
build-reset:
	@rm -f $(BUILD_DIR)/.ninja_deps $(BUILD_DIR)/.ninja_log
	@$(UI) ok "cleared $(BUILD_DIR) dependency log; the next build is a full one"

# What is wrong with this tree, and (FIX=1) the repairs that are safe to make.
# Covers the failures that look like a slow build rather than like a fault:
# a truncated deps log, ccache disabled by a dependency, no memory headroom.
doctor:
	@$(DOCTOR)

# The optional half of the build UI. rich goes into a repo-local venv so the
# system python is untouched and 'make clean' territory stays out of it; the
# bash renderer keeps working if this is never run.
ui-deps:
	@$(UI) step "installing the rich build UI into .cache/py"
	@$(PYTHON) -m venv .cache/py 2>/dev/null || $(PYTHON) -m venv --without-pip .cache/py
	@.cache/py/bin/python -m pip install --quiet --upgrade rich \
	    && $(UI) ok "rich installed; builds now use the python renderer" \
	    || $(UI) warn "could not install rich; the bash renderer stays in use"

build-all: configure
	$(call forge,all,building all targets)

# Run-oriented commands build only their required target. In particular this
# keeps the demo, map generator, simulation harness, and test executables out
# of the edit/build/run loop for the game.
build-game: configure
	$(call forge,game,building game)

build: build-game

build-demo: configure
	$(call forge,psx_demo,building psx_demo)

build-mapgen: configure
	$(call forge,mapgen,building mapgen)

build-sim: configure
	$(call forge,game_sim,building game_sim)

build-editor: configure
	$(call forge,scene_editor,building scene_editor)

build-cook: configure
	$(call forge,scene_cook,building scene_cook)

build-acp: configure
	$(call forge,raven_acp,building raven_acp)

build-player: configure
	$(call forge,raven_player,building raven_player)

build-export: configure
	$(call forge,raven_export,building raven_export)

# The generic app build, for the APP=-driven targets below.
build-app: configure
	$(call forge,$(APP_TARGET),building $(APP_TARGET))

# Detects pacman/apt/dnf/zypper/apk/brew; installs toolchain + SDL2 + glm,
# plus the Vulkan loader and the SPIR-V toolchain the RHI compiles through.
deps:
	./tools/install-deps.sh

# ---- run -------------------------------------------------------------------
# `run` is the primary entry point; `game` is a back-compat alias.
run game: build-game
	@$(UI) step "running game$(if $(RUN_ARGS), $(RUN_ARGS))$(if $(SEED), seed $(SEED))$(if $(PRESET), preset $(PRESET))"
	cd $(BUILD_DIR) && env $(RUN_ENV) ./game $(RUN_ARGS)

# ---- the model showroom ----------------------------------------------------
# A small dressed chamber whose only moving part is the model turning on the
# plinth: three stage lights, a framed camera, and nothing else that moves. It
# is the fastest way to look at an asset in the game's own renderer.
#
#   make demo                                   play what the .scn currently says
#   make demo MODEL=kit.prop_chest              swap the model
#   make demo MODEL=meshes/props/prop_malenia.obj FIT=2.4
#   make demo SHOT=/tmp/x.png FRAME=200         one frame instead of playing
#   make demo LIST=1                            what MODEL= will accept
#
# MODEL takes a kit prefab id from assets/config/kit.toml or any mesh path under
# assets/; either way the model is measured, scaled to FIT metres and stood on
# the plinth, so a prefab authored in centimetres frames like one authored in
# metres. FIT/SPIN/YAW default to the values in tools/author_turntable.py.
#
# A plain `make demo` cooks and plays the checked-in scene, so anything you drag
# in the editor survives. Passing any of MODEL/FIT/SPIN/YAW re-authors the file
# from the script first, which discards hand edits -- move a value you liked
# into the STAGE block at the top of the script.
DEMO_SCENE  = assets/scenes/turntable.scn
DEMO_AUTHOR = $(if $(MODEL),--subject "$(MODEL)",)$(if $(FIT), --fit $(FIT),)\
$(if $(SPIN), --spin $(SPIN),)$(if $(YAW), --yaw $(YAW),)

demo: build-cook build-game
ifdef LIST
	@$(PYTHON) tools/author_turntable.py --list-subjects
else
	$(if $(strip $(DEMO_AUTHOR)),$(PYTHON) tools/author_turntable.py \
	    --output $(DEMO_SCENE) $(DEMO_AUTHOR),)
	$(if $(SHOT),$(MAKE) look SCENE=$(DEMO_SCENE) SHOT=$(SHOT) \
	    $(if $(FRAME),FRAME=$(FRAME),),$(MAKE) scene SCENE=$(DEMO_SCENE))
endif

# The PSX shader sample, which `demo` used to be.
psx-demo: build-demo
	cd $(BUILD_DIR) && env $(RUN_ENV) ./psx_demo

# ---- editor ----------------------------------------------------------------
# The placement editor. SCENE= opens a specific .scn; with none it opens the
# shipped showroom scene.
#   make editor
#   make editor SCENE=assets/scenes/ritual_boss_showroom.scn
#   make material                  open straight into the material staging scene
editor: build-editor
	cd $(BUILD_DIR) && env $(RUN_ENV) ./scene_editor $(if $(SCENE),$(abspath $(SCENE)),)

material: build-editor
	cd $(BUILD_DIR) && env $(RUN_ENV) RAVEN_EDITOR_MATERIAL=1 \
	    ./scene_editor $(if $(SCENE),$(abspath $(SCENE)),)

# ---- projects --------------------------------------------------------------
# A project is a directory with a project.toml: somebody else's game, made in
# this editor and played by a runtime with none of this game's code in it. See
# docs/projects.md.
#
#   make play PROJECT=~/games/my-game       play it
#   make export PROJECT=~/games/my-game     build a distributable beside it
#   make export PROJECT=... OUT=~/ship
play: build-player
	@test -n "$(PROJECT)" || { echo "usage: make play PROJECT=<dir>"; exit 2; }
	cd $(BUILD_DIR) && env $(RUN_ENV) ./raven_player $(abspath $(PROJECT))

export: build-player build-export
	@test -n "$(PROJECT)" || { echo "usage: make export PROJECT=<dir> [OUT=<dir>]"; exit 2; }
	cd $(BUILD_DIR) && env $(RUN_ENV) ./raven_export $(abspath $(PROJECT)) \
	    --out $(if $(OUT),$(abspath $(OUT)),$(abspath $(PROJECT))-build) \
	    --player ./raven_player --overwrite

# ---- asset pipeline --------------------------------------------------------
# The Asset Conditioning Pipeline: every DCC source in assets/ through its
# exporter, into a pack the game loads instead. Incremental -- a warm run is
# under a second -- so this is safe to put in front of `run`.
#
#   make acp                     condition everything
#   make acp TYPE=mesh           one row of the pipeline
#   make acp FILTER=viewmodel    one subtree
#   make acp FORCE=1             ignore the build keys
#   make acp-check               fail if anything is stale (what CI runs)
#   make assetdb                 the resource database: what is tracked
#   make assetdb STAMP=1         write a .meta for every asset that lacks one
#
# The game picks the pack up automatically from build/cooked. To run without
# it -- to prove the conditioned and source paths agree -- set
# RAVEN_COOKED_DIR=/dev/null.
acp: build-acp
	./$(BUILD_DIR)/raven_acp build \
	    $(if $(TYPE),--type $(TYPE)) $(if $(FILTER),--filter $(FILTER)) \
	    $(if $(FORCE),--force) $(if $(STAMP),--stamp) $(if $(QUIET),--quiet)

acp-check: build-acp
	./$(BUILD_DIR)/raven_acp build --check

acp-clean:
	rm -rf $(BUILD_DIR)/cooked

assetdb: build-acp
	./$(BUILD_DIR)/raven_acp db $(if $(STAMP),--stamp) $(if $(LIST),--list) \
	    $(if $(TYPE),--type $(TYPE))

# Which extension belongs to which row of the pipeline.
assetformats: build-acp
	./$(BUILD_DIR)/raven_acp formats

# Cook an authored .scn into a runtime .map -- the same cooker the editor calls
# in-process, which is what makes the two produce identical bytes.
#   make cook SCENE=assets/scenes/ritual_boss_showroom.scn
#   make cook SCENE=... OUT=/tmp/level.map
#   make cook SCENE=... VALIDATE=1        report issues, write nothing
cook: build-cook
ifndef SCENE
	$(error set SCENE=<file.scn> (optional OUT=<file.map>, VALIDATE=1))
endif
	./$(BUILD_DIR)/scene_cook $(abspath $(SCENE)) \
	    --kit $(abspath assets/config/kit.toml) \
	    $(if $(VALIDATE),--validate-only,--out $(if $(OUT),$(abspath $(OUT)),$(abspath $(basename $(SCENE)).map)))

# Cook a scene and immediately play it: the editor's F5, from the shell.
scene: build-cook build-game
ifndef SCENE
	$(error set SCENE=<file.scn>)
endif
	$(MAKE) cook SCENE=$(SCENE) OUT=$(BUILD_DIR)/scene.map
	cd $(BUILD_DIR) && env $(RUN_ENV) ./game scene.map

# Compact imported-prefab turntable. PREFAB= is an id from kit.toml.
# PRESET= uses the common run option;
# VIEWER_PRESET= supplies this target's default when PRESET is omitted. Explicit
# CLI options in VIEWER_FLAGS= win over either, matching Engine's normal
# command-line > environment > default precedence.
#
#   make prefab-viewer
#   make prefab-viewer PREFAB=kit.prop_raccoon_head SUBJECT_SCALE=0.8
#   make prefab-viewer PRESET=ps1 WIREFRAME=1
#   make prefab-viewer VIEWER_PRESET=pixel-3d
#   make prefab-viewer VIEWER_FLAGS="--render-preset psx-horror"
#   make prefab-viewer SHOT=/tmp/prefab.png FRAME=200
PREFAB          ?= kit.prop_boss_placeholder
SUBJECT_NAME    ?=
SUBJECT_MATERIAL ?=
SUBJECT_SCALE   ?= 1.0
SUBJECT_YAW     ?= -20.0
SUBJECT_Y       ?= 0.0
GROUND_CLEARANCE ?= 0.02
VIEWER_PRESET   ?= modern-ps1
VIEWER_FLAGS    ?=
PREFAB_SCENE     = $(BUILD_DIR)/prefab-viewer.scn
PREFAB_MAP       = $(BUILD_DIR)/prefab-viewer.map

prefab-viewer: build-cook build-game
	$(PYTHON) tools/author_cozy_lair.py --output $(PREFAB_SCENE) \
	    --prefab "$(PREFAB)" --subject-scale $(SUBJECT_SCALE) --subject-yaw $(SUBJECT_YAW) \
	    --subject-y $(SUBJECT_Y) --ground-clearance $(GROUND_CLEARANCE) \
	    $(if $(SUBJECT_NAME),--subject-name "$(SUBJECT_NAME)",) \
	    $(if $(SUBJECT_MATERIAL),--subject-material "$(SUBJECT_MATERIAL)",)
	$(MAKE) cook SCENE=$(PREFAB_SCENE) OUT=$(PREFAB_MAP)
	cd $(BUILD_DIR) && env $(RUN_ENV) \
	    $(if $(PRESET),,RAVEN_RENDER_PRESET=$(VIEWER_PRESET)) \
	    ./game $(abspath $(PREFAB_MAP)) $(VIEWER_FLAGS)

# --- clips ------------------------------------------------------------------
# A scene that authors a Camera plays itself, which makes it the one thing in
# this project that can be filmed without a hand on the mouse. These wrap that.
#
#   make clip SCENE=assets/scenes/spin_portal.scn
#   make clip SCENE=... SECONDS=6 WIDTH=480 OUT=docs/media/teaser
#   make clip SCENE=... MP4=1              also encode an .mp4 beside the .gif
#
# Cooks first, so the clip is always of what the .scn currently says. Recording
# pins the simulation timestep, so the same scene films identically on a fast
# machine and a slow one.
CLIP_SECONDS ?= 10
CLIP_FPS     ?= 20
CLIP_WIDTH   ?= 320
# Warm-up frames dropped before the first capture: the level is fully built by
# then, so a load hitch is not baked into the clip's timing.
CLIP_START   ?= 60
CLIP_FRAMES  := $(shell expr $(CLIP_SECONDS) \* $(CLIP_FPS))
CLIP_OUT      = $(if $(OUT),$(OUT),docs/media/$(basename $(notdir $(SCENE))))
CLIP_DIR      = $(BUILD_DIR)/clip-frames

clip: build-cook build-game
ifndef SCENE
	$(error set SCENE=<file.scn> (optional OUT=<path-without-extension>, SECONDS=, FPS=, WIDTH=, MP4=1))
endif
	@mkdir -p $(dir $(CLIP_OUT))
	$(MAKE) cook SCENE=$(SCENE) OUT=$(BUILD_DIR)/clip.map
	@rm -rf $(CLIP_DIR)
	cd $(BUILD_DIR) && env $(RUN_ENV) ./game clip.map \
	    --record $(abspath $(CLIP_OUT)).gif \
	    --record-frames $(CLIP_FRAMES) --record-fps $(CLIP_FPS) \
	    --record-start $(CLIP_START) --record-width $(CLIP_WIDTH) \
	    --record-keep-frames --record-frame-dir clip-frames
	$(if $(MP4),$(MAKE) clip-mp4 OUT=$(CLIP_OUT),)
	@$(UI) ok "wrote $(CLIP_OUT).gif ($(CLIP_SECONDS)s at $(CLIP_FPS) fps)"

# Re-encodes the frames the last `make clip` kept, so a second format costs no
# second run of the game. Nearest-neighbour scaling: bilinear turns a
# low-resolution retro image into mush.
clip-mp4:
	ffmpeg -y -loglevel error -framerate $(CLIP_FPS) \
	    -i $(CLIP_DIR)/frame_%05d.png -c:v libx264 -pix_fmt yuv420p -crf 20 \
	    -vf "scale=720:-2:flags=neighbor" $(CLIP_OUT).mp4
	@$(UI) ok "wrote $(CLIP_OUT).mp4"

# One frame of a scene, for a look rather than a clip. The fast loop while
# framing a shot: edit the .scn, run this, read the PNG.
#   make look SCENE=assets/scenes/spin_portal.scn
#   make look SCENE=... FRAME=400 SHOT=/tmp/x.png
look: build-cook build-game
ifndef SCENE
	$(error set SCENE=<file.scn> (optional FRAME=<n>, SHOT=<path.png>))
endif
	$(MAKE) cook SCENE=$(SCENE) OUT=$(BUILD_DIR)/clip.map
	cd $(BUILD_DIR) && env $(RUN_ENV) \
	    RAVEN_SCREENSHOT=$(if $(SHOT),$(abspath $(SHOT)),$(abspath $(BUILD_DIR))/look.png) \
	    RAVEN_SCREENSHOT_FRAME=$(if $(FRAME),$(FRAME),200) ./game clip.map
	@$(UI) ok "wrote $(if $(SHOT),$(SHOT),$(BUILD_DIR)/look.png)"

# Start a new shot from the one that works: copies the example scene under a new
# name and opens it. Beats an empty document, because a shot is mostly lighting
# and framing and those are the parts nobody wants to re-derive.
#   make new-clip NAME=my_teaser
new-clip: build-editor
ifndef NAME
	$(error set NAME=<scene-name>)
endif
	@test ! -f assets/scenes/$(NAME).scn || \
	    ($(UI) err "assets/scenes/$(NAME).scn already exists" && false)
	@$(PYTHON) -c "import json,sys; \
d=json.load(open('assets/scenes/spin_portal.scn')); \
d['id']='scene.$(NAME)'; \
json.dump(d, open('assets/scenes/$(NAME).scn','w'), indent=2)"
	@$(UI) ok "created assets/scenes/$(NAME).scn from spin_portal"
	$(MAKE) editor SCENE=assets/scenes/$(NAME).scn

# Blender -> engine. The front of the asset pipeline, which had no target:
#   make asset BLEND=assets/source/models/Raccoon_Head.blend LIST=1
#   make asset BLEND=... OBJECT=Mapache NAME=prop_raccoon_head
# Prints the `size = [...]` line a kit.toml entry needs, so the next step is a
# paste rather than a measurement. See docs/assets-pipeline.md.
asset:
ifndef BLEND
	$(error set BLEND=<file.blend>)
endif
	$(PYTHON) tools/blend_to_obj.py $(BLEND) $(if $(OUT),$(OUT),assets/meshes/props) \
	    $(if $(OBJECT),--object $(OBJECT),) $(if $(NAME),--name $(NAME),) \
	    $(if $(SCALE),--scale $(SCALE),) $(if $(LIST),--list,) \
	    $(if $(NO_BAKE),--no-bake-colours,)

# Generate a .map from a BSP seed: make mapgen SEED=7 OUT=out.map
mapgen: build-mapgen
	cd $(BUILD_DIR) && ./mapgen $(if $(SEED),$(SEED),1) $(if $(OUT),$(OUT),out.map)

# Headless playthrough/action simulation (no window). Runs a scripted action
# sequence through the game systems and reports pass/fail.
#   make sim                       run the built-in smoke script
#   make sim SCRIPT=path/to.txt    run a custom action script
sim: build-sim
	cd $(BUILD_DIR) && ./game_sim $(if $(SCRIPT),$(abspath $(SCRIPT)),)

# ---- test / analysis -------------------------------------------------------
# ctest's own "N/M Test #k" lines feed the same progress bar the compiler does,
# so the 150-test suite reports like a build instead of scrolling like one.
# --output-on-failure keeps a failing test's output, which prints in full.
test: build-all
	@cd $(BUILD_DIR) && $(UI) run "running tests" -- \
	    ctest --output-on-failure $(if $(TEST),-R $(TEST),)

# Address/UB/Leak sanitizer build of game + game_sim (test targets aren't
# ASan-hardened). Run e.g. `make asan && make run BUILD_DIR=build-asan`.
#
# Takes a lock on its own tree, like every other build rule here: build-asan is
# a second build directory and gets its own .ninja_deps, which two concurrent
# `make asan` runs would corrupt exactly the way they would corrupt build/'s.
# This was the one build path that skipped it.
asan:
	$(MAKE) configure BUILD_DIR=build-asan BUILD_TYPE=Debug CMAKE_ARGS=-DENABLE_ASAN=ON
	$(if $(FLOCK),$(FLOCK) build-asan/.build.lock,) \
	    cmake --build build-asan --target game game_sim -j$(JOBS)

# Frame-time percentiles over N frames (default 300), vsync off for real cost.
bench: build-game
	cd $(BUILD_DIR) && env $(RUN_ENV) RAVEN_BENCH_FRAMES=$(if $(BENCH),$(BENCH),300) ./game

# Deterministic screenshot capture (fixed timestep). Requires SHOT=<path>.
screenshot: build-game
ifndef SHOT
	$(error set SHOT=<path.png> (optional FRAME=<n>, SEED=<n>, PRESET=<name>))
endif
	cd $(BUILD_DIR) && env $(RUN_ENV) RAVEN_SCREENSHOT=$(SHOT) \
	    RAVEN_SCREENSHOT_FRAME=$(if $(FRAME),$(FRAME),200) ./game $(RUN_ARGS)

# JSON-emitting visual/GPU regression entry points. Artifacts default to
# artifacts/visual and may be redirected with ARTIFACT_DIR=<path>.
VISUAL_COMMON = --build-dir $(BUILD_DIR) \
	--artifact-dir $(if $(ARTIFACT_DIR),$(ARTIFACT_DIR),artifacts/visual)
VISUAL_ARGS = --frame $(if $(FRAME),$(FRAME),90) \
	--fixed-dt $(if $(FIXED_DT),$(FIXED_DT),0.016666667) \
	--seed $(if $(SEED),$(SEED),1) \
	--display-mode $(if $(DISPLAY_MODE),$(DISPLAY_MODE),auto) \
	$(if $(PRESET),--preset $(PRESET),) \
	$(if $(MAP),--map $(MAP),)

# Drives the editor through the edits that only a mouse could reach -- removing
# a component, deleting an entity, unparenting one -- one per frame, with the
# entity selected a frame earlier so the gizmo, inspector and outliner have all
# drawn it. Every one of those was reported as a crash and none of them was
# reproducible from a test until this existed.
editor-selftest: build-editor
	cd $(BUILD_DIR) && env $(RUN_ENV) RAVEN_EDITOR_SELFTEST=1 ./scene_editor

# Connector: the browser window onto the engine's debug channels.
#   make connector              collect directly (no Redis needed)
#   make connector REDIS=1      subscribe to a real Redis instead
# Then run the game with RAVEN_CONNECTOR=1.
connector:
	$(PYTHON) tools/connector/server.py $(if $(REDIS),--redis,) \
	    $(if $(PORT),--port $(PORT),) --open

# The game, already pointed at a running Connector.
run-connected: build-game
	cd $(BUILD_DIR) && env $(RUN_ENV) RAVEN_CONNECTOR=1 ./game

visual-test: build-game
	$(PYTHON) tools/visual_test.py $(VISUAL_COMMON) screenshot $(VISUAL_ARGS)

visual-bench: build-game
	$(PYTHON) tools/visual_test.py $(VISUAL_COMMON) benchmark $(VISUAL_ARGS) \
		--frames $(if $(BENCH),$(BENCH),120)

renderdoc-capture: build-app
	$(PYTHON) tools/visual_test.py $(VISUAL_COMMON) capture $(VISUAL_ARGS) --app $(APP_TARGET)

# ---- GPU + native debugging ------------------------------------------------
# All of these take APP=game|scene_editor|psx_demo, because every one of them is
# an eng::Application driving the same renderer -- a shader bug reproduces in
# whichever is quickest to get to, and that is often the editor.

# Launch under the RenderDoc UI with the app already configured. Capture with
# F12 (or the key RenderDoc shows), then inspect the frame.
#   make renderdoc                     the game
#   make renderdoc APP=scene_editor    the editor's viewport
#   make renderdoc FRAME=200           auto-capture one frame, headless
renderdoc: build-app
	@command -v renderdoccmd >/dev/null 2>&1 || { \
	    $(UI) err "renderdoccmd not found -- install RenderDoc, or use 'make renderdoc-capture'"; \
	    exit 1; }
ifdef FRAME
	cd $(BUILD_DIR) && env $(RUN_ENV) RAVEN_RENDERDOC_FRAME=$(FRAME) \
	    RAVEN_RENDERDOC_CAPTURE=$(abspath $(if $(OUT),$(OUT),capture)) \
	    renderdoccmd capture --wait-for-exit \
	        --capture-file $(abspath $(if $(OUT),$(OUT),capture)) ./$(APP_TARGET) $(RUN_ARGS)
else
	@command -v qrenderdoc >/dev/null 2>&1 \
	    && (cd $(BUILD_DIR) && env $(RUN_ENV) qrenderdoc --launch ./$(APP_TARGET) $(RUN_ARGS)) \
	    || (cd $(BUILD_DIR) && env $(RUN_ENV) renderdoccmd capture ./$(APP_TARGET) $(RUN_ARGS))
endif

# Interactive debugger on a Debug build. Builds into build-debug/ so the
# optimised tree is left alone.
#   make gdb                  the game, break on segfault with a backtrace
#   make gdb APP=scene_editor
#   make gdb BATCH=1          run to completion, print a backtrace, exit
gdb:
	$(MAKE) build-app BUILD_DIR=build-debug BUILD_TYPE=Debug APP=$(APP)
	cd build-debug && env $(RUN_ENV) gdb \
	    $(if $(BATCH),-batch -ex run -ex "bt full" -ex quit,-ex run) \
	    --args ./$(APP_TARGET) $(RUN_ARGS)

# Memory errors that ASan cannot see (uninitialised reads through the driver).
# Slow: expect single-digit FPS, so pair it with FRAME= to exit on its own.
valgrind:
	$(MAKE) build-app BUILD_DIR=build-debug BUILD_TYPE=Debug APP=$(APP)
	cd build-debug && env $(RUN_ENV) $(if $(FRAME),RAVEN_SCREENSHOT_FRAME=$(FRAME) RAVEN_SCREENSHOT=/dev/null,) \
	    valgrind --leak-check=full --track-origins=yes \
	    --suppressions=$(abspath tools/valgrind.supp) \
	    ./$(APP_TARGET) $(RUN_ARGS) 2>&1 | tee valgrind-$(APP_TARGET).log

# CPU profile of a fixed number of frames. Writes perf.data next to the binary
# and prints the hot paths; use `perf report` there for the full tree.
perf: build-app
	cd $(BUILD_DIR) && env $(RUN_ENV) RAVEN_BENCH_FRAMES=$(if $(BENCH),$(BENCH),600) \
	    perf record -g --call-graph dwarf -o perf-$(APP_TARGET).data ./$(APP_TARGET) $(RUN_ARGS)
	cd $(BUILD_DIR) && perf report -i perf-$(APP_TARGET).data --stdio | head -40

# ---- docs / debug / clean --------------------------------------------------
docs: configure
	$(BUILD_LOCK) cmake --build $(BUILD_DIR) --target docs
	@if command -v xdg-open >/dev/null 2>&1; then \
		xdg-open "$(BUILD_DIR)/docs/html/index.html"; \
	elif command -v open >/dev/null 2>&1; then \
		open "$(BUILD_DIR)/docs/html/index.html"; \
	else \
		$(UI) note "docs generated at $(BUILD_DIR)/docs/html/index.html"; \
	fi

# The Vulkan RHI implementation guide. Static HTML checked into the repo, so
# there is nothing to build -- unlike `docs`, which runs Doxygen first.
VULKAN_KIT = $(CURDIR)/docs/vulkan-impl-survival-kit/index.html

vulkan-kit:
	@if command -v xdg-open >/dev/null 2>&1; then \
		xdg-open "$(VULKAN_KIT)"; \
	elif command -v open >/dev/null 2>&1; then \
		open "$(VULKAN_KIT)"; \
	else \
		$(UI) note "open $(VULKAN_KIT) in a browser"; \
	fi

# Unoptimised build with symbols, in its own tree so the Release one survives.
debug:
	$(MAKE) build-app BUILD_DIR=build-debug BUILD_TYPE=Debug APP=$(APP)

debug-run: debug
	cd build-debug && env $(RUN_ENV) ./$(APP_TARGET) $(RUN_ARGS)

clean:
	rm -rf $(BUILD_DIR) build-debug build-asan

# Piped through the formatter, which paints the target names and options and
# swaps the title line for the wordmark. The text itself is unchanged, so
# `make help | grep` and `make help PLAIN=1` behave as they always did.
help:
	@$(MAKE) --no-print-directory help-text | $(UI) helpfmt

help-text:
	@echo "Raven Engine build/run CLI"
	@echo ""
	@echo "Targets:"
	@echo "  connector          browser view of the engine's debug channels"
	@echo "  run-connected      the game, reporting into a running connector"
	@echo "  make [build]        configure + build the game"
	@echo "  make build-all      build every executable and test target"
	@echo "  make doctor         diagnose the tree/toolchain/ccache (FIX=1 repairs)"
	@echo "  make build-reset    clear a truncated ninja deps log (fixes"
	@echo "                      'rebuilds everything with no edits')"
	@echo "  make run            build + run the game (alias: game)"
	@echo "  make demo           the model showroom (MODEL=, FIT=, SPIN=, YAW=, LIST=1, SHOT=)"
	@echo "  make psx-demo       build + run the PSX shader sample"
	@echo "  make editor         build + run the placement editor (SCENE=)"
	@echo "  make material       editor, opened in the material staging scene"
	@echo "  make play PROJECT=  play a project (docs/projects.md)"
	@echo "  make export PROJECT= build a distributable (OUT=)"
	@echo "  make acp            condition all assets (TYPE=, FILTER=, FORCE=1)"
	@echo "  make acp-check      fail if any asset is stale -- what CI runs"
	@echo "  make assetdb        the resource database (STAMP=1, LIST=1, TYPE=)"
	@echo "  make assetformats   which extension is which pipeline row"
	@echo "  make cook SCENE=    cook a .scn to a .map (OUT=, VALIDATE=1)"
	@echo "  make scene SCENE=   cook a .scn and play it immediately"
	@echo "  make prefab-viewer  compact turntable (PREFAB=<kit.id>, SUBJECT_SCALE=, VIEWER_PRESET=)"
	@echo "  make look SCENE=    cook + one screenshot (FRAME=, SHOT=)"
	@echo "  make clip SCENE=    cook + record a GIF (SECONDS=, FPS=, WIDTH=, OUT=, MP4=1)"
	@echo "  make new-clip NAME= start a new shot from the example scene"
	@echo "  make asset BLEND=   .blend -> engine .obj (LIST=1, OBJECT=, NAME=, SCALE=)"
	@echo "  make mapgen         generate a .map (SEED=, OUT=)"
	@echo "  make sim            headless action-simulation harness (SCRIPT=)"
	@echo "  make test           build + run the ctest suite"
	@echo "  make editor-selftest  drive the editor through the edits a mouse makes"
	@echo "  make asan           ASan+UBSan+Leak build of game + game_sim"
	@echo "  make bench          frame-time percentiles (BENCH=<frames>)"
	@echo "  make screenshot     deterministic capture (SHOT=<path> FRAME=)"
	@echo "  make visual-test    screenshot + JSON artifact validation"
	@echo "  make visual-bench   frame metrics + JSON (BENCH=<frames>)"
	@echo "  make renderdoc-capture  deterministic single-frame .rdc (APP=)"
	@echo "  make renderdoc      launch under RenderDoc (APP=, FRAME= to auto-capture)"
	@echo "  make gdb            debug build under gdb (APP=, BATCH=1)"
	@echo "  make valgrind       memcheck run (APP=, FRAME=)"
	@echo "  make perf           CPU profile + hot paths (APP=, BENCH=)"
	@echo "  make debug-run      run the Debug build (APP=)"
	@echo "  make deps           install build dependencies (any distro)"
	@echo "  make docs           generate + open API docs"
	@echo "  make vulkan-kit     open the Vulkan RHI implementation guide"
	@echo "  make debug          Debug build in build-debug/"
	@echo "  make ui-deps        install the rich build UI into .cache/py"
	@echo "  make clean          remove build directories"
	@echo ""
	@echo "Run options (make run/demo/prefab-viewer/screenshot/bench):"
	@echo "  SEED=<n>            world seed            (RAVEN_GEN_SEED)"
	@echo "  PRESET=<name>       render preset: ps1 ps2 gamecube n64 pixel-3d"
	@echo "                      modern-ps1 dungeon psx-horror fire-dimension"
	@echo "                      poison-swamp          (RAVEN_RENDER_PRESET)"
	@echo "  VIEWER_PRESET=<name> prefab-viewer default when PRESET is omitted"
	@echo "  PREFAB=<kit.id>      subject prefab from assets/config/kit.toml"
	@echo "  SUBJECT_SCALE=<n>    uniform scale; SUBJECT_YAW=<degrees>; SUBJECT_Y=<offset>"
	@echo "  GROUND_CLEARANCE=<m> gap above floor after auto-grounding"
	@echo "  SUBJECT_MATERIAL=<id> material override; SUBJECT_NAME=<label>"
	@echo "  VIEWER_FLAGS=<args>  prefab-viewer game flags; --render-preset overrides vars"
	@echo "  MAP=<file.map>      play an authored map (positional arg)"
	@echo "  SHOWROOM=<file>     override the editable depth-zero showroom TOML"
	@echo "  COLLIDERS=1         collider wireframe    (RAVEN_SHOW_COLLIDERS)"
	@echo "  WIREFRAME=1         mesh wireframe        (RAVEN_WIREFRAME)"
	@echo "  PROFILE=1           log per-phase timings (RAVEN_PROFILE)"
	@echo "  PORTAL=1            portal showcase pose  (RAVEN_SHOWCASE_PORTAL)"
	@echo "  SHOT=<path> FRAME=<n>   screenshot        (RAVEN_SCREENSHOT*)"
	@echo "  FIXED_DT=<s>        deterministic timestep(RAVEN_FIXED_DT)"
	@echo ""
	@echo "  MATERIAL=1          editor opens in material staging (RAVEN_EDITOR_MATERIAL)"
	@echo "  SCENE=<file.scn>    scene for editor/cook targets"
	@echo ""
	@echo "Debug options (make renderdoc/gdb/valgrind/perf/debug-run):"
	@echo "  APP=game|scene_editor|psx_demo   which executable to drive"
	@echo "  FRAME=<n>           auto-capture frame / exit frame"
	@echo "  BATCH=1             gdb: run to completion, print backtrace, exit"
	@echo "  OUT=<path>          renderdoc: capture file"
	@echo ""
	@echo "Output options (every target):"
	@echo "  PLAIN=1             no colour, no progress bar (also NO_COLOR=1)"
	@echo "  JOBS=<n>            parallel compile jobs; defaults from free memory"
	@echo "  NO_DEPS_CHECK=1     skip the ninja deps-log integrity check"
	@echo ""
	@echo "Build config: BUILD_DIR=$(BUILD_DIR) BUILD_TYPE=$(BUILD_TYPE) GENERATOR=$(GENERATOR) JOBS=$(JOBS) APP=$(APP)"
