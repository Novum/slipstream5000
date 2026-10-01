#ifndef SLIPSTREAM5000_RENDERER_PROJECTION_H
#define SLIPSTREAM5000_RENDERER_PROJECTION_H
#include "renderer_lifecycle.h"
void SlipRenderer_SetFlags(SlipRendererState *, uint16_t flags);
void SlipRenderer_SetShapeFlags(SlipRendererState *, uint16_t flags);
void SlipRenderer_DefaultProjection(SlipRendererState *);
#endif
