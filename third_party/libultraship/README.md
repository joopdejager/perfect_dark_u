# GX2 shader generator (from libultraship)

These files come from [libultraship](https://github.com/Kenix3/libultraship),
the library behind Ship of Harkinian and other N64 ports. They were written by
GaryOderNichts for libultraship's Wii U support, which was later removed from
libultraship. They were taken from the last commit that still had them,
`6f978e0d237530f304ba2018bbdc539b175c184a` (`src/graphic/Fast3D/`).

Licence: MIT (`LICENSE`, libultraship's licence; `gx2_shader_inl.h` also
names Ali Bouhlel, from RetroArch's Wii U shaders, and says it is MIT
licensed).

## What it does

The Wii U's GPU (an AMD R700-family chip) runs shaders as its own machine
code. Normally you write shaders in a high-level language and compile them on
a PC. This generator skips the compiler: it writes the machine code
instructions for each colour-combiner setup directly, at run time, the moment
the game first needs that combination. That's why the Wii U build needs no
shader compiler and no set of precompiled shaders.

`gx2_shader_inl.h` holds macros that build each instruction word.
`gx2_shader_gen.c` strings them together for a given `CCFeatures` (the
combiner description Fast3D produces).

## Changes made for this port

- Removed the "mask" and "blend" texture inputs. They belong to libultraship's
  texture pack feature, which the Perfect Dark port doesn't have.
- Vertex attributes match the packed vertex layout of this port's
  `port/fast3d/gfx_pc.cpp`. libultraship instead changed its `gfx_pc.cpp` to
  pad every attribute to 4 floats, and doing it here keeps that shared file
  untouched.
- `SHADER_TEXINFO0` starts after this port's last combiner input
  (`SHADER_LOD_FRAC`) rather than `SHADER_NOISE`.
- Include path of `gfx_cc.h`; added the `assert.h` and `string.h` includes.
- Bug fix: texture colour registers no longer use R1 or R2, where the
  texture coordinates arrive. Upstream could put texture 1 in R2, so when
  both textures were clamped, asking texture 1 for its size overwrote its own
  coordinates. With noise, texture 0 could land in R2 the same way. Found
  through Perfect Dark's HUD text, which uses two clamped textures and came
  out illegible.
