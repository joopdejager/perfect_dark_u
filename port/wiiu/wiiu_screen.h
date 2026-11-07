#ifndef _IN_WIIU_SCREEN_H
#define _IN_WIIU_SCREEN_H

// A minimal text screen for the Wii U, drawn with SDL's built-in GX2 2D
// renderer. It needs no GX2 code of our own, so it works before the real
// renderer (phase 4) exists. Used for error messages and the status screen.

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIIU_SCREEN_W 1280
#define WIIU_SCREEN_H 720

// Size of one character cell at scale 1, in pixels.
#define WIIU_GLYPH_SIZE 8

// Returns the shared window renderer, creating the SDL window on first use.
// Returns NULL if SDL video could not be started.
SDL_Renderer *wiiuScreenGetRenderer(void);
SDL_Window *wiiuScreenGetWindow(void);

// Draws text at (x, y), scaled up `scale` times. Handles '\n'. Lines longer
// than the screen are wrapped. Returns the y coordinate below the last line.
int wiiuScreenText(int x, int y, int scale, Uint32 rgb, const char *text);

// Draws the most recent log lines in the area starting at y.
void wiiuScreenLog(int y, int scale);

// Remembers a log line for wiiuScreenLog(). Called by sysLogPrintf().
void wiiuScreenAddLogLine(const char *prefix, const char *msg);

// Closes the window and SDL. Safe to call more than once.
void wiiuScreenShutdown(void);

#ifdef __cplusplus
}
#endif

#endif
