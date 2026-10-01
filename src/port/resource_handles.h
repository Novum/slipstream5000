#ifndef SLIPSTREAM5000_RESOURCE_HANDLES_H
#define SLIPSTREAM5000_RESOURCE_HANDLES_H
#include "resource.h"

/* Typed views of the allocator's block header and handle-record payload. */
typedef struct SlipResourceHandleAllocation {
	SlipResourceBlock *block;
	SlipResourceHandle *records;
} SlipResourceHandleAllocation;

typedef struct SlipResourceHandleCalls {
	void *context;
	bool (*allocate)(void *, uint32_t bytes, SlipResourceHandleAllocation *allocation);
} SlipResourceHandleCalls;

bool SlipResource_NewHandle(uint32_t *handle, SlipResourceHandle **record, const SlipResourceHandleCalls *calls);
#endif
