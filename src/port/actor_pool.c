#include "actor_pool.h"
#include "runtime.h"
#include <stddef.h>

void SlipActorPool_InitializeActors(SlipActorPool *pool) {
	SlipActorRecord *actor = pool->activeSentinel;
	actor->next = actor;
	actor->previous = actor;
	uint32_t remaining = pool->actorCount;
	actor = pool->actorFreeSentinel;
	do {
		SlipActorRecord *const next = actor + 1;
		actor->next = next;
		next->previous = actor;
		actor = next;
	} while (--remaining != 0);
	actor->next = pool->actorFreeSentinel;
	pool->actorFreeSentinel->previous = actor;
}

void SlipActorPool_InitializeParts(SlipActorPool *pool) {
	uint32_t remaining = pool->partCount;
	SlipActorPartRecord *part = pool->partFreeSentinel;
	do {
		SlipActorPartRecord *const next = part + 1;
		part->nextSibling = next;
		next->previousSibling = part;
		part = next;
	} while (--remaining != 0);
	part->nextSibling = pool->partFreeSentinel;
	pool->partFreeSentinel->previousSibling = part;
}

bool SlipActorPool_Allocate(SlipActorPool *pool, SlipActorRecord **result) {
	SlipActorRecord *const free = pool->actorFreeSentinel;
	SlipActorRecord *const actor = free->next;
	*result = actor;
	if (actor == free)
		return false;
	SlipActorRecord *const next = actor->next;
	free->next = next;
	next->previous = free;
	SlipActorRecord *const activeSentinel = pool->activeSentinel;
	SlipActorRecord *const first = activeSentinel->next;
	activeSentinel->next = actor;
	first->previous = actor;
	actor->next = first;
	actor->previous = activeSentinel;
	return true;
}

SlipActorPartRecord *SlipActorPool_AllocatePart(SlipActorPool *pool, SlipActorPartRecord *parent) {
	SlipActorPartRecord *const part = pool->partFreeSentinel->nextSibling;
	if (part == pool->partFreeSentinel)
		SlipRuntime_Fatal("ArtBodyAlloc - make it resize the slots buffer!");
	SlipActorPartRecord *const previous = part->previousSibling;
	SlipActorPartRecord *next = part->nextSibling;
	next->previousSibling = previous;
	previous->nextSibling = next;
	part->parent = parent;
	if (parent != NULL && parent->firstChild != NULL) {
		SlipActorPartRecord *const first = parent->firstChild;
		next = first->nextSibling;
		next->previousSibling = part;
		part->nextSibling = next;
		part->previousSibling = first;
		first->nextSibling = part;
	} else {
		if (parent != NULL)
			parent->firstChild = part;
		part->previousSibling = part;
		part->nextSibling = part;
	}
	return part;
}

void SlipActorPool_FreePart(SlipActorPool *pool, SlipActorPartRecord *part) {
	SlipActorPartRecord *const previous = part->previousSibling;
	SlipActorPartRecord *const next = part->nextSibling;
	next->previousSibling = previous;
	previous->nextSibling = next;
	SlipActorPartRecord *const free = pool->partFreeSentinel;
	SlipActorPartRecord *const first = free->nextSibling;
	first->previousSibling = part;
	part->nextSibling = first;
	part->previousSibling = free;
	free->nextSibling = part;
}

void SlipActorPool_FreeActor(SlipActorPool *pool, SlipActorRecord *actor) {
	uint32_t remaining = actor->partCount;
	SlipActorPartRecord **part = actor->parts;
	do {
		SlipActorPool_FreePart(pool, *part++);
	} while (--remaining != 0);
	SlipActorRecord *const next = actor->next;
	SlipActorRecord *const previous = actor->previous;
	previous->next = next;
	next->previous = previous;
	SlipActorRecord *const free = pool->actorFreeSentinel;
	SlipActorRecord *const first = free->next;
	free->next = actor;
	first->previous = actor;
	actor->next = first;
	actor->previous = free;
}

bool SlipActorPool_Initialize(SlipActorPool *pool, uint16_t actors, const SlipActorPoolCalls *calls) {
	pool->partCount = 80;
	if (pool->initialized != 0)
		return true; /* CMP initialized,0 leaves carry clear. */
	pool->initialized = UINT32_MAX;
	pool->actorCount = actors;
	const uint32_t actorBytes = (uint32_t)(uint16_t)(actors + 2u) * 216u;
	const uint32_t partBytes = (uint32_t)(uint16_t)(pool->partCount + 1u) * 352u;
	const uint32_t bytes = partBytes + actorBytes;
	pool->partRegionOffset = actorBytes;
	if (bytes < partBytes)
		return false;
	uint16_t resourceHandle;
	if (!calls->allocate(calls->context, bytes, 0, &resourceHandle))
		return false;
	pool->resourceHandle = resourceHandle;
	SlipActorPoolStorage storage = calls->lock(calls->context, resourceHandle);
	pool->partFreeSentinel = storage.parts;
	pool->activeSentinel = storage.actors;
	pool->actorFreeSentinel = storage.actors + 1;
	SlipActorPool_InitializeActors(pool);
	SlipActorPool_InitializeParts(pool);
	calls->registerExit(calls->context, SlipActorPool_Shutdown);
	calls->registerEvent(calls->context, 1, SlipActorPool_ObjectEvent);
	return true;
}

void SlipActorPool_ObjectEvent(SlipActorPool *pool, uint16_t object, uint16_t events, const SlipActorPoolCalls *calls) {
	if (object == 0 || (events & 1u) == 0 || pool->initialized == 0)
		return;
	SlipActorRecord *actor = pool->activeSentinel->next;
	for (;;) {
		SlipActorRecord *const next = actor->next;
		if (actor == pool->activeSentinel)
			return;
		if (actor->ownerObject == object)
			calls->destroyObjectActor(calls->context, object);
		actor = next;
	}
}

void SlipActorPool_Shutdown(SlipActorPool *pool, const SlipActorPoolCalls *calls) {
	if (pool->initialized != 0) {
		pool->initialized = 0;
		SlipActorRecord *actor = pool->activeSentinel->next;
		for (;;) {
			SlipActorRecord *const next = actor->next;
			if (actor == pool->activeSentinel)
				break;
			SlipActorPool_FreeActor(pool, actor);
			actor = next;
		}
		calls->unlock(calls->context, pool->resourceHandle);
		calls->release(calls->context, pool->resourceHandle);
	}
}

void SlipActorPool_SetMode(SlipActorPool *pool, uint32_t mode) { pool->mode = mode; }

uint32_t SlipActorPool_GetMode(const SlipActorPool *pool) { return pool->mode; }
