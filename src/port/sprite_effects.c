#include "sprite_effects.h"
#include "byte_order.h"
#include "runtime.h"

SlipSpriteZoomState SlipSprite_zoom;
int16_t SlipPalette_blendAmount;

/* Byte access here is to serialized SPR and palette payloads, never native
 * structs. Native resource locks provide resident file bytes. */

SlipSpriteBounds SlipSprite_Bounds(uint16_t resource, const SlipSpriteEffectResources *resources) {
	const uint8_t *const sprite = resources->lock(resources->context, resource);
	SlipSpriteBounds bounds;
	bounds.left = (int16_t)SlipBytes_ReadLE16(sprite + 4);
	bounds.top = (int16_t)SlipBytes_ReadLE16(sprite + 6);
	bounds.right = (int16_t)(bounds.left + SlipBytes_ReadLE16(sprite) - 1);
	bounds.bottom = (int16_t)(bounds.top + SlipBytes_ReadLE16(sprite + 2) - 1);
	resources->unlock(resources->context, resource);
	return bounds;
}

SlipSpriteDimensions SlipSprite_Dimensions(uint16_t resource, const SlipSpriteEffectResources *resources) {
	const uint8_t *const sprite = resources->lock(resources->context, resource);
	SlipSpriteDimensions dimensions = {SlipBytes_ReadLE16(sprite), SlipBytes_ReadLE16(sprite + 2)};
	resources->unlock(resources->context, resource);
	return dimensions;
}

void SlipSprite_Grayscale(uint16_t resource, const uint8_t *externalPalette,
                          const SlipSpriteEffectResources *resources) {
	uint8_t *const sprite = resources->lock(resources->context, resource);
	const uint8_t *palette = externalPalette;
	uint16_t paletteOffset = SlipBytes_ReadLE16(sprite + 14);
	if (paletteOffset != 0)
		palette = sprite + paletteOffset;
	uint32_t remaining = (uint16_t)(SlipBytes_ReadLE16(sprite) * SlipBytes_ReadLE16(sprite + 2));
	uint8_t *pixel = sprite + 16;
	do {
		uint16_t value = *pixel;
		const uint16_t first = SlipBytes_ReadLE16(palette);
		if ((int16_t)value < (int16_t)first)
			SlipRuntime_Fatal("SpriteGreyscale: Pixel not in palette range.");
		value = (uint16_t)(value - first);
		if ((int16_t)value >= (int16_t)SlipBytes_ReadLE16(palette + 2))
			SlipRuntime_Fatal("SpriteGreyscale: Pixel not in palette range.");
		const uint8_t *const rgb = palette + 4 + (uint32_t)value * 3;
		const uint16_t sum = (uint16_t)(rgb[0] + rgb[1] + rgb[2]);
		*pixel++ = (uint8_t)(sum / 3);
	} while (--remaining != 0);
	paletteOffset = SlipBytes_ReadLE16(sprite + 14);
	if (paletteOffset != 0) {
		uint8_t *embedded = sprite + paletteOffset;
		remaining = SlipBytes_ReadLE16(embedded + 2);
		if (remaining > 0x40)
			remaining = 0x40;
		embedded[0] = embedded[1] = 0;
		embedded[2] = (uint8_t)remaining;
		embedded[3] = (uint8_t)(remaining >> 8);
		uint8_t gray = 0;
		embedded += 4;
		do {
			embedded[0] = embedded[1] = embedded[2] = gray;
			embedded += 3;
			++gray;
		} while (--remaining != 0);
	}
	resources->unlock(resources->context, resource);
}

uint16_t SlipPalette_Blend(const uint8_t *first, const uint8_t *second, int16_t amount,
                           const SlipSpriteEffectResources *resources) {
	SlipPalette_blendAmount = amount;
	const uint16_t count = SlipBytes_ReadLE16(second + 2);
	if (count != SlipBytes_ReadLE16(first + 2))
		SlipRuntime_Fatal("MakeFadePalette: Range error.");
	uint32_t remaining = (uint32_t)count * 3;
	uint16_t resource;
	if (!resources->allocate(resources->context, remaining + 4, 0, &resource))
		resources->allocationError(resources->context);
	uint8_t *destination = resources->lock(resources->context, resource);
	const uint16_t start = SlipBytes_ReadLE16(second);
	if (start != SlipBytes_ReadLE16(first))
		SlipRuntime_Fatal("MakeFadePalette: Range error.");
	destination[0] = (uint8_t)start;
	destination[1] = (uint8_t)(start >> 8);
	destination[2] = (uint8_t)count;
	destination[3] = (uint8_t)(count >> 8);
	destination += 4;
	first += 4;
	second += 4;
	do {
		const int16_t difference = (int8_t)(uint8_t)(*second - *first);
		const int32_t product = difference * SlipPalette_blendAmount;
		*destination++ = (uint8_t)(*first + (product >> 14));
		++first;
		++second;
	} while (--remaining != 0);
	resources->unlock(resources->context, resource);
	return resource;
}

void SlipSprite_Zoom(uint16_t resource, int16_t scale, int16_t centerX, int16_t centerY, int16_t targetX,
                     int16_t targetY, const SlipSpriteEffectResources *resources) {
	SlipSprite_zoom.scaleQ14 = scale;
	const uint8_t *const sprite = resources->lock(resources->context, resource);
	SlipSprite_zoom.originBounds[0] = SlipSprite_zoom.originBounds[2] = centerX;
	SlipSprite_zoom.originBounds[1] = SlipSprite_zoom.originBounds[3] = centerY;
	SlipSprite_zoom.targetBounds[0] = targetX;
	SlipSprite_zoom.targetBounds[1] = targetY;
	SlipSprite_zoom.targetBounds[2] = (int16_t)(targetX + SlipBytes_ReadLE16(sprite) - 1);
	SlipSprite_zoom.targetBounds[3] = (int16_t)(targetY + SlipBytes_ReadLE16(sprite + 2) - 1);
	for (unsigned i = 0; i < 4; ++i) {
		const int16_t difference = (int16_t)(SlipSprite_zoom.targetBounds[i] - SlipSprite_zoom.originBounds[i]);
		const int32_t product = difference * SlipSprite_zoom.scaleQ14;
		SlipSprite_zoom.scaledBounds[i] = (int16_t)((product >> 14) + SlipSprite_zoom.originBounds[i]);
	}
	resources->unlock(resources->context, resource);
	resources->drawScaled(resources->context, resource, SlipSprite_zoom.scaledBounds[0],
	                      SlipSprite_zoom.scaledBounds[1], SlipSprite_zoom.scaledBounds[2],
	                      SlipSprite_zoom.scaledBounds[3]);
}
