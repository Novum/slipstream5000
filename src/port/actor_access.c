#include "actor_access.h"
#include "actor_tags.h"

bool SlipActor_TestOwner(uint16_t object, SlipActorRecord **actor, const SlipActorAccessCalls *calls) {
	*actor = calls->actor(calls->context, object);
	return (*actor)->ownerObject == object;
}

bool SlipActor_SelectPart(uint16_t object, uint32_t tag, SlipActorPartRecord **part,
                          const SlipActorAccessCalls *calls) {
	SlipActorRecord *actor;
	if (!SlipActor_TestOwner(object, &actor, calls))
		return false;
	if (tag == SLIP_ACTOR_PART_MAIN) {
		*part = actor->parts[0];
		return true;
	}
	if (tag == actor->cachedPartTag) {
		*part = actor->parts[SLIP_ACTOR_CACHED_PART_INDEX];
		return true;
	}
	for (uint32_t i = 0; i < actor->partCount; ++i) {
		SlipActorPartRecord *const candidate = actor->parts[i];
		if (tag == candidate->tag) {
			actor->parts[SLIP_ACTOR_CACHED_PART_INDEX] = candidate;
			actor->cachedPartTag = tag;
			*part = candidate;
			return true;
		}
	}
	return false;
}

bool SlipActor_GetAngle(uint16_t object, uint32_t tag, uint16_t *angle, const SlipActorAccessCalls *calls) {
	SlipActorPartRecord *part;
	if (!SlipActor_SelectPart(object, tag, &part, calls))
		return false;
	*angle = part->angle;
	return true;
}

bool SlipActor_SetAngle(uint16_t object, uint32_t tag, uint16_t angle, const SlipActorAccessCalls *calls) {
	SlipActorPartRecord *part;
	if (!SlipActor_SelectPart(object, tag, &part, calls))
		return false;
	const uint16_t previous = part->angle;
	part->angle = angle;
	if (previous != angle)
		part->matrixValid = 0;
	return true;
}

void SlipActor_Destroy(SlipActorPool *pool, uint16_t object, const SlipActorAccessCalls *calls) {
	SlipActorRecord *actor;
	if (SlipActor_TestOwner(object, &actor, calls))
		SlipActorPool_FreeActor(pool, actor);
}
