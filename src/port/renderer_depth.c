#include "renderer_depth.h"
#include "shape_format.h"

int32_t SlipRenderer_MinimumDepth(SlipRendererState *state, uint16_t countAndFlags, const uint8_t *indices,
                                  void *transformContext) {
	uint16_t remaining = countAndFlags & SLIP_PRIMITIVE_VERTEX_COUNT_MASK;
	int32_t minimum = INT32_MAX;
	do {
		const uint16_t index = (uint16_t)((uint16_t)indices[0] | (uint16_t)indices[1] << 8);
		indices += SLIP_SERIALIZED_INDEX_BYTES;
		SlipDraw3DVertexRecord *const vertex = state->activeVertices + index;
		if ((vertex->flags & SLIP_VERTEX_TRANSFORMED) == 0) {
			vertex->flags = SLIP_VERTEX_TRANSFORMED;
			const uint32_t sourceX = (uint16_t)vertex->sourceX | (uint32_t)(uint16_t)vertex->sourceY << 16;
			const uint32_t sourceY = (uint16_t)vertex->sourceY | (uint32_t)(uint16_t)vertex->sourceZ << 16;
			const uint32_t sourceZ = (uint16_t)vertex->sourceZ | (uint32_t)vertex->sourceFollowingWord << 16;
			vertex->world = state->activeTransform(sourceX, sourceY, sourceZ, vertex, transformContext);
		}
		const int32_t depth = vertex->world.z;
		if (depth < 0)
			return depth;
		if (depth <= minimum)
			minimum = depth;
	} while (--remaining != 0);
	const int64_t product = (int64_t)(int32_t)state->projection.inverseProjectionScale * minimum;
	return (int32_t)(uint32_t)((uint64_t)product >> 16);
}
