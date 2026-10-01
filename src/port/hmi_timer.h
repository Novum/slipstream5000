#ifndef SLIPSTREAM5000_HMI_TIMER_H
#define SLIPSTREAM5000_HMI_TIMER_H

#include <setjmp.h>
#include <stdint.h>

typedef struct HmiTimerState HmiTimerState;
typedef void (*HmiTimerCallback)(HmiTimerState *);

struct HmiTimerState {
	uint32_t divisor;
	HmiTimerCallback callbacks[16];
	uint32_t rates[16];
	uint32_t increments[16];
	uint32_t accumulators[16];
	uint8_t songForSlot[16];
	uint8_t currentSong;
	uint32_t dispatchDepth;
	uint32_t currentSlot;
	HmiTimerCallback interruptCallback;
	HmiTimerCallback savedVector;
	uint16_t interruptActive;
	uint32_t hardwareEnabled;
	uint32_t dpmiMode;
	HmiTimerCallback protectedTimerVector;
	HmiTimerCallback dosTimerVector;
	/* Native equivalent of the saved interrupt stack continuation. */
	jmp_buf *interruptExit;

	struct HmiMusicState *musicState;

	uint8_t picInterruptMask;
	uint8_t picCommand;
	uint8_t pitControl;
	uint16_t pitChannel0Reload;
};

uint32_t HmiTimer_SetRate(HmiTimerState *, uint32_t slot, uint32_t rate);

void HmiTimer_Construct(HmiTimerState *);
uint32_t HmiTimer_Register(HmiTimerState *, uint32_t rate, HmiTimerCallback callback, uint32_t *slot);
uint32_t HmiTimer_Remove(HmiTimerState *, uint32_t slot);
uint32_t HmiTimer_GetRate(const HmiTimerState *, uint32_t slot);
uint32_t HmiTimer_SetDivisor(HmiTimerState *, uint32_t divisor);
void HmiTimer_ProgramPit(HmiTimerState *, uint32_t divisor);
void HmiTimer_Mask(HmiTimerState *);
void HmiTimer_Unmask(HmiTimerState *);
void HmiTimer_Dispatch(HmiTimerState *);
void HmiTimer_Interrupt(HmiTimerState *);
void HmiTimer_Install(HmiTimerState *, uint32_t divisor, HmiTimerCallback);
void HmiTimer_Restore(HmiTimerState *);
void HmiTimer_Chain(HmiTimerState *);
void HmiTimer_SystemTick(HmiTimerState *);
uint32_t HmiTimer_Initialize(HmiTimerState *, uint32_t rate, uint32_t flags);
uint32_t HmiTimer_Shutdown(HmiTimerState *, uint32_t unused);

#endif
