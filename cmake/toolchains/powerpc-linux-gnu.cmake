# Cross-compile for 32-bit big-endian PowerPC Linux.
#
# This is a test target, not something anyone plays on. The Wii U's CPU is a
# 32-bit big-endian PowerPC, so building for PowerPC Linux and running the
# result under qemu-ppc catches byte-order bugs before the Wii U build exists.
# See docs/wiiu/big-endian.md.
#
# Usage:
#   cmake -Bbuild-ppc -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/powerpc-linux-gnu.cmake \
#         -DCMAKE_PREFIX_PATH=/path/to/ppc/sysroot .

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR ppc)

set(CMAKE_C_COMPILER powerpc-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER powerpc-linux-gnu-g++)

# Look for libraries and headers only in the target sysroot and prefix path,
# never in the host's x86_64 directories.
set(CMAKE_FIND_ROOT_PATH /usr/powerpc-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
