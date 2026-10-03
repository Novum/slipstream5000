#include "resource_names.h"
#include "resource.h"
#include "runtime.h"
#include <string.h>

/* byteSize accounts for two serialized words followed by the terminated name.
 * Native entries are stored separately using sizeof(SlipResourceNameEntry). */
enum { SLIP_RESOURCE_NAME_TABLE_GROWTH_BYTES = 20000 };

SlipResourceNameEntry *SlipResource_nameTable;
uint32_t SlipResource_nameTableUsedBytes;
uint32_t SlipResource_nameTableCapacity;
SlipResourceNameEntry *SlipResource_nameTableNext;
SlipResourceBlock *SlipResource_nameTableBlock;
char SlipResource_validatedName[SLIP_RESOURCE_NAME_BUFFER_BYTES];
char SlipResource_searchName[SLIP_RESOURCE_NAME_BUFFER_BYTES];
const char *SlipResource_searchNameStart;
uint32_t SlipResource_searchNameLength;

bool SlipResource_ValidateName(const char name[SLIP_RESOURCE_NAME_BYTES], const SlipResourceNameCalls *calls) {
	for (unsigned i = 0; i < SLIP_RESOURCE_NAME_BYTES; ++i)
		SlipResource_validatedName[i] = (char)SlipResource_Uppercase((uint8_t)name[i]);
	uint32_t size;
	bool found = calls->fileSize(calls->context, SlipResource_validatedName, &size);
	if (!found)
		SlipRuntime_error = SLIP_RUNTIME_ERROR_FILE_UNAVAILABLE;
	return found;
}

bool SlipResource_FindName(const char name[SLIP_RESOURCE_NAME_BYTES], uint16_t *handle) {
	for (unsigned i = 0; i < SLIP_RESOURCE_NAME_BYTES; ++i)
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
		if ((uint16_t)(entry->byteSize - SLIP_RESOURCE_NAME_ENTRY_HEADER_BYTES) ==
		        (uint16_t)SlipResource_searchNameLength &&
		    memcmp(entry->name, SlipResource_searchNameStart, SlipResource_searchNameLength) == 0) {
			*handle = entry->handle;
			return true;
		}
		offset += entry->byteSize;
		++entry;
	} while (offset != SlipResource_nameTableUsedBytes);
	return false;
}

bool SlipResource_FindOrCreateName(const char name[SLIP_RESOURCE_NAME_BYTES], uint16_t *handle,
                                   const SlipResourceNameCalls *calls) {
	if (!SlipResource_ValidateName(name, calls))
		return false;
	if (!SlipResource_FindName(name, handle))
		calls->add(calls->context, SlipResource_searchName, handle);
	return true;
}

void SlipResource_AddName(const char name[SLIP_RESOURCE_NAME_BUFFER_BYTES], uint16_t *handle,
                          const SlipResourceNameAllocationCalls *storage, const SlipResourceHandleCalls *handles) {
	const char *character = name;
	uint32_t length = 0;
	do {
		++length;
	} while (*character++ != 0);
	const uint32_t entryBytes = length + SLIP_RESOURCE_NAME_ENTRY_HEADER_BYTES;
	if (SlipResource_nameTableUsedBytes + entryBytes >= SlipResource_nameTableCapacity) {
		SlipResource_nameTableCapacity += SLIP_RESOURCE_NAME_TABLE_GROWTH_BYTES;
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
