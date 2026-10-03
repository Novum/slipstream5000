#include "resource_anonymous.h"
#include "runtime.h"

bool SlipResource_AllocateAnonymous(uint32_t bytes, uint32_t flags, uint32_t *handle,
                                    const SlipResourceAllocationCalls *blocks, const SlipResourceHandleCalls *handles) {
	SlipResourceBlock *block;
	if ((flags & SLIP_RESOURCE_ALLOCATE_MOVABLE) != 0) {
		if (!SlipResource_Allocate(bytes, &block, blocks)) {
			SlipRuntime_error = SLIP_RUNTIME_ERROR_MEMORY_EXHAUSTED;
			return false;
		}
		block->flags |= SLIP_RESOURCE_BLOCK_MOVABLE;
	} else {
		if (!SlipResource_AllocateBackRetry(bytes, &block, blocks)) {
			SlipRuntime_error = SLIP_RUNTIME_ERROR_MEMORY_EXHAUSTED;
			return false;
		}
	}
	SlipResource_MoveBlockToTail(block);
	SlipResourceHandle *record;
	if (!SlipResource_NewHandle(handle, &record, handles)) {
		SlipRuntime_error = SLIP_RUNTIME_ERROR_HANDLES_EXHAUSTED;
		return false;
	}
	block->handleByteOffset = (uint32_t)(record - SlipResource_handles) * SLIP_RESOURCE_DOS_HANDLE_BYTES;
	record->block = block;
	record->nameOffset = UINT32_MAX;
	return true;
}
