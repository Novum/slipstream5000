#include "resource_platform.h"

enum {
	SLIP_RESOURCE_EXTENDED_ALLOCATION_RETRY_COUNT = 32,
	SLIP_RESOURCE_EXTENDED_PAGE_BYTES = 4096,
	SLIP_RESOURCE_CONVENTIONAL_MIN_PARAGRAPHS = 16,
	SLIP_DOS_MEMORY_DAMAGED = 7
};

/* Retain the original 28-bit range and round down to a page boundary. */
#define SLIP_RESOURCE_EXTENDED_AVAILABLE_BYTES_MASK UINT32_C(0x0ffff000)

uint32_t SlipResource_extendedHandles[SLIP_RESOURCE_EXTENDED_REGION_CAPACITY];
uint16_t SlipResource_extendedCount;
uint16_t SlipResource_conventionalSelectors[SLIP_RESOURCE_CONVENTIONAL_REGION_CAPACITY];
uint16_t SlipResource_conventionalCount;
uint32_t SlipResource_allocationRetries;
uint32_t SlipResource_requestedAllocationBytes;

void SlipResource_ResetPlatform(void *context) {
	(void)context;
	SlipResource_extendedCount = 0;
	SlipResource_conventionalCount = 0;
}

uint32_t SlipResource_AvailableExtended(void *context) {
	const SlipResourceMemoryServices *const services = context;
	uint32_t bytes;
	if (!services->queryLargestExtendedBlock(services->context, &bytes))
		SlipRuntime_Fatal("DOSExtendGetLargest - DOS4GW Int031h function 0500h failed");
	if (bytes < SlipResource_reservedExtendedBytes)
		return 0;
	bytes -= SlipResource_reservedExtendedBytes;
	if (bytes < SlipResource_allocationReserveBytes)
		return 0;
	return (bytes - SlipResource_allocationReserveBytes) & SLIP_RESOURCE_EXTENDED_AVAILABLE_BYTES_MASK;
}

bool SlipResource_AllocateExtended(void *context, uint32_t bytes, SlipResourceRegion *region) {
	const SlipResourceMemoryServices *const services = context;
	if (SlipResource_extendedCount == SLIP_RESOURCE_EXTENDED_REGION_CAPACITY)
		return false;
	SlipResource_requestedAllocationBytes = bytes;
	SlipResourceExtendedAllocation allocation;
	uint32_t attemptedBytes = bytes;
	if (!services->allocateExtended(services->context, attemptedBytes, &allocation)) {
		SlipResource_allocationRetries = SLIP_RESOURCE_EXTENDED_ALLOCATION_RETRY_COUNT;
		for (;;) {
			SlipResource_allocationReserveBytes += SLIP_RESOURCE_EXTENDED_PAGE_BYTES;
			if (SlipResource_requestedAllocationBytes < SlipResource_allocationReserveBytes)
				return false;
			attemptedBytes = SlipResource_requestedAllocationBytes - SlipResource_allocationReserveBytes;
			if (services->allocateExtended(services->context, attemptedBytes, &allocation))
				break;
			if (--SlipResource_allocationRetries == 0)
				return false;
		}
	}
	const uint16_t index = SlipResource_extendedCount++;
	SlipResource_extendedHandles[index] = allocation.handle;
	region->block = allocation.block;
	region->bytes = attemptedBytes;
	return true;
}

uint16_t SlipResource_AvailableConventional(void *context) {
	const SlipResourceMemoryServices *const services = context;
	SlipResourceConventionalAllocation allocation = services->allocateConventional(services->context, UINT16_MAX);
	if (!allocation.failed || allocation.errorCode == SLIP_DOS_MEMORY_DAMAGED)
		SlipRuntime_Fatal("ResInstall - DOS memory damaged");
	uint16_t paragraphs = allocation.availableParagraphs;
	if (paragraphs < SlipResource_reservedConventionalParagraphs)
		paragraphs = 0;
	else
		paragraphs = (uint16_t)(paragraphs - SlipResource_reservedConventionalParagraphs);
	if (paragraphs <= SLIP_RESOURCE_CONVENTIONAL_MIN_PARAGRAPHS)
		paragraphs = 0;
	return paragraphs;
}

bool SlipResource_AllocateConventional(void *context, uint16_t paragraphs, SlipResourceRegion *region) {
	const SlipResourceMemoryServices *const services = context;
	SlipResourceConventionalAllocation allocation = services->allocateConventional(services->context, paragraphs);
	if (allocation.failed)
		return false;
	const uint16_t index = SlipResource_conventionalCount++;
	SlipResource_conventionalSelectors[index] = allocation.selector;
	region->block = allocation.block;
	region->bytes = (uint32_t)paragraphs << SLIP_RESOURCE_PARAGRAPH_SHIFT;
	return true;
}

void SlipResource_FreePlatform(void *context) {
	const SlipResourceMemoryServices *const services = context;
	uint32_t count = SlipResource_extendedCount;
	for (uint32_t index = 0; index < count; ++index) {
		if (!services->freeExtended(services->context, SlipResource_extendedHandles[index]))
			SlipRuntime_Fatal("DOSExtendGetFreeAll - DOS4GW Int031h function 0502h failed");
	}
	count = SlipResource_conventionalCount;
	for (uint32_t index = 0; index < count; ++index) {
		if (services->freeConventional(services->context, SlipResource_conventionalSelectors[index]) ==
		    SLIP_DOS_MEMORY_DAMAGED)
			SlipRuntime_Fatal("DOSExtendFreeAll - DOS memory damaged");
	}
}
