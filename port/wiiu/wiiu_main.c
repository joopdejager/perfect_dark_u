// The Wii U's entry point.
//
// The Wii U gives an app's main thread a small stack by default, while the game
// was written for PCs, where the main thread gets megabytes. Running out of
// stack on the Wii U doesn't always crash: it can silently overwrite other
// memory and hang. So main() moves to a big stack of its own before running
// the game's real main(), pdMain() in port/src/main.c.
//
// A "fiber" switch keeps the same thread and CPU core, just on a new stack.
// That matters because SDL's video and the HOME menu handling (ProcUI) must
// stay on the core the app started on.

#include <stdint.h>
#include <stdlib.h>
#include <malloc.h>
#include <coreinit/fiber.h>
#include <coreinit/debug.h>

#include "wiiu.h"

#define GAME_STACK_SIZE (8 * 1024 * 1024)

int pdMain(int argc, const char **argv);

static int mainArgc;
static const char **mainArgv;
static int mainResult;
static uint8_t *gameStack;

void wiiuGetGameStack(uintptr_t *lo, uintptr_t *hi)
{
	*lo = (uintptr_t)gameStack;
	*hi = (uintptr_t)gameStack + GAME_STACK_SIZE;
}

static void gameEntry(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
	mainResult = pdMain(mainArgc, mainArgv);
}

int main(int argc, char **argv)
{
	mainArgc = argc;
	mainArgv = (const char **)argv;

	uint8_t *stack = memalign(64, GAME_STACK_SIZE);
	gameStack = stack;
	if (!stack) {
		OSFatal("Perfect Dark: could not allocate the game's stack.");
	}

	// stacks grow downwards, so pass the top end (less a little headroom
	// for the first stack frame's back chain)
	OSSwitchFiberEx(0, 0, 0, 0, gameEntry, stack + GAME_STACK_SIZE - 64);

	return mainResult;
}
