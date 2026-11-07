#ifndef _IN_WIIU_OVERLAY_H
#define _IN_WIIU_OVERLAY_H

// The debug overlay: frame rate, frame timing and renderer numbers, drawn in
// the top left corner over the game. Hold ZL and press Minus to show or hide it.

#include <stdint.h>
#include <coreinit/time.h>

#ifdef __cplusplus
extern "C" {
#endif

// Shows the overlay if hidden, hides it if shown.
void wiiuOverlayToggle(void);

// Frame timing, from the window manager: how long it waited for the GPU
// before this frame, and the moment each frame is shown.
void wiiuOverlayFrameWaited(OSTime ticks);
void wiiuOverlayFrameShown(void);

#ifdef __cplusplus
}
#endif

#endif
