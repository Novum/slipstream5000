#ifndef SLIPSTREAM5000_SPRITE_EFFECTS_H
#define SLIPSTREAM5000_SPRITE_EFFECTS_H
#include <stdbool.h>
#include <stdint.h>

/* Resource/device bindings used by the original SPR/palette routines. */
typedef struct SlipSpriteEffectResources {
	void *context;
	uint8_t *(*lock)(void *, uint16_t);
	void (*unlock)(void *, uint16_t);
	bool (*allocate)(void *, uint32_t bytes, uint32_t flags, uint16_t *);
	void (*allocationError)(void *); /* Does not return. */
	void (*drawScaled)(void *, uint16_t, int16_t left, int16_t top, int16_t right, int16_t bottom);
} SlipSpriteEffectResources;

typedef struct SlipSpriteZoomState {
	int16_t scaleQ14;
	int16_t targetBounds[4], originBounds[4], scaledBounds[4];
} SlipSpriteZoomState;

extern SlipSpriteZoomState SlipSprite_zoom;
extern int16_t SlipPalette_blendAmount;

typedef struct SlipSpriteBounds {
	int16_t left, top, right, bottom;
} SlipSpriteBounds;

typedef struct SlipSpriteDimensions {
	uint16_t width, height;
} SlipSpriteDimensions;

SlipSpriteBounds SlipSprite_Bounds(uint16_t resource, const SlipSpriteEffectResources *resources);
SlipSpriteDimensions SlipSprite_Dimensions(uint16_t resource, const SlipSpriteEffectResources *resources);

void SlipSprite_Grayscale(uint16_t resource, const uint8_t *externalPalette,
                          const SlipSpriteEffectResources *resources);
uint16_t SlipPalette_Blend(const uint8_t *first, const uint8_t *second, int16_t amount,
                           const SlipSpriteEffectResources *resources);
void SlipSprite_Zoom(uint16_t resource, int16_t scale, int16_t centerX, int16_t centerY, int16_t targetX,
                     int16_t targetY, const SlipSpriteEffectResources *resources);
#endif
