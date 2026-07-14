#!/bin/sh
# Installs everything needed to build the project, on any major distro:
#   toolchain (gcc/cmake/make/git/pkg-config), SDL2, glm, and OGRE >= 14
#   (with the Overlay component the debug UI needs).
#
# OGRE: the distro package is used when it exists at version >= 14
# (Arch, recent Fedora); otherwise OGRE is built from source and installed
# to /usr/local. Override the tag with OGRE_VERSION=vX.Y.Z.
#
# Usage: make deps   (or ./scripts/install-deps.sh)
set -eu

OGRE_VERSION="${OGRE_VERSION:-v14.5.2}"
OGRE_BUILD_DIR="${OGRE_BUILD_DIR:-/tmp/ogre-src-build}"
JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

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
# Includes the X11/GL/freetype dev headers an OGRE source build needs, so
# the fallback path below works everywhere.
case "$PM" in
pacman)
    $SUDO pacman -S --needed --noconfirm \
        gcc make cmake git pkgconf sdl2 glm mesa freetype2 \
        libx11 libxrandr zlib
    ;;
apt)
    $SUDO apt-get update
    $SUDO apt-get install -y \
        build-essential cmake git pkg-config libsdl2-dev libglm-dev \
        libgl1-mesa-dev libfreetype-dev libx11-dev libxrandr-dev \
        zlib1g-dev libpugixml-dev
    ;;
dnf)
    $SUDO dnf install -y \
        gcc-c++ make cmake git pkgconf-pkg-config SDL2-devel glm-devel \
        mesa-libGL-devel freetype-devel libX11-devel libXrandr-devel \
        zlib-devel pugixml-devel
    ;;
zypper)
    $SUDO zypper --non-interactive install \
        gcc-c++ make cmake git pkg-config SDL2-devel glm-devel \
        Mesa-libGL-devel freetype2-devel libX11-devel libXrandr-devel \
        zlib-devel pugixml-devel
    ;;
apk)
    $SUDO apk add --no-cache \
        build-base cmake git pkgconf sdl2-dev glm-dev mesa-dev \
        freetype-dev libx11-dev libxrandr-dev zlib-dev pugixml-dev
    ;;
brew)
    brew install cmake git pkg-config sdl2 glm freetype
    ;;
esac

# --------------------------------------------------------- OGRE >= 14 check
have_ogre() {
    pkg-config --atleast-version=14 OGRE 2>/dev/null && return 0
    # Some installs ship CMake config but no .pc file; probe with CMake.
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    cat > "$tmp/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.16)
project(probe NONE)
find_package(OGRE 14 REQUIRED)
EOF
    cmake -S "$tmp" -B "$tmp/b" >/dev/null 2>&1
}

if have_ogre; then
    echo "==> OGRE >= 14 already installed"
    exit 0
fi

# Try the distro package first where a >= 14 build is known to exist.
case "$PM" in
pacman) $SUDO pacman -S --needed --noconfirm ogre || true ;;
dnf)    $SUDO dnf install -y ogre-devel || true ;;
esac

if have_ogre; then
    echo "==> OGRE >= 14 installed from distro package"
    exit 0
fi

# ------------------------------------------------- OGRE from source fallback
echo "==> no OGRE >= 14 package; building $OGRE_VERSION from source"
if [ ! -d "$OGRE_BUILD_DIR/.git" ]; then
    git clone --depth 1 --branch "$OGRE_VERSION" \
        https://github.com/OGRECave/ogre.git "$OGRE_BUILD_DIR"
fi
cmake -S "$OGRE_BUILD_DIR" -B "$OGRE_BUILD_DIR/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DOGRE_BUILD_RENDERSYSTEM_GL3PLUS=ON \
    -DOGRE_BUILD_COMPONENT_OVERLAY=ON \
    -DOGRE_BUILD_COMPONENT_OVERLAY_IMGUI=ON \
    -DOGRE_BUILD_COMPONENT_BITES=OFF \
    -DOGRE_BUILD_SAMPLES=OFF \
    -DOGRE_BUILD_TOOLS=OFF \
    -DOGRE_INSTALL_SAMPLES=OFF \
    -DOGRE_BUILD_DEPENDENCIES=ON
cmake --build "$OGRE_BUILD_DIR/build" -j"$JOBS"
$SUDO cmake --install "$OGRE_BUILD_DIR/build"
# Refresh the dynamic linker cache so libOgre*.so in /usr/local resolve.
command -v ldconfig >/dev/null 2>&1 && $SUDO ldconfig || true

if have_ogre; then
    echo "==> OGRE $OGRE_VERSION built and installed"
else
    echo "error: OGRE install finished but find_package(OGRE 14) still fails" >&2
    exit 1
fi
