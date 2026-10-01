#ifndef SLIPSTREAM5000_RENDERER_CULLING_H
#define SLIPSTREAM5000_RENDERER_CULLING_H
#include "renderer_lifecycle.h"
/* True is the original carry-clear (visible) result. */
bool SlipRenderer_PlaneVisible(SlipRendererState *, int16_t, int16_t, int16_t, uint16_t, void *sourceContext);
SlipDraw3DVertexRecord *SlipRenderer_TransformVertex(SlipRendererState *, uint16_t index, void *transformContext);

int SlipRenderer_ClassifyPolygon(SlipRendererState *, uint16_t countAndFlags, const uint8_t *serializedIndices,
                                 void *transformContext);
int32_t SlipRenderer_PolygonDepth(SlipRendererState *, uint16_t countAndFlags, const uint8_t *serializedIndices,
                                  void *transformContext);
#endif
