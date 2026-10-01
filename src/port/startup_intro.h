#ifndef SLIPSTREAM5000_STARTUP_INTRO_H
#define SLIPSTREAM5000_STARTUP_INTRO_H

#include "game_sound.h"
#include "input.h"

#include <stdbool.h>

typedef struct SlipStartupIntroHost {
	void (*pollEvents)(void *context);
	bool (*inputHeld)(void *context, SlipInputCode inputCode);
	SlipInputCode (*popInput)(void *context);
	bool (*testAndClearInput)(void *context, SlipInputCode inputCode);
	void (*presentFrame)(void *context);
	bool (*isRunning)(void *context);
	SlipGameSoundState *sound;
	bool (*lockSound)(void *context);
	void (*unlockSound)(void *context);
	void *context;
} SlipStartupIntroHost;

bool SlipStartupIntro_Run(const char *resPath, const SlipStartupIntroHost *host);

#endif
