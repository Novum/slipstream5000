#ifndef SLIP_SOFTWARE_RASTER_H
#define SLIP_SOFTWARE_RASTER_H
#include "raster_backend.h"
void RasterSoftware_DrawLineSolid(uint8_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1);
void RasterSoftware_DrawLineClipped(uint8_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1);
void RasterSoftware_FillRectClipped(uint8_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1);
void RasterSoftware_FillRectUnchecked(uint16_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1);
void RasterSoftware_DrawSolidFlatPolygon(uint8_t color, const RasterPoint *points, uint16_t count);
void RasterSoftware_DrawShadedFlatPolygon(const RasterShadedPoint *points, uint16_t count);
void RasterSoftware_DrawDitheredFlatPolygon(uint8_t color, uint8_t dither, const RasterPoint *points, uint16_t count);
void RasterSoftware_DrawSpriteScaled(const uint8_t *record, size_t bytes, const uint8_t *pixels, size_t pixelBytes,
                                     int16_t left, int16_t top, int16_t right, int16_t bottom);
void RasterSoftware_DrawTexturedPolygon(uint16_t texture, RasterTexturedPoint *points, uint32_t count, bool perspective,
                                        bool opaque);
extern const RasterDrawBackend RasterSoftware_backend;

#endif
