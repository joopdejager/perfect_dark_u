#ifndef _IN_WIIU_H
#define _IN_WIIU_H

// Wii U platform layer. Only built for the Wii U (see CMakeLists.txt).

#ifdef __cplusplus
extern "C" {
#endif

// Everything the game reads or writes lives in one folder on the SD card:
//   sd:/wiiu/apps/perfectdark/data/pd.ntsc-final.z64   <- the ROM goes here
//   sd:/wiiu/apps/perfectdark/pd.ini, saves, logs
// "/vol/external01" is how the Wii U names the SD card.
#define WIIU_APP_DIR  "/vol/external01/wiiu/apps/perfectdark"
#define WIIU_DATA_DIR WIIU_APP_DIR "/data"
// The same folders as a user would see them on a PC reading the SD card.
#define WIIU_APP_DIR_DISPLAY  "sd:/wiiu/apps/perfectdark"
#define WIIU_DATA_DIR_DISPLAY WIIU_APP_DIR_DISPLAY "/data"

// Microseconds since the console started, from its own clock.
unsigned long long wiiuGetMicroseconds(void);

// Called once at startup from sysInit(): creates the folders above so the
// user only has to copy the ROM in.
void wiiuInit(void);

// Shows an error message on the TV and GamePad and waits until the user
// presses a button or leaves through the HOME menu. Called by sysFatalError().
void wiiuShowFatalError(const char *msg);

#ifdef __cplusplus
}
#endif

#endif
