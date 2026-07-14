# Convenience wrapper around the CMake build.
#   make            - configure (if needed) + build all targets
#   make run-demo   - build then run the PSX sample (forces X11 under Wayland)
#   make run-game   - build then run the game
#   make debug      - Debug-type build in build-debug/
#   make clean      - remove build directories

BUILD_DIR   ?= build
BUILD_TYPE  ?= Release
JOBS        ?= $(shell nproc)

.PHONY: all build run-demo run-game debug clean

all: build

build:
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	cmake --build $(BUILD_DIR) -j$(JOBS)

run-demo: build
	cd $(BUILD_DIR) && SDL_VIDEODRIVER=x11 ./psx_demo

run-game: build
	cd $(BUILD_DIR) && SDL_VIDEODRIVER=x11 ./game

debug:
	$(MAKE) build BUILD_DIR=build-debug BUILD_TYPE=Debug

clean:
	rm -rf $(BUILD_DIR) build-debug
