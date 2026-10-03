#ifndef SLIPSTREAM5000_TEXTURE_RESIZE_H
#define SLIPSTREAM5000_TEXTURE_RESIZE_H
#include <stdbool.h>
#include <stdint.h>

/* Texture residency scales dimensions in Q30; resampling positions use Q16. */
enum {
	SLIP_TEXTURE_RESIZE_SCALE_FRACTION_BITS = 30,
	SLIP_TEXTURE_RESIZE_SCALE_ONE_Q30 = 1u << SLIP_TEXTURE_RESIZE_SCALE_FRACTION_BITS,
	SLIP_TEXTURE_RESIZE_SCALE_HALF_Q30 = SLIP_TEXTURE_RESIZE_SCALE_ONE_Q30 / 2,
	SLIP_TEXTURE_RESIZE_PIXEL_FRACTION_BITS = 16
};

typedef struct SlipTextureResizeState {
	uint32_t width, height, resizedWidth, resizedHeight, resizedBytes;
	uint32_t scale;
	uint8_t *sprite;
	uint16_t resource;
	uint32_t stepX, stepY;
} SlipTextureResizeState;

typedef struct SlipTextureResizeCalls {
	void *context;
	bool (*resident)(void *, uint16_t);
	bool (*load)(void *, uint16_t);
	uint8_t *(*lock)(void *, uint16_t);
	void (*unlock)(void *, uint16_t);
	bool (*resize)(void *, uint16_t, uint32_t bytes);
} SlipTextureResizeCalls;

bool SlipTexture_Resize(SlipTextureResizeState *, uint16_t resource, uint32_t scale, const SlipTextureResizeCalls *);
extern SlipTextureResizeState SlipTextureHost_resize;
extern const SlipTextureResizeCalls SlipTextureHost_resizeCalls;
#endif
