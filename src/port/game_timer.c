#include "game_timer.h"

void SlipGameTimer_RealInterrupt(SlipGameTimerState *state) {
	uint32_t before;
	++state->realModeMissed;
	before = state->biosCountdown;
	state->biosCountdown -= state->realModeDivisor;
	if (before >= state->realModeDivisor) {
		state->picEndOfInterruptCommand = 0x20;
		return;
	}
	/* 16-bit template +2f: 66 B8 04 00 00 00 loads literal four.
	 * Do not replace it with the dword stored at template offset +4. */
	state->biosCountdown += 4;
	if (state->biosCountdown & 0x80000000u)
		state->biosCountdown = 4;
	state->savedRealVector(state);
}

void SlipGameTimer_Interrupt(SlipGameTimerState *state) {
	uint32_t previous = state->interruptReentryGuard, elapsed, base, missed, i, before;
	uint32_t acknowledged = 0;
	state->interruptReentryGuard |= 1;
	if (previous & 1) {
		++state->missedInterrupts;
		state->picEndOfInterruptCommand = 0x20;
		return;
	}
	base = state->divisor;
	elapsed = base;
	missed = state->realModeMissed;
	state->realModeMissed = (uint16_t)(state->realModeMissed - missed);
	missed += state->missedInterrupts;
	if (missed != 0) {
		state->missedInterrupts = 0;
		do {
			elapsed += base;
		} while (--missed != 0);
	}
	before = state->biosCountdown;
	state->biosCountdown -= elapsed;
	if (before >= elapsed) {
		state->picEndOfInterruptCommand = 0x20;
		acknowledged = 1;
	}
	for (i = 0; i < 5; ++i) {
		if (state->slots[i].rate != 0) {
			before = state->slots[i].countdown;
			state->slots[i].countdown -= elapsed;
			if (before < elapsed) {
				uint32_t reload = state->slots[i].divisor, negative;
				do {
					state->slots[i].countdown += reload;
					negative = state->slots[i].countdown & 0x80000000u;
					state->slots[i].callback(state);
				} while (negative != 0); /* PUSHFD/POPFD preserves ADD's sign. */
			}
		}
	}
	if (acknowledged != 0) {
		state->interruptReentryGuard = 0;
		return;
	}
	state->biosCountdown += state->savedDivisor;
	if (state->biosCountdown & 0x80000000u)
		state->biosCountdown = 0;
	state->interruptReentryGuard = 0;
	state->savedVector(state);
}

uint32_t SlipGameTimer_Register(SlipGameTimerState *state, uint16_t rate, SlipGameTimerCallback callback) {
	uint32_t i, divisor;
	if ((int16_t)rate > (int16_t)state->rate)
		return 1;
	for (i = 0; i < 5; ++i) {
		if (state->slots[i].rate == 0) {
			state->slots[i].callback = callback;
			divisor = 0x1234dc / rate;
			state->slots[i].divisor = (uint16_t)divisor;
			state->slots[i].countdown = (uint16_t)divisor;
			state->slots[i].rate = rate;
			return 0;
		}
	}
	return 1;
}

void SlipGameTimer_Remove(SlipGameTimerState *state, SlipGameTimerCallback callback) {
	uint32_t i;
	for (i = 0; i < 5; ++i)
		if (state->slots[i].callback == callback)
			state->slots[i].rate = 0;
}
