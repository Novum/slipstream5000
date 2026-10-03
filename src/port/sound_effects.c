#include "sound_effects.h"

#include "config_settings.h"
#include "draw3d.h"
#include "frame_timer.h"
#include "game_errors.h"
#include "race.h"
#include "resource_host.h"
#include "runtime.h"

#include <stdio.h>
#include <string.h>

enum {
	SLIP_SOUND_TRACK_CODE_BYTES = 4,
	SLIP_SOUND_SET_COUNT = 3,
	SLIP_SOUND_INITIAL_QUEUE_CONTINUATION = 4,
	SLIP_SOUND_MAXIMUM_DISTANCE = 73200,
	SLIP_SOUND_FLYBY_VOLUME = 16384,
	SLIP_SOUND_FLYBY_SAMPLE_INDEX = 2,
	SLIP_SOUND_TRACK_CHANGE_SAMPLE_INDEX = 23,
	SLIP_SOUND_AMBIENT_SAMPLE_BASE_INDEX = 8,
	SLIP_SOUND_ENGINE_SPEED_RATE_SHIFT = 2,
	SLIP_SOUND_AMBIENT_FADE_STEP_SHIFT = 1
};

static bool SlipSoundEffects_installed;
uint32_t SlipSoundEffects_externalSoundFiles;

void SlipSoundEffects_Install(void) { SlipSoundEffects_installed = true; }

static const char *const SlipSoundEffects_fixedNames[SLIP_SOUND_EFFECT_RESOURCE_COUNT] = {
    "LOW.SMP",     "HIGH.SMP",     "JETPASS1.SMP", "SCRAPE2.SMP",  "SCRAPE1.SMP",  "CRASH.SMP",
    "BLASTER.SMP", "MISSILE.SMP",  "BONUSCOL.SMP", "PITSLP.SMP",   "CROWDLP.SMP",  "MINEDROP.SMP",
    NULL,          "WATERHIT.SMP", "EXPLOSN.SMP",  "LASERHIT.SMP", "DISRUPTR.SMP", "ENGSTART.SMP",
    NULL,          "BOMBER.SMP",   "SCRAMBLE.SMP", "HYPERNEU.SMP", "AMBLER.SMP",   "WOOSH.SMP"};

static const char SlipSoundEffects_lowTrackCodes[SLIP_RACE_TRACK_COUNT][SLIP_SOUND_TRACK_CODE_BYTES + 1] = {
    "EM01", "EF12", "EM09", "EF08", "EF10", "EF06", "EM03", "EM05", "EM07", "EM11"};
static const char SlipSoundEffects_highTrackCodes[SLIP_RACE_TRACK_COUNT][SLIP_SOUND_TRACK_CODE_BYTES + 1] = {
    "EM02", "EF13", "EM10", "EF09", "EF11", "EF07", "EM04", "EM06", "EM08", "EM12"};

static bool SlipSoundEffects_Lock(SlipSoundEffectsState *state) {
	return state->lockSound == NULL || state->lockSound(state->context);
}

static void SlipSoundEffects_Unlock(SlipSoundEffectsState *state) {
	if (state->unlockSound != NULL)
		state->unlockSound(state->context);
}

static void SlipSoundEffects_PlayResource(SlipSoundEffectsState *state, uint32_t resourceIndex);

static void SlipSoundEffects_ReleaseResources(SlipSoundEffectsState *state) {
	uint32_t i;
	for (i = 0; i < SLIP_SOUND_EFFECT_RESOURCE_COUNT; ++i) {
		const uint16_t resource = state->resources[i];
		if (resource != 0) {
			SlipResourceHost_Unlock(NULL, resource);
			SlipResourceHost_Release(NULL, resource);
		}
	}
}

bool SlipSoundEffects_Initialize(SlipSoundEffectsState *state, const char *const *archives, size_t archiveCount,
                                 uint16_t track, uint16_t soundSet, SlipGameSoundState *gameSound,
                                 SlipSoundEffectObjectPosition objectPosition, SlipSoundEffectTrackLight trackLight,
                                 SlipSoundEffectLock lockSound, SlipSoundEffectUnlock unlockSound, void *context) {
	char lowName[SLIP_RESOURCE_NAME_BUFFER_BYTES];
	char highName[SLIP_RESOURCE_NAME_BUFFER_BYTES];
	uint16_t trackIndex;
	uint32_t i;

	(void)archives;
	(void)archiveCount;
	state->initialized = false;
	if (!SlipSoundEffects_installed)
		return true;
	if (track == 0u || track > SLIP_RACE_TRACK_COUNT || soundSet >= SLIP_SOUND_SET_COUNT || gameSound == NULL)
		return false;
	SlipArchive *savedPrimaryArchive = NULL;
	if (SlipSoundEffects_externalSoundFiles != 0) {
		savedPrimaryArchive = SlipFile_GetPrimaryArchive();
		SlipFile_SetPrimaryArchive(NULL);
	}
	trackIndex = (uint16_t)(track - 1u);
	snprintf(lowName, sizeof(lowName), "%s.SMP", SlipSoundEffects_lowTrackCodes[trackIndex]);
	snprintf(highName, sizeof(highName), "%s.SMP", SlipSoundEffects_highTrackCodes[trackIndex]);
	lowName[0] = (char)SlipConfig_LanguageInitial();
	highName[0] = lowName[0];

	for (i = 0; i < SLIP_SOUND_EFFECT_RESOURCE_COUNT; ++i) {
		const char *name = SlipSoundEffects_fixedNames[i];
		if (i == SLIP_SOUND_EFFECT_TRACK_LOW_INDEX)
			name = lowName;
		else if (i == SLIP_SOUND_EFFECT_TRACK_HIGH_INDEX)
			name = highName;
		if (!SlipResourceHost_Load(NULL, name, &state->resources[i]))
			SlipGame_ResourceFailure();
		const uint8_t *const sample = SlipResourceHost_Lock(NULL, state->resources[i]);
		uint32_t sampleBytes;
		SlipResourceHost_Size(NULL, state->resources[i], &sampleBytes);
		state->samplePayloads[i] = (SlipResourcePayload){.data = (uint8_t *)sample, .size = sampleBytes};
	}
	if (SlipSoundEffects_externalSoundFiles != 0)
		SlipFile_SetPrimaryArchive(savedPrimaryArchive);
	state->gameSound = gameSound;
	state->objectPosition = objectPosition;
	state->trackLight = trackLight;
	state->lockSound = lockSound;
	state->unlockSound = unlockSound;
	state->context = context;
	for (i = 0; i < 2; ++i)
		state->engines[i].object = 0;
	state->ambientRequested = 0;
	state->ambientCurrent = 0;
	state->ambientHandle = 0;
	state->engineLoopsEnabled = 0;
	state->initialized = true;
	return true;
}

void SlipSoundEffects_ReleaseTrackSamples(SlipSoundEffectsState *state) {
	if (!state->initialized)
		return;
	SlipResourceHost_Unlock(NULL, state->resources[SLIP_SOUND_EFFECT_TRACK_LOW_INDEX]);
	SlipResourceHost_Release(NULL, state->resources[SLIP_SOUND_EFFECT_TRACK_LOW_INDEX]);
	state->resources[SLIP_SOUND_EFFECT_TRACK_LOW_INDEX] = 0;
	SlipResourceHost_Unlock(NULL, state->resources[SLIP_SOUND_EFFECT_TRACK_HIGH_INDEX]);
	SlipResourceHost_Release(NULL, state->resources[SLIP_SOUND_EFFECT_TRACK_HIGH_INDEX]);
	state->resources[SLIP_SOUND_EFFECT_TRACK_HIGH_INDEX] = 0;
}

void SlipSoundEffects_Shutdown(SlipSoundEffectsState *state) {
	uint32_t i;
	if (state == NULL || !state->initialized)
		return;
	if (SlipSoundEffects_Lock(state)) {
		for (i = 0; i < SLIP_SOUND_ENGINE_COUNT; ++i) {
			if (state->engines[i].object != 0u && state->engines[i].loopHandle != 0u)
				SlipGameSound_Stop(state->gameSound, state->engines[i].loopHandle);
		}
		if (state->ambientHandle != 0u)
			SlipGameSound_Stop(state->gameSound, state->ambientHandle);
		SlipSoundEffects_Unlock(state);
	}

	while (SlipGameSound_ActiveCount(state->gameSound) != 0u) {
	}
	SlipSoundEffects_ReleaseResources(state);
	state->initialized = false;
}

void SlipSoundEffects_BeginFrame(SlipSoundEffectsState *state) {
	uint32_t i;
	if (state != NULL && state->initialized) {
		for (i = 0; i < SLIP_SOUND_ENGINE_COUNT; ++i)
			state->engines[i].frameSubmissionCount = 0;
		memset(state->requests, 0, sizeof(state->requests));
	}
}

void SlipSoundEffects_EnableEngineLoops(SlipSoundEffectsState *state) {
	if (state != NULL)
		state->engineLoopsEnabled = 1u;
}

static void SlipSoundEffects_UpdateAmbient(SlipSoundEffectsState *state) {
	uint32_t volumeStep;
	uint32_t ambientVolume;
	if (!SlipSoundEffects_Lock(state))
		return;
	if (state->ambientRequested != 0u) {
		if (state->ambientCurrent != state->ambientRequested) {
			const SlipResourcePayload *sample;
			if (state->ambientHandle != 0u)
				SlipGameSound_Stop(state->gameSound, state->ambientHandle);
			state->ambientCurrent = state->ambientRequested;
			sample = &state->samplePayloads[state->ambientCurrent + SLIP_SOUND_AMBIENT_SAMPLE_BASE_INDEX];
			state->ambientHandle =
			    SlipGameSound_PlayLoopingFullVolume(state->gameSound, sample->data, (uint32_t)sample->size);
		}
		ambientVolume = state->ambientVolume;
		if (ambientVolume != INT16_MAX) {
			volumeStep = SlipFrameTimer_Step() << SLIP_SOUND_AMBIENT_FADE_STEP_SHIFT;
			ambientVolume += volumeStep;
			if ((int32_t)ambientVolume > INT16_MAX)
				ambientVolume = INT16_MAX;
			state->ambientVolume = ambientVolume;
			SlipGameSound_SetVolume(state->gameSound, state->ambientHandle, (int32_t)ambientVolume);
		}
	} else if (state->ambientCurrent != 0u) {
		if (state->ambientHandle != 0u) {
			ambientVolume = state->ambientVolume;
			volumeStep = SlipFrameTimer_Step() << SLIP_SOUND_AMBIENT_FADE_STEP_SHIFT;
			ambientVolume = ambientVolume < volumeStep ? 0u : ambientVolume - volumeStep;
			state->ambientVolume = ambientVolume;
			if (ambientVolume != 0u) {
				SlipGameSound_SetVolume(state->gameSound, state->ambientHandle, (int32_t)ambientVolume);
			} else {
				SlipGameSound_Stop(state->gameSound, state->ambientHandle);
				state->ambientHandle = 0u;
			}
		}
		if (state->ambientHandle == 0u)
			state->ambientCurrent = 0u;
	}
	SlipSoundEffects_Unlock(state);
}

void SlipSoundEffects_StopAmbient(SlipSoundEffectsState *state) {
	if (state == NULL || state->ambientCurrent == 0u || !SlipSoundEffects_Lock(state))
		return;
	if (state->ambientHandle != 0u) {
		SlipGameSound_Stop(state->gameSound, state->ambientHandle);
		state->ambientHandle = 0u;
	}
	state->ambientCurrent = 0u;
	SlipSoundEffects_Unlock(state);
}

void SlipSoundEffects_AddEngine(SlipSoundEffectsState *state, uint16_t trackChangeMode, int32_t speed,
                                int32_t steeringInput, uint16_t object) {
	uint32_t i;
	uint16_t light;
	if (state == NULL || !state->initialized)
		return;
	for (i = 0; i < SLIP_SOUND_ENGINE_COUNT; ++i) {
		if (state->engines[i].object == object)
			break;
	}
	if (i == SLIP_SOUND_ENGINE_COUNT) {
		for (i = 0; i < SLIP_SOUND_ENGINE_COUNT; ++i) {
			if (state->engines[i].object == 0u) {
				state->engines[i].object = object;
				state->engines[i].trackLight = UINT32_MAX;
				state->engines[i].loopHandle = 0u;
				break;
			}
		}
	}
	if (i == SLIP_SOUND_ENGINE_COUNT) {
		SlipRuntime_Fatal("FxAddEngine: Too many engines this frame!");
		return;
	}
	state->engines[i].trackChangeMode = trackChangeMode;
	state->engines[i].speed = speed;
	state->engines[i].steeringInput = steeringInput;
	++state->engines[i].frameSubmissionCount;

	if (state->trackLight == NULL || !state->trackLight(state->context, object, &light))
		return;
	if (state->engines[i].trackLight == UINT32_MAX) {
		state->engines[i].trackLight = light;
	} else if (state->engines[i].trackLight != light) {
		state->engines[i].trackLight = light;
		if (trackChangeMode != SLIP_SOUND_TRACK_CHANGE_SUPPRESS)
			SlipSoundEffects_PlayResource(state, SLIP_SOUND_TRACK_CHANGE_SAMPLE_INDEX);
	}
}

void SlipSoundEffects_Queue(SlipSoundEffectsState *state, int32_t positionX, int32_t positionY, int32_t positionZ,
                            uint32_t effect, uint16_t object, uint16_t positionMode) {
	uint32_t i;
	if (state == NULL || !state->initialized)
		return;
	for (i = 0; i < SLIP_SOUND_EFFECT_QUEUE_COUNT; ++i) {
		SlipSoundEffectRequest *const request = &state->requests[i];
		if (request->effect == SLIP_SOUND_EFFECT_NONE) {
			request->effect = effect;
			request->positionMode = positionMode;
			request->object = object;
			request->positionX = positionX;
			request->positionY = positionY;
			request->positionZ = positionZ;
			break;
		}
	}
}

void SlipSoundEffects_SetAmbient(SlipSoundEffectsState *state, uint32_t ambient) { state->ambientRequested = ambient; }

static void SlipSoundEffects_PlayResource(SlipSoundEffectsState *state, uint32_t resourceIndex) {
	const SlipResourcePayload *sample;
	if (state == NULL || !state->initialized || !SlipSoundEffects_Lock(state))
		return;
	sample = &state->samplePayloads[resourceIndex];
	(void)SlipGameSound_Play(state->gameSound, sample->data, (uint32_t)sample->size);
	SlipSoundEffects_Unlock(state);
}

void SlipSoundEffects_PlayLow(SlipSoundEffectsState *state) {
	if (state->initialized && state->resources[SLIP_SOUND_EFFECT_TRACK_LOW_INDEX] != 0)
		SlipSoundEffects_PlayResource(state, SLIP_SOUND_EFFECT_TRACK_LOW_INDEX);
}

void SlipSoundEffects_PlayFlyby(SlipSoundEffectsState *state) {
	const SlipResourcePayload *const sample = &state->samplePayloads[SLIP_SOUND_FLYBY_SAMPLE_INDEX];
	if (!state->initialized || !SlipSoundEffects_Lock(state))
		return;
	(void)SlipGameSound_PlayPositioned(state->gameSound, sample->data, (uint32_t)sample->size, SLIP_SOUND_FLYBY_VOLUME);
	SlipSoundEffects_Unlock(state);
}

void SlipSoundEffects_PlayHigh(SlipSoundEffectsState *state) {
	if (state->initialized && state->resources[SLIP_SOUND_EFFECT_TRACK_HIGH_INDEX] != 0)
		SlipSoundEffects_PlayResource(state, SLIP_SOUND_EFFECT_TRACK_HIGH_INDEX);
}

static void SlipSoundEffects_Play(SlipSoundEffectsState *state, const SlipSoundEffectRequest *request,
                                  uint32_t distance) {
	const SlipResourcePayload *sample;

	static const uint8_t resourceIndices[SLIP_SOUND_EFFECT_COUNT] = {5,  3,  4,  6,  7,  8,  11, 13,
	                                                                 14, 15, 16, 17, 19, 20, 21, 22};
	if (request->effect == SLIP_SOUND_EFFECT_NONE || request->effect > SLIP_SOUND_EFFECT_COUNT)
		return;
	sample = &state->samplePayloads[resourceIndices[request->effect - 1u]];
	if (!SlipSoundEffects_Lock(state))
		return;
	if (distance == 0u) {
		(void)SlipGameSound_Play(state->gameSound, sample->data, (uint32_t)sample->size);
	} else if (distance != SLIP_SOUND_MAXIMUM_DISTANCE) {
		const uint32_t volume =
		    (uint32_t)(((uint64_t)INT16_MAX * (SLIP_SOUND_MAXIMUM_DISTANCE - distance)) / SLIP_SOUND_MAXIMUM_DISTANCE);
		(void)SlipGameSound_PlayPositioned(state->gameSound, sample->data, (uint32_t)sample->size, (int32_t)volume);
	}
	SlipSoundEffects_Unlock(state);
}

void SlipSoundEffects_EndFrame(SlipSoundEffectsState *state, uint32_t listenerMode, int32_t listenerX,
                               int32_t listenerY, int32_t listenerZ, uint16_t listenerObject,
                               uint16_t secondListenerObject) {
	uint32_t i;

	uint32_t queueContinuation = SLIP_SOUND_INITIAL_QUEUE_CONTINUATION;
	if (state == NULL || !state->initialized)
		return;
	for (i = 0; i < SLIP_SOUND_ENGINE_COUNT; ++i) {
		if (state->engines[i].object != 0u && state->engines[i].frameSubmissionCount == 0u) {
			if (state->engines[i].loopHandle != 0u && SlipSoundEffects_Lock(state)) {
				SlipGameSound_Stop(state->gameSound, state->engines[i].loopHandle);
				SlipSoundEffects_Unlock(state);
			}
			state->engines[i].object = 0u;
		}
	}
	if (state->engineLoopsEnabled != 0u) {
		for (i = 0; i < SLIP_SOUND_ENGINE_COUNT; ++i) {
			if (state->engines[i].object != 0u && SlipSoundEffects_Lock(state)) {
				if (state->engines[i].loopHandle == 0u) {
					const SlipResourcePayload *const sample = &state->samplePayloads[0];
					state->engines[i].loopHandle =
					    SlipGameSound_PlayLooping(state->gameSound, sample->data, (uint32_t)sample->size);
				}
				SlipGameSound_SetRate(state->gameSound, state->engines[i].loopHandle,
				                      (uint32_t)(state->engines[i].speed >> SLIP_SOUND_ENGINE_SPEED_RATE_SHIFT) +
				                          HMI_DIGITAL_RATE_ONE_Q16);
				SlipSoundEffects_Unlock(state);
			}
		}
	}
	for (i = 0; i < SLIP_SOUND_EFFECT_QUEUE_COUNT && queueContinuation != 0u; ++i) {
		SlipSoundEffectRequest *const request = &state->requests[i];
		int32_t sourceX, sourceY, sourceZ;
		SlipDraw3DApproxAbsVectorLength approximate;
		uint32_t distance = 0u;

		if (request->effect == SLIP_SOUND_EFFECT_NONE)
			continue;

		bool spatial =
		    request->positionMode != SLIP_SOUND_POSITION_NONE &&
		    !(request->positionMode != SLIP_SOUND_POSITION_COORDINATES && listenerMode == SLIP_SOUND_LISTENER_OBJECT &&
		      (request->object == listenerObject || request->object == secondListenerObject));
		if (spatial) {
			if (request->positionMode == SLIP_SOUND_POSITION_COORDINATES) {
				sourceX = request->positionX;
				sourceY = request->positionY;
				sourceZ = request->positionZ;
			} else {
				if (state->objectPosition == NULL ||
				    !state->objectPosition(state->context, request->object, &sourceX, &sourceY, &sourceZ))
					continue;
			}
			if (listenerMode == SLIP_SOUND_LISTENER_OBJECT) {
				if (state->objectPosition == NULL ||
				    !state->objectPosition(state->context, listenerObject, &listenerX, &listenerY, &listenerZ))
					continue;
			}
			SlipDraw3D_ApproxAbsVectorLength((uint32_t)listenerX - (uint32_t)sourceX,
			                                 (uint32_t)listenerY - (uint32_t)sourceY,
			                                 (uint32_t)listenerZ - (uint32_t)sourceZ, &approximate);
			distance = approximate.approximateLength;

			queueContinuation = approximate.otherQuarter;
		}
		if ((int32_t)distance <= SLIP_SOUND_MAXIMUM_DISTANCE) {
			SlipSoundEffects_Play(state, request, distance);
			--queueContinuation;
		}
	}
	SlipSoundEffects_UpdateAmbient(state);
}
