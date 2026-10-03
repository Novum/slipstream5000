#include "actor_object_host.h"
#include "track_world.h"

SlipView3DMatrix SlipObject_matrixCopy;

void SlipActorObject_Attach(void *context, uint16_t object, SlipActorRecord *actor) {
	(void)context;
	SlipObject_table[object / SLIP_OBJECT_DOS_STRIDE].actorData = actor;
}

SlipActorRecord *SlipActorObject_Get(void *context, uint16_t object) {
	(void)context;
	return SlipObject_table[object / SLIP_OBJECT_DOS_STRIDE].actorData;
}

SlipView3DVec32 SlipActorObject_Position(void *context, uint16_t object) {
	(void)context;
	return SlipObject_table[object / SLIP_OBJECT_DOS_STRIDE].position;
}

SlipView3DVec32 SlipActorObject_ViewPosition(void *context, uint16_t object) {
	(void)context;
	SlipObject *const entry = &SlipObject_table[object / SLIP_OBJECT_DOS_STRIDE];
	if ((entry->flags & SLIP_OBJECT_VIEW_POSITION_VALID) == 0) {
		SlipObject *const camera = SlipObject_table;
		SlipView3DVec32 relative = {(int32_t)((uint32_t)entry->position.x - (uint32_t)camera->position.x),
		                            (int32_t)((uint32_t)entry->position.y - (uint32_t)camera->position.y),
		                            (int32_t)((uint32_t)entry->position.z - (uint32_t)camera->position.z)};
		entry->viewPosition = SlipView3D_TransformPositionByRows(&camera->matrix, relative);
		entry->flags |= SLIP_OBJECT_VIEW_POSITION_VALID;
	}
	return entry->viewPosition;
}

const SlipView3DMatrix *SlipActorObject_Matrix(void *context, uint16_t object) {
	(void)context;
	SlipObject_matrixCopy = SlipObject_table[object / SLIP_OBJECT_DOS_STRIDE].matrix;
	return &SlipObject_matrixCopy;
}

const SlipActorAccessCalls SlipActorObject_access = {.actor = SlipActorObject_Get};
const SlipActorTransformCalls SlipActorObject_transform = {.access = {.actor = SlipActorObject_Get},
                                                           .getObjectPosition = SlipActorObject_Position,
                                                           .getObjectMatrix = SlipActorObject_Matrix};
