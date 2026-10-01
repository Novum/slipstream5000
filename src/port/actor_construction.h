#ifndef SLIPSTREAM5000_ACTOR_CONSTRUCTION_H
#define SLIPSTREAM5000_ACTOR_CONSTRUCTION_H
#include "actor_pool.h"

typedef struct SlipActorConstruction {
	const uint8_t *artPayload;
	SlipActorRecord *createdActor;
	uint16_t objectOffset, artResourceHandle;
	SlipActorRecord *partOwnerActor;
	const uint8_t *currentBodyRecord;
	uint32_t namedShapeCount;
} SlipActorConstruction;

typedef struct SlipActorConstructionCalls {
	void *context;
	const uint8_t *(*lockResource)(void *, uint16_t);
	void (*attachActor)(void *, uint16_t objectOffset, SlipActorRecord *);
	SlipView3DVec32 (*objectPosition)(void *, uint16_t objectOffset);
	uint32_t (*vectorLength)(void *, SlipView3DVec32);
	bool (*findResourceByName)(void *, const char *name, uint16_t *handle);
	void (*unlockResource)(void *, uint16_t);
} SlipActorConstructionCalls;

bool SlipActor_Create(SlipActorPool *, SlipActorConstruction *, uint16_t objectOffset, uint16_t artResourceHandle,
                      const SlipActorConstructionCalls *);
void SlipActor_InitializeParts(SlipActorPool *, SlipActorConstruction *, SlipActorRecord *,
                               const uint8_t *currentBodyRecord, SlipActorPartRecord *parent,
                               const SlipActorConstructionCalls *);
#endif
