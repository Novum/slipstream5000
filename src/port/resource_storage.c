#include "resource_storage.h"
#include "draw3d.h"
#include "renderer_lifecycle.h"
#include "runtime.h"
#include "sprite.h"
#include "sprite_format.h"
#include <stdlib.h>

struct SlipResourceStorageNode {
	SlipResourceBlock block;
	SlipResourceStorage *storage;
	uint32_t blockOffset;
	SlipResourceHandle *handles;
	SlipResourceNameEntry *names;
	SlipActorRecord *actors;
	SlipActorPartRecord *parts;
	size_t actorCapacity, partCapacity;
	SlipDraw3DVertexRecord *vertices;
	uint16_t *specular;
	size_t vertexCapacity, specularCapacity;
	SlipRendererPolygon *polygons;
	SlipDraw3DListNode *listNodes;
	SlipRaceCollisionVertex *collisionVertices;
	size_t listNodeCapacity;
	SlipDraw3DRecordPool *recordPool;
	SlipRendererDrawState *drawStates;
	SlipDraw3DStateRecord *drawStateRecords;
	size_t drawStateRecordCapacity;
	RasterTexturedPoint *points;
	RasterPerspectiveEntry *perspectiveTable;
	size_t polygonCapacity, drawStateCapacity, pointCapacity;
	SlipDraw3DMaterialTable *materials;
	size_t materialCapacity;
	SlipSprite generatedSprite;
	SlipResourceStorageNode *next;
};

/* Offsets of the original resource list sentinels relative to the bound DOS image. */
enum {
	SLIP_RESOURCE_FREE_BLOCKS_IMAGE_OFFSET = 0x242d4,
	SLIP_RESOURCE_ALLOCATED_BLOCKS_IMAGE_OFFSET = 0x242f4,
	SLIP_RESOURCE_HEADER_WORD_BITS = 16,
	SLIP_RESOURCE_HEADER_LINK_COUNT = 4
};

static bool hasImageBase;
static uint32_t imageBase;

void SlipResourceStorage_BindImageBase(uint32_t address) {
	hasImageBase = true;
	imageBase = address;
}

static uint32_t SlipResourceStorage_BlockAbiAddress(const SlipResourceBlock *block) {
	if (block == NULL)
		return 0;
	if (block == &SlipResource_freeBlocks)
		return hasImageBase ? imageBase + SLIP_RESOURCE_FREE_BLOCKS_IMAGE_OFFSET : (uint32_t)(uintptr_t)block;
	if (block == &SlipResource_allocatedBlocks)
		return hasImageBase ? imageBase + SLIP_RESOURCE_ALLOCATED_BLOCKS_IMAGE_OFFSET : (uint32_t)(uintptr_t)block;
	const SlipResourceStorageNode *const node = (const SlipResourceStorageNode *)block;
	return node->storage->hasLinearAddress ? node->storage->linearAddress + node->blockOffset
	                                       : (uint32_t)(uintptr_t)(node->storage->bytes + node->blockOffset);
}

void SlipResourceStorage_HeaderMatrix(const SlipResourceBlock *block, SlipView3DMatrix *matrix) {
	const uint32_t links[SLIP_RESOURCE_HEADER_LINK_COUNT] = {
	    SlipResourceStorage_BlockAbiAddress(block->next), SlipResourceStorage_BlockAbiAddress(block->previous),
	    SlipResourceStorage_BlockAbiAddress(block->physicalPrevious),
	    SlipResourceStorage_BlockAbiAddress(block->physicalNext)};
	for (unsigned link = 0; link < SLIP_RESOURCE_HEADER_LINK_COUNT; ++link) {
		matrix->m[link * 2] = (int16_t)(uint16_t)links[link];
		matrix->m[link * 2 + 1] = (int16_t)(uint16_t)(links[link] >> SLIP_RESOURCE_HEADER_WORD_BITS);
	}
	matrix->m[8] = (int16_t)(uint16_t)block->capacityBytes;
}

void SlipResourceStorage_Bind(SlipResourceStorage *storage, uint8_t *bytes, uint32_t size) {
	*storage = (SlipResourceStorage){.bytes = bytes, .size = size};
}

void SlipResourceStorage_ReleaseViews(SlipResourceStorage *storage) {
	while (storage->nodes != NULL) {
		SlipResourceStorageNode *const node = storage->nodes;
		storage->nodes = node->next;
		free(node->handles);
		free(node->names);
		free(node->actors);
		free(node->parts);
		free(node->vertices);
		free(node->specular);
		free(node->polygons);
		free(node->listNodes);
		free(node->collisionVertices);
		free(node->recordPool);
		free(node->drawStates);
		free(node->drawStateRecords);
		free(node->points);
		free(node->perspectiveTable);
		free(node->materials);
		free(node);
	}
}

SlipResourceBlock *SlipResourceStorage_BlockAt(SlipResourceStorage *storage, uint32_t offset) {
	for (SlipResourceStorageNode *node = storage->nodes; node != NULL; node = node->next) {
		if (node->blockOffset == offset)
			return &node->block;
	}

	if (storage->size < SLIP_RESOURCE_DOS_BLOCK_HEADER_BYTES ||
	    offset > storage->size - SLIP_RESOURCE_DOS_BLOCK_HEADER_BYTES)
		SlipRuntime_Fatal("Resource storage view lies outside its host region");
	SlipResourceStorageNode *const node = calloc(1, sizeof(*node));
	if (node == NULL)
		SlipRuntime_Fatal("Cannot allocate native resource metadata");
	node->storage = storage;
	node->blockOffset = offset;
	node->next = storage->nodes;
	storage->nodes = node;
	return &node->block;
}

uint32_t SlipResourceStorage_BlockOffset(const SlipResourceBlock *block) {
	return ((const SlipResourceStorageNode *)block)->blockOffset;
}

enum { SLIP_RESOURCE_PERSPECTIVE_TABLE_CAPACITY = 512 };

RasterPerspectiveEntry *SlipResourceStorage_PerspectiveTable(SlipResourceBlock *block) {
	SlipResourceStorageNode *const node = (SlipResourceStorageNode *)block;
	if (node->perspectiveTable == NULL) {
		node->perspectiveTable = malloc(SLIP_RESOURCE_PERSPECTIVE_TABLE_CAPACITY * sizeof(*node->perspectiveTable));
		if (node->perspectiveTable == NULL)
			SlipRuntime_Fatal("Cannot allocate native perspective table records");
	}
	return node->perspectiveTable;
}

bool SlipResourceStorage_BlockLinearAddress(const SlipResourceBlock *block, uint32_t *address) {
	if (block == NULL || block == &SlipResource_freeBlocks || block == &SlipResource_allocatedBlocks)
		return false;
	const SlipResourceStorageNode *const node = (const SlipResourceStorageNode *)block;
	if (!node->storage->hasLinearAddress)
		return false;
	*address = node->storage->linearAddress + node->blockOffset;
	return true;
}

SlipResourceBlock *SlipResourceStorage_ResizeRemainder(void *context, SlipResourceBlock *block, uint32_t alignedBytes) {
	(void)context;
	SlipResourceStorageNode *const node = (SlipResourceStorageNode *)block;
	return SlipResourceStorage_BlockAt(node->storage,
	                                   node->blockOffset + alignedBytes + SLIP_RESOURCE_DOS_BLOCK_HEADER_BYTES);
}

uint8_t *SlipResourceStorage_Payload(SlipResourceBlock *block) {
	SlipResourceStorageNode *const node = (SlipResourceStorageNode *)block;

	return node->storage->bytes + node->blockOffset + SLIP_RESOURCE_DOS_BLOCK_HEADER_BYTES;
}

SlipSprite *SlipResourceStorage_GeneratedSprite(SlipResourceBlock *block) {
	SlipResourceStorageNode *const node = (SlipResourceStorageNode *)block;

	node->generatedSprite.pixels = SlipResourceStorage_Payload(block) + SLIP_SPRITE_HEADER_BYTES;
	return &node->generatedSprite;
}

SlipActorPoolStorage SlipResourceStorage_ActorPool(SlipResourceBlock *block, uint16_t actors, uint16_t parts) {
	SlipResourceStorageNode *const node = (SlipResourceStorageNode *)block;
	const size_t actorCapacity = (size_t)actors + 2u;
	const size_t partCapacity = (size_t)parts + 1u;
	if (actorCapacity > node->actorCapacity) {
		SlipActorRecord *const records = realloc(node->actors, actorCapacity * sizeof(*records));
		if (records == NULL)
			SlipRuntime_Fatal("Cannot allocate native actor views");
		node->actors = records;
		node->actorCapacity = actorCapacity;
	}
	if (partCapacity > node->partCapacity) {
		SlipActorPartRecord *const records = realloc(node->parts, partCapacity * sizeof(*records));
		if (records == NULL)
			SlipRuntime_Fatal("Cannot allocate native actor part views");
		node->parts = records;
		node->partCapacity = partCapacity;
	}
	return (SlipActorPoolStorage){node->actors, node->parts};
}

/* Native representation storage. These arrays are never byte-decoded and
 * repeated locks retain addresses and untouched values within their capacity. */
SlipDraw3DVertexRecord *SlipResourceStorage_Vertices(SlipResourceBlock *block, uint32_t count) {
	SlipResourceStorageNode *const node = (SlipResourceStorageNode *)block;

	const size_t capacity = count != 0 ? count : 1;
	if (capacity > node->vertexCapacity) {
		SlipDraw3DVertexRecord *const vertices = realloc(node->vertices, capacity * sizeof(*vertices));
		if (vertices == NULL)
			SlipRuntime_Fatal("Cannot allocate native vertex views");
		node->vertices = vertices;
		node->vertexCapacity = capacity;
	}
	return node->vertices;
}

uint16_t *SlipResourceStorage_Specular(SlipResourceBlock *block, uint32_t count) {
	SlipResourceStorageNode *const node = (SlipResourceStorageNode *)block;
	if (count > node->specularCapacity) {
		uint16_t *const specular = realloc(node->specular, (size_t)count * sizeof(*specular));
		if (specular == NULL)
			SlipRuntime_Fatal("Cannot allocate native specular views");
		node->specular = specular;
		node->specularCapacity = count;
	}
	return node->specular;
}

SlipDraw3DListNode *SlipResourceStorage_ListNodes(SlipResourceBlock *block, uint32_t count) {
	SlipResourceStorageNode *const node = (SlipResourceStorageNode *)block;
	if (count > node->listNodeCapacity) {
		SlipDraw3DListNode *const nodes = realloc(node->listNodes, (size_t)count * sizeof(*nodes));
		if (nodes == NULL)
			SlipRuntime_Fatal("Cannot allocate native draw-list views");
		node->listNodes = nodes;
		node->listNodeCapacity = count;
	}
	return node->listNodes;
}

SlipRaceCollisionVertex *SlipResourceStorage_CollisionVertices(SlipResourceBlock *block) {
	SlipResourceStorageNode *const node = (SlipResourceStorageNode *)block;
	if (node->collisionVertices == NULL) {
		node->collisionVertices = malloc(SLIP_COLLISION_VERTEX_COUNT * sizeof(*node->collisionVertices));
		if (node->collisionVertices == NULL)
			SlipRuntime_Fatal("Cannot allocate native collision vertex views");
	}
	return node->collisionVertices;
}

SlipDraw3DRecordPool *SlipResourceStorage_RecordPool(SlipResourceBlock *block) {
	SlipResourceStorageNode *const node = (SlipResourceStorageNode *)block;
	if (node->recordPool == NULL) {
		node->recordPool = malloc(sizeof(*node->recordPool));
		if (node->recordPool == NULL)
			SlipRuntime_Fatal("Cannot allocate native polygon offset view");
	}
	return node->recordPool;
}

/* The expanded material table has a count followed by typed 54h records.
 * Storage binding does not set that count or
 * alter any existing material. */
SlipDraw3DMaterialTable *SlipResourceStorage_Materials(SlipResourceBlock *block, uint32_t count) {
	SlipResourceStorageNode *const node = (SlipResourceStorageNode *)block;
	if (node->materials == NULL || count > node->materialCapacity) {
		SlipDraw3DMaterialTable *const table =
		    realloc(node->materials, sizeof(*table) + (size_t)count * sizeof(table->records[0]));
		if (table == NULL)
			SlipRuntime_Fatal("Cannot allocate native material views");
		node->materials = table;
		node->materialCapacity = count;
	}
	return node->materials;
}

SlipRendererPolygon *SlipResourceStorage_Polygons(SlipResourceBlock *block, uint32_t count) {
	SlipResourceStorageNode *const node = (SlipResourceStorageNode *)block;
	if (count > node->polygonCapacity) {
		SlipRendererPolygon *const polygons = realloc(node->polygons, (size_t)count * sizeof(*polygons));
		if (polygons == NULL)
			SlipRuntime_Fatal("Cannot allocate native polygon views");
		node->polygons = polygons;
		node->polygonCapacity = count;
	}
	return node->polygons;
}

SlipDraw3DStateRecord *SlipResourceStorage_DrawStateRecords(SlipResourceBlock *block, uint32_t count) {
	SlipResourceStorageNode *const node = (SlipResourceStorageNode *)block;
	if (count > node->drawStateRecordCapacity) {
		SlipDraw3DStateRecord *const states = realloc(node->drawStateRecords, (size_t)count * sizeof(*states));
		if (states == NULL)
			SlipRuntime_Fatal("Cannot allocate native offset draw-state views");
		/* Only newly created host pointer/callback identities need initialization. */
		for (size_t index = node->drawStateRecordCapacity; index < count; ++index)
			states[index] = (SlipDraw3DStateRecord){0};
		node->drawStateRecords = states;
		node->drawStateRecordCapacity = count;
	}
	return node->drawStateRecords;
}

SlipRendererDrawState *SlipResourceStorage_DrawStates(SlipResourceBlock *block, uint32_t count) {
	SlipResourceStorageNode *const node = (SlipResourceStorageNode *)block;
	if (count > node->drawStateCapacity) {
		SlipRendererDrawState *const states = realloc(node->drawStates, (size_t)count * sizeof(*states));
		if (states == NULL)
			SlipRuntime_Fatal("Cannot allocate native draw-state views");

		for (size_t i = node->drawStateCapacity; i < count; ++i)
			states[i] = (SlipRendererDrawState){0};
		node->drawStates = states;
		node->drawStateCapacity = count;
	}
	return node->drawStates;
}

RasterTexturedPoint *SlipResourceStorage_Points(SlipResourceBlock *block, uint32_t count) {
	SlipResourceStorageNode *const node = (SlipResourceStorageNode *)block;
	if (count > node->pointCapacity) {
		RasterTexturedPoint *const points = realloc(node->points, (size_t)count * sizeof(*points));
		if (points == NULL)
			SlipRuntime_Fatal("Cannot allocate native raster point views");
		node->points = points;
		node->pointCapacity = count;
	}
	return node->points;
}

SlipResourceBlock *SlipResourceStorage_Front(void *context, SlipResourceBlock *block, uint32_t requestedBytes) {
	(void)context;
	SlipResourceStorageNode *const node = (SlipResourceStorageNode *)block;
	const uint32_t alignedBytes = (requestedBytes + SLIP_RESOURCE_ALIGNMENT_LOW_MASK) & SLIP_RESOURCE_ALIGNMENT_MASK;
	SlipResourceBlock *remainder = NULL;

	if (block->capacityBytes != alignedBytes && block->capacityBytes - alignedBytes >= SLIP_RESOURCE_MIN_SPLIT_BYTES)
		remainder = SlipResourceStorage_BlockAt(node->storage, node->blockOffset +
		                                                           SLIP_RESOURCE_DOS_BLOCK_HEADER_BYTES + alignedBytes);
	return SlipResource_AllocateFront(block, requestedBytes, remainder);
}

bool SlipResourceStorage_Back(void *context, SlipResourceBlock *block, uint32_t requestedBytes,
                              SlipResourceBlock **allocation) {
	(void)context;
	SlipResourceStorageNode *const node = (SlipResourceStorageNode *)block;
	const uint32_t alignedBytes = (requestedBytes + SLIP_RESOURCE_ALIGNMENT_LOW_MASK) & SLIP_RESOURCE_ALIGNMENT_MASK;
	SlipResourceBlock *split = NULL;

	if (block->capacityBytes != alignedBytes && block->capacityBytes - alignedBytes >= SLIP_RESOURCE_MIN_SPLIT_BYTES)
		split = SlipResourceStorage_BlockAt(node->storage, node->blockOffset + block->capacityBytes - alignedBytes);
	const uint32_t freeBytes = SlipResource_freeBytes;
	*allocation = SlipResource_AllocateBack(block, requestedBytes, split);

	return freeBytes >= (*allocation)->capacityBytes + SLIP_RESOURCE_DOS_BLOCK_HEADER_BYTES;
}

const SlipResourceAllocationCalls SlipResourceStorage_allocationCalls = {
    .allocateFront = SlipResourceStorage_Front,
    .allocateBack = SlipResourceStorage_Back,
    .reclaimBlock = SlipResource_ReclaimBlock,
    .compact = SlipResource_Compact,
    .reclaimUnlocked = SlipResource_ReclaimUnlocked,
};

bool SlipResourceStorage_HandleTable(void *context, uint32_t bytes, SlipResourceHandleAllocation *allocation) {
	(void)context;
	if (!SlipResource_Allocate(bytes, &allocation->block, &SlipResourceStorage_allocationCalls))
		return false;
	SlipResourceStorageNode *const node = (SlipResourceStorageNode *)allocation->block;

	SlipResourceHandle *const records =
	    realloc(node->handles, (bytes / SLIP_RESOURCE_DOS_HANDLE_BYTES) * sizeof(*records));
	if (records == NULL)
		SlipRuntime_Fatal("Cannot allocate native resource handle views");
	node->handles = records;
	allocation->records = records;
	return true;
}

bool SlipResourceStorage_NameTable(void *context, uint32_t bytes, SlipResourceNameAllocation *allocation) {
	(void)context;
	if (!SlipResource_Allocate(bytes, &allocation->block, &SlipResourceStorage_allocationCalls))
		return false;
	SlipResourceStorageNode *const node = (SlipResourceStorageNode *)allocation->block;

	SlipResourceNameEntry *const entries =
	    realloc(node->names, (bytes / SLIP_RESOURCE_NAME_ENTRY_MINIMUM_BYTES) * sizeof(*entries));
	if (entries == NULL)
		SlipRuntime_Fatal("Cannot allocate native resource name views");
	node->names = entries;
	allocation->entries = entries;
	return true;
}
