#include "hmi_timer.h"

#include <stddef.h>

void HmiTimer_Construct(HmiTimerState *state) {
	uint32_t i;
	*state = (HmiTimerState){0};
	state->divisor = UINT32_MAX;
	for (i = 0; i < 16; ++i)
		state->songForSlot[i] = 0xffu;
}

uint32_t HmiTimer_Initialize(HmiTimerState *state, uint32_t rate, uint32_t flags) {
	state->dpmiMode = (state->dpmiMode & 0xffffff00u) | ((flags & 2u) != 0);
	if ((flags & 1u) == 0) {
		state->hardwareEnabled = (state->hardwareEnabled & 0xffffff00u) | 1u;
		HmiTimer_Mask(state);
		HmiTimer_Install(state, 0xffffu, HmiTimer_Dispatch);
		HmiTimer_Unmask(state);
	} else {
		state->hardwareEnabled &= 0xffffff00u;
	}
	if (rate != 0 && (flags & 1u) == 0) {
		if (rate == 0xff00u) {
			HmiTimer_SetDivisor(state, 0xffffu);
			state->rates[15] = 0xff00u;
		} else {
			HmiTimer_SetDivisor(state, 0x1234dcu / rate);
			state->rates[15] = rate;
		}
		state->callbacks[15] = HmiTimer_SystemTick;
		state->increments[15] = 0x10000u;
	} else {
		state->divisor = 0xffffu;
	}
	return 0;
}

uint32_t HmiTimer_Shutdown(HmiTimerState *state, uint32_t unused) {
	(void)unused;
	if ((state->hardwareEnabled & 0xffu) != 0) {
		HmiTimer_Mask(state);
		HmiTimer_Restore(state);
		HmiTimer_Unmask(state);
	}
	return 0;
}

void HmiTimer_SystemTick(HmiTimerState *state) { HmiTimer_Chain(state); }

void HmiTimer_Chain(HmiTimerState *state) {
	if (state->hardwareEnabled != 0) {
		state->interruptActive = 0;

		longjmp(*state->interruptExit, 1);
	}
}

void HmiTimer_Install(HmiTimerState *state, uint32_t divisor, HmiTimerCallback callback) {
	state->interruptCallback = callback;

	if (state->hardwareEnabled != 0) {
		state->picInterruptMask |= 1u;
		if (state->dpmiMode != 0) {
			state->savedVector = state->protectedTimerVector;
			state->protectedTimerVector = HmiTimer_Interrupt;
		} else {
			state->savedVector = state->dosTimerVector;
			state->dosTimerVector = HmiTimer_Interrupt;
		}
		state->pitControl = 0x36;
		state->pitChannel0Reload = (uint16_t)(divisor & 0xffffu);
		state->picInterruptMask &= 0xfeu;
	}
}

void HmiTimer_Restore(HmiTimerState *state) {
	if (state->hardwareEnabled != 0) {
		state->picInterruptMask |= 1u;
		if (state->dpmiMode != 0)
			state->protectedTimerVector = state->savedVector;
		else
			state->dosTimerVector = state->savedVector;
		/* Two zero bytes go to port 40 without a new control word. */
		state->pitChannel0Reload = 0;
		state->picInterruptMask &= 0xfeu;
	}
}

void HmiTimer_Interrupt(HmiTimerState *state) {

	if (state->interruptActive != 1) {
		jmp_buf continuation;
		jmp_buf *const previous = state->interruptExit;
		state->interruptActive = 1;
		state->interruptExit = &continuation;
		if (setjmp(continuation) != 0) {
			state->interruptExit = previous;
			state->savedVector(state);
			return;
		}
		state->interruptCallback(state);
		state->interruptExit = previous;
		state->interruptActive = 0;
	}
	/* Both paths acknowledge the PIC, including a suppressed re-entry. */
	state->picCommand = 0x20;
}

void HmiTimer_Dispatch(HmiTimerState *state) {
	++state->dispatchDepth;
	for (state->currentSlot = 0; state->currentSlot < 16; ++state->currentSlot) {
		if (state->callbacks[state->currentSlot] != NULL) {
			state->accumulators[state->currentSlot] += state->increments[state->currentSlot];

			if ((state->accumulators[state->currentSlot] & 0x10000u) != 0) {
				state->accumulators[state->currentSlot] &= 0xffffu;
				if (state->songForSlot[state->currentSlot] != 0xffu)
					state->currentSong = state->songForSlot[state->currentSlot];
				state->callbacks[state->currentSlot](state);
			}
		}
	}
	--state->dispatchDepth;
}

uint32_t HmiTimer_Register(HmiTimerState *state, uint32_t rate, HmiTimerCallback callback, uint32_t *slot) {
	uint32_t scale = 0;
	uint32_t i;
	uint32_t selected;
	for (i = 0; i < 16; ++i) {
		if (state->callbacks[i] == NULL)
			break;
	}
	if (i >= 16)
		return 11;
	if ((state->hardwareEnabled & 0xffu) != 0)
		HmiTimer_Mask(state);
	selected = i;
	state->callbacks[i] = callback;
	state->rates[i] = rate;
	if (0x1234dcu / rate < state->divisor) {
		HmiTimer_SetDivisor(state, 0x1234dcu / rate);
		scale = (state->divisor << 16) / (0x1234dcu / rate);
	}
	for (i = 0; i < 16; ++i) {
		if (state->callbacks[i] != NULL) {
			if (state->rates[i] == 0xff00u) {
				if (state->divisor == 0xffffu)
					state->increments[i] = 0x10000u;
				else
					state->increments[i] = 0x123333u / (0x1234dcu / state->divisor);
			} else {
				state->increments[i] = (state->rates[i] << 16) / (0x1234dcu / state->divisor);
			}
			if (scale != 0) {
				/* Both IMUL results truncate before their independent SHR 16.
				 * This is not a conventional 16.16 multiply. */
				const uint32_t low = (state->accumulators[i] * (scale & 0xffffu)) >> 16;
				const uint32_t high = (state->accumulators[i] * (scale >> 16)) >> 16;
				state->accumulators[i] = low + high;
			}
		}
	}
	if ((state->hardwareEnabled & 0xffu) != 0)
		HmiTimer_Unmask(state);
	*slot = selected;
	return 0;
}

void HmiTimer_ProgramPit(HmiTimerState *state, uint32_t divisor) {
	if (state->hardwareEnabled != 0) {
		state->picInterruptMask |= 1u;
		state->pitControl = 0x36;
		/* OUT low byte, then high byte: PIT latch takes the low 16 bits. */
		state->pitChannel0Reload = (uint16_t)(divisor & 0xffffu);
		state->picInterruptMask &= 0xfeu;
	}
}

void HmiTimer_Mask(HmiTimerState *state) {
	if (state->hardwareEnabled != 0)
		state->picInterruptMask |= 1u;
}

void HmiTimer_Unmask(HmiTimerState *state) {
	if (state->hardwareEnabled != 0)
		state->picInterruptMask &= 0xfeu;
}

uint32_t HmiTimer_SetDivisor(HmiTimerState *state, uint32_t divisor) {
	state->divisor = divisor;
	HmiTimer_ProgramPit(state, divisor);
	return 0;
}

uint32_t HmiTimer_GetRate(const HmiTimerState *state, uint32_t slot) { return state->rates[slot]; }

uint32_t HmiTimer_SetRate(HmiTimerState *state, uint32_t slot, uint32_t rate) {
	uint32_t i;
	if (slot >= 16)
		return 10;
	if (state->callbacks[slot] == NULL)
		return 10;
	if ((state->hardwareEnabled & 0xffu) != 0)
		HmiTimer_Mask(state);
	state->rates[slot] = rate;
	if (0x1234dcu / rate < state->divisor)
		HmiTimer_SetDivisor(state, 0x1234dcu / rate);
	for (i = 0; i < 16; ++i) {
		if (state->callbacks[i] != NULL) {
			if (state->rates[i] == 0xff00u) {
				if (state->divisor == 0xffffu)
					state->increments[i] = 0x10000u;
				else
					state->increments[i] = 0x123333u / (0x1234dcu / state->divisor);
			} else {
				state->increments[i] = (state->rates[i] << 16) / (0x1234dcu / state->divisor);
			}
			state->accumulators[i] = 0;
		}
	}
	if ((state->hardwareEnabled & 0xffu) != 0)
		HmiTimer_Unmask(state);
	return 0;
}

uint32_t HmiTimer_Remove(HmiTimerState *state, uint32_t slot) {
	uint32_t maximum = 0;
	uint32_t i;
	/* Unlike SetRate, the original does not validate the slot. */
	state->callbacks[slot] = NULL;
	for (i = 0; i < 16; ++i) {
		if (state->callbacks[i] != NULL && state->rates[i] > maximum && state->rates[i] != 0xff00u)
			maximum = state->rates[i];
	}
	if (maximum != 0)
		HmiTimer_SetDivisor(state, 0x1234dcu / maximum);
	else
		HmiTimer_SetDivisor(state, 0xffffu);
	if ((state->hardwareEnabled & 0xffu) != 0)
		HmiTimer_Mask(state);
	for (i = 0; i < 16; ++i) {
		if (state->callbacks[i] != NULL) {
			if (state->rates[i] == 0xff00u) {
				if (state->divisor == 0xffffu)
					state->increments[i] = 0x10000u;
				else
					state->increments[i] = 0x123333u / (0x1234dcu / state->divisor);
			} else {
				state->increments[i] = (state->rates[i] << 16) / (0x1234dcu / state->divisor);
			}
			state->accumulators[i] = 0;
		}
	}
	if ((state->hardwareEnabled & 0xffu) != 0)
		HmiTimer_Unmask(state);
	return 0;
}
