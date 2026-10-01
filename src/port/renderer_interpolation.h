#ifndef SLIPSTREAM5000_RENDERER_INTERPOLATION_H
#define SLIPSTREAM5000_RENDERER_INTERPOLATION_H
#include "renderer_lifecycle.h"
void SlipRenderer_InterpolateTexture14(SlipRendererState *, SlipRendererPolygon *, const SlipRendererPolygon *,
                                       uint16_t fraction);
void SlipRenderer_InterpolateTexture30(SlipRendererState *, SlipRendererPolygon *, const SlipRendererPolygon *,
                                       uint32_t fraction);
void SlipRenderer_InterpolateDepthTexture14(SlipRendererState *, SlipRendererPolygon *, const SlipRendererPolygon *,
                                            uint16_t fraction);
void SlipRenderer_InterpolateDepthTexture30(SlipRendererState *, SlipRendererPolygon *, const SlipRendererPolygon *,
                                            uint32_t fraction);
void SlipRenderer_InterpolateHorizontal(SlipRendererState *, SlipRendererPolygon *, const SlipRendererPolygon *,
                                        int32_t limit);
void SlipRenderer_InterpolateVertical(SlipRendererState *, SlipRendererPolygon *, const SlipRendererPolygon *,
                                      int32_t limit);
void SlipRenderer_ProjectClipped(SlipRendererState *, SlipRendererPolygon *, SlipDraw3DVec32 world);
void SlipRenderer_InterpolateDepth(SlipRendererState *, SlipRendererPolygon *, const SlipRendererPolygon *,
                                   int32_t limit);

typedef struct SlipRendererPlaneIntersection {
	SlipDraw3DVec32 step;
	uint32_t fraction;
} SlipRendererPlaneIntersection;

SlipRendererPlaneIntersection SlipRenderer_IntersectPlaneQ30(SlipDraw3DVec32 delta, int32_t targetDepth,
                                                             int32_t insideDepth);
SlipRendererPlaneIntersection SlipRenderer_IntersectPlaneQ14(SlipDraw3DVec32 delta, int32_t targetDepth,
                                                             int32_t insideDepth);
void SlipRenderer_InterpolateAuxiliary(SlipRendererState *, SlipRendererPolygon *, const SlipRendererPolygon *);
void SlipRenderer_InterpolatePostPlane(SlipRendererPolygon *, const SlipRendererPolygon *);
#endif
