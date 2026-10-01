#ifndef SLIPSTREAM5000_HMI_OPL_OUTPUT_H
#define SLIPSTREAM5000_HMI_OPL_OUTPUT_H
#include "../opal/opal.h"
#include "game_timer.h"
#include "hmi_music.h"
#include <SDL3/SDL.h>

typedef struct HmiOplOutput {
	Opal chip;
	SlipGameTimerState *timer;
	SDL_AudioStream *stream;
	uint64_t pitPhase;
	uint64_t nonzeroFrames;
	uint8_t registerAddress;
	bool streamWriteFailed;
} HmiOplOutput;

void HmiOplOutput_Construct(HmiOplOutput *, HmiA002State *, SlipGameTimerState *);
void HmiOplOutput_Render(HmiOplOutput *, int16_t *stereo, uint32_t frames);
/* Opens paused so initialization/song changes can finish before playback. */
bool HmiOplOutput_Open(HmiOplOutput *);
bool HmiOplOutput_Resume(HmiOplOutput *);
void HmiOplOutput_Close(HmiOplOutput *);
#endif
