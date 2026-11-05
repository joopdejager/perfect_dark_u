# CLAUDE.md

Working rules for this repository: a Wii U port of Perfect Dark, built on the
Perfect Dark PC port (fgsfdsfgs/perfect_dark). See `docs/wiiu/README.md` for the
project overview, layout and plan.

## Never commit game data

- No ROMs (`*.z64`, `*.n64`, `*.v64`, `pd.gbc`), extracted assets, or files
  derived from them. `.gitignore` covers the usual names; check `git status`
  before every commit anyway.
- To test with a ROM, keep it outside the repository or in an ignored path.

## Workflow

- Work on a branch and open a pull request into `main`. Don't push to `main` directly.
- One phase step per pull request, with commit messages in plain language that
  explain why a change was made, not just what it does.
- When a change teaches something about how the port works, add a short note
  under `docs/wiiu/`. The project owner is learning as the work goes.
- Update the "Current phase" line in `docs/wiiu/README.md` when a phase finishes.

## Code placement

- Wii U-only code goes in `port/wiiu/`, the GX2 renderer in `port/fast3d/gfx_gx2.*`,
  packaging files in `dist/wiiu/`.
- Keep edits to shared files (game code, `port/src`, `CMakeLists.txt`) minimal
  and behind platform checks, so merges from the upstream PC port stay easy.
- Don't change game behaviour in `src/game` unless a platform bug requires it,
  and say why in the commit.
- Borrowed code goes under `third_party/<project>/` with its licence file. No
  GPL code (it would relicense the port); MIT, BSD and zlib are fine.

## Building (Linux, PC target)

Needs gcc/g++ 10+, cmake, make, python3, SDL2 2.0.12+, libGL and zlib.

```bash
cmake -G"Unix Makefiles" -Bbuild .
```

```bash
cmake --build build -j4
```

The executable is `build/pd.<arch>`. Running it needs `data/pd.ntsc-final.z64`
next to it. The game targets the ntsc-final ROM (md5 `e03b088b6ac9e0080440efed07c1e40f`).

Wii U build instructions will be added in phase 3.

## Platform facts worth remembering

- The Wii U is big-endian with 32-bit pointers, like the N64. The port's
  `PLATFORM_BIG_ENDIAN` switch (`src/include/platform.h`) exists but has never
  been tested, and `CMakeLists.txt` hard-codes `TARGET_IS_BIG_ENDIAN FALSE`.
- Data loaded from the ROM goes through the converters in `port/src/preprocess/`.
  On big-endian, byte swaps there should become no-ops; bugs show up as
  silently corrupt data, not crashes.
- There's no OpenGL on the Wii U. Rendering must go through GX2.
