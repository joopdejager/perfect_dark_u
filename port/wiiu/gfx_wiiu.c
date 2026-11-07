// Fast3D window manager for the Wii U.
//
// This is the Wii U's counterpart of port/fast3d/gfx_sdl2.cpp. It owns the
// screen and handles system events (such as the HOME menu) and frame timing.
//
// For now (phase 3) the screen is SDL's own GX2-backed 2D renderer, and each
// frame it shows a status page instead of the game's picture, because the
// GX2 renderer that would draw the game doesn't exist yet. Phase 4 replaces
// this with a screen set up for that renderer.

#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>

#include "platform.h"
#include "system.h"

#include "../fast3d/gfx_window_manager_api.h"
#include "wiiu.h"
#include "wiiu_screen.h"

static SDL_Renderer *ren;
static int targetFps = 60;
static int swapInterval = 1;
static Uint64 perfFreq;
static Uint64 startCounter;
static unsigned int frameCount;

static void wiiu_init(const struct GfxWindowInitSettings *set)
{
	ren = wiiuScreenGetRenderer();
	if (!ren) {
		sysFatalError("Could not start video:\n%s", SDL_GetError());
	}
	perfFreq = SDL_GetPerformanceFrequency();
	startCounter = SDL_GetPerformanceCounter();
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
	return true;
}

static void wiiu_draw_status(void)
{
	char line[256];
	const double secs = (double)(SDL_GetPerformanceCounter() - startCounter) / (double)perfFreq;

	SDL_SetRenderDrawColor(ren, 0x08, 0x08, 0x10, 0xff);
	SDL_RenderClear(ren);

	int y = 48;
	y = wiiuScreenText(48, y, 4, 0xff4040, "Perfect Dark");
	y += 8;
	y = wiiuScreenText(48, y, 2, 0xffffff,
		"The game is running natively on the Wii U.\n"
		"The renderer that draws it is not written yet (phase 4),\n"
		"so this status page is shown instead.");
	y += 16;
	snprintf(line, sizeof(line), "Frames: %u   Time: %.0f s", frameCount, secs);
	y = wiiuScreenText(48, y, 2, 0x80ff80, line);
	y += 8;
	y = wiiuScreenText(48, y, 2, 0xc0c0c0, "Press HOME to quit.");
	y += 24;
	wiiuScreenLog(y, 1);
}

static void wiiu_swap_buffers_begin(void)
{
	++frameCount;
	wiiu_draw_status();
	// waits for the next vertical blank, which paces the game at 60 Hz
	SDL_RenderPresent(ren);
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
