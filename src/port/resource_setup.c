#include "resource_setup.h"
#include "runtime.h"

uint32_t SlipResource_allocationReserveBytes;
SlipResourceSetupServices SlipResource_setupServices;
uint32_t SlipResource_installed;
uint32_t SlipResource_reservedExtendedBytes;
uint16_t SlipResource_reservedConventionalParagraphs;
uint32_t SlipResource_unusedInstallationValue;
SlipResourceLoadErrorHandler SlipResource_loadErrorHandler;

void SlipResource_SetLoadErrorHandler(SlipResourceLoadErrorHandler handler) { SlipResource_loadErrorHandler = handler; }

void SlipResource_Initialize(uint16_t handleCount, uint32_t reservedConventionalBytes, uint32_t reservedExtendedBytes,
                             uint32_t unusedValue) {
	SlipResource_reservedExtendedBytes = reservedExtendedBytes;
	SlipResource_unusedInstallationValue = unusedValue;
	if (SlipResource_installed != 0)
		return;
	SlipResource_installed = UINT32_MAX;
	SlipResource_handleCount = handleCount;
	SlipResource_reservedConventionalParagraphs = (uint16_t)((reservedConventionalBytes + 15u) >> 4);
	SlipResource_InitializeHeap(&SlipResource_setupServices.heap);
	SlipResource_InitializeHandles(&SlipResource_setupServices.handles);
	SlipResource_InitializeNames(&SlipResource_setupServices.names);
	SlipResource_callbackCount = 0;
	SlipResource_loadErrorHandler = NULL;
	SlipResource_SetReclaimEnabled(0);
	SlipResource_setupServices.registerExit(SlipResource_Shutdown);
}

void SlipResource_Shutdown(void) {
	if (SlipResource_installed == 0)
		return;
	SlipResource_installed = 0;
	SlipResource_callbackCount = 0;
	SlipResource_ClearHandleIndex();
	SlipResource_ReleaseHeap();
	SlipResource_loadErrorHandler = NULL;
}

void SlipResource_ClearHandleIndex(void) { SlipResource_activeHandles = NULL; }

void SlipResource_ReleaseHeap(void) {
	SlipResource_setupServices.releasePlatform(SlipResource_setupServices.releaseContext);
}

void SlipResource_SetReclaimEnabled(uint32_t enabled) { SlipResource_reclaimEnabled = enabled; }

uint32_t SlipResource_GetReclaimEnabled(void) { return SlipResource_reclaimEnabled; }

void SlipResource_InitializeHeap(const SlipResourceHeapCalls *calls) {
	SlipResource_freeBytes = 0;
	SlipResource_cachedBytes = 0;
	SlipResource_totalBytes = 0;
	SlipResource_allocationReserveBytes = 4096;
	SlipResource_allocatedBlocks.next = &SlipResource_allocatedBlocks;
	SlipResource_allocatedBlocks.previous = &SlipResource_allocatedBlocks;
	SlipResource_freeBlocks.next = &SlipResource_freeBlocks;
	SlipResource_freeBlocks.previous = &SlipResource_freeBlocks;
	calls->resetPlatform(calls->context);
	for (;;) {
		const uint32_t bytes = calls->availableExtended(calls->context);
		if (bytes == 0)
			break;
		SlipResourceRegion region;
		if (!calls->allocateExtended(calls->context, bytes, &region))
			break;
		SlipResource_AddRegion(region.block, region.bytes);
	}
	if (SlipResource_totalBytes == 0)
		SlipRuntime_Fatal("ResInstall - failed to allocate linear memory");
	for (;;) {
		const uint16_t paragraphs = calls->availableConventional(calls->context);
		if (paragraphs == 0)
			return;
		SlipResourceRegion region;
		if (!calls->allocateConventional(calls->context, paragraphs, &region))
			return;
		SlipResource_AddRegion(region.block, region.bytes);
	}
}

void SlipResource_InitializeHandles(const SlipResourceHandleCalls *calls) {
	const uint32_t bytes = (uint32_t)(uint16_t)(SlipResource_handleCount + SLIP_RESOURCE_HANDLE_SENTINEL_COUNT) *
	                       SLIP_RESOURCE_DOS_HANDLE_BYTES;
	SlipResourceHandleAllocation allocation;
	if (!calls->allocate(calls->context, bytes, &allocation))
		SlipRuntime_Fatal("ResIntCreateIndex - Insufficient Memory");
	SlipResource_InitHandleTable(allocation.block, allocation.records);
}

void SlipResource_InitializeNames(const SlipResourceNameAllocationCalls *calls) {
	SlipResource_nameTableCapacity = 40000u;
	SlipResourceNameAllocation allocation;
	if (!calls->allocate(calls->context, SlipResource_nameTableCapacity, &allocation))
		SlipRuntime_Fatal("ResNamesInstall - out of memory");
	allocation.block->flags |= SLIP_RESOURCE_BLOCK_MOVABLE;
	allocation.block->handleByteOffset = UINT32_MAX;
	SlipResource_nameTable = allocation.entries;
	SlipResource_nameTableBlock = allocation.block;
	SlipResource_nameTableUsedBytes = 0;
	/* Typed view of the now-empty table's base+used position. */
	SlipResource_nameTableNext = allocation.entries;
}
