#!/bin/sh
# Builds the Wii U app (build-wiiu/pd.ppc.wuhb) using devkitPro's official
# Docker image, which contains the whole Wii U toolchain (devkitPPC, wut,
# SDL2 for Wii U). Needs Docker. Usage: tools/wiiu/build-wiiu.sh [cmake args]
#
# If you have devkitPro installed natively instead, the same two commands
# work without Docker:
#   cmake -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/WiiU.cmake -Bbuild-wiiu .
#   cmake --build build-wiiu -j4
set -eu

IMAGE="${DEVKITPPC_IMAGE:-devkitpro/devkitppc:latest}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

docker run --rm \
	-v "$ROOT:/src" -w /src \
	--user "$(id -u):$(id -g)" -e HOME=/tmp \
	"$IMAGE" sh -c '
		git config --global --add safe.directory /src
		cmake -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/WiiU.cmake" -Bbuild-wiiu "$@" .
		cmake --build build-wiiu -j"$(nproc)"
	' sh "$@"

echo
echo "Built: $ROOT/build-wiiu/pd.ppc.wuhb"
