#ifndef SLIPSTREAM5000_RESOURCE_LOAD_H
#define SLIPSTREAM5000_RESOURCE_LOAD_H
#include "resource.h"

typedef struct SlipResourceLoadCalls {
	void *context;
	bool (*fileSize)(void *, uint32_t nameOffset, uint32_t *size);
	bool (*allocate)(void *, uint32_t size, SlipResourceBlock **block);
	bool (*readFile)(void *, uint32_t nameOffset, SlipResourceBlock *destination);
	void (*loaded)(void *, SlipResourceHandle *record);
} SlipResourceLoadCalls;

bool SlipResource_LoadRecord(SlipResourceHandle *record, const SlipResourceLoadCalls *calls);
#endif
