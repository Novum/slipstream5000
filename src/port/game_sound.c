#include "game_sound.h"
#include "config_settings.h"

#include <string.h>

enum {
	SLIP_GAME_SOUND_HANDLE_BIAS = 1u << 16,
	SLIP_GAME_SOUND_SAMPLE_CHANNEL = 2,
	SLIP_GAME_SOUND_SAMPLE_ID = 0x1000,
	SLIP_GAME_SOUND_LOOP_SAMPLE_ID = 0x1100,
	SLIP_GAME_SOUND_ENGINE_QUIET_VOLUME = 0x3000,
	SLIP_GAME_SOUND_ENGINE_LOUD_VOLUME = 0x6000
};

static uint32_t SlipGameSound_Start(SlipGameSoundState *state, HmiDigitalSampleDescriptor *descriptor) {
	return (uint32_t)HmiDigital_StartSample(state->digitalDriver, descriptor) + SLIP_GAME_SOUND_HANDLE_BIAS;
}

static void SlipGameSound_SetCommonDescriptor(HmiDigitalSampleDescriptor *descriptor, const uint8_t *sampleData,
                                              uint32_t sampleLength, uint16_t flags) {
	descriptor->sampleData = sampleData;
	descriptor->sampleLength = sampleLength;
	descriptor->channel = SLIP_GAME_SOUND_SAMPLE_CHANNEL;
	descriptor->sampleID = SLIP_GAME_SOUND_SAMPLE_ID;
	descriptor->callbackOffset = 0;
	descriptor->callbackSelector = 0;
	descriptor->flags = flags;
}

void SlipGameSound_Reset(SlipGameSoundState *state, HmiDigitalDriver *digitalDriver) {
	uint32_t i;
	memset(state, 0, sizeof(*state));
	state->digitalDriver = digitalDriver;
	for (i = 0; i < HMI_MUSIC_TRACK_COUNT; ++i)
		state->musicRouting[i] = HMI_MUSIC_UNROUTED_DRIVER;
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
	SlipGameSound_SetCommonDescriptor(&state->sampleDescriptor, sampleData, sampleLength, HMI_DIGITAL_APPLY_VOLUME);
	state->sampleDescriptor.volume = (int16_t)volume;
	return SlipGameSound_Start(state, &state->sampleDescriptor);
}

void SlipGameSound_SetRate(SlipGameSoundState *state, uint32_t encodedHandle, uint32_t rate) {
	if (state->initialized != 0 && SlipConfig_SoundEffects() != 0 && state->digitalCard != 0) {
		HmiDigital_SetPlaybackRate(state->digitalDriver, encodedHandle & UINT16_MAX, rate);
	}
}

void SlipGameSound_SetVolume(SlipGameSoundState *state, uint32_t encodedHandle, int32_t volume) {
	if (state->initialized != 0 && SlipConfig_SoundEffects() != 0 && state->digitalCard != 0) {
		HmiDigital_SetVolume(state->digitalDriver, encodedHandle & UINT16_MAX, (int16_t)volume);
	}
}

uint32_t SlipGameSound_PlayLooping(SlipGameSoundState *state, const uint8_t *sampleData, uint32_t sampleLength) {
	HmiDigitalSampleDescriptor *const descriptor = &state->loopDescriptor;

	if (state->initialized == 0 || SlipConfig_SoundEffects() == 0)
		return 0;

	const int engineSetting = SlipConfig_EngineSounds();
	if (engineSetting == SLIP_CONFIG_ENGINE_SOUND_OFF || state->digitalCard == 0)
		return 0;
	descriptor->sampleData = sampleData;
	descriptor->sampleLength = sampleLength;
	descriptor->loopCount = HMI_DIGITAL_LOOP_FOREVER;
	descriptor->channel = SLIP_GAME_SOUND_SAMPLE_CHANNEL;
	descriptor->volume = engineSetting == SLIP_CONFIG_ENGINE_SOUND_QUIET ? SLIP_GAME_SOUND_ENGINE_QUIET_VOLUME
	                                                                     : SLIP_GAME_SOUND_ENGINE_LOUD_VOLUME;
	descriptor->sampleID = SLIP_GAME_SOUND_LOOP_SAMPLE_ID;
	descriptor->callbackOffset = 0;
	descriptor->callbackSelector = 0;
	descriptor->flags = (HMI_DIGITAL_LOOP | HMI_DIGITAL_RESAMPLE | HMI_DIGITAL_APPLY_VOLUME);
	descriptor->playbackRateQ16 = HMI_DIGITAL_RATE_ONE_Q16;
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
	descriptor->loopCount = HMI_DIGITAL_LOOP_FOREVER;
	descriptor->channel = SLIP_GAME_SOUND_SAMPLE_CHANNEL;
	descriptor->volume = INT16_MAX;
	descriptor->sampleID = SLIP_GAME_SOUND_LOOP_SAMPLE_ID;
	descriptor->callbackOffset = 0;
	descriptor->callbackSelector = 0;
	descriptor->flags = (HMI_DIGITAL_LOOP | HMI_DIGITAL_APPLY_VOLUME);
	return SlipGameSound_Start(state, descriptor);
}

void SlipGameSound_Stop(SlipGameSoundState *state, uint32_t encodedHandle) {
	if (state->initialized != 0 && state->digitalCard != 0 && encodedHandle != 0) {
		HmiDigital_StopSample(state->digitalDriver, encodedHandle & UINT16_MAX);
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
	status = HmiDigital_VoiceStatus(state->digitalDriver, encodedHandle & UINT16_MAX);
	return status == HMI_DIGITAL_STATUS_STOPPED ? 1u : 0u;
}
