#ifndef SLIPSTREAM5000_RASTER_TEXTURE_EDGES_H
#define SLIPSTREAM5000_RASTER_TEXTURE_EDGES_H
#include "raster.h"

typedef struct RasterTextureEdge {
	const RasterTexturedPoint *point;
	int32_t x;
	uint32_t u, v;
	int32_t uStep, vStep, xStep;
	uint16_t xFraction, remainingScanlines;
} RasterTextureEdge;

bool Raster_StepAffineLeft(RasterTextureEdge *, const RasterTexturedPoint *begin, const RasterTexturedPoint *end,
                           int32_t scanline, int32_t bottom);
bool Raster_StepAffineRight(RasterTextureEdge *, const RasterTexturedPoint *begin, const RasterTexturedPoint *end,
                            int32_t scanline, int32_t bottom);

/* Perspective edges keep both endpoint depths and their per-row accumulators. */
typedef struct RasterPerspectiveEdge {
	const RasterTexturedPoint *point;
	int32_t x, xStep;
	uint16_t xFraction, remainingScanlines;
	uint32_t depth, u, v;
	uint32_t startDepthStep, startDepthSum, endDepthStep, endDepthSum;
} RasterPerspectiveEdge;

bool Raster_StepPerspectiveLeft(RasterPerspectiveEdge *, const RasterTexturedPoint *begin,
                                const RasterTexturedPoint *end, int32_t scanline, int32_t bottom);
bool Raster_StepPerspectiveRight(RasterPerspectiveEdge *, const RasterTexturedPoint *begin,
                                 const RasterTexturedPoint *end, int32_t scanline, int32_t bottom);
#endif
