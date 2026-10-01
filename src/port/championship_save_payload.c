#include "championship_save_payload.h"
#include <stddef.h>

typedef char SaveRacerSizeCheck[sizeof(SlipChampionshipSavedRacer) == 50 ? 1 : -1];
typedef char SaveHeaderSizeCheck[offsetof(SlipChampionshipSavePayload, racers) == 4 ? 1 : -1];

bool SlipChampionshipSave_Pack(const SlipRaceRacerTable *racers, uint32_t stage,
                               const SlipChampionshipSavePayloadCalls *calls,
                               SlipChampionshipSavePayloadResult *result) {
	const uint16_t bytes = (uint16_t)(50u * racers->racerCount + 4u);
	uint16_t resource;
	if (!calls->allocate(calls->context, bytes, 0, &resource))
		return false;
	SlipChampionshipSavePayload *const payload = calls->lock(calls->context, resource);
	payload->racerCount = racers->racerCount;
	payload->stage = (uint16_t)stage;
	for (uint16_t index = 0; index < racers->racerCount; ++index) {
		const SlipRaceRacerState *const source = &racers->records[index];
		SlipChampionshipSavedRacer *const destination = &payload->racers[index];
		destination->tuningIndex = source->tuningIndex;
		destination->racePosition = source->racePosition;
		destination->racerType = source->racerType;
		destination->bonusScore = source->bonusScore;
		destination->championshipPoints = source->championshipPoints;
		destination->championshipPosition = source->championshipPosition;
		destination->movementDamageQ16 = source->movementDamageQ16;
		destination->handlingDamageQ16 = source->handlingDamageQ16;
		destination->primaryWeapon = source->primaryWeaponIndex;
		destination->secondaryWeapon = source->secondaryWeaponIndex;
		destination->primaryAmmo = source->primaryWeaponAmmo;
		destination->secondaryAmmo = source->secondaryWeaponAmmo;
		destination->powerupRecord = source->powerupRecord;
		destination->powerupFlags = source->powerupFlags;
		destination->propulsionProfileIndex = source->propulsionProfileIndex;
	}
	uint16_t checksum = 0;
	const unsigned char *const fileBytes = (const unsigned char *)payload;
	for (uint16_t index = 0; index < bytes; ++index)
		checksum = (uint16_t)(checksum + fileBytes[index]);
	calls->unlock(calls->context, resource);
	*result = (SlipChampionshipSavePayloadResult){resource, checksum, bytes};
	return true;
}
