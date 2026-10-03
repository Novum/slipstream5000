#include "resource.h"
#include "archive_format.h"
#include "host_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	/* DispatchLoaded can scan three extension bytes beyond a dotless name. */
	SLIP_RES_RESIDENT_NAME_BUFFER_BYTES = SLIP_RESOURCE_NAME_BUFFER_BYTES + SLIP_RESOURCE_EXTENSION_BYTES,
	SLIP_RES_EXTENSION_LENGTH = SLIP_RESOURCE_EXTENSION_BYTES,
	SLIP_RES_EXTENSION_CHARACTER_BITS = 8
};

#define SLIP_RES_EXTENSION_SPACE_PADDING UINT32_C(0x20202020)

static const uint8_t kIndexXorKey[SLIP_ARCHIVE_INDEX_NAME_BYTES] = {'S', 'O', 'F', 'T', 'W', 'A', 'R', 'E',
                                                                    'R', 'E', 'F', 'I', 'N', 'E', 'R', 'Y'};

uint8_t SlipResource_Uppercase(uint8_t character) {
	if (character >= 'a' && character <= 'z') {
		character -= 'a' - 'A';
	}
	return character;
}

uint32_t SlipResource_ExtensionKey(uint32_t extension) {
	uint32_t key = 0;
	for (unsigned i = 0; i < 4; ++i) {
		const uint8_t character = (uint8_t)(extension >> (24u - 8u * i));
		key |= (uint32_t)SlipResource_Uppercase(character) << (8u * i);
	}
	return key;
}

static void SlipResource_PackResourceName(const char *name, uint8_t packed[SLIP_RESOURCE_NAME_BYTES]) {
	size_t i;
	size_t out = 0;
	const char *const dot = strchr(name, '.');

	memset(packed, ' ', SLIP_RESOURCE_NAME_BYTES);
	for (i = 0; name[i] != '\0' && name + i != dot && out < SLIP_RESOURCE_BASE_NAME_BYTES; ++i) {
		packed[out++] = SlipResource_Uppercase((uint8_t)name[i]);
	}

	if (dot != NULL) {
		out = SLIP_RESOURCE_BASE_NAME_BYTES;
		packed[out++] = '.';
		for (i = 1; dot[i] != '\0' && out < SLIP_RESOURCE_NAME_BYTES; ++i) {
			packed[out++] = SlipResource_Uppercase((uint8_t)dot[i]);
		}
	}
}

void SlipResource_ReleaseHandle(SlipResourcePayload *payload) {
	if (payload != NULL) {
		if (payload->ownsData) {
			free(payload->data);
		}
		payload->data = NULL;
		payload->size = 0;
		payload->ownsData = false;
	}
}

void SlipResource_ReleaseSequence(SlipResourcePayload *payloads, uint16_t count) {
	while (count != 0u) {
		SlipResource_ReleaseHandle(payloads);
		++payloads;
		--count;
	}
}

typedef struct SlipResourceNameRecord {
	char name[SLIP_RES_RESIDENT_NAME_BUFFER_BYTES];
	SlipResourcePayload payload;
	SlipResourceHandle handle;
	SlipResourceBlock block;
} SlipResourceNameRecord;

static SlipResourceNameRecord **g_resourceNameRecords;
static size_t g_resourceNameRecordCount;

SlipResourceCallback SlipResource_callbacks[SLIP_RESOURCE_CALLBACK_CAPACITY];
uint32_t SlipResource_callbackCount;

uint32_t SlipResource_visitExtension;
SlipResourceResidentCallback SlipResource_residentCallback;
const char *SlipResource_residentName;

void SlipResource_VisitResident(uint32_t extension, SlipResourceResidentCallback callback,
                                const SlipResourceResidentCalls *calls) {
	SlipResource_visitExtension = SlipResource_ExtensionKey(extension);
	SlipResource_residentCallback = callback;
	SlipResourceHandle *record = *calls->activeHandles;
	for (;;) {
		record = record->next;
		if (record == *calls->activeHandles)
			return;
		if (record->nameOffset == UINT32_MAX || record->block == NULL)
			continue;
		SlipResource_residentName = calls->name(calls->context, record->nameOffset);
		if (!SlipResource_ResidentExtension(SlipResource_residentName))
			continue;
		if (SlipResource_visitExtension != SlipResource_residentExtension)
			continue;
		uint8_t *const payload = calls->payload(calls->context, record->block);
		SlipResource_residentCallback(SlipResource_residentName, payload);
	}
}

uint32_t SlipResource_loadExtension;

SlipResourceHandle *SlipResource_loadingRecord;

void SlipResource_DispatchLoaded(SlipResourceHandle *record, const char *name, struct SlipShape3DHeader *shape) {
	SlipResource_loadingRecord = record;
	if (record->nameOffset == UINT32_MAX)
		return;
	const char *character = name;
	for (unsigned i = 0; i < SLIP_RESOURCE_NAME_BYTES; ++i) {
		if (*character++ == '.')
			break;
	}
	SlipResource_loadExtension = SLIP_RES_EXTENSION_SPACE_PADDING;
	for (unsigned i = 0; i < SLIP_RES_EXTENSION_LENGTH; ++i) {
		const uint8_t value = SlipResource_Uppercase((uint8_t)*character++);
		const unsigned shift = i * SLIP_RES_EXTENSION_CHARACTER_BITS;
		SlipResource_loadExtension =
		    (SlipResource_loadExtension & ~((uint32_t)UINT8_MAX << shift)) | ((uint32_t)value << shift);
		if (value == '\0')
			break;
	}
	for (uint32_t i = 0; i < SlipResource_callbackCount; ++i) {
		if (SlipResource_callbacks[i].extensionKey == SlipResource_loadExtension) {
			const SlipResourceLoadedCallback callback = SlipResource_callbacks[i].loadedCallback;
			if (callback != NULL)
				callback(shape);
			return;
		}
	}
}

static SlipResourceHandle cachedSentinel = {.next = &cachedSentinel, .previous = &cachedSentinel};
static SlipResourceHandle *cachedHandles = &cachedSentinel;

static const char *SlipResource_CachedName(void *context, uint32_t offset) {
	(void)context;
	return g_resourceNameRecords[offset]->name;
}

static uint8_t *SlipResource_CachedPayload(void *context, SlipResourceBlock *block) {
	(void)context;
	return g_resourceNameRecords[block->handleByteOffset / SLIP_RESOURCE_DOS_HANDLE_BYTES]->payload.data;
}

const SlipResourceResidentCalls SlipResource_cachedResidentCalls = {
    .activeHandles = &cachedHandles, .name = SlipResource_CachedName, .payload = SlipResource_CachedPayload};

static SlipResourcePayload *SlipResource_NameTableFind(const char *name) {

	char normalized[SLIP_RESOURCE_NAME_BUFFER_BYTES] = {0};
	size_t i;
	for (i = 0; i < SLIP_RESOURCE_NAME_BYTES && name[i] != '\0'; ++i) {
		normalized[i] = (char)SlipResource_Uppercase((uint8_t)name[i]);
	}

	for (i = 0; i < g_resourceNameRecordCount; ++i) {
		if (strncmp(g_resourceNameRecords[i]->name, normalized, sizeof(g_resourceNameRecords[i]->name) - 1u) == 0) {
			return &g_resourceNameRecords[i]->payload;
		}
	}
	return NULL;
}

static int SlipResource_NameTableAdd(const char *name, const SlipResourcePayload *payload) {
	SlipResourceNameRecord *entry;
	SlipResourceNameRecord **records;

	if (strlen(name) > SLIP_RESOURCE_NAME_BYTES) {
		return 0;
	}
	records =
	    (SlipResourceNameRecord **)realloc(g_resourceNameRecords, (g_resourceNameRecordCount + 1u) * sizeof(*records));
	if (records == NULL) {
		return 0;
	}
	g_resourceNameRecords = records;
	entry = calloc(1, sizeof(*entry));
	if (entry == NULL)
		return 0;
	const uint32_t index = (uint32_t)g_resourceNameRecordCount;
	g_resourceNameRecords[g_resourceNameRecordCount++] = entry;
	entry->handle.nameOffset = index;
	entry->handle.block = &entry->block;
	entry->block.handleByteOffset = index * SLIP_RESOURCE_DOS_HANDLE_BYTES;
	entry->handle.next = &cachedSentinel;
	entry->handle.previous = cachedSentinel.previous;
	cachedSentinel.previous->next = &entry->handle;
	cachedSentinel.previous = &entry->handle;

	const size_t length = strlen(name);
	for (size_t i = 0; i < length; ++i) {
		entry->name[i] = (char)SlipResource_Uppercase((uint8_t)name[i]);
	}
	entry->name[length] = '\0';
	entry->payload = *payload;
	SlipResource_DispatchLoaded(&entry->handle, entry->name, (struct SlipShape3DHeader *)entry->payload.data);
	return 1;
}

int SlipResource_LoadWildcardSequence(const char *const *archives, size_t archiveCount, const char *pattern,
                                      uint32_t firstIndex, uint16_t count, SlipResourcePayload *payloads) {
	char expandedName[SLIP_RESOURCE_WILDCARD_BUFFER_BYTES];
	size_t sourceIndex = 0;
	size_t destinationIndex = 0;
	size_t wildcardIndex;
	uint16_t wildcardDigits = 1;
	uint32_t sequenceIndex = firstIndex;

	if (archives == NULL || archiveCount == 0 || pattern == NULL || payloads == NULL || count == 0) {
		return 0;
	}
	while (pattern[sourceIndex] != '*') {
		expandedName[destinationIndex++] = pattern[sourceIndex++];
		if (destinationIndex == SLIP_RESOURCE_NAME_BUFFER_BYTES) {
			return 0;
		}
	}
	wildcardIndex = destinationIndex;
	expandedName[destinationIndex++] = pattern[sourceIndex++];
	while (pattern[sourceIndex] == '*') {
		expandedName[destinationIndex++] = pattern[sourceIndex++];
		++wildcardDigits;
	}
	do {
		expandedName[destinationIndex++] = pattern[sourceIndex];
	} while (pattern[sourceIndex++] != '\0');

	for (uint16_t entryIndex = 0; entryIndex < count; ++entryIndex) {
		uint32_t value = sequenceIndex;
		uint32_t divisor = 1;
		uint16_t digit;

		for (digit = 1; digit < wildcardDigits; ++digit) {
			divisor *= 10u;
		}
		for (digit = 0; digit < wildcardDigits; ++digit) {
			expandedName[wildcardIndex + digit] = (char)(value / divisor) + '0';
			value %= divisor;
			if (divisor != 1u) {
				divisor /= 10u;
			}
		}
		if (!SlipResource_LoadByName(archives, archiveCount, expandedName, payloads)) {
			return 0;
		}
		++payloads;
		++sequenceIndex;
	}
	return 1;
}

static int SlipResource_LoadPayload(const char *resPath, const char *name, SlipResourcePayload *payload) {
	FILE *fp;
	uint8_t packedName[SLIP_RESOURCE_NAME_BYTES];
	uint8_t tail[SLIP_ARCHIVE_INDEX_OFFSET_BYTES];
	uint8_t countBytes[sizeof(uint32_t)];
	uint32_t indexOffset;
	uint32_t countWord;
	uint32_t count;
	int encrypted;
	uint32_t i;

	payload->data = NULL;
	payload->size = 0;
	payload->ownsData = false;
	SlipResource_PackResourceName(name, packedName);

	fp = SlipHostFile_OpenStream(resPath, "rb");
	if (fp == NULL) {
		return 0;
	}

	if (fseek(fp, -(long)SLIP_ARCHIVE_INDEX_OFFSET_BYTES, SEEK_END) != 0 ||
	    fread(tail, 1, sizeof(tail), fp) != sizeof(tail)) {
		fclose(fp);
		return 0;
	}
	indexOffset = (uint32_t)tail[0] | ((uint32_t)tail[1] << 8) | ((uint32_t)tail[2] << 16) | ((uint32_t)tail[3] << 24);

	if (fseek(fp, (long)indexOffset, SEEK_SET) != 0 ||
	    fread(countBytes, 1, sizeof(countBytes), fp) != sizeof(countBytes)) {
		fclose(fp);
		return 0;
	}
	countWord = (uint32_t)countBytes[0] | ((uint32_t)countBytes[1] << 8) | ((uint32_t)countBytes[2] << 16) |
	            ((uint32_t)countBytes[3] << 24);
	encrypted = (countWord & SLIP_ARCHIVE_COUNT_ENCRYPTED) != 0;
	count = countWord & ~SLIP_ARCHIVE_COUNT_ENCRYPTED;

	for (i = 0; i < count; ++i) {
		uint8_t entry[SLIP_ARCHIVE_ENTRY_BYTES];
		uint8_t keyed[SLIP_ARCHIVE_INDEX_NAME_BYTES];
		uint32_t offset;
		uint32_t size;
		uint32_t j;

		if (fread(entry, 1, sizeof(entry), fp) != sizeof(entry)) {
			fclose(fp);
			return 0;
		}

		memcpy(keyed, entry + SLIP_ARCHIVE_ENTRY_NAME_OFFSET, sizeof(keyed));
		if (encrypted) {
			for (j = 0; j < sizeof(keyed); ++j) {
				keyed[j] ^= kIndexXorKey[j];
			}
		}

		if (memcmp(keyed, packedName, SLIP_RESOURCE_NAME_BYTES) != 0) {
			continue;
		}

		offset = (uint32_t)entry[SLIP_ARCHIVE_ENTRY_PAYLOAD_OFFSET] |
		         ((uint32_t)entry[SLIP_ARCHIVE_ENTRY_PAYLOAD_OFFSET + 1] << 8) |
		         ((uint32_t)entry[SLIP_ARCHIVE_ENTRY_PAYLOAD_OFFSET + 2] << 16) |
		         ((uint32_t)entry[SLIP_ARCHIVE_ENTRY_PAYLOAD_OFFSET + 3] << 24);
		size = (uint32_t)entry[SLIP_ARCHIVE_ENTRY_PAYLOAD_SIZE_OFFSET] |
		       ((uint32_t)entry[SLIP_ARCHIVE_ENTRY_PAYLOAD_SIZE_OFFSET + 1] << 8) |
		       ((uint32_t)entry[SLIP_ARCHIVE_ENTRY_PAYLOAD_SIZE_OFFSET + 2] << 16) |
		       ((uint32_t)entry[SLIP_ARCHIVE_ENTRY_PAYLOAD_SIZE_OFFSET + 3] << 24);
		payload->data = (uint8_t *)malloc(size);
		if (payload->data == NULL) {
			fclose(fp);
			return 0;
		}
		payload->ownsData = true;

		if (fseek(fp, (long)offset, SEEK_SET) != 0 || fread(payload->data, 1, size, fp) != size) {
			SlipResource_ReleaseHandle(payload);
			fclose(fp);
			return 0;
		}
		payload->size = size;
		fclose(fp);
		return 1;
	}

	fclose(fp);
	return 0;
}

int SlipResource_LoadByName(const char *const *archives, size_t archiveCount, const char *name,
                            SlipResourcePayload *payload) {
	size_t i;
	SlipResourcePayload *loadedPayload;

	if (archives == NULL || archiveCount == 0 || name == NULL || payload == NULL) {
		return 0;
	}
	loadedPayload = SlipResource_NameTableFind(name);
	if (loadedPayload != NULL) {
		*payload = *loadedPayload;
		payload->ownsData = false;
		return 1;
	}
	for (i = 0; i < archiveCount; ++i) {
		if (archives[i] != NULL && SlipResource_LoadPayload(archives[i], name, payload)) {
			if (!SlipResource_NameTableAdd(name, payload)) {
				SlipResource_ReleaseHandle(payload);
				return 0;
			}
			payload->ownsData = false;
			return 1;
		}
	}
	return 0;
}

void SlipResource_MergeNext(SlipResourceBlock *block) {
	SlipResourceBlock *const next = block->physicalNext;
	if (next != NULL && (next->flags & SLIP_RESOURCE_BLOCK_ALLOCATED) == 0) {
		SlipResourceBlock *const following = next->physicalNext;
		if (following != NULL) {
			following->physicalPrevious = block;
		}
		next->previous->next = next->next;
		next->next->previous = next->previous;

		block->capacityBytes += next->capacityBytes + SLIP_RESOURCE_DOS_BLOCK_HEADER_BYTES;
		block->physicalNext = following;
	}
}

void SlipResource_Coalesce(SlipResourceBlock *block) {
	SlipResource_MergeNext(block);
	SlipResourceBlock *const previous = block->physicalPrevious;
	if (previous != NULL && (previous->flags & SLIP_RESOURCE_BLOCK_ALLOCATED) == 0) {
		SlipResource_MergeNext(previous);
	}
}

SlipResourceHandle *SlipResource_handles;
SlipResourceBlock *SlipResource_handleTableBlock;
SlipResourceBlock SlipResource_freeBlocks;
uint32_t SlipResource_freeBytes;
uint32_t SlipResource_cachedBytes;

void SlipResource_ReleaseBlock(SlipResourceBlock *block) {
	block->flags &= (uint16_t)~SLIP_RESOURCE_BLOCK_ALLOCATED;
	if (block->handleByteOffset != UINT32_MAX) {

		SlipResource_handles[block->handleByteOffset / SLIP_RESOURCE_DOS_HANDLE_BYTES].block = NULL;
	}
	const uint32_t bytes = block->capacityBytes + SLIP_RESOURCE_DOS_BLOCK_HEADER_BYTES;
	if ((block->flags & SLIP_RESOURCE_BLOCK_CACHED) != 0) {
		SlipResource_cachedBytes -= bytes;
	}
	SlipResource_freeBytes += bytes;
	block->previous->next = block->next;
	block->next->previous = block->previous;
	SlipResourceBlock *const previous = SlipResource_freeBlocks.next;
	SlipResourceBlock *const next = previous->next;
	previous->next = block;
	block->previous = previous;
	next->previous = block;
	block->next = next;
}

SlipResourceHandle *SlipResource_freeHandles;

void SlipResource_RecycleHandle(SlipResourceHandle *handle) {
	handle->previous->next = handle->next;
	handle->next->previous = handle->previous;
	SlipResourceHandle *const sentinel = SlipResource_freeHandles;
	SlipResourceHandle *const last = sentinel->previous;
	sentinel->previous = handle;
	handle->next = sentinel;
	last->next = handle;
	handle->previous = last;
}

void SlipResource_ReleaseAnonymous(SlipResourceHandle *handle) {
	SlipResourceBlock *const block = handle->block;
	if (block != NULL) {
		SlipResource_ReleaseBlock(block);
		SlipResource_Coalesce(block);
	}
	SlipResource_RecycleHandle(handle);
}

SlipResourceHandle *SlipResource_activeHandles;
uint16_t SlipResource_handleCount;

void SlipResource_InitHandleTable(SlipResourceBlock *allocation, SlipResourceHandle *records) {
	allocation->handleByteOffset = UINT32_MAX;
	allocation->flags |= SLIP_RESOURCE_BLOCK_MOVABLE;
	SlipResource_handles = records;
	SlipResource_handleTableBlock = allocation;
	SlipResource_activeHandles = records;
	SlipResource_freeHandles = records + 1;
	records->next = records;
	records->previous = records;
	uint32_t remainingRecordCount = SlipResource_handleCount;
	SlipResourceHandle *entry = SlipResource_freeHandles;
	do {
		SlipResourceHandle *const next = entry + 1;
		entry->next = next;
		next->previous = entry;
		entry = next;
	} while (--remainingRecordCount != 0);
	entry->next = SlipResource_freeHandles;
	SlipResource_freeHandles->previous = entry;
}

void SlipResource_RelocateHandles(SlipResourceBlock *allocation, SlipResourceHandle *records, uint16_t oldCount,
                                  SlipResourceBlock *oldAllocation) {
	SlipResourceHandle *const oldRecords = SlipResource_handles;
	allocation->flags |= SLIP_RESOURCE_BLOCK_MOVABLE;
	allocation->handleByteOffset = UINT32_MAX;
	SlipResource_handles = records;
	SlipResource_handleTableBlock = allocation;
	SlipResource_activeHandles = records;
	SlipResource_freeHandles = records + 1;
	const uint32_t copiedRecordCount = (uint32_t)oldCount + SLIP_RESOURCE_HANDLE_SENTINEL_COUNT;
	for (uint32_t i = 0; i < copiedRecordCount; ++i) {
		records[i] = oldRecords[i];
		records[i].next = records + (oldRecords[i].next - oldRecords);
		records[i].previous = records + (oldRecords[i].previous - oldRecords);
	}
	uint32_t remainingRecordCount =
	    (uint32_t)SlipResource_handleCount + SLIP_RESOURCE_HANDLE_SENTINEL_COUNT - copiedRecordCount;
	SlipResourceHandle *const first = records + copiedRecordCount;
	SlipResourceHandle *entry = first;
	--remainingRecordCount;
	do {
		SlipResourceHandle *const next = entry + 1;
		entry->next = next;
		next->previous = entry;
		entry = next;
	} while (--remainingRecordCount != 0);
	entry->next = first;
	first->previous = entry;
	SlipResource_freeHandles->next = first;
	first->previous = SlipResource_freeHandles;
	SlipResource_freeHandles->previous = entry;
	entry->next = SlipResource_freeHandles;
	SlipResource_ReleaseBlock(oldAllocation);
}

uint32_t SlipResource_TakeAvailableHandle(SlipResourceHandle *handle) {
	handle->previous->next = handle->next;
	handle->next->previous = handle->previous;
	SlipResourceHandle *const previous = SlipResource_activeHandles->next;
	SlipResourceHandle *const next = previous->next;
	previous->next = handle;
	next->previous = handle;
	handle->next = next;
	handle->previous = previous;
	handle->block = NULL;

	return (uint32_t)(handle - SlipResource_handles);
}

SlipResourceBlock SlipResource_allocatedBlocks;

void SlipResource_MoveBlockToTail(SlipResourceBlock *block) {
	block->previous->next = block->next;
	block->next->previous = block->previous;
	SlipResourceBlock *const last = SlipResource_allocatedBlocks.previous;
	SlipResourceBlock *const next = last->next;
	last->next = block;
	block->previous = last;
	next->previous = block;
	block->next = next;
}

bool SlipResource_ReclaimBlock(void) {
	SlipResourceBlock *block = SlipResource_allocatedBlocks.previous;
	do {
		SlipResourceBlock *const previous = block->previous;
		if ((block->flags & SLIP_RESOURCE_BLOCK_CACHED) != 0) {
			SlipResource_ReleaseBlock(block);
			SlipResource_Coalesce(block);
			return true;
		}
		block = previous;
	} while (block != &SlipResource_allocatedBlocks);
	return false;
}

uint32_t SlipResource_reclaimEnabled;
uint32_t SlipResource_compactBytes;

bool SlipResource_CompactStep(void) { return false; }

bool SlipResource_Compact(uint32_t requestedBytes) {
	const uint32_t alignedBytes = (requestedBytes + SLIP_RESOURCE_ALIGNMENT_LOW_MASK) & SLIP_RESOURCE_ALIGNMENT_MASK;
	SlipResourceBlock *first = SlipResource_freeBlocks.next;
	while (first != &SlipResource_freeBlocks) {
		uint32_t combinedCapacityBytes = 0u - SLIP_RESOURCE_DOS_BLOCK_HEADER_BYTES;
		SlipResourceBlock *block = first;
		for (;;) {
			if ((block->flags & SLIP_RESOURCE_BLOCK_ALLOCATED) == 0) {
				combinedCapacityBytes += block->capacityBytes + SLIP_RESOURCE_DOS_BLOCK_HEADER_BYTES;
				if (combinedCapacityBytes >= alignedBytes) {
					SlipResource_compactBytes = alignedBytes;
					do {
						if (!SlipResource_CompactStep()) {
							return false;
						}
					} while (first->capacityBytes < SlipResource_compactBytes);
					return true;
				}
			}
			SlipResourceBlock *const next = block->physicalNext;
			if (next == NULL || ((next->flags & SLIP_RESOURCE_BLOCK_ALLOCATED) != 0 &&
			                     ((next->flags & SLIP_RESOURCE_BLOCK_MOVABLE) == 0 || next->lockCount != 0))) {
				break;
			}
			block = next;
		}
		first = first->next;
	}
	return false;
}

bool SlipResource_ReclaimUnlocked(void) {
	if (SlipResource_reclaimEnabled != 0) {
		SlipResourceBlock *block = SlipResource_allocatedBlocks.next;
		do {
			SlipResourceBlock *const next = block->next;
			if (block->lockCount == 0 && (block->flags & SLIP_RESOURCE_BLOCK_RECLAIM_WHEN_UNLOCKED) != 0) {
				SlipResource_ReleaseBlock(block);
				SlipResource_Coalesce(block);
				return true;
			}
			block = next;
		} while (block != &SlipResource_allocatedBlocks);
	}
	return false;
}

uint32_t SlipResource_frontRequestedBytes;

SlipResourceBlock *SlipResource_AllocateFront(SlipResourceBlock *block, uint32_t requestedBytes,
                                              SlipResourceBlock *remainder) {
	SlipResource_frontRequestedBytes = requestedBytes;
	const uint32_t alignedBytes = (requestedBytes + SLIP_RESOURCE_ALIGNMENT_LOW_MASK) & SLIP_RESOURCE_ALIGNMENT_MASK;
	if (block->capacityBytes != alignedBytes) {
		const uint32_t excessBytes = block->capacityBytes - alignedBytes;
		if (excessBytes >= SLIP_RESOURCE_MIN_SPLIT_BYTES) {
			SlipResourceBlock *const next = block->next;
			next->previous = remainder;
			SlipResourceBlock *const following = block->physicalNext;
			if (following != NULL) {
				following->physicalPrevious = remainder;
			}
			remainder->previous = block;
			block->next = remainder;
			remainder->next = next;
			remainder->capacityBytes = excessBytes - SLIP_RESOURCE_DOS_BLOCK_HEADER_BYTES;
			remainder->physicalPrevious = block;
			remainder->physicalNext = following;
			remainder->flags = 0;
			block->physicalNext = remainder;
			block->capacityBytes = alignedBytes;
		}
	}
	block->previous->next = block->next;
	block->next->previous = block->previous;
	block->flags = SLIP_RESOURCE_BLOCK_ALLOCATED;
	block->lockCount = 0;
	SlipResource_freeBytes -= block->capacityBytes + SLIP_RESOURCE_DOS_BLOCK_HEADER_BYTES;
	SlipResourceBlock *const previous = SlipResource_allocatedBlocks.next;
	SlipResourceBlock *const next = previous->next;
	previous->next = block;
	block->previous = previous;
	next->previous = block;
	block->next = next;
	block->requestedBytes = SlipResource_frontRequestedBytes;
	return block;
}

uint32_t SlipResource_backRequestedBytes;

SlipResourceBlock *SlipResource_AllocateBack(SlipResourceBlock *block, uint32_t requestedBytes,
                                             SlipResourceBlock *splitAllocation) {
	SlipResource_backRequestedBytes = requestedBytes;
	const uint32_t alignedBytes = (requestedBytes + SLIP_RESOURCE_ALIGNMENT_LOW_MASK) & SLIP_RESOURCE_ALIGNMENT_MASK;
	if (block->capacityBytes != alignedBytes) {
		const uint32_t excessBytes = block->capacityBytes - alignedBytes;
		if (excessBytes >= SLIP_RESOURCE_MIN_SPLIT_BYTES) {
			SlipResourceBlock *const remainder = block;
			block = splitAllocation;
			remainder->capacityBytes = excessBytes - SLIP_RESOURCE_DOS_BLOCK_HEADER_BYTES;
			SlipResourceBlock *const following = remainder->physicalNext;
			remainder->physicalNext = block;
			block->physicalPrevious = remainder;
			block->physicalNext = following;
			remainder->flags = 0;
			block->capacityBytes = alignedBytes;
			if (following != NULL) {
				following->physicalPrevious = block;
			}
		} else {
			block->previous->next = block->next;
			block->next->previous = block->previous;
		}
	}

	block->flags = SLIP_RESOURCE_BLOCK_ALLOCATED;
	block->lockCount = 0;
	SlipResource_freeBytes -= block->capacityBytes + SLIP_RESOURCE_DOS_BLOCK_HEADER_BYTES;
	SlipResourceBlock *const previous = SlipResource_allocatedBlocks.next;
	SlipResourceBlock *const next = previous->next;
	previous->next = block;
	block->previous = previous;
	next->previous = block;
	block->next = next;
	block->requestedBytes = SlipResource_backRequestedBytes;
	return block;
}

uint32_t SlipResource_totalBytes;

void SlipResource_AddRegion(SlipResourceBlock *block, uint32_t bytes) {
	SlipResource_freeBytes += bytes;
	SlipResource_totalBytes += bytes;
	block->capacityBytes = bytes - SLIP_RESOURCE_DOS_BLOCK_HEADER_BYTES;
	block->flags = 0;
	SlipResourceBlock *const previous = SlipResource_freeBlocks.next;
	SlipResourceBlock *const next = previous->next;
	previous->next = block;
	block->previous = previous;
	block->next = next;
	next->previous = block;
	block->physicalPrevious = NULL;
	block->physicalNext = NULL;
}

void SlipResource_Unlock(uint16_t handle) {
	SlipResourceBlock *const block = SlipResource_handles[handle].block;
	block->lockCount = (uint16_t)(block->lockCount - 1u);
	if (block->lockCount == 0) {
		SlipResource_MoveBlockToTail(block);
	}
}

SlipResourceBlock *SlipResource_ActivateResident(SlipResourceHandle *handle) {
	SlipResourceBlock *const block = handle->block;
	if ((block->flags & SLIP_RESOURCE_BLOCK_CACHED) != 0) {
		block->flags &= (uint16_t)~SLIP_RESOURCE_BLOCK_CACHED;
		SlipResource_cachedBytes -= block->capacityBytes + SLIP_RESOURCE_DOS_BLOCK_HEADER_BYTES;
	}
	return block;
}

void SlipResource_MarkReclaimable(SlipResourceBlock *block) {
	if ((block->flags & SLIP_RESOURCE_BLOCK_CACHED) == 0) {
		SlipResource_cachedBytes += block->capacityBytes + SLIP_RESOURCE_DOS_BLOCK_HEADER_BYTES;
		block->flags |= SLIP_RESOURCE_BLOCK_CACHED;
	}
}

void SlipResource_ReleaseRecord(uint16_t handle) {
	SlipResourceHandle *const record = &SlipResource_handles[handle];
	if (record->nameOffset == UINT32_MAX) {
		SlipResource_ReleaseAnonymous(record);
	} else {
		SlipResourceBlock *const block = record->block;
		if (block != NULL) {
			if ((block->flags & SLIP_RESOURCE_BLOCK_RELEASE_IMMEDIATELY) == 0) {
				SlipResource_MarkReclaimable(block);
			} else {
				SlipResource_ReleaseBlock(block);
				SlipResource_Coalesce(block);
			}
		}
	}
}

uint32_t SlipResource_residentExtension;

bool SlipResource_ResidentExtension(const char *name) {
	const char *character = name;
	for (;;) {
		const uint8_t value = (uint8_t)*character++;
		if (value == '\0') {
			return false;
		}
		if (value == '.') {
			break;
		}
	}
	SlipResource_residentExtension = SLIP_RES_EXTENSION_SPACE_PADDING;
	for (unsigned i = 0; i < SLIP_RES_EXTENSION_LENGTH; ++i) {
		const uint8_t value = (uint8_t)*character;
		if (value == '\0') {
			break;
		}
		const uint32_t shift = i * SLIP_RES_EXTENSION_CHARACTER_BITS;
		SlipResource_residentExtension = (SlipResource_residentExtension & ~((uint32_t)UINT8_MAX << shift)) |
		                                 ((uint32_t)SlipResource_Uppercase(value) << shift);
		++character;
	}
	return true;
}

SlipResourceUsage SlipResource_GetUsage(void) {
	return (SlipResourceUsage){SlipResource_totalBytes, SlipResource_freeBytes + SlipResource_cachedBytes};
}
