#ifndef SLIPSTREAM5000_RENDERER_BOUNDS_H
#define SLIPSTREAM5000_RENDERER_BOUNDS_H
#include "actor_shape.h"
#include "renderer_lifecycle.h"
void SlipRenderer_SetBounds(SlipRendererState *, SlipView3DVec32 minimum, SlipView3DVec32 maximum);
/* The caller supplies its active projection binding; box globals stay in the
 * shared renderer state. No per-shape reset of the accumulated any-corner mask. */
void SlipRenderer_ClipBoxCorner(SlipRendererState *, const SlipDraw3DProjectState *, SlipDraw3DVec32 point);
SlipActorShapeBounds SlipRenderer_ProjectBounds(SlipRendererState *, SlipView3DVec32 translation,
                                                const SlipView3DMatrix *, const SlipDraw3DProjectState *);
#endif
