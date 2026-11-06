#!/bin/bash
# Download Debian's 32-bit PowerPC system libraries (glibc, Mesa, X11, SDL2)
# into a folder that qemu-ppc can use as the target's root filesystem.
# This is what lets the PowerPC test build open a window and draw.
# See docs/wiiu/big-endian.md.
#
# Every file is verified: Ubuntu's Debian archive keyring checks the main
# Debian archive, which vouches for the current Debian Ports keyring, which
# checks the PowerPC (Ports) archive; each package is then checked against
# the signed hash list.
#
# Needs: network access to deb.debian.org, debian-archive-keyring, gpgv, python3
# Usage: tools/wiiu/fetch-ppc-sysroot.sh [dir]   (default: build-ppc-sysroot)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$(realpath -m "${1:-$ROOT/build-ppc-sysroot}")"
WORK="$OUT/.work"
FETCH="python3 -I $ROOT/tools/wiiu/debfetch.py"
MAIN=https://deb.debian.org/debian
PORTS=https://deb.debian.org/debian-ports

# Succeeds if at least one signature on the file is valid. Debian signs with
# more than one key, and gpgv fails if any of them is missing from the keyring.
verify() {
	local status
	status="$(gpgv --status-fd 1 --keyring "$1" "$2" 2>/dev/null || true)"
	grep -q '^\[GNUPG:\] VALIDSIG' <<<"$status" || { echo "signature check failed: $2" >&2; exit 1; }
	echo "verified signature on $(basename "$2")"
}

mkdir -p "$WORK"
cd "$WORK"

# 1. Main archive, checked with Ubuntu's Debian keyring
curl -sSf -o main-InRelease "$MAIN/dists/unstable/InRelease"
verify /usr/share/keyrings/debian-archive-keyring.gpg main-InRelease
$FETCH index main-InRelease "$MAIN" unstable main/binary-all/Packages.xz main-all.Packages

# 2. Current Debian Ports keyring, from the verified main archive
$FETCH pkgs main-all.Packages "$MAIN" keyring debian-ports-archive-keyring
rm -rf keyring/x && dpkg-deb -x keyring/*.deb keyring/x

# 3. PowerPC archive, checked with that keyring
curl -sSf -o ports-InRelease "$PORTS/dists/unstable/InRelease"
verify keyring/x/usr/share/keyrings/debian-ports-archive-keyring.gpg ports-InRelease
$FETCH index ports-InRelease "$PORTS" unstable main/binary-powerpc/Packages.xz ppc.Packages

# 4. The libraries the game needs at runtime, plus their dependencies
$FETCH pkgs ppc.Packages "$PORTS" debs \
	libc6 libgcc-s1 libstdc++6 zlib1g libsdl2-2.0-0 \
	libgl1 libglx-mesa0 libgl1-mesa-dri libegl1 libegl-mesa0

for d in debs/*.deb; do dpkg-deb -x "$d" "$OUT"; done
ln -sfn usr/lib "$OUT/lib"
ln -sfn usr/bin "$OUT/bin"
echo "PowerPC system libraries installed in $OUT"
