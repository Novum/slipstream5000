#include "renderer_vertices.h"
#include "byte_order.h"
#include "shape_format.h"

/* Serialized SHP vertex words; native vertex records are assigned by field. */

void SlipRenderer_BuildVertices(SlipRendererState *state, const uint8_t *source, uint32_t count, int16_t stride,
                                SlipDraw3DTransformFn transform, SlipDraw3DSourcePointFn sourcePoint,
                                const SlipRendererAllocationCalls *calls) {
	if (count > state->vertexCapacity)
		SlipRenderer_GrowVertices(state, count + SLIP_DRAW3D_VERTEX_BUFFER_GROWTH_RESERVE, calls);
	for (;;) {
		const uint32_t bytes = count * SLIP_DRAW3D_VERTEX_RECORD_SIZE;
		const uint32_t available =
		    (uint32_t)(state->vertexLimit - state->vertexCursor) * SLIP_DRAW3D_VERTEX_RECORD_SIZE;
		if (bytes <= available) {
			SlipDraw3DVertexRecord *const first = state->vertexCursor;
			state->vertexCursor = first + bytes / SLIP_DRAW3D_VERTEX_RECORD_SIZE;
			state->activeVertices = first;
			state->currentState->vertices = first;
			state->currentState->transform = transform;
			state->activeTransform = transform;
			state->currentState->source = sourcePoint;
			state->activeSource = sourcePoint;
			SlipDraw3DVertexRecord *destination = first;
			const int32_t sourceAdjustment = (int16_t)(uint16_t)((uint16_t)stride - SLIP_SHAPE_VERTEX_BYTES);
			uint32_t remaining = count;
			do {
				destination->flags = 0;
				destination->sourceX = SlipBytes_ReadLEI16(source + SLIP_SHAPE_VERTEX_X_OFFSET);
				destination->sourceY = SlipBytes_ReadLEI16(source + SLIP_SHAPE_VERTEX_Y_OFFSET);
				destination->sourceZ = SlipBytes_ReadLEI16(source + SLIP_SHAPE_VERTEX_Z_OFFSET);
				++destination;
				source += SLIP_SHAPE_VERTEX_BYTES + sourceAdjustment;
			} while (--remaining != 0);
			return;
		}
		SlipRenderer_GrowVertices(state,
		                          (bytes - available) / SLIP_DRAW3D_VERTEX_RECORD_SIZE +
		                              SLIP_DRAW3D_VERTEX_BUFFER_GROWTH_RESERVE + state->vertexCapacity,
		                          calls);
	}
}
