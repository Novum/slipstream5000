#ifndef SLIPSTREAM5000_CHAMPIONSHIP_SAVE_PAYLOAD_H
#define SLIPSTREAM5000_CHAMPIONSHIP_SAVE_PAYLOAD_H
#include "race.h"

#pragma pack(push, 1)

typedef struct SlipChampionshipSavedRacer {
	uint16_t tuningIndex, racerType;
	uint32_t bonusScore;
	uint16_t championshipPoints, championshipPosition, racePosition;
	uint32_t movementDamageQ16, handlingDamageQ16;
	uint32_t primaryWeapon, secondaryWeapon, primaryAmmo, secondaryAmmo;
	uint32_t powerupRecord, powerupFlags, propulsionProfileIndex;
} SlipChampionshipSavedRacer;

typedef struct SlipChampionshipSavePayload {
	uint16_t racerCount, stage;
	SlipChampionshipSavedRacer racers[];
} SlipChampionshipSavePayload;

#pragma pack(pop)

typedef struct SlipChampionshipSavePayloadCalls {
	void *context;
	bool (*allocate)(void *, uint32_t bytes, uint32_t flags, uint16_t *handle);
	SlipChampionshipSavePayload *(*lock)(void *, uint16_t handle);
	void (*unlock)(void *, uint16_t handle);
} SlipChampionshipSavePayloadCalls;

typedef struct SlipChampionshipSavePayloadResult {
	uint16_t resource, checksum, bytes;
} SlipChampionshipSavePayloadResult;

bool SlipChampionshipSave_Pack(const SlipRaceRacerTable *racers, uint32_t stage,
                               const SlipChampionshipSavePayloadCalls *calls,
                               SlipChampionshipSavePayloadResult *result);
#endif
