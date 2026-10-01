#ifndef SLIPSTREAM5000_SOFTWARE_CURSOR_H
#define SLIPSTREAM5000_SOFTWARE_CURSOR_H
#include <stdint.h>

typedef struct SlipSoftwareCursor {
	uint8_t visible, updatesSuspended;
	int16_t x, y;
} SlipSoftwareCursor;

typedef struct SlipSoftwareCursorCalls {
	void *context;
	uint8_t *displayPage;
	void (*saveBackground)(void *, uint8_t *);
	void (*draw)(void *, uint8_t *);
	void (*restoreBackground)(void *, uint8_t *);
} SlipSoftwareCursorCalls;

void SlipSoftwareCursor_Show(SlipSoftwareCursor *, const SlipSoftwareCursorCalls *);
void SlipSoftwareCursor_Hide(SlipSoftwareCursor *, const SlipSoftwareCursorCalls *);
void SlipSoftwareCursor_Move(SlipSoftwareCursor *, int16_t x, int16_t y, const SlipSoftwareCursorCalls *);
#endif
