#ifndef SLIPSTREAM5000_INPUT_BIOS_HOST_H
#define SLIPSTREAM5000_INPUT_BIOS_HOST_H
#include "input.h"

/* Host representation of the reference PC BIOS 001e..003e keyboard ring.
 * Entries are BIOS character/scan pairs, not SDL key symbols. */
enum { SLIP_BIOS_KEY_SLOTS = 16 };

typedef struct SlipInputBiosHostQueue {
	SlipInputBiosKey keys[SLIP_BIOS_KEY_SLOTS];
	unsigned head, tail;
	uint8_t altDigits; /* BIOS keyboard token */
} SlipInputBiosHostQueue;

bool SlipInputBiosHost_Push(SlipInputBiosHostQueue *, SlipInputBiosKey);
bool SlipInputBiosHost_Available(void *);

SlipInputBiosKey SlipInputBiosHost_ReadReady(void *);
void SlipInputBiosHost_Clear(void *);

typedef struct SlipInputBiosModifiers {
	bool shift, control, alt, capsLock, numLock;
} SlipInputBiosModifiers;

typedef struct SlipInputBiosModifierState {
	bool leftShift, rightShift, leftControl, rightControl, leftAlt, rightAlt;
	bool capsLock, numLock;
} SlipInputBiosModifierState;

/* PC enhanced keyboard modifier branch, after the game interceptor accepts the scan. */
SlipInputBiosModifiers SlipInputBiosHost_UpdateModifiers(SlipInputBiosModifierState *, uint8_t scan, bool down,
                                                         bool extended);
/* Normal-key make codes only; other BIOS keyboard branches are separate. */
SlipInputBiosKey SlipInputBiosHost_TranslateNormal(uint8_t scan, SlipInputBiosModifiers, bool extended);
/* Navigation make-code branch: scans 47..53 except keypad +/-.
 * The BIOS caller handles Ctrl-Alt-Delete and Insert flags before this call. */
SlipInputBiosKey SlipInputBiosHost_TranslateNavigation(uint8_t scan, SlipInputBiosModifiers, bool extended,
                                                       uint8_t *altDigits);
/* Platform key event: modifiers describe BIOS state after this event. */
void SlipInputBiosHost_KeyEvent(SlipInputBiosHostQueue *, uint8_t scan, bool down, bool extended,
                                SlipInputBiosModifiers, bool pressed[256], bool held[256]);
extern const SlipInputBiosCalls SlipInputBiosHost_calls;
void SlipInputBiosHost_ApplyKey(uint8_t scan, bool down, bool extended, bool pressed[256], bool held[256]);
#endif
