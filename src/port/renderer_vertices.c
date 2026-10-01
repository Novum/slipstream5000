#include "renderer_vertices.h"
#include "byte_order.h"

/* Serialized SHP vertex words; native vertex records are assigned by field. */

void SlipRenderer_BuildVertices(SlipRendererState *state, const uint8_t *source, uint32_t count, int16_t stride,
                                SlipDraw3DTransformFn transform, SlipDraw3DSourcePointFn sourcePoint,
                                const SlipRendererAllocationCalls *calls) {
	if (count > state->vertexCapacity)
		SlipRenderer_GrowVertices(state, count + 64u, calls);
	for (;;) {
		const uint32_t bytes = count * 64u;
		const uint32_t available = (uint32_t)(state->vertexLimit - state->vertexCursor) * 64u;
		if (bytes <= available) {
			SlipDraw3DVertexRecord *const first = state->vertexCursor;
			state->vertexCursor = first + bytes / 64u;
			state->activeVertices = first;
			state->currentState->vertices = first;
			state->currentState->transform = transform;
			state->activeTransform = transform;
			state->currentState->source = sourcePoint;
			state->activeSource = sourcePoint;
			SlipDraw3DVertexRecord *destination = first;
			const int32_t sourceAdjustment = (int16_t)(uint16_t)((uint16_t)stride - 6u);
			uint32_t remaining = count;
			do {
				destination->flags = 0;
				destination->sourceX = SlipBytes_ReadLEI16(source);
				destination->sourceY = SlipBytes_ReadLEI16(source + 2);
				destination->sourceZ = SlipBytes_ReadLEI16(source + 4);
				++destination;
				source += 6 + sourceAdjustment;
			} while (--remaining != 0);
			return;
		}
		SlipRenderer_GrowVertices(state, (bytes - available) / 64u + 64u + state->vertexCapacity, calls);
	}
}
