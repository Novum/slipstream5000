#include "overlay.h"
#include "raster/software.h"
#include <string.h>

static uint8_t *overlayPixels;
static uint8_t *coveragePixels;

static bool BeginCoverage(RasterSurfaceBinding *saved) {
	if (g_screenBufferBase != overlayPixels)
		return false;
	Raster_BindSprite(coveragePixels, SLIPSTREAM_SCREEN_WIDTH, SLIPSTREAM_SCREEN_HEIGHT, saved);
	Raster_SetClipRect(saved->minX, saved->minY, saved->maxX, saved->maxY);
	return true;
}

static void DrawLineSolid(uint8_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
	RasterSoftware_DrawLineSolid(color, x0, y0, x1, y1);
	RasterSurfaceBinding saved;
	if (BeginCoverage(&saved)) {
		RasterSoftware_DrawLineSolid(1, x0, y0, x1, y1);
		Raster_RestoreScreen(&saved);
	}
}

static void DrawLineClipped(uint8_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
	RasterSoftware_DrawLineClipped(color, x0, y0, x1, y1);
	RasterSurfaceBinding saved;
	if (BeginCoverage(&saved)) {
		RasterSoftware_DrawLineClipped(1, x0, y0, x1, y1);
		Raster_RestoreScreen(&saved);
	}
}

static void FillRectClipped(uint8_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
	RasterSoftware_FillRectClipped(color, x0, y0, x1, y1);
	RasterSurfaceBinding saved;
	if (BeginCoverage(&saved)) {
		RasterSoftware_FillRectClipped(1, x0, y0, x1, y1);
		Raster_RestoreScreen(&saved);
	}
}

static void FillRectUnchecked(uint16_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
	RasterSoftware_FillRectUnchecked(color, x0, y0, x1, y1);
	RasterSurfaceBinding saved;
	if (BeginCoverage(&saved)) {
		RasterSoftware_FillRectUnchecked((color & 0x8000u) | 1u, x0, y0, x1, y1);
		Raster_RestoreScreen(&saved);
	}
}

static void DrawSolidFlatPolygon(uint8_t color, const RasterPoint *points, uint16_t count) {
	RasterSoftware_DrawSolidFlatPolygon(color, points, count);
	RasterSurfaceBinding saved;
	if (BeginCoverage(&saved)) {
		RasterSoftware_DrawSolidFlatPolygon(1, points, count);
		Raster_RestoreScreen(&saved);
	}
}

static const RasterDrawBackend overlayBackend = {
    .drawLineSolid = DrawLineSolid,
    .drawLineClipped = DrawLineClipped,
    .fillRectClipped = FillRectClipped,
    .fillRectUnchecked = FillRectUnchecked,
    .drawSolidFlatPolygon = DrawSolidFlatPolygon,
    .drawShadedFlatPolygon = RasterSoftware_DrawShadedFlatPolygon,
    .drawDitheredFlatPolygon = RasterSoftware_DrawDitheredFlatPolygon,
    .drawSpriteScaled = RasterSoftware_DrawSpriteScaled,
    .drawTexturedPolygon = RasterSoftware_DrawTexturedPolygon,
};

void RasterOverlay_Begin(uint8_t *pixels, uint8_t *coverage) {
	overlayPixels = pixels;
	coveragePixels = coverage;
	Raster_SetDrawBackend(&overlayBackend);
}

void RasterOverlay_End(void) {
	overlayPixels = coveragePixels = NULL;
	Raster_SetDrawBackend(NULL);
}

void RasterOverlay_MarkWritten(const uint8_t *pixels, size_t count) {
	if (!coveragePixels || (uintptr_t)pixels < (uintptr_t)overlayPixels)
		return;
	size_t offset = (uintptr_t)pixels - (uintptr_t)overlayPixels;
	const size_t size = SLIPSTREAM_SCREEN_WIDTH * SLIPSTREAM_SCREEN_HEIGHT;
	if (offset < size && count <= size - offset)
		memset(coveragePixels + offset, 1, count);
}
