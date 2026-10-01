#ifndef SLIPSTREAM5000_RENDERER_STATE_H
#define SLIPSTREAM5000_RENDERER_STATE_H
#include "renderer_lifecycle.h"
void SlipRenderer_AdvanceBackground(SlipRendererState *);
void SlipRenderer_SetCamera(SlipRendererState *, SlipView3DVec32 position, const SlipView3DMatrix *matrix);
void SlipRenderer_SelectState(SlipRendererState *, uint16_t index);
uint32_t SlipRenderer_GetState(const SlipRendererState *);
void SlipRenderer_ResetState(SlipRendererState *);
void SlipRenderer_RestoreVertices(SlipRendererState *);
void SlipRenderer_SetOrigin(SlipRendererState *, const SlipView3DMatrix *draw, const SlipView3DMatrix *world,
                            SlipView3DVec32 position);
#endif
