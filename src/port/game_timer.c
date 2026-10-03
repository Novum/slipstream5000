#include "game_timer.h"
#include "hmi_timer.h"

enum { SLIP_GAME_TIMER_REAL_MODE_COUNTDOWN_RELOAD = 4, SLIP_GAME_TIMER_INTERRUPT_ACTIVE = 1u };

static const uint32_t SLIP_GAME_TIMER_COUNTDOWN_SIGN_BIT = UINT32_C(1) << 31;

void SlipGameTimer_RealInterrupt(SlipGameTimerState *state) {
	uint32_t before;
	++state->realModeMissed;
	before = state->biosCountdown;
	state->biosCountdown -= state->realModeDivisor;
	if (before >= state->realModeDivisor) {
		state->picEndOfInterruptCommand = HMI_TIMER_PIC_END_OF_INTERRUPT;
		return;
	}
	/* 16-bit template +2f: 66 B8 04 00 00 00 loads literal four.
	 * Do not replace it with the dword stored at template offset +4. */
	state->biosCountdown += SLIP_GAME_TIMER_REAL_MODE_COUNTDOWN_RELOAD;
	if (state->biosCountdown & SLIP_GAME_TIMER_COUNTDOWN_SIGN_BIT)
		state->biosCountdown = SLIP_GAME_TIMER_REAL_MODE_COUNTDOWN_RELOAD;
	state->savedRealVector(state);
}

void SlipGameTimer_Interrupt(SlipGameTimerState *state) {
	uint32_t previous = state->interruptReentryGuard, elapsed, base, missed, i, before;
	uint32_t acknowledged = 0;
	state->interruptReentryGuard |= SLIP_GAME_TIMER_INTERRUPT_ACTIVE;
	if (previous & SLIP_GAME_TIMER_INTERRUPT_ACTIVE) {
		++state->missedInterrupts;
		state->picEndOfInterruptCommand = HMI_TIMER_PIC_END_OF_INTERRUPT;
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
		state->picEndOfInterruptCommand = HMI_TIMER_PIC_END_OF_INTERRUPT;
		acknowledged = 1;
	}
	for (i = 0; i < SLIP_GAME_TIMER_SLOT_COUNT; ++i) {
		if (state->slots[i].rate != 0) {
			before = state->slots[i].countdown;
			state->slots[i].countdown -= elapsed;
			if (before < elapsed) {
				uint32_t reload = state->slots[i].divisor, negative;
				do {
					state->slots[i].countdown += reload;
					negative = state->slots[i].countdown & SLIP_GAME_TIMER_COUNTDOWN_SIGN_BIT;
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
	if (state->biosCountdown & SLIP_GAME_TIMER_COUNTDOWN_SIGN_BIT)
		state->biosCountdown = 0;
	state->interruptReentryGuard = 0;
	state->savedVector(state);
}

uint32_t SlipGameTimer_Register(SlipGameTimerState *state, uint16_t rate, SlipGameTimerCallback callback) {
	uint32_t i, divisor;
	if ((int16_t)rate > (int16_t)state->rate)
		return 1;
	for (i = 0; i < SLIP_GAME_TIMER_SLOT_COUNT; ++i) {
		if (state->slots[i].rate == 0) {
			state->slots[i].callback = callback;
			divisor = HMI_TIMER_PIT_CLOCK_HZ / rate;
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
	for (i = 0; i < SLIP_GAME_TIMER_SLOT_COUNT; ++i)
		if (state->slots[i].callback == callback)
			state->slots[i].rate = 0;
}
