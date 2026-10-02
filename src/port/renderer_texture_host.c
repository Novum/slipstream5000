#include "raster/raster.h"
#include "renderer_host.h"

void SlipRendererHost_DrawAffine(void *context, RasterTexturedPoint *points, uint32_t count, uint32_t texture) {
	(void)context;
	Raster_DrawTexturedPolygon((uint16_t)texture, points, count, false, false);
}

void SlipRendererHost_DrawOpaqueAffine(void *context, RasterTexturedPoint *points, uint32_t count, uint32_t texture) {
	(void)context;
	Raster_DrawTexturedPolygon((uint16_t)texture, points, count, false, true);
}

void SlipRendererHost_DrawPerspective(void *context, RasterTexturedPoint *points, uint32_t count, uint32_t texture) {
	(void)context;
	Raster_DrawTexturedPolygon((uint16_t)texture, points, count, true, false);
}

void SlipRendererHost_DrawOpaquePerspective(void *context, RasterTexturedPoint *points, uint32_t count,
                                            uint32_t texture) {
	(void)context;
	Raster_DrawTexturedPolygon((uint16_t)texture, points, count, true, true);
}

const SlipRendererRasterCalls SlipRendererHost_rasterCalls = {.line = SlipRendererHost_DrawLine,
                                                              .rectangle = SlipRendererHost_DrawRectangle,
                                                              .flat = SlipRendererHost_DrawFlat,
                                                              .dithered = SlipRendererHost_DrawDithered,
                                                              .shaded = SlipRendererHost_DrawShaded,
                                                              .opaqueAffine = SlipRendererHost_DrawOpaqueAffine,
                                                              .maskedAffine = SlipRendererHost_DrawAffine,
                                                              .opaquePerspective =
                                                                  SlipRendererHost_DrawOpaquePerspective,
                                                              .maskedPerspective = SlipRendererHost_DrawPerspective};
