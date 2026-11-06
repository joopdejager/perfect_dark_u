# Big-endian support (phase 2)

The Wii U's CPU is big-endian, like the N64's. The PC port was written for
little-endian PCs, and it has a `PLATFORM_BIG_ENDIAN` switch that had never been
turned on. This note covers turning it on, how to test it without a Wii U, and
what was found.

## What big-endian means here

A 32-bit number takes 4 bytes in memory. Big-endian CPUs (N64, Wii U) store
the most significant byte first; little-endian ones (x86, ARM PCs) store it
last. The value `0x12345678` is stored as:

```
big-endian:    12 34 56 78
little-endian: 78 56 34 12
```

The game's data in the ROM is big-endian. The PC port therefore converts it
as it loads, swapping the bytes of every number (`PD_BE32()` and friends in
`src/include/platform.h`). On the Wii U the data is already in the right order,
so those swaps must do nothing. The switch already handles that.

The subtler problem is **bitfields**: struct fields smaller than a byte or
spanning parts of a word, like `u32 flags : 18`. Compilers place them starting
from the most significant bit on big-endian CPUs and from the least
significant bit on little-endian CPUs. So the PC port reverses the field order
for little-endian, and must keep the original N64 order for big-endian.

## Testing without a Wii U

We build the game for 32-bit big-endian PowerPC Linux and run it under
`qemu-ppc`, which emulates a PowerPC CPU on a normal PC. It's the same kind of
CPU as the Wii U's, so byte-order bugs show up the same way.

```bash
sudo apt-get install gcc-powerpc-linux-gnu g++-powerpc-linux-gnu qemu-user
```

Ubuntu doesn't ship PowerPC versions of SDL2 and zlib, so build them once:

```bash
tools/wiiu/build-ppc-deps.sh
```

Then build the game:

```bash
D=$PWD/build-ppc-deps
cmake -G"Unix Makefiles" -Bbuild-ppc \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/powerpc-linux-gnu.cmake \
  -DCMAKE_FIND_ROOT_PATH="/usr/powerpc-linux-gnu;$D" \
  -DSDL2_LIBRARY=$D/lib/libSDL2.so -DSDL2_INCLUDE_DIR=$D/include/SDL2 \
  -DZLIB_LIBRARY=$D/lib/libz.so -DZLIB_INCLUDE_DIR=$D/include \
  -DGL_LIBRARY=dl .
```

```bash
cmake --build build-ppc -j4
```

The configure step should print `Target is big-endian: TRUE`. Run it with:

```bash
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy qemu-ppc -L /usr/powerpc-linux-gnu -E LD_LIBRARY_PATH=$D/lib build-ppc/pd.ppc
```

`-DGL_LIBRARY=dl` is a placeholder: the renderer loads OpenGL at runtime
through SDL, and there's no PowerPC OpenGL library to link against anyway.

## Changes made

| File | Change | Why |
|---|---|---|
| `CMakeLists.txt` | Detect the target's byte order instead of hard-coding little-endian; define `PLATFORM_BIG_ENDIAN` for big-endian targets | Replaces a `# TODO`. `platform.h` also detects it, but not every file that checks the switch includes that header. |
| `src/include/platform.h` | Recognise PowerPC (`PLATFORM_PPC`) | It only knew x86 and ARM and refused to compile on anything else. |
| `src/include/types.h` | Fix the big-endian bitfield order of `struct packedpad` | **The first real big-endian bug.** See below. |
| `cmake/toolchains/powerpc-linux-gnu.cmake` | New: cross-compile settings | For the test build. |
| `tools/wiiu/build-ppc-deps.sh` | New: builds PowerPC SDL2 and zlib | For the test build. |

## Bug found: pad headers

Pads are the points levels use to place guards, objects and lift stops. Each
has a 32-bit header holding three bitfields: 18 bits of flags, a 10-bit room
number and a 4-bit lift number. The PC port's big-endian layout listed them as
`liftnum, flags, room`. The N64 original, which is big-endian code, has
`flags, room, liftnum`.

On a Wii U every pad's flags, room and lift number would have read wrong.
That wouldn't crash; levels would just misbehave in odd ways, which makes this
kind of bug very hard to trace from symptoms. A test program run under qemu-ppc
confirmed it:

```
original PC port layout, ppc:  flags=2bcd5 room=159 liftnum=a -> WRONG
fixed layout, ppc:             flags=2abcd room=155 liftnum=9 -> OK
fixed layout, x86_64:          flags=2abcd room=155 liftnum=9 -> OK
```

The little-endian layout was already right and is unchanged, so PC builds
aren't affected.

## What was reviewed

- **Every big-endian-only layout** in the port: `packedpad` and
  `soundnumhack` in `types.h`, and seven graphics command layouts in
  `include/PR/gbi.h` (`Tri`, `Gdma`, `Gtri`, `Gtri4`, `Gtexture`, `GunkC0`, the
  triangle vertex order macro). Each was compared with the N64 original. Only
  `packedpad` was wrong.
- **Bitfield changes** between the port and the original decomp, in `types.h`
  and `gbi.h`. Apart from the layouts above they're field renames. Those structs
  live only in memory and are never read raw from ROM data, so their order
  doesn't matter.
- **Byte swaps**: no code swaps bytes outside the `PD_BE`/`PD_LE` macros, and
  no code reads a wider value through a byte pointer.
- **Texture decoding** in `port/fast3d/gfx_pc.cpp` reads pixels a byte at a time,
  which works on either byte order.
- **Audio**: `port/src/mixer.c` already has a big-endian path.
- **Compiler warnings**: the PowerPC build has 155 warnings against 199 on
  x86_64. Two appear only on PowerPC. `mema.c:560` truncates a 64-bit size to a
  32-bit pointer, which is safe because the value always fits.
  `activemenu.c:1477` is a compiler false alarm.

32-bit code paths themselves are already tested upstream, because the PC port
ships i686 builds. So the parts that are new on the Wii U are the big-endian ones.

## What's still untested

The game starts under qemu-ppc and stops where the PC build does with no
display: opening an OpenGL window. Getting further needs two things:

1. **The ROM.** Every converter in `port/src/preprocess/` runs on ROM data, so
   their big-endian behaviour can only be checked with it.
2. **OpenGL on emulated PowerPC.** That would mean building Mesa for PowerPC
   from source, which is a large job. A cheaper option may be to test the
   loaders and converters without the renderer.

Without either, the remaining runtime testing moves to the Wii U itself in
phase 3, where the game will run natively.
