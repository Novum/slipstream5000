#include "resource_handles.h"

bool SlipResource_NewHandle(uint32_t *handle, SlipResourceHandle **record, const SlipResourceHandleCalls *calls) {
	for (;;) {
		*record = SlipResource_freeHandles->next;
		if (*record != SlipResource_freeHandles) {
			*handle = SlipResource_TakeAvailableHandle(*record);
			return true;
		}
		const uint16_t oldCount = SlipResource_handleCount;
		SlipResource_handleCount = (uint16_t)(oldCount * 2u);
		const uint32_t bytes = (uint32_t)(uint16_t)(SlipResource_handleCount + SLIP_RESOURCE_HANDLE_SENTINEL_COUNT) *
		                       SLIP_RESOURCE_DOS_HANDLE_BYTES;
		SlipResourceHandleAllocation allocation;
		if (!calls->allocate(calls->context, bytes, &allocation))
			return false;
		SlipResource_RelocateHandles(allocation.block, allocation.records, oldCount, SlipResource_handleTableBlock);
	}
}
