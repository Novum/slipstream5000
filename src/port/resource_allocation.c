#include "resource_allocation.h"

bool SlipResource_FindFront(uint32_t requestedBytes, SlipResourceBlock **allocation,
                            const SlipResourceAllocationCalls *calls) {
	const uint32_t alignedBytes = (requestedBytes + SLIP_RESOURCE_ALIGNMENT_LOW_MASK) & SLIP_RESOURCE_ALIGNMENT_MASK;
	if (alignedBytes >= SlipResource_freeBytes)
		return false;
	SlipResourceBlock *block = SlipResource_freeBlocks.next;
	do {
		if (block->capacityBytes > alignedBytes) {
			*allocation = calls->allocateFront(calls->context, block, requestedBytes);
			return true;
		}
		block = block->next;
	} while (block != &SlipResource_freeBlocks);
	return false;
}

bool SlipResource_FindBack(uint32_t requestedBytes, SlipResourceBlock **allocation,
                           const SlipResourceAllocationCalls *calls) {
	const uint32_t alignedBytes = (requestedBytes + SLIP_RESOURCE_ALIGNMENT_LOW_MASK) & SLIP_RESOURCE_ALIGNMENT_MASK;
	if (alignedBytes >= SlipResource_freeBytes)
		return false;
	SlipResourceBlock *block = SlipResource_freeBlocks.previous;
	do {
		if (block->capacityBytes > alignedBytes)
			return calls->allocateBack(calls->context, block, requestedBytes, allocation);
		block = block->previous;
	} while (block != &SlipResource_freeBlocks);
	return false;
}

bool SlipResource_Allocate(uint32_t requestedBytes, SlipResourceBlock **allocation,
                           const SlipResourceAllocationCalls *calls) {
	for (;;) {
		if (SlipResource_FindFront(requestedBytes, allocation, calls))
			return true;
		if (calls->reclaimBlock())
			continue;
		if (calls->compact(requestedBytes))
			continue;
		if (!calls->reclaimUnlocked())
			return false;
	}
}

bool SlipResource_AllocateBackRetry(uint32_t requestedBytes, SlipResourceBlock **allocation,
                                    const SlipResourceAllocationCalls *calls) {
	for (;;) {
		if (SlipResource_FindBack(requestedBytes, allocation, calls))
			return true;
		if (calls->reclaimBlock())
			continue;
		if (calls->compact(requestedBytes))
			continue;
		if (!calls->reclaimUnlocked())
			return false;
	}
}
