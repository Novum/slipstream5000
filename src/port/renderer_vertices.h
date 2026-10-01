#ifndef SLIPSTREAM5000_RENDERER_VERTICES_H
#define SLIPSTREAM5000_RENDERER_VERTICES_H
#include "renderer_allocation.h"
void SlipRenderer_BuildVertices(SlipRendererState *, const uint8_t *source, uint32_t count, int16_t stride,
                                SlipDraw3DTransformFn, SlipDraw3DSourcePointFn, const SlipRendererAllocationCalls *);
#endif
