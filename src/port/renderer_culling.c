#include "renderer_culling.h"
#include "shape_format.h"

SlipDraw3DVertexRecord *SlipRenderer_TransformVertex(SlipRendererState *state, uint16_t index, void *transformContext) {
	SlipDraw3DVertexRecord *const vertex = state->activeVertices + index;
	if ((vertex->flags & SLIP_VERTEX_TRANSFORMED) == 0) {
		vertex->flags = SLIP_VERTEX_TRANSFORMED;
		const uint32_t sourceX = (uint16_t)vertex->sourceX | (uint32_t)(uint16_t)vertex->sourceY << SLIP_WORD_BITS;
		const uint32_t sourceY = (uint16_t)vertex->sourceY | (uint32_t)(uint16_t)vertex->sourceZ << SLIP_WORD_BITS;
		const uint32_t sourceZ = (uint16_t)vertex->sourceZ | (uint32_t)vertex->sourceFollowingWord << SLIP_WORD_BITS;
		vertex->world = state->activeTransform(sourceX, sourceY, sourceZ, vertex, transformContext);
	}
	return vertex;
}

int SlipRenderer_ClassifyPolygon(SlipRendererState *state, uint16_t countAndFlags, const uint8_t *serializedIndices,
                                 void *transformContext) {
	uint32_t count = countAndFlags & SLIP_PRIMITIVE_VERTEX_COUNT_MASK;
	SlipDraw3DVertexRecord *savedVertices[SLIP_PRIMITIVE_VERTEX_COUNT_MASK];
	state->polygonAllClipMask = SLIP_CLIP_ALL;
	state->polygonAnyClipMask = 0;
	for (uint32_t i = 0; i < count; ++i) {
		const uint16_t index =
		    (uint16_t)((uint16_t)serializedIndices[0] | (uint16_t)serializedIndices[1] << SLIP_BYTE_BITS);
		SlipDraw3DVertexRecord *const vertex = SlipRenderer_TransformVertex(state, index, transformContext);
		uint32_t flags = vertex->flags;
		if ((flags & SLIP_VERTEX_DEPTH_CLASSIFIED) == 0) {
			flags = SLIP_VERTEX_TRANSFORMED_AND_DEPTH_CLASSIFIED;
			if (vertex->world.z < state->projection.minZ)
				flags |= SLIP_CLIP_NEAR;
			if (vertex->world.z > state->projection.maxZ)
				flags |= SLIP_CLIP_FAR;
			if ((state->shapeFlags & SLIP_SHAPE_CLIP_AUXILIARY) != 0) {
				flags &= ~SLIP_CLIP_AUXILIARY;
				SlipDraw3DVec32 origin = state->projection.auxiliaryClipPlaneOrigin;
				SlipDraw3DVec32 normal = state->projection.auxiliaryClipPlaneNormal;
				const int32_t x = (int32_t)((uint32_t)vertex->world.x - (uint32_t)origin.x);
				const int32_t y = (int32_t)((uint32_t)vertex->world.y - (uint32_t)origin.y);
				const int32_t z = (int32_t)((uint32_t)vertex->world.z - (uint32_t)origin.z);
				uint64_t distance = (uint64_t)((int64_t)x * normal.x);
				distance += (uint64_t)((int64_t)y * normal.y);
				distance += (uint64_t)((int64_t)z * normal.z);
				vertex->depth = (int32_t)(uint32_t)((distance >> SLIP_NORMAL_FRACTION_BITS) +
				                                    ((distance >> SLIP_NORMAL_ROUND_BIT) & 1u));
				if (vertex->depth < 0)
					flags |= SLIP_CLIP_AUXILIARY;
			}
			vertex->flags |= flags;
			flags = vertex->flags;
		}
		state->polygonAllClipMask &= flags;
		state->polygonAnyClipMask |= flags;
		serializedIndices += SLIP_SERIALIZED_INDEX_BYTES;
		savedVertices[i] = vertex;
	}
	if (state->polygonAllClipMask != 0)
		return -1;
	state->polygonAllClipMask = UINT32_MAX;
	do {
		SlipDraw3DVertexRecord *const vertex = savedVertices[--count];
		if ((vertex->flags & SLIP_VERTEX_VIEW_MASK_CLASSIFIED) == 0) {
			vertex->flags |= SLIP_VERTEX_VIEW_MASK_CLASSIFIED;
			vertex->clipMask = state->projection.projectMask(vertex->world, &state->projection);
		}
		state->polygonAllClipMask &= vertex->clipMask;
		state->polygonAnyClipMask |= vertex->clipMask;
	} while (count != 0);
	if (state->polygonAllClipMask != 0)
		return -1;
	return state->polygonAnyClipMask != 0 ? 1 : 0;
}

bool SlipRenderer_PlaneVisible(SlipRendererState *state, int16_t normalX, int16_t normalY, int16_t normalZ,
                               uint16_t index, void *sourceContext) {
	if (state->projection.projectionMode == 1) {
		const SlipView3DMatrix *const matrix = &state->currentState->matrix;
		uint32_t sum = (uint32_t)((int32_t)normalZ * matrix->m[8]);
		sum += (uint32_t)((int32_t)normalY * matrix->m[5]);
		sum += (uint32_t)((int32_t)normalX * matrix->m[2]);
		return (int16_t)((int32_t)sum >> 14) <= 0;
	}
	SlipDraw3DVertexRecord *const vertex = state->activeVertices + index;
	SlipView3DVec32 source = state->activeSource(vertex->sourceX, vertex->sourceY, vertex->sourceZ, sourceContext);
	const int32_t x = (int32_t)((uint32_t)source.x - (uint32_t)state->origin.x);
	const int32_t y = (int32_t)((uint32_t)source.y - (uint32_t)state->origin.y);
	const int32_t z = (int32_t)((uint32_t)source.z - (uint32_t)state->origin.z);
	const int64_t sum = (int64_t)x * normalX + (int64_t)y * normalY + (int64_t)z * normalZ;
	return sum < 0;
}

int32_t SlipRenderer_PolygonDepth(SlipRendererState *state, uint16_t countAndFlags, const uint8_t *serializedIndices,
                                  void *transformContext) {
	uint16_t count = countAndFlags & SLIP_PRIMITIVE_VERTEX_COUNT_MASK;
	int32_t minimum = INT32_MAX;
	do {
		const uint16_t index =
		    (uint16_t)((uint16_t)serializedIndices[0] | (uint16_t)serializedIndices[1] << SLIP_BYTE_BITS);
		serializedIndices += SLIP_SERIALIZED_INDEX_BYTES;
		SlipDraw3DVertexRecord *const vertex = state->activeVertices + index;
		if ((vertex->flags & SLIP_VERTEX_TRANSFORMED) == 0) {
			vertex->flags = SLIP_VERTEX_TRANSFORMED;
			const uint32_t x = (uint16_t)vertex->sourceX | (uint32_t)(uint16_t)vertex->sourceY << SLIP_WORD_BITS;
			const uint32_t y = (uint16_t)vertex->sourceY | (uint32_t)(uint16_t)vertex->sourceZ << SLIP_WORD_BITS;
			const uint32_t z = (uint16_t)vertex->sourceZ | (uint32_t)vertex->sourceFollowingWord << SLIP_WORD_BITS;
			vertex->world = state->activeTransform(x, y, z, vertex, transformContext);
		}
		if (vertex->world.z < 0)
			return vertex->world.z;
		if (vertex->world.z <= minimum)
			minimum = vertex->world.z;
	} while (--count != 0);
	const int64_t product = (int64_t)(int32_t)state->projection.inverseProjectionScale * minimum;
	return (int32_t)((uint64_t)product >> SLIP_WORD_BITS);
}
