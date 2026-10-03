#ifndef SLIPSTREAM5000_CHAMPIONSHIP_SAVE_FILE_H
#define SLIPSTREAM5000_CHAMPIONSHIP_SAVE_FILE_H
#include "championship_save_format.h"
#include "championship_save_payload.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum SlipChampionshipSaveOpenMode {
	SLIP_SAVE_OPEN_READ = 0,
	SLIP_SAVE_OPEN_UPDATE = 1
} SlipChampionshipSaveOpenMode;

/* SLIPSTRM.SAV disk directory. Native game state does not use this layout. */
#pragma pack(push, 1)

typedef struct SlipChampionshipSaveDirectoryHeader {
	uint16_t version, slotCount;
} SlipChampionshipSaveDirectoryHeader;

typedef struct SlipChampionshipSaveDirectoryEntry {
	uint16_t payloadOffset;
	char name[SLIP_SAVE_NAME_BYTES];
	uint16_t checksum;
} SlipChampionshipSaveDirectoryEntry;

typedef struct SlipChampionshipSaveDirectory {
	SlipChampionshipSaveDirectoryHeader header;
	SlipChampionshipSaveDirectoryEntry slots[];
} SlipChampionshipSaveDirectory;

#pragma pack(pop)

typedef struct SlipChampionshipSaveFileCalls {
	void *context;
	bool (*open)(void *, const char *path, uint8_t mode, int32_t *file);
	bool (*create)(void *, const char *path, int32_t *file);
	bool (*write)(void *, int32_t file, int32_t offset, const void *serialized, uint16_t bytes);
	bool (*close)(void *, int32_t file);
} SlipChampionshipSaveFileCalls;

void SlipChampionshipSave_InitializeFile(const SlipChampionshipSaveFileCalls *calls);

typedef struct SlipChampionshipSaveDirectoryCalls {
	void *context;
	bool (*load)(void *, const char *path, uint16_t *resource);
	SlipChampionshipSaveDirectory *(*lockDirectory)(void *, uint16_t resource);
	bool (*allocate)(void *, uint32_t bytes, uint32_t flags, uint16_t *resource);
	char *(*lockNames)(void *, uint16_t resource);
	void (*unlock)(void *, uint16_t resource);
	void (*release)(void *, uint16_t resource);
} SlipChampionshipSaveDirectoryCalls;

typedef struct SlipChampionshipSaveNames {
	uint16_t resource, slotCount;
} SlipChampionshipSaveNames;

SlipChampionshipSaveNames SlipChampionshipSave_LoadNames(const SlipChampionshipSaveDirectoryCalls *calls);

typedef struct SlipChampionshipSaveStoreCalls {
	SlipChampionshipSaveFileCalls file;
	SlipChampionshipSaveDirectoryCalls directory;
	void *context;
	bool (*invalidate)(void *, uint16_t slot);
	bool (*pack)(void *, SlipChampionshipSavePayloadResult *result);
	bool (*size)(void *, const char *path, uint32_t *bytes);
	const SlipChampionshipSavePayload *(*lockPayload)(void *, uint16_t resource);
	bool (*rewrite)(void *, const char *path, const SlipChampionshipSaveDirectory *, uint32_t bytes);
} SlipChampionshipSaveStoreCalls;

bool SlipChampionshipSave_Store(uint16_t slot, const char name[SLIP_SAVE_NAME_BYTES],
                                const SlipChampionshipSaveStoreCalls *calls);
bool SlipChampionshipSave_Load(uint16_t slot, SlipRaceRacerTable *racers, uint32_t *stage,
                               const SlipChampionshipSaveDirectoryCalls *calls);
#endif
