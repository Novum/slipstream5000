#include "draw.h"
#include "raster/affine.h"
#include "renderer.h"
#include "resource_host.h"

static void RasterGpu_DrawLineSolid(uint8_t c, int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
	SlipRaceGpu_Line(c, x0, y0, x1, y1);
}

static void RasterGpu_FillRectUnchecked(uint16_t c, int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
	if (c & 0x8000u) {
		SlipRaceGpu_Line((uint8_t)c, x0, y0, x1, y0);
		SlipRaceGpu_Line((uint8_t)c, x1, y0, x1, y1);
		SlipRaceGpu_Line((uint8_t)c, x1, y1, x0, y1);
		SlipRaceGpu_Line((uint8_t)c, x0, y1, x0, y0);
		return;
	}
	RasterPoint p[] = {{x0, y0}, {x1 + 1, y0}, {x1 + 1, y1 + 1}, {x0, y1 + 1}};
	SlipRaceGpu_Flat(p, 4, (uint8_t)c, 0);
}

static void RasterGpu_FillRectClipped(uint8_t c, int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
	RasterGpu_FillRectUnchecked(c, x0, y0, x1, y1);
}

static void RasterGpu_DrawSolidFlatPolygon(uint8_t c, const RasterPoint *p, uint16_t n) {
	SlipRaceGpu_Flat(p, n, c, 0);
}

static void RasterGpu_DrawShadedFlatPolygon(const RasterShadedPoint *p, uint16_t n) { SlipRaceGpu_Shaded(p, n); }

static void RasterGpu_DrawDitheredFlatPolygon(uint8_t c, uint8_t d, const RasterPoint *p, uint16_t n) {
	SlipRaceGpu_Flat(p, n, c, d);
}

static void RasterGpu_DrawSpriteScaled(const uint8_t *record, size_t bytes, const uint8_t *pixels, size_t pixelBytes,
                                       int16_t left, int16_t top, int16_t right, int16_t bottom) {
	(void)pixels;
	(void)pixelBytes;
	SlipRaceGpu_Sprite(record, bytes, left, top, right, bottom);
}

static void DrawTexturedPolygon(uint16_t texture, RasterTexturedPoint *points, uint32_t count, bool perspective,
                                bool opaque) {
	(void)perspective;
	(void)opaque;
	SlipResourceHost_Lock(NULL, texture);
	const SlipResourcePayload payload = SlipResourceHost_Payload(texture);
	SlipRaceGpu_Texture(payload.data, payload.size, Raster_textureRowScroll, points, count);
	SlipResourceHost_Unlock(NULL, texture);
}

const RasterDrawBackend RasterGpu_backend = {
    .drawLineSolid = RasterGpu_DrawLineSolid,
    .drawLineClipped = RasterGpu_DrawLineSolid,
    .fillRectClipped = RasterGpu_FillRectClipped,
    .fillRectUnchecked = RasterGpu_FillRectUnchecked,
    .drawSolidFlatPolygon = RasterGpu_DrawSolidFlatPolygon,
    .drawShadedFlatPolygon = RasterGpu_DrawShadedFlatPolygon,
    .drawDitheredFlatPolygon = RasterGpu_DrawDitheredFlatPolygon,
    .drawSpriteScaled = RasterGpu_DrawSpriteScaled,
    .drawTexturedPolygon = DrawTexturedPolygon,
};
