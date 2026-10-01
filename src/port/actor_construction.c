#include "actor_construction.h"
#include "byte_order.h"
#include "runtime.h"
#include <stddef.h>

static SlipView3DVec32 SlipActor_ArtPosition(const uint8_t *bytes) {
	return (SlipView3DVec32){(int32_t)SlipBytes_ReadLE32(bytes), (int32_t)SlipBytes_ReadLE32(bytes + 4),
	                         (int32_t)SlipBytes_ReadLE32(bytes + 8)};
}

void SlipActor_InitializeParts(SlipActorPool *pool, SlipActorConstruction *state, SlipActorRecord *actor,
                               const uint8_t *body, SlipActorPartRecord *parent,
                               const SlipActorConstructionCalls *calls) {
	state->partOwnerActor = actor;
	const uint8_t *const firstBody = body;
	do {
		state->currentBodyRecord = body;
		SlipActorPartRecord *const part = SlipActorPool_AllocatePart(pool, parent);
		part->angle = 0;
		part->firstChild = NULL;
		part->worldPosition = (SlipView3DVec32){0, 0, 0};
		const uint32_t childOffset = SlipBytes_ReadLE32(body + 4);
		for (unsigned i = 0; i < 8; ++i) {
			const char *const name = (const char *)(state->currentBodyRecord + 0x1c + i * 14);
			uint16_t handle = 0;
			if (*name != 0 && !calls->findResourceByName(calls->context, name, &handle)) {
				pool->initialized = 0;
				SlipRuntime_Fatal("ArticSlotInit - one of the body shapes is missing");
			}
			part->shapes[i] = handle;
		}
		for (unsigned i = 0; i < 8; ++i) {
			const char *const name = (const char *)(state->currentBodyRecord + 0x8c + i * 14);
			uint16_t handle = 0;
			if (*name != 0 && !calls->findResourceByName(calls->context, name, &handle)) {
				pool->initialized = 0;
				SlipRuntime_Fatal("ArticSlotInit - one of the body shapes is missing");
			}
			part->replayShapes[i] = handle;
		}
		state->namedShapeCount = 0;
		for (unsigned i = 0; i < 4; ++i) {
			const uint8_t *const entry = state->currentBodyRecord + 0xfc + i * 26;
			uint16_t handle = 0;
			if (*entry != 0) {
				if (!calls->findResourceByName(calls->context, (const char *)entry, &handle)) {
					pool->initialized = 0;
					SlipRuntime_Fatal("ArticSlotInit - one of the body shapes is missing");
				}
				++state->namedShapeCount;
			}
			part->destruction[i].shape = handle;
			part->destruction[i].position = SlipActor_ArtPosition(entry + 14);
		}
		part->destructionCount = (uint16_t)state->namedShapeCount;
		state->namedShapeCount = 0;
		for (unsigned i = 0; i < 4; ++i) {
			const uint8_t *const entry = state->currentBodyRecord + 0x164 + i * 26;
			uint16_t handle = 0;
			if (*entry != 0) {
				if (!calls->findResourceByName(calls->context, (const char *)entry, &handle)) {
					pool->initialized = 0;
					SlipRuntime_Fatal("ArticSlotInit - one of the body shapes is missing");
				}
				++state->namedShapeCount;
			}
			part->debris[i].shape = handle;
			part->debris[i].position = SlipActor_ArtPosition(entry + 14);
		}
		part->debrisCount = (uint16_t)state->namedShapeCount;
		part->rotationCallbackOffset = SlipBytes_ReadLE32(state->currentBodyRecord + 0x0c);
		part->localPosition = SlipActor_ArtPosition(state->currentBodyRecord + 0x10);
		uint32_t count = SlipBytes_ReadLE32(state->currentBodyRecord + 0x1cc);
		if (count > 5)
			count = 5;
		part->namedPointCount = count;
		for (uint32_t i = 0; i < count; ++i) {
			const uint8_t *const point = state->currentBodyRecord + 0x1d0 + i * 16;
			part->namedPoints[i].tag = SlipBytes_ReadLE32(point);
			part->namedPoints[i].position = SlipActor_ArtPosition(point + 4);
		}
		part->tag = SlipBytes_ReadLE32(state->currentBodyRecord);
		state->partOwnerActor->parts[state->partOwnerActor->partCount] = part;
		++state->partOwnerActor->partCount;
		if (childOffset != 0)
			SlipActor_InitializeParts(pool, state, state->partOwnerActor, state->artPayload + childOffset, part, calls);
		const uint32_t siblingOffset = SlipBytes_ReadLE32(body + 8);
		if (siblingOffset == 0)
			break;
		body = state->artPayload + siblingOffset;
	} while (body != firstBody);
}

bool SlipActor_Create(SlipActorPool *pool, SlipActorConstruction *state, uint16_t object, uint16_t resource,
                      const SlipActorConstructionCalls *calls) {
	state->objectOffset = object;
	state->artResourceHandle = resource;
	state->artPayload = calls->lockResource(calls->context, resource);
	SlipActorRecord *actor;
	if (!SlipActorPool_Allocate(pool, &actor)) {
		SlipRuntime_error = 7;
		calls->unlockResource(calls->context, state->artResourceHandle);
		return false;
	}
	actor->ownerObject = state->objectOffset;
	actor->resourceHandle = state->artResourceHandle;
	actor->partCount = 0;
	actor->cachedPartTag = 0;
	state->createdActor = actor;
	calls->attachActor(calls->context, state->objectOffset, actor);
	SlipView3DVec32 position = calls->objectPosition(calls->context, state->objectOffset);
	position.x = (int32_t)((uint32_t)position.x - 1u);
	actor->cachedPosition = position;
	actor->childrenInSortTree = SlipBytes_ReadLE32(state->artPayload + 0x24);
	actor->minimum = SlipActor_ArtPosition(state->artPayload + 4);
	actor->maximum = SlipActor_ArtPosition(state->artPayload + 0x10);
	SlipView3DVec32 extent = {(int32_t)(0u - (uint32_t)actor->minimum.x), (int32_t)(0u - (uint32_t)actor->minimum.y),
	                          (int32_t)(0u - (uint32_t)actor->minimum.z)};
	if (extent.x < actor->maximum.x)
		extent.x = actor->maximum.x;
	if (extent.y < actor->maximum.y)
		extent.y = actor->maximum.y;
	if (extent.z < actor->maximum.z)
		extent.z = actor->maximum.z;
	actor->radius = calls->vectorLength(calls->context, extent);
	for (unsigned i = 0; i < 8; ++i)
		actor->lodDistances[i] = SlipBytes_ReadLE32(state->artPayload + 0x28 + i * 4);
	for (unsigned i = 0; i < 8; ++i)
		actor->replayLodDistances[i] = SlipBytes_ReadLE32(state->artPayload + 0x48 + i * 4);
	const uint8_t *const body = state->artPayload + SlipBytes_ReadLE32(state->artPayload + 0x20);
	SlipActor_InitializeParts(pool, state, state->createdActor, body, NULL, calls);
	calls->unlockResource(calls->context, state->artResourceHandle);
	return true;
}
