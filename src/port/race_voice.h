#ifndef SLIPSTREAM5000_RACE_VOICE_H
#define SLIPSTREAM5000_RACE_VOICE_H

#include "game_sound.h"
#include "resource.h"
#include "runtime.h"

#include <stdint.h>

typedef struct SlipRaceVoiceRecord {
	char sampleName[14];
	uint16_t resourceHandle;
	const uint8_t *sampleData;
	uint32_t playbackHandle;
	uint32_t speakingDriver;
} SlipRaceVoiceRecord;

extern SlipRaceVoiceRecord SlipRaceVoice_resultsRecords[12];

extern SlipRaceVoiceRecord SlipRaceVoice_alternateRecords[10];

extern SlipRaceVoiceRecord SlipRaceVoice_raceRecords[85];

extern uint32_t SlipRaceVoice_bank;
extern uint32_t SlipRaceVoice_speakingDriver;
extern SlipRaceVoiceRecord *SlipRaceVoice_currentRecord;

extern uint32_t SlipRaceVoice_loadOnDemand;
extern uint32_t SlipRaceVoice_suppressRecent;
extern uint32_t SlipRaceVoice_recentSelections[4];
extern uint32_t SlipRaceVoice_pendingSelection;

typedef struct SlipRaceVoiceCalls {
	void *context;
	uint32_t (*speechEnabled)(void *);
	uint8_t (*languageInitial)(void *);
	bool (*find)(void *, const char *, uint16_t *);
	bool (*load)(void *, const char *, uint16_t *);
	uint32_t (*resourceSize)(void *, uint16_t);
	uint32_t (*available)(void *);
	const uint8_t *(*lock)(void *, uint16_t);
	void (*unlock)(void *, uint16_t);
	void (*release)(void *, uint16_t);
	uint32_t (*play)(void *, const uint8_t *, uint32_t);
	void (*stop)(void *, uint32_t);
	uint32_t (*stopped)(void *, uint32_t);
	void (*error)(void *); /* Original non-returning target. */
} SlipRaceVoiceCalls;

bool SlipRaceVoice_CanPreload(uint32_t digitalCard, uint32_t bank, uint32_t reserve, const SlipRaceVoiceCalls *);
bool SlipRaceVoice_Setup(uint32_t digitalCard, uint32_t bank, uint32_t loadOnDemand, uint32_t suppressRecent,
                         uint32_t reserve, const SlipRaceVoiceCalls *);
void SlipRaceVoice_Play(uint32_t digitalCard, uint32_t selection, const SlipRaceVoiceCalls *);
void SlipRaceVoice_ShutdownWithCalls(uint32_t digitalCard, const SlipRaceVoiceCalls *);
uint32_t SlipRaceVoice_SpeakingDriverWithCalls(uint32_t digitalCard, const SlipRaceVoiceCalls *);

void SlipRaceVoice_Shutdown(SlipGameSoundState *sound);

uint32_t SlipRaceVoice_SpeakingDriver(const SlipGameSoundState *sound);

struct SlipRaceRacerState;

typedef struct SlipRacePositionVoiceCalls {
	void *context;
	SlipRandomState (*randomState)(void *);
	const struct SlipRaceRacerState *(*racer)(void *);
	uint32_t (*random)(void *);
	void (*play)(void *, uint32_t selection);
	void (*restoreRandom)(void *, SlipRandomState);
} SlipRacePositionVoiceCalls;

void SlipRaceVoice_PositionChange(uint16_t minimumPosition, uint16_t previousMinimumPosition,
                                  const SlipRacePositionVoiceCalls *);
#endif
