#include "texture_edges.h"

enum { EDGE_FRACTION_BITS = 16, EDGE_HALF_PIXEL = 1u << (EDGE_FRACTION_BITS - 1) };

bool Raster_StepAffineLeft(RasterTextureEdge *edge, const RasterTexturedPoint *begin, const RasterTexturedPoint *end,
                           int32_t scanline, int32_t bottom) {
	for (;;) {
		edge->x = edge->point->x;
		edge->u = edge->point->scaledU;
		edge->v = edge->point->scaledV;
		if (scanline == bottom)
			return false;
		const RasterTexturedPoint *const previous = edge->point;
		if (edge->point == begin)
			edge->point = end;
		--edge->point;
		if (scanline > edge->point->y) {
			edge->point = previous;
			return true;
		}
		const int32_t nextX = edge->point->x;
		if (scanline == edge->point->y)
			continue;
		const int32_t height = (int32_t)((uint32_t)edge->point->y - (uint32_t)scanline);
		edge->remainingScanlines = (uint16_t)height;
		edge->uStep = (int32_t)(edge->point->scaledU - edge->u) / height;
		edge->vStep = (int32_t)(edge->point->scaledV - edge->v) / height;
		const int32_t xDifference = (int32_t)(((uint32_t)nextX - (uint32_t)edge->x) << EDGE_FRACTION_BITS);
		edge->xStep = xDifference / height;
		edge->xFraction = EDGE_HALF_PIXEL;
		return false;
	}
}

bool Raster_StepAffineRight(RasterTextureEdge *edge, const RasterTexturedPoint *begin, const RasterTexturedPoint *end,
                            int32_t scanline, int32_t bottom) {
	for (;;) {
		edge->x = edge->point->x;
		edge->u = edge->point->scaledU;
		edge->v = edge->point->scaledV;
		if (scanline == bottom)
			return false;
		const RasterTexturedPoint *const previous = edge->point;
		++edge->point;
		if (edge->point == end)
			edge->point = begin;
		if (scanline > edge->point->y) {
			edge->point = previous;
			return true;
		}
		const int32_t nextX = edge->point->x;
		if (scanline == edge->point->y)
			continue;
		const int32_t height = (int32_t)((uint32_t)edge->point->y - (uint32_t)scanline);
		edge->remainingScanlines = (uint16_t)height;
		edge->uStep = (int32_t)(edge->point->scaledU - edge->u) / height;
		edge->vStep = (int32_t)(edge->point->scaledV - edge->v) / height;
		const int32_t xDifference = (int32_t)(((uint32_t)nextX - (uint32_t)edge->x) << EDGE_FRACTION_BITS);
		edge->xStep = xDifference / height;
		edge->xFraction = EDGE_HALF_PIXEL;
		return false;
	}
}

bool Raster_StepPerspectiveLeft(RasterPerspectiveEdge *edge, const RasterTexturedPoint *begin,
                                const RasterTexturedPoint *end, int32_t scanline, int32_t bottom) {
	for (;;) {
		edge->x = edge->point->x;
		edge->depth = (uint32_t)edge->point->depth;
		edge->u = edge->point->scaledU;
		edge->v = edge->point->scaledV;
		if (scanline == bottom)
			return false;
		const RasterTexturedPoint *const previous = edge->point;
		if (edge->point == begin)
			edge->point = end;
		--edge->point;
		if (scanline > edge->point->y) {
			edge->point = previous;
			return true;
		}
		const int32_t nextX = edge->point->x;
		if (scanline == edge->point->y)
			continue;
		const uint32_t height = (uint32_t)edge->point->y - (uint32_t)scanline;
		edge->remainingScanlines = (uint16_t)height;
		edge->startDepthSum = 0;
		edge->endDepthSum = 0;
		edge->startDepthStep = edge->depth / height;
		edge->endDepthStep = (uint32_t)edge->point->depth / height;
		const int32_t xDifference = (int32_t)(((uint32_t)nextX - (uint32_t)edge->x) << EDGE_FRACTION_BITS);
		edge->xStep = xDifference / (int32_t)height;
		edge->xFraction = EDGE_HALF_PIXEL;
		return false;
	}
}

bool Raster_StepPerspectiveRight(RasterPerspectiveEdge *edge, const RasterTexturedPoint *begin,
                                 const RasterTexturedPoint *end, int32_t scanline, int32_t bottom) {
	for (;;) {
		edge->x = edge->point->x;
		edge->depth = (uint32_t)edge->point->depth;
		edge->u = edge->point->scaledU;
		edge->v = edge->point->scaledV;
		if (scanline == bottom)
			return false;
		const RasterTexturedPoint *const previous = edge->point;
		++edge->point;
		if (edge->point == end)
			edge->point = begin;
		if (scanline > edge->point->y) {
			edge->point = previous;
			return true;
		}
		const int32_t nextX = edge->point->x;
		if (scanline == edge->point->y)
			continue;
		const uint32_t height = (uint32_t)edge->point->y - (uint32_t)scanline;
		edge->remainingScanlines = (uint16_t)height;
		edge->startDepthSum = 0;
		edge->endDepthSum = 0;
		edge->startDepthStep = edge->depth / height;
		edge->endDepthStep = (uint32_t)edge->point->depth / height;
		const int32_t xDifference = (int32_t)(((uint32_t)nextX - (uint32_t)edge->x) << EDGE_FRACTION_BITS);
		edge->xStep = xDifference / (int32_t)height;
		edge->xFraction = EDGE_HALF_PIXEL;
		return false;
	}
}
