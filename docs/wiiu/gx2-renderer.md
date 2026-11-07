# GX2 renderer (phase 4)

This note covers how the game's picture gets drawn on the Wii U. Phase 3
ran the game with a renderer that drew nothing. Phase 4 replaces it with one
that drives the Wii U's GPU through GX2, the console's graphics library.

## Standing on libultraship's shoulders

Ship of Harkinian (the Ocarina of Time PC port) once had a Wii U version. Its
renderer lived in libultraship, the library it shares with other N64 ports,
and was written by GaryOderNichts. libultraship later dropped Wii U support,
but its last version is still in its history, under the MIT licence. Its
renderer is a Fast3D back end, the same kind of thing as our
`port/fast3d/gfx_opengl.cpp`. That's because both ports' Fast3D comes from the
same original, so most of it fits our port directly.

What we took:

| Ours | From libultraship | Changes |
|---|---|---|
| `third_party/libultraship/gx2_shader_gen.c`, `.h`, `gx2_shader_inl.h` | The shader generator, as is apart from small changes | Listed in `third_party/libultraship/README.md` |
| `port/fast3d/gfx_gx2.cpp` | The back end, as a starting point | Rewritten for our Fast3D interface and for sharing the GPU with SDL (below) |

We didn't take its window manager (`gfx_wiiu.cpp`). It sets up the screen and
the HOME menu handling by hand, which SDL already does for us.

## Shaders without a compiler

A shader is a small program the GPU runs for every corner of a triangle
(vertex shader) and every pixel (pixel shader). N64 games describe how each
pixel's colour is worked out with the *colour combiner*. It's a formula like
`(texture − shade) × primitive + shade`, set differently for nearly every
object. Fast3D turns each combiner setup into its own shader.

On a PC the OpenGL back end writes each shader as GLSL text and the graphics
driver compiles it. The Wii U has no compiler on the console, and a game
normally ships shaders precompiled on a PC. Perfect Dark uses hundreds of
combiner setups, though, and nobody knows the full list in advance.

GaryOderNichts' generator gets around that: it writes the GPU's machine code
instructions directly, from a handful of patterns (mix, multiply, fog,
texture clamping, noise). That's quick enough to do while the game runs, the
first time each setup is seen. `pd.log` lists each one as `GX2: shader N made for ...`.

## How a frame is drawn

1. **Wait** (`gfx_wiiu.c`, `wiiu_start_frame`): until the GPU has shown the
   previous frame. This paces the game to the screen. It also means the GPU
   has finished with the previous frame's vertex data, so that memory can be
   reused.
2. **Draw** (`gfx_gx2.cpp`): the game's display list runs through Fast3D,
   which calls the back end:
   - `draw_triangles` copies the vertices into a 32 MB buffer and queues a
     draw for the GPU.
   - Textures are copied to GPU-readable memory when they're first used.
   - Shaders are generated when they're first needed.
3. **Copy out** (`end_frame`): the finished picture (1280×720) is copied into
   the TV's and the GamePad's screen buffers. GX2 scales it to each screen.
4. **Show** (`wiiu_swap_buffers_begin`): swap the screen buffers.

## Sharing the GPU with SDL

SDL still owns the screen: its Wii U video driver starts GX2, creates the
screen buffers and handles the HOME menu. That menu takes the screen away
from the app and gives it back, and the screen buffers must be freed and
recreated around it. SDL's 2D renderer also still draws the startup page and
the error page.

GPU settings (which shader, blending, depth test and so on) live in a
**context state**. `gfx_gx2.cpp` has its own, separate from SDL's, and
switches to it at the start of every frame. So neither side has to know what
the other set. Clearing and copying framebuffers also change settings
internally, so the back end switches back to its context after those too.

## Differences from OpenGL worth knowing

- **Which way is up.** OpenGL counts pixel rows from the bottom, GX2 from the
  top. Fast3D gives viewports and scissor boxes the OpenGL way, so the back end
  flips them. Textures made from framebuffers come out the right way up on
  their own, so `invert_y` is always false.
- **Texture memory layout.** The GPU normally stores textures in a "tiled"
  order, which is faster to sample but needs converting to. Textures here use
  "linear aligned" layout instead: plain rows, so the CPU can copy pixels in
  directly. Each row may be padded (the surface's `pitch`).
- **Caches.** The CPU and GPU don't share caches. After the CPU writes
  something the GPU will read (vertices, textures, shaders), `GX2Invalidate`
  flushes it out of the CPU's cache.
- **Vertex layout.** Our `gfx_pc.cpp` packs vertices tightly: a texture
  coordinate is 2 to 4 floats depending on clamping, and an input colour is 3
  floats unless the shader uses alpha. libultraship padded everything to 4
  floats instead, by changing its `gfx_pc.cpp`. Here the fetch shader (the GPU
  program that reads vertex data) is told each attribute's real size and
  fills in the rest. That keeps the shared `gfx_pc.cpp` untouched.

## The debug overlay

The overlay (Minus, see [wiiu-build.md](wiiu-build.md)) is drawn by the
same back end, after the game's frame and before it's copied to the screens.
It uses one fixed shader: texture colour times vertex colour, made by the
same generator from a hand-written combiner setup. Its text uses the 8×8 font
from the startup page, uploaded as a texture. Fast3D only sends settings that
change, so after the overlay the back end puts back the last viewport,
scissor, depth, blending and shader that Fast3D set.

## Memory

Everything (framebuffers, textures, vertices) is in MEM2, the main memory.
The Wii U also has 32 MB of fast MEM1 next to the GPU, where framebuffers
draw faster, but SDL claims all of it for its own use. Moving our
framebuffers there is a phase 6 (speed) job.

## Stages

| Stage | What | Status |
|---|---|---|
| 1 | Set up our own drawing, shaders, textures, blending, depth, fog | Done: tested on a Wii U, the title, menus and a new game look right |
| 2–3 | Check menus, text and 3D levels; fix what's wrong | After stage 1's test |
| 4 | Effects that copy the screen: blurred menu backgrounds, night vision, sun flares (`copy_framebuffer`) | Not started |

Stage 1 covers more than first planned (untextured triangles only), because
the libultraship code does textures, blending, depth and fog already. Not
done yet:

- **`copy_framebuffer`** does nothing, so effects that read the screen back
  will look wrong or be missing (stage 4).
- **The blur filter** (`SHADER_OPT_BLUR`, menu backgrounds) is drawn as a
  plain texture.
- **Three-point filtering** (the N64's own texture filter, an option in the PC
  port) falls back to ordinary bilinear filtering.
- **Mipmaps** aren't generated.
- **Texture slots reused within one frame.** When Fast3D's texture cache
  reuses a texture slot of the same size within a frame, the new picture
  overwrites the old one before the GPU has drawn with it. Earlier draws that
  frame may then show the new texture. That can only happen when the cache is
  full, or after the game frees textures. If it shows up as flickering
  textures, the fix is to give each upload fresh memory.

## Reading pd.log

Besides the startup lines, the renderer writes:

- `GX2: renderer ready`: the back end started.
- `GX2: shader N made for ...`: a new combiner setup was seen.
- `GX2: frame N: D draws, T triangles, S skipped, K KB of vertices`: on the
  first frame and then every 600 frames (about 10–20 seconds). `D` and `T`
  show the game is handing the GPU work. `S` counts draws skipped because a
  texture they needed wasn't loaded.

If the screen stays black but these numbers are healthy, the problem is in
how things are drawn, not whether they are.
