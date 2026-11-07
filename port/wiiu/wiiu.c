#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <SDL.h>
#include <coreinit/debug.h>
#include <coreinit/time.h>

#include "wiiu.h"
#include "wiiu_screen.h"

static void wiiuShutdown(void)
{
	wiiuScreenShutdown();
	// SDL_Quit() also does the Wii U's exit handshake: it returns to the
	// Wii U Menu if we're quitting on our own, or finishes closing if the
	// user picked "Close" in the HOME menu.
	SDL_Quit();
}

unsigned long long wiiuGetMicroseconds(void)
{
	return (unsigned long long)OSTicksToMicroseconds(OSGetSystemTime());
}

void wiiuInit(void)
{
	// mkdir() fails harmlessly if the folder exists already
	mkdir("/vol/external01/wiiu", 0777);
	mkdir("/vol/external01/wiiu/apps", 0777);
	mkdir(WIIU_APP_DIR, 0777);
	mkdir(WIIU_DATA_DIR, 0777);

	// runs after the game's own exit handler, which saves settings
	atexit(wiiuShutdown);

	wiiuWatchdogInit();
}

static int isButtonEvent(const SDL_Event *ev)
{
	return ev->type == SDL_CONTROLLERBUTTONDOWN || ev->type == SDL_JOYBUTTONDOWN
		|| ev->type == SDL_FINGERDOWN || ev->type == SDL_KEYDOWN;
}

void wiiuShowFatalError(const char *msg)
{
	SDL_Renderer *ren = wiiuScreenGetRenderer();
	if (!ren) {
		// No SDL window: fall back to the system's own error screen. The user
		// has to hold the power button to get out of it.
		OSFatal(msg);
		return;
	}

	// open the controllers so their button presses come through as events
	SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);
	for (int i = 0; i < SDL_NumJoysticks(); ++i) {
		SDL_JoystickOpen(i);
	}

	// ignore buttons for a moment, so one held down from before doesn't
	// dismiss the message before it's been seen
	const Uint32 acceptAfter = SDL_GetTicks() + 1000;

	for (;;) {
		wiiuWatchdogBeat(); // waiting for a button isn't a freeze

		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			if (ev.type == SDL_QUIT) {
				return; // closed from the HOME menu
			}
			if (isButtonEvent(&ev) && SDL_TICKS_PASSED(SDL_GetTicks(), acceptAfter)) {
				return;
			}
		}

		SDL_SetRenderDrawColor(ren, 0x20, 0x08, 0x08, 0xff);
		SDL_RenderClear(ren);

		int y = 48;
		y = wiiuScreenText(48, y, 4, 0xff4040, "Perfect Dark");
		y += 16;
		y = wiiuScreenText(48, y, 3, 0xffffff, msg);
		y += 24;
		y = wiiuScreenText(48, y, 2, 0xc0c0c0,
			"ROM goes here on the SD card:\n  " WIIU_DATA_DIR_DISPLAY "/\n"
			"Settings, saves and pd.log:\n  " WIIU_APP_DIR_DISPLAY "/");
		y += 24;
		wiiuScreenText(48, y, 2, 0xffd060, "Press any button to return to the Wii U Menu.");
		y += 40;
		wiiuScreenLog(y, 2);

		SDL_RenderPresent(ren);
	}
}
