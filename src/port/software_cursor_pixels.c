#include "software_cursor_pixels.h"
#include "raster/overlay.h"
#include "raster/raster.h"
#include <string.h>

static const uint8_t defaultCursorPixels[] = {16, 0, 0, 0,  16, 0,  16, 0,  16, 0, 0, 0, 0,
                                              0,  0, 0, 16, 0,  16, 0,  16, 0,  0, 0, 16};
const SlipCursorSprite SlipCursor_default = {5, 5, 0, 2, 2, defaultCursorPixels};

void SlipCursor_SelectSprite(SlipCursorPixels *state, bool useDefault, const SlipCursorSprite *custom) {
	if (!useDefault)
		state->sprite = custom;
	else
		state->sprite = &SlipCursor_default;
}

void SlipCursor_Draw(SlipCursorPixels *state, const SlipSoftwareCursor *cursor, uint8_t *displayPage) {
	const SlipCursorSprite *const sprite = state->sprite;
	int16_t left = (int16_t)(cursor->x - sprite->hotspotX);
	int16_t top = (int16_t)(cursor->y - sprite->hotspotY);
	const int16_t right = (int16_t)(sprite->width + left - 1);
	const int16_t bottom = (int16_t)(sprite->height + top - 1);
	if (left > state->clipRight || right < state->clipLeft || top > state->clipBottom || bottom < state->clipTop)
		return;
	state->drawWidth = sprite->width;
	state->drawHeight = sprite->height;
	state->sourceX = state->sourceY = 0;
	if (left < state->clipLeft) {
		const uint16_t clipped = (uint16_t)(state->clipLeft - left);
		state->sourceX = (uint16_t)(state->sourceX + clipped);
		state->drawWidth = (uint16_t)(state->drawWidth - clipped);
		left = state->clipLeft;
	}
	if (top < state->clipTop) {
		const uint16_t clipped = (uint16_t)(state->clipTop - top);
		state->sourceY = (uint16_t)(state->sourceY + clipped);
		state->drawHeight = (uint16_t)(state->drawHeight - clipped);
		top = state->clipTop;
	}
	if (right > state->clipRight)
		state->drawWidth = (uint16_t)(state->drawWidth - (uint16_t)(right - state->clipRight));
	if (bottom > state->clipBottom)
		state->drawHeight = (uint16_t)(state->drawHeight - (uint16_t)(bottom - state->clipBottom));

	uint8_t *destination = displayPage + state->rowOffsets[(uint16_t)top] + (uint16_t)left;
	const uint16_t sourceOffset = (uint16_t)((uint32_t)sprite->width * state->sourceY + state->sourceX);
	const uint8_t *source = sprite->pixels + sourceOffset;
	uint16_t remainingRows = state->drawHeight;
	if (sprite->transparentColor == 0xffff) {
		do {
			memcpy(destination, source, state->drawWidth);
			RasterOverlay_MarkWritten(destination, state->drawWidth);
			destination += 320;
			source += sprite->width;
		} while (--remainingRows != 0);
	} else {
		const uint8_t transparent = (uint8_t)sprite->transparentColor;
		do {
			const uint8_t *pixel = source;
			uint8_t *output = destination;
			uint32_t remainingColumns = state->drawWidth;
			do {
				const uint8_t color = *pixel++;
				if (color != transparent) {
					*output = color;
					RasterOverlay_MarkWritten(output, 1);
				}
				++output;
			} while (--remainingColumns != 0);
			destination += 320;
			source += sprite->width;
		} while (--remainingRows != 0);
	}
}

void SlipCursor_RestoreWidth(SlipCursorPixels *state) { state->background.width = state->restoredWidth; }

void SlipCursor_SaveBackground(SlipCursorPixels *state, const SlipSoftwareCursor *cursor, const uint8_t *displayPage) {
	const SlipCursorSprite *const sprite = state->sprite;
	int16_t left = (int16_t)(cursor->x - sprite->hotspotX);
	int16_t top = (int16_t)(cursor->y - sprite->hotspotY);
	state->captureWidth = sprite->width;
	state->captureHeight = sprite->height;
	const int16_t right = (int16_t)(sprite->width + left - 1);
	const int16_t bottom = (int16_t)(sprite->height + top - 1);
	if (left > state->clipRight || right < state->clipLeft || top > state->clipBottom || bottom < state->clipTop) {
		state->background.width = 0;
		return;
	}
	if (left < state->clipLeft) {
		state->captureWidth = (uint16_t)(state->captureWidth - (uint16_t)(state->clipLeft - left));
		left = state->clipLeft;
	}
	if (top < state->clipTop) {
		state->captureHeight = (uint16_t)(state->captureHeight - (uint16_t)(state->clipTop - top));
		top = state->clipTop;
	}
	if (right > state->clipRight)
		state->captureWidth = (uint16_t)(state->captureWidth - (uint16_t)(right - state->clipRight));
	if (bottom > state->clipBottom)
		state->captureHeight = (uint16_t)(state->captureHeight - (uint16_t)(bottom - state->clipBottom));
	state->background.x = left;
	state->background.y = top;
	const uint8_t *source = displayPage + state->rowOffsets[(uint16_t)top] + (uint16_t)left;
	state->background.width = state->captureWidth;
	state->background.transparentColor = 0xffff;
	uint16_t remainingRows = state->captureHeight;
	state->background.height = remainingRows;
	uint8_t *destination = state->background.pixels;
	do {
		memcpy(destination, source, state->captureWidth);
		destination += state->captureWidth;
		source += 320;
	} while (--remainingRows != 0);
}

void SlipCursor_RestoreBackground(SlipCursorPixels *state, uint8_t *displayPage) {
	if (state->background.width != 0) {
		state->restoredWidth = state->background.width;
		uint8_t *destination =
		    displayPage + state->rowOffsets[(uint16_t)state->background.y] + (uint16_t)state->background.x;
		const uint32_t width = state->background.width;
		uint16_t remainingRows = state->background.height;
		state->background.width = 0;
		const uint8_t *source = state->background.pixels;
		do {
			memcpy(destination, source, width);
			source += width;
			destination += 320;
		} while (--remainingRows != 0);
	}
}
