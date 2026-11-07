// Fast3D window manager for the Wii U.
//
// This is the Wii U's counterpart of port/fast3d/gfx_sdl2.cpp. It owns the
// screen and handles system events (such as the HOME menu) and frame timing.
//
// SDL's Wii U video driver does the system side of the screen: it starts
// GX2, sets up the TV and GamePad screen buffers, and handles the HOME menu
// (which takes the screen away and gives it back). We keep all of that and
// only add frame pacing and showing frames. The game's picture itself is
// drawn by port/fast3d/gfx_gx2.cpp, which also copies it into SDL's screen
// buffers at the end of each frame.
//
// SDL's own 2D renderer is still used for the startup page and the error
// page (port/wiiu/wiiu_screen.c). Each side keeps its GPU settings in its
// own context state, so they don't disturb each other.

#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>
#include <gx2/event.h>
#include <gx2/swap.h>
#include <gx2/display.h>
#include <gx2/state.h>

#include "platform.h"
#include "system.h"

#include "../fast3d/gfx_window_manager_api.h"
#include "wiiu.h"
#include "wiiu_screen.h"

static SDL_Renderer *ren;
static int targetFps = 60;
static int swapInterval = 1;
static Uint64 perfFreq;
static unsigned int frameCount;

static void wiiu_init(const struct GfxWindowInitSettings *set)
{
	ren = wiiuScreenGetRenderer();
	if (!ren) {
		sysFatalError("Could not start video:\n%s", SDL_GetError());
	}
	sysLogPrintf(LOG_NOTE, "wiiu: screen ready");
	perfFreq = SDL_GetPerformanceFrequency();
	// show each frame at the next vertical blank (60 Hz)
	GX2SetSwapInterval(swapInterval);
}

static void wiiu_close(void)
{
}

static int wiiu_get_display_mode(int modenum, int *out_w, int *out_h)
{
	if (modenum != 0) {
		return 0;
	}
	*out_w = WIIU_SCREEN_W;
	*out_h = WIIU_SCREEN_H;
	return 1;
}

static int wiiu_get_current_display_mode(int *out_w, int *out_h)
{
	return wiiu_get_display_mode(0, out_w, out_h);
}

static int wiiu_get_num_display_modes(void)
{
	return 1;
}

// The Wii U is always "fullscreen" at one fixed size, so the window controls
// are no-ops.
static int32_t wiiu_get_fullscreen_state(void) { return 1; }
static void wiiu_set_fullscreen_changed_callback(void (*cb)(bool)) { }
static void wiiu_set_fullscreen(bool enable) { }
static void wiiu_set_fullscreen_exclusive(bool exc) { }
static void wiiu_set_fullscreen_flag(int32_t mode) { }
static int32_t wiiu_get_fullscreen_flag_mode(void) { return 0; }
static int32_t wiiu_get_maximized_state(void) { return 0; }
static void wiiu_set_maximize(bool enable) { }
static void wiiu_set_cursor_visibility(bool visible) { }
static void wiiu_set_closest_resolution(int32_t w, int32_t h, bool center) { }
static void wiiu_set_dimensions(uint32_t w, uint32_t h, int32_t x, int32_t y) { }
static void wiiu_set_window_title(const char *title) { }

static void wiiu_get_active_window_refresh_rate(uint32_t *refresh_rate)
{
	*refresh_rate = 60;
}

static void wiiu_get_dimensions(uint32_t *w, uint32_t *h, int32_t *x, int32_t *y)
{
	*w = WIIU_SCREEN_W;
	*h = WIIU_SCREEN_H;
	*x = 0;
	*y = 0;
}

static void wiiu_get_centered_positions(int32_t w, int32_t h, int32_t *x, int32_t *y)
{
	*x = (WIIU_SCREEN_W - w) / 2;
	*y = (WIIU_SCREEN_H - h) / 2;
}

static void wiiu_handle_events(void)
{
	// Controller input reaches port/src/input.c through an SDL event watch,
	// which sees every event as it's queued. Here we only empty the queue and
	// look for the request to quit, which SDL sends when the user picks
	// "Close" in the HOME menu.
	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {
		if (ev.type == SDL_QUIT) {
			exit(0);
		}
	}
}

static bool wiiu_start_frame(void)
{
	// Wait until the GPU has shown the previous frame. That paces the game to
	// the screen, and it means the GPU is done with the previous frame's
	// vertex data, so gfx_gx2.cpp can reuse that memory. If the GPU takes
	// longer than 10 screen refreshes, skip this frame.
	for (int waits = 0; waits < 10; ++waits) {
		uint32_t swapCount, flipCount;
		OSTime lastFlip, lastVsync;
		GX2GetSwapStatus(&swapCount, &flipCount, &lastFlip, &lastVsync);
		if (flipCount >= swapCount) {
			return true;
		}
		GX2WaitForVsync();
	}
	return false;
}

static void wiiu_swap_buffers_begin(void)
{
	wiiuWatchdogBeat();
	if (++frameCount == 1) {
		// from here on the game draws the screen, not the startup page
		wiiuScreenBootDone();
		sysLogPrintf(LOG_NOTE, "wiiu: first frame");
	}

	// gfx_gx2.cpp has copied the frame into the screen buffers; show it
	GX2SwapScanBuffers();
	GX2Flush();
	GX2SetTVEnable(TRUE);
	GX2SetDRCEnable(TRUE);
}

static void wiiu_swap_buffers_end(void)
{
}

static double wiiu_get_time(void)
{
	return (double)SDL_GetPerformanceCounter() / (double)perfFreq;
}

static int32_t wiiu_get_target_fps(void)
{
	return targetFps;
}

static void wiiu_set_target_fps(int fps)
{
	targetFps = fps;
}

static bool wiiu_can_disable_vsync(void)
{
	return false;
}

static void *wiiu_get_window_handle(void)
{
	return wiiuScreenGetWindow();
}

static int wiiu_get_swap_interval(void)
{
	return swapInterval;
}

static bool wiiu_set_swap_interval(int interval)
{
	// vsync is always on
	return interval == 1;
}

struct GfxWindowManagerAPI gfx_wiiu = {
	wiiu_init,
	wiiu_close,
	wiiu_get_display_mode,
	wiiu_get_current_display_mode,
	wiiu_get_num_display_modes,
	wiiu_get_fullscreen_state,
	wiiu_set_fullscreen_changed_callback,
	wiiu_set_fullscreen,
	wiiu_set_fullscreen_exclusive,
	wiiu_set_fullscreen_flag,
	wiiu_get_fullscreen_flag_mode,
	wiiu_get_maximized_state,
	wiiu_set_maximize,
	wiiu_get_active_window_refresh_rate,
	wiiu_set_cursor_visibility,
	wiiu_set_closest_resolution,
	wiiu_set_dimensions,
	wiiu_get_dimensions,
	wiiu_get_centered_positions,
	wiiu_handle_events,
	wiiu_start_frame,
	wiiu_swap_buffers_begin,
	wiiu_swap_buffers_end,
	wiiu_get_time,
	wiiu_get_target_fps,
	wiiu_set_target_fps,
	wiiu_can_disable_vsync,
	wiiu_get_window_handle,
	wiiu_set_window_title,
	wiiu_get_swap_interval,
	wiiu_set_swap_interval,
};
