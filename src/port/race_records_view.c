#include "race_records_view.h"
#include "frame_timer.h"

void SlipLapRecords_Animate(SlipLapRecordsAnimation *state, const SlipView3DMaths *maths) {
	const uint32_t step = SlipFrameTimer_Values().stepQ14;
	const uint32_t fadeStep = (uint32_t)(((uint64_t)0xc000u * step) >> 14);
	for (unsigned i = 0; i < 3; ++i) {
		state->fade[i] = (int32_t)((uint32_t)state->fade[i] + fadeStep);
		if (state->fade[i] > 0xffff)
			state->fade[i] = 0xffff;
	}
	const uint16_t angle = (uint16_t)((0x3000u * (uint16_t)SlipFrameTimer_Step()) >> 14);
	SlipView3D_ApplyPitchMatrix(maths, (int16_t)angle, &state->matrix);
	SlipView3D_ApplyRow0Row1Rotation(maths, (int16_t)(uint16_t)(angle * 2u), &state->matrix);
	SlipView3D_OrthonormalizeForwardBasis(&state->matrix);
}
