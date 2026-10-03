#include "renderer_allocation.h"
#include "renderer_flags.h"
#include "runtime.h"

/* Squaring a doubled Q14 value produces Q30; shift back to Q14. */
enum { SLIP_SPECULAR_SQUARED_FRACTION_SHIFT = SLIP_NORMAL_FRACTION_BITS + 2 };

void SlipRenderer_GrowVertices(SlipRendererState *state, uint32_t capacity, const SlipRendererAllocationCalls *calls) {
	const uint16_t oldResource = state->vertexResource;
	SlipDraw3DVertexRecord *const oldLimit = state->vertexLimit;
	SlipDraw3DVertexRecord *const oldBase = state->vertexBase;
	state->vertexCapacity = capacity;
	const uint32_t bytes = capacity * SLIP_DRAW3D_VERTEX_RECORD_SIZE;
	uint16_t resource;
	if (!calls->allocate(calls->context, bytes, 0, &resource))
		SlipRuntime_Fatal("PointsReInstall - Out of point space");
	state->vertexResource = resource;
	state->vertexBase = calls->lockVertices(calls->context, resource);
	state->vertexLimit = state->vertexBase + bytes / SLIP_DRAW3D_VERTEX_RECORD_SIZE;
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
	state->polygonCount = SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT;
	const uint32_t bytes = (uint32_t)(uint16_t)(state->polygonCount + 1u) * SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE;
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
	const uint32_t bytes = capacity * SLIP_DRAW3D_VERTEX_RECORD_SIZE;
	uint16_t resource;
	if (!calls->allocate(calls->context, bytes, 0, &resource))
		SlipRuntime_Fatal("ResAlloc failed during Draw3DInstall");
	state->vertexResource = resource;
	SlipDraw3DVertexRecord *const vertices = calls->lockVertices(calls->context, resource);
	state->vertexCursor = vertices;
	state->vertexBase = vertices;
	state->vertexLimit = vertices + bytes / SLIP_DRAW3D_VERTEX_RECORD_SIZE;
}

void SlipRenderer_AllocateSpecular(SlipRendererState *state, const SlipRendererAllocationCalls *calls) {
	uint32_t value = 0;
	for (;;) {
		uint32_t power = value;
		for (unsigned squaring = 0; squaring < SLIP_DRAW3D_SPECULAR_SQUARING_STEPS; ++squaring) {
			const uint16_t doubled = (uint16_t)(power << 1);
			power = ((uint32_t)doubled * doubled) >> SLIP_SPECULAR_SQUARED_FRACTION_SHIFT;
		}
		if ((uint16_t)power != 0)
			break;
		++value;
	}
	state->specularThreshold = value;
	const uint32_t count = SLIP_LIGHT_UNIT - value + 1u;
	uint16_t resource;
	if (!calls->allocate(calls->context, count * sizeof(*state->specularTable), 0, &resource))
		SlipRuntime_Fatal("InstallSpecularTables - out of memory");
	state->specularResource = resource;
	state->specularTable = calls->lockSpecular(calls->context, resource);
	uint16_t *destination = state->specularTable;
	for (uint32_t entryIndex = 0; entryIndex < count; ++entryIndex) {
		uint32_t power = value;
		for (unsigned squaring = 0; squaring < SLIP_DRAW3D_SPECULAR_SQUARING_STEPS; ++squaring) {
			const uint16_t doubled = (uint16_t)(power << 1);
			power = ((uint32_t)doubled * doubled) >> SLIP_SPECULAR_SQUARED_FRACTION_SHIFT;
		}
		*destination++ = (uint16_t)power;
		++value;
	}
}

void SlipRenderer_FreeSpecular(SlipRendererState *state, const SlipRendererAllocationCalls *calls) {
	calls->unlock(calls->context, state->specularResource);
	calls->release(calls->context, state->specularResource);
}
