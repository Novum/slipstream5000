#ifndef SLIPSTREAM5000_RENDERER_LIGHTING_H
#define SLIPSTREAM5000_RENDERER_LIGHTING_H
#include "renderer_lifecycle.h"
uint16_t SlipRenderer_Specular(const SlipRendererState *, SlipDraw3DVec32 relative, int16_t normalX, int16_t normalY,
                               int16_t normalZ);
uint32_t SlipRenderer_VertexColour(SlipRendererState *, const SlipDraw3DMaterialRecord *, SlipDraw3DVertexRecord *,
                                   int16_t normalX, int16_t normalY, int16_t normalZ, void *sourceContext);
#endif
