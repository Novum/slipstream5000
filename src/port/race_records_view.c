#include "race_records_view.h"
#include "fixed_point.h"
#include "frame_timer.h"

enum {
	SLIP_LAP_RECORDS_FADE_RATE_Q16 = 0xc000,
	SLIP_LAP_RECORDS_PITCH_RATE = 0x3000,
	SLIP_LAP_RECORDS_ROLL_RATE_MULTIPLIER = 2
};

void SlipLapRecords_Animate(SlipLapRecordsAnimation *state, const SlipView3DMaths *maths) {
	const uint32_t step = SlipFrameTimer_Values().stepQ14;
	const uint32_t fadeStep = (uint32_t)(((uint64_t)SLIP_LAP_RECORDS_FADE_RATE_Q16 * step) >> SLIP_Q14_FRACTION_BITS);
	for (unsigned i = 0; i < SLIP_LAP_RECORDS_ROW_COUNT; ++i) {
		state->fade[i] = (int32_t)((uint32_t)state->fade[i] + fadeStep);
		if (state->fade[i] > UINT16_MAX)
			state->fade[i] = UINT16_MAX;
	}
	const uint16_t angle =
	    (uint16_t)((SLIP_LAP_RECORDS_PITCH_RATE * (uint16_t)SlipFrameTimer_Step()) >> SLIP_Q14_FRACTION_BITS);
	SlipView3D_ApplyPitchMatrix(maths, (int16_t)angle, &state->matrix);
	SlipView3D_ApplyRow0Row1Rotation(maths, (int16_t)(uint16_t)(angle * SLIP_LAP_RECORDS_ROLL_RATE_MULTIPLIER),
	                                 &state->matrix);
	SlipView3D_OrthonormalizeForwardBasis(&state->matrix);
}
