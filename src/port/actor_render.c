#include "actor_render.h"
#include <stddef.h>

bool SlipActor_SelectShape(const SlipActorRenderState *state, const SlipActorPartRecord *part, uint16_t *shape) {
	*shape = part->shapes[state->shapeLodIndex];
	return *shape != 0;
}

bool SlipActor_SelectReplayShape(const SlipActorRenderState *state, const SlipActorPartRecord *part, uint16_t *shape) {
	*shape = part->replayShapes[state->alternateShapeLodIndex];
	return *shape != 0;
}

void SlipActor_PrepareDraw(SlipActorRenderState *state, SlipActorPartRecord *part, const SlipActorRenderCalls *calls) {
	part->hasDrawShape = false;
	part->drawShape = UINT16_MAX;
	uint16_t shape;
	if (SlipActor_SelectShape(state, part, &shape)) {
		part->drawShape = shape;
		part->hasDrawShape = true;
	}
	void *const context = calls->transform.access.context;
	const SlipView3DMatrix *camera = calls->transform.getObjectMatrix(context, 0);
	SlipView3D_ComposeMatrix(&part->worldMatrix, camera, &part->drawMatrix);
	SlipView3DVec32 cameraPosition = calls->transform.getObjectPosition(context, 0);
	SlipView3DVec32 delta = {(int32_t)(0u - (uint32_t)cameraPosition.x + (uint32_t)part->worldPosition.x +
	                                   (uint32_t)state->objectWorldPosition.x),
	                         (int32_t)(0u - (uint32_t)cameraPosition.y + (uint32_t)part->worldPosition.y +
	                                   (uint32_t)state->objectWorldPosition.y),
	                         (int32_t)(0u - (uint32_t)cameraPosition.z + (uint32_t)part->worldPosition.z +
	                                   (uint32_t)state->objectWorldPosition.z)};
	camera = calls->transform.getObjectMatrix(context, 0);
	part->drawPosition = SlipView3D_TransformPositionByRows(camera, delta);
	SlipActorPartRecord *child = part->firstChild;
	if (child != NULL) {
		SlipActorPartRecord *const firstChild = child;
		do {
			SlipActor_PrepareDraw(state, child, calls);
			child = child->nextSibling;
		} while (child != firstChild);
	}
}

void SlipActor_DrawParts(SlipActorRenderState *state, SlipActorPartRecord *part, const SlipActorRenderCalls *calls) {
	calls->drawPart(calls->transform.access.context, part);
	if (state->childrenInSortTree != 0)
		return;
	SlipActorPartRecord *child = part->firstChild;
	if (child != NULL) {
		SlipActorPartRecord *const firstChild = child;
		do {
			SlipActor_DrawParts(state, child, calls);
			child = child->nextSibling;
		} while (child != firstChild);
	}
}

void SlipActor_Draw(SlipActorRenderState *state, const SlipActorPool *pool, uint16_t object,
                    const SlipView3DMaths *maths, const SlipActorRenderCalls *calls) {
	SlipActor_Rebuild(&state->transform, object, maths, &calls->transform);
	void *const context = calls->transform.access.context;
	state->viewDepth = calls->viewPosition(context, object).z;
	state->objectWorldPosition = calls->transform.getObjectPosition(context, object);
	(void)SlipActor_TestOwner(object, &state->actor, &calls->transform.access);
	SlipActorRecord *const actor = state->actor;
	state->childrenInSortTree = actor->childrenInSortTree;
	const uint32_t reciprocal = calls->projectionReciprocal(context);
	const uint64_t product = (uint64_t)((int64_t)(int32_t)reciprocal * state->viewDepth);
	const int32_t distance = (int32_t)(uint32_t)(product >> 16);
	uint32_t lod = 0;
	while (distance >= (int32_t)actor->lodDistances[lod]) {
		if (++lod == 8)
			return;
	}
	if (lod < pool->mode)
		lod = pool->mode;
	state->shapeLodIndex = lod;
	SlipActorPartRecord *const root = actor->parts[0];
	SlipActor_PrepareDraw(state, root, calls);
	SlipActor_DrawParts(state, root, calls);
}
