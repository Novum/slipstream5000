#ifndef SLIPSTREAM5000_RESOURCE_STORAGE_H
#define SLIPSTREAM5000_RESOURCE_STORAGE_H
#include "actor_pool.h"
#include "race_collision.h"
#include "resource_allocation.h"
#include "resource_names.h"

typedef struct SlipResourceStorageNode SlipResourceStorageNode;

typedef struct SlipResourceStorage {
	uint8_t *bytes;
	uint32_t size;
	SlipResourceStorageNode *nodes;
	/* Optional address supplied by the platform binding, never a native pointer. */
	uint32_t linearAddress;
	bool hasLinearAddress;
} SlipResourceStorage;

void SlipResourceStorage_Bind(SlipResourceStorage *, uint8_t *bytes, uint32_t size);
void SlipResourceStorage_ReleaseViews(SlipResourceStorage *);
SlipRaceCollisionVertex *SlipResourceStorage_CollisionVertices(SlipResourceBlock *);
SlipResourceBlock *SlipResourceStorage_BlockAt(SlipResourceStorage *, uint32_t offset);
uint32_t SlipResourceStorage_BlockOffset(const SlipResourceBlock *);
bool SlipResourceStorage_BlockLinearAddress(const SlipResourceBlock *, uint32_t *address);

void SlipResourceStorage_BindImageBase(uint32_t address);
struct SlipView3DMatrix;
void SlipResourceStorage_HeaderMatrix(const SlipResourceBlock *, struct SlipView3DMatrix *);
SlipResourceBlock *SlipResourceStorage_ResizeRemainder(void *, SlipResourceBlock *, uint32_t alignedBytes);
uint8_t *SlipResourceStorage_Payload(SlipResourceBlock *);
struct SlipSprite;

struct SlipSprite *SlipResourceStorage_GeneratedSprite(SlipResourceBlock *);
struct RasterPerspectiveEntry;
struct RasterPerspectiveEntry *SlipResourceStorage_PerspectiveTable(SlipResourceBlock *);

SlipActorPoolStorage SlipResourceStorage_ActorPool(SlipResourceBlock *, uint16_t actors, uint16_t parts);
union SlipDraw3DVertexRecord;
union SlipDraw3DVertexRecord *SlipResourceStorage_Vertices(SlipResourceBlock *, uint32_t count);
uint16_t *SlipResourceStorage_Specular(SlipResourceBlock *, uint32_t count);
struct SlipDraw3DMaterialTable;
struct SlipDraw3DMaterialTable *SlipResourceStorage_Materials(SlipResourceBlock *, uint32_t count);
struct SlipRendererPolygon;
struct SlipDraw3DListNode;
struct SlipDraw3DListNode *SlipResourceStorage_ListNodes(SlipResourceBlock *, uint32_t count);
struct SlipDraw3DRecordPool;
/* Offset-linked view of the same polygon allocation used by the byte-oriented renderer. */
struct SlipDraw3DRecordPool *SlipResourceStorage_RecordPool(SlipResourceBlock *);
struct SlipRendererDrawState;
struct SlipDraw3DStateRecord;
/* Offset-cursor view of the same draw-state resource consumed by the byte renderer. */
struct SlipDraw3DStateRecord *SlipResourceStorage_DrawStateRecords(SlipResourceBlock *, uint32_t count);
struct RasterTexturedPoint;
struct SlipRendererPolygon *SlipResourceStorage_Polygons(SlipResourceBlock *, uint32_t count);
struct SlipRendererDrawState *SlipResourceStorage_DrawStates(SlipResourceBlock *, uint32_t count);
struct RasterTexturedPoint *SlipResourceStorage_Points(SlipResourceBlock *, uint32_t count);

/* Signature adapters to the existing translated allocator and table creators. */
SlipResourceBlock *SlipResourceStorage_Front(void *, SlipResourceBlock *, uint32_t);
bool SlipResourceStorage_Back(void *, SlipResourceBlock *, uint32_t, SlipResourceBlock **);
extern const SlipResourceAllocationCalls SlipResourceStorage_allocationCalls;
bool SlipResourceStorage_HandleTable(void *, uint32_t bytes, SlipResourceHandleAllocation *);
bool SlipResourceStorage_NameTable(void *, uint32_t bytes, SlipResourceNameAllocation *);
#endif
