#ifndef SLIPSTREAM5000_SOFTWARE_CURSOR_PIXELS_H
#define SLIPSTREAM5000_SOFTWARE_CURSOR_PIXELS_H
#include "software_cursor.h"
#include <stdbool.h>

typedef struct SlipCursorSprite {
	uint16_t width, height, transparentColor;
	int16_t hotspotX, hotspotY;
	const uint8_t *pixels;
} SlipCursorSprite;

typedef struct SlipCursorBackground {
	uint16_t width, height;
	int16_t x, y;
	uint16_t transparentColor;
	uint8_t *pixels;
} SlipCursorBackground;

typedef struct SlipCursorPixels {
	const SlipCursorSprite *sprite;
	SlipCursorBackground background;
	uint16_t restoredWidth;
	uint16_t sourceX, sourceY, drawWidth, drawHeight;
	uint16_t captureWidth, captureHeight;
	int16_t clipLeft, clipTop, clipRight, clipBottom;
	const uint32_t *rowOffsets;
} SlipCursorPixels;

void SlipCursor_Draw(SlipCursorPixels *, const SlipSoftwareCursor *, uint8_t *displayPage);
void SlipCursor_SaveBackground(SlipCursorPixels *, const SlipSoftwareCursor *, const uint8_t *displayPage);
void SlipCursor_RestoreBackground(SlipCursorPixels *, uint8_t *displayPage);
void SlipCursor_RestoreWidth(SlipCursorPixels *);

void SlipCursor_SelectSprite(SlipCursorPixels *, bool useDefault, const SlipCursorSprite *custom);
extern const SlipCursorSprite SlipCursor_default;
#endif
