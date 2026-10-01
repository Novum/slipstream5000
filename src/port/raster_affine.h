#ifndef SLIPSTREAM5000_RASTER_AFFINE_H
#define SLIPSTREAM5000_RASTER_AFFINE_H
#include "raster_texture_edges.h"
#include "sprite.h"
extern uint32_t Raster_textureRowScroll;
void Raster_SetTextureRowScroll(uint16_t scroll);
void Raster_ClearTextureRowScroll(void);

typedef struct RasterAffineDrawState {
	const SlipSprite *texture;
	const RasterTexturedPoint *begin, *end;
	int32_t bottom;
	RasterTextureEdge left, right;
} RasterAffineDrawState;

typedef struct RasterAffineDrawCalls {
	void *context;
	const SlipSprite *(*lock)(void *, uint16_t resource);
	void (*textureRows)(void *, const SlipSprite *);
	void (*span)(void *, const RasterAffineDrawState *, int32_t scanline);
	void (*unlock)(void *, uint16_t resource);
} RasterAffineDrawCalls;

/* Drawing requires at least two points. */
void Raster_DrawAffine(RasterAffineDrawState *, uint16_t texture, RasterTexturedPoint *points, uint32_t count,
                       const RasterAffineDrawCalls *);

typedef struct RasterOpaqueAffineState {
	uint8_t *currentRun, *nextRun;
	int32_t runLength, previousLeft, previousRight;
	uint32_t extensions;
	uint8_t *extensionSource, *extensionDestination;
	int32_t extensionLength;
	bool edgeChanged;
	int16_t predictedLeft, predictedRight;
} RasterOpaqueAffineState;

void Raster_DrawOpaqueAffine(RasterAffineDrawState *, RasterOpaqueAffineState *, uint16_t texture,
                             RasterTexturedPoint *, uint32_t count, uint8_t *const *screenRows,
                             const RasterAffineDrawCalls *);
#endif
