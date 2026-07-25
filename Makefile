# Convenience wrapper around the CMake build + run/test CLI.
#
# Quick start:
#   make            configure (if needed) + build everything
#   make run        build + run the game
#   make help       full target + option reference
#
# Run options are plain make variables mapped to the game's PSX_* env vars, e.g.
#   make run SEED=42 PRESET=ps1            # seed + render preset
#   make run MAP=level.map                 # play an authored .map
#   make run COLLIDERS=1 WIREFRAME=1       # debug overlays
#   make screenshot SHOT=/tmp/x.png FRAME=200
#   make sim SCRIPT=game/sim/scripts/smoke.txt

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
# Positional game argument (e.g. a .map file to play).
RUN_ARGS := $(MAP)

.PHONY: all build run game demo mapgen sim test asan bench screenshot \
        deps docs debug clean help

all: build

# ---- build -----------------------------------------------------------------
build:
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
	      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	cmake --build $(BUILD_DIR) -j$(JOBS)

# Detects pacman/apt/dnf/zypper/apk/brew; installs toolchain + SDL2 + glm,
# then OGRE >= 14 (distro package where available, source build otherwise).
deps:
	./scripts/install-deps.sh

# ---- run -------------------------------------------------------------------
# `run` is the primary entry point; `game` is a back-compat alias.
run game: build
	cd $(BUILD_DIR) && env $(RUN_ENV) ./game $(RUN_ARGS)

demo: build
	cd $(BUILD_DIR) && env $(RUN_ENV) ./psx_demo

# Generate a .map from a BSP seed: make mapgen SEED=7 OUT=out.map
mapgen: build
	cd $(BUILD_DIR) && ./mapgen $(if $(SEED),$(SEED),1) $(if $(OUT),$(OUT),out.map)

# Headless playthrough/action simulation (no window). Runs a scripted action
# sequence through the game systems and reports pass/fail.
#   make sim                       run the built-in smoke script
#   make sim SCRIPT=path/to.txt    run a custom action script
sim: build
	cd $(BUILD_DIR) && ./game_sim $(if $(SCRIPT),$(abspath $(SCRIPT)),)

# ---- test / analysis -------------------------------------------------------
test: build
	cd $(BUILD_DIR) && ctest --output-on-failure

# Address/UB/Leak sanitizer build of game + game_sim (test targets aren't
# ASan-hardened). Run e.g. `make asan && make run BUILD_DIR=build-asan`.
asan:
	cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
	cmake --build build-asan --target game game_sim -j$(JOBS)

# Frame-time percentiles over N frames (default 300), vsync off for real cost.
bench: build
	cd $(BUILD_DIR) && env $(RUN_ENV) PSX_BENCH_FRAMES=$(if $(BENCH),$(BENCH),300) ./game

# Deterministic screenshot capture (fixed timestep). Requires SHOT=<path>.
screenshot: build
ifndef SHOT
	$(error set SHOT=<path.png> (optional FRAME=<n>, SEED=<n>, PRESET=<name>))
endif
	cd $(BUILD_DIR) && env $(RUN_ENV) PSX_SCREENSHOT=$(SHOT) \
	    PSX_SCREENSHOT_FRAME=$(if $(FRAME),$(FRAME),200) ./game $(RUN_ARGS)

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

debug:
	$(MAKE) build BUILD_DIR=build-debug BUILD_TYPE=Debug

clean:
	rm -rf $(BUILD_DIR) build-debug build-asan

help:
	@echo "psx-dungeon-crawler build/run CLI"
	@echo ""
	@echo "Targets:"
	@echo "  make [build]        configure + build all targets"
	@echo "  make run            build + run the game (alias: game)"
	@echo "  make demo           build + run the PSX shader sample"
	@echo "  make mapgen         generate a .map (SEED=, OUT=)"
	@echo "  make sim            headless action-simulation harness (SCRIPT=)"
	@echo "  make test           build + run the ctest suite"
	@echo "  make asan           ASan+UBSan+Leak build of game + game_sim"
	@echo "  make bench          frame-time percentiles (BENCH=<frames>)"
	@echo "  make screenshot     deterministic capture (SHOT=<path> FRAME=)"
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
	@echo "  COLLIDERS=1         collider wireframe    (PSX_SHOW_COLLIDERS)"
	@echo "  WIREFRAME=1         mesh wireframe        (PSX_WIREFRAME)"
	@echo "  PROFILE=1           log per-phase timings (PSX_PROFILE)"
	@echo "  PORTAL=1            portal showcase pose  (PSX_SHOWCASE_PORTAL)"
	@echo "  SHOT=<path> FRAME=<n>   screenshot        (PSX_SCREENSHOT*)"
	@echo "  FIXED_DT=<s>        deterministic timestep(PSX_FIXED_DT)"
	@echo ""
	@echo "Build config: BUILD_DIR=$(BUILD_DIR) BUILD_TYPE=$(BUILD_TYPE) JOBS=$(JOBS)"
