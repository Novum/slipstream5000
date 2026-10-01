#ifndef SLIPSTREAM5000_SPRITE_H
#define SLIPSTREAM5000_SPRITE_H

#include "resource.h"

#include <stdint.h>

typedef struct SlipSprite {
	const uint8_t *data;
	/* Pixel storage is separate from the serialized header for generated sprites. */
	const uint8_t *pixels;
	uint16_t width;
	uint16_t height;
	int16_t x;
	int16_t y;
	int transparentColor;
	uint16_t paletteOffset;
} SlipSprite;

int SlipSprite_FromPayload(const SlipResourcePayload *payload, SlipSprite *sprite);
void SlipSprite_ApplyPalette(const SlipSprite *sprite);
void SlipSprite_Draw(const SlipSprite *sprite, uint8_t *dst, int dstPitch, int x, int y);
void SlipSprite_DrawClipped(const SlipSprite *sprite, uint8_t *dst, int dstPitch, int x, int y);
void SlipSprite_DrawDissolve(const SlipSprite *sprite, uint8_t *dst, int dstPitch, int x, int y, uint16_t level);

#endif
