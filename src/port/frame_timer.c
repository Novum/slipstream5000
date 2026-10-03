#include "frame_timer.h"

enum {
	SLIP_FRAME_TIMER_MILLISECONDS_PER_SECOND = 1000,
	SLIP_FRAME_TIMER_PERIOD_FRACTION_BITS = 16,
	SLIP_FRAME_TIMER_MAXIMUM_DELTA_MS = 1000,
	SLIP_FRAME_TIMER_STARTUP_SUPPRESSED_SAMPLES = 2
};

static const uint32_t SLIP_FRAME_TIMER_UPPER_WORD_MASK = UINT32_MAX ^ UINT16_MAX;

uint32_t SlipFrameTimer_inverse;
uint32_t SlipFrameTimer_delta;
uint32_t SlipFrameTimer_step;
static uint32_t g_frameTimerPreviousTick;
static uint32_t g_frameTimerMinimumDelta;
static uint32_t g_frameTimerAccumulated;
static uint32_t g_frameTimerWorkingDelta;
static uint32_t g_frameTimerSuppressedSamples;

void SlipFrameTimer_InitializeHostRate(uint16_t timerRate) {
	uint32_t periodQ16;

	if (timerRate > SLIP_FRAME_TIMER_MAXIMUM_RATE_HZ) {
		timerRate = SLIP_FRAME_TIMER_MAXIMUM_RATE_HZ;
	}
	if (timerRate == 0) {
		return;
	}
	periodQ16 = (SLIP_FRAME_TIMER_MILLISECONDS_PER_SECOND << SLIP_FRAME_TIMER_PERIOD_FRACTION_BITS) / timerRate;
	g_frameTimerMinimumDelta = periodQ16 >> SLIP_FRAME_TIMER_PERIOD_FRACTION_BITS;
	g_frameTimerPreviousTick = 0;
	g_frameTimerAccumulated = 0;
	g_frameTimerWorkingDelta = 0;
	SlipFrameTimer_Reset();
}

void SlipFrameTimer_Reset(void) {
	g_frameTimerSuppressedSamples = SLIP_FRAME_TIMER_STARTUP_SUPPRESSED_SAMPLES;
	g_frameTimerAccumulated = 0;
	g_frameTimerWorkingDelta = 0;
}

void SlipFrameTimer_Update(uint32_t tick) {
	if (g_frameTimerSuppressedSamples == 0) {
		g_frameTimerAccumulated += g_frameTimerWorkingDelta;
		g_frameTimerWorkingDelta = tick - g_frameTimerPreviousTick;
		g_frameTimerPreviousTick = tick;
		if (g_frameTimerWorkingDelta > SLIP_FRAME_TIMER_MAXIMUM_DELTA_MS) {
			g_frameTimerWorkingDelta = SLIP_FRAME_TIMER_MAXIMUM_DELTA_MS;
		}
		if (g_frameTimerWorkingDelta < g_frameTimerMinimumDelta) {
			g_frameTimerWorkingDelta = g_frameTimerMinimumDelta;
		}
		SlipFrameTimer_SetDelta((uint16_t)g_frameTimerWorkingDelta);
	} else {
		--g_frameTimerSuppressedSamples;
		SlipFrameTimer_delta = 0;
		SlipFrameTimer_step = 0;
		SlipFrameTimer_inverse = 0;
		g_frameTimerPreviousTick = tick;
	}
}

SlipFrameTimerValues SlipFrameTimer_Values(void) {
	return (SlipFrameTimerValues){SlipFrameTimer_delta, SlipFrameTimer_step, SlipFrameTimer_inverse};
}

uint32_t SlipFrameTimer_Step(void) { return SlipFrameTimer_step; }

void SlipFrameTimer_SetRecordedDelta(uint16_t milliseconds) {
	SlipFrameTimer_SetDelta(milliseconds);
	g_frameTimerWorkingDelta = milliseconds;
}

void SlipFrameTimer_SetDelta(uint16_t deltaMilliseconds) {
	uint32_t boundedMilliseconds = deltaMilliseconds;
	uint16_t numeratorHighWord;
	uint16_t quotientLowWord;

	if (boundedMilliseconds > SLIP_FRAME_TIMER_MAXIMUM_DELTA_MS)
		boundedMilliseconds = SLIP_FRAME_TIMER_MAXIMUM_DELTA_MS;
	SlipFrameTimer_delta = boundedMilliseconds;
	numeratorHighWord = (uint16_t)boundedMilliseconds;
	quotientLowWord = 0;
	numeratorHighWord >>= 1;
	quotientLowWord = (uint16_t)((int16_t)quotientLowWord >> 1);
	numeratorHighWord >>= 1;
	quotientLowWord = (uint16_t)((int16_t)quotientLowWord >> 1);
	quotientLowWord =
	    (uint16_t)(((uint32_t)numeratorHighWord << 16 | quotientLowWord) / SLIP_FRAME_TIMER_MILLISECONDS_PER_SECOND);
	SlipFrameTimer_step = (SlipFrameTimer_step & SLIP_FRAME_TIMER_UPPER_WORD_MASK) | quotientLowWord;
	SlipFrameTimer_inverse = 0;
	if ((uint16_t)boundedMilliseconds != 0) {
		quotientLowWord = (uint16_t)(SLIP_FRAME_TIMER_MILLISECONDS_PER_SECOND / (uint16_t)boundedMilliseconds);
		SlipFrameTimer_inverse = (SlipFrameTimer_inverse & SLIP_FRAME_TIMER_UPPER_WORD_MASK) | quotientLowWord;
	}
}
