#ifndef SLIPSTREAM5000_HMI_TIMER_H
#define SLIPSTREAM5000_HMI_TIMER_H

#include <setjmp.h>
#include <stdint.h>

/* Timer slot layout, PIT/PIC commands, and Q16 callback-rate scaling.
 * SYSTEM_RATE is the middleware sentinel; SYSTEM_RATE_Q16 is the BIOS
 * callback rate used when the PIT divisor changes. */
enum {
	HMI_TIMER_SLOT_COUNT = 16,
	HMI_TIMER_SYSTEM_SLOT = HMI_TIMER_SLOT_COUNT - 1,
	HMI_TIMER_PIT_CLOCK_HZ = 0x1234dc,
	HMI_TIMER_SYSTEM_RATE_Q16 = 0x123333,
	HMI_TIMER_SYSTEM_RATE = 0xff00,
	HMI_TIMER_MAXIMUM_DIVISOR = 0xffff,
	HMI_TIMER_FRACTION_BITS = 16,
	HMI_TIMER_FRACTION_ONE = 1 << HMI_TIMER_FRACTION_BITS,
	HMI_TIMER_HIGH_BYTES_MASK = 0xffffff00u,
	HMI_TIMER_NO_SONG = 0xff,
	HMI_TIMER_IRQ0_MASK = 1,
	HMI_TIMER_IRQ0_CLEAR_MASK = 0xfe,
	HMI_TIMER_PIT_CHANNEL0_SQUARE_WAVE = 0x36,
	HMI_TIMER_PIC_END_OF_INTERRUPT = 0x20,
	HMI_TIMER_USE_DPMI = 2,
	HMI_TIMER_SKIP_HARDWARE = 1,
	HMI_TIMER_ERROR_INVALID_HANDLE = 10,
	HMI_TIMER_ERROR_NO_HANDLES = 11
};

typedef struct HmiTimerState HmiTimerState;
typedef void (*HmiTimerCallback)(HmiTimerState *);

struct HmiTimerState {
	uint32_t divisor;
	HmiTimerCallback callbacks[HMI_TIMER_SLOT_COUNT];
	uint32_t rates[HMI_TIMER_SLOT_COUNT];
	uint32_t increments[HMI_TIMER_SLOT_COUNT];
	uint32_t accumulators[HMI_TIMER_SLOT_COUNT];
	uint8_t songForSlot[HMI_TIMER_SLOT_COUNT];
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
