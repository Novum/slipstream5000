#include "race_physics.h"

#include "frame_timer.h"
#include "race_collision.h"

uint32_t SlipRacePhysics_completedSubsteps;

void SlipRacePhysics_RunFrame(void) {
	SlipFrameTimerValues timer = SlipFrameTimer_Values();
	uint32_t elapsedStepOrCount = timer.deltaMilliseconds;
	const uint32_t frameStep = timer.stepQ14;
	const uint32_t frameRate = timer.frameRateHz;
	const uint32_t initialEventValue = 0;
	const uint32_t initialContactType = 0;

	if (frameStep == 0)
		return;
	SlipRaceCollision_frameStep = frameStep;
	SlipRacePhysics_completedSubsteps = 0;
	SlipRaceCollision_AdvanceUncollidableObjects();
	SlipRaceCollision_ClearBodyContacts();
	for (;;) {
		SlipRaceCollision_PrepareBodies();
		SlipRaceCollision_firstTime = SLIP_COLLISION_NO_CONTACT_TIME;
		SlipRaceCollision_FindBodyCollisions();
		if (SlipRaceCollision_firstTime != 0) {
			if (SlipRaceCollision_preStep != 0) {
				elapsedStepOrCount = SlipRaceCollision_frameStep;
				SlipRaceCollision_preStep(elapsedStepOrCount);
			}
			SlipRaceCollision_IntegrateBodies();
			if (SlipRaceCollision_postStep != 0) {
				SlipRaceCollision_postStep();
			}
		}
		SlipRaceCollision_FinalizeBodyCollisions();
		SlipRaceCollision_DispatchBodyEvents(elapsedStepOrCount, frameRate, initialEventValue, initialContactType);
		++SlipRacePhysics_completedSubsteps;
		elapsedStepOrCount = SlipRacePhysics_completedSubsteps;
		if (elapsedStepOrCount == SLIP_COLLISION_SUBSTEP_LIMIT)
			return;
		elapsedStepOrCount = SlipRaceCollision_frameStep;
		if (elapsedStepOrCount == 0)
			return;
		SlipRaceCollision_PrepareRemainingStep(elapsedStepOrCount, frameStep, frameRate, initialEventValue,
		                                       initialContactType);
	}
}
