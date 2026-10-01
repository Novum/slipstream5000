#include "race_voice_host.h"
#include "config_settings.h"
#include "game_errors.h"
#include "resource_host.h"

static uint32_t SlipRaceVoiceHost_SpeechEnabled(void *context) {
	(void)context;
	return (uint32_t)SlipConfig_Speech();
}

static uint8_t SlipRaceVoiceHost_LanguageInitial(void *context) {
	(void)context;
	return SlipConfig_LanguageInitial();
}

static uint32_t SlipRaceVoiceHost_ResourceSize(void *context, uint16_t handle) {
	uint32_t bytes;
	SlipResourceHost_Size(context, handle, &bytes);
	return bytes;
}

static uint32_t SlipRaceVoiceHost_AvailableBytes(void *context) {
	(void)context;
	return SlipResource_freeBytes + SlipResource_cachedBytes;
}

static uint32_t SlipRaceVoiceHost_Play(void *context, const uint8_t *sample, uint32_t bytes) {
	return SlipGameSound_PlayAlternate(context, sample, bytes);
}

static void SlipRaceVoiceHost_Stop(void *context, uint32_t handle) { SlipGameSound_Stop(context, handle); }

static uint32_t SlipRaceVoiceHost_Stopped(void *context, uint32_t handle) {
	return SlipGameSound_IsStopped(context, handle);
}

static void SlipRaceVoiceHost_ResourceError(void *context) {
	(void)context;
	SlipGame_ResourceFailure();
}

SlipRaceVoiceCalls SlipRaceVoiceHost_Calls(SlipGameSoundState *sound) {
	return (SlipRaceVoiceCalls){.context = sound,
	                            .speechEnabled = SlipRaceVoiceHost_SpeechEnabled,
	                            .languageInitial = SlipRaceVoiceHost_LanguageInitial,
	                            .find = SlipResourceHost_Find,
	                            .load = SlipResourceHost_Load,
	                            .resourceSize = SlipRaceVoiceHost_ResourceSize,
	                            .available = SlipRaceVoiceHost_AvailableBytes,
	                            .lock = SlipResourceHost_Lock,
	                            .unlock = SlipResourceHost_Unlock,
	                            .release = SlipResourceHost_Release,
	                            .play = SlipRaceVoiceHost_Play,
	                            .stop = SlipRaceVoiceHost_Stop,
	                            .stopped = SlipRaceVoiceHost_Stopped,
	                            .error = SlipRaceVoiceHost_ResourceError};
}
