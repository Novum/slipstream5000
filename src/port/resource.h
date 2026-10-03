#ifndef SLIPSTREAM5000_RESOURCE_H
#define SLIPSTREAM5000_RESOURCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
	SLIP_RESOURCE_BASE_NAME_BYTES = 8,
	SLIP_RESOURCE_EXTENSION_BYTES = 3,
	SLIP_RESOURCE_NAME_BYTES = SLIP_RESOURCE_BASE_NAME_BYTES + 1 + SLIP_RESOURCE_EXTENSION_BYTES,
	SLIP_RESOURCE_NAME_BUFFER_BYTES = SLIP_RESOURCE_NAME_BYTES + 1,
	SLIP_RESOURCE_WILDCARD_BUFFER_BYTES = SLIP_RESOURCE_NAME_BUFFER_BYTES + 1,
	SLIP_RESOURCE_CALLBACK_CAPACITY = 32
};

struct SlipShape3DHeader;
typedef void (*SlipResourceLoadedCallback)(struct SlipShape3DHeader *header);

typedef struct SlipResourceCallback {
	uint32_t extensionKey;
	SlipResourceLoadedCallback loadedCallback;
	uint32_t zeroInitializedWord;
} SlipResourceCallback;

extern SlipResourceCallback SlipResource_callbacks[SLIP_RESOURCE_CALLBACK_CAPACITY];
extern uint32_t SlipResource_callbackCount;
void SlipResource_RegisterCallback(uint32_t extension, SlipResourceLoadedCallback callback);
extern uint32_t SlipResource_loadExtension;

enum {
	SLIP_RESOURCE_PARAGRAPH_SHIFT = 4,
	SLIP_RESOURCE_PARAGRAPH_BYTES = 1 << SLIP_RESOURCE_PARAGRAPH_SHIFT,
	SLIP_RESOURCE_DOS_BLOCK_HEADER_BYTES = 0x20,
	SLIP_RESOURCE_DOS_HANDLE_BYTES = 0x10,
	SLIP_RESOURCE_HANDLE_SENTINEL_COUNT = 2,
	SLIP_RESOURCE_MIN_SPLIT_BYTES = 0x100
};

#define SLIP_RESOURCE_ALIGNMENT_LOW_MASK UINT32_C(3)
#define SLIP_RESOURCE_ALIGNMENT_MASK (~SLIP_RESOURCE_ALIGNMENT_LOW_MASK)

/* Block +1c flags, named from the original tests and state transitions. */
enum {
	SLIP_RESOURCE_BLOCK_ALLOCATED = 0x01,
	SLIP_RESOURCE_BLOCK_RECLAIM_WHEN_UNLOCKED = 0x02,
	SLIP_RESOURCE_BLOCK_CACHED = 0x04,
	SLIP_RESOURCE_BLOCK_RELEASE_IMMEDIATELY = 0x08,
	SLIP_RESOURCE_BLOCK_MOVABLE = 0x10,
	SLIP_RESOURCE_BLOCK_ALLOCATED_MOVABLE = SLIP_RESOURCE_BLOCK_ALLOCATED | SLIP_RESOURCE_BLOCK_MOVABLE,
	SLIP_RESOURCE_BLOCK_ALLOCATED_RECLAIMABLE =
	    SLIP_RESOURCE_BLOCK_ALLOCATED | SLIP_RESOURCE_BLOCK_RECLAIM_WHEN_UNLOCKED,
	SLIP_RESOURCE_BLOCK_ALLOCATED_CACHED = SLIP_RESOURCE_BLOCK_ALLOCATED | SLIP_RESOURCE_BLOCK_CACHED,
	SLIP_RESOURCE_BLOCK_ALLOCATED_IMMEDIATE_RELEASE =
	    SLIP_RESOURCE_BLOCK_ALLOCATED | SLIP_RESOURCE_BLOCK_RELEASE_IMMEDIATELY,
	SLIP_RESOURCE_BLOCK_CACHED_MOVABLE = SLIP_RESOURCE_BLOCK_CACHED | SLIP_RESOURCE_BLOCK_MOVABLE,
	SLIP_RESOURCE_BLOCK_ALLOCATED_CACHED_MOVABLE = SLIP_RESOURCE_BLOCK_ALLOCATED_MOVABLE | SLIP_RESOURCE_BLOCK_CACHED
};

enum { SLIP_RESOURCE_ALLOCATE_MOVABLE = 0x01 };

typedef struct SlipResourceBlock {
	struct SlipResourceBlock *next, *previous;
	struct SlipResourceBlock *physicalPrevious, *physicalNext;
	uint32_t capacityBytes, requestedBytes, handleByteOffset;
	uint16_t flags, lockCount;
} SlipResourceBlock;

typedef struct SlipResourceHandle {
	struct SlipResourceHandle *next, *previous;
	SlipResourceBlock *block;
	uint32_t nameOffset;
} SlipResourceHandle;

extern SlipResourceHandle *SlipResource_loadingRecord;
/* Name-table storage must include the three bytes after the twelve-byte scan
 * when it has no dot. The shape view belongs to record's resident block. */
void SlipResource_DispatchLoaded(SlipResourceHandle *record, const char *name, struct SlipShape3DHeader *shape);

extern SlipResourceHandle *SlipResource_handles;

extern SlipResourceBlock *SlipResource_handleTableBlock;
extern SlipResourceHandle *SlipResource_freeHandles;
extern SlipResourceHandle *SlipResource_activeHandles;
extern uint16_t SlipResource_handleCount;
void SlipResource_InitHandleTable(SlipResourceBlock *allocation, SlipResourceHandle *records);
void SlipResource_RelocateHandles(SlipResourceBlock *allocation, SlipResourceHandle *records, uint16_t oldCount,
                                  SlipResourceBlock *oldAllocation);
uint32_t SlipResource_TakeAvailableHandle(SlipResourceHandle *handle);
void SlipResource_RecycleHandle(SlipResourceHandle *handle);
void SlipResource_ReleaseAnonymous(SlipResourceHandle *handle);
extern SlipResourceBlock SlipResource_freeBlocks;
extern SlipResourceBlock SlipResource_allocatedBlocks;
SlipResourceBlock *SlipResource_ActivateResident(SlipResourceHandle *handle);
void SlipResource_MarkReclaimable(SlipResourceBlock *block);
void SlipResource_ReleaseRecord(uint16_t handle);
void SlipResource_Unlock(uint16_t handle);
void SlipResource_MoveBlockToTail(SlipResourceBlock *block);
bool SlipResource_ReclaimBlock(void);
extern uint32_t SlipResource_reclaimEnabled;
extern uint32_t SlipResource_compactBytes;
bool SlipResource_CompactStep(void);
bool SlipResource_Compact(uint32_t requestedBytes);
bool SlipResource_ReclaimUnlocked(void);
extern uint32_t SlipResource_backRequestedBytes;
SlipResourceBlock *SlipResource_AllocateBack(SlipResourceBlock *block, uint32_t requestedBytes,
                                             SlipResourceBlock *splitAllocation);
extern uint32_t SlipResource_frontRequestedBytes;
SlipResourceBlock *SlipResource_AllocateFront(SlipResourceBlock *block, uint32_t requestedBytes,
                                              SlipResourceBlock *remainder);
extern uint32_t SlipResource_totalBytes;
void SlipResource_AddRegion(SlipResourceBlock *block, uint32_t bytes);
extern uint32_t SlipResource_freeBytes;
extern uint32_t SlipResource_cachedBytes;
void SlipResource_ReleaseBlock(SlipResourceBlock *block);

void SlipResource_MergeNext(SlipResourceBlock *block);
void SlipResource_Coalesce(SlipResourceBlock *block);

extern uint32_t SlipResource_residentExtension;
bool SlipResource_ResidentExtension(const char *name);
typedef void (*SlipResourceResidentCallback)(const char *name, uint8_t *payload);

typedef struct SlipResourceResidentCalls {
	void *context;
	SlipResourceHandle **activeHandles;
	const char *(*name)(void *, uint32_t offset);
	uint8_t *(*payload)(void *, SlipResourceBlock *);
} SlipResourceResidentCalls;

extern const SlipResourceResidentCalls SlipResource_cachedResidentCalls;
extern uint32_t SlipResource_visitExtension;
extern SlipResourceResidentCallback SlipResource_residentCallback;
extern const char *SlipResource_residentName;
void SlipResource_VisitResident(uint32_t extension, SlipResourceResidentCallback, const SlipResourceResidentCalls *);
uint32_t SlipResource_ExtensionKey(uint32_t extension);
uint8_t SlipResource_Uppercase(uint8_t character);

typedef struct SlipResourcePayload {
	uint8_t *data;
	size_t size;
	bool ownsData;

	uint32_t address;
} SlipResourcePayload;

void SlipResource_ReleaseHandle(SlipResourcePayload *payload);
void SlipResource_ReleaseSequence(SlipResourcePayload *payloads, uint16_t count);
int SlipResource_LoadByName(const char *const *archives, size_t archiveCount, const char *name,
                            SlipResourcePayload *payload);
int SlipResource_LoadWildcardSequence(const char *const *archives, size_t archiveCount, const char *pattern,
                                      uint32_t firstIndex, uint16_t count, SlipResourcePayload *payloads);

typedef struct SlipResourceUsage {
	uint32_t totalBytes;
	uint32_t availableBytes;
} SlipResourceUsage;

SlipResourceUsage SlipResource_GetUsage(void);

#endif
