#include <stdio.h>
#include <string.h>
#include <SDL.h>

#include "wiiu_screen.h"

// Public domain 8x8 font. In each byte the lowest bit is the leftmost pixel.
#include "../../third_party/font8x8/font8x8_basic.h"

#define FONT_COLS 16
#define FONT_ROWS 8

#define LOG_LINES 24
#define LOG_LINE_LEN 160

static SDL_Window *wnd;
static SDL_Renderer *ren;
static SDL_Texture *fontTex;

static char logLines[LOG_LINES][LOG_LINE_LEN];
static int logNext;
static int logCount;

static void screenCreateFont(void)
{
	// Put all 128 glyphs into one 128x64 texture: white where the glyph has a
	// pixel, transparent elsewhere. Text is then drawn by copying glyphs out of it.
	static Uint32 pixels[FONT_ROWS * WIIU_GLYPH_SIZE][FONT_COLS * WIIU_GLYPH_SIZE];

	for (int c = 0; c < 128; ++c) {
		const int gx = (c % FONT_COLS) * WIIU_GLYPH_SIZE;
		const int gy = (c / FONT_COLS) * WIIU_GLYPH_SIZE;
		for (int y = 0; y < WIIU_GLYPH_SIZE; ++y) {
			const unsigned char bits = (unsigned char)font8x8_basic[c][y];
			for (int x = 0; x < WIIU_GLYPH_SIZE; ++x) {
				pixels[gy + y][gx + x] = (bits & (1 << x)) ? 0xffffffff : 0;
			}
		}
	}

	fontTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC,
		FONT_COLS * WIIU_GLYPH_SIZE, FONT_ROWS * WIIU_GLYPH_SIZE);
	if (fontTex) {
		SDL_UpdateTexture(fontTex, NULL, pixels, sizeof(pixels[0]));
		SDL_SetTextureBlendMode(fontTex, SDL_BLENDMODE_BLEND);
	}
}

SDL_Renderer *wiiuScreenGetRenderer(void)
{
	if (ren) {
		return ren;
	}

	if (!SDL_WasInit(SDL_INIT_VIDEO) && SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
		fprintf(stderr, "wiiu: SDL video init failed: %s\n", SDL_GetError());
		return NULL;
	}

	// SDL shows this window on both the TV and the GamePad.
	wnd = SDL_CreateWindow("Perfect Dark", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		WIIU_SCREEN_W, WIIU_SCREEN_H, 0);
	if (!wnd) {
		fprintf(stderr, "wiiu: SDL_CreateWindow failed: %s\n", SDL_GetError());
		return NULL;
	}

	ren = SDL_CreateRenderer(wnd, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!ren) {
		fprintf(stderr, "wiiu: SDL_CreateRenderer failed: %s\n", SDL_GetError());
		SDL_DestroyWindow(wnd);
		wnd = NULL;
		return NULL;
	}

	screenCreateFont();

	return ren;
}

SDL_Window *wiiuScreenGetWindow(void)
{
	wiiuScreenGetRenderer();
	return wnd;
}

int wiiuScreenText(int x, int y, int scale, Uint32 rgb, const char *text)
{
	if (!ren || !fontTex || !text) {
		return y;
	}

	const int cell = WIIU_GLYPH_SIZE * scale;
	const int left = x;

	SDL_SetTextureColorMod(fontTex, (rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);

	for (const char *p = text; *p; ++p) {
		const unsigned char c = (unsigned char)*p;

		if (c == '\n' || x + cell > WIIU_SCREEN_W) {
			x = left;
			y += cell + scale * 2;
			if (c == '\n') {
				continue;
			}
		}

		if (c < 128 && c != ' ') {
			const SDL_Rect src = {
				(c % FONT_COLS) * WIIU_GLYPH_SIZE, (c / FONT_COLS) * WIIU_GLYPH_SIZE,
				WIIU_GLYPH_SIZE, WIIU_GLYPH_SIZE
			};
			const SDL_Rect dst = { x, y, cell, cell };
			SDL_RenderCopy(ren, fontTex, &src, &dst);
		}

		x += cell;
	}

	return y + cell + scale * 2;
}

void wiiuScreenLog(int y, int scale)
{
	const int lineH = WIIU_GLYPH_SIZE * scale + scale * 2;
	const int fit = (WIIU_SCREEN_H - y) / lineH;
	int n = logCount < fit ? logCount : fit;

	for (int i = n; i > 0; --i) {
		const char *line = logLines[(logNext - i + LOG_LINES) % LOG_LINES];
		const Uint32 colour = strncmp(line, "ERROR", 5) == 0 ? 0xff6060 :
			(strncmp(line, "WARNING", 7) == 0 ? 0xffd060 : 0xa0a0a0);
		// draw only what fits on one line
		char clipped[LOG_LINE_LEN];
		int maxChars = (WIIU_SCREEN_W - 32) / (WIIU_GLYPH_SIZE * scale);
		if (maxChars > LOG_LINE_LEN - 1) {
			maxChars = LOG_LINE_LEN - 1;
		}
		snprintf(clipped, maxChars + 1, "%s", line);
		wiiuScreenText(16, y, scale, colour, clipped);
		y += lineH;
	}
}

void wiiuScreenAddLogLine(const char *prefix, const char *msg)
{
	// split multi-line messages so each line is stored separately
	const char *start = msg;
	while (start) {
		const char *end = strchr(start, '\n');
		const int len = end ? (int)(end - start) : (int)strlen(start);
		snprintf(logLines[logNext], LOG_LINE_LEN, "%s%.*s", prefix, len, start);
		logNext = (logNext + 1) % LOG_LINES;
		if (logCount < LOG_LINES) {
			++logCount;
		}
		start = end ? end + 1 : NULL;
		prefix = "";
	}
}

void wiiuScreenShutdown(void)
{
	if (fontTex) {
		SDL_DestroyTexture(fontTex);
		fontTex = NULL;
	}
	if (ren) {
		SDL_DestroyRenderer(ren);
		ren = NULL;
	}
	if (wnd) {
		SDL_DestroyWindow(wnd);
		wnd = NULL;
	}
}
