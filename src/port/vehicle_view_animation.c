#include "vehicle_view_animation.h"
#include "actor_tags.h"
#include "fixed_point.h"
#include "frame_timer.h"

enum { SLIP_VEHICLE_VIEW_FAN_ROTATION_RATE = INT16_MAX, SLIP_VEHICLE_VIEW_JET_RATE_SHIFT = 1 };

void SlipVehicleView_Animate(SlipVehicleViewAnimation *state, uint16_t object,
                             const SlipVehicleViewAnimationCalls *calls) {
	const int16_t frameStep = (int16_t)SlipFrameTimer_Step();
	state->fanStep = (int16_t)(((int32_t)SLIP_VEHICLE_VIEW_FAN_ROTATION_RATE * frameStep) >> SLIP_Q14_FRACTION_BITS);
	state->jetStep = (int16_t)(state->fanStep >> SLIP_VEHICLE_VIEW_JET_RATE_SHIFT);
	int16_t angle = state->jetAngle;
	if (state->jetDirection == 0) {
		angle = (int16_t)(angle + state->jetStep);
		if (angle >= SLIP_ANGLE_QUARTER_TURN) {
			angle = SLIP_ANGLE_QUARTER_TURN;
			state->jetDirection = 1;
		}
	} else {
		angle = (int16_t)(angle - state->jetStep);
		if (angle <= 0) {
			angle = 0;
			state->jetDirection = 0;
		}
	}
	state->jetAngle = angle;
	for (uint32_t partTag = SLIP_ACTOR_FIRST_FAN; partTag != (SLIP_ACTOR_FIRST_FAN + SLIP_ACTOR_FAN_COUNT); ++partTag) {
		uint16_t fanAngle;
		if (calls->getAngle(calls->context, object, partTag, &fanAngle))
			calls->setAngle(calls->context, object, partTag, (uint16_t)(fanAngle + state->fanStep));
	}
	for (uint32_t partTag = SLIP_ACTOR_FIRST_JET; partTag != (SLIP_ACTOR_FIRST_JET + SLIP_ACTOR_JET_COUNT); ++partTag)
		calls->setAngle(calls->context, object, partTag, (uint16_t)state->jetAngle);
}
