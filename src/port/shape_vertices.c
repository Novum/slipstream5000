#include "shape_vertices.h"
#include "byte_order.h"

/* These loads read the serialized SHP header and vertex-list count. */

void SlipShapeVertices_Setup(SlipActorShapeState *state, SlipView3DVec32 view, SlipView3DVec32 world) {
	state->viewPosition = view;
	state->worldPosition = world;
}

void SlipShapeVertices_Matrices(SlipActorShapeState *state, const SlipView3DMatrix *world, const SlipView3DMatrix *draw,
                                const SlipShapeVertexCalls *calls) {
	state->drawMatrix = *draw;
	calls->setOrigin(calls->context, &state->drawMatrix, world, state->worldPosition);
}

void SlipShapeVertices_Initialize(SlipActorShapeState *state, uint8_t *shape, const SlipShapeVertexCalls *calls) {
	state->shape = shape;
	const uint16_t scale = SlipBytes_ReadLE16(shape + 2);
	SlipDraw3DTransformFn transform;
	SlipDraw3DSourcePointFn source;
	if (scale != 0) {
		state->transformShift = 14u - scale;
		state->sourceShift = scale;
		transform = SlipShapeVertices_TransformShifted;
		source = SlipShapeVertices_SourceShifted;
	} else {
		transform = SlipShapeVertices_Transform;
		source = SlipShapeVertices_Source;
	}
	const uint8_t *const vertices = shape + SlipBytes_ReadLE32(shape + 0x10);
	const uint16_t count = SlipBytes_ReadLE16(vertices);
	calls->buildVertices(calls->context, vertices + 2, count, 6, transform, source);
}

SlipDraw3DVec32 SlipShapeVertices_Transform(uint32_t x, uint32_t y, uint32_t z, SlipDraw3DVertexRecord *record,
                                            void *context) {
	(void)record;
	SlipActorShapeState *const state = context;
	SlipView3DVec32 rotated =
	    SlipView3D_TransformPosition16(&state->drawMatrix, (SlipView3DVec32){(int16_t)x, (int16_t)y, (int16_t)z});
	return (SlipDraw3DVec32){(int32_t)((uint32_t)rotated.x + (uint32_t)state->viewPosition.x),
	                         (int32_t)((uint32_t)rotated.y + (uint32_t)state->viewPosition.y),
	                         (int32_t)((uint32_t)rotated.z + (uint32_t)state->viewPosition.z)};
}

SlipView3DVec32 SlipShapeVertices_Source(int16_t x, int16_t y, int16_t z, void *context) {
	(void)context;
	return (SlipView3DVec32){x, y, z};
}

SlipView3DVec32 SlipShapeVertices_SourceShifted(int16_t x, int16_t y, int16_t z, void *context) {
	SlipActorShapeState *const state = context;
	const uint32_t shift = state->sourceShift & 31u;
	return (SlipView3DVec32){(int32_t)((uint32_t)(int32_t)x << shift), (int32_t)((uint32_t)(int32_t)y << shift),
	                         (int32_t)((uint32_t)(int32_t)z << shift)};
}

SlipDraw3DVec32 SlipShapeVertices_TransformShifted(uint32_t x, uint32_t y, uint32_t z, SlipDraw3DVertexRecord *record,
                                                   void *context) {
	(void)record;
	SlipActorShapeState *const state = context;
	const SlipView3DMatrix *const matrix = &state->drawMatrix;
	int32_t sourceX = (int16_t)x, sourceY = (int16_t)y, sourceZ = (int16_t)z;
	const uint32_t sourceXForOutputX = (uint32_t)(sourceX * matrix->m[0]);
	const uint32_t sourceYForOutputX = (uint32_t)(sourceY * matrix->m[3]);
	const uint32_t sourceZForOutputX = (uint32_t)(sourceZ * matrix->m[6]);
	const uint32_t sourceXForOutputY = (uint32_t)(sourceX * matrix->m[1]);
	const uint32_t sourceYForOutputY = (uint32_t)(sourceY * matrix->m[4]);
	const uint32_t sourceZForOutputY = (uint32_t)(sourceZ * matrix->m[7]);
	const uint32_t sourceXForOutputZ = (uint32_t)(sourceX * matrix->m[2]);
	const uint32_t sourceYForOutputZ = (uint32_t)(sourceY * matrix->m[5]);
	const uint32_t sourceZForOutputZ = (uint32_t)(sourceZ * matrix->m[8]);
	const uint32_t sumZ = sourceZForOutputZ + sourceYForOutputZ + sourceXForOutputZ;
	const uint32_t sumY = sourceZForOutputY + sourceYForOutputY + sourceXForOutputY;
	const uint32_t sumX = sourceZForOutputX + sourceYForOutputX + sourceXForOutputX;
	const uint32_t shift = state->transformShift & 31u;
	return (SlipDraw3DVec32){(int32_t)((uint32_t)((int32_t)sumX >> shift) + (uint32_t)state->viewPosition.x),
	                         (int32_t)((uint32_t)((int32_t)sumY >> shift) + (uint32_t)state->viewPosition.y),
	                         (int32_t)((uint32_t)((int32_t)sumZ >> shift) + (uint32_t)state->viewPosition.z)};
}
