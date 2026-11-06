#!/bin/bash
# Run the PowerPC test build under qemu-ppc on a virtual display.
# See docs/wiiu/big-endian.md.
#
# Usage: tools/wiiu/run-ppc.sh <rom> [game args...]
#   e.g. tools/wiiu/run-ppc.sh ~/roms/pd.ntsc-final.z64 --boot-stage 0x30
#
# Set SHOTS=<dir> to save a screenshot of the virtual display every
# SHOT_INTERVAL seconds (default 60). Set DURATION to stop after that many
# seconds (default: run until the game exits).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ROM="$(realpath "$1")"; shift
SYSROOT="${SYSROOT:-$ROOT/build-ppc-sysroot}"
RUNDIR="${RUNDIR:-$ROOT/build-ppc/run}"
DISP="${DISP:-:51}"
mkdir -p "$RUNDIR/save" "$RUNDIR/xdg"
chmod 700 "$RUNDIR/xdg"

# +byteswappedclients: the X server must accept a big-endian client.
Xvfb "$DISP" +byteswappedclients -screen 0 1280x960x24 </dev/null >/dev/null 2>&1 &
XP=$!
trap 'kill $XP 2>/dev/null || true' EXIT
sleep 2

# GALLIUM_DRIVER=softpipe: Mesa's faster llvmpipe can't generate 32-bit
# PowerPC code. FORCE_EGL: the X server's GLX extension doesn't work for
# byte-swapped clients, so render on the client side through EGL instead.
DISPLAY="$DISP" XDG_RUNTIME_DIR="$RUNDIR/xdg" SDL_AUDIODRIVER=dummy \
GALLIUM_DRIVER=softpipe SDL_VIDEO_X11_FORCE_EGL=1 SDL_VIDEO_FORCE_EGL=1 \
	timeout "${DURATION:-0}" qemu-ppc -L "$SYSROOT" -E LD_LIBRARY_PATH=/usr/lib/powerpc-linux-gnu \
	"$ROOT/build-ppc/pd.ppc" --rom-file "$ROM" --savedir "$RUNDIR/save" --basedir "$RUNDIR" "$@" \
	</dev/null >"$RUNDIR/pd.log" 2>&1 &
GP=$!

if [ -n "${SHOTS:-}" ]; then
	mkdir -p "$SHOTS"
	t=0
	while kill -0 $GP 2>/dev/null; do
		sleep "${SHOT_INTERVAL:-60}"; t=$((t + ${SHOT_INTERVAL:-60}))
		DISPLAY="$DISP" import -window root "$SHOTS/shot-$(printf %04d $t).png" 2>/dev/null || true
	done
fi
wait $GP || true
echo "Game log: $RUNDIR/pd.log"
