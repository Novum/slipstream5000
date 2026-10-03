#include "hmi_timer.h"

#include <stddef.h>

void HmiTimer_Construct(HmiTimerState *state) {
	uint32_t i;
	*state = (HmiTimerState){0};
	state->divisor = UINT32_MAX;
	for (i = 0; i < HMI_TIMER_SLOT_COUNT; ++i)
		state->songForSlot[i] = HMI_TIMER_NO_SONG;
}

uint32_t HmiTimer_Initialize(HmiTimerState *state, uint32_t rate, uint32_t flags) {
	state->dpmiMode = (state->dpmiMode & HMI_TIMER_HIGH_BYTES_MASK) | ((flags & HMI_TIMER_USE_DPMI) != 0);
	if ((flags & HMI_TIMER_SKIP_HARDWARE) == 0) {
		state->hardwareEnabled = (state->hardwareEnabled & HMI_TIMER_HIGH_BYTES_MASK) | 1u;
		HmiTimer_Mask(state);
		HmiTimer_Install(state, HMI_TIMER_MAXIMUM_DIVISOR, HmiTimer_Dispatch);
		HmiTimer_Unmask(state);
	} else {
		state->hardwareEnabled &= HMI_TIMER_HIGH_BYTES_MASK;
	}
	if (rate != 0 && (flags & HMI_TIMER_SKIP_HARDWARE) == 0) {
		if (rate == HMI_TIMER_SYSTEM_RATE) {
			HmiTimer_SetDivisor(state, HMI_TIMER_MAXIMUM_DIVISOR);
			state->rates[HMI_TIMER_SYSTEM_SLOT] = HMI_TIMER_SYSTEM_RATE;
		} else {
			HmiTimer_SetDivisor(state, HMI_TIMER_PIT_CLOCK_HZ / rate);
			state->rates[HMI_TIMER_SYSTEM_SLOT] = rate;
		}
		state->callbacks[HMI_TIMER_SYSTEM_SLOT] = HmiTimer_SystemTick;
		state->increments[HMI_TIMER_SYSTEM_SLOT] = HMI_TIMER_FRACTION_ONE;
	} else {
		state->divisor = HMI_TIMER_MAXIMUM_DIVISOR;
	}
	return 0;
}

uint32_t HmiTimer_Shutdown(HmiTimerState *state, uint32_t unused) {
	(void)unused;
	if ((state->hardwareEnabled & UINT8_MAX) != 0) {
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
		state->picInterruptMask |= HMI_TIMER_IRQ0_MASK;
		if (state->dpmiMode != 0) {
			state->savedVector = state->protectedTimerVector;
			state->protectedTimerVector = HmiTimer_Interrupt;
		} else {
			state->savedVector = state->dosTimerVector;
			state->dosTimerVector = HmiTimer_Interrupt;
		}
		state->pitControl = HMI_TIMER_PIT_CHANNEL0_SQUARE_WAVE;
		state->pitChannel0Reload = (uint16_t)(divisor & UINT16_MAX);
		state->picInterruptMask &= HMI_TIMER_IRQ0_CLEAR_MASK;
	}
}

void HmiTimer_Restore(HmiTimerState *state) {
	if (state->hardwareEnabled != 0) {
		state->picInterruptMask |= HMI_TIMER_IRQ0_MASK;
		if (state->dpmiMode != 0)
			state->protectedTimerVector = state->savedVector;
		else
			state->dosTimerVector = state->savedVector;
		/* Two zero bytes go to port 40 without a new control word. */
		state->pitChannel0Reload = 0;
		state->picInterruptMask &= HMI_TIMER_IRQ0_CLEAR_MASK;
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
	state->picCommand = HMI_TIMER_PIC_END_OF_INTERRUPT;
}

void HmiTimer_Dispatch(HmiTimerState *state) {
	++state->dispatchDepth;
	for (state->currentSlot = 0; state->currentSlot < HMI_TIMER_SLOT_COUNT; ++state->currentSlot) {
		if (state->callbacks[state->currentSlot] != NULL) {
			state->accumulators[state->currentSlot] += state->increments[state->currentSlot];

			if ((state->accumulators[state->currentSlot] & HMI_TIMER_FRACTION_ONE) != 0) {
				state->accumulators[state->currentSlot] &= UINT16_MAX;
				if (state->songForSlot[state->currentSlot] != HMI_TIMER_NO_SONG)
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
	for (i = 0; i < HMI_TIMER_SLOT_COUNT; ++i) {
		if (state->callbacks[i] == NULL)
			break;
	}
	if (i >= HMI_TIMER_SLOT_COUNT)
		return HMI_TIMER_ERROR_NO_HANDLES;
	if ((state->hardwareEnabled & UINT8_MAX) != 0)
		HmiTimer_Mask(state);
	selected = i;
	state->callbacks[i] = callback;
	state->rates[i] = rate;
	if (HMI_TIMER_PIT_CLOCK_HZ / rate < state->divisor) {
		HmiTimer_SetDivisor(state, HMI_TIMER_PIT_CLOCK_HZ / rate);
		scale = (state->divisor << HMI_TIMER_FRACTION_BITS) / (HMI_TIMER_PIT_CLOCK_HZ / rate);
	}
	for (i = 0; i < HMI_TIMER_SLOT_COUNT; ++i) {
		if (state->callbacks[i] != NULL) {
			if (state->rates[i] == HMI_TIMER_SYSTEM_RATE) {
				if (state->divisor == HMI_TIMER_MAXIMUM_DIVISOR)
					state->increments[i] = HMI_TIMER_FRACTION_ONE;
				else
					state->increments[i] = HMI_TIMER_SYSTEM_RATE_Q16 / (HMI_TIMER_PIT_CLOCK_HZ / state->divisor);
			} else {
				state->increments[i] =
				    (state->rates[i] << HMI_TIMER_FRACTION_BITS) / (HMI_TIMER_PIT_CLOCK_HZ / state->divisor);
			}
			if (scale != 0) {
				/* Both IMUL results truncate before their independent SHR 16.
				 * This is not a conventional 16.16 multiply. */
				const uint32_t low = (state->accumulators[i] * (scale & UINT16_MAX)) >> HMI_TIMER_FRACTION_BITS;
				const uint32_t high =
				    (state->accumulators[i] * (scale >> HMI_TIMER_FRACTION_BITS)) >> HMI_TIMER_FRACTION_BITS;
				state->accumulators[i] = low + high;
			}
		}
	}
	if ((state->hardwareEnabled & UINT8_MAX) != 0)
		HmiTimer_Unmask(state);
	*slot = selected;
	return 0;
}

void HmiTimer_ProgramPit(HmiTimerState *state, uint32_t divisor) {
	if (state->hardwareEnabled != 0) {
		state->picInterruptMask |= HMI_TIMER_IRQ0_MASK;
		state->pitControl = HMI_TIMER_PIT_CHANNEL0_SQUARE_WAVE;
		/* OUT low byte, then high byte: PIT latch takes the low 16 bits. */
		state->pitChannel0Reload = (uint16_t)(divisor & UINT16_MAX);
		state->picInterruptMask &= HMI_TIMER_IRQ0_CLEAR_MASK;
	}
}

void HmiTimer_Mask(HmiTimerState *state) {
	if (state->hardwareEnabled != 0)
		state->picInterruptMask |= HMI_TIMER_IRQ0_MASK;
}

void HmiTimer_Unmask(HmiTimerState *state) {
	if (state->hardwareEnabled != 0)
		state->picInterruptMask &= HMI_TIMER_IRQ0_CLEAR_MASK;
}

uint32_t HmiTimer_SetDivisor(HmiTimerState *state, uint32_t divisor) {
	state->divisor = divisor;
	HmiTimer_ProgramPit(state, divisor);
	return 0;
}

uint32_t HmiTimer_GetRate(const HmiTimerState *state, uint32_t slot) { return state->rates[slot]; }

uint32_t HmiTimer_SetRate(HmiTimerState *state, uint32_t slot, uint32_t rate) {
	uint32_t i;
	if (slot >= HMI_TIMER_SLOT_COUNT)
		return HMI_TIMER_ERROR_INVALID_HANDLE;
	if (state->callbacks[slot] == NULL)
		return HMI_TIMER_ERROR_INVALID_HANDLE;
	if ((state->hardwareEnabled & UINT8_MAX) != 0)
		HmiTimer_Mask(state);
	state->rates[slot] = rate;
	if (HMI_TIMER_PIT_CLOCK_HZ / rate < state->divisor)
		HmiTimer_SetDivisor(state, HMI_TIMER_PIT_CLOCK_HZ / rate);
	for (i = 0; i < HMI_TIMER_SLOT_COUNT; ++i) {
		if (state->callbacks[i] != NULL) {
			if (state->rates[i] == HMI_TIMER_SYSTEM_RATE) {
				if (state->divisor == HMI_TIMER_MAXIMUM_DIVISOR)
					state->increments[i] = HMI_TIMER_FRACTION_ONE;
				else
					state->increments[i] = HMI_TIMER_SYSTEM_RATE_Q16 / (HMI_TIMER_PIT_CLOCK_HZ / state->divisor);
			} else {
				state->increments[i] =
				    (state->rates[i] << HMI_TIMER_FRACTION_BITS) / (HMI_TIMER_PIT_CLOCK_HZ / state->divisor);
			}
			state->accumulators[i] = 0;
		}
	}
	if ((state->hardwareEnabled & UINT8_MAX) != 0)
		HmiTimer_Unmask(state);
	return 0;
}

uint32_t HmiTimer_Remove(HmiTimerState *state, uint32_t slot) {
	uint32_t maximum = 0;
	uint32_t i;
	/* Unlike SetRate, the original does not validate the slot. */
	state->callbacks[slot] = NULL;
	for (i = 0; i < HMI_TIMER_SLOT_COUNT; ++i) {
		if (state->callbacks[i] != NULL && state->rates[i] > maximum && state->rates[i] != HMI_TIMER_SYSTEM_RATE)
			maximum = state->rates[i];
	}
	if (maximum != 0)
		HmiTimer_SetDivisor(state, HMI_TIMER_PIT_CLOCK_HZ / maximum);
	else
		HmiTimer_SetDivisor(state, HMI_TIMER_MAXIMUM_DIVISOR);
	if ((state->hardwareEnabled & UINT8_MAX) != 0)
		HmiTimer_Mask(state);
	for (i = 0; i < HMI_TIMER_SLOT_COUNT; ++i) {
		if (state->callbacks[i] != NULL) {
			if (state->rates[i] == HMI_TIMER_SYSTEM_RATE) {
				if (state->divisor == HMI_TIMER_MAXIMUM_DIVISOR)
					state->increments[i] = HMI_TIMER_FRACTION_ONE;
				else
					state->increments[i] = HMI_TIMER_SYSTEM_RATE_Q16 / (HMI_TIMER_PIT_CLOCK_HZ / state->divisor);
			} else {
				state->increments[i] =
				    (state->rates[i] << HMI_TIMER_FRACTION_BITS) / (HMI_TIMER_PIT_CLOCK_HZ / state->divisor);
			}
			state->accumulators[i] = 0;
		}
	}
	if ((state->hardwareEnabled & UINT8_MAX) != 0)
		HmiTimer_Unmask(state);
	return 0;
}
