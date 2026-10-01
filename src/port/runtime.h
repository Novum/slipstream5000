#ifndef SLIPSTREAM5000_RUNTIME_H
#define SLIPSTREAM5000_RUNTIME_H

#include <stdint.h>

typedef void (*SlipRuntimeCleanup)(void);

#if defined(_MSC_VER)
#define SLIP_RUNTIME_NORETURN __declspec(noreturn)
#elif defined(__GNUC__) || defined(__clang__)
#define SLIP_RUNTIME_NORETURN __attribute__((noreturn))
#else
#define SLIP_RUNTIME_NORETURN
#endif

extern uint16_t SlipRuntime_cleanupCount;
extern SlipRuntimeCleanup SlipRuntime_cleanupCallbacks[40];
extern uint8_t SlipRuntime_active;
extern uint32_t SlipRuntime_error;

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
