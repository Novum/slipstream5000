#include "renderer_allocation.h"
#include "runtime.h"

void SlipRenderer_GrowVertices(SlipRendererState *state, uint32_t capacity, const SlipRendererAllocationCalls *calls) {
	const uint16_t oldResource = state->vertexResource;
	SlipDraw3DVertexRecord *const oldLimit = state->vertexLimit;
	SlipDraw3DVertexRecord *const oldBase = state->vertexBase;
	state->vertexCapacity = capacity;
	const uint32_t bytes = capacity * 64u;
	uint16_t resource;
	if (!calls->allocate(calls->context, bytes, 0, &resource))
		SlipRuntime_Fatal("PointsReInstall - Out of point space");
	state->vertexResource = resource;
	state->vertexBase = calls->lockVertices(calls->context, resource);
	state->vertexLimit = state->vertexBase + bytes / 64u;
	uint32_t remaining = state->stateCount;
	SlipRendererDrawState *record = state->states;
	do {

		record->vertices = (SlipDraw3DVertexRecord *)((uintptr_t)record->vertices +
		                                              ((uintptr_t)state->vertexBase - (uintptr_t)oldBase));
		++record;
	} while (--remaining != 0);
	state->vertexCursor = state->vertexBase + (state->vertexCursor - oldBase);
	SlipDraw3DVertexRecord *destination = state->vertexBase;
	for (SlipDraw3DVertexRecord *source = oldBase; source != oldLimit; ++source)
		*destination++ = *source;
	calls->unlock(calls->context, oldResource);
	calls->release(calls->context, oldResource);
}

bool SlipRenderer_AllocatePolygons(SlipRendererState *state, const SlipRendererAllocationCalls *calls) {
	state->polygonCount = 64;
	const uint32_t bytes = (uint32_t)(uint16_t)(state->polygonCount + 1u) * 60u;
	uint16_t resource;
	if (!calls->allocate(calls->context, bytes, 0, &resource))
		return false;
	state->polygonResource = resource;
	state->freePolygons = calls->lockPolygons(calls->context, resource);
	uint32_t remaining = state->polygonCount;
	SlipRendererPolygon *current = state->freePolygons;
	do {
		SlipRendererPolygon *const next = current + 1;
		current->next = next;
		next->previous = current;
		current = next;
	} while (--remaining != 0);
	SlipRendererPolygon *const first = state->freePolygons;
	current->next = first;
	first->previous = current;
	state->activePolygons = NULL;
	return true;
}

void SlipRenderer_AllocateVertices(SlipRendererState *state, uint32_t capacity,
                                   const SlipRendererAllocationCalls *calls) {
	state->vertexCapacity = capacity;
	const uint32_t bytes = capacity * 64u;
	uint16_t resource;
	if (!calls->allocate(calls->context, bytes, 0, &resource))
		SlipRuntime_Fatal("ResAlloc failed during Draw3DInstall");
	state->vertexResource = resource;
	SlipDraw3DVertexRecord *const vertices = calls->lockVertices(calls->context, resource);
	state->vertexCursor = vertices;
	state->vertexBase = vertices;
	state->vertexLimit = vertices + bytes / 64u;
}

void SlipRenderer_AllocateSpecular(SlipRendererState *state, const SlipRendererAllocationCalls *calls) {
	uint32_t value = 0;
	for (;;) {
		uint32_t power = value;
		uint32_t remaining = 5;
		do {
			const uint16_t doubled = (uint16_t)(power << 1);
			power = ((uint32_t)doubled * doubled) >> 16;
		} while (--remaining != 0);
		if ((uint16_t)power != 0)
			break;
		++value;
	}
	state->specularThreshold = value;
	uint32_t count = 0x4000u - value + 1u;
	uint16_t resource;
	if (!calls->allocate(calls->context, count * 2u, 0, &resource))
		SlipRuntime_Fatal("InstallSpecularTables - out of memory");
	state->specularResource = resource;
	state->specularTable = calls->lockSpecular(calls->context, resource);
	uint16_t *destination = state->specularTable;
	do {
		uint32_t power = value;
		uint32_t remaining = 5;
		do {
			const uint16_t doubled = (uint16_t)(power << 1);
			power = ((uint32_t)doubled * doubled) >> 16;
		} while (--remaining != 0);
		*destination++ = (uint16_t)power;
		++value;
	} while (--count != 0);
}

void SlipRenderer_FreeSpecular(SlipRendererState *state, const SlipRendererAllocationCalls *calls) {
	calls->unlock(calls->context, state->specularResource);
	calls->release(calls->context, state->specularResource);
}
