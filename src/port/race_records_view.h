#ifndef SLIPSTREAM5000_RACE_RECORDS_VIEW_H
#define SLIPSTREAM5000_RACE_RECORDS_VIEW_H
#include "race_records.h"
#include "view3d.h"

enum { SLIP_LAP_RECORDS_ROW_FADE_DELAY = 32768 };

typedef struct SlipLapRecordsAnimation {
	int32_t fade[SLIP_LAP_RECORDS_ROW_COUNT];
	SlipView3DMatrix matrix;
} SlipLapRecordsAnimation;

void SlipLapRecords_Animate(SlipLapRecordsAnimation *state, const SlipView3DMaths *maths);
#endif
