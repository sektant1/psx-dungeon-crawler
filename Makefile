# Convenience wrapper around the CMake build + run/test CLI.
#
# Quick start:
#   make            configure (if needed) + build the game
#   make run        build + run the game
#   make help       full target + option reference
#
# Run options are plain make variables mapped to the game's PSX_* env vars, e.g.
#   make run SEED=42 PRESET=ps1            # seed + render preset
#   make run MAP=level.map                 # play an authored .map
#   make run SHOWROOM=game/assets/showroom.toml
#   make run COLLIDERS=1 WIREFRAME=1       # debug overlays
#   make screenshot SHOT=/tmp/x.png FRAME=200
#   make sim SCRIPT=game/sim/scripts/smoke.txt
#
# Every app target (game, editor, demo) shares the same run/debug options, so
# `make gdb APP=scene_editor` and `make renderdoc APP=scene_editor` work exactly
# like they do for the game.

# ---- configuration ---------------------------------------------------------
BUILD_DIR   ?= build
BUILD_TYPE  ?= Release
JOBS        ?= $(shell nproc)
# Force X11 on Wayland (XWayland): the GL3Plus path is unreliable on native
# Wayland. Override with SDL_VIDEODRIVER=... on the command line if needed.
SDL_VIDEODRIVER ?= x11

# ---- run-option -> PSX_* env mapping ---------------------------------------
# Each variable is only exported when the user sets it, so unset options keep
# the game's own defaults. Add a mapping here to expose a new env var.
RUN_ENV := SDL_VIDEODRIVER=$(SDL_VIDEODRIVER)
ifdef SEED
RUN_ENV += PSX_GEN_SEED=$(SEED)
endif
ifdef PRESET
RUN_ENV += PSX_RENDER_PRESET=$(PRESET)
endif
ifdef SHOT
RUN_ENV += PSX_SCREENSHOT=$(SHOT)
endif
ifdef FRAME
RUN_ENV += PSX_SCREENSHOT_FRAME=$(FRAME)
endif
ifdef FIXED_DT
RUN_ENV += PSX_FIXED_DT=$(FIXED_DT)
endif
ifdef BENCH
RUN_ENV += PSX_BENCH_FRAMES=$(BENCH)
endif
ifdef PROFILE
RUN_ENV += PSX_PROFILE=$(PROFILE)
endif
ifdef COLLIDERS
RUN_ENV += PSX_SHOW_COLLIDERS=$(COLLIDERS)
endif
ifdef WIREFRAME
RUN_ENV += PSX_WIREFRAME=$(WIREFRAME)
endif
ifdef PORTAL
RUN_ENV += PSX_SHOWCASE_PORTAL=$(PORTAL)
endif
ifdef SHOWROOM
RUN_ENV += PSX_SHOWROOM_MAP=$(abspath $(SHOWROOM))
endif
ifdef MATERIAL
RUN_ENV += PSX_EDITOR_MATERIAL=$(MATERIAL)
endif
ifdef RENDERDOC_FRAME
RUN_ENV += PSX_RENDERDOC_FRAME=$(RENDERDOC_FRAME)
endif
ifdef RENDERDOC_OUT
RUN_ENV += PSX_RENDERDOC_CAPTURE=$(abspath $(RENDERDOC_OUT))
endif
# Positional game argument (e.g. a .map file to play).
RUN_ARGS := $(MAP)

# Which executable the generic app targets drive. Every target below that takes
# APP= works for any of these, because they are all eng::Application consumers.
APP        ?= game
APP_TARGET := $(if $(filter scene_editor,$(APP)),scene_editor,\
              $(if $(filter psx_demo,$(APP)),psx_demo,game))

.PHONY: all configure build build-all build-app build-game build-demo build-mapgen build-sim \
        build-editor build-cook editor cook scene material \
        run game demo mapgen sim test asan bench screenshot visual-test \
        visual-bench renderdoc-capture renderdoc gdb valgrind perf deps docs \
        debug debug-run clean help

all: build

# ---- build -----------------------------------------------------------------
# Avoid re-running CMake on every invocation. CMake's generated build system
# still performs its own dependency check, so edits to CMake inputs regenerate
# normally when cmake --build runs.
configure:
	@if [ ! -f "$(BUILD_DIR)/CMakeCache.txt" ] || \
	    ! grep -Fqx "CMAKE_BUILD_TYPE:STRING=$(BUILD_TYPE)" "$(BUILD_DIR)/CMakeCache.txt"; then \
		cmake -B "$(BUILD_DIR)" -DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" \
		      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON; \
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

# The generic app build, for the APP=-driven targets below.
build-app: configure
	cmake --build $(BUILD_DIR) --target $(APP_TARGET) -j$(JOBS)

# Detects pacman/apt/dnf/zypper/apk/brew; installs toolchain + SDL2 + glm,
# then OGRE >= 14 (distro package where available, source build otherwise).
deps:
	./scripts/install-deps.sh

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
#   make editor SCENE=game/assets/scenes/ritual_boss_showroom.scn
#   make material                  open straight into the material staging scene
editor: build-editor
	cd $(BUILD_DIR) && env $(RUN_ENV) ./scene_editor $(if $(SCENE),$(abspath $(SCENE)),)

material: build-editor
	cd $(BUILD_DIR) && env $(RUN_ENV) PSX_EDITOR_MATERIAL=1 \
	    ./scene_editor $(if $(SCENE),$(abspath $(SCENE)),)

# Cook an authored .scn into a runtime .map -- the same cooker the editor calls
# in-process, which is what makes the two produce identical bytes.
#   make cook SCENE=game/assets/scenes/ritual_boss_showroom.scn
#   make cook SCENE=... OUT=/tmp/level.map
#   make cook SCENE=... VALIDATE=1        report issues, write nothing
cook: build-cook
ifndef SCENE
	$(error set SCENE=<file.scn> (optional OUT=<file.map>, VALIDATE=1))
endif
	./$(BUILD_DIR)/scene_cook $(abspath $(SCENE)) \
	    --kit $(abspath game/assets/kit.toml) \
	    $(if $(VALIDATE),--validate-only,--out $(if $(OUT),$(abspath $(OUT)),$(abspath $(basename $(SCENE)).map)))

# Cook a scene and immediately play it: the editor's F5, from the shell.
scene: build-cook build-game
ifndef SCENE
	$(error set SCENE=<file.scn>)
endif
	$(MAKE) cook SCENE=$(SCENE) OUT=$(BUILD_DIR)/scene.map
	cd $(BUILD_DIR) && env $(RUN_ENV) ./game scene.map

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
	cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
	cmake --build build-asan --target game game_sim -j$(JOBS)

# Frame-time percentiles over N frames (default 300), vsync off for real cost.
bench: build-game
	cd $(BUILD_DIR) && env $(RUN_ENV) PSX_BENCH_FRAMES=$(if $(BENCH),$(BENCH),300) ./game

# Deterministic screenshot capture (fixed timestep). Requires SHOT=<path>.
screenshot: build-game
ifndef SHOT
	$(error set SHOT=<path.png> (optional FRAME=<n>, SEED=<n>, PRESET=<name>))
endif
	cd $(BUILD_DIR) && env $(RUN_ENV) PSX_SCREENSHOT=$(SHOT) \
	    PSX_SCREENSHOT_FRAME=$(if $(FRAME),$(FRAME),200) ./game $(RUN_ARGS)

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

visual-test: build-game
	python tools/visual_test.py $(VISUAL_COMMON) screenshot $(VISUAL_ARGS)

visual-bench: build-game
	python tools/visual_test.py $(VISUAL_COMMON) benchmark $(VISUAL_ARGS) \
		--frames $(if $(BENCH),$(BENCH),120)

renderdoc-capture: build-app
	python tools/visual_test.py $(VISUAL_COMMON) capture $(VISUAL_ARGS) --app $(APP_TARGET)

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
	cd $(BUILD_DIR) && env $(RUN_ENV) PSX_RENDERDOC_FRAME=$(FRAME) \
	    PSX_RENDERDOC_CAPTURE=$(abspath $(if $(OUT),$(OUT),capture)) \
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
	cd build-debug && env $(RUN_ENV) $(if $(FRAME),PSX_SCREENSHOT_FRAME=$(FRAME) PSX_SCREENSHOT=/dev/null,) \
	    valgrind --leak-check=full --track-origins=yes \
	    --suppressions=$(abspath tools/valgrind.supp) \
	    ./$(APP_TARGET) $(RUN_ARGS) 2>&1 | tee valgrind-$(APP_TARGET).log

# CPU profile of a fixed number of frames. Writes perf.data next to the binary
# and prints the hot paths; use `perf report` there for the full tree.
perf: build-app
	cd $(BUILD_DIR) && env $(RUN_ENV) PSX_BENCH_FRAMES=$(if $(BENCH),$(BENCH),600) \
	    perf record -g --call-graph dwarf -o perf-$(APP_TARGET).data ./$(APP_TARGET) $(RUN_ARGS)
	cd $(BUILD_DIR) && perf report -i perf-$(APP_TARGET).data --stdio | head -40

# ---- docs / debug / clean --------------------------------------------------
docs:
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	cmake --build $(BUILD_DIR) --target docs
	@if command -v xdg-open >/dev/null 2>&1; then \
		xdg-open "$(BUILD_DIR)/docs/html/index.html"; \
	elif command -v open >/dev/null 2>&1; then \
		open "$(BUILD_DIR)/docs/html/index.html"; \
	else \
		echo "Docs generated at $(BUILD_DIR)/docs/html/index.html"; \
	fi

# Unoptimised build with symbols, in its own tree so the Release one survives.
debug:
	$(MAKE) build-app BUILD_DIR=build-debug BUILD_TYPE=Debug APP=$(APP)

debug-run: debug
	cd build-debug && env $(RUN_ENV) ./$(APP_TARGET) $(RUN_ARGS)

clean:
	rm -rf $(BUILD_DIR) build-debug build-asan

help:
	@echo "psx-dungeon-crawler build/run CLI"
	@echo ""
	@echo "Targets:"
	@echo "  make [build]        configure + build the game"
	@echo "  make build-all      build every executable and test target"
	@echo "  make run            build + run the game (alias: game)"
	@echo "  make demo           build + run the PSX shader sample"
	@echo "  make editor         build + run the placement editor (SCENE=)"
	@echo "  make material       editor, opened in the material staging scene"
	@echo "  make cook SCENE=    cook a .scn to a .map (OUT=, VALIDATE=1)"
	@echo "  make scene SCENE=   cook a .scn and play it immediately"
	@echo "  make mapgen         generate a .map (SEED=, OUT=)"
	@echo "  make sim            headless action-simulation harness (SCRIPT=)"
	@echo "  make test           build + run the ctest suite"
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
	@echo "  make debug          Debug build in build-debug/"
	@echo "  make clean          remove build directories"
	@echo ""
	@echo "Run options (make run/demo/screenshot/bench):"
	@echo "  SEED=<n>            world seed            (PSX_GEN_SEED)"
	@echo "  PRESET=<name>       render preset: ps1 ps2 gamecube n64"
	@echo "                      pixel-3d modern-ps1  (PSX_RENDER_PRESET)"
	@echo "  MAP=<file.map>      play an authored map (positional arg)"
	@echo "  SHOWROOM=<file>     override the editable depth-zero showroom TOML"
	@echo "  COLLIDERS=1         collider wireframe    (PSX_SHOW_COLLIDERS)"
	@echo "  WIREFRAME=1         mesh wireframe        (PSX_WIREFRAME)"
	@echo "  PROFILE=1           log per-phase timings (PSX_PROFILE)"
	@echo "  PORTAL=1            portal showcase pose  (PSX_SHOWCASE_PORTAL)"
	@echo "  SHOT=<path> FRAME=<n>   screenshot        (PSX_SCREENSHOT*)"
	@echo "  FIXED_DT=<s>        deterministic timestep(PSX_FIXED_DT)"
	@echo ""
	@echo "  MATERIAL=1          editor opens in material staging (PSX_EDITOR_MATERIAL)"
	@echo "  SCENE=<file.scn>    scene for editor/cook targets"
	@echo ""
	@echo "Debug options (make renderdoc/gdb/valgrind/perf/debug-run):"
	@echo "  APP=game|scene_editor|psx_demo   which executable to drive"
	@echo "  FRAME=<n>           auto-capture frame / exit frame"
	@echo "  BATCH=1             gdb: run to completion, print backtrace, exit"
	@echo "  OUT=<path>          renderdoc: capture file"
	@echo ""
	@echo "Build config: BUILD_DIR=$(BUILD_DIR) BUILD_TYPE=$(BUILD_TYPE) JOBS=$(JOBS) APP=$(APP)"
