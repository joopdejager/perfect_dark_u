// GX2 rendering back end for Fast3D (Wii U).
//
// PLACEHOLDER. This is where phase 4 turns Fast3D's draw calls into GX2
// calls, the way gfx_opengl.cpp turns them into OpenGL calls. For now every
// function accepts its call and draws nothing, so the rest of the game can be
// built and run on the Wii U before the renderer exists. The functions
// return plausible values (texture and framebuffer ids, a shader object) so
// Fast3D keeps working normally around them.

#include <stdint.h>
#include <stdlib.h>
#include <map>
#include <utility>

#include "gfx_rendering_api.h"
#include "gfx_gx2.h"

struct ShaderProgram {
    uint64_t shader_id0;
    uint32_t shader_id1;
};

static std::map<std::pair<uint64_t, uint32_t>, ShaderProgram*> shaders;
static uint32_t next_texture_id = 1;
static int next_framebuffer_id = 1;
static enum FilteringMode texture_filter = FILTER_LINEAR;

static const char* gfx_gx2_get_name(void) {
    return "GX2 (placeholder)";
}

static int gfx_gx2_get_max_texture_size(void) {
    return 8192;
}

static struct GfxClipParameters gfx_gx2_get_clip_parameters(void) {
    // GX2 can be set up either way; phase 4 decides
    return { false, false };
}

static void gfx_gx2_unload_shader(struct ShaderProgram* old_prg) {
}

static void gfx_gx2_load_shader(struct ShaderProgram* new_prg) {
}

static struct ShaderProgram* gfx_gx2_create_and_load_new_shader(uint64_t shader_id0, uint32_t shader_id1) {
    ShaderProgram* prg = new ShaderProgram { shader_id0, shader_id1 };
    shaders[{ shader_id0, shader_id1 }] = prg;
    return prg;
}

static struct ShaderProgram* gfx_gx2_lookup_shader(uint64_t shader_id0, uint32_t shader_id1) {
    auto it = shaders.find({ shader_id0, shader_id1 });
    return it != shaders.end() ? it->second : nullptr;
}

static void gfx_gx2_shader_get_info(struct ShaderProgram* prg, uint8_t* num_inputs, bool used_textures[2]) {
    *num_inputs = 0;
    used_textures[0] = false;
    used_textures[1] = false;
}

static void gfx_gx2_clear_shaders(void) {
    for (auto& it : shaders) {
        delete it.second;
    }
    shaders.clear();
}

static uint32_t gfx_gx2_new_texture(void) {
    return next_texture_id++;
}

static void gfx_gx2_select_texture(int tile, uint32_t texture_id, bool linear_filter) {
}

static void gfx_gx2_upload_texture(const uint8_t* rgba32_buf, uint32_t width, uint32_t height, bool gen_mipmaps) {
}

static void gfx_gx2_set_sampler_parameters(int sampler, bool linear_filter, uint32_t cms, uint32_t cmt, bool mipmaps) {
}

static void gfx_gx2_set_depth_mode(bool depth_test, bool depth_update, bool depth_compare, bool depth_source_prim,
                                   uint16_t zmode) {
}

static void gfx_gx2_set_depth_range(float znear, float zfar) {
}

static void gfx_gx2_set_viewport(int x, int y, int width, int height) {
}

static void gfx_gx2_set_scissor(int x, int y, int width, int height) {
}

static void gfx_gx2_set_use_alpha(bool use_alpha, bool modulate) {
}

static void gfx_gx2_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {
}

static void gfx_gx2_init(void) {
}

static void gfx_gx2_on_resize(void) {
}

static void gfx_gx2_start_frame(void) {
}

static void gfx_gx2_end_frame(void) {
}

static void gfx_gx2_finish_render(void) {
}

static int gfx_gx2_create_framebuffer(void) {
    return next_framebuffer_id++;
}

static void gfx_gx2_update_framebuffer_parameters(int fb_id, uint32_t width, uint32_t height, uint32_t msaa_level,
                                                  bool opengl_invert_y, bool render_target, bool has_depth_buffer,
                                                  bool can_extract_depth) {
}

static bool gfx_gx2_start_draw_to_framebuffer(int fb_id, float noise_scale) {
    return true;
}

static void gfx_gx2_copy_framebuffer(int fb_dst, int fb_src, int left, int top, bool flip_y, bool use_back) {
}

static void gfx_gx2_clear_framebuffer(bool clear_color, bool clear_depth) {
}

static void gfx_gx2_resolve_msaa_color_buffer(int fb_id_target, int fb_id_source) {
}

static void* gfx_gx2_get_framebuffer_texture_id(int fb_id) {
    return (void*)(uintptr_t)fb_id;
}

static void gfx_gx2_select_texture_fb(int fb_id) {
}

static void gfx_gx2_delete_texture(uint32_t texID) {
}

static void gfx_gx2_set_texture_filter(enum FilteringMode mode) {
    texture_filter = mode;
}

static enum FilteringMode gfx_gx2_get_texture_filter(void) {
    return texture_filter;
}

static void gfx_gx2_set_mipmap_filter(enum MipmapFilteringMode mode) {
}

static void gfx_gx2_set_anisotropy_level(int level) {
}

static int gfx_gx2_get_max_anisotropy_level(void) {
    return 1;
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
