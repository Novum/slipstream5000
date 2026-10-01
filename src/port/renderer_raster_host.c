#include "renderer_host.h"

enum { RASTER_POINT_CAPACITY = 32 };

void SlipRendererHost_DrawLine(void *context, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t colour) {
	(void)context;
	Raster_DrawLineSolid((uint8_t)colour, (int16_t)x1, (int16_t)y1, (int16_t)x2, (int16_t)y2);
}

void SlipRendererHost_DrawRectangle(void *context, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2,
                                    uint32_t colour) {
	(void)context;
	Raster_FillRectUnchecked((uint16_t)colour, (int16_t)x1, (int16_t)y1, (int16_t)x2, (int16_t)y2);
}

void SlipRendererHost_DrawFlat(void *context, RasterTexturedPoint *points, uint32_t count, uint32_t colour) {
	(void)context;
	RasterPoint coordinates[RASTER_POINT_CAPACITY];
	for (uint32_t i = 0; i < count; ++i)
		coordinates[i] = (RasterPoint){points[i].x, points[i].y};
	Raster_DrawSolidFlatPolygon((uint8_t)colour, coordinates, (uint16_t)count);
}

void SlipRendererHost_DrawShaded(void *context, RasterTexturedPoint *points, uint32_t count) {
	(void)context;
	RasterShadedPoint coordinates[RASTER_POINT_CAPACITY];
	for (uint32_t i = 0; i < count; ++i)
		coordinates[i] = (RasterShadedPoint){points[i].x, points[i].y, (uint16_t)points[i].reserved08};
	Raster_DrawShadedFlatPolygon(coordinates, (uint16_t)count);
}

void SlipRendererHost_DrawDithered(void *context, RasterTexturedPoint *points, uint32_t count, uint32_t colour,
                                   uint32_t dither) {
	(void)context;
	RasterPoint coordinates[RASTER_POINT_CAPACITY];
	for (uint32_t i = 0; i < count; ++i)
		coordinates[i] = (RasterPoint){points[i].x, points[i].y};
	Raster_DrawDitheredFlatPolygon((uint8_t)colour, (uint8_t)dither, coordinates, (uint16_t)count);
}

const SlipRendererRasterCalls SlipRendererHost_flatRasterCalls = {.line = SlipRendererHost_DrawLine,
                                                                  .rectangle = SlipRendererHost_DrawRectangle,
                                                                  .flat = SlipRendererHost_DrawFlat,
                                                                  .dithered = SlipRendererHost_DrawDithered,
                                                                  .shaded = SlipRendererHost_DrawShaded};
