#!/bin/bash
# Build SDL2 and zlib for 32-bit big-endian PowerPC Linux, for the test build
# described in docs/wiiu/big-endian.md. Ubuntu doesn't ship PowerPC versions
# of these libraries, so they're compiled from source into a local folder.
#
# Needs: gcc-powerpc-linux-gnu g++-powerpc-linux-gnu cmake git
#
# Usage: tools/wiiu/build-ppc-deps.sh [install-dir]   (default: build-ppc-deps)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PREFIX="$(realpath -m "${1:-$ROOT/build-ppc-deps}")"
WORK="$PREFIX/.work"
TC="$ROOT/cmake/toolchains/powerpc-linux-gnu.cmake"
FIND="-DCMAKE_FIND_ROOT_PATH=/usr/powerpc-linux-gnu;$PREFIX"

mkdir -p "$WORK"
[ -d "$WORK/zlib" ] || git clone -q --depth 1 -b v1.3.1 https://github.com/madler/zlib "$WORK/zlib"
[ -d "$WORK/sdl2" ] || git clone -q --depth 1 -b release-2.30.x https://github.com/libsdl-org/SDL "$WORK/sdl2"

cmake -S "$WORK/zlib" -B "$WORK/b-zlib" -DCMAKE_TOOLCHAIN_FILE="$TC" "$FIND" \
	-DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_BUILD_TYPE=Release
cmake --build "$WORK/b-zlib" -j"$(nproc)"
cmake --install "$WORK/b-zlib"

# No display or sound back ends: PowerPC versions of X11, Wayland, PulseAudio
# and so on aren't available. The dummy and offscreen drivers are enough to
# start the game far enough to test it.
cmake -S "$WORK/sdl2" -B "$WORK/b-sdl" -DCMAKE_TOOLCHAIN_FILE="$TC" "$FIND" \
	-DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_BUILD_TYPE=Release \
	-DSDL_TEST=OFF -DSDL_STATIC=OFF -DSDL_OPENGL=ON -DSDL_OPENGLES=OFF \
	-DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF \
	-DSDL_PULSEAUDIO=OFF -DSDL_PIPEWIRE=OFF -DSDL_ALSA=OFF -DSDL_JACK=OFF -DSDL_SNDIO=OFF \
	-DSDL_LIBUDEV=OFF -DSDL_DBUS=OFF -DSDL_IBUS=OFF -DSDL_LIBSAMPLERATE=OFF
cmake --build "$WORK/b-sdl" -j"$(nproc)"
cmake --install "$WORK/b-sdl"

echo "PowerPC SDL2 and zlib installed in $PREFIX"
