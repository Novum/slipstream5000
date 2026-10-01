#ifndef SLIPSTREAM5000_SOUND_EFFECTS_H
#define SLIPSTREAM5000_SOUND_EFFECTS_H

#include "game_sound.h"
#include "resource.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
	SLIP_SOUND_EFFECT_RESOURCE_COUNT = 24,
	SLIP_SOUND_EFFECT_QUEUE_COUNT = 20,
	SLIP_SOUND_EFFECT_TRACK_LOW_INDEX = 12,
	SLIP_SOUND_EFFECT_TRACK_HIGH_INDEX = 18
};

typedef struct SlipSoundEffectRequest {
	uint32_t effect;
	uint16_t positionMode;
	uint16_t object;
	int32_t positionX;
	int32_t positionY;
	int32_t positionZ;
} SlipSoundEffectRequest;

typedef bool (*SlipSoundEffectObjectPosition)(void *context, uint16_t object, int32_t *x, int32_t *y, int32_t *z);
typedef bool (*SlipSoundEffectTrackLight)(void *context, uint16_t object, uint16_t *light);
typedef bool (*SlipSoundEffectLock)(void *context);
typedef void (*SlipSoundEffectUnlock)(void *context);

typedef struct SlipSoundEffectsState {
	bool initialized;
	SlipGameSoundState *gameSound;
	uint16_t resources[SLIP_SOUND_EFFECT_RESOURCE_COUNT];
	SlipResourcePayload samplePayloads[SLIP_SOUND_EFFECT_RESOURCE_COUNT];
	SlipSoundEffectRequest requests[SLIP_SOUND_EFFECT_QUEUE_COUNT];

	struct {
		uint16_t object;
		uint16_t trackChangeMode;
		uint32_t trackLight;
		int32_t speed;
		int32_t steeringInput;
		uint16_t frameSubmissionCount;
		uint32_t loopHandle;
	} engines[2];

	uint32_t ambientRequested;
	uint32_t ambientCurrent;
	uint32_t ambientHandle;
	uint32_t ambientVolume;
	uint32_t engineLoopsEnabled;
	SlipSoundEffectObjectPosition objectPosition;
	SlipSoundEffectTrackLight trackLight;
	SlipSoundEffectLock lockSound;
	SlipSoundEffectUnlock unlockSound;
	void *context;
} SlipSoundEffectsState;

extern uint32_t SlipSoundEffects_externalSoundFiles;
void SlipSoundEffects_Install(void);
void SlipSoundEffects_ReleaseTrackSamples(SlipSoundEffectsState *);
bool SlipSoundEffects_Initialize(SlipSoundEffectsState *state, const char *const *archives, size_t archiveCount,
                                 uint16_t track, uint16_t soundSet, SlipGameSoundState *gameSound,
                                 SlipSoundEffectObjectPosition objectPosition, SlipSoundEffectTrackLight trackLight,
                                 SlipSoundEffectLock lockSound, SlipSoundEffectUnlock unlockSound, void *context);
void SlipSoundEffects_Shutdown(SlipSoundEffectsState *state);
void SlipSoundEffects_BeginFrame(SlipSoundEffectsState *state);
void SlipSoundEffects_EnableEngineLoops(SlipSoundEffectsState *state);
void SlipSoundEffects_StopAmbient(SlipSoundEffectsState *state);
void SlipSoundEffects_AddEngine(SlipSoundEffectsState *state, uint16_t trackChangeMode, int32_t speed,
                                int32_t steeringInput, uint16_t object);
void SlipSoundEffects_Queue(SlipSoundEffectsState *state, int32_t positionX, int32_t positionY, int32_t positionZ,
                            uint32_t effect, uint16_t object, uint16_t positionMode);
void SlipSoundEffects_SetAmbient(SlipSoundEffectsState *state, uint32_t ambient);
void SlipSoundEffects_PlayLow(SlipSoundEffectsState *state);
void SlipSoundEffects_PlayHigh(SlipSoundEffectsState *state);
void SlipSoundEffects_PlayFlyby(SlipSoundEffectsState *state);
void SlipSoundEffects_EndFrame(SlipSoundEffectsState *state, uint32_t listenerMode, int32_t listenerX,
                               int32_t listenerY, int32_t listenerZ, uint16_t listenerObject,
                               uint16_t secondListenerObject);

#endif
