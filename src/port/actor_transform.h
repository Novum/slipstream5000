#ifndef SLIPSTREAM5000_ACTOR_TRANSFORM_H
#define SLIPSTREAM5000_ACTOR_TRANSFORM_H
#include "actor_access.h"

typedef struct SlipActorTransformState {
	uint32_t dirty;
} SlipActorTransformState;

typedef struct SlipActorTransformCalls {
	SlipActorAccessCalls access;
	SlipView3DVec32 (*getObjectPosition)(void *, uint16_t object);
	const SlipView3DMatrix *(*getObjectMatrix)(void *, uint16_t object);
} SlipActorTransformCalls;

void SlipActor_Rebuild(SlipActorTransformState *, uint16_t object, const SlipView3DMaths *,
                       const SlipActorTransformCalls *);
void SlipActor_RebuildChildren(SlipActorTransformState *, SlipActorPartRecord *, const SlipView3DMaths *);
#endif
