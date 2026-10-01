#include "frame_timer.h"

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

	if (timerRate > 0x46u) {
		timerRate = 0x46u;
	}
	if (timerRate == 0) {
		return;
	}
	periodQ16 = 0x03e80000u / timerRate;
	g_frameTimerMinimumDelta = periodQ16 >> 16;
	g_frameTimerPreviousTick = 0;
	g_frameTimerAccumulated = 0;
	g_frameTimerWorkingDelta = 0;
	SlipFrameTimer_Reset();
}

void SlipFrameTimer_Reset(void) {
	g_frameTimerSuppressedSamples = 2;
	g_frameTimerAccumulated = 0;
	g_frameTimerWorkingDelta = 0;
}

void SlipFrameTimer_Update(uint32_t tick) {
	if (g_frameTimerSuppressedSamples == 0) {
		g_frameTimerAccumulated += g_frameTimerWorkingDelta;
		g_frameTimerWorkingDelta = tick - g_frameTimerPreviousTick;
		g_frameTimerPreviousTick = tick;
		if (g_frameTimerWorkingDelta > 1000u) {
			g_frameTimerWorkingDelta = 1000u;
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

	if (boundedMilliseconds > 1000u)
		boundedMilliseconds = 1000u;
	SlipFrameTimer_delta = boundedMilliseconds;
	numeratorHighWord = (uint16_t)boundedMilliseconds;
	quotientLowWord = 0;
	numeratorHighWord >>= 1;
	quotientLowWord = (uint16_t)((int16_t)quotientLowWord >> 1);
	numeratorHighWord >>= 1;
	quotientLowWord = (uint16_t)((int16_t)quotientLowWord >> 1);
	quotientLowWord = (uint16_t)(((uint32_t)numeratorHighWord << 16 | quotientLowWord) / 1000u);
	SlipFrameTimer_step = (SlipFrameTimer_step & 0xffff0000u) | quotientLowWord;
	SlipFrameTimer_inverse = 0;
	if ((uint16_t)boundedMilliseconds != 0) {
		quotientLowWord = (uint16_t)(1000u / (uint16_t)boundedMilliseconds);
		SlipFrameTimer_inverse = (SlipFrameTimer_inverse & 0xffff0000u) | quotientLowWord;
	}
}
