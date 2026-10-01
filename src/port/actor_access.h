#ifndef SLIPSTREAM5000_ACTOR_ACCESS_H
#define SLIPSTREAM5000_ACTOR_ACCESS_H
#include "actor_pool.h"

typedef struct SlipActorAccessCalls {
	void *context;
	SlipActorRecord *(*actor)(void *, uint16_t object);
} SlipActorAccessCalls;

bool SlipActor_TestOwner(uint16_t object, SlipActorRecord **actor, const SlipActorAccessCalls *);

bool SlipActor_SelectPart(uint16_t object, uint32_t tag, SlipActorPartRecord **part, const SlipActorAccessCalls *);
bool SlipActor_GetAngle(uint16_t object, uint32_t tag, uint16_t *angle, const SlipActorAccessCalls *);
bool SlipActor_SetAngle(uint16_t object, uint32_t tag, uint16_t angle, const SlipActorAccessCalls *);
void SlipActor_Destroy(SlipActorPool *, uint16_t object, const SlipActorAccessCalls *);
#endif
