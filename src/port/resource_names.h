#ifndef SLIPSTREAM5000_RESOURCE_NAMES_H
#define SLIPSTREAM5000_RESOURCE_NAMES_H
#include "resource_handles.h"
#include <stdbool.h>
#include <stdint.h>

/* Serialized entries contain a handle, byte size, and terminated name. */
enum {
	SLIP_RESOURCE_NAME_ENTRY_HEADER_BYTES = 2 * sizeof(uint16_t),
	SLIP_RESOURCE_NAME_ENTRY_MINIMUM_BYTES = SLIP_RESOURCE_NAME_ENTRY_HEADER_BYTES + 1
};

typedef struct SlipResourceNameEntry {
	uint16_t handle;
	uint16_t byteSize;
	char name[SLIP_RESOURCE_NAME_BUFFER_BYTES]; /* The normalized source name has at most twelve bytes. */
} SlipResourceNameEntry;

extern SlipResourceNameEntry *SlipResource_nameTable;
extern uint32_t SlipResource_nameTableUsedBytes;
extern uint32_t SlipResource_nameTableCapacity;
/* Typed views of base+used and base-0x20; these track the native entry array. */
extern SlipResourceNameEntry *SlipResource_nameTableNext;
extern SlipResourceBlock *SlipResource_nameTableBlock;

typedef struct SlipResourceNameAllocation {
	SlipResourceBlock *block;
	SlipResourceNameEntry *entries;
} SlipResourceNameAllocation;

typedef struct SlipResourceNameAllocationCalls {
	void *context;
	bool (*allocate)(void *, uint32_t bytes, SlipResourceNameAllocation *allocation);
} SlipResourceNameAllocationCalls;

void SlipResource_AddName(const char name[SLIP_RESOURCE_NAME_BUFFER_BYTES], uint16_t *handle,
                          const SlipResourceNameAllocationCalls *storage, const SlipResourceHandleCalls *handles);

extern char SlipResource_validatedName[SLIP_RESOURCE_NAME_BUFFER_BYTES];
extern char SlipResource_searchName[SLIP_RESOURCE_NAME_BUFFER_BYTES];
extern const char *SlipResource_searchNameStart;
extern uint32_t SlipResource_searchNameLength;

typedef struct SlipResourceNameCalls {
	void *context;
	bool (*fileSize)(void *, const char *name, uint32_t *size);
	void (*add)(void *, const char *normalizedName, uint16_t *handle);
} SlipResourceNameCalls;

bool SlipResource_ValidateName(const char name[SLIP_RESOURCE_NAME_BYTES], const SlipResourceNameCalls *calls);
bool SlipResource_FindName(const char name[SLIP_RESOURCE_NAME_BYTES], uint16_t *handle);
bool SlipResource_FindOrCreateName(const char name[SLIP_RESOURCE_NAME_BYTES], uint16_t *handle,
                                   const SlipResourceNameCalls *calls);
#endif
