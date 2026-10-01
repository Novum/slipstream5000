#ifndef SLIPSTREAM5000_RESOURCE_ALLOCATION_H
#define SLIPSTREAM5000_RESOURCE_ALLOCATION_H
#include "resource.h"

typedef struct SlipResourceAllocationCalls {
	void *context;
	SlipResourceBlock *(*allocateFront)(void *, SlipResourceBlock *, uint32_t requestedBytes);
	bool (*allocateBack)(void *, SlipResourceBlock *, uint32_t requestedBytes, SlipResourceBlock **allocation);
	bool (*reclaimBlock)(void);
	bool (*compact)(uint32_t requestedBytes);
	bool (*reclaimUnlocked)(void);
} SlipResourceAllocationCalls;

bool SlipResource_FindFront(uint32_t requestedBytes, SlipResourceBlock **allocation,
                            const SlipResourceAllocationCalls *calls);
bool SlipResource_FindBack(uint32_t requestedBytes, SlipResourceBlock **allocation,
                           const SlipResourceAllocationCalls *calls);
bool SlipResource_Allocate(uint32_t requestedBytes, SlipResourceBlock **allocation,
                           const SlipResourceAllocationCalls *calls);
bool SlipResource_AllocateBackRetry(uint32_t requestedBytes, SlipResourceBlock **allocation,
                                    const SlipResourceAllocationCalls *calls);
#endif
