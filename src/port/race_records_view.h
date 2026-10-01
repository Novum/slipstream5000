#ifndef SLIPSTREAM5000_RACE_RECORDS_VIEW_H
#define SLIPSTREAM5000_RACE_RECORDS_VIEW_H
#include "view3d.h"

typedef struct SlipLapRecordsAnimation {
	int32_t fade[3];
	SlipView3DMatrix matrix;
} SlipLapRecordsAnimation;

void SlipLapRecords_Animate(SlipLapRecordsAnimation *state, const SlipView3DMaths *maths);
#endif
