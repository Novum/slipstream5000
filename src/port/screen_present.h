#ifndef SLIPSTREAM5000_SCREEN_PRESENT_H
#define SLIPSTREAM5000_SCREEN_PRESENT_H
#include "software_cursor_pixels.h"

typedef struct SlipScreenPresentCalls {
	void *context;
	void (*waitRetrace)(void *);
	void (*commitPalette)(void *);
} SlipScreenPresentCalls;

void SlipScreen_CopyChanged(const uint8_t *current, const uint8_t *previous, uint8_t *display);
void SlipScreen_Present(uint8_t **drawPage, uint8_t **previousPage, uint8_t *display, SlipSoftwareCursor *,
                        SlipCursorPixels *, const SlipScreenPresentCalls *);
void SlipScreen_PresentSingle(uint8_t *drawPage, uint8_t *display, SlipSoftwareCursor *, SlipCursorPixels *,
                              const SlipScreenPresentCalls *);
#endif
