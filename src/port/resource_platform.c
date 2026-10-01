#include "resource_platform.h"

uint32_t SlipResource_extendedHandles[16];
uint16_t SlipResource_extendedCount;
uint16_t SlipResource_conventionalSelectors[16];
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
	return (bytes - SlipResource_allocationReserveBytes) & 0x0ffff000u;
}

bool SlipResource_AllocateExtended(void *context, uint32_t bytes, SlipResourceRegion *region) {
	const SlipResourceMemoryServices *const services = context;
	if (SlipResource_extendedCount == 16)
		return false;
	SlipResource_requestedAllocationBytes = bytes;
	SlipResourceExtendedAllocation allocation;
	uint32_t attemptedBytes = bytes;
	if (!services->allocateExtended(services->context, attemptedBytes, &allocation)) {
		SlipResource_allocationRetries = 32;
		for (;;) {
			SlipResource_allocationReserveBytes += 4096;
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
	SlipResourceConventionalAllocation allocation = services->allocateConventional(services->context, 0xffff);
	if (!allocation.failed || allocation.errorCode == 7)
		SlipRuntime_Fatal("ResInstall - DOS memory damaged");
	uint16_t paragraphs = allocation.availableParagraphs;
	if (paragraphs < SlipResource_reservedConventionalParagraphs)
		paragraphs = 0;
	else
		paragraphs = (uint16_t)(paragraphs - SlipResource_reservedConventionalParagraphs);
	if (paragraphs <= 16)
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
	region->bytes = (uint32_t)paragraphs << 4;
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
		if (services->freeConventional(services->context, SlipResource_conventionalSelectors[index]) == 7)
			SlipRuntime_Fatal("DOSExtendFreeAll - DOS memory damaged");
	}
}
