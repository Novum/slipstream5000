#ifndef SLIPSTREAM5000_RASTER_PERSPECTIVE_H
#define SLIPSTREAM5000_RASTER_PERSPECTIVE_H
#include "raster_texture_edges.h"
#include "sprite.h"

typedef struct RasterPerspectiveDrawState {
	const SlipSprite *texture;
	const RasterTexturedPoint *begin, *end;
	int32_t bottom;
	RasterPerspectiveEdge left, right;
	uint32_t leftU, leftV, rightU, rightV, leftDepth, rightDepth;
} RasterPerspectiveDrawState;

typedef struct RasterPerspectiveDrawCalls {
	void *context;
	const SlipSprite *(*lock)(void *, uint16_t resource);
	void (*textureRows)(void *, const SlipSprite *);
	void (*span)(void *, const RasterPerspectiveDrawState *, int32_t scanline);
	void (*unlock)(void *, uint16_t resource);
} RasterPerspectiveDrawCalls;

void Raster_DrawPerspective(RasterPerspectiveDrawState *, uint16_t texture, RasterTexturedPoint *, uint32_t count,
                            const RasterPerspectiveDrawCalls *);

typedef struct RasterOpaquePerspectiveState {
	uint8_t *currentRun, *nextRun;
	int32_t runLength, previousLeft, previousRight;
	uint32_t extensionMask;
	uint8_t *extensionSource, *extensionDestination;
	int32_t extensionLength;
	bool edgeEndsOnNextScanline;
	int16_t nextScanlineLeft, nextScanlineRight;
} RasterOpaquePerspectiveState;

void Raster_DrawOpaquePerspective(RasterPerspectiveDrawState *, RasterOpaquePerspectiveState *, uint16_t texture,
                                  RasterTexturedPoint *, uint32_t count, uint8_t *const *screenRows,
                                  const RasterPerspectiveDrawCalls *);
#endif
