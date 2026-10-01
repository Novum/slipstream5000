#include "resource_resize.h"
#include "runtime.h"

uint32_t SlipResource_resizeAlignedBytes, SlipResource_resizeRequestedBytes;

bool SlipResource_Resize(uint16_t resource, uint32_t requestedBytes, const SlipResourceAccessCalls *access,
                         const SlipResourceResizeCalls *calls) {
	SlipResource_resizeRequestedBytes = requestedBytes;
	SlipResource_resizeAlignedBytes =
	    (requestedBytes + SLIP_RESOURCE_ALIGNMENT_LOW_MASK) & SLIP_RESOURCE_ALIGNMENT_MASK;
	if (resource == 0)
		SlipRuntime_Fatal("ResResize called with zero resource ID");
	SlipResourceHandle *const handle = &SlipResource_handles[resource];
	SlipResourceBlock *block = handle->block;
	if (block == NULL) {
		if (!access->load(access->context, handle)) {
			if (access->loadError != NULL)
				access->loadError(access->context, handle);
			else
				SlipRuntime_Fatal("ResResize - failed because LoadFile was unable to purge sufficient memory");
		}
		block = handle->block;
	} else {
		block = calls->activate(handle);
	}
	if (block->requestedBytes <= SlipResource_resizeAlignedBytes)
		return false;
	uint32_t remainingBytes = block->capacityBytes - SlipResource_resizeAlignedBytes;
	if (remainingBytes >= SLIP_RESOURCE_MIN_SPLIT_BYTES) {
		SlipResourceBlock *const remainder = calls->remainder(calls->context, block, SlipResource_resizeAlignedBytes);
		SlipResourceBlock *const next = block->physicalNext;
		if (next != NULL)
			next->physicalPrevious = remainder;
		remainingBytes -= SLIP_RESOURCE_DOS_BLOCK_HEADER_BYTES;
		remainder->capacityBytes = remainingBytes;
		SlipResourceBlock *const first = SlipResource_freeBlocks.next;
		SlipResourceBlock *const following = first->next;
		first->next = remainder;
		remainder->previous = first;
		following->previous = remainder;
		remainder->next = following;
		SlipResource_freeBytes += remainder->capacityBytes + SLIP_RESOURCE_DOS_BLOCK_HEADER_BYTES;
		remainder->physicalPrevious = block;
		remainder->physicalNext = next;
		remainder->flags = 0;
		block->physicalNext = remainder;
		block->capacityBytes = SlipResource_resizeAlignedBytes;
		block->requestedBytes = SlipResource_resizeRequestedBytes;
		calls->coalesce(remainder);
	}
	return true;
}
