# Perfect Dark for Wii U

This repository is a fork of the [Perfect Dark PC port](https://github.com/fgsfdsfgs/perfect_dark),
which is itself built on the [Perfect Dark decompilation](https://github.com/n64decomp/perfect_dark).
The goal is a native Wii U build of the game: the game's C code compiled
for the Wii U's PowerPC CPU, with no N64 emulator involved.

This folder documents the Wii U work: what each layer does, why changes were
made, and where things stand. It's written to be readable by someone new to
game porting.

You need your own Perfect Dark ROM to play. No ROMs or game assets are, or ever
will be, included in this repository.

## How a decomp port works

An N64 game has three layers. A port keeps the top one and replaces the bottom two.

```
┌───────────────────────────────────────────┐
│ Game code (AI, weapons, levels, menus)    │  src/game, src/lib
│ Plain C. Compiles for any CPU.            │  → kept as is
├───────────────────────────────────────────┤
│ libultra (Nintendo's N64 system library)  │  port/src/libultra.c and friends
│ threads, timers, controllers, saves       │  → replaced by the PC port
├───────────────────────────────────────────┤
│ N64 hardware                              │
│  - RSP/RDP chips (graphics and audio)     │  port/fast3d (renderer),
│  - cartridge, memory, controller ports    │  port/src/mixer.c (audio)
└───────────────────────────────────────────┘
```

The game never draws anything itself. Each frame it writes a **display list**,
a list of commands such as "load these vertices, use this texture, draw these
triangles", and hands it to the graphics chip. The port's renderer, Fast3D,
reads those lists and turns them into OpenGL calls. For the Wii U, we add a
second back end that turns them into GX2 calls, GX2 being the Wii U's graphics API.

## Where the Wii U code goes

| Path | Contents |
|---|---|
| `port/fast3d/gfx_gx2.*` | GX2 rendering back end, next to the existing `gfx_opengl.cpp` |
| `port/wiiu/` | Wii U platform layer: startup, file paths on the SD card, input, audio output |
| `dist/wiiu/` | Packaging files: icon, app metadata for the `.wuhb` package |
| `third_party/` | Code borrowed from other projects, each keeping its own licence |
| `docs/wiiu/` | This documentation |
| `tools/wiiu/` | Build and test scripts |

`gfx_gx2.cpp` is still a placeholder; phase 4 fills it in.

Notes so far:

- [linux-build.md](linux-build.md): building and running the PC version.
- [big-endian.md](big-endian.md): making the port work on big-endian CPUs, tested under QEMU.
- [wiiu-build.md](wiiu-build.md): building, installing and running the Wii U app.

## Why this route

- **The Wii U CPU matches the N64 in the ways that matter.** Both are big-endian
  with 32-bit pointers, so the game's data files, which embed 32-bit pointers,
  load as they are. The PC port had to convert all of that for 64-bit
  little-endian PCs.
- **The PC port is partly ready for big-endian.** `src/include/platform.h` has
  a `PLATFORM_BIG_ENDIAN` switch that makes the byte-swap macros (`PD_BE32` and
  friends) do nothing. It's also used in `include/PR/gbi.h`, `src/include/types.h`
  and `port/src/mixer.c`. It has never been tested: `CMakeLists.txt` still
  hard-codes `TARGET_IS_BIG_ENDIAN FALSE` with a `# TODO`.
- **There's prior art for every hard part.**
  - The PC port's Switch build shows how a devkitPro console target fits into
    the CMake build (look for `NINTENDO_SWITCH`).
  - The Ship of Harkinian Wii U port shows a GX2 renderer for a Fast3D-style
    engine, and how to generate its shaders.
  - Our64 shows CafeGLSL compiling shaders on the console.

## Plan

| Phase | Work | Done when |
|---|---|---|
| 1. Foundations | Repository setup, docs, Linux build working | Port builds on Linux |
| 2. Big-endian | Build for PowerPC Linux, run under QEMU, fix byte-order bugs | Game boots and runs correctly in QEMU |
| 3. Wii U build | devkitPPC/wut toolchain, `.wuhb` packaging, SD card paths, saves | App launches on a Wii U |
| 4. GX2 renderer | GX2 back end for Fast3D, then the effects that read the screen (night vision, sun flares) | Menus and missions render correctly |
| 5. Input and audio | GamePad, Pro Controller, Wii Remotes; audio output | Playable start to finish |
| 6. Speed | Profiling, using all 3 CPU cores, faster vertex maths | Smooth in single player, then multiplayer |

Current phase: **3, Wii U build.** Phase 1 is done ([linux-build.md](linux-build.md)), and so is phase 2: the game runs correctly on big-endian PowerPC under QEMU ([big-endian.md](big-endian.md)). Phase 3 has an installable `.wuhb` that starts, finds the ROM on the SD card and runs the game without drawing it ([wiiu-build.md](wiiu-build.md)). It still needs a test on a real Wii U.

## Keeping up with the PC port

The PC port is still developed. To pull in its fixes:

```bash
git remote add upstream https://github.com/fgsfdsfgs/perfect_dark
```

```bash
git fetch upstream port
```

```bash
git merge upstream/port
```

Keep Wii U changes to shared files small and behind `PLATFORM_WIIU` (or similar)
checks, so these merges stay easy.

## Licences

- Decompilation and PC port: MIT (`LICENSE`).
- Fast3D renderer: MIT (`port/fast3d/LICENSE.txt`).
- Anything added under `third_party/` keeps its own licence, recorded next to it.
  So far: `font8x8` (public domain), used for the Wii U's text pages.
  Code under GPL, such as GLideN64 or Our64's own code, isn't copied in, because
  it would make the whole port GPL. Reading it for reference is fine.

## Glossary

- **Aroma**: the current Wii U homebrew environment, used to run unofficial apps.
- **devkitPPC / wut**: the compiler toolchain and Wii U libraries for homebrew.
- **GX2**: the Wii U's graphics API (it has no OpenGL).
- **Display list**: the list of drawing commands an N64 game builds each frame.
- **Fast3D**: the renderer that turns display lists into modern graphics calls.
- **`.wuhb`**: the package format for Wii U homebrew apps.
- **Cemu**: a Wii U emulator, handy for testing builds on a PC.
- **Big-endian**: storing the most significant byte of a number first. The N64
  and Wii U do this; x86 and ARM PCs do the opposite (little-endian).
