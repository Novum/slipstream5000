#ifndef SLIPSTREAM5000_RESOURCE_NAMES_H
#define SLIPSTREAM5000_RESOURCE_NAMES_H
#include "resource_handles.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct SlipResourceNameEntry {
	uint16_t handle;
	uint16_t byteSize;
	char name[13]; /* The normalized source name has at most twelve bytes. */
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

void SlipResource_AddName(const char name[13], uint16_t *handle, const SlipResourceNameAllocationCalls *storage,
                          const SlipResourceHandleCalls *handles);

extern char SlipResource_validatedName[13];
extern char SlipResource_searchName[13];
extern const char *SlipResource_searchNameStart;
extern uint32_t SlipResource_searchNameLength;

typedef struct SlipResourceNameCalls {
	void *context;
	bool (*fileSize)(void *, const char *name, uint32_t *size);
	void (*add)(void *, const char *normalizedName, uint16_t *handle);
} SlipResourceNameCalls;

bool SlipResource_ValidateName(const char name[12], const SlipResourceNameCalls *calls);
bool SlipResource_FindName(const char name[12], uint16_t *handle);
bool SlipResource_FindOrCreateName(const char name[12], uint16_t *handle, const SlipResourceNameCalls *calls);
#endif
