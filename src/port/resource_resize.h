#ifndef SLIPSTREAM5000_RESOURCE_RESIZE_H
#define SLIPSTREAM5000_RESOURCE_RESIZE_H
#include "resource_access.h"

typedef struct SlipResourceResizeCalls {
	void *context;
	SlipResourceBlock *(*remainder)(void *, SlipResourceBlock *, uint32_t alignedBytes);
	SlipResourceBlock *(*activate)(SlipResourceHandle *);
	void (*coalesce)(SlipResourceBlock *);
} SlipResourceResizeCalls;

extern uint32_t SlipResource_resizeAlignedBytes, SlipResource_resizeRequestedBytes;
bool SlipResource_Resize(uint16_t resource, uint32_t requestedBytes, const SlipResourceAccessCalls *,
                         const SlipResourceResizeCalls *);
#endif
