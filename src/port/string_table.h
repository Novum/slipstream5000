#ifndef SLIPSTREAM5000_STRING_TABLE_H
#define SLIPSTREAM5000_STRING_TABLE_H

#include "resource.h"

#include <stddef.h>

enum {
	SLIP_STRING_TABLE_SLOT_COUNT = 5,
	SLIP_STRING_TABLE_TAG_BYTES = sizeof(uint32_t),
	SLIP_STRING_TABLE_LENGTH_OFFSET = SLIP_STRING_TABLE_TAG_BYTES,
	SLIP_STRING_TABLE_TEXT_OFFSET = SLIP_STRING_TABLE_LENGTH_OFFSET + sizeof(uint16_t)
};

typedef struct SlipStringTableSlot {
	uint16_t resource;
	uint16_t locks;
	const uint8_t *data;
} SlipStringTableSlot;

typedef struct SlipStringTableState {
	SlipStringTableSlot slots[SLIP_STRING_TABLE_SLOT_COUNT];
	uint8_t language;
	char filename[SLIP_RESOURCE_NAME_BUFFER_BYTES];
} SlipStringTableState;

typedef struct SlipStringTableResources {
	void *context;
	bool (*load)(void *context, const char *name, uint16_t *resource);
	const uint8_t *(*lock)(void *context, uint16_t resource);
	void (*unlock)(void *context, uint16_t resource);
	void (*release)(void *context, uint16_t resource);
} SlipStringTableResources;

extern SlipStringTableState SlipStringTable_state;
void SlipStringTable_SetLanguage(SlipStringTableState *state, uint8_t language);
bool SlipStringTable_Load(SlipStringTableState *state, const char name[SLIP_RESOURCE_BASE_NAME_BYTES],
                          const SlipStringTableResources *resources, SlipStringTableSlot **slot);
void SlipStringTable_Release(SlipStringTableSlot *slot, const SlipStringTableResources *resources);
const char *SlipStringTable_Get(SlipStringTableSlot *slot, uint32_t tag, const SlipStringTableResources *resources);
void SlipStringTable_Unlock(SlipStringTableSlot *slot, const SlipStringTableResources *resources);

int SlipStringTable_FindText(const SlipResourcePayload *payload, const char tag[SLIP_STRING_TABLE_TAG_BYTES], char *dst,
                             size_t dstSize);

#endif
