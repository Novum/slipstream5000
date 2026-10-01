#ifndef SLIPSTREAM5000_ARTIC_SLOT_H
#define SLIPSTREAM5000_ARTIC_SLOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "resource.h"
#include "view3d.h"

typedef struct SlipObject SlipObject;

#pragma pack(push, 2)

typedef struct SlipArticDebrisEntry {
	uint16_t shape;
	uint16_t unusedStorage;
	SlipView3DVec32 position;
} SlipArticDebrisEntry;

typedef struct SlipArticPartHeader {
	uint32_t tag;
	uint32_t parent;
	uint32_t firstChild;
	uint32_t nextSibling;
	uint32_t previousSibling;
	SlipView3DVec32 localPosition;
	uint32_t drawShape;
	SlipView3DVec32 worldPosition;
	SlipView3DVec32 drawPosition;
	uint16_t shapes[8];
	uint16_t replayShapes[8];
	uint16_t debrisCount;
	SlipArticDebrisEntry debris[4];
	uint16_t destructionCount;
	SlipArticDebrisEntry destruction[4];
} SlipArticPartHeader;

typedef struct SlipArticNamedPoint {
	uint32_t tag;
	SlipView3DVec32 position;
} SlipArticNamedPoint;

typedef struct SlipArticPartRecord {
	SlipArticPartHeader header;
	uint32_t rotationCallbackOffset;
	SlipView3DMatrix worldMatrix;
	SlipView3DMatrix drawMatrix;
	uint16_t matrixValid;
	uint16_t angle;
	uint32_t namedPointCount;
	SlipArticNamedPoint namedPoints[5];
} SlipArticPartRecord;

typedef struct SlipArticActorHeader {
	uint32_t next;
	uint32_t previous;
	SlipView3DVec32 cachedPosition;
	SlipView3DVec32 minimum;
	SlipView3DVec32 maximum;
	uint32_t radius;
	uint32_t parts[17];
	uint32_t partCount;
	uint32_t cachedPartTag;
	uint32_t lodDistances[8];
	uint32_t replayLodDistances[8];
	uint32_t childrenInSortTree;
	uint16_t owner;
	uint16_t resource;
} SlipArticActorHeader;

typedef struct SlipArticActorRecord {
	SlipArticActorHeader header;
	SlipView3DMatrix cachedMatrix;
} SlipArticActorRecord;

#pragma pack(pop)

bool SlipArticSlot_SelectDebris(uint16_t object, uint32_t destruction, const SlipObject *objects, size_t objectBytes,
                                uint8_t *pool, size_t poolBytes, uint32_t poolAddress, SlipArticDebrisEntry *selected);

typedef struct SlipArticSlotPart {
	uint8_t *partRecord;
	uint32_t partAddress;
	bool selectionFailed;
} SlipArticSlotPart;

typedef struct SlipArticSlotPosition {
	uint32_t positionX;
	uint32_t positionY;
	uint32_t positionZ;
	bool lookupFailed;
} SlipArticSlotPosition;

bool SlipArticSlot_WorldPosition(uint32_t partTag, uint32_t pointTag, uint16_t object, const SlipObject *objectTable,
                                 size_t objectTableBytes, uint8_t *slotPool, size_t slotPoolBytes,
                                 uint32_t slotPoolAddress, uint8_t *artData, size_t artDataBytes,
                                 uint32_t artDataAddress, const SlipView3DMaths *maths, SlipArticSlotPosition *result);

typedef struct SlipArticSlotMainBounds {
	int32_t minX;
	int32_t minY;
	int32_t minZ;
	int32_t maxX;
	int32_t maxY;
	int32_t maxZ;
} SlipArticSlotMainBounds;

typedef struct SlipArticSlotResourceCalls {
	void *context;
	bool (*allocate)(void *, uint32_t, uint32_t, uint16_t *);
	const uint8_t *(*lock)(void *, uint16_t);
	SlipResourcePayload (*payload)(uint16_t);
	void (*unlock)(void *, uint16_t);
	void (*release)(void *, uint16_t);
	void (*registerExit)(void *, void (*)(void));
} SlipArticSlotResourceCalls;

typedef struct SlipArticSlotPool {
	uint8_t *allocation;
	size_t allocationBytes;
	uint32_t allocationAddress;
	uint8_t *activeSentinel;
	uint32_t activeSentinelAddress;
	uint8_t *actorFreeSentinel;
	uint32_t actorFreeSentinelAddress;
	uint8_t *partFreeSentinel;
	uint32_t partFreeSentinelAddress;
	uint16_t actorCount;
	uint16_t resource;
	const SlipArticSlotResourceCalls *resourceCalls;
} SlipArticSlotPool;

bool SlipArticSlot_InitializeResourcePool(uint16_t actorCount, const SlipArticSlotResourceCalls *calls,
                                          SlipArticSlotPool *result);

void SlipArticSlot_Shutdown(SlipArticSlotPool *pool);

typedef struct SlipArticSlotAllocate {
	uint8_t *actorRecord;
	uint32_t actorAddress;
	bool allocationFailed;
} SlipArticSlotAllocate;

typedef struct SlipArticSlotAllocatePart {
	uint8_t *partRecord;
	uint32_t partAddress;
} SlipArticSlotAllocatePart;

typedef struct SlipArticSlotCreate {
	uint8_t *actorRecord;
	uint32_t actorAddress;
	bool creationFailed;
} SlipArticSlotCreate;

typedef int (*SlipArticSlotFindResource)(void *user, const char name[13], uint32_t *resourceHandle);

extern uint32_t SlipArticSlot_initialized;

uint32_t SlipArticSlot_PoolBytes(uint16_t actorCount);

bool SlipArticSlot_InitializePool(uint16_t actorCount, uint8_t *allocation, size_t allocationBytes,
                                  uint32_t allocationAddress, SlipArticSlotPool *result);

bool SlipArticSlot_Allocate(SlipArticSlotPool *pool, SlipArticSlotAllocate *result);

void SlipArticSlot_AllocatePart(SlipArticSlotPool *pool, uint32_t parentAddress, SlipArticSlotAllocatePart *result);

bool SlipArticSlot_InitializeParts(SlipArticSlotPool *pool, uint32_t actorAddress, const uint8_t *body,
                                   uint32_t parentAddress, const uint8_t *payload, size_t payloadBytes,
                                   SlipArticSlotFindResource findResource, void *findResourceUser);

typedef void (*SlipArticSlotReleaseResource)(void *user, uint32_t handle);
void SlipArticSlot_ReleaseResources(const uint8_t *payload, SlipArticSlotFindResource findResource,
                                    SlipArticSlotReleaseResource releaseResource, void *user);

void SlipArticSlot_PreloadResources(const uint8_t *payload, SlipArticSlotFindResource findResource,
                                    void *findResourceUser);

bool SlipArticSlot_Create(uint16_t object, uint16_t resourceHandle, const uint8_t *payloadFrom, size_t payloadBytesFrom,
                          SlipArticSlotPool *pool, SlipObject *objectTable, size_t objectTableBytes,
                          SlipArticSlotFindResource findResource, void *findResourceUser, SlipArticSlotCreate *result);

bool SlipArticSlot_TestOwner(uint16_t object, const SlipObject *objectTable, size_t objectTableBytes, uint8_t *slotPool,
                             size_t slotPoolBytes, uint32_t slotPoolAddress, uint8_t **actorRecord, bool *zeroFlag);

bool SlipArticSlot_GetMainBounds(uint16_t object, const SlipObject *objectTable, size_t objectTableBytes,
                                 uint8_t *slotPool, size_t slotPoolBytes, uint32_t slotPoolAddress,
                                 SlipArticSlotMainBounds *result);

bool SlipArticSlot_GetMainShape(uint16_t object, uint16_t shapeIndex, const SlipObject *objectTable,
                                size_t objectTableBytes, uint8_t *slotPool, size_t slotPoolBytes,
                                uint32_t slotPoolAddress, uint16_t *axOut, bool *selectionFailed);

bool SlipArticSlot_GetExtent(uint16_t object, const SlipObject *objectTable, size_t objectTableBytes, uint8_t *slotPool,
                             size_t slotPoolBytes, uint32_t slotPoolAddress, int32_t *extentOut);

bool SlipArticSlot_SelectPart(uint32_t partTag, uint16_t object, const SlipObject *objectTable, size_t objectTableBytes,
                              uint8_t *slotPool, size_t slotPoolBytes, uint32_t slotPoolAddress, uint8_t *artData,
                              size_t artDataBytes, uint32_t artDataAddress, SlipArticSlotPart *result);

bool SlipArticSlot_FindTag(uint32_t partTag, uint16_t object, const SlipObject *objectTable, size_t objectTableBytes,
                           uint8_t *slotPool, size_t slotPoolBytes, uint32_t slotPoolAddress, uint8_t *artData,
                           size_t artDataBytes, uint32_t artDataAddress, bool *carryOut);

bool SlipArticSlot_Rebuild(uint16_t object, const SlipObject *objectTable, size_t objectTableBytes, uint8_t *slotPool,
                           size_t slotPoolBytes, uint32_t slotPoolAddress, uint8_t *artData, size_t artDataBytes,
                           uint32_t artDataAddress, const SlipView3DMaths *maths);

bool SlipArticSlot_Position(uint32_t partTag, uint32_t pointTag, uint16_t object, const SlipObject *objectTable,
                            size_t objectTableBytes, uint8_t *slotPool, size_t slotPoolBytes, uint32_t slotPoolAddress,
                            uint8_t *artData, size_t artDataBytes, uint32_t artDataAddress,
                            const SlipView3DMaths *maths, SlipArticSlotPosition *result);

bool SlipArticSlot_SetAngle(uint32_t partTag, uint16_t angle, uint16_t object, const SlipObject *objectTable,
                            size_t objectTableBytes, uint8_t *slotPool, size_t slotPoolBytes, uint32_t slotPoolAddress,
                            uint8_t *artData, size_t artDataBytes, uint32_t artDataAddress, bool *carryOut);

#endif
