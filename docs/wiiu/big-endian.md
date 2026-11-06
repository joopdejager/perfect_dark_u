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

The configure step should print `Target is big-endian: TRUE`.

`-DGL_LIBRARY=dl` is a placeholder: the renderer loads OpenGL at runtime
through SDL, so nothing needs linking against a PowerPC OpenGL library.

## Running it with the ROM

To actually play under qemu-ppc, the game needs PowerPC versions of glibc,
Mesa (software OpenGL), the X11 client libraries and SDL2 with X11 support.
Debian still publishes these for 32-bit PowerPC in its "Ports" archive. This
script downloads and verifies them (it needs network access to `deb.debian.org`):

```bash
sudo apt-get install debian-archive-keyring gpgv xz-utils xvfb imagemagick
```

```bash
tools/wiiu/fetch-ppc-sysroot.sh
```

Then run the game on a virtual display, here booting straight into the
Defection mission and saving a screenshot every 60 seconds:

```bash
SHOTS=shots tools/wiiu/run-ppc.sh /path/to/pd.ntsc-final.z64 --boot-stage 0x30
```

`--skip-intro` boots into the Carrington Institute instead. Expect it to be
slow: well under one frame per second, because QEMU emulates every PowerPC
instruction and Mesa draws everything in software. It's for checking that
things work, not for playing.

Three workarounds are built into `run-ppc.sh`, each for a quirk of this test
setup rather than of the game:

- **`+byteswappedclients`** on the virtual X server, which otherwise refuses a
  big-endian program.
- **EGL instead of GLX** (`SDL_VIDEO_FORCE_EGL`). The X server's own OpenGL
  extension fails with byte-swapped clients, so Mesa renders on the client side
  and sends finished images over the plain X protocol.
- **Mesa's `softpipe` renderer.** The faster `llvmpipe` crashes because its code
  generator doesn't support 32-bit PowerPC.

## Changes made

| File | Change | Why |
|---|---|---|
| `CMakeLists.txt` | Detect the target's byte order instead of hard-coding little-endian; define `PLATFORM_BIG_ENDIAN` for big-endian targets | Replaces a `# TODO`. `platform.h` also detects it, but not every file that checks the switch includes that header. |
| `src/include/platform.h` | Recognise PowerPC (`PLATFORM_PPC`) | It only knew x86 and ARM and refused to compile on anything else. |
| `src/include/types.h` | Fix the big-endian bitfield order of `struct packedpad` | **The first real big-endian bug.** See below. |
| `cmake/toolchains/powerpc-linux-gnu.cmake` | New: cross-compile settings | For the test build. |
| `tools/wiiu/build-ppc-deps.sh` | New: builds PowerPC SDL2 and zlib | For the test build. |
| `tools/wiiu/fetch-ppc-sysroot.sh`, `tools/wiiu/debfetch.py` | New: download and verify Debian's PowerPC system libraries | So the test build can open a window and draw. |
| `tools/wiiu/run-ppc.sh` | New: runs the test build under qemu-ppc on a virtual display | Optionally saves screenshots. |
| `port/src/preprocess/misc.c`, `port/src/preprocess/gbi.c`, `port/fast3d/gfx_pc.cpp` | Big-endian fixes | See "Bugs found by running with the ROM" below. |

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

## Bugs found by running with the ROM

The code review above checked everything written in terms of the byte-swap
macros, and that code turned out to be sound. Running the game found three more
bugs, all in hand-written bit tricks that were only right on little-endian.
Each fix applies only to big-endian builds; the PC code paths are unchanged.

| Bug | Where | What happened | Fix |
|---|---|---|---|
| Texture list scrambled | `port/src/preprocess/misc.c`, `preprocessTexturesList()` | The loader reshuffles each entry's bits (shift, byte swap, swap two 4-bit fields), and the shuffle isn't one of the macros, so it ran on big-endian too. Every texture offset became garbage, and the first texture load crashed copying 241 KB. | Skip the conversion: the ROM layout is already the native one. |
| Graphics commands with their words swapped | `port/src/preprocess/gbi.c`, `gbiConvertGdl()` and `gbiGdlRewriteAddrs()` | On 32-bit builds a command is held as one 64-bit number, and the code assumed its first word is the low half. That's only true on little-endian. Converted display lists had their two words reversed: "Unknown GBI opcode". | Keep the first word in the high half on big-endian, and read the opcode from there. The address rewrite stays a no-op, as it is on 32-bit PC builds. |
| Text drawn as solid blocks | `port/fast3d/gfx_pc.cpp`, `palette_to_rgba32()` | Font glyphs use IA16 palettes (intensity plus alpha). The N64 keeps intensity in the high byte; the renderer reads it from the low byte, which only works on PCs because the palette bytes get swapped twice. On big-endian they're swapped zero times, so glyphs came out fully opaque. | On big-endian, read IA16 the N64 way. |

How they were found, which is worth repeating for future bugs:

- **A debugger** (`gdb-multiarch` attached to `qemu-ppc -g`) pinpointed the
  texture crash.
- **Screenshots** of the virtual display showed the wrong-opcode error and the
  block text.
- **Comparing data between x86 and PowerPC** settled where the remaining
  problems are not. With a temporary hook logging a fingerprint of every texture
  and vertex colour table the renderer received, 74 of 75 textures and 58 of 61
  colour tables were byte-identical between the two. The few differences were
  timing: animated textures and lighting caught at different moments.

## Status

Under qemu-ppc with the ROM, the PowerPC build:

- boots and draws the 3D title logo correctly;
- loads the Carrington Institute level, with geometry, textures, models and
  text all correct;
- loads the Defection mission (`--boot-stage 0x30`) and plays its opening
  fly-through of the city;
- runs for several minutes without crashing.

## Known issues

- **Yellow-green tint.** Frames on PowerPC look tinted compared with x86
  (grey walls come out yellow-green). Textures and lighting colours reach the
  renderer identical to x86, and a small test program confirmed Mesa itself
  draws and reads back correct colours. So the cause is somewhere between
  drawing a frame and getting it onto the virtual display, possibly specific to
  this test setup. It may never appear on the Wii U, which has its own graphics
  back end. Parked for now, since it doesn't block anything.
- **Not tested yet:** audio output (the test runs use SDL's dummy audio
  driver), saving, and controller input.
