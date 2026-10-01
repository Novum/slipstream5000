#ifndef SLIPSTREAM5000_RENDERER_DEPTH_H
#define SLIPSTREAM5000_RENDERER_DEPTH_H
#include "renderer_lifecycle.h"
int32_t SlipRenderer_MinimumDepth(SlipRendererState *, uint16_t countAndFlags, const uint8_t *indices,
                                  void *transformContext);
#endif
