#include "actor_transform.h"
#include "actor_format.h"
#include <stddef.h>

static void SlipActor_NoRotation(const SlipView3DMaths *maths, int16_t angle, SlipView3DMatrix *matrix) {
	(void)maths;
	(void)angle;
	(void)matrix;
}

typedef void (*SlipActorRotation)(const SlipView3DMaths *, int16_t, SlipView3DMatrix *);
static const SlipActorRotation rotations[] = {SlipActor_NoRotation, SlipView3D_ApplyPitchMatrix,
                                              SlipView3D_ApplyRow0Row1Rotation, SlipView3D_ApplyRow0Row2Rotation};

void SlipActor_RebuildChildren(SlipActorTransformState *state, SlipActorPartRecord *part,
                               const SlipView3DMaths *maths) {
	const uint32_t savedDirty = state->dirty;
	SlipActorPartRecord *child = part->firstChild;
	if (child != NULL) {
		SlipActorPartRecord *const firstChild = child;
		do {
			if (state->dirty != 0 || child->matrixValid == 0) {
				child->matrixValid = UINT16_MAX;
				state->dirty = UINT32_MAX;
				SlipActorPartRecord *const parent = child->parent;
				child->worldMatrix = parent->worldMatrix;
				SlipView3DVec32 local = {(int16_t)child->localPosition.x, (int16_t)child->localPosition.y,
				                         (int16_t)child->localPosition.z};
				SlipView3DVec32 rotated = SlipView3D_TransformPosition16(&parent->worldMatrix, local);
				child->worldPosition.x =
				    (int32_t)((uint32_t)(int32_t)(int16_t)rotated.x + (uint32_t)parent->worldPosition.x);
				child->worldPosition.y =
				    (int32_t)((uint32_t)(int32_t)(int16_t)rotated.y + (uint32_t)parent->worldPosition.y);
				child->worldPosition.z =
				    (int32_t)((uint32_t)(int32_t)(int16_t)rotated.z + (uint32_t)parent->worldPosition.z);
				rotations[child->rotationCallbackOffset / SLIP_ART_ROTATION_CALLBACK_ENTRY_BYTES](
				    maths, (int16_t)child->angle, &child->worldMatrix);
			}
			SlipActor_RebuildChildren(state, child, maths);
			child = child->nextSibling;
		} while (child != firstChild);
	}
	state->dirty = savedDirty;
}

void SlipActor_Rebuild(SlipActorTransformState *state, uint16_t object, const SlipView3DMaths *maths,
                       const SlipActorTransformCalls *calls) {
	SlipActorRecord *actor;
	if (!SlipActor_TestOwner(object, &actor, &calls->access))
		return;
	state->dirty = 0;
	void *const context = calls->access.context;
	SlipView3DVec32 position = calls->getObjectPosition(context, object);
	bool changed = position.x != actor->cachedPosition.x || position.y != actor->cachedPosition.y ||
	               position.z != actor->cachedPosition.z;
	if (!changed) {
		const SlipView3DMatrix *const matrix = calls->getObjectMatrix(context, object);
		for (unsigned i = 0; i < 9; ++i) {
			if (matrix->m[i] != actor->cachedMatrix.m[i]) {
				changed = true;
				break;
			}
		}
	}
	if (changed) {
		state->dirty = UINT32_MAX;
		actor->cachedMatrix = *calls->getObjectMatrix(context, object);
		actor->cachedPosition = calls->getObjectPosition(context, object);
	}
	const SlipView3DMatrix *const matrix = calls->getObjectMatrix(context, object);
	SlipActorPartRecord *const root = actor->parts[0];
	root->worldMatrix = *matrix;
	SlipActor_RebuildChildren(state, root, maths);
}
