// Crash and freeze recovery for the Wii U.
//
// Without this, a crash or a hang leaves the console stuck until it's
// switched off at the power button. Two parts:
//
// - Crashes (bad memory access, bad instruction): the Wii U lets an app
//   handle these itself. We save where it happened and make the game thread
//   continue in recoveryEntry() instead, on a spare stack. That shows the
//   error page and exits to the Wii U Menu the normal way.
//
// - Freezes: a watchdog thread on the same CPU core, at a slightly higher
//   priority, wakes up 10 times a second. The game "beats" each frame and
//   each log line. If it hasn't for 2 seconds and HOME is pressed on the
//   GamePad, the watchdog pauses the game thread, points it at
//   recoveryEntry(), and lets it go. After 10 seconds without a beat it also
//   writes where the game is stuck to pd.log, even if nobody presses HOME.
//
// In both cases the log and the screen show the code address (PC) and the
// chain of callers. Matching those against the build's .elf file, with
// powerpc-eabi-addr2line, names the function. See docs/wiiu/wiiu-build.md.

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <coreinit/thread.h>
#include <coreinit/exception.h>
#include <coreinit/context.h>
#include <coreinit/time.h>
#include <coreinit/core.h>
#include <vpad/input.h>
#include <proc_ui/procui.h>

#include "platform.h"
#include "system.h"
#include "wiiu.h"

#define BEAT_TIMEOUT_MS 2000
#define REPORT_TIMEOUT_MS 10000

int pdMain(int argc, const char **argv);

static OSThread *gameThread;
static volatile OSTime lastBeat;
static volatile int recovering;

static OSThread watchdogThread;
static uint8_t watchdogStack[64 * 1024] __attribute__((aligned(16)));

static uint8_t recoveryStack[256 * 1024] __attribute__((aligned(16)));
static OSContext savedCtx;
static const char *problem;

void wiiuWatchdogBeat(void)
{
	lastBeat = OSGetSystemTime();
}

// Writes PC, LR and the chain of callers found by walking the stack.
static void formatTrace(char *out, size_t size, const OSContext *ctx, int withAddress)
{
	uintptr_t lo, hi;
	wiiuGetGameStack(&lo, &hi);

	int n = snprintf(out, size, "PC %08x  LR %08x  SP %08x", (unsigned)ctx->srr0,
		(unsigned)ctx->lr, (unsigned)ctx->gpr[1]);
	if (withAddress) {
		n += snprintf(out + n, size - n, "  address %08x", (unsigned)ctx->dar);
	}
	n += snprintf(out + n, size - n, "\nCallers:");

	// PowerPC stack frames: [sp] holds the caller's sp, [caller's sp + 4]
	// holds the return address into the caller.
	uint32_t sp = ctx->gpr[1];
	for (int i = 0; i < 12 && n < (int)size - 12; ++i) {
		if (sp < lo || sp + 8 > hi || (sp & 3)) {
			break;
		}
		const uint32_t next = *(uint32_t *)sp;
		if (next <= sp || next + 8 > hi) {
			break;
		}
		n += snprintf(out + n, size - n, " %08x", (unsigned)*(uint32_t *)(next + 4));
		sp = next;
	}

	// The code may not sit at the address the .elf says; this shows the shift.
	snprintf(out + n, size - n, "\npdMain is at %08x", (unsigned)(uintptr_t)pdMain);
}

// The game thread continues here after a crash or a freeze.
static void recoveryEntry(void)
{
	static char trace[512];
	formatTrace(trace, sizeof(trace), &savedCtx, problem[0] == 'c');
	sysLogPrintf(LOG_ERROR, "wiiu: the game %s", problem);
	sysLogPrintf(LOG_ERROR, "%s", trace);
	sysFatalError("The game %s.\n\n%s", problem, trace);
}

// Saves `ctx` and changes it so the thread resumes in recoveryEntry().
static void redirect(OSContext *ctx, const char *what)
{
	memcpy(&savedCtx, ctx, sizeof(savedCtx));
	problem = what;

	uint32_t sp = (uint32_t)(uintptr_t)(recoveryStack + sizeof(recoveryStack) - 64);
	*(uint32_t *)sp = 0; // end of the stack frame chain
	ctx->gpr[1] = sp;
	ctx->srr0 = (uint32_t)(uintptr_t)recoveryEntry;
	ctx->lr = 0;
}

static BOOL onCrash(OSContext *ctx, const char *what)
{
	if (recovering) {
		// crashed again while recovering: let the system handle it
		return FALSE;
	}
	recovering = 1;
	redirect(ctx, what);
	return TRUE;
}

static BOOL onDsi(OSContext *ctx) { return onCrash(ctx, "crashed (bad memory access)"); }
static BOOL onIsi(OSContext *ctx) { return onCrash(ctx, "crashed (jumped to a bad address)"); }
static BOOL onProgram(OSContext *ctx) { return onCrash(ctx, "crashed (bad instruction)"); }

static int homePressed(void)
{
	VPADStatus status;
	VPADReadError err;
	if (VPADRead(VPAD_CHAN_0, &status, 1, &err) > 0 && err == VPAD_READ_SUCCESS) {
		return (status.hold & VPAD_BUTTON_HOME) != 0;
	}
	return 0;
}

static int watchdogMain(int argc, void *argv)
{
	int reported = 0;

	for (;;) {
		OSSleepTicks(OSMillisecondsToTicks(100));

		if (recovering) {
			continue;
		}

		// In the HOME menu (or before the screen is up) the game is paused
		// on purpose, so that doesn't count as a freeze.
		if (!ProcUIInForeground()) {
			lastBeat = OSGetSystemTime();
			continue;
		}

		const OSTime stale = OSGetSystemTime() - lastBeat;

		if (stale < OSMillisecondsToTicks(BEAT_TIMEOUT_MS)) {
			reported = 0;
			continue;
		}

		if (!reported && stale >= OSMillisecondsToTicks(REPORT_TIMEOUT_MS)) {
			// the game thread is paused while we run, so its context is current
			static char trace[512];
			formatTrace(trace, sizeof(trace), &gameThread->context, 0);
			sysLogPrintf(LOG_WARNING, "wiiu: no progress for %d s, game is at:", REPORT_TIMEOUT_MS / 1000);
			sysLogPrintf(LOG_WARNING, "%s", trace);
			reported = 1;
		}

		// Only read the GamePad while the game is stuck; normally SDL reads it,
		// and reading here too would steal its button presses.
		if (homePressed()) {
			recovering = 1;
			OSSuspendThread(gameThread);
			redirect(&gameThread->context, "froze (stopped making progress)");
			OSResumeThread(gameThread);
		}
	}

	return 0;
}

void wiiuWatchdogInit(void)
{
	gameThread = OSGetCurrentThread();
	lastBeat = OSGetSystemTime();

	OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_THREAD, OS_EXCEPTION_TYPE_DSI, onDsi);
	OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_THREAD, OS_EXCEPTION_TYPE_ISI, onIsi);
	OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_THREAD, OS_EXCEPTION_TYPE_PROGRAM, onProgram);

	// same core as the game, one step higher priority (lower number), so it
	// can always interrupt a stuck game thread
	const OSThreadAttributes core = (OSThreadAttributes)(OS_THREAD_ATTRIB_AFFINITY_CPU0 << OSGetCoreId());
	int32_t prio = OSGetThreadPriority(gameThread) - 1;
	if (prio < 0) {
		prio = 0;
	}
	if (OSCreateThread(&watchdogThread, watchdogMain, 0, NULL,
			watchdogStack + sizeof(watchdogStack), sizeof(watchdogStack),
			prio, core)) {
		OSSetThreadName(&watchdogThread, "PD watchdog");
		OSResumeThread(&watchdogThread);
	} else {
		sysLogPrintf(LOG_WARNING, "wiiu: could not start the watchdog");
	}
}
