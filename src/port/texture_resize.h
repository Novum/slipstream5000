#ifndef SLIPSTREAM5000_TEXTURE_RESIZE_H
#define SLIPSTREAM5000_TEXTURE_RESIZE_H
#include <stdbool.h>
#include <stdint.h>

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
