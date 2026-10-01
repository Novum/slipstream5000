#include "vehicle_view_animation.h"
#include "frame_timer.h"

void SlipVehicleView_Animate(SlipVehicleViewAnimation *state, uint16_t object,
                             const SlipVehicleViewAnimationCalls *calls) {
	const int16_t frameStep = (int16_t)SlipFrameTimer_Step();
	state->fanStep = (int16_t)(((int32_t)0x7fff * frameStep) >> 14);
	state->jetStep = (int16_t)(state->fanStep >> 1);
	int16_t angle = state->jetAngle;
	if (state->jetDirection == 0) {
		angle = (int16_t)(angle + state->jetStep);
		if (angle >= 0x4000) {
			angle = 0x4000;
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
	for (uint32_t partTag = 0x66616e31; partTag != 0x66616e35; ++partTag) {
		uint16_t fanAngle;
		if (calls->getAngle(calls->context, object, partTag, &fanAngle))
			calls->setAngle(calls->context, object, partTag, (uint16_t)(fanAngle + state->fanStep));
	}
	for (uint32_t partTag = 0x6a657431; partTag != 0x6a657435; ++partTag)
		calls->setAngle(calls->context, object, partTag, (uint16_t)state->jetAngle);
}
