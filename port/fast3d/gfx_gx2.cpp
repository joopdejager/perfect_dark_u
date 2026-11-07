// GX2 rendering back end for Fast3D (Wii U).
//
// This turns Fast3D's draw calls into GX2 calls, the way gfx_opengl.cpp turns
// them into OpenGL calls. GX2 is the Wii U's own graphics library: lower level
// than OpenGL, closer to how the GPU actually works.
//
// Based on gfx_gx2.cpp from libultraship (Ship of Harkinian's library),
// created in 2022 by GaryOderNichts, MIT licence: see
// third_party/libultraship/LICENSE. Adapted to this port's newer Fast3D
// interface (depth modes, depth range, modulate blending) and to sharing the
// GPU with SDL, which owns the screen here (see port/wiiu/gfx_wiiu.c).
//
// Shaders are written as GPU machine code at run time by
// third_party/libultraship/gx2_shader_gen.c, one per colour-combiner setup,
// so no shader compiler is needed.
//
// Not done yet (later stages of phase 4, see docs/wiiu/gx2-renderer.md):
// copying between framebuffers, the blur filter, three-point filtering and
// mipmaps. They fall back to something simpler or do nothing.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <algorithm>
#include <map>
#include <utility>

#include <coreinit/memory.h>
#include <gx2/clear.h>
#include <gx2/context.h>
#include <gx2/draw.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/sampler.h>
#include <gx2/shaders.h>
#include <gx2/surface.h>
#include <gx2/swap.h>
#include <gx2/texture.h>
#include <gx2/utils.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/gbi.h>

#include "platform.h"
#include "system.h"

#include "gfx_cc.h"
#include "gfx_rendering_api.h"
#include "gfx_pc.h"
#include "gfx_gx2.h"

#include "../../third_party/libultraship/gx2_shader_gen.h"

#define ALIGN_UP(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

// Vertex data for one frame goes into this buffer, one draw after another.
// The GPU reads it later, so it can't be reused until the frame is shown;
// the window manager waits for that before the next frame starts.
#define DRAW_BUFFER_SIZE (32 * 1024 * 1024)

struct ShaderProgram {
    struct ShaderGroup group;
    uint8_t num_inputs;
    bool used_textures[2];
    bool used_noise;
    int32_t window_params_offset;
};

struct Texture {
    GX2Texture texture;
    GX2Sampler sampler;
    bool uploaded;
};

struct Framebuffer {
    GX2ColorBuffer color_buffer;
    GX2DepthBuffer depth_buffer;
    bool has_depth;
    // the same memory as color_buffer, for drawing this buffer as a texture
    struct Texture tex;
};

static GX2ContextState* context_state;

static struct Framebuffer main_framebuffer;
static struct Framebuffer* current_framebuffer;

static std::map<std::pair<uint64_t, uint32_t>, struct ShaderProgram> shader_program_pool;
static struct ShaderProgram* current_shader_program;

// textures bound to the two texture slots (the shaders always read texture 0
// from slot 0 and texture 1 from slot 1)
static struct Texture* bound_textures[2];
static int current_tile;

static uint8_t* draw_buffer;
static uint8_t* draw_ptr;

static uint32_t frame_count;

// numbers for pd.log, to tell "drawing nothing" apart from "drawing wrongly"
static uint32_t stat_draws;
static uint32_t stat_tris;
static uint32_t stat_skipped;
static float current_noise_scale = 1.0f;
static enum FilteringMode current_filter_mode = FILTER_LINEAR;

static float depth_near = 0.0f;
static float depth_far = 1.0f;
static int viewport[4];

// The last settings Fast3D asked for. Fast3D remembers what it set and only
// sends changes, so after the overlay (below) changes them, they're put back.
static struct {
    bool test, update, compare, source_prim;
    uint16_t zmode;
} last_depth = { true, true, true, false, ZMODE_OPA };
static bool last_use_alpha, last_modulate;
static int last_scissor[4] = { 0, 0, 1, 1 };

// 2D drawing on top of each finished frame (the Wii U debug overlay)
static GfxGx2OverlayFn overlay_fn;
static struct ShaderProgram overlay_prg;
static bool overlay_prg_ready;

static const char* gfx_gx2_get_name(void) {
    return "GX2";
}

static int gfx_gx2_get_max_texture_size(void) {
    return 8192;
}

static struct GfxClipParameters gfx_gx2_get_clip_parameters(void) {
    // GX2 uses OpenGL's -1..1 depth range by default. Unlike OpenGL, a
    // framebuffer's first row is the top of the picture whether it's the
    // screen or a texture, so nothing ever needs flipping.
    return { false, false };
}

static void gfx_gx2_unload_shader(struct ShaderProgram* old_prg) {
    current_shader_program = nullptr;
}

static void gfx_gx2_load_shader(struct ShaderProgram* new_prg) {
    current_shader_program = new_prg;

    GX2SetFetchShader(&new_prg->group.fetchShader);
    GX2SetVertexShader(&new_prg->group.vertexShader);
    GX2SetPixelShader(&new_prg->group.pixelShader);
}

static struct ShaderProgram* gfx_gx2_create_and_load_new_shader(uint64_t shader_id0, uint32_t shader_id1) {
    struct CCFeatures cc_features;
    gfx_cc_get_features(shader_id0, shader_id1, &cc_features);

    struct ShaderProgram* prg = &shader_program_pool[std::make_pair(shader_id0, shader_id1)];

    if (gx2GenerateShaderGroup(&prg->group, &cc_features) != 0) {
        shader_program_pool.erase(std::make_pair(shader_id0, shader_id1));
        sysFatalError("Could not create a GPU shader (%016llx-%08x): out of memory.",
                      (unsigned long long)shader_id0, (unsigned)shader_id1);
    }

    prg->num_inputs = cc_features.num_inputs;
    prg->used_textures[0] = cc_features.used_textures[0];
    prg->used_textures[1] = cc_features.used_textures[1];
    prg->used_noise = false;
    for (int c = 0; c < 2; c++) {
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 4; j++) {
                prg->used_noise |= cc_features.c[c][i][j] == SHADER_NOISE;
            }
        }
    }
    prg->used_noise |= cc_features.opt_alpha && cc_features.opt_noise;

    GX2UniformVar* uniform = GX2GetPixelUniformVar(&prg->group.pixelShader, "window_params");
    prg->window_params_offset = uniform ? (int32_t)uniform->offset : -1;

    sysLogPrintf(LOG_NOTE, "GX2: shader %u made for %016llx-%08x", (unsigned)shader_program_pool.size(),
                 (unsigned long long)shader_id0, (unsigned)shader_id1);

    gfx_gx2_load_shader(prg);
    return prg;
}

static struct ShaderProgram* gfx_gx2_lookup_shader(uint64_t shader_id0, uint32_t shader_id1) {
    auto it = shader_program_pool.find(std::make_pair(shader_id0, shader_id1));
    return it == shader_program_pool.end() ? nullptr : &it->second;
}

static void gfx_gx2_shader_get_info(struct ShaderProgram* prg, uint8_t* num_inputs, bool used_textures[2]) {
    *num_inputs = prg->num_inputs;
    used_textures[0] = prg->used_textures[0];
    used_textures[1] = prg->used_textures[1];
}

static void gfx_gx2_clear_shaders(void) {
    // the GPU may still be using them
    GX2DrawDone();
    for (auto& it : shader_program_pool) {
        gx2FreeShaderGroup(&it.second.group);
    }
    shader_program_pool.clear();
    current_shader_program = nullptr;
}

static uint32_t gfx_gx2_new_texture(void) {
    // pointers are 32 bits on the Wii U, so the id is simply the pointer
    struct Texture* tex = (struct Texture*)calloc(1, sizeof(struct Texture));
    GX2InitSampler(&tex->sampler, GX2_TEX_CLAMP_MODE_WRAP, GX2_TEX_XY_FILTER_MODE_POINT);
    return (uint32_t)(uintptr_t)tex;
}

static void gfx_gx2_delete_texture(uint32_t texture_id) {
    struct Texture* tex = (struct Texture*)(uintptr_t)texture_id;
    if (!tex) {
        return;
    }
    for (int i = 0; i < 2; i++) {
        if (bound_textures[i] == tex) {
            bound_textures[i] = nullptr;
        }
    }
    GX2DrawDone();
    free(tex->texture.surface.image);
    free(tex);
}

static void gfx_gx2_select_texture(int tile, uint32_t texture_id, bool linear_filter) {
    bound_textures[tile] = (struct Texture*)(uintptr_t)texture_id;
    current_tile = tile;
}

static void gfx_gx2_upload_texture(const uint8_t* rgba32_buf, uint32_t width, uint32_t height, bool gen_mipmaps) {
    struct Texture* tex = bound_textures[current_tile];
    if (!tex) {
        return;
    }

    if (tex->texture.surface.width != width || tex->texture.surface.height != height ||
        !tex->texture.surface.image) {
        if (tex->texture.surface.image) {
            // the GPU may still read the old picture this frame
            GX2DrawDone();
            free(tex->texture.surface.image);
        }

        memset(&tex->texture, 0, sizeof(GX2Texture));
        tex->texture.surface.use = GX2_SURFACE_USE_TEXTURE;
        tex->texture.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
        tex->texture.surface.width = width;
        tex->texture.surface.height = height;
        tex->texture.surface.depth = 1;
        tex->texture.surface.mipLevels = 1;
        tex->texture.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
        tex->texture.surface.aa = GX2_AA_MODE1X;
        // "linear aligned" keeps rows in plain order, so the CPU can copy
        // pixels in directly instead of rearranging them into the GPU's tiles
        tex->texture.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
        tex->texture.viewNumMips = 1;
        tex->texture.viewNumSlices = 1;
        tex->texture.compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);

        GX2CalcSurfaceSizeAndAlignment(&tex->texture.surface);
        GX2InitTextureRegs(&tex->texture);

        tex->texture.surface.image = memalign(tex->texture.surface.alignment, tex->texture.surface.imageSize);
        if (!tex->texture.surface.image) {
            sysFatalError("Out of memory for a %ux%u texture.", (unsigned)width, (unsigned)height);
        }
    }

    // rows in the GPU's copy are `pitch` pixels apart, which can be more than the width
    uint8_t* dst = (uint8_t*)tex->texture.surface.image;
    const uint32_t dst_stride = tex->texture.surface.pitch * 4;
    for (uint32_t y = 0; y < height; ++y) {
        memcpy(dst + y * dst_stride, rgba32_buf + y * width * 4, width * 4);
    }

    // flush the CPU cache so the GPU sees the new pixels
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, tex->texture.surface.image, tex->texture.surface.imageSize);
    tex->uploaded = true;
}

static GX2TexClampMode gfx_cm_to_gx2(uint32_t val) {
    switch (val) {
        case G_TX_NOMIRROR | G_TX_CLAMP:
            return GX2_TEX_CLAMP_MODE_CLAMP;
        case G_TX_MIRROR | G_TX_WRAP:
            return GX2_TEX_CLAMP_MODE_MIRROR;
        case G_TX_MIRROR | G_TX_CLAMP:
            return GX2_TEX_CLAMP_MODE_MIRROR_ONCE;
        case G_TX_NOMIRROR | G_TX_WRAP:
            return GX2_TEX_CLAMP_MODE_WRAP;
    }
    return GX2_TEX_CLAMP_MODE_WRAP;
}

static void gfx_gx2_set_sampler_parameters(int tile, bool linear_filter, uint32_t cms, uint32_t cmt, bool mipmaps) {
    struct Texture* tex = bound_textures[tile];
    current_tile = tile;
    if (!tex) {
        return;
    }

    // three-point filtering isn't done yet; it gets plain bilinear filtering
    const bool linear = linear_filter && current_filter_mode != FILTER_NONE;
    GX2InitSampler(&tex->sampler, GX2_TEX_CLAMP_MODE_CLAMP,
                   linear ? GX2_TEX_XY_FILTER_MODE_LINEAR : GX2_TEX_XY_FILTER_MODE_POINT);
    GX2InitSamplerClamping(&tex->sampler, gfx_cm_to_gx2(cms), gfx_cm_to_gx2(cmt), GX2_TEX_CLAMP_MODE_WRAP);
}

static void gfx_gx2_set_depth_mode(bool depth_test, bool depth_update, bool depth_compare, bool depth_source_prim,
                                   uint16_t zmode) {
    last_depth = { depth_test, depth_update, depth_compare, depth_source_prim, zmode };

    // the same rules as gfx_opengl.cpp
    GX2CompareFunction func = GX2_COMPARE_FUNC_ALWAYS;
    bool decal = false;

    if (depth_test && depth_compare) {
        switch (zmode) {
            case ZMODE_OPA:
            case ZMODE_XLU:
                func = depth_source_prim ? GX2_COMPARE_FUNC_LEQUAL : GX2_COMPARE_FUNC_LESS;
                break;
            case ZMODE_DEC:
                func = GX2_COMPARE_FUNC_LEQUAL;
                decal = true;
                break;
            case ZMODE_INTER:
            default:
                func = GX2_COMPARE_FUNC_LEQUAL;
                break;
        }
    }

    GX2SetDepthOnlyControl(depth_test, depth_test && depth_update, func);

    // decals (bullet holes, shadows, posters) are pulled slightly towards the
    // camera so they don't flicker against the wall they're on
    if (decal) {
        GX2SetPolygonOffset(-2.0f, -2.0f, -2.0f, -2.0f, 0.0f);
    } else {
        GX2SetPolygonOffset(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    }
    GX2SetPolygonControl(GX2_FRONT_FACE_CCW, FALSE, FALSE, FALSE, GX2_POLYGON_MODE_TRIANGLE,
                         GX2_POLYGON_MODE_TRIANGLE, decal, decal, FALSE);
}

static void gfx_gx2_apply_viewport(void) {
    GX2SetViewport(viewport[0], viewport[1], viewport[2], viewport[3], depth_near, depth_far);
}

static void gfx_gx2_set_depth_range(float znear, float zfar) {
    depth_near = znear;
    depth_far = zfar;
    gfx_gx2_apply_viewport();
}

static uint32_t current_fb_height(void) {
    return current_framebuffer ? current_framebuffer->color_buffer.surface.height : 1;
}

static void gfx_gx2_set_viewport(int x, int y, int width, int height) {
    // Fast3D counts y from the bottom (OpenGL's way), GX2 from the top
    viewport[0] = x;
    viewport[1] = (int)current_fb_height() - y - height;
    viewport[2] = width;
    viewport[3] = height;
    gfx_gx2_apply_viewport();
}

static void gfx_gx2_set_scissor(int x, int y, int width, int height) {
    last_scissor[0] = x;
    last_scissor[1] = y;
    last_scissor[2] = width;
    last_scissor[3] = height;

    const int fb_w = current_framebuffer ? (int)current_framebuffer->color_buffer.surface.width : 1;
    const int fb_h = (int)current_fb_height();

    // flip y as for the viewport, then keep the rectangle inside the buffer
    int x0 = std::max(x, 0);
    int y0 = std::max(fb_h - y - height, 0);
    int x1 = std::min(x + width, fb_w);
    int y1 = std::min(fb_h - y, fb_h);

    if (x1 <= x0 || y1 <= y0) {
        // nothing visible: an empty rectangle lets nothing through
        GX2SetScissor(0, 0, 0, 0);
        return;
    }

    GX2SetScissor(x0, y0, x1 - x0, y1 - y0);
}

static void gfx_gx2_set_use_alpha(bool use_alpha, bool modulate) {
    last_use_alpha = use_alpha;
    last_modulate = modulate;
    GX2SetColorControl(GX2_LOGIC_OP_COPY, use_alpha ? 0xff : 0x00, FALSE, TRUE);
    if (modulate) {
        GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_DST_COLOR, GX2_BLEND_MODE_ZERO,
                           GX2_BLEND_COMBINE_MODE_ADD, FALSE, GX2_BLEND_MODE_ZERO, GX2_BLEND_MODE_ZERO,
                           GX2_BLEND_COMBINE_MODE_ADD);
    } else {
        GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_INV_SRC_ALPHA,
                           GX2_BLEND_COMBINE_MODE_ADD, FALSE, GX2_BLEND_MODE_ZERO, GX2_BLEND_MODE_ZERO,
                           GX2_BLEND_COMBINE_MODE_ADD);
    }
}

static void gfx_gx2_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {
    struct ShaderProgram* prg = current_shader_program;
    if (!prg) {
        return;
    }

    // bind this draw's textures
    for (int i = 0; i < 2; i++) {
        if (prg->used_textures[i]) {
            struct Texture* tex = bound_textures[i];
            if (!tex || !tex->uploaded) {
                // nothing to sample: skip the draw rather than read garbage
                ++stat_skipped;
                return;
            }
            GX2SetPixelTexture(&tex->texture, i);
            GX2SetPixelSampler(&tex->sampler, i);
        }
    }

    if (prg->used_noise && prg->window_params_offset >= 0) {
        const float window_params[4] = { current_noise_scale, (float)frame_count, 0.0f, 0.0f };
        GX2SetPixelUniformReg(prg->window_params_offset, 4, window_params);
    }

    const size_t vbo_size = sizeof(float) * buf_vbo_len;

    if (draw_ptr + vbo_size > draw_buffer + DRAW_BUFFER_SIZE) {
        // out of room this frame: wait for the GPU to use up what's queued
        sysLogPrintf(LOG_WARNING, "GX2: vertex buffer full, waiting for the GPU");
        GX2DrawDone();
        draw_ptr = draw_buffer;
    }

    float* vbo = (float*)draw_ptr;
    draw_ptr += ALIGN_UP(vbo_size, GX2_VERTEX_BUFFER_ALIGNMENT);

    memcpy(vbo, buf_vbo, vbo_size);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, vbo, vbo_size);

    GX2SetAttribBuffer(0, vbo_size, prg->group.stride, vbo);
    GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, 3 * buf_vbo_num_tris, 0, 1);

    ++stat_draws;
    stat_tris += buf_vbo_num_tris;
}

// Sets up `fb`'s colour buffer, and its depth buffer if asked, at the given size.
static bool gfx_gx2_alloc_framebuffer(struct Framebuffer* fb, uint32_t width, uint32_t height, bool has_depth) {
    if (fb->color_buffer.surface.image) {
        GX2DrawDone();
        free(fb->color_buffer.surface.image);
    }
    if (fb->depth_buffer.surface.image) {
        free(fb->depth_buffer.surface.image);
    }
    memset(fb, 0, sizeof(*fb));

    GX2ColorBuffer* cb = &fb->color_buffer;
    cb->surface.use = GX2_SURFACE_USE_TEXTURE_COLOR_BUFFER_TV;
    cb->surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    cb->surface.width = width;
    cb->surface.height = height;
    cb->surface.depth = 1;
    cb->surface.mipLevels = 1;
    cb->surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    cb->surface.aa = GX2_AA_MODE1X;
    cb->surface.tileMode = GX2_TILE_MODE_DEFAULT;
    cb->viewNumSlices = 1;
    GX2CalcSurfaceSizeAndAlignment(&cb->surface);
    GX2InitColorBufferRegs(cb);

    // For now everything lives in MEM2, the Wii U's main memory. MEM1, the
    // small fast memory next to the GPU, belongs to SDL here; moving the
    // framebuffers there is a phase 6 (speed) job.
    cb->surface.image = memalign(cb->surface.alignment, cb->surface.imageSize);
    if (!cb->surface.image) {
        return false;
    }
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU, cb->surface.image, cb->surface.imageSize);

    if (has_depth) {
        GX2DepthBuffer* db = &fb->depth_buffer;
        db->surface.use = GX2_SURFACE_USE_DEPTH_BUFFER;
        db->surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
        db->surface.width = width;
        db->surface.height = height;
        db->surface.depth = 1;
        db->surface.mipLevels = 1;
        db->surface.format = GX2_SURFACE_FORMAT_FLOAT_R32;
        db->surface.aa = GX2_AA_MODE1X;
        db->surface.tileMode = GX2_TILE_MODE_DEFAULT;
        db->viewNumSlices = 1;
        db->depthClear = 1.0f;
        GX2CalcSurfaceSizeAndAlignment(&db->surface);
        GX2InitDepthBufferRegs(db);

        db->surface.image = memalign(db->surface.alignment, db->surface.imageSize);
        if (!db->surface.image) {
            return false;
        }
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU, db->surface.image, db->surface.imageSize);
        fb->has_depth = true;
    }

    // a texture view of the colour buffer, for effects that draw one buffer into another
    GX2Texture* t = &fb->tex.texture;
    t->surface = cb->surface;
    t->surface.use = GX2_SURFACE_USE_TEXTURE;
    t->viewNumMips = 1;
    t->viewNumSlices = 1;
    t->compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
    GX2InitTextureRegs(t);
    GX2InitSampler(&fb->tex.sampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_LINEAR);
    fb->tex.uploaded = true;

    return true;
}

static void gfx_gx2_init(void) {
    // The window manager has already started GX2 (through SDL). This back
    // end keeps all of its GPU settings in its own "context state", so they
    // don't mix with SDL's: switching context swaps every setting at once.
    context_state = (GX2ContextState*)memalign(GX2_CONTEXT_STATE_ALIGNMENT, sizeof(GX2ContextState));
    draw_buffer = (uint8_t*)memalign(GX2_VERTEX_BUFFER_ALIGNMENT, DRAW_BUFFER_SIZE);
    if (!context_state || !draw_buffer) {
        sysFatalError("Not enough memory to start the GX2 renderer.");
    }
    draw_ptr = draw_buffer;

    GX2SetupContextStateEx(context_state, TRUE);
    GX2SetContextState(context_state);

    // the generated shaders keep their constants in registers
    GX2SetShaderModeEx(GX2_SHADER_MODE_UNIFORM_REGISTER, 48, 64, 0, 0, 200, 192);

    // depth outside the near/far range is clamped, not cut off (like OpenGL's GL_DEPTH_CLAMP)
    GX2SetRasterizerClipControl(TRUE, FALSE);

    gfx_gx2_set_depth_mode(true, true, true, false, ZMODE_OPA);
    gfx_gx2_set_use_alpha(false, false);

    sysLogPrintf(LOG_NOTE, "GX2: renderer ready");
}

static void gfx_gx2_on_resize(void) {
}

static void gfx_gx2_start_frame(void) {
    // SDL may have drawn something (with its own settings) since our last frame
    GX2SetContextState(context_state);
    if (current_framebuffer) {
        GX2SetColorBuffer(&current_framebuffer->color_buffer, GX2_RENDER_TARGET_0);
        if (current_framebuffer->has_depth) {
            GX2SetDepthBuffer(&current_framebuffer->depth_buffer);
        }
    }
    frame_count++;
}

// Draws the overlay over the finished picture, then restores Fast3D's settings.
static void gfx_gx2_draw_overlay(void) {
    if (!overlay_prg_ready) {
        // one fixed combiner setup: texture colour times vertex colour, with alpha
        // (combiner slots: TEXEL0, 0, INPUT_1, 0 for colour and for alpha)
        struct CCFeatures cc;
        const uint64_t id0 = 0x0108 | (0x0108ull << 16);
        gfx_cc_get_features(id0, SHADER_OPT_ALPHA, &cc);
        if (gx2GenerateShaderGroup(&overlay_prg.group, &cc) != 0) {
            overlay_fn = nullptr;
            return;
        }
        overlay_prg.num_inputs = cc.num_inputs;
        overlay_prg.used_textures[0] = true;
        overlay_prg_ready = true;
    }

    GX2SetColorBuffer(&main_framebuffer.color_buffer, GX2_RENDER_TARGET_0);
    GX2SetDepthBuffer(&main_framebuffer.depth_buffer);
    GX2SetViewport(0.0f, 0.0f, main_framebuffer.color_buffer.surface.width,
                   main_framebuffer.color_buffer.surface.height, 0.0f, 1.0f);
    GX2SetScissor(0, 0, main_framebuffer.color_buffer.surface.width, main_framebuffer.color_buffer.surface.height);
    GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS);
    GX2SetPolygonOffset(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    GX2SetPolygonControl(GX2_FRONT_FACE_CCW, FALSE, FALSE, FALSE, GX2_POLYGON_MODE_TRIANGLE,
                         GX2_POLYGON_MODE_TRIANGLE, FALSE, FALSE, FALSE);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0xff, FALSE, TRUE);
    GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_INV_SRC_ALPHA,
                       GX2_BLEND_COMBINE_MODE_ADD, FALSE, GX2_BLEND_MODE_ZERO, GX2_BLEND_MODE_ZERO,
                       GX2_BLEND_COMBINE_MODE_ADD);
    GX2SetFetchShader(&overlay_prg.group.fetchShader);
    GX2SetVertexShader(&overlay_prg.group.vertexShader);
    GX2SetPixelShader(&overlay_prg.group.pixelShader);

    overlay_fn();

    // put back what Fast3D last set
    gfx_gx2_set_depth_mode(last_depth.test, last_depth.update, last_depth.compare, last_depth.source_prim,
                           last_depth.zmode);
    gfx_gx2_set_use_alpha(last_use_alpha, last_modulate);
    if (current_framebuffer) {
        GX2SetColorBuffer(&current_framebuffer->color_buffer, GX2_RENDER_TARGET_0);
        if (current_framebuffer->has_depth) {
            GX2SetDepthBuffer(&current_framebuffer->depth_buffer);
        }
    }
    gfx_gx2_apply_viewport();
    gfx_gx2_set_scissor(last_scissor[0], last_scissor[1], last_scissor[2], last_scissor[3]);
    if (current_shader_program) {
        gfx_gx2_load_shader(current_shader_program);
    }
}

static void gfx_gx2_end_frame(void) {
    if (overlay_fn) {
        gfx_gx2_draw_overlay();
    }

    // Copy the finished picture to the TV's and the GamePad's screen buffers.
    // GX2 scales it to each screen's size. The window manager then shows it.
    GX2CopyColorBufferToScanBuffer(&main_framebuffer.color_buffer, GX2_SCAN_TARGET_TV);
    GX2CopyColorBufferToScanBuffer(&main_framebuffer.color_buffer, GX2_SCAN_TARGET_DRC);

    // the copies change GPU settings; put ours back
    GX2SetContextState(context_state);

    // every 10 seconds or so (and on the first frame), what the last frame drew
    if (frame_count == 1 || frame_count % 600 == 0) {
        sysLogPrintf(LOG_NOTE, "GX2: frame %u: %u draws, %u triangles, %u skipped, %u KB of vertices",
                     (unsigned)frame_count, (unsigned)stat_draws, (unsigned)stat_tris, (unsigned)stat_skipped,
                     (unsigned)((draw_ptr - draw_buffer) / 1024));
    }
    stat_draws = stat_tris = stat_skipped = 0;

    draw_ptr = draw_buffer;
}

static void gfx_gx2_finish_render(void) {
}

static int gfx_gx2_create_framebuffer(void) {
    struct Framebuffer* fb = (struct Framebuffer*)calloc(1, sizeof(struct Framebuffer));
    // the id is the pointer; 0 means the main framebuffer
    return (int)(uintptr_t)fb;
}

static struct Framebuffer* fb_from_id(int fb_id) {
    return fb_id == 0 ? &main_framebuffer : (struct Framebuffer*)(uintptr_t)fb_id;
}

static void gfx_gx2_update_framebuffer_parameters(int fb_id, uint32_t width, uint32_t height, uint32_t msaa_level,
                                                  bool opengl_invert_y, bool render_target, bool has_depth_buffer,
                                                  bool can_extract_depth) {
    struct Framebuffer* fb = fb_from_id(fb_id);
    if (!fb) {
        return;
    }

    width = std::max(width, 1U);
    height = std::max(height, 1U);

    // the main framebuffer always has a depth buffer
    if (fb_id == 0) {
        has_depth_buffer = true;
    }

    if (fb->color_buffer.surface.image && fb->color_buffer.surface.width == width &&
        fb->color_buffer.surface.height == height && (fb->has_depth || !has_depth_buffer)) {
        return;
    }

    if (!gfx_gx2_alloc_framebuffer(fb, width, height, has_depth_buffer)) {
        sysFatalError("Not enough memory for a %ux%u framebuffer.", (unsigned)width, (unsigned)height);
    }

    if (fb == current_framebuffer) {
        GX2SetColorBuffer(&fb->color_buffer, GX2_RENDER_TARGET_0);
        if (fb->has_depth) {
            GX2SetDepthBuffer(&fb->depth_buffer);
        }
    }
}

static bool gfx_gx2_start_draw_to_framebuffer(int fb_id, float noise_scale) {
    struct Framebuffer* fb = fb_from_id(fb_id);
    if (!fb || !fb->color_buffer.surface.image) {
        return false;
    }

    if (noise_scale != 0.0f) {
        current_noise_scale = 1.0f / noise_scale;
    }

    GX2SetColorBuffer(&fb->color_buffer, GX2_RENDER_TARGET_0);
    if (fb->has_depth) {
        GX2SetDepthBuffer(&fb->depth_buffer);
    }
    current_framebuffer = fb;
    return true;
}

static void gfx_gx2_copy_framebuffer(int fb_dst, int fb_src, int left, int top, bool flip_y, bool use_back) {
    // TODO (phase 4, stage 4): used by effects that read the screen back,
    // such as the blurred menu backgrounds
}

static void gfx_gx2_clear_framebuffer(bool clear_color, bool clear_depth) {
    struct Framebuffer* fb = current_framebuffer;
    if (!fb) {
        return;
    }

    if (clear_color) {
        GX2ClearColor(&fb->color_buffer, 0.0f, 0.0f, 0.0f, 1.0f);
    }
    if (clear_depth && fb->has_depth) {
        GX2ClearDepthStencilEx(&fb->depth_buffer, fb->depth_buffer.depthClear, fb->depth_buffer.stencilClear,
                               GX2_CLEAR_FLAGS_DEPTH);
    }

    // clearing changes GPU settings; put ours back
    GX2SetContextState(context_state);
}

static void gfx_gx2_resolve_msaa_color_buffer(int fb_id_target, int fb_id_source) {
    // no multisampling on the Wii U build (yet)
}

static void* gfx_gx2_get_framebuffer_texture_id(int fb_id) {
    struct Framebuffer* fb = fb_from_id(fb_id);
    return fb ? &fb->tex : nullptr;
}

static void gfx_gx2_select_texture_fb(int fb_id) {
    struct Framebuffer* fb = fb_from_id(fb_id);
    if (fb && fb->color_buffer.surface.image) {
        bound_textures[0] = &fb->tex;
    }
}

static void gfx_gx2_set_texture_filter(enum FilteringMode mode) {
    current_filter_mode = mode;
}

static enum FilteringMode gfx_gx2_get_texture_filter(void) {
    return current_filter_mode;
}

static void gfx_gx2_set_mipmap_filter(enum MipmapFilteringMode mode) {
}

static void gfx_gx2_set_anisotropy_level(int level) {
}

static int gfx_gx2_get_max_anisotropy_level(void) {
    return 1;
}

extern "C" void gfx_gx2_set_overlay(GfxGx2OverlayFn fn) {
    overlay_fn = fn;
}

extern "C" void gfx_gx2_get_stats(struct GfxGx2Stats* out) {
    out->draws = stat_draws;
    out->triangles = stat_tris;
    out->skipped = stat_skipped;
    out->vertex_bytes = (uint32_t)(draw_ptr - draw_buffer);
    out->shaders = (uint32_t)shader_program_pool.size();
}

extern "C" uint32_t gfx_gx2_overlay_texture(const uint8_t* rgba, uint32_t width, uint32_t height) {
    const uint32_t id = gfx_gx2_new_texture();
    struct Texture* saved = bound_textures[0];
    const int saved_tile = current_tile;

    bound_textures[0] = (struct Texture*)(uintptr_t)id;
    current_tile = 0;
    gfx_gx2_upload_texture(rgba, width, height, false);
    GX2InitSampler(&bound_textures[0]->sampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_POINT);

    bound_textures[0] = saved;
    current_tile = saved_tile;
    return id;
}

extern "C" void gfx_gx2_overlay_quads(uint32_t texture_id, const struct GfxGx2Quad* quads, size_t count) {
    struct Texture* tex = (struct Texture*)(uintptr_t)texture_id;
    if (!tex || !tex->uploaded || count == 0 || !overlay_prg_ready) {
        return;
    }

    // per vertex: position (4 floats), texture coordinate (2), colour (4)
    const size_t floats_per_vertex = 10;
    const size_t size = count * 6 * floats_per_vertex * sizeof(float);
    if (draw_ptr + size > draw_buffer + DRAW_BUFFER_SIZE) {
        return;
    }
    float* vbo = (float*)draw_ptr;
    draw_ptr += ALIGN_UP(size, GX2_VERTEX_BUFFER_ALIGNMENT);

    // pixels to the GPU's -1..1 coordinates, y pointing up
    const float sx = 2.0f / main_framebuffer.color_buffer.surface.width;
    const float sy = 2.0f / main_framebuffer.color_buffer.surface.height;

    float* v = vbo;
    for (size_t i = 0; i < count; i++) {
        const struct GfxGx2Quad* q = &quads[i];
        const float x0 = q->x0 * sx - 1.0f, x1 = q->x1 * sx - 1.0f;
        const float y0 = 1.0f - q->y0 * sy, y1 = 1.0f - q->y1 * sy;
        const float r = ((q->rgba >> 24) & 0xff) / 255.0f;
        const float g = ((q->rgba >> 16) & 0xff) / 255.0f;
        const float b = ((q->rgba >> 8) & 0xff) / 255.0f;
        const float a = (q->rgba & 0xff) / 255.0f;
        const float corners[6][4] = {
            { x0, y0, q->u0, q->v0 }, { x1, y0, q->u1, q->v0 }, { x0, y1, q->u0, q->v1 },
            { x1, y0, q->u1, q->v0 }, { x1, y1, q->u1, q->v1 }, { x0, y1, q->u0, q->v1 },
        };
        for (int c = 0; c < 6; c++) {
            *v++ = corners[c][0];
            *v++ = corners[c][1];
            *v++ = 0.0f;
            *v++ = 1.0f;
            *v++ = corners[c][2];
            *v++ = corners[c][3];
            *v++ = r;
            *v++ = g;
            *v++ = b;
            *v++ = a;
        }
    }

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, vbo, size);
    GX2SetPixelTexture(&tex->texture, 0);
    GX2SetPixelSampler(&tex->sampler, 0);
    GX2SetAttribBuffer(0, size, overlay_prg.group.stride, vbo);
    GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, count * 6, 0, 1);
}

struct GfxRenderingAPI gfx_gx2_api = {
    gfx_gx2_get_name,
    gfx_gx2_get_max_texture_size,
    gfx_gx2_get_clip_parameters,
    gfx_gx2_unload_shader,
    gfx_gx2_load_shader,
    gfx_gx2_create_and_load_new_shader,
    gfx_gx2_lookup_shader,
    gfx_gx2_shader_get_info,
    gfx_gx2_clear_shaders,
    gfx_gx2_new_texture,
    gfx_gx2_select_texture,
    gfx_gx2_upload_texture,
    gfx_gx2_set_sampler_parameters,
    gfx_gx2_set_depth_mode,
    gfx_gx2_set_depth_range,
    gfx_gx2_set_viewport,
    gfx_gx2_set_scissor,
    gfx_gx2_set_use_alpha,
    gfx_gx2_draw_triangles,
    gfx_gx2_init,
    gfx_gx2_on_resize,
    gfx_gx2_start_frame,
    gfx_gx2_end_frame,
    gfx_gx2_finish_render,
    gfx_gx2_create_framebuffer,
    gfx_gx2_update_framebuffer_parameters,
    gfx_gx2_start_draw_to_framebuffer,
    gfx_gx2_copy_framebuffer,
    gfx_gx2_clear_framebuffer,
    gfx_gx2_resolve_msaa_color_buffer,
    gfx_gx2_get_framebuffer_texture_id,
    gfx_gx2_select_texture_fb,
    gfx_gx2_delete_texture,
    gfx_gx2_set_texture_filter,
    gfx_gx2_get_texture_filter,
    gfx_gx2_set_mipmap_filter,
    gfx_gx2_set_anisotropy_level,
    gfx_gx2_get_max_anisotropy_level,
};
