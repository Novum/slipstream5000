#include "resource.h"

enum { SLIP_RES_EXTENSION_LENGTH = SLIP_RESOURCE_EXTENSION_BYTES, SLIP_RES_EXTENSION_CHARACTER_BITS = 8 };

#define SLIP_RES_EXTENSION_SPACE_PADDING UINT32_C(0x20202020)

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
