#include "renderer_lifecycle.h"
#include "runtime.h"

void SlipRenderer_Initialize(SlipRendererState *state, uint16_t vertices, const SlipRendererLifecycleCalls *calls) {
	if (state->initialized == 0) {
		void *const context = calls->context;
		state->initialized = UINT32_MAX;
		calls->initializeVertices(context, vertices);
		if (!calls->initializePolygons(context))
			SlipRuntime_Fatal("ResAlloc failed during Draw3DInstall");
		calls->initializeSpecular(context);
		state->stateCount = 32;
		uint16_t resource;
		if (!calls->allocate(context, (uint32_t)state->stateCount * 56u, 0, &resource))
			SlipRuntime_Fatal("ResAlloc failed during Draw3DInstall");
		state->stateResource = resource;
		state->states = calls->lockStates(context, resource);
		if (!calls->allocate(context, 0x380, 0, &resource))
			SlipRuntime_Fatal("ResAlloc failed during Draw3DInstall");
		state->reservedResource = resource;

		if (!calls->allocate(context, 0x402, 0, &resource))
			SlipRuntime_Fatal("ResAlloc failed during Draw3DInstall");
		state->pointResource = resource;
		state->reservedWorkspace = calls->lockReserved(context, state->reservedResource);
		state->points = calls->lockPoints(context, state->pointResource);
		SlipDraw3D_materialCallbackCount = 0;
		calls->initializeProjection(context);
		calls->advanceBackground(context);
		SlipRenderer_Begin(state, calls);
		calls->resetLighting(context);
		calls->registerExit(context, SlipRenderer_Shutdown);
		state->materials->memoryBudget = UINT32_MAX;
		state->materials->maximumTextureSize = UINT32_MAX;
		state->materials->maximumTextureFrame = UINT32_MAX;
	}
}

void SlipRenderer_Shutdown(SlipRendererState *state, const SlipRendererLifecycleCalls *calls) {
	if (state->initialized != 0) {
		state->initialized = 0;
		void *const context = calls->context;
		calls->freeSpecular(context);
		calls->unlock(context, state->pointResource);
		calls->release(context, state->pointResource);
		calls->unlock(context, state->vertexResource);
		calls->release(context, state->vertexResource);
		calls->unlock(context, state->polygonResource);
		calls->release(context, state->polygonResource);
		calls->unlock(context, state->stateResource);
		calls->release(context, state->stateResource);
		calls->unlock(context, state->reservedResource);
		calls->release(context, state->reservedResource);
		calls->freeMaterials(context);
	}
}

void SlipRenderer_Begin(SlipRendererState *state, const SlipRendererLifecycleCalls *calls) {
	state->projection.auxiliaryClipPlaneEnabled = 0;
	state->vertexCursor = state->vertexBase;
	calls->resetDrawState(calls->context);
}
