#ifndef SLIPSTREAM5000_ACTOR_RENDER_H
#define SLIPSTREAM5000_ACTOR_RENDER_H
#include "actor_transform.h"

typedef struct SlipActorRenderState {
	SlipActorTransformState transform;
	uint32_t childrenInSortTree;
	int32_t viewDepth;
	SlipView3DVec32 objectWorldPosition;
	SlipActorRecord *actor;
	uint32_t shapeLodIndex, alternateShapeLodIndex;
} SlipActorRenderState;

typedef struct SlipActorRenderCalls {
	SlipActorTransformCalls transform;
	SlipView3DVec32 (*viewPosition)(void *, uint16_t object);
	uint32_t (*projectionReciprocal)(void *);
	void (*drawPart)(void *, SlipActorPartRecord *);
} SlipActorRenderCalls;

bool SlipActor_SelectShape(const SlipActorRenderState *, const SlipActorPartRecord *, uint16_t *);
bool SlipActor_SelectReplayShape(const SlipActorRenderState *, const SlipActorPartRecord *, uint16_t *);
void SlipActor_PrepareDraw(SlipActorRenderState *, SlipActorPartRecord *, const SlipActorRenderCalls *);
void SlipActor_DrawParts(SlipActorRenderState *, SlipActorPartRecord *, const SlipActorRenderCalls *);
void SlipActor_Draw(SlipActorRenderState *, const SlipActorPool *, uint16_t object, const SlipView3DMaths *,
                    const SlipActorRenderCalls *);
#endif
