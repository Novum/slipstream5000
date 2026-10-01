#ifndef SLIPSTREAM5000_GAME_SOUND_H
#define SLIPSTREAM5000_GAME_SOUND_H

#include "hmi_digital.h"
#include "hmi_music.h"

#include <stdint.h>

typedef struct SlipGameSoundState {
	uint32_t initialized;
	uint32_t digitalCard;
	uint32_t musicCard;
	uint16_t musicVolumeSetting;
	uint32_t musicRouting[32];
	HmiMusicSongDescriptor musicDescriptor;
	uint32_t branchRequest, branchSong;
	uint32_t timerGuard;
	HmiDigitalDriver *digitalDriver;
	HmiDigitalSampleDescriptor sampleDescriptor;
	HmiDigitalSampleDescriptor loopDescriptor;
} SlipGameSoundState;

void SlipGameSound_Reset(SlipGameSoundState *state, HmiDigitalDriver *digitalDriver);
uint32_t SlipGameSound_Play(SlipGameSoundState *state, const uint8_t *sampleData, uint32_t sampleLength);
uint32_t SlipGameSound_PlayAlternate(SlipGameSoundState *state, const uint8_t *sampleData, uint32_t sampleLength);
uint32_t SlipGameSound_PlayPositioned(SlipGameSoundState *state, const uint8_t *sampleData, uint32_t sampleLength,
                                      int32_t volume);
void SlipGameSound_SetRate(SlipGameSoundState *state, uint32_t encodedHandle, uint32_t rate);
void SlipGameSound_SetVolume(SlipGameSoundState *state, uint32_t encodedHandle, int32_t volume);
uint32_t SlipGameSound_PlayLooping(SlipGameSoundState *state, const uint8_t *sampleData, uint32_t sampleLength);
uint32_t SlipGameSound_PlayLoopingFullVolume(SlipGameSoundState *state, const uint8_t *sampleData,
                                             uint32_t sampleLength);
void SlipGameSound_Stop(SlipGameSoundState *state, uint32_t encodedHandle);
uint32_t SlipGameSound_ActiveCount(const SlipGameSoundState *state);
uint32_t SlipGameSound_IsStopped(const SlipGameSoundState *state, uint32_t encodedHandle);

#endif
