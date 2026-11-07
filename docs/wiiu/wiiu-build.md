# Wii U build (phase 3)

This note covers building the game as a Wii U app, installing it, and what the
pieces are. Phase 3 got the app to start, find its files on the SD card and
run the game without drawing it. Drawing came in phase 4, see
[gx2-renderer.md](gx2-renderer.md).

## Building

The Wii U toolchain comes from devkitPro. The easiest way to get it is
devkitPro's official Docker image, which needs nothing else installed:

```bash
tools/wiiu/build-wiiu.sh
```

The result is `build-wiiu/pd.ppc.wuhb`. With devkitPro installed natively
(`DEVKITPRO` set), the same build without Docker is:

```bash
cmake -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/WiiU.cmake -Bbuild-wiiu .
```

```bash
cmake --build build-wiiu -j4
```

The configure step should print `Target platform: ppc-wiiu` and
`Target is big-endian: TRUE`.

devkitPro's package servers refuse connections from cloud data centres, which
is why the cloud sessions working on this repository use the Docker image.
At home either way works.

## Installing

You need a Wii U running the [Aroma](https://aroma.foryour.cafe/) homebrew
environment.

1. Copy `pd.ppc.wuhb` to `sd:/wiiu/apps/` on the SD card. Any file name works,
   for example `perfectdark.wuhb`.
2. Copy your ROM to `sd:/wiiu/apps/perfectdark/data/pd.ntsc-final.z64`. It must
   be the NTSC 1.1 ROM in `.z64` byte order, the same one the PC port uses.
   The app creates this folder on its first run, so you can also start it once
   first and copy the ROM in afterwards.
3. Perfect Dark now appears on the Wii U Menu. Start it like any other app.

Settings (`pd.ini`) and saves go in `sd:/wiiu/apps/perfectdark/`.

It also runs in [Cemu](https://cemu.info/): use File → Load and pick the
`.wuhb`. Cemu's emulated SD card is the `sdcard` folder in its data folder,
so the ROM goes in `sdcard/wiiu/apps/perfectdark/data/` there.

## What you'll see

- **Without the ROM:** an error page saying the ROM file couldn't be opened,
  with where it should go. Press any button to return to the Wii U Menu.
- **With the ROM:** the startup page while the ROM loads, then the game
  itself, drawn by the GX2 renderer (phase 4). Press HOME for the HOME menu.
  (Phase 3 builds showed a status page with a frame counter instead.)

Any other startup problem shows the same error page with its message, so
problems can be read off the screen without a PC attached.

## Debug overlay

Press **Minus** to show or hide numbers in the top left corner:

| Line | Meaning |
|---|---|
| FPS | Frames shown per second (the screen refreshes at 60) |
| frame, worst | Average and longest time between frames over the last half second |
| CPU busy, GPU wait | How much of each frame the CPU spent working (game logic and preparing the drawing) and how long it then waited for the GPU to show the previous frame. A short wait with a low FPS means the CPU is the bottleneck. |
| draws, tris, verts | What the renderer sent to the GPU that frame |
| shaders, free mem | Shaders made so far, and free main memory |

Minus isn't used by the game (it's SDL's "Back" button, which the default
bindings leave free). The toggle is in `port/wiiu/gfx_wiiu.c`
(`wiiu_handle_events`), the overlay in `port/wiiu/wiiu_overlay.c`.

## Controls

The Wii U build uses the PC port's controller bindings unchanged
(`pcjoybinds` in `port/src/input.c`). SDL reports the GamePad's and Pro
Controller's buttons by the names printed on them, so "A" below is the
button labelled A.

The game only understands N64 controller buttons, so each Wii U button
stands in for one. The game starts in the PC port's own control style
("ExtendedControls" in `pd.ini`, on by default); it's set up for two
sticks, and uses the N64 buttons like this:

| Wii U | N64 button the game sees | Does (PC port control style) |
|---|---|---|
| Right stick | Control stick | Look and turn |
| Left stick | Second stick (the 2-controller styles' second pad) | Walk and strafe |
| ZR | Z | Fire; accept in menus |
| ZL | R | Aim |
| R | L | Switch to the weapon's other fire mode |
| L | D-pad down | Weapon wheel |
| A | A | Use (doors, switches); accept in menus |
| B | D-pad left | Previous weapon; back in menus |
| X | X (extra button `0x40`) | Reload |
| Y | Y (extra button `0x80`) | Next weapon |
| + | Start | Pause menu |
| D-pad | C buttons (up, down, left, right) | Whatever the C buttons do in the chosen control style |
| Left stick click | Extra button `0x8000` | Crouch: stand, half, full |
| Minus | (none) | Debug overlay |
| Right stick click | (none) | Nothing |

Combinations from the PC port: A + ZR, or R + ZR, fires the other fire mode
once; A + B, or A + X, detonates remote mines.

Two settings change this: **SwapSticks** (on by default) makes the right
stick the N64 control stick; with it off the left stick is. With
**ExtendedControls** off, the game uses its original N64 control styles
(1.1 to 2.4, picked in the game's options), where the N64 buttons mean what
they did on the N64. Both are in `pd.ini`. So is a per-button binding list,
which can be edited to remap.

Nothing has been changed for the Wii U yet. Phase 5 is where the bindings,
Wii Remotes and the touch screen get looked at properly.

## Name entry keyboard

The PC port adds a "TYPE WITH KEYBOARD" row to the game's on-screen keyboard.
It switches to typing on a real keyboard, and you leave it with Esc, which a
Wii U doesn't have. The Wii U build leaves the row out and keeps the N64's
five-row keyboard (`src/game/menuitem.c`, `MENU_KEYBOARD_ROWS`).

Ideas for later (phase 5): use the Wii U's own on-screen keyboard for names,
and the GamePad's touch screen for menus.

While the game starts up, a "Starting up..." page lists its log messages as
they happen. If startup hangs, the last line on screen shows where. The full
log is also saved to `sd:/wiiu/apps/perfectdark/pd.log` on every run (with the
FTP plugin running, it can be fetched over the network).

## The main thread's stack

The first test on hardware got stuck on the splash screen. The likeliest
cause: the Wii U gives an app's main thread a small stack by default, and the
game was written for PCs, where the main thread gets megabytes. Running out of
stack on the Wii U doesn't always crash; it can silently overwrite other memory
and hang. So `port/wiiu/wiiu_main.c` now moves to an 8 MB stack before running
the game's real `main()`. It uses a "fiber" switch, which keeps the same thread
and CPU core, because SDL's video and the HOME menu handling must stay on the
core the app started on.

## Crashes and freezes

A crash or a hang used to leave the console stuck until it was switched off.
Now (`port/wiiu/wiiu_watchdog.c`):

- **A crash** (the game touching memory it shouldn't, or running a bad
  instruction) shows the error page instead. The Wii U lets an app handle its
  own crashes: we note where it happened and send the game thread to a
  recovery function, which shows the page and exits normally.
- **A freeze**: a small "watchdog" thread checks 10 times a second whether
  the game is still making progress (drawing frames or logging). If it hasn't
  for 2 seconds, pressing **HOME** on the GamePad stops the game and shows the
  error page. Either way, pressing any button returns to the Wii U Menu.
  After 10 seconds without progress, where the game is stuck also goes into
  `pd.log`, even if the console is then switched off.

The page and the log show code addresses: `PC` (the instruction it was on),
`LR` and `Callers` (the functions that called it). The program that produced
them turns those into function names and source lines. It needs the
`build-wiiu/pd.ppc.elf` from the same build:

```bash
docker run --rm -v $PWD:/src devkitpro/devkitppc sh -c '$DEVKITPRO/devkitPPC/bin/powerpc-eabi-addr2line -f -C -e /src/build-wiiu/pd.ppc.elf 0x0218fdfc'
```

If `pdMain is at` on the page differs from `pdMain`'s address in the `.elf`
(`powerpc-eabi-nm`), the code was loaded elsewhere and the addresses need
shifting by the difference first.

## Bugs found on the console

| Symptom | Cause | Fix |
|---|---|---|
| Stuck on the splash screen, power-off needed | Not known at the time; there was no way to see where it stopped | Startup log on screen and in `pd.log`; 8 MB game stack; crash and freeze recovery (above) |
| "The game crashed (bad memory access)", address `00000008`, in `sysSleep()` the first time the game waited for its next frame | wut's `nanosleep()` writes the "time remaining" result into its second argument without checking for NULL, which the PC code passes. Linux's C library checks, so QEMU didn't show it. | On the Wii U, `sysSleep()` uses the console's own `OSSleepTicks()` |
| Crash as soon as CamSpy was selected, address `0000066c`, in `gfx_gx2_copy_framebuffer` | Framebuffer ids were memory addresses, but the game's framebuffer copy command packs two ids into 11 bits each, so they arrived cut short | Ids are small numbers (an index), like the OpenGL back end's |
| HUD text (bottom of the screen, ammo counter) illegible | A bug in the borrowed shader generator: with two clamped textures, asking texture 1 for its size overwrote its own coordinates, because both used register R2 | Texture registers skip R1 and R2 (`third_party/libultraship/README.md`) |

The crash was found by the crash page: the `PC` and `Callers` addresses,
shifted by the difference in `pdMain`'s address and run through `addr2line`,
named `__wut_nanosleep` called from `sysSleep` called from `frametimeCalculate`.

## How the pieces fit

| Piece | What it is |
|---|---|
| `.elf` | What the compiler produces, the same format Linux uses. |
| `.rpx` | The Wii U's own executable format. `elf2rpl` converts the `.elf`. |
| `.wuhb` | "Wii U Homebrew Bundle": the `.rpx` plus a name, an icon and splash screens. Aroma lists these on the Wii U Menu. |
| wut | The library for writing Wii U homebrew: the C library, file access, and headers for the Wii U's system libraries. |
| SDL2 for Wii U | The same SDL2 the PC port uses, ported to the Wii U. It handles the window, controllers, audio and the HOME menu. That's why so little new code was needed. |

On a PC, Fast3D draws through two parts: a *window manager* (`gfx_sdl2.cpp`)
that owns the window and its events, and a *rendering back end*
(`gfx_opengl.cpp`) that turns draw calls into OpenGL. The Wii U has no OpenGL,
so both are swapped out:

- **`port/wiiu/gfx_wiiu.c`**, the Wii U window manager. SDL sets up the
  screen; this paces frames and shows them.
- **`port/fast3d/gfx_gx2.cpp`**, the GX2 rendering back end, which draws the
  game. In phase 3 it was a placeholder that drew nothing.

## Changes made

| File | Change |
|---|---|
| `CMakeLists.txt` | A Wii U target: picks SDL2 and zlib from devkitPro, leaves out the OpenGL files, builds the `.rpx` and `.wuhb`. Other platforms leave out the Wii U files. |
| `src/include/platform.h` | Recognise the Wii U (`PLATFORM_WIIU`). |
| `port/src/video.c` | Use the Wii U window manager and GX2 back end; 1280×720 by default. |
| `port/src/system.c` | Wii U startup hook, error page instead of a message box, SD card paths, on-screen log. |
| `port/src/fs.c` | On the Wii U, keep everything in the app's SD card folder. |
| `port/wiiu/` | New: the Wii U platform layer (paths, error page, text drawing, window manager). |
| `port/fast3d/gfx_gx2.*` | New: placeholder GX2 back end. |
| `dist/wiiu/` | New: icon and splash screens (plain text, no game artwork). |
| `third_party/font8x8/` | New: a public domain 8×8 bitmap font for the text pages. |
| `tools/wiiu/build-wiiu.sh` | New: the Docker build. |

Each change to a shared file is behind `PLATFORM_WIIU` or `NINTENDO_WIIU`,
and the PC build compiles exactly the files it did before.

## Status

Tested on a Wii U with Aroma (build `ae26f494b`):

- The app installs, starts, finds the ROM on the SD card and loads it.
- The game runs: the status page's frame counter climbs.
- **Audio plays**, through SDL's Wii U audio, with no Wii U-specific code.
- The GamePad is detected as a controller.
- A missing ROM, a crash or a freeze each lead to the error page and a clean
  exit to the Wii U Menu.

Not checked yet: controller input in the game (hard to judge without a
picture; phase 5), and whether saves land in `sd:/wiiu/apps/perfectdark/`.
