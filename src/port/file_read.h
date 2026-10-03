#ifndef SLIPSTREAM5000_FILE_READ_H
#define SLIPSTREAM5000_FILE_READ_H
#include "archive_format.h"
#include "resource.h"
#include <stdbool.h>
#include <stdint.h>

enum { SLIP_FILE_ARCHIVE_CAPACITY = 3 };

/* Serialized .RES directory entry, read from disk and decrypted in place. */
#pragma pack(push, 1)

typedef struct SlipArchiveEntry {
	uint32_t kind;
	uint8_t name[SLIP_ARCHIVE_INDEX_NAME_BYTES];
	uint32_t offset, length;
} SlipArchiveEntry;

#pragma pack(pop)

typedef struct SlipArchive {
	int32_t file;
	uint16_t indexResource;
	uint32_t count;
} SlipArchive;

extern SlipArchive SlipFile_archives[SLIP_FILE_ARCHIVE_CAPACITY];
extern SlipArchive *SlipFile_primaryArchive, *SlipFile_secondaryArchive;
extern uint8_t SlipArchive_readName[SLIP_RESOURCE_NAME_BYTES], SlipArchive_sizeName[SLIP_RESOURCE_NAME_BYTES];
extern uint8_t *SlipArchive_readDestination;
extern uint32_t SlipArchive_countWord, SlipArchive_indexOffset;

typedef struct SlipFileReadCalls {
	void *context;
	bool (*open)(void *, const char *, int32_t *file);
	bool (*close)(void *, int32_t file);
	bool (*size)(void *, int32_t file, uint32_t *size);
	bool (*read)(void *, int32_t file, int32_t offset, uint8_t *, uint32_t length);
	bool (*seekEnd)(void *, int32_t file, int32_t offset); /* INT 21h/4202 */
	bool (*allocate)(void *, uint32_t bytes, uint32_t flags, uint16_t *resource);
	uint8_t *(*lock)(void *, uint16_t resource);
	void (*unlock)(void *, uint16_t resource);
	void (*release)(void *, uint16_t resource);
} SlipFileReadCalls;

void SlipFile_SetPrimaryArchive(SlipArchive *);
SlipArchive *SlipFile_GetPrimaryArchive(void);

bool SlipArchive_Open(const char *, SlipArchive **, const SlipFileReadCalls *);
void SlipArchive_Close(SlipArchive *, const SlipFileReadCalls *);
bool SlipArchive_Read(SlipArchive *, const char *, uint8_t *destination, const SlipFileReadCalls *);
bool SlipArchive_Size(SlipArchive *, const char *, uint32_t *size, const SlipFileReadCalls *);
bool SlipFile_Read(const char *, uint8_t *destination, const SlipFileReadCalls *);
bool SlipFile_Size(const char *, uint32_t *size, const SlipFileReadCalls *);
#endif
