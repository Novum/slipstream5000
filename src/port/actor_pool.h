#ifndef SLIPSTREAM5000_ACTOR_POOL_H
#define SLIPSTREAM5000_ACTOR_POOL_H
#include "view3d.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct SlipActorDebris {
	uint16_t shape;
	SlipView3DVec32 position;
} SlipActorDebris;

typedef struct SlipActorNamedPoint {
	uint32_t tag;
	SlipView3DVec32 position;
} SlipActorNamedPoint;

typedef struct SlipActorPartRecord {
	struct SlipActorPartRecord *parent, *firstChild;           /* part+04,08 */
	struct SlipActorPartRecord *nextSibling, *previousSibling; /* part+0c,10 */
	uint32_t tag;                                              /* part+00 */
	SlipView3DVec32 localPosition, worldPosition;              /* part+14,24 */
	uint16_t shapes[8], replayShapes[8];                       /* part+3c,4c */
	uint16_t debrisCount, destructionCount;                    /* part+5c,9e */
	SlipActorDebris debris[4], destruction[4];                 /* part+5e,a0 */
	uint32_t rotationCallbackOffset;
	uint16_t matrixValid, angle;        /* part+108,10a */
	SlipView3DMatrix worldMatrix;       /* part+e4 */
	SlipView3DMatrix drawMatrix;        /* part+f6 */
	SlipView3DVec32 drawPosition;       /* part+30 */
	uint16_t drawShape;                 /* resourceHandle word of part+20 */
	bool hasDrawShape;                  /* part+20 != ffffffff */
	uint32_t namedPointCount;           /* part+10c */
	SlipActorNamedPoint namedPoints[5]; /* part+110 */
} SlipActorPartRecord;

typedef struct SlipActorRecord {
	struct SlipActorRecord *next, *previous;          /* actor+00,04 */
	SlipActorPartRecord *parts[17];                   /* actor+30 */
	uint32_t partCount;                               /* actor+74 */
	uint16_t ownerObject;                             /* actor+c0 */
	uint16_t resourceHandle;                          /* actor+c2 */
	SlipView3DVec32 cachedPosition, minimum, maximum; /* actor+08,14,20 */
	uint32_t radius, cachedPartTag;                   /* actor+2c,78 */
	uint32_t lodDistances[8], replayLodDistances[8];  /* actor+7c,9c */
	uint32_t childrenInSortTree;
	SlipView3DMatrix cachedMatrix; /* actor+c4 */
} SlipActorRecord;

typedef struct SlipActorPool {
	SlipActorRecord *activeSentinel, *actorFreeSentinel;
	SlipActorPartRecord *partFreeSentinel;
	uint32_t actorCount;
	uint16_t partCount;
	uint32_t initialized, mode;
	uint32_t partRegionOffset;
	uint16_t resourceHandle;
} SlipActorPool;

void SlipActorPool_InitializeActors(SlipActorPool *);
void SlipActorPool_InitializeParts(SlipActorPool *);
bool SlipActorPool_Allocate(SlipActorPool *, SlipActorRecord **);
SlipActorPartRecord *SlipActorPool_AllocatePart(SlipActorPool *, SlipActorPartRecord *parent);
void SlipActorPool_FreePart(SlipActorPool *, SlipActorPartRecord *);
void SlipActorPool_FreeActor(SlipActorPool *, SlipActorRecord *);

typedef struct SlipActorPoolStorage {
	SlipActorRecord *actors;
	SlipActorPartRecord *parts;
} SlipActorPoolStorage;
typedef struct SlipActorPoolCalls SlipActorPoolCalls;
typedef void (*SlipActorPoolCleanup)(SlipActorPool *, const SlipActorPoolCalls *);
typedef void (*SlipActorPoolEvent)(SlipActorPool *, uint16_t object, uint16_t events, const SlipActorPoolCalls *);

struct SlipActorPoolCalls {
	void *context;
	bool (*allocate)(void *, uint32_t bytes, uint16_t flags, uint16_t *resourceHandle);
	SlipActorPoolStorage (*lock)(void *, uint16_t resourceHandle);
	void (*registerExit)(void *, SlipActorPoolCleanup);
	void (*registerEvent)(void *, uint16_t event, SlipActorPoolEvent);
	void (*destroyObjectActor)(void *, uint16_t object);
	void (*unlock)(void *, uint16_t resourceHandle);
	void (*release)(void *, uint16_t resourceHandle);
};

bool SlipActorPool_Initialize(SlipActorPool *, uint16_t actors, const SlipActorPoolCalls *);
void SlipActorPool_ObjectEvent(SlipActorPool *, uint16_t object, uint16_t events, const SlipActorPoolCalls *);
void SlipActorPool_Shutdown(SlipActorPool *, const SlipActorPoolCalls *);
void SlipActorPool_SetMode(SlipActorPool *, uint32_t mode);
uint32_t SlipActorPool_GetMode(const SlipActorPool *);
#endif
