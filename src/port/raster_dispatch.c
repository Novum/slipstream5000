#include "raster/software.h"
#include "raster_backend.h"

static const RasterDrawBackend *drawBackend = &RasterSoftware_backend;

void Raster_SetDrawBackend(const RasterDrawBackend *backend) {
	drawBackend = backend ? backend : &RasterSoftware_backend;
}

void Raster_DrawLineSolid(uint8_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
	drawBackend->drawLineSolid(color, x0, y0, x1, y1);
}

void Raster_DrawLineClipped(uint8_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
	drawBackend->drawLineClipped(color, x0, y0, x1, y1);
}

void Raster_FillRectClipped(uint8_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
	drawBackend->fillRectClipped(color, x0, y0, x1, y1);
}

void Raster_FillRectUnchecked(uint16_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
	drawBackend->fillRectUnchecked(color, x0, y0, x1, y1);
}

void Raster_DrawSolidFlatPolygon(uint8_t color, const RasterPoint *points, uint16_t count) {
	drawBackend->drawSolidFlatPolygon(color, points, count);
}

void Raster_DrawShadedFlatPolygon(const RasterShadedPoint *points, uint16_t count) {
	drawBackend->drawShadedFlatPolygon(points, count);
}

void Raster_DrawDitheredFlatPolygon(uint8_t color, uint8_t dither, const RasterPoint *points, uint16_t count) {
	drawBackend->drawDitheredFlatPolygon(color, dither, points, count);
}

void Raster_DrawSpriteScaled(const uint8_t *record, size_t bytes, const uint8_t *pixels, size_t pixelBytes,
                             int16_t left, int16_t top, int16_t right, int16_t bottom) {
	drawBackend->drawSpriteScaled(record, bytes, pixels, pixelBytes, left, top, right, bottom);
}

void Raster_DrawTexturedPolygon(uint16_t texture, RasterTexturedPoint *points, uint32_t count, bool perspective,
                                bool opaque) {
	drawBackend->drawTexturedPolygon(texture, points, count, perspective, opaque);
}
