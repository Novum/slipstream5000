#include "resource_names.h"
#include "resource.h"
#include "runtime.h"
#include <string.h>

SlipResourceNameEntry *SlipResource_nameTable;
uint32_t SlipResource_nameTableUsedBytes;
uint32_t SlipResource_nameTableCapacity;
SlipResourceNameEntry *SlipResource_nameTableNext;
SlipResourceBlock *SlipResource_nameTableBlock;
char SlipResource_validatedName[13];
char SlipResource_searchName[13];
const char *SlipResource_searchNameStart;
uint32_t SlipResource_searchNameLength;

bool SlipResource_ValidateName(const char name[12], const SlipResourceNameCalls *calls) {
	for (unsigned i = 0; i < 12; ++i)
		SlipResource_validatedName[i] = (char)SlipResource_Uppercase((uint8_t)name[i]);
	uint32_t size;
	bool found = calls->fileSize(calls->context, SlipResource_validatedName, &size);
	if (!found)
		SlipRuntime_error = 2;
	return found;
}

bool SlipResource_FindName(const char name[12], uint16_t *handle) {
	for (unsigned i = 0; i < 12; ++i)
		SlipResource_searchName[i] = (char)SlipResource_Uppercase((uint8_t)name[i]);
	if (SlipResource_nameTableUsedBytes == 0)
		return false;
	SlipResource_searchNameStart = SlipResource_searchName;
	const char *character = SlipResource_searchName;
	uint32_t length = 0;
	do {
		++length;
	} while (*character++ != 0);
	SlipResource_searchNameLength = length;
	uint32_t offset = 0;
	SlipResourceNameEntry *entry = SlipResource_nameTable;
	do {
		if ((uint16_t)(entry->byteSize - 4u) == (uint16_t)SlipResource_searchNameLength &&
		    memcmp(entry->name, SlipResource_searchNameStart, SlipResource_searchNameLength) == 0) {
			*handle = entry->handle;
			return true;
		}
		offset += entry->byteSize;
		++entry;
	} while (offset != SlipResource_nameTableUsedBytes);
	return false;
}

bool SlipResource_FindOrCreateName(const char name[12], uint16_t *handle, const SlipResourceNameCalls *calls) {
	if (!SlipResource_ValidateName(name, calls))
		return false;
	if (!SlipResource_FindName(name, handle))
		calls->add(calls->context, SlipResource_searchName, handle);
	return true;
}

void SlipResource_AddName(const char name[13], uint16_t *handle, const SlipResourceNameAllocationCalls *storage,
                          const SlipResourceHandleCalls *handles) {
	const char *character = name;
	uint32_t length = 0;
	do {
		++length;
	} while (*character++ != 0);
	const uint32_t entryBytes = length + 4u;
	if (SlipResource_nameTableUsedBytes + entryBytes >= SlipResource_nameTableCapacity) {
		SlipResource_nameTableCapacity += 20000u;
		SlipResourceNameEntry *const oldEntries = SlipResource_nameTable;
		SlipResourceBlock *const oldBlock = SlipResource_nameTableBlock;
		const size_t count = (size_t)(SlipResource_nameTableNext - oldEntries);
		SlipResourceNameAllocation allocation;
		if (!storage->allocate(storage->context, SlipResource_nameTableCapacity, &allocation))
			SlipRuntime_Fatal("AddResName - out of space");
		allocation.block->flags |= SLIP_RESOURCE_BLOCK_MOVABLE;
		allocation.block->handleByteOffset = UINT32_MAX;
		SlipResource_nameTable = allocation.entries;
		SlipResource_nameTableBlock = allocation.block;
		memcpy(allocation.entries, oldEntries, count * sizeof(*oldEntries));
		SlipResource_nameTableNext = allocation.entries + count;
		SlipResource_ReleaseBlock(oldBlock);
		SlipResource_Coalesce(oldBlock);
	}
	SlipResourceNameEntry *const entry = SlipResource_nameTableNext;
	const uint32_t entryOffset = SlipResource_nameTableUsedBytes;
	SlipResource_nameTableUsedBytes += entryBytes;
	++SlipResource_nameTableNext;
	entry->byteSize = (uint16_t)entryBytes;
	memcpy(entry->name, name, length);

	uint32_t newHandle = (uint32_t)(uintptr_t)(name + length);
	SlipResourceHandle *record;
	SlipResource_NewHandle(&newHandle, &record, handles);
	record->block = NULL;
	entry->handle = (uint16_t)newHandle;
	record->nameOffset = entryOffset;
	*handle = (uint16_t)newHandle;
}
