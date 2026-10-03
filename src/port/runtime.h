#ifndef SLIPSTREAM5000_RUNTIME_H
#define SLIPSTREAM5000_RUNTIME_H

#include <stdint.h>

enum { SLIP_RUNTIME_CLEANUP_CALLBACK_CAPACITY = 40 };

enum {
	SLIP_RUNTIME_ERROR_FILE_UNAVAILABLE = 2,
	SLIP_RUNTIME_ERROR_READ_FAILED = 3,
	SLIP_RUNTIME_ERROR_CREATE_FAILED = 4,
	SLIP_RUNTIME_ERROR_WRITE_FAILED = 5,
	SLIP_RUNTIME_ERROR_MEMORY_EXHAUSTED = 6,
	SLIP_RUNTIME_ERROR_CAPACITY_EXHAUSTED = 7,
	SLIP_RUNTIME_ERROR_HANDLES_EXHAUSTED = 8
};

typedef void (*SlipRuntimeCleanup)(void);

#if defined(_MSC_VER)
#define SLIP_RUNTIME_NORETURN __declspec(noreturn)
#elif defined(__GNUC__) || defined(__clang__)
#define SLIP_RUNTIME_NORETURN __attribute__((noreturn))
#else
#define SLIP_RUNTIME_NORETURN
#endif

extern uint16_t SlipRuntime_cleanupCount;
extern SlipRuntimeCleanup SlipRuntime_cleanupCallbacks[SLIP_RUNTIME_CLEANUP_CALLBACK_CAPACITY];
extern uint8_t SlipRuntime_active;
extern uint32_t SlipRuntime_error;

/* Random samples contain 16 bits; scaling a sample by a range divides by 2^16. */
enum { SLIP_RANDOM_SAMPLE_BITS = 16 };

typedef struct SlipRandomState {
	uint32_t stateWords;
	uint32_t stateTail;
} SlipRandomState;

extern uint32_t SlipRandom_stateWords;
extern uint16_t SlipRandom_stateTail;

void SlipRuntime_Shutdown(void);
void SlipRuntime_RegisterExit(SlipRuntimeCleanup callback);
SLIP_RUNTIME_NORETURN void SlipRuntime_Fatal(const char *message);

void SlipRandom_SetState(uint32_t stateWords, uint16_t stateTail);
void SlipRandom_Stir(uint8_t biosTickLow);
SlipRandomState SlipRandom_GetState(void);
uint32_t SlipRandom_Next(void);
uint32_t SlipRandom_Range(uint16_t inclusiveMaximum);

#endif
