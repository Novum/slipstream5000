#include "runtime.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

enum {
	SLIP_RANDOM_INITIAL_STATE_WORDS = 0x02485a4au,
	SLIP_RANDOM_INITIAL_STATE_TAIL = 0xb753u,
	SLIP_RANDOM_STIR_CALLS_PER_TICK_SHIFT = 2
};

uint16_t SlipRuntime_cleanupCount;
SlipRuntimeCleanup SlipRuntime_cleanupCallbacks[SLIP_RUNTIME_CLEANUP_CALLBACK_CAPACITY];
uint8_t SlipRuntime_active;
uint32_t SlipRuntime_error;
uint32_t SlipRandom_stateWords = SLIP_RANDOM_INITIAL_STATE_WORDS;
uint16_t SlipRandom_stateTail = SLIP_RANDOM_INITIAL_STATE_TAIL;

void SlipRuntime_RegisterExit(SlipRuntimeCleanup callback) {
	const uint32_t existingCount = SlipRuntime_cleanupCount;
	const uint32_t insertionIndex = existingCount;
	uint32_t index;

	for (index = 0; index < existingCount; ++index) {
		if (SlipRuntime_cleanupCallbacks[index] == callback) {
			return;
		}
	}
	if (insertionIndex == SLIP_RUNTIME_CLEANUP_CALLBACK_CAPACITY) {
		SlipRuntime_Fatal("ERROR: RegisterExit list full.");
	}
	++SlipRuntime_cleanupCount;
	SlipRuntime_cleanupCallbacks[insertionIndex] = callback;
}

void SlipRandom_Stir(uint8_t biosTickLow) {
	uint32_t count = ((uint32_t)biosTickLow + 1u) << SLIP_RANDOM_STIR_CALLS_PER_TICK_SHIFT;
	do {
		SlipRandom_Next();
	} while (--count != 0);
}

void SlipRandom_SetState(uint32_t stateWords, uint16_t stateTail) {
	SlipRandom_stateWords = stateWords;
	SlipRandom_stateTail = stateTail;
}

SlipRandomState SlipRandom_GetState(void) { return (SlipRandomState){SlipRandom_stateWords, SlipRandom_stateTail}; }

uint32_t SlipRandom_Next(void) {
	uint16_t stateSum = (uint16_t)SlipRandom_stateWords;
	uint16_t stateAddend = (uint16_t)(SlipRandom_stateWords >> 16);

	SlipRandom_stateWords = (SlipRandom_stateWords & (UINT32_MAX ^ UINT16_MAX)) | stateAddend;
	stateSum = (uint16_t)(stateSum + stateAddend);
	stateAddend = SlipRandom_stateTail;
	SlipRandom_stateWords = (SlipRandom_stateWords & UINT16_MAX) | ((uint32_t)stateSum << 16);
	stateSum = (uint16_t)(stateSum + stateAddend);
	SlipRandom_stateTail = stateSum;
	return stateSum;
}

uint32_t SlipRandom_Range(uint16_t inclusiveMaximum) {
	const uint16_t rangeSize = (uint16_t)(inclusiveMaximum + 1u);
	const uint32_t product = (uint16_t)SlipRandom_Next() * (uint32_t)rangeSize;

	return product >> SLIP_RANDOM_SAMPLE_BITS;
}

void SlipRuntime_Shutdown(void) {
	if (SlipRuntime_active != 0) {
		uint32_t remainingCallbacks = SlipRuntime_cleanupCount;

		if (remainingCallbacks != 0) {
			SlipRuntimeCleanup *callback = &SlipRuntime_cleanupCallbacks[remainingCallbacks - 1u];
			do {
				(*callback)();
				--callback;
				--remainingCallbacks;
			} while (remainingCallbacks != 0);
		}
		SlipRuntime_active = 0;
	}
}

enum { SLIP_RUNTIME_FATAL_MESSAGE_CHARACTERS = 160 };

SLIP_RUNTIME_NORETURN void SlipRuntime_Fatal(const char *message) {
	char buffer[SLIP_RUNTIME_FATAL_MESSAGE_CHARACTERS + 1];
	char *destination = buffer;

	do {
		const char character = *message++;
		if (character == '\0')
			break;
		*destination++ = character;
	} while (destination < buffer + SLIP_RUNTIME_FATAL_MESSAGE_CHARACTERS);
	*destination = '\0';
	SlipRuntime_Shutdown();
	fputs(buffer, stderr);
	fputs("\r\n", stderr);
	SDL_TriggerBreakpoint();
	_Exit(0);
}
