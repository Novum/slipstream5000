#include "save_slot_animation.h"
#include "frame_timer.h"

bool SlipSaveSlotAnimation_Update(SlipSaveSlotAnimation *animation) {
	unsigned pending = 1;
	const uint16_t elapsed = (uint16_t)SlipFrameTimer_Values().deltaMilliseconds;
	bool borrow = animation->remainingMilliseconds < elapsed;
	animation->remainingMilliseconds = (uint16_t)(animation->remainingMilliseconds - elapsed);
	if (borrow) {
		animation->remainingMilliseconds = (uint16_t)(animation->remainingMilliseconds + SLIP_SAVE_SLOT_FRAME_PERIOD);
		if ((int16_t)animation->remainingMilliseconds < 0)
			animation->remainingMilliseconds = 0;
		--pending;
		for (unsigned slot = 0; slot < SLIP_SAVE_SLOT_COUNT; ++slot) {
			int16_t frame = animation->currentFrame[slot];
			if (frame != animation->targetFrame[slot]) {
				++pending;
				if (frame > animation->targetFrame[slot])
					--frame;
				else
					++frame;
				animation->currentFrame[slot] = frame;
			}
		}
	}
	return pending != 0;
}
