// The debug overlay (see wiiu_overlay.h).
//
// It draws through the GX2 renderer (port/fast3d/gfx_gx2.cpp) on top of the
// finished frame, using the same 8x8 font as the startup page. The numbers
// are averaged over half a second so they're readable.

#include <stdio.h>
#include <string.h>
#include <coreinit/memheap.h>
#include <coreinit/memexpheap.h>

#include "platform.h"
#include "system.h"

#include "../fast3d/gfx_gx2.h"
#include "wiiu_overlay.h"
#include "wiiu_screen.h"

#define SCALE 2
#define CELL (WIIU_GLYPH_SIZE * SCALE)
#define LINE_H (CELL + 4)
#define MARGIN 12
#define MAX_QUADS 512

static int enabled;
static uint32_t fontTexture;

// the half second being measured
static OSTime windowStart;
static OSTime lastShown;
static uint32_t windowFrames;
static OSTime windowWait;
static OSTime windowMaxFrame;

// the last half second's results, as shown
static float fps;
static float frameMs;
static float maxFrameMs;
static float waitMs;

static struct GfxGx2Quad quads[MAX_QUADS];
static size_t numQuads;

void wiiuOverlayFrameWaited(OSTime ticks)
{
	windowWait += ticks;
}

void wiiuOverlayFrameShown(void)
{
	const OSTime now = OSGetSystemTime();

	if (lastShown) {
		const OSTime frame = now - lastShown;
		if (frame > windowMaxFrame) {
			windowMaxFrame = frame;
		}
		++windowFrames;
	} else {
		windowStart = now;
	}
	lastShown = now;

	const OSTime elapsed = now - windowStart;
	if (elapsed >= OSMillisecondsToTicks(500) && windowFrames > 0) {
		const float elapsedMs = (float)OSTicksToMicroseconds(elapsed) / 1000.0f;
		fps = windowFrames * 1000.0f / elapsedMs;
		frameMs = elapsedMs / windowFrames;
		maxFrameMs = (float)OSTicksToMicroseconds(windowMaxFrame) / 1000.0f;
		waitMs = (float)OSTicksToMicroseconds(windowWait) / 1000.0f / windowFrames;

		windowStart = now;
		windowFrames = 0;
		windowWait = 0;
		windowMaxFrame = 0;
	}
}

static void addQuad(float x0, float y0, float x1, float y1, int glyph, uint32_t rgba)
{
	if (numQuads >= MAX_QUADS) {
		return;
	}
	const float cols = WIIU_FONT_ATLAS_W / WIIU_GLYPH_SIZE;
	const float gu = (float)WIIU_GLYPH_SIZE / WIIU_FONT_ATLAS_W;
	const float gv = (float)WIIU_GLYPH_SIZE / WIIU_FONT_ATLAS_H;
	const float u = (glyph % (int)cols) * gu;
	const float v = (glyph / (int)cols) * gv;

	struct GfxGx2Quad *q = &quads[numQuads++];
	q->x0 = x0;
	q->y0 = y0;
	q->x1 = x1;
	q->y1 = y1;
	q->u0 = u;
	q->v0 = v;
	q->u1 = u + gu;
	q->v1 = v + gv;
	q->rgba = rgba;
}

static void addText(int x, int y, uint32_t rgba, const char *text)
{
	for (const char *p = text; *p; ++p, x += CELL) {
		const unsigned char c = (unsigned char)*p;
		if (c > ' ' && c < WIIU_FONT_SOLID) {
			addQuad(x, y, x + CELL, y + CELL, c, rgba);
		}
	}
}

static void overlayDraw(void)
{
	char lines[6][96];
	int n = 0;
	struct GfxGx2Stats st;
	gfx_gx2_get_stats(&st);

	const uint32_t freeMem = MEMGetTotalFreeSizeForExpHeap(MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM2));

	snprintf(lines[n++], sizeof(lines[0]), "FPS %5.1f", fps);
	snprintf(lines[n++], sizeof(lines[0]), "frame %5.1f ms  worst %5.1f ms", frameMs, maxFrameMs);
	snprintf(lines[n++], sizeof(lines[0]), "CPU busy %5.1f ms  GPU wait %5.1f ms",
		frameMs > waitMs ? frameMs - waitMs : 0.0f, waitMs);
	snprintf(lines[n++], sizeof(lines[0]), "draws %u  tris %u  verts %u KB",
		(unsigned)st.draws, (unsigned)st.triangles, (unsigned)(st.vertex_bytes / 1024));
	snprintf(lines[n++], sizeof(lines[0]), "shaders %u  free mem %u MB",
		(unsigned)st.shaders, (unsigned)(freeMem / (1024 * 1024)));
	snprintf(lines[n++], sizeof(lines[0]), "Minus: hide");

	size_t widest = 0;
	for (int i = 0; i < n; i++) {
		if (strlen(lines[i]) > widest) {
			widest = strlen(lines[i]);
		}
	}

	numQuads = 0;

	// a dark box behind the text, so it's readable on any background
	addQuad(MARGIN, MARGIN, MARGIN + 16 + widest * CELL, MARGIN + 12 + n * LINE_H, WIIU_FONT_SOLID, 0x000000b0);

	int y = MARGIN + 8;
	for (int i = 0; i < n; i++, y += LINE_H) {
		const uint32_t colour = i == 0 ? 0x80ff80ff : (i == n - 1 ? 0x909090ff : 0xffffffff);
		addText(MARGIN + 8, y, colour, lines[i]);
	}

	gfx_gx2_overlay_quads(fontTexture, quads, numQuads);
}

void wiiuOverlayToggle(void)
{
	if (!fontTexture) {
		static uint32_t pixels[WIIU_FONT_ATLAS_W * WIIU_FONT_ATLAS_H];
		wiiuScreenFontAtlas(pixels);
		fontTexture = gfx_gx2_overlay_texture((const uint8_t *)pixels, WIIU_FONT_ATLAS_W, WIIU_FONT_ATLAS_H);
	}

	enabled = !enabled;
	gfx_gx2_set_overlay(enabled ? overlayDraw : NULL);
	sysLogPrintf(LOG_NOTE, "wiiu: debug overlay %s", enabled ? "on" : "off");
}
