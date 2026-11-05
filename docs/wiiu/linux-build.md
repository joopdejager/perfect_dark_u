# Building and running on Linux

The Linux PC build is our reference point. If something works there but not on
the Wii U, the bug is in our Wii U code, not in the game. This note records
what has been checked and how to run the build on a machine with no screen,
such as a cloud container.

## Verified (phase 1)

On Ubuntu 24.04, x86_64, with gcc 13.3, cmake 3.28 and SDL2 2.30:

- The port builds unchanged from upstream: one 15 MB executable, `build/pd.x86_64`,
  in about 25 seconds on 4 cores.
- About 200 compiler warnings, all from upstream code. Many are casts between
  pointers and 32-bit integers, which only warn on 64-bit targets because N64
  code assumes pointers are 32 bits. They're harmless here and won't appear on
  the Wii U, which has 32-bit pointers.
- Started with no ROM, it opens an OpenGL 3.0 window and then reports
  `Could not open ROM file pd.ntsc-final.z64` in a dialog box. That's correct.
- **No ROM is needed to build.** The game reads the ROM only at startup.

## Installing dependencies (Ubuntu/Debian)

```bash
sudo apt-get install build-essential cmake python3 libsdl2-dev libgl-dev zlib1g-dev
```

## Building

```bash
cmake -G"Unix Makefiles" -Bbuild .
```

```bash
cmake --build build -j4
```

## Running with no screen

The game needs a window with OpenGL, even just to report a missing ROM. On a
machine with no display, use a virtual X server with software OpenGL (Mesa):

```bash
sudo apt-get install xvfb libgl1-mesa-dri
```

```bash
SDL_AUDIODRIVER=dummy xvfb-run -a -s "-screen 0 1280x960x24" ./build/pd.x86_64
```

Error dialogs wait for a click that never comes, so wrap the command in
`timeout 20` and read the log it prints.

## Where the game looks for files

From the startup log:

| What | Where |
|---|---|
| ROM and data | `data/` next to the executable (`build/data/pd.ntsc-final.z64`) |
| Saves and settings | `~/.local/share/perfectdark/` |

The Wii U build will need its own choice for both, on the SD card. That's phase 3.
