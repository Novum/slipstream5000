#ifndef SLIPSTREAM5000_GAME_TIMER_H
#define SLIPSTREAM5000_GAME_TIMER_H
#include <stdint.h>

enum { SLIP_GAME_TIMER_SLOT_COUNT = 5 };
typedef struct SlipGameTimerState SlipGameTimerState;
typedef void (*SlipGameTimerCallback)(SlipGameTimerState *);

struct SlipGameTimerState {
	/* Native context for the game's music hook. */
	struct SlipGameSoundState *gameSound;
	struct HmiTimerState *musicTimer;
	uint16_t rate;
	uint16_t divisor, savedDivisor;
	uint32_t missedInterrupts, interruptReentryGuard;
	/* Fields in the relocated real-mode helper at +0/+8. */
	uint32_t biosCountdown;
	uint16_t realModeMissed;
	uint16_t realModeDivisor;
	SlipGameTimerCallback savedRealVector;
	uint8_t picEndOfInterruptCommand;
	SlipGameTimerCallback savedVector;

	struct {
		SlipGameTimerCallback callback;
		uint32_t countdown;
		uint16_t rate, divisor;
	} slots[SLIP_GAME_TIMER_SLOT_COUNT];
};

uint32_t SlipGameTimer_Register(SlipGameTimerState *, uint16_t, SlipGameTimerCallback);
void SlipGameTimer_Remove(SlipGameTimerState *, SlipGameTimerCallback);
void SlipGameTimer_Interrupt(SlipGameTimerState *);
void SlipGameTimer_RealInterrupt(SlipGameTimerState *);
#endif
