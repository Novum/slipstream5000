#ifndef SLIP_RASTER_BACKEND_H
#define SLIP_RASTER_BACKEND_H

#include "raster/raster.h"

/* A complete drawing backend, selected once at a drawing phase boundary. */
typedef struct RasterDrawBackend {
	void (*drawLineSolid)(uint8_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1);
	void (*drawLineClipped)(uint8_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1);
	void (*fillRectClipped)(uint8_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1);
	void (*fillRectUnchecked)(uint16_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1);
	void (*drawSolidFlatPolygon)(uint8_t color, const RasterPoint *points, uint16_t count);
	void (*drawShadedFlatPolygon)(const RasterShadedPoint *points, uint16_t count);
	void (*drawDitheredFlatPolygon)(uint8_t color, uint8_t dither, const RasterPoint *points, uint16_t count);
	void (*drawSpriteScaled)(const uint8_t *record, size_t bytes, const uint8_t *pixels, size_t pixelBytes,
	                         int16_t left, int16_t top, int16_t right, int16_t bottom);
	void (*drawTexturedPolygon)(uint16_t texture, RasterTexturedPoint *points, uint32_t count, bool perspective,
	                            bool opaque);
} RasterDrawBackend;

/* NULL restores the default software backend. */
void Raster_SetDrawBackend(const RasterDrawBackend *backend);

#endif
