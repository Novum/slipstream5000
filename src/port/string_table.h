#ifndef SLIPSTREAM5000_STRING_TABLE_H
#define SLIPSTREAM5000_STRING_TABLE_H

#include "resource.h"

#include <stddef.h>

typedef struct SlipStringTableSlot {
	uint16_t resource;
	uint16_t locks;
	const uint8_t *data;
} SlipStringTableSlot;

typedef struct SlipStringTableState {
	SlipStringTableSlot slots[5];
	uint8_t language;
	char filename[13];
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
bool SlipStringTable_Load(SlipStringTableState *state, const char name[8], const SlipStringTableResources *resources,
                          SlipStringTableSlot **slot);
void SlipStringTable_Release(SlipStringTableSlot *slot, const SlipStringTableResources *resources);
const char *SlipStringTable_Get(SlipStringTableSlot *slot, uint32_t tag, const SlipStringTableResources *resources);
void SlipStringTable_Unlock(SlipStringTableSlot *slot, const SlipStringTableResources *resources);

int SlipStringTable_FindText(const SlipResourcePayload *payload, const char tag[4], char *dst, size_t dstSize);

#endif
