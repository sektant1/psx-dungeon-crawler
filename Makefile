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

# ---- configuration ---------------------------------------------------------
BUILD_DIR   ?= build
BUILD_TYPE  ?= Release
JOBS        ?= $(shell nproc)
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
        build-editor build-cook build-acp acp acp-check acp-clean assetdb \
        editor cook scene material prefab-viewer \
        run game demo mapgen sim test asan bench screenshot visual-test \
        editor-selftest clip clip-mp4 look new-clip assetformats \
        visual-bench renderdoc-capture renderdoc gdb valgrind perf deps docs \
        vulkan-kit asset debug debug-run clean help

all: build

# ---- build -----------------------------------------------------------------
# Avoid re-running CMake on every invocation. CMake's generated build system
# still performs its own dependency check, so edits to CMake inputs regenerate
# normally when cmake --build runs.
configure:
	@if [ ! -f "$(BUILD_DIR)/CMakeCache.txt" ] && [ "$(GENERATOR)" = "Ninja" ] && ! command -v ninja >/dev/null 2>&1; then \
		echo "ninja not found -- run 'make deps' or install Ninja"; \
		exit 2; \
	fi
	@if [ -f "$(BUILD_DIR)/CMakeCache.txt" ]; then \
		actual=$$(grep '^CMAKE_GENERATOR:INTERNAL=' "$(BUILD_DIR)/CMakeCache.txt" | cut -d= -f2-); \
		if [ -n "$$actual" ] && [ "$$actual" != "$(GENERATOR)" ] && [ "$(GENERATOR_ORIGIN)" != "file" ]; then \
			echo "$(BUILD_DIR) uses '$$actual', requested '$(GENERATOR)'"; \
			echo "choose another BUILD_DIR or remove that build tree before switching generators"; \
			exit 2; \
		fi; \
	fi
	@if [ ! -f "$(BUILD_DIR)/CMakeCache.txt" ]; then \
		cmake -B "$(BUILD_DIR)" -G "$(GENERATOR)" \
		      -DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" \
		      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON $(CMAKE_ARGS); \
	elif [ -n "$(strip $(CMAKE_ARGS))" ] || ! grep -Fqx "CMAKE_BUILD_TYPE:STRING=$(BUILD_TYPE)" "$(BUILD_DIR)/CMakeCache.txt"; then \
		cmake -B "$(BUILD_DIR)" -DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" \
		      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON $(CMAKE_ARGS); \
	fi

build-all: configure
	cmake --build $(BUILD_DIR) -j$(JOBS)

# Run-oriented commands build only their required target. In particular this
# keeps the demo, map generator, simulation harness, and test executables out
# of the edit/build/run loop for the game.
build-game: configure
	cmake --build $(BUILD_DIR) --target game -j$(JOBS)

build: build-game

build-demo: configure
	cmake --build $(BUILD_DIR) --target psx_demo -j$(JOBS)

build-mapgen: configure
	cmake --build $(BUILD_DIR) --target mapgen -j$(JOBS)

build-sim: configure
	cmake --build $(BUILD_DIR) --target game_sim -j$(JOBS)

build-editor: configure
	cmake --build $(BUILD_DIR) --target scene_editor -j$(JOBS)

build-cook: configure
	cmake --build $(BUILD_DIR) --target scene_cook -j$(JOBS)

build-acp: configure
	cmake --build $(BUILD_DIR) --target raven_acp -j$(JOBS)

# The generic app build, for the APP=-driven targets below.
build-app: configure
	cmake --build $(BUILD_DIR) --target $(APP_TARGET) -j$(JOBS)

# Detects pacman/apt/dnf/zypper/apk/brew; installs toolchain + SDL2 + glm,
# plus the Vulkan loader and the SPIR-V toolchain the RHI compiles through.
deps:
	./tools/install-deps.sh

# ---- run -------------------------------------------------------------------
# `run` is the primary entry point; `game` is a back-compat alias.
run game: build-game
	cd $(BUILD_DIR) && env $(RUN_ENV) ./game $(RUN_ARGS)

demo: build-demo
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
	@echo "wrote $(CLIP_OUT).gif ($(CLIP_SECONDS)s at $(CLIP_FPS) fps)"

# Re-encodes the frames the last `make clip` kept, so a second format costs no
# second run of the game. Nearest-neighbour scaling: bilinear turns a
# low-resolution retro image into mush.
clip-mp4:
	ffmpeg -y -loglevel error -framerate $(CLIP_FPS) \
	    -i $(CLIP_DIR)/frame_%05d.png -c:v libx264 -pix_fmt yuv420p -crf 20 \
	    -vf "scale=720:-2:flags=neighbor" $(CLIP_OUT).mp4
	@echo "wrote $(CLIP_OUT).mp4"

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
	@echo "wrote $(if $(SHOT),$(SHOT),$(BUILD_DIR)/look.png)"

# Start a new shot from the one that works: copies the example scene under a new
# name and opens it. Beats an empty document, because a shot is mostly lighting
# and framing and those are the parts nobody wants to re-derive.
#   make new-clip NAME=my_teaser
new-clip: build-editor
ifndef NAME
	$(error set NAME=<scene-name>)
endif
	@test ! -f assets/scenes/$(NAME).scn || \
	    (echo "assets/scenes/$(NAME).scn already exists" && false)
	@$(PYTHON) -c "import json,sys; \
d=json.load(open('assets/scenes/spin_portal.scn')); \
d['id']='scene.$(NAME)'; \
json.dump(d, open('assets/scenes/$(NAME).scn','w'), indent=2)"
	@echo "created assets/scenes/$(NAME).scn from spin_portal"
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
test: build-all
	cd $(BUILD_DIR) && ctest --output-on-failure

# Address/UB/Leak sanitizer build of game + game_sim (test targets aren't
# ASan-hardened). Run e.g. `make asan && make run BUILD_DIR=build-asan`.
asan:
	$(MAKE) configure BUILD_DIR=build-asan BUILD_TYPE=Debug CMAKE_ARGS=-DENABLE_ASAN=ON
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
	    echo "renderdoccmd not found -- install RenderDoc, or use 'make renderdoc-capture'"; \
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
	cmake --build $(BUILD_DIR) --target docs
	@if command -v xdg-open >/dev/null 2>&1; then \
		xdg-open "$(BUILD_DIR)/docs/html/index.html"; \
	elif command -v open >/dev/null 2>&1; then \
		open "$(BUILD_DIR)/docs/html/index.html"; \
	else \
		echo "Docs generated at $(BUILD_DIR)/docs/html/index.html"; \
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
		echo "Open $(VULKAN_KIT) in a browser"; \
	fi

# Unoptimised build with symbols, in its own tree so the Release one survives.
debug:
	$(MAKE) build-app BUILD_DIR=build-debug BUILD_TYPE=Debug APP=$(APP)

debug-run: debug
	cd build-debug && env $(RUN_ENV) ./$(APP_TARGET) $(RUN_ARGS)

clean:
	rm -rf $(BUILD_DIR) build-debug build-asan

help:
	@echo "Raven Engine build/run CLI"
	@echo ""
	@echo "Targets:"
	@echo "  make [build]        configure + build the game"
	@echo "  make build-all      build every executable and test target"
	@echo "  make run            build + run the game (alias: game)"
	@echo "  make demo           build + run the PSX shader sample"
	@echo "  make editor         build + run the placement editor (SCENE=)"
	@echo "  make material       editor, opened in the material staging scene"
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
	@echo "Build config: BUILD_DIR=$(BUILD_DIR) BUILD_TYPE=$(BUILD_TYPE) GENERATOR=$(GENERATOR) JOBS=$(JOBS) APP=$(APP)"
