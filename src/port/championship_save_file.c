#include "championship_save_file.h"
#include <string.h>

/* Preserve the original two-byte spare tail in the native name-list allocation. */
enum { SLIP_SAVE_NAMES_SPARE_BYTES = 2 };

typedef char
    SaveDirectoryHeaderSizeCheck[sizeof(SlipChampionshipSaveDirectoryHeader) == SLIP_SAVE_DIRECTORY_HEADER_BYTES ? 1
                                                                                                                 : -1];
typedef char
    SaveDirectoryEntrySizeCheck[sizeof(SlipChampionshipSaveDirectoryEntry) == SLIP_SAVE_DIRECTORY_ENTRY_BYTES ? 1 : -1];

void SlipChampionshipSave_InitializeFile(const SlipChampionshipSaveFileCalls *calls) {
	static const SlipChampionshipSaveDirectoryHeader header = {SLIP_SAVE_DIRECTORY_VERSION, SLIP_SAVE_SLOT_COUNT};
	static const SlipChampionshipSaveDirectoryEntry emptyEntry = {0};
	int32_t file;
	if (!calls->open(calls->context, "SLIPSTRM.SAV", SLIP_SAVE_OPEN_READ, &file)) {
		if (!calls->create(calls->context, "SLIPSTRM.SAV", &file))
			return;
		if (calls->write(calls->context, file, 0, &header, sizeof(header))) {
			for (unsigned slot = 0; slot < SLIP_SAVE_SLOT_COUNT; ++slot) {
				if (!calls->write(calls->context, file, -1, &emptyEntry, sizeof(emptyEntry)))
					break;
			}
		}
	}
	(void)calls->close(calls->context, file);
}

SlipChampionshipSaveNames SlipChampionshipSave_LoadNames(const SlipChampionshipSaveDirectoryCalls *calls) {
	SlipChampionshipSaveNames result = {0, 0};
	uint16_t directoryResource;
	if (!calls->load(calls->context, "SLIPSTRM.SAV", &directoryResource))
		return result;

	const SlipChampionshipSaveDirectory *const directory = calls->lockDirectory(calls->context, directoryResource);
	if (directory->header.version == SLIP_SAVE_DIRECTORY_VERSION) {
		result.slotCount = directory->header.slotCount;
		const uint16_t namesBytes = (uint16_t)(SLIP_SAVE_NAME_BYTES * result.slotCount + SLIP_SAVE_NAMES_SPARE_BYTES);
		uint16_t namesResource;
		if (calls->allocate(calls->context, namesBytes, 0, &namesResource)) {
			char *destination = calls->lockNames(calls->context, namesResource);
			for (uint32_t slot = 0; slot < result.slotCount; ++slot) {
				const char *source =
				    directory->slots[slot].payloadOffset != 0 ? directory->slots[slot].name : "[Unused Slot]";
				do {
					*destination++ = *source;
				} while (*source++ != '\0');
			}
			*destination = '\0';
			calls->unlock(calls->context, directoryResource);
			calls->release(calls->context, directoryResource);
			calls->unlock(calls->context, namesResource);
			result.resource = namesResource;
			return result;
		}
	}
	calls->unlock(calls->context, directoryResource);
	calls->release(calls->context, directoryResource);
	return result;
}

bool SlipChampionshipSave_Store(uint16_t slot, const char name[SLIP_SAVE_NAME_BYTES],
                                const SlipChampionshipSaveStoreCalls *calls) {
	const SlipChampionshipSaveFileCalls *const fileCalls = &calls->file;
	const SlipChampionshipSaveDirectoryCalls *const resourceCalls = &calls->directory;
	SlipChampionshipSave_InitializeFile(fileCalls);
	(void)calls->invalidate(calls->context, slot);
	SlipChampionshipSavePayloadResult payload;
	if (!calls->pack(calls->context, &payload))
		return false;
	uint32_t fileBytes;
	if (!calls->size(calls->context, "SLIPSTRM.SAV", &fileBytes))
		return false;
	int32_t file;
	if (!fileCalls->open(fileCalls->context, "SLIPSTRM.SAV", SLIP_SAVE_OPEN_UPDATE, &file))
		return false;
	const SlipChampionshipSavePayload *const serialized = calls->lockPayload(calls->context, payload.resource);
	const uint16_t payloadOffset = (uint16_t)fileBytes;
	const uint32_t updatedBytes = fileBytes + payload.bytes;
	if (!fileCalls->write(fileCalls->context, file, (int32_t)fileBytes, serialized, payload.bytes)) {
		resourceCalls->unlock(resourceCalls->context, payload.resource);
		(void)fileCalls->close(fileCalls->context, file);
		resourceCalls->release(resourceCalls->context, payload.resource);
		return false;
	}
	resourceCalls->unlock(resourceCalls->context, payload.resource);
	if (!fileCalls->close(fileCalls->context, file))
		return false;
	resourceCalls->release(resourceCalls->context, payload.resource);
	uint16_t directoryResource;
	if (!resourceCalls->load(resourceCalls->context, "SLIPSTRM.SAV", &directoryResource))
		return false;

	SlipChampionshipSaveDirectory *const directory =
	    resourceCalls->lockDirectory(resourceCalls->context, directoryResource);
	directory->slots[slot].payloadOffset = payloadOffset;
	directory->slots[slot].checksum = payload.checksum;
	memcpy(directory->slots[slot].name, name, SLIP_SAVE_NAME_BYTES);
	(void)calls->rewrite(calls->context, "SLIPSTRM.SAV", directory, updatedBytes);
	resourceCalls->unlock(resourceCalls->context, directoryResource);
	resourceCalls->release(resourceCalls->context, directoryResource);
	return true;
}

bool SlipChampionshipSave_Load(uint16_t slot, SlipRaceRacerTable *racers, uint32_t *stage,
                               const SlipChampionshipSaveDirectoryCalls *calls) {
	uint16_t resource;
	if (!calls->load(calls->context, "SLIPSTRM.SAV", &resource))
		return false;
	const SlipChampionshipSaveDirectory *const directory = calls->lockDirectory(calls->context, resource);
	bool loaded = false;
	if (directory->header.version == SLIP_SAVE_DIRECTORY_VERSION &&
	    (int16_t)slot < (int16_t)directory->header.slotCount) {
		const uint16_t offset = directory->slots[slot].payloadOffset;
		if (offset != 0) {
			const SlipChampionshipSavePayload *const payload = (const void *)((const uint8_t *)directory + offset);
			racers->racerCount = payload->racerCount;
			*stage = payload->stage;
			for (uint16_t i = 0; i < payload->racerCount; ++i) {
				const SlipChampionshipSavedRacer *const source = &payload->racers[i];
				SlipRaceRacerState *const destination = &racers->records[i];
				destination->tuningIndex = source->tuningIndex;
				destination->racePosition = source->racePosition;
				destination->racerType = source->racerType;
				destination->bonusScore = source->bonusScore;
				destination->championshipPoints = source->championshipPoints;
				destination->championshipPosition = source->championshipPosition;
				destination->movementDamageQ16 = source->movementDamageQ16;
				destination->handlingDamageQ16 = source->handlingDamageQ16;
				destination->primaryWeaponIndex = source->primaryWeapon;
				destination->secondaryWeaponIndex = source->secondaryWeapon;
				destination->primaryWeaponAmmo = source->primaryAmmo;
				destination->secondaryWeaponAmmo = source->secondaryAmmo;
				destination->powerupRecord = source->powerupRecord;
				destination->powerupFlags = source->powerupFlags;
				destination->propulsionProfileIndex = source->propulsionProfileIndex;
			}
			loaded = true;
		}
	}
	calls->unlock(calls->context, resource);
	calls->release(calls->context, resource);
	return loaded;
}
