#!/bin/sh
# Installs the system toolchain and platform headers needed to configure the
# project. CMake/CPM fetches pinned source dependencies, including the temporary
# Vulkan RHI renderer and Jolt physics. No renderer package is installed: the
# engine talks to Vulkan directly and compiles its own SPIR-V.
#
# Usage: make deps   (or ./tools/install-deps.sh)
set -eu

if [ "$(id -u)" = 0 ]; then
    SUDO=""
elif command -v sudo >/dev/null 2>&1; then
    SUDO="sudo"
else
    echo "error: not root and sudo not found" >&2
    exit 1
fi

# ---------------------------------------------------------------- detection
if   command -v pacman  >/dev/null 2>&1; then PM=pacman
elif command -v apt-get >/dev/null 2>&1; then PM=apt
elif command -v dnf     >/dev/null 2>&1; then PM=dnf
elif command -v zypper  >/dev/null 2>&1; then PM=zypper
elif command -v apk     >/dev/null 2>&1; then PM=apk
elif command -v brew    >/dev/null 2>&1; then PM=brew
else
    echo "error: no supported package manager found (pacman/apt/dnf/zypper/apk/brew)" >&2
    exit 1
fi
echo "==> package manager: $PM"

# ------------------------------------------------- base deps (per distro)
# Includes the X11 headers SDL needs for windowing. The renderer itself needs
# renderer replaces it. ninja/ccache/mold are build-speed
# tools the CMake configure step picks up automatically when present: ninja for
# scheduling, ccache for repeat compiles, mold for link time.
case "$PM" in
pacman)
    $SUDO pacman -S --needed --noconfirm \
        gcc make cmake ninja ccache mold git pkgconf sdl2 glm mesa freetype2 \
        libx11 libxrandr zlib
    ;;
apt)
    $SUDO apt-get update
    $SUDO apt-get install -y \
        build-essential cmake ninja-build ccache mold git pkg-config \
        libsdl2-dev libglm-dev \
        libgl1-mesa-dev libfreetype-dev libx11-dev libxrandr-dev \
        zlib1g-dev libpugixml-dev
    ;;
dnf)
    $SUDO dnf install -y \
        gcc-c++ make cmake ninja-build ccache mold git pkgconf-pkg-config \
        SDL2-devel glm-devel \
        mesa-libGL-devel freetype-devel libX11-devel libXrandr-devel \
        zlib-devel pugixml-devel
    ;;
zypper)
    $SUDO zypper --non-interactive install \
        gcc-c++ make cmake ninja ccache mold git pkg-config SDL2-devel glm-devel \
        Mesa-libGL-devel freetype2-devel libX11-devel libXrandr-devel \
        zlib-devel pugixml-devel
    ;;
apk)
    $SUDO apk add --no-cache \
        build-base cmake samurai ccache mold git pkgconf sdl2-dev glm-dev mesa-dev \
        freetype-dev libx11-dev libxrandr-dev zlib-dev pugixml-dev
    ;;
brew)
    brew install cmake ninja ccache git pkg-config sdl2 glm freetype
    ;;
esac

echo "==> system prerequisites installed; CMake will fetch pinned dependencies"
