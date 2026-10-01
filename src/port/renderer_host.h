#ifndef SLIPSTREAM5000_RENDERER_HOST_H
#define SLIPSTREAM5000_RENDERER_HOST_H
#include "renderer_polygons.h"
extern SlipRendererState SlipRendererHost_state;
extern const SlipRendererLifecycleCalls SlipRendererHost_lifecycleCalls;
extern const SlipRendererLineCalls SlipRendererHost_lineCalls;
extern const SlipRendererRasterCalls SlipRendererHost_flatRasterCalls;
extern const SlipRendererDepthClipCalls SlipRendererHost_depthClipCalls;
extern const SlipRendererScreenClipCalls SlipRendererHost_screenClipCalls;
void SlipRendererHost_DrawAffine(void *, RasterTexturedPoint *, uint32_t count, uint32_t texture);
void SlipRendererHost_DrawOpaqueAffine(void *, RasterTexturedPoint *, uint32_t count, uint32_t texture);
void SlipRendererHost_DrawPerspective(void *, RasterTexturedPoint *, uint32_t count, uint32_t texture);
void SlipRendererHost_DrawOpaquePerspective(void *, RasterTexturedPoint *, uint32_t count, uint32_t texture);
void SlipRendererHost_DrawLine(void *, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t colour);
void SlipRendererHost_DrawRectangle(void *, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t colour);
void SlipRendererHost_DrawFlat(void *, RasterTexturedPoint *, uint32_t count, uint32_t colour);
void SlipRendererHost_DrawShaded(void *, RasterTexturedPoint *, uint32_t count);
void SlipRendererHost_DrawDithered(void *, RasterTexturedPoint *, uint32_t count, uint32_t colour, uint32_t dither);
extern const SlipRendererRasterCalls SlipRendererHost_rasterCalls;
bool SlipRendererHost_SubmitSolid(void *sourceContext, uint16_t countAndFlags, int16_t x, int16_t y, int16_t z,
                                  uint16_t material, const uint8_t *serializedIndices);
bool SlipRendererHost_SubmitTextured(void *sourceContext, uint16_t countAndFlags, int16_t x, int16_t y, int16_t z,
                                     uint16_t material, const uint8_t *serializedIndices);
#endif
