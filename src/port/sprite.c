#include "sprite.h"
#include "byte_order.h"
#include "random_sequence.h"
#include "raster/overlay.h"
#include "sprite_format.h"

#include "raster/raster.h"
#include "vga_dac.h"

#include <stddef.h>
#include <string.h>

enum { SLIP_SPRITE_DISSOLVE_INITIAL_SEED = 0x5a4a };

int SlipSprite_FromPayload(const SlipResourcePayload *payload, SlipSprite *sprite) {
	uint16_t transparent;
	size_t pixelBytes;

	if (payload == NULL || payload->data == NULL || payload->size < SLIP_SPRITE_HEADER_BYTES) {
		return 0;
	}

	sprite->data = payload->data;
	sprite->pixels = payload->data + SLIP_SPRITE_HEADER_BYTES;
	sprite->width = SlipBytes_ReadLE16(payload->data + SLIP_SPRITE_WIDTH_OFFSET);
	sprite->height = SlipBytes_ReadLE16(payload->data + SLIP_SPRITE_HEIGHT_OFFSET);
	sprite->x = SlipBytes_ReadLEI16(payload->data + SLIP_SPRITE_X_OFFSET);
	sprite->y = SlipBytes_ReadLEI16(payload->data + SLIP_SPRITE_Y_OFFSET);
	transparent = SlipBytes_ReadLE16(payload->data + SLIP_SPRITE_TRANSPARENT_COLOUR_OFFSET);
	sprite->transparentColor = transparent == SLIP_SPRITE_NO_TRANSPARENT_COLOUR ? -1 : (int)(transparent & UINT8_MAX);
	sprite->paletteOffset = SlipBytes_ReadLE16(payload->data + SLIP_SPRITE_PALETTE_OFFSET);

	pixelBytes = (size_t)sprite->width * (size_t)sprite->height;
	return sprite->width != 0 && sprite->height != 0 && payload->size >= SLIP_SPRITE_HEADER_BYTES + pixelBytes;
}

void SlipSprite_ApplyPalette(const SlipSprite *sprite) {
	uint16_t start;
	uint16_t count;
	const uint8_t *p;

	if (sprite->paletteOffset == 0) {
		return;
	}

	p = sprite->data + sprite->paletteOffset;
	start = SlipBytes_ReadLE16(p + SLIP_PALETTE_START_OFFSET);
	count = SlipBytes_ReadLE16(p + SLIP_PALETTE_COUNT_OFFSET);
	p += SLIP_PALETTE_HEADER_BYTES;

	SlipVgaDac_WriteRange(start, count, p);
}

void SlipSprite_Draw(const SlipSprite *sprite, uint8_t *dst, int dstPitch, int x, int y) {
	const uint8_t *const src = sprite->pixels;
	uint16_t row;

	for (row = 0; row < sprite->height; ++row) {
		uint8_t *const out = dst + (y + (int)row) * dstPitch + x;
		uint16_t col;
		for (col = 0; col < sprite->width; ++col) {
			const uint8_t pixel = src[(size_t)row * sprite->width + col];
			if ((int)pixel != sprite->transparentColor) {
				out[col] = pixel;
				RasterOverlay_MarkWritten(out + col, 1);
			}
		}
	}
}

void SlipSprite_DrawDissolve(const SlipSprite *sprite, uint8_t *dst, int dstPitch, int x, int y, uint16_t level) {
	int right, bottom, copyWidth, copyHeight;
	int sourceX = 0, sourceY = 0;
	uint16_t random = SLIP_SPRITE_DISSOLVE_INITIAL_SEED;
	const uint8_t *src;

	if (sprite == NULL || sprite->pixels == NULL || dst == NULL || dstPitch <= 0)
		return;

	if (x == SLIP_SPRITE_USE_STORED_POSITION) {
		x = sprite->x;
		y = sprite->y;
	}

	if (level == UINT16_MAX) {
		SlipSprite_DrawClipped(sprite, dst, dstPitch, x, y);
		return;
	}

	right = (int16_t)(x + sprite->width - 1);
	bottom = (int16_t)(y + sprite->height - 1);
	if (x > g_clipMaxX || right < g_clipMinX || y > g_clipMaxY || bottom < g_clipMinY)
		return;
	copyWidth = sprite->width;
	copyHeight = sprite->height;
	if (x < g_clipMinX) {
		sourceX = g_clipMinX - x;
		copyWidth -= sourceX;
		x = g_clipMinX;
	}
	if (y < g_clipMinY) {
		sourceY = g_clipMinY - y;
		copyHeight -= sourceY;
		y = g_clipMinY;
	}
	if (right > g_clipMaxX)
		copyWidth -= right - g_clipMaxX;
	if (bottom > g_clipMaxY)
		copyHeight -= bottom - g_clipMaxY;

	src = sprite->pixels + (uint16_t)(sourceY * sprite->width + sourceX);
	for (int row = 0; row < copyHeight; ++row) {
		const uint8_t *const sourceRow = src + (size_t)row * sprite->width;
		uint8_t *const out = dst + (y + row) * dstPitch + x;
		for (int col = 0; col < copyWidth; ++col) {

			const uint16_t incremented = (uint16_t)(random + 1u);
			random = (uint16_t)(incremented >> 1);
			if ((incremented & 1u) != 0)
				random ^= SLIP_RANDOM_LFSR_FEEDBACK_MASK;
			if (random < level) {
				out[col] = sourceRow[col];
				RasterOverlay_MarkWritten(out + col, 1);
			}
		}
	}
}

void SlipSprite_DrawClipped(const SlipSprite *sprite, uint8_t *dst, int dstPitch, int x, int y) {
	int right;
	int bottom;
	int sourceX = 0;
	int sourceY = 0;
	int copyWidth;
	int copyHeight;
	int row;

	if (sprite == NULL || sprite->pixels == NULL || dst == NULL || dstPitch <= 0) {
		return;
	}

	if ((uint16_t)x == SLIP_SPRITE_USE_STORED_POSITION) {
		x = sprite->x;
		y = sprite->y;
	}
	right = x + (int)sprite->width - 1;
	bottom = y + (int)sprite->height - 1;
	if (x > g_clipMaxX || right < g_clipMinX || y > g_clipMaxY || bottom < g_clipMinY) {
		return;
	}
	copyWidth = sprite->width;
	copyHeight = sprite->height;
	if (x < g_clipMinX) {
		sourceX = g_clipMinX - x;
		copyWidth -= sourceX;
		x = g_clipMinX;
	}
	if (y < g_clipMinY) {
		sourceY = g_clipMinY - y;
		copyHeight -= sourceY;
		y = g_clipMinY;
	}
	if (right > g_clipMaxX) {
		copyWidth -= right - g_clipMaxX;
	}
	if (bottom > g_clipMaxY) {
		copyHeight -= bottom - g_clipMaxY;
	}
	for (row = 0; row < copyHeight; ++row) {
		const uint8_t *const src = sprite->pixels + (size_t)(sourceY + row) * sprite->width + (size_t)sourceX;
		uint8_t *const out = dst + (y + row) * dstPitch + x;
		int col;

		if (sprite->transparentColor < 0) {
			memcpy(out, src, (size_t)copyWidth);
			RasterOverlay_MarkWritten(out, (size_t)copyWidth);
			continue;
		}
		for (col = 0; col < copyWidth; ++col) {
			if ((int)src[col] != sprite->transparentColor) {
				out[col] = src[col];
				RasterOverlay_MarkWritten(out + col, 1);
			}
		}
	}
}
