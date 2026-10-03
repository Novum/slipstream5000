#ifndef SLIPSTREAM5000_FRAME_TIMER_H
#define SLIPSTREAM5000_FRAME_TIMER_H

#include <stdint.h>

enum { SLIP_FRAME_TIMER_MAXIMUM_RATE_HZ = 70 };

typedef struct SlipFrameTimerValues {
	uint32_t deltaMilliseconds;
	uint32_t stepQ14;
	uint32_t frameRateHz;
} SlipFrameTimerValues;

extern uint32_t SlipFrameTimer_inverse;
extern uint32_t SlipFrameTimer_delta;
extern uint32_t SlipFrameTimer_step;

void SlipFrameTimer_InitializeHostRate(uint16_t timerRate);
void SlipFrameTimer_Reset(void);
void SlipFrameTimer_Update(uint32_t tick);
SlipFrameTimerValues SlipFrameTimer_Values(void);
uint32_t SlipFrameTimer_Step(void);
void SlipFrameTimer_SetDelta(uint16_t deltaMilliseconds);
void SlipFrameTimer_SetRecordedDelta(uint16_t milliseconds);

#endif
