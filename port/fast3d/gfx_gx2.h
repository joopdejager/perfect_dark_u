#ifndef GFX_GX2_H
#define GFX_GX2_H

#include <stddef.h>
#include <stdint.h>

#include "gfx_rendering_api.h"

#ifdef __cplusplus
extern "C" {
#endif

extern struct GfxRenderingAPI gfx_gx2_api;

// What the renderer did this frame, for the Wii U debug overlay.
struct GfxGx2Stats {
    uint32_t draws;        // draw calls sent to the GPU
    uint32_t triangles;
    uint32_t skipped;      // draws skipped because a texture wasn't loaded
    uint32_t vertex_bytes; // vertex data used this frame
    uint32_t shaders;      // shaders made so far
};
void gfx_gx2_get_stats(struct GfxGx2Stats* out);

// 2D drawing on top of each finished frame. `fn` is called at the end of
// every frame, with drawing set up for gfx_gx2_overlay_quads(). NULL turns
// it off. Coordinates are pixels on the 1280x720 picture, from the top left.
typedef void (*GfxGx2OverlayFn)(void);
void gfx_gx2_set_overlay(GfxGx2OverlayFn fn);

struct GfxGx2Quad {
    float x0, y0, x1, y1; // corners, in pixels
    float u0, v0, u1, v1; // texture corners, 0..1
    uint32_t rgba;        // colour the texture is multiplied by, 0xRRGGBBAA
};

// Makes a texture for overlay drawing from RGBA pixels; returns its id.
uint32_t gfx_gx2_overlay_texture(const uint8_t* rgba, uint32_t width, uint32_t height);

// Draws textured, tinted rectangles. Only valid inside the overlay function.
void gfx_gx2_overlay_quads(uint32_t texture_id, const struct GfxGx2Quad* quads, size_t count);

#ifdef __cplusplus
}
#endif

#endif
