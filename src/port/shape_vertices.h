#ifndef SLIPSTREAM5000_SHAPE_VERTICES_H
#define SLIPSTREAM5000_SHAPE_VERTICES_H
#include "actor_shape.h"
#include "draw3d.h"

typedef struct SlipShapeVertexCalls {
	void *context;
	void (*setOrigin)(void *, const SlipView3DMatrix *draw, const SlipView3DMatrix *world,
	                  SlipView3DVec32 worldPosition);
	void (*buildVertices)(void *, const uint8_t *vertices, uint16_t count, int16_t stride,
	                      SlipDraw3DTransformFn transform, SlipDraw3DSourcePointFn source);
} SlipShapeVertexCalls;

void SlipShapeVertices_Setup(SlipActorShapeState *, SlipView3DVec32 view, SlipView3DVec32 world);
void SlipShapeVertices_Matrices(SlipActorShapeState *, const SlipView3DMatrix *world, const SlipView3DMatrix *draw,
                                const SlipShapeVertexCalls *);
void SlipShapeVertices_Initialize(SlipActorShapeState *, uint8_t *shape, const SlipShapeVertexCalls *);
SlipDraw3DVec32 SlipShapeVertices_Transform(uint32_t x, uint32_t y, uint32_t z, SlipDraw3DVertexRecord *, void *state);
SlipView3DVec32 SlipShapeVertices_Source(int16_t x, int16_t y, int16_t z, void *state);
SlipView3DVec32 SlipShapeVertices_SourceShifted(int16_t x, int16_t y, int16_t z, void *state);
SlipDraw3DVec32 SlipShapeVertices_TransformShifted(uint32_t x, uint32_t y, uint32_t z, SlipDraw3DVertexRecord *,
                                                   void *state);
#endif
