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
	SLIP_SOUND_ENGINE_COUNT = 2,
	SLIP_SOUND_EFFECT_TRACK_LOW_INDEX = 12,
	SLIP_SOUND_EFFECT_TRACK_HIGH_INDEX = 18
};

/* Queue IDs map to samples through resourceIndices in sound_effects.c. */
typedef enum SlipSoundEffect {
	SLIP_SOUND_EFFECT_NONE = 0,
	SLIP_SOUND_EFFECT_CRASH = 1,
	SLIP_SOUND_EFFECT_SCRAPE_2 = 2,
	SLIP_SOUND_EFFECT_SCRAPE_1 = 3,
	SLIP_SOUND_EFFECT_BLASTER = 4,
	SLIP_SOUND_EFFECT_MISSILE = 5,
	SLIP_SOUND_EFFECT_BONUS_COLLECT = 6,
	SLIP_SOUND_EFFECT_MINE_DROP = 7,
	SLIP_SOUND_EFFECT_WATER_HIT = 8,
	SLIP_SOUND_EFFECT_EXPLOSION = 9,
	SLIP_SOUND_EFFECT_LASER_HIT = 10,
	SLIP_SOUND_EFFECT_DISRUPTOR = 11,
	SLIP_SOUND_EFFECT_ENGINE_START = 12,
	SLIP_SOUND_EFFECT_BOMBER = 13,
	SLIP_SOUND_EFFECT_SCRAMBLE = 14,
	SLIP_SOUND_EFFECT_HYPERNEU = 15,
	SLIP_SOUND_EFFECT_AMBLER = 16,
	SLIP_SOUND_EFFECT_COUNT = 16
} SlipSoundEffect;

typedef enum SlipSoundPositionMode {
	SLIP_SOUND_POSITION_NONE = 0,
	SLIP_SOUND_POSITION_OBJECT = 1,
	SLIP_SOUND_POSITION_COORDINATES = 2
} SlipSoundPositionMode;

typedef enum SlipSoundTrackChangeMode {
	SLIP_SOUND_TRACK_CHANGE_PLAY = 0,
	SLIP_SOUND_TRACK_CHANGE_SUPPRESS = 2
} SlipSoundTrackChangeMode;

typedef enum SlipSoundListenerMode {
	SLIP_SOUND_LISTENER_COORDINATES = 0,
	SLIP_SOUND_LISTENER_OBJECT = 1
} SlipSoundListenerMode;

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
	} engines[SLIP_SOUND_ENGINE_COUNT];

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
