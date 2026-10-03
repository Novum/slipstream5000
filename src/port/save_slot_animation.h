#ifndef SLIPSTREAM5000_SAVE_SLOT_ANIMATION_H
#define SLIPSTREAM5000_SAVE_SLOT_ANIMATION_H
#include "championship_save_format.h"
#include <stdbool.h>
#include <stdint.h>

enum { SLIP_SAVE_SLOT_LAST_FRAME = 8, SLIP_SAVE_SLOT_FRAME_PERIOD = 60 };

typedef struct SlipSaveSlotAnimation {
	int16_t currentFrame[SLIP_SAVE_SLOT_COUNT];
	int16_t targetFrame[SLIP_SAVE_SLOT_COUNT];
	uint16_t remainingMilliseconds;
} SlipSaveSlotAnimation;

bool SlipSaveSlotAnimation_Update(SlipSaveSlotAnimation *animation);
#endif
