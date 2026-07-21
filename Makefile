# Convenience wrapper around the CMake build.
#   make            - configure (if needed) + build all targets
#   make deps       - install all build dependencies, any distro (incl. OGRE)
#   make demo       - build then run the PSX sample (forces X11 under Wayland)
#   make game       - build then run the game
#   make debug      - Debug-type build in build-debug/
#   make clean      - remove build directories

BUILD_DIR   ?= build
BUILD_TYPE  ?= Release
JOBS        ?= $(shell nproc)

.PHONY: all build deps demo game debug clean

all: build

# Detects pacman/apt/dnf/zypper/apk/brew, installs toolchain + SDL2 + glm,
# then OGRE >= 14 (distro package where available, source build otherwise).
deps:
	./scripts/install-deps.sh

build:
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	cmake --build $(BUILD_DIR) -j$(JOBS)

demo: build
	cd $(BUILD_DIR) && SDL_VIDEODRIVER=x11 ./psx_demo

game: build
	cd $(BUILD_DIR) && SDL_VIDEODRIVER=x11 ./game

debug:
	$(MAKE) build BUILD_DIR=build-debug BUILD_TYPE=Debug

clean:
	rm -rf $(BUILD_DIR) build-debug
