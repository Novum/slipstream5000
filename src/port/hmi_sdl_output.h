#ifndef SLIPSTREAM5000_HMI_SDL_OUTPUT_H
#define SLIPSTREAM5000_HMI_SDL_OUTPUT_H

#include "hmi_mixer_1000.h"

#include <SDL3/SDL_audio.h>

#include <stdbool.h>
#include <stdint.h>

typedef struct HmiSdlOutput {
	SDL_AudioStream *stream;
	HmiMixer1000State *mixer;
	uint32_t outputRate;
	uint32_t mixerTimerRate;
	uint32_t dmaReadPosition;
	uint32_t timerPhase;
	bool mixerFailed;
} HmiSdlOutput;

bool HmiSdlOutput_Open(HmiSdlOutput *output, HmiMixer1000State *mixer, uint32_t outputRate, uint32_t mixerTimerRate);
void HmiSdlOutput_Close(HmiSdlOutput *output);
bool HmiSdlOutput_Lock(HmiSdlOutput *output);
bool HmiSdlOutput_Unlock(HmiSdlOutput *output);
bool HmiSdlOutput_Provide(HmiSdlOutput *output, SDL_AudioStream *stream, uint32_t amount);

#endif
