#include "artic_slot.h"
#include "actor_resources.h"
#include "byte_order.h"

#include "runtime.h"
#include "track_world.h"

#include <string.h>

uint32_t SlipArticSlot_initialized;

enum { ARTIC_OBJECT_RELEASE_SERVER = 1u };

static SlipArticSlotPool *installedPool;

static void SlipArticSlot_ShutdownInstalledPool(void) { SlipArticSlot_Shutdown(installedPool); }

static uint32_t SlipArticSlot_ObjectEvent(uint32_t events, uint32_t payload, uint32_t value, uint32_t flags,
                                          uint16_t object, uintptr_t data, uint32_t frame);

typedef char SlipArticDebrisStrideCheck[sizeof(SlipArticDebrisEntry) == 0x10 ? 1 : -1];
typedef char SlipArticDebrisCountCheck[offsetof(SlipArticPartHeader, debrisCount) == 0x5c ? 1 : -1];
typedef char SlipArticDebrisOffsetCheck[offsetof(SlipArticPartHeader, debris) == 0x5e ? 1 : -1];
typedef char SlipArticDestructionCountCheck[offsetof(SlipArticPartHeader, destructionCount) == 0x9e ? 1 : -1];
typedef char SlipArticDestructionOffsetCheck[offsetof(SlipArticPartHeader, destruction) == 0xa0 ? 1 : -1];
typedef char SlipArticActorPartsCheck[offsetof(SlipArticActorHeader, parts) == 0x30 ? 1 : -1];
typedef char SlipArticActorOwnerCheck[offsetof(SlipArticActorHeader, owner) == 0xc0 ? 1 : -1];
typedef char SlipArticPartStrideCheck[sizeof(SlipArticPartRecord) == 0x160 ? 1 : -1];
typedef char SlipArticPartMatrixCheck[offsetof(SlipArticPartRecord, worldMatrix) == 0xe4 ? 1 : -1];
typedef char SlipArticPartPointCountCheck[offsetof(SlipArticPartRecord, namedPointCount) == 0x10c ? 1 : -1];
typedef char SlipArticPartPointsCheck[offsetof(SlipArticPartRecord, namedPoints) == 0x110 ? 1 : -1];
typedef char SlipArticActorMatrixCheck[offsetof(SlipArticActorRecord, cachedMatrix) == 0xc4 ? 1 : -1];

static void SlipArticSlot_Write16(uint8_t *p, uint16_t value) {
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
}

static void SlipArticSlot_Write32(uint8_t *p, uint32_t value) {
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
	p[2] = (uint8_t)(value >> 16);
	p[3] = (uint8_t)(value >> 24);
}

static uint32_t SlipArticSlot_AddressFromPointer(const SlipArticSlotPool *pool, const uint8_t *p) {
	return pool->allocationAddress + (uint32_t)(p - pool->allocation);
}

static uint8_t *SlipArticSlot_Pointer(const SlipArticSlotPool *pool, uint32_t recordAddress) {
	uint32_t offset;

	if (recordAddress < pool->allocationAddress)
		return NULL;
	offset = recordAddress - pool->allocationAddress;
	if ((size_t)offset >= pool->allocationBytes)
		return NULL;
	return pool->allocation + offset;
}

/* Legacy payload ABI bindings for callers which already locked the ART.
 * Traversal and singleton-base semantics live in the complete translations. */
void SlipArticSlot_PreloadResources(const uint8_t *payload, SlipArticSlotFindResource loadResource, void *user) {
	const SlipActorResourceCalls calls = {.context = user, .load = loadResource};
	SlipActorResources_preloadPayload = payload;
	SlipActor_PreloadNode(payload + SlipBytes_ReadLE32(payload + 0x20), &calls);
}

void SlipArticSlot_ReleaseResources(const uint8_t *payload, SlipArticSlotFindResource findResource,
                                    SlipArticSlotReleaseResource releaseResource, void *user) {
	const SlipActorResourceCalls calls = {.context = user, .find = findResource, .release = releaseResource};
	SlipActorResources_releasePayload = payload;
	SlipActor_ReleaseNode(payload + SlipBytes_ReadLE32(payload + 0x20), &calls);
}

uint32_t SlipArticSlot_PoolBytes(uint16_t actorCount) {
	const uint32_t actorBytes = (uint32_t)(uint16_t)(actorCount + 2u) * 0x00d8u;
	const uint32_t partBytes = (uint32_t)(0x0050u + 1u) * 0x0160u;

	return actorBytes + partBytes;
}

static bool SlipArticSlot_InitializePoolWithCalls(uint16_t actorCount, uint8_t *allocation, size_t allocationBytes,
                                                  uint32_t allocationAddress, const SlipArticSlotResourceCalls *calls,
                                                  SlipArticSlotPool *result) {
	const uint32_t actorBytes = (uint32_t)(uint16_t)(actorCount + 2u) * 0x00d8u;
	const uint32_t requiredBytes = SlipArticSlot_PoolBytes(actorCount);
	uint8_t *activeSentinel;
	uint8_t *actorRecord;
	uint8_t *partRecord;
	uint32_t i;

	if (result == NULL || (calls == NULL && (allocation == NULL || allocationBytes < requiredBytes))) {
		return false;
	}

	if (SlipArticSlot_initialized != 0)
		return true;

	SlipArticSlot_initialized = UINT32_MAX;
	uint16_t resource = 0;
	if (calls != NULL) {
		if (!calls->allocate(calls->context, requiredBytes, 0, &resource))
			return false;
		allocation = (uint8_t *)calls->lock(calls->context, resource);
		SlipResourcePayload payload = calls->payload(resource);
		allocationBytes = payload.size;
		allocationAddress = payload.address;
	}
	*result = (SlipArticSlotPool){allocation,
	                              allocationBytes,
	                              allocationAddress,
	                              allocation,
	                              allocationAddress,
	                              allocation + 0x00d8u,
	                              allocationAddress + 0x00d8u,
	                              allocation + actorBytes,
	                              allocationAddress + actorBytes,
	                              actorCount,
	                              resource,
	                              calls};

	activeSentinel = result->activeSentinel;
	SlipArticActorHeader *const active = (void *)activeSentinel;
	active->next = result->activeSentinelAddress;
	active->previous = result->activeSentinelAddress;
	actorRecord = result->actorFreeSentinel;
	for (i = 0; i < actorCount; ++i) {
		uint8_t *const nextAddress = actorRecord + 0x00d8u;
		SlipArticActorHeader *const actor = (void *)actorRecord;
		SlipArticActorHeader *const next = (void *)nextAddress;
		actor->next = SlipArticSlot_AddressFromPointer(result, nextAddress);
		next->previous = SlipArticSlot_AddressFromPointer(result, actorRecord);
		actorRecord = nextAddress;
	}
	((SlipArticActorHeader *)(void *)actorRecord)->next = result->actorFreeSentinelAddress;
	((SlipArticActorHeader *)(void *)result->actorFreeSentinel)->previous =
	    SlipArticSlot_AddressFromPointer(result, actorRecord);

	partRecord = result->partFreeSentinel;
	for (i = 0; i < 0x50u; ++i) {
		uint8_t *const nextAddress = partRecord + 0x0160u;
		SlipArticPartHeader *const part = (void *)partRecord;
		SlipArticPartHeader *const next = (void *)nextAddress;
		part->nextSibling = SlipArticSlot_AddressFromPointer(result, nextAddress);
		next->previousSibling = SlipArticSlot_AddressFromPointer(result, partRecord);
		partRecord = nextAddress;
	}
	((SlipArticPartHeader *)(void *)partRecord)->nextSibling = result->partFreeSentinelAddress;
	((SlipArticPartHeader *)(void *)result->partFreeSentinel)->previousSibling =
	    SlipArticSlot_AddressFromPointer(result, partRecord);

	installedPool = result;
	if (calls != NULL)
		calls->registerExit(calls->context, SlipArticSlot_ShutdownInstalledPool);
	SlipObject_SetServer(ARTIC_OBJECT_RELEASE_SERVER, SlipArticSlot_ObjectEvent);
	return true;
}

bool SlipArticSlot_InitializePool(uint16_t actorCount, uint8_t *allocation, size_t allocationBytes,
                                  uint32_t allocationAddress, SlipArticSlotPool *result) {
	return SlipArticSlot_InitializePoolWithCalls(actorCount, allocation, allocationBytes, allocationAddress, NULL,
	                                             result);
}

bool SlipArticSlot_InitializeResourcePool(uint16_t actorCount, const SlipArticSlotResourceCalls *calls,
                                          SlipArticSlotPool *result) {
	return SlipArticSlot_InitializePoolWithCalls(actorCount, NULL, 0, 0, calls, result);
}

bool SlipArticSlot_Allocate(SlipArticSlotPool *pool, SlipArticSlotAllocate *result) {
	uint8_t *freeSentinel;
	uint8_t *actorRecord;
	uint8_t *next;
	uint8_t *activeSentinel;
	uint8_t *activeNext;

	if (pool == NULL || result == NULL)
		return false;
	freeSentinel = pool->actorFreeSentinel;
	actorRecord = SlipArticSlot_Pointer(pool, ((SlipArticActorHeader *)(void *)freeSentinel)->next);
	if (actorRecord == NULL)
		return false;
	if (actorRecord == freeSentinel) {
		*result = (SlipArticSlotAllocate){NULL, 0, true};
		return true;
	}
	next = SlipArticSlot_Pointer(pool, ((SlipArticActorHeader *)(void *)actorRecord)->next);
	if (next == NULL)
		return false;
	((SlipArticActorHeader *)(void *)freeSentinel)->next = SlipArticSlot_AddressFromPointer(pool, next);
	((SlipArticActorHeader *)(void *)next)->previous = pool->actorFreeSentinelAddress;

	activeSentinel = pool->activeSentinel;
	activeNext = SlipArticSlot_Pointer(pool, ((SlipArticActorHeader *)(void *)activeSentinel)->next);
	if (activeNext == NULL)
		return false;
	((SlipArticActorHeader *)(void *)activeSentinel)->next = SlipArticSlot_AddressFromPointer(pool, actorRecord);
	((SlipArticActorHeader *)(void *)activeNext)->previous = SlipArticSlot_AddressFromPointer(pool, actorRecord);
	((SlipArticActorHeader *)(void *)actorRecord)->next = SlipArticSlot_AddressFromPointer(pool, activeNext);
	((SlipArticActorHeader *)(void *)actorRecord)->previous = pool->activeSentinelAddress;
	*result = (SlipArticSlotAllocate){actorRecord, SlipArticSlot_AddressFromPointer(pool, actorRecord), false};
	return true;
}

static void SlipArticSlot_FreePart(SlipArticSlotPool *pool, uint32_t partAddress) {
	SlipArticPartHeader *const part = (void *)SlipArticSlot_Pointer(pool, partAddress);
	SlipArticPartHeader *const previous = (void *)SlipArticSlot_Pointer(pool, part->previousSibling);
	SlipArticPartHeader *const next = (void *)SlipArticSlot_Pointer(pool, part->nextSibling);
	next->previousSibling = part->previousSibling;
	previous->nextSibling = part->nextSibling;
	SlipArticPartHeader *const freeList = (void *)pool->partFreeSentinel;
	SlipArticPartHeader *const first = (void *)SlipArticSlot_Pointer(pool, freeList->nextSibling);
	first->previousSibling = partAddress;
	part->nextSibling = freeList->nextSibling;
	part->previousSibling = pool->partFreeSentinelAddress;
	freeList->nextSibling = partAddress;
}

static void SlipArticSlot_FreeActor(SlipArticSlotPool *pool, uint32_t actorAddress) {
	SlipArticActorHeader *const actor = (void *)SlipArticSlot_Pointer(pool, actorAddress);
	uint32_t remaining = actor->partCount;
	uint32_t index = 0;
	do {
		SlipArticSlot_FreePart(pool, actor->parts[index++]);
	} while (--remaining != 0);
	SlipArticActorHeader *const next = (void *)SlipArticSlot_Pointer(pool, actor->next);
	SlipArticActorHeader *const previous = (void *)SlipArticSlot_Pointer(pool, actor->previous);
	previous->next = actor->next;
	next->previous = actor->previous;
	SlipArticActorHeader *const freeList = (void *)pool->actorFreeSentinel;
	SlipArticActorHeader *const first = (void *)SlipArticSlot_Pointer(pool, freeList->next);
	freeList->next = actorAddress;
	first->previous = actorAddress;
	actor->next = SlipArticSlot_AddressFromPointer(pool, (uint8_t *)first);
	actor->previous = pool->actorFreeSentinelAddress;
}

static void SlipArticSlot_Destroy(uint16_t object) {
	uint8_t *actorStorage;
	bool owned;
	if (SlipArticSlot_TestOwner(object, SlipObject_table, SLIP_OBJECT_TABLE_DOS_BYTES, installedPool->allocation,
	                            installedPool->allocationBytes, installedPool->allocationAddress, &actorStorage,
	                            &owned) &&
	    owned)
		SlipArticSlot_FreeActor(installedPool, SlipArticSlot_AddressFromPointer(installedPool, actorStorage));
}

static uint32_t SlipArticSlot_ObjectEvent(uint32_t events, uint32_t payload, uint32_t value, uint32_t flags,
                                          uint16_t object, uintptr_t data, uint32_t frame) {
	(void)payload;
	(void)value;
	(void)flags;
	(void)data;
	(void)frame;
	if (object == 0 || (events & ARTIC_OBJECT_RELEASE_SERVER) == 0 || SlipArticSlot_initialized == 0)
		return events;
	SlipArticActorHeader *const sentinel = (void *)installedPool->activeSentinel;
	uint32_t actorAddress = sentinel->next;
	for (;;) {
		SlipArticActorHeader *const actor = (void *)SlipArticSlot_Pointer(installedPool, actorAddress);
		const uint32_t nextAddress = actor->next;
		if (actorAddress == installedPool->activeSentinelAddress)
			return events;
		if (actor->owner == object)
			SlipArticSlot_Destroy(object);
		actorAddress = nextAddress;
	}
}

void SlipArticSlot_Shutdown(SlipArticSlotPool *pool) {
	if (SlipArticSlot_initialized != 0) {
		SlipArticSlot_initialized = 0;
		SlipArticActorHeader *const sentinel = (void *)pool->activeSentinel;
		uint32_t actorAddress = sentinel->next;
		for (;;) {
			SlipArticActorHeader *const actor = (void *)SlipArticSlot_Pointer(pool, actorAddress);
			const uint32_t nextAddress = actor->next;
			if (actorAddress == pool->activeSentinelAddress)
				break;
			SlipArticSlot_FreeActor(pool, actorAddress);
			actorAddress = nextAddress;
		}
		if (pool->resourceCalls != NULL) {
			pool->resourceCalls->unlock(pool->resourceCalls->context, pool->resource);
			pool->resourceCalls->release(pool->resourceCalls->context, pool->resource);
		}
	}
}

void SlipArticSlot_AllocatePart(SlipArticSlotPool *pool, uint32_t parentAddress, SlipArticSlotAllocatePart *result) {
	uint8_t *freeSentinel;
	uint32_t partAddress;
	uint8_t *partRecord;
	uint32_t nextAddress;
	uint32_t previousAddress;
	uint8_t *next;
	uint8_t *previous;

	freeSentinel = pool->partFreeSentinel;
	partAddress = ((SlipArticPartHeader *)(void *)freeSentinel)->nextSibling;
	if (partAddress == pool->partFreeSentinelAddress) {
		SlipRuntime_Fatal("ArtBodyAlloc - make it resize the slots buffer!");
	}
	partRecord = SlipArticSlot_Pointer(pool, partAddress);
	nextAddress = ((SlipArticPartHeader *)(void *)partRecord)->previousSibling;
	previousAddress = ((SlipArticPartHeader *)(void *)partRecord)->nextSibling;
	next = SlipArticSlot_Pointer(pool, nextAddress);
	previous = SlipArticSlot_Pointer(pool, previousAddress);
	((SlipArticPartHeader *)(void *)previous)->previousSibling = nextAddress;
	((SlipArticPartHeader *)(void *)next)->nextSibling = previousAddress;
	((SlipArticPartHeader *)(void *)partRecord)->parent = parentAddress;

	if (parentAddress != 0) {
		uint8_t *const parent = SlipArticSlot_Pointer(pool, parentAddress);
		const uint32_t firstChildAddress = ((SlipArticPartHeader *)(void *)parent)->firstChild;

		if (firstChildAddress != 0) {
			uint8_t *const firstChild = SlipArticSlot_Pointer(pool, firstChildAddress);
			const uint32_t childPreviousAddress = ((SlipArticPartHeader *)(void *)firstChild)->nextSibling;
			uint8_t *const childPrevious = SlipArticSlot_Pointer(pool, childPreviousAddress);

			((SlipArticPartHeader *)(void *)childPrevious)->previousSibling = partAddress;
			((SlipArticPartHeader *)(void *)partRecord)->nextSibling = childPreviousAddress;
			((SlipArticPartHeader *)(void *)partRecord)->previousSibling = firstChildAddress;
			((SlipArticPartHeader *)(void *)firstChild)->nextSibling = partAddress;
		} else {
			((SlipArticPartHeader *)(void *)parent)->firstChild = partAddress;
			((SlipArticPartHeader *)(void *)partRecord)->previousSibling = partAddress;
			((SlipArticPartHeader *)(void *)partRecord)->nextSibling = partAddress;
		}
	} else {
		((SlipArticPartHeader *)(void *)partRecord)->previousSibling = partAddress;
		((SlipArticPartHeader *)(void *)partRecord)->nextSibling = partAddress;
	}
	*result = (SlipArticSlotAllocatePart){partRecord, partAddress};
}

static bool SlipArticSlot_BodyRange(const uint8_t *payload, size_t payloadBytes, const uint8_t *body, size_t bytes) {
	size_t offset;

	if (payload == NULL || body < payload) {
		return false;
	}
	offset = (size_t)(body - payload);
	return offset <= payloadBytes && bytes <= payloadBytes - offset;
}

static bool SlipArticSlot_BodyFromOffset(const uint8_t *payload, size_t payloadBytes, uint32_t offset,
                                         const uint8_t **body) {
	if (offset >= payloadBytes)
		return false;
	*body = payload + offset;
	return true;
}

static uint16_t SlipArticSlot_FindBodyResource(const uint8_t *resourceName, SlipArticSlotFindResource findResource,
                                               void *findResourceUser) {
	char name[13];
	uint32_t resourceHandle;

	if (resourceName[0] == 0)
		return 0;
	memcpy(name, resourceName, 12u);
	name[12] = '\0';
	if (findResource == NULL || !findResource(findResourceUser, name, &resourceHandle)) {
		SlipArticSlot_initialized = 0;
		SlipRuntime_Fatal("ArticSlotInit - one of the body shapes is missing");
	}
	return (uint16_t)resourceHandle;
}

bool SlipArticSlot_InitializeParts(SlipArticSlotPool *pool, uint32_t actorAddress, const uint8_t *bodyStart,
                                   uint32_t parentAddress, const uint8_t *payload, size_t payloadBytes,
                                   SlipArticSlotFindResource findResource, void *findResourceUser) {
	uint8_t *const actorRecord = SlipArticSlot_Pointer(pool, actorAddress);
	const uint8_t *const firstBody = bodyStart;
	const uint8_t *body = bodyStart;

	if (actorRecord == NULL)
		return false;
	do {
		SlipArticSlotAllocatePart allocate;
		uint8_t *partRecord;
		uint32_t partAddress;
		uint32_t childOffset;
		uint32_t resourceCount;
		uint32_t entryIndexOrPointCount;

		if (!SlipArticSlot_BodyRange(payload, payloadBytes, body, 0x1d0u)) {
			return false;
		}
		childOffset = SlipBytes_ReadLE32(body + 0x04u);
		SlipArticSlot_AllocatePart(pool, parentAddress, &allocate);
		partRecord = allocate.partRecord;
		partAddress = allocate.partAddress;
		SlipArticSlot_Write16(partRecord + 0x10au, 0);
		SlipArticSlot_Write32(partRecord + 0x08u, 0);
		SlipArticSlot_Write32(partRecord + 0x24u, 0);
		SlipArticSlot_Write32(partRecord + 0x28u, 0);
		SlipArticSlot_Write32(partRecord + 0x2cu, 0);

		for (entryIndexOrPointCount = 0; entryIndexOrPointCount < 8u; ++entryIndexOrPointCount) {
			SlipArticSlot_Write16(partRecord + 0x3cu + entryIndexOrPointCount * 2u,
			                      SlipArticSlot_FindBodyResource(body + 0x1cu + entryIndexOrPointCount * 0x0eu,
			                                                     findResource, findResourceUser));
		}
		for (entryIndexOrPointCount = 0; entryIndexOrPointCount < 8u; ++entryIndexOrPointCount) {
			SlipArticSlot_Write16(partRecord + 0x4cu + entryIndexOrPointCount * 2u,
			                      SlipArticSlot_FindBodyResource(body + 0x8cu + entryIndexOrPointCount * 0x0eu,
			                                                     findResource, findResourceUser));
		}

		resourceCount = 0;
		for (entryIndexOrPointCount = 0; entryIndexOrPointCount < 4u; ++entryIndexOrPointCount) {
			const uint8_t *const destructionShapeRecord = body + 0xfcu + entryIndexOrPointCount * 0x1au;
			const uint16_t destructionShapeHandle =
			    SlipArticSlot_FindBodyResource(destructionShapeRecord, findResource, findResourceUser);
			SlipArticDebrisEntry *const destination =
			    &((SlipArticPartHeader *)(void *)partRecord)->destruction[entryIndexOrPointCount];

			if (destructionShapeHandle != 0)
				++resourceCount;
			destination->shape = destructionShapeHandle;
			destination->position.x = (int32_t)SlipBytes_ReadLE32(destructionShapeRecord + 0x0eu);
			destination->position.y = (int32_t)SlipBytes_ReadLE32(destructionShapeRecord + 0x12u);
			destination->position.z = (int32_t)SlipBytes_ReadLE32(destructionShapeRecord + 0x16u);
		}
		((SlipArticPartHeader *)(void *)partRecord)->destructionCount = (uint16_t)resourceCount;

		resourceCount = 0;
		for (entryIndexOrPointCount = 0; entryIndexOrPointCount < 4u; ++entryIndexOrPointCount) {
			const uint8_t *const debrisShapeRecord = body + 0x164u + entryIndexOrPointCount * 0x1au;
			const uint16_t debrisShapeHandle =
			    SlipArticSlot_FindBodyResource(debrisShapeRecord, findResource, findResourceUser);
			SlipArticDebrisEntry *const destination =
			    &((SlipArticPartHeader *)(void *)partRecord)->debris[entryIndexOrPointCount];

			if (debrisShapeHandle != 0)
				++resourceCount;
			destination->shape = debrisShapeHandle;
			destination->position.x = (int32_t)SlipBytes_ReadLE32(debrisShapeRecord + 0x0eu);
			destination->position.y = (int32_t)SlipBytes_ReadLE32(debrisShapeRecord + 0x12u);
			destination->position.z = (int32_t)SlipBytes_ReadLE32(debrisShapeRecord + 0x16u);
		}
		((SlipArticPartHeader *)(void *)partRecord)->debrisCount = (uint16_t)resourceCount;
		SlipArticSlot_Write32(partRecord + 0xe0u, SlipBytes_ReadLE32(body + 0x0cu));
		SlipArticSlot_Write32(partRecord + 0x14u, SlipBytes_ReadLE32(body + 0x10u));
		SlipArticSlot_Write32(partRecord + 0x18u, SlipBytes_ReadLE32(body + 0x14u));
		SlipArticSlot_Write32(partRecord + 0x1cu, SlipBytes_ReadLE32(body + 0x18u));

		entryIndexOrPointCount = SlipBytes_ReadLE32(body + 0x1ccu);
		if (entryIndexOrPointCount > 5u)
			entryIndexOrPointCount = 5u;
		SlipArticSlot_Write32(partRecord + 0x10cu, entryIndexOrPointCount);
		if (entryIndexOrPointCount != 0) {
			if (!SlipArticSlot_BodyRange(payload, payloadBytes, body,
			                             0x1d0u + (size_t)entryIndexOrPointCount * 0x10u)) {
				return false;
			}
			memcpy(partRecord + 0x110u, body + 0x1d0u, (size_t)entryIndexOrPointCount * 0x10u);
		}
		SlipArticSlot_Write32(partRecord, SlipBytes_ReadLE32(body));
		SlipArticSlot_Write32(actorRecord + 0x30u + SlipBytes_ReadLE32(actorRecord + 0x74u) * 4u, partAddress);
		SlipArticSlot_Write32(actorRecord + 0x74u, SlipBytes_ReadLE32(actorRecord + 0x74u) + 1u);

		if (childOffset != 0) {
			const uint8_t *childBody;

			if (!SlipArticSlot_BodyFromOffset(payload, payloadBytes, childOffset, &childBody) ||
			    !SlipArticSlot_InitializeParts(pool, actorAddress, childBody, partAddress, payload, payloadBytes,
			                                   findResource, findResourceUser)) {
				return false;
			}
		}
		{
			const uint32_t siblingOffset = SlipBytes_ReadLE32(body + 0x08u);

			if (siblingOffset == 0)
				break;
			if (!SlipArticSlot_BodyFromOffset(payload, payloadBytes, siblingOffset, &body)) {
				return false;
			}
		}
	} while (body != firstBody);
	return true;
}

bool SlipArticSlot_Create(uint16_t object, uint16_t resourceHandle, const uint8_t *payloadFrom, size_t payloadBytesFrom,
                          SlipArticSlotPool *pool, SlipObject *objectTable, size_t objectTableBytes,
                          SlipArticSlotFindResource findResource, void *findResourceUser, SlipArticSlotCreate *result) {
	SlipArticSlotAllocate allocate;
	SlipObjectSlotDataWriteResult setSlot;
	SlipObjectPosition objectPosition;
	uint8_t *actorRecord;
	uint32_t actorAddress;
	int32_t radiusX;
	int32_t radiusY;
	int32_t radiusZ;
	uint32_t bodyOffset;
	const uint8_t *body;

	if (result == NULL || pool == NULL || payloadFrom == NULL || payloadBytesFrom < 0x68u) {
		return false;
	}
	if (pool->resourceCalls != NULL)
		payloadFrom = pool->resourceCalls->lock(pool->resourceCalls->context, resourceHandle);
	if (!SlipArticSlot_Allocate(pool, &allocate)) {
		return false;
	}
	if (allocate.allocationFailed) {
		SlipRuntime_error = 7;
		if (pool->resourceCalls != NULL)
			pool->resourceCalls->unlock(pool->resourceCalls->context, resourceHandle);
		*result = (SlipArticSlotCreate){NULL, 0, true};
		return true;
	}

	actorRecord = allocate.actorRecord;
	actorAddress = allocate.actorAddress;
	SlipArticSlot_Write16(actorRecord + 0xc0u, object);
	SlipArticSlot_Write16(actorRecord + 0xc2u, resourceHandle);
	SlipArticSlot_Write32(actorRecord + 0x74u, 0);
	SlipArticSlot_Write32(actorRecord + 0x78u, 0);
	if (!SlipObject_SetDrawData(objectTable, objectTableBytes, object, actorAddress, &setSlot) ||
	    !SlipObject_Position(objectTable, objectTableBytes, object, &objectPosition)) {
		return false;
	}
	SlipArticSlot_Write32(actorRecord + 0x08u, objectPosition.positionX - 1u);
	SlipArticSlot_Write32(actorRecord + 0x0cu, objectPosition.positionY);
	SlipArticSlot_Write32(actorRecord + 0x10u, objectPosition.positionZ);
	SlipArticSlot_Write32(actorRecord + 0xbcu, SlipBytes_ReadLE32(payloadFrom + 0x24u));
	SlipArticSlot_Write32(actorRecord + 0x14u, SlipBytes_ReadLE32(payloadFrom + 0x04u));
	SlipArticSlot_Write32(actorRecord + 0x18u, SlipBytes_ReadLE32(payloadFrom + 0x08u));
	SlipArticSlot_Write32(actorRecord + 0x1cu, SlipBytes_ReadLE32(payloadFrom + 0x0cu));
	SlipArticSlot_Write32(actorRecord + 0x20u, SlipBytes_ReadLE32(payloadFrom + 0x10u));
	SlipArticSlot_Write32(actorRecord + 0x24u, SlipBytes_ReadLE32(payloadFrom + 0x14u));
	SlipArticSlot_Write32(actorRecord + 0x28u, SlipBytes_ReadLE32(payloadFrom + 0x18u));

	radiusX = (int32_t)(0u - SlipBytes_ReadLE32(payloadFrom + 0x04u));
	if (radiusX < (int32_t)SlipBytes_ReadLE32(payloadFrom + 0x10u)) {
		radiusX = (int32_t)SlipBytes_ReadLE32(payloadFrom + 0x10u);
	}
	radiusY = (int32_t)(0u - SlipBytes_ReadLE32(payloadFrom + 0x08u));
	if (radiusY < (int32_t)SlipBytes_ReadLE32(payloadFrom + 0x14u)) {
		radiusY = (int32_t)SlipBytes_ReadLE32(payloadFrom + 0x14u);
	}
	radiusZ = (int32_t)(0u - SlipBytes_ReadLE32(payloadFrom + 0x0cu));
	if (radiusZ < (int32_t)SlipBytes_ReadLE32(payloadFrom + 0x18u)) {
		radiusZ = (int32_t)SlipBytes_ReadLE32(payloadFrom + 0x18u);
	}
	SlipArticSlot_Write32(actorRecord + 0x2cu, SlipView3D_VectorLength(radiusX, radiusY, radiusZ));
	memcpy(actorRecord + 0x7cu, payloadFrom + 0x28u, 0x20u);
	memcpy(actorRecord + 0x9cu, payloadFrom + 0x48u, 0x20u);
	bodyOffset = SlipBytes_ReadLE32(payloadFrom + 0x20u);
	if (bodyOffset >= payloadBytesFrom)
		return false;
	body = payloadFrom + bodyOffset;
	if (!SlipArticSlot_InitializeParts(pool, actorAddress, body, 0, payloadFrom, payloadBytesFrom, findResource,
	                                   findResourceUser)) {
		return false;
	}
	if (pool->resourceCalls != NULL)
		pool->resourceCalls->unlock(pool->resourceCalls->context, resourceHandle);
	*result = (SlipArticSlotCreate){actorRecord, actorAddress, false};
	return true;
}

static uint8_t *SlipArticSlot_PointerFromAddress(uint8_t *base, size_t bytes, uint32_t baseAddress,
                                                 uint32_t recordAddress) {
	const uint32_t offset = recordAddress - baseAddress;

	if (recordAddress < baseAddress || offset >= bytes) {
		return NULL;
	}
	return base + offset;
}

typedef enum SlipArticSlotRotation {
	SLIP_ARTIC_SLOT_ROTATION_NONE,
	SLIP_ARTIC_SLOT_ROTATION_PITCH,
	SLIP_ARTIC_SLOT_ROTATION_ROW0_ROW1,
	SLIP_ARTIC_SLOT_ROTATION_ROW0_ROW2,
} SlipArticSlotRotation;

static void SlipArticSlot_Rotation(const SlipView3DMaths *maths, int16_t angle, SlipView3DMatrix *matrix) {
	(void)maths;
	(void)angle;
	(void)matrix;
}

static void SlipArticSlot_ApplyRotation(SlipArticSlotRotation rotation, const SlipView3DMaths *maths, int16_t angle,
                                        SlipView3DMatrix *matrix) {
	switch (rotation) {
	case SLIP_ARTIC_SLOT_ROTATION_NONE:
		SlipArticSlot_Rotation(maths, angle, matrix);
		break;
	case SLIP_ARTIC_SLOT_ROTATION_PITCH:
		SlipView3D_ApplyPitchMatrix(maths, angle, matrix);
		break;
	case SLIP_ARTIC_SLOT_ROTATION_ROW0_ROW1:
		SlipView3D_ApplyRow0Row1Rotation(maths, angle, matrix);
		break;
	case SLIP_ARTIC_SLOT_ROTATION_ROW0_ROW2:
		SlipView3D_ApplyRow0Row2Rotation(maths, angle, matrix);
		break;
	}
}

static int32_t SlipArticSlot_dirty;

static void SlipArticSlot_RebuildChildren(uint8_t *partRecord, uint8_t *artData, size_t artDataBytes,
                                          uint32_t artDataAddress, const SlipView3DMaths *maths) {
	const int32_t savedDirty = SlipArticSlot_dirty;
	const SlipArticPartRecord *const part = (const void *)partRecord;
	uint32_t childAddress = part->header.firstChild;

	if (childAddress != 0) {
		const uint32_t firstChildAddress = childAddress;

		do {
			uint8_t *const childRecord =
			    SlipArticSlot_PointerFromAddress(artData, artDataBytes, artDataAddress, childAddress);
			SlipArticPartRecord *const child = (void *)childRecord;

			if (SlipArticSlot_dirty != 0 || child->matrixValid == 0) {
				const uint32_t parentAddress = child->header.parent;
				uint8_t *const parentBytes =
				    SlipArticSlot_PointerFromAddress(artData, artDataBytes, artDataAddress, parentAddress);
				const SlipArticPartRecord *const parent = (const void *)parentBytes;
				SlipView3DMatrix parentMatrix;
				SlipView3DVec32 local;
				SlipView3DVec32 transformed;
				SlipArticSlotRotation rotation;

				child->matrixValid = UINT16_MAX;
				SlipArticSlot_dirty = -1;
				parentMatrix = parent->worldMatrix;
				child->worldMatrix = parentMatrix;
				local =
				    (SlipView3DVec32){(int16_t)child->header.localPosition.x, (int16_t)child->header.localPosition.y,
				                      (int16_t)child->header.localPosition.z};
				transformed = SlipView3D_TransformPosition16(&parentMatrix, local);
				child->header.worldPosition = (SlipView3DVec32){
				    (int32_t)((uint32_t)(int32_t)(int16_t)transformed.x + (uint32_t)parent->header.worldPosition.x),
				    (int32_t)((uint32_t)(int32_t)(int16_t)transformed.y + (uint32_t)parent->header.worldPosition.y),
				    (int32_t)((uint32_t)(int32_t)(int16_t)transformed.z + (uint32_t)parent->header.worldPosition.z)};
				rotation = (SlipArticSlotRotation)(child->rotationCallbackOffset >> 2);
				SlipArticSlot_ApplyRotation(rotation, maths, (int16_t)child->angle, &child->worldMatrix);
			}
			SlipArticSlot_RebuildChildren(childRecord, artData, artDataBytes, artDataAddress, maths);
			childAddress = child->header.nextSibling;
		} while (childAddress != firstChildAddress);
	}
	SlipArticSlot_dirty = savedDirty;
}

bool SlipArticSlot_TestOwner(uint16_t object, const SlipObject *objectTable, size_t objectTableBytes, uint8_t *slotPool,
                             size_t slotPoolBytes, uint32_t slotPoolAddress, uint8_t **actorRecordOut, bool *zeroFlag) {
	SlipObjectSlotDataReadResult slot;
	uint32_t actorAddress;
	uint32_t slotOffset;
	uint8_t *actorRecord;

	if (actorRecordOut == NULL || zeroFlag == NULL ||
	    !SlipObject_GetDrawData(objectTable, objectTableBytes, object, &slot)) {
		return false;
	}
	actorAddress = slot.drawData;
	if (actorAddress < slotPoolAddress) {
		return false;
	}
	slotOffset = actorAddress - slotPoolAddress;
	if (slotPool == NULL || (size_t)slotOffset + 0xc2u > slotPoolBytes) {
		return false;
	}
	actorRecord = slotPool + slotOffset;
	*actorRecordOut = actorRecord;
	*zeroFlag = object == ((const SlipArticActorHeader *)(const void *)actorRecord)->owner;
	return true;
}

bool SlipArticSlot_SelectDebris(uint16_t object, uint32_t destruction, const SlipObject *objects, size_t objectBytes,
                                uint8_t *pool, size_t poolBytes, uint32_t poolAddress, SlipArticDebrisEntry *selected) {
	uint8_t *actorStorage;
	bool owned;
	if (!SlipArticSlot_TestOwner(object, objects, objectBytes, pool, poolBytes, poolAddress, &actorStorage, &owned) ||
	    !owned)
		return false;
	const SlipArticActorHeader *const actor = (const SlipArticActorHeader *)(const void *)actorStorage;
	const uint32_t partOffset = actor->parts[0] - poolAddress;
	if (actor->parts[0] < poolAddress || partOffset > poolBytes || sizeof(SlipArticPartHeader) > poolBytes - partOffset)
		return false;
	const SlipArticPartHeader *const part = (const SlipArticPartHeader *)(const void *)(pool + partOffset);
	uint16_t count;
	const SlipArticDebrisEntry *entries;
	if (destruction != 0) {
		count = part->destructionCount;
		entries = part->destruction;
	} else {
		count = part->debrisCount;
		entries = part->debris;
	}
	if (count == 0)
		return false;
	const uint16_t index = (uint16_t)(((uint32_t)(uint16_t)SlipRandom_Next() * count) >> 16);
	selected->shape = entries[index].shape;
	selected->position = entries[index].position;
	return true;
}

bool SlipArticSlot_GetMainBounds(uint16_t object, const SlipObject *objectTable, size_t objectTableBytes,
                                 uint8_t *slotPool, size_t slotPoolBytes, uint32_t slotPoolAddress,
                                 SlipArticSlotMainBounds *result) {
	uint8_t *actorRecord;
	bool zeroFlag;

	if (result == NULL || !SlipArticSlot_TestOwner(object, objectTable, objectTableBytes, slotPool, slotPoolBytes,
	                                               slotPoolAddress, &actorRecord, &zeroFlag)) {
		return false;
	}
	if (!zeroFlag) {
		return false;
	}
	*result = (SlipArticSlotMainBounds){
	    (int32_t)SlipBytes_ReadLE32(actorRecord + 0x14u), (int32_t)SlipBytes_ReadLE32(actorRecord + 0x18u),
	    (int32_t)SlipBytes_ReadLE32(actorRecord + 0x1cu), (int32_t)SlipBytes_ReadLE32(actorRecord + 0x20u),
	    (int32_t)SlipBytes_ReadLE32(actorRecord + 0x24u), (int32_t)SlipBytes_ReadLE32(actorRecord + 0x28u)};
	return true;
}

bool SlipArticSlot_GetMainShape(uint16_t object, uint16_t shapeIndex, const SlipObject *objectTable,
                                size_t objectTableBytes, uint8_t *slotPool, size_t slotPoolBytes,
                                uint32_t slotPoolAddress, uint16_t *axOut, bool *selectionFailed) {
	uint8_t *actorRecord;
	bool zeroFlag;
	uint32_t mainPartAddress;
	uint32_t mainOffset;
	uint8_t *mainPart;

	if (axOut == NULL || selectionFailed == NULL ||
	    !SlipArticSlot_TestOwner(object, objectTable, objectTableBytes, slotPool, slotPoolBytes, slotPoolAddress,
	                             &actorRecord, &zeroFlag)) {
		return false;
	}
	if (!zeroFlag) {
		*selectionFailed = true;
		return true;
	}
	mainPartAddress = SlipBytes_ReadLE32(actorRecord + 0x30u);
	if (mainPartAddress < slotPoolAddress) {
		return false;
	}
	mainOffset = mainPartAddress - slotPoolAddress;
	if ((size_t)mainOffset > slotPoolBytes ||
	    slotPoolBytes - (size_t)mainOffset < (size_t)0x3eu + (size_t)shapeIndex * 2u) {
		return false;
	}
	mainPart = slotPool + mainOffset;
	*axOut = SlipBytes_ReadLE16(mainPart + 0x3cu + (size_t)shapeIndex * 2u);
	*selectionFailed = false;
	return true;
}

bool SlipArticSlot_GetExtent(uint16_t object, const SlipObject *objectTable, size_t objectTableBytes, uint8_t *slotPool,
                             size_t slotPoolBytes, uint32_t slotPoolAddress, int32_t *extentOut) {
	uint8_t *actorRecord;
	bool zeroFlag;

	if (extentOut == NULL || !SlipArticSlot_TestOwner(object, objectTable, objectTableBytes, slotPool, slotPoolBytes,
	                                                  slotPoolAddress, &actorRecord, &zeroFlag)) {
		return false;
	}
	if (!zeroFlag) {
		SlipRuntime_Fatal("ArticSlotGetExtent - this slot is not an artic slot");
	}
	*extentOut = (int32_t)SlipBytes_ReadLE32(actorRecord + 0x2cu);
	return true;
}

bool SlipArticSlot_Rebuild(uint16_t object, const SlipObject *objectTable, size_t objectTableBytes, uint8_t *slotPool,
                           size_t slotPoolBytes, uint32_t slotPoolAddress, uint8_t *artData, size_t artDataBytes,
                           uint32_t artDataAddress, const SlipView3DMaths *maths) {
	uint8_t *actorRecord;
	bool zeroFlag;
	SlipObjectPosition position;
	SlipObjectMatrixCopy matrixCopy;
	SlipView3DMatrix objectMatrix;
	bool matrixChanged;
	uint32_t mainPartAddress;
	uint8_t *mainPart;

	if (!SlipArticSlot_TestOwner(object, objectTable, objectTableBytes, slotPool, slotPoolBytes, slotPoolAddress,
	                             &actorRecord, &zeroFlag)) {
		return false;
	}
	if (!zeroFlag) {
		return true;
	}
	SlipArticSlot_dirty = 0;
	if (!SlipObject_Position(objectTable, objectTableBytes, object, &position)) {
		return false;
	}
	SlipArticActorRecord *const actor = (void *)actorRecord;
	matrixChanged = position.positionX != (uint32_t)actor->header.cachedPosition.x ||
	                position.positionY != (uint32_t)actor->header.cachedPosition.y ||
	                position.positionZ != (uint32_t)actor->header.cachedPosition.z;
	if (!matrixChanged) {
		if (!SlipObject_MatrixCopy(objectTable, objectTableBytes, object, &objectMatrix, &matrixCopy))
			return false;
		for (unsigned i = 0; i < 9; ++i) {
			if (actor->cachedMatrix.m[i] != objectMatrix.m[i]) {
				matrixChanged = true;
				break;
			}
		}
	}
	if (matrixChanged) {
		SlipArticSlot_dirty = -1;
		if (!SlipObject_MatrixCopy(objectTable, objectTableBytes, object, &objectMatrix, &matrixCopy))
			return false;
		actor->cachedMatrix = objectMatrix;
		if (!SlipObject_Position(objectTable, objectTableBytes, object, &position))
			return false;
		actor->header.cachedPosition =
		    (SlipView3DVec32){(int32_t)position.positionX, (int32_t)position.positionY, (int32_t)position.positionZ};
	}
	if (!SlipObject_MatrixCopy(objectTable, objectTableBytes, object, &objectMatrix, &matrixCopy)) {
		return false;
	}
	mainPartAddress = actor->header.parts[0];
	mainPart = SlipArticSlot_PointerFromAddress(artData, artDataBytes, artDataAddress, mainPartAddress);
	((SlipArticPartRecord *)(void *)mainPart)->worldMatrix = objectMatrix;
	SlipArticSlot_RebuildChildren(mainPart, artData, artDataBytes, artDataAddress, maths);
	return true;
}

bool SlipArticSlot_SelectPart(uint32_t partTag, uint16_t object, const SlipObject *objectTable, size_t objectTableBytes,
                              uint8_t *slotPool, size_t slotPoolBytes, uint32_t slotPoolAddress, uint8_t *artData,
                              size_t artDataBytes, uint32_t artDataAddress, SlipArticSlotPart *result) {
	uint8_t *actorRecord;
	bool zeroFlag;
	uint32_t selectedPartAddress;
	uint8_t *selectedHost;

	if (!SlipArticSlot_TestOwner(object, objectTable, objectTableBytes, slotPool, slotPoolBytes, slotPoolAddress,
	                             &actorRecord, &zeroFlag)) {
		return false;
	}
	if (!zeroFlag) {
		*result = (SlipArticSlotPart){NULL, 0, true};
		return true;
	}
	SlipArticActorHeader *const actor = (void *)actorRecord;
	if (partTag == 0x6d61696eu) {
		selectedPartAddress = actor->parts[0];
		selectedHost = SlipArticSlot_PointerFromAddress(artData, artDataBytes, artDataAddress, selectedPartAddress);
		*result = (SlipArticSlotPart){selectedHost, selectedPartAddress, false};
		return true;
	}
	if (partTag == actor->cachedPartTag) {
		selectedPartAddress = actor->parts[16];
		selectedHost = SlipArticSlot_PointerFromAddress(artData, artDataBytes, artDataAddress, selectedPartAddress);
		*result = (SlipArticSlotPart){selectedHost, selectedPartAddress, false};
		return true;
	}
	{
		uint32_t remaining = actor->partCount;
		uint32_t *entry = actor->parts;

		while (remaining != 0) {
			const uint32_t candidateAddress = *entry;
			uint8_t *const candidateRecord =
			    SlipArticSlot_PointerFromAddress(artData, artDataBytes, artDataAddress, candidateAddress);

			if (partTag == ((const SlipArticPartHeader *)(const void *)candidateRecord)->tag) {
				actor->parts[16] = candidateAddress;
				actor->cachedPartTag = partTag;
				*result = (SlipArticSlotPart){candidateRecord, candidateAddress, false};
				return true;
			}
			++entry;
			--remaining;
		}
	}
	*result = (SlipArticSlotPart){NULL, 0, true};
	return true;
}

bool SlipArticSlot_FindTag(uint32_t partTag, uint16_t object, const SlipObject *objectTable, size_t objectTableBytes,
                           uint8_t *slotPool, size_t slotPoolBytes, uint32_t slotPoolAddress, uint8_t *artData,
                           size_t artDataBytes, uint32_t artDataAddress, bool *carryOut) {
	SlipArticSlotPart selectedPartResult;

	if (!SlipArticSlot_SelectPart(partTag, object, objectTable, objectTableBytes, slotPool, slotPoolBytes,
	                              slotPoolAddress, artData, artDataBytes, artDataAddress, &selectedPartResult)) {
		return false;
	}
	*carryOut = selectedPartResult.selectionFailed;
	return true;
}

bool SlipArticSlot_Position(uint32_t partTag, uint32_t pointTag, uint16_t object, const SlipObject *objectTable,
                            size_t objectTableBytes, uint8_t *slotPool, size_t slotPoolBytes, uint32_t slotPoolAddress,
                            uint8_t *artData, size_t artDataBytes, uint32_t artDataAddress,
                            const SlipView3DMaths *maths, SlipArticSlotPosition *result) {
	SlipArticSlotPart selectedPartResult;
	uint32_t remaining;
	uint8_t *entry;

	if (!SlipArticSlot_SelectPart(partTag, object, objectTable, objectTableBytes, slotPool, slotPoolBytes,
	                              slotPoolAddress, artData, artDataBytes, artDataAddress, &selectedPartResult)) {
		return false;
	}
	if (selectedPartResult.selectionFailed) {
		*result = (SlipArticSlotPosition){0, 0, 0, true};
		return true;
	}
	if (!SlipArticSlot_Rebuild(object, objectTable, objectTableBytes, slotPool, slotPoolBytes, slotPoolAddress, artData,
	                           artDataBytes, artDataAddress, maths)) {
		return false;
	}
	remaining = SlipBytes_ReadLE32(selectedPartResult.partRecord + 0x10cu);
	entry = selectedPartResult.partRecord + 0x110u;
	while (remaining != 0) {
		if (pointTag == SlipBytes_ReadLE32(entry)) {
			*result = (SlipArticSlotPosition){SlipBytes_ReadLE32(entry + 0x04u), SlipBytes_ReadLE32(entry + 0x08u),
			                                  SlipBytes_ReadLE32(entry + 0x0cu), false};
			return true;
		}
		entry += 0x10u;
		--remaining;
	}
	*result = (SlipArticSlotPosition){0, 0, 0, true};
	return true;
}

bool SlipArticSlot_WorldPosition(uint32_t partTag, uint32_t pointTag, uint16_t object, const SlipObject *objectTable,
                                 size_t objectTableBytes, uint8_t *slotPool, size_t slotPoolBytes,
                                 uint32_t slotPoolAddress, uint8_t *artData, size_t artDataBytes,
                                 uint32_t artDataAddress, const SlipView3DMaths *maths, SlipArticSlotPosition *result) {
	SlipArticSlotPart selectedPartResult;
	SlipObjectPosition objectPosition;
	SlipView3DMatrix partMatrix;
	SlipView3DVec32 transformed;
	uint32_t remaining;
	const SlipArticNamedPoint *entry;

	if (result == NULL || maths == NULL ||
	    !SlipArticSlot_SelectPart(partTag, object, objectTable, objectTableBytes, slotPool, slotPoolBytes,
	                              slotPoolAddress, artData, artDataBytes, artDataAddress, &selectedPartResult)) {
		return false;
	}
	if (selectedPartResult.selectionFailed) {

		result->lookupFailed = true;
		return true;
	}

	if (!SlipArticSlot_Rebuild(object, objectTable, objectTableBytes, slotPool, slotPoolBytes, slotPoolAddress, artData,
	                           artDataBytes, artDataAddress, maths) ||
	    !SlipObject_Position(objectTable, objectTableBytes, object, &objectPosition)) {
		return false;
	}

	const SlipArticPartRecord *const part = (const void *)selectedPartResult.partRecord;
	remaining = part->namedPointCount;
	entry = part->namedPoints;
	while (remaining != 0) {
		if (pointTag == entry->tag) {
			break;
		}
		++entry;
		--remaining;
	}
	if (remaining == 0) {

		*result = (SlipArticSlotPosition){objectPosition.positionX, objectPosition.positionY, 0, true};
		return true;
	}

	partMatrix = part->worldMatrix;
	transformed = SlipView3D_TransformPosition16(&partMatrix, entry->position);

	*result = (SlipArticSlotPosition){
	    (uint32_t)transformed.x + (uint32_t)part->header.worldPosition.x + objectPosition.positionX,
	    (uint32_t)transformed.y + (uint32_t)part->header.worldPosition.y + objectPosition.positionY,
	    (uint32_t)transformed.z + (uint32_t)part->header.worldPosition.z + objectPosition.positionZ, false};
	return true;
}

bool SlipArticSlot_SetAngle(uint32_t partTag, uint16_t angle, uint16_t object, const SlipObject *objectTable,
                            size_t objectTableBytes, uint8_t *slotPool, size_t slotPoolBytes, uint32_t slotPoolAddress,
                            uint8_t *artData, size_t artDataBytes, uint32_t artDataAddress, bool *carryOut) {
	SlipArticSlotPart selectedPartResult;
	uint16_t previousAngle;

	if (!SlipArticSlot_SelectPart(partTag, object, objectTable, objectTableBytes, slotPool, slotPoolBytes,
	                              slotPoolAddress, artData, artDataBytes, artDataAddress, &selectedPartResult)) {
		return false;
	}
	*carryOut = selectedPartResult.selectionFailed;
	if (selectedPartResult.selectionFailed) {
		return true;
	}
	previousAngle = SlipBytes_ReadLE16(selectedPartResult.partRecord + 0x10au);
	SlipArticSlot_Write16(selectedPartResult.partRecord + 0x10au, angle);
	if (previousAngle != angle) {
		SlipArticSlot_Write16(selectedPartResult.partRecord + 0x108u, 0u);
	}
	return true;
}
