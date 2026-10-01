#include "game_sound.h"
#include "config_settings.h"

#include <string.h>

static uint32_t SlipGameSound_Start(SlipGameSoundState *state, HmiDigitalSampleDescriptor *descriptor) {
	return (uint32_t)HmiDigital_StartSample(state->digitalDriver, descriptor) + 0x10000u;
}

static void SlipGameSound_SetCommonDescriptor(HmiDigitalSampleDescriptor *descriptor, const uint8_t *sampleData,
                                              uint32_t sampleLength, uint16_t flags) {
	descriptor->sampleData = sampleData;
	descriptor->sampleLength = sampleLength;
	descriptor->channel = 2;
	descriptor->sampleID = 0x1000;
	descriptor->callbackOffset = 0;
	descriptor->callbackSelector = 0;
	descriptor->flags = flags;
}

void SlipGameSound_Reset(SlipGameSoundState *state, HmiDigitalDriver *digitalDriver) {
	uint32_t i;
	memset(state, 0, sizeof(*state));
	state->digitalDriver = digitalDriver;
	for (i = 0; i < 32; ++i)
		state->musicRouting[i] = 0xff;
}

uint32_t SlipGameSound_Play(SlipGameSoundState *state, const uint8_t *sampleData, uint32_t sampleLength) {
	if (state->initialized == 0 || SlipConfig_SoundEffects() == 0 || state->digitalCard == 0) {
		return 0;
	}
	SlipGameSound_SetCommonDescriptor(&state->sampleDescriptor, sampleData, sampleLength, 0);
	return SlipGameSound_Start(state, &state->sampleDescriptor);
}

uint32_t SlipGameSound_PlayAlternate(SlipGameSoundState *state, const uint8_t *sampleData, uint32_t sampleLength) {
	if (state->initialized == 0 || SlipConfig_Speech() == 0 || state->digitalCard == 0) {
		return 0;
	}
	SlipGameSound_SetCommonDescriptor(&state->sampleDescriptor, sampleData, sampleLength, 0);
	return SlipGameSound_Start(state, &state->sampleDescriptor);
}

uint32_t SlipGameSound_PlayPositioned(SlipGameSoundState *state, const uint8_t *sampleData, uint32_t sampleLength,
                                      int32_t volume) {
	if (state->initialized == 0 || SlipConfig_SoundEffects() == 0 || state->digitalCard == 0) {
		return 0;
	}
	SlipGameSound_SetCommonDescriptor(&state->sampleDescriptor, sampleData, sampleLength, 0x0100u);
	state->sampleDescriptor.volume = (int16_t)volume;
	return SlipGameSound_Start(state, &state->sampleDescriptor);
}

void SlipGameSound_SetRate(SlipGameSoundState *state, uint32_t encodedHandle, uint32_t rate) {
	if (state->initialized != 0 && SlipConfig_SoundEffects() != 0 && state->digitalCard != 0) {
		HmiDigital_SetPlaybackRate(state->digitalDriver, encodedHandle & 0xffffu, rate);
	}
}

void SlipGameSound_SetVolume(SlipGameSoundState *state, uint32_t encodedHandle, int32_t volume) {
	if (state->initialized != 0 && SlipConfig_SoundEffects() != 0 && state->digitalCard != 0) {
		HmiDigital_SetVolume(state->digitalDriver, encodedHandle & 0xffffu, (int16_t)volume);
	}
}

uint32_t SlipGameSound_PlayLooping(SlipGameSoundState *state, const uint8_t *sampleData, uint32_t sampleLength) {
	HmiDigitalSampleDescriptor *const descriptor = &state->loopDescriptor;

	if (state->initialized == 0 || SlipConfig_SoundEffects() == 0)
		return 0;

	const int engineSetting = SlipConfig_EngineSounds();
	if (engineSetting == 0 || state->digitalCard == 0)
		return 0;
	descriptor->sampleData = sampleData;
	descriptor->sampleLength = sampleLength;
	descriptor->loopCount = -1;
	descriptor->channel = 2;
	descriptor->volume = engineSetting == 1 ? 0x3000 : 0x6000;
	descriptor->sampleID = 0x1100;
	descriptor->callbackOffset = 0;
	descriptor->callbackSelector = 0;
	descriptor->flags = 0x4500u;
	descriptor->playbackRateQ16 = 0x10000u;
	return SlipGameSound_Start(state, descriptor);
}

uint32_t SlipGameSound_PlayLoopingFullVolume(SlipGameSoundState *state, const uint8_t *sampleData,
                                             uint32_t sampleLength) {
	HmiDigitalSampleDescriptor *const descriptor = &state->loopDescriptor;

	if (state->initialized == 0 || SlipConfig_SoundEffects() == 0 || state->digitalCard == 0) {
		return 0;
	}
	descriptor->sampleData = sampleData;
	descriptor->sampleLength = sampleLength;
	descriptor->loopCount = -1;
	descriptor->channel = 2;
	descriptor->volume = 0x7fff;
	descriptor->sampleID = 0x1100;
	descriptor->callbackOffset = 0;
	descriptor->callbackSelector = 0;
	descriptor->flags = 0x4100u;
	return SlipGameSound_Start(state, descriptor);
}

void SlipGameSound_Stop(SlipGameSoundState *state, uint32_t encodedHandle) {
	if (state->initialized != 0 && state->digitalCard != 0 && encodedHandle != 0) {
		HmiDigital_StopSample(state->digitalDriver, encodedHandle & 0xffffu);
	}
}

uint32_t SlipGameSound_ActiveCount(const SlipGameSoundState *state) {
	if (state->initialized == 0 || state->digitalCard == 0) {
		return 0;
	}
	return HmiDigital_ActiveVoiceCount(state->digitalDriver);
}

uint32_t SlipGameSound_IsStopped(const SlipGameSoundState *state, uint32_t encodedHandle) {
	uint32_t status;

	if (state->initialized == 0 || state->digitalCard == 0 || encodedHandle == 0) {
		return 1;
	}
	status = HmiDigital_VoiceStatus(state->digitalDriver, encodedHandle & 0xffffu);
	return status == 1 ? 1u : 0u;
}
