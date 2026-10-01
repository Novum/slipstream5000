#include "renderer_polygons.h"
#include "renderer_interpolation.h"
#include "shape_format.h"

bool SlipRenderer_ClipDepth(SlipRendererState *state, uint32_t allClipMask, uint32_t *anyClipMask,
                            const SlipRendererDepthClipCalls *calls) {
	SlipRendererPolygonMasks masks = {allClipMask, *anyClipMask};
	if ((state->shapeFlags & SLIP_SHAPE_CLIP_AUXILIARY) != 0 && (masks.any & SLIP_CLIP_AUXILIARY) != 0) {
		SlipRendererClipEdges edges;
		if (calls->edges(calls->context, SLIP_CLIP_AUXILIARY, &edges))
			return true;
		calls->auxiliary(calls->context, edges.firstTarget, edges.firstInside);
		calls->auxiliary(calls->context, edges.secondTarget, edges.secondInside);
		masks = SlipRenderer_ActivePolygonMasks(state);
		if ((masks.all & SLIP_CLIP_ALL) != 0)
			return true;
	}
	if ((masks.any & SLIP_CLIP_DEPTH) != 0) {
		if ((masks.any & SLIP_CLIP_NEAR) != 0) {
			SlipRendererClipEdges edges;
			if (calls->edges(calls->context, SLIP_CLIP_NEAR, &edges))
				return true;
			calls->depth(calls->context, edges.firstTarget, edges.firstInside, state->projection.minZ);
			calls->depth(calls->context, edges.secondTarget, edges.secondInside, state->projection.minZ);
		}

		if ((masks.any & SLIP_CLIP_FAR) != 0) {
			SlipRendererClipEdges edges;
			if (calls->edges(calls->context, SLIP_CLIP_FAR, &edges))
				return true;
			calls->depth(calls->context, edges.firstTarget, edges.firstInside, state->projection.maxZ);
			calls->depth(calls->context, edges.secondTarget, edges.secondInside, state->projection.maxZ);
		}
		masks = SlipRenderer_ActivePolygonMasks(state);
	}
	if ((masks.all & SLIP_CLIP_SCREEN) != 0)
		return true;
	*anyClipMask = masks.any;
	return false;
}

bool SlipRenderer_SubmitSolid(SlipRendererState *state, uint32_t countAndFlags, int16_t normalX, int16_t normalY,
                              int16_t normalZ, uint16_t material, const uint8_t *serializedStream,
                              const SlipRendererSubmitCalls *calls) {
	if (state->postClipPlanes != NULL || (state->shapeFlags & SLIP_SHAPE_INSIDE_VIEW) == 0) {
		SlipRenderer_ReturnActivePolygons(state);
		if (calls->buildSolid(calls->context, countAndFlags, normalX, normalY, normalZ, material, serializedStream))
			return true;
		calls->draw(calls->context);
	} else {
		calls->drawUnclipped(calls->context, (uint16_t)countAndFlags, normalX, normalY, normalZ, material,
		                     serializedStream);
	}
	return false;
}

void SlipRenderer_SelectTextureRaster(SlipRendererState *state, int16_t normalX, int16_t normalY, int16_t normalZ) {
	if ((state->projection.renderFlags & SLIP_RENDER_ALTERNATE_TEXTURE_RASTER) == 0) {
		const SlipView3DMatrix *const matrix = &state->currentState->matrix;
		uint32_t product = (uint32_t)((int32_t)normalX * matrix->m[2]);
		uint16_t facing =
		    (uint16_t)((product >> SLIP_NORMAL_FRACTION_BITS) + ((product >> SLIP_NORMAL_ROUND_BIT) & 1u));
		product = (uint32_t)((int32_t)normalY * matrix->m[5]);
		facing =
		    (uint16_t)(facing + (product >> SLIP_NORMAL_FRACTION_BITS) + ((product >> SLIP_NORMAL_ROUND_BIT) & 1u));
		product = (uint32_t)((int32_t)normalZ * matrix->m[8]);
		facing =
		    (uint16_t)(facing + (product >> SLIP_NORMAL_FRACTION_BITS) + ((product >> SLIP_NORMAL_ROUND_BIT) & 1u));
		if ((int16_t)facing <= SLIP_TEXTURE_AFFINE_FACING_THRESHOLD)
			state->projection.renderFlags |= SLIP_RENDER_ALTERNATE_TEXTURE_RASTER;
	}
}

bool SlipRenderer_SubmitTextured(SlipRendererState *state, uint32_t countAndFlags, int16_t normalX, int16_t normalY,
                                 int16_t normalZ, uint16_t materialIndex, const uint8_t *serializedStream,
                                 const SlipRendererTexturedSubmitCalls *calls) {
	SlipRenderer_ReturnActivePolygons(state);
	const uint32_t savedFlags = state->projection.renderFlags;
	SlipDraw3DMaterialRecord *material = NULL;
	uint32_t texture = 0;
	if ((savedFlags & SLIP_RENDER_SOLID_TEXTURE_FALLBACK) == 0 &&
	    (countAndFlags & SLIP_PRIMITIVE_TEXTURE_COORDINATES) != 0) {
		SlipDraw3DMaterialTable *const table = state->materials->table;
		material = table->records + (materialIndex < (uint16_t)table->count ? materialIndex : 0);
		texture = material->textureHandles[state->materials->frame];
	}
	bool rejected;
	if (texture != 0) {
		calls->selectRaster(calls->context, normalX, normalY, normalZ);
		if ((state->projection.renderFlags & SLIP_RENDER_MASKED_TEXTURE) == 0) {
			state->projection.renderFlags &= ~SLIP_RENDER_MASKED_TEXTURE;
			if (material->textureTransparency == 0)
				state->projection.renderFlags |= SLIP_RENDER_MASKED_TEXTURE;
		}
		state->activeMaterial = material;
		rejected =
		    calls->buildTextured(calls->context, countAndFlags, normalX, normalY, normalZ, texture, serializedStream);
		if (!rejected)
			calls->draw(calls->context);
	} else {

		countAndFlags &= SLIP_PRIMITIVE_SOLID_COUNT_FLAGS_MASK;
		SlipDraw3DMaterialTable *const table = state->materials->table;
		material = table->records + (materialIndex < (uint16_t)table->count ? materialIndex : 0);
		rejected = material->skipFlatPolygon != 0;
		if (!rejected) {
			rejected = calls->buildSolid(calls->context, countAndFlags, normalX, normalY, normalZ, materialIndex,
			                             serializedStream);
			if (!rejected)
				calls->draw(calls->context);
		}
	}
	state->projection.renderFlags = savedFlags;
	return rejected;
}

void SlipRenderer_ReturnActivePolygons(SlipRendererState *state) {
	SlipRendererPolygon *current = state->activePolygons;
	if (current != NULL) {
		SlipRendererPolygon *next;
		do {
			next = current->next;
			SlipRendererPolygon *const following = current->next;
			SlipRendererPolygon *const previous = current->previous;
			previous->next = following;
			following->previous = previous;
			SlipRendererPolygon *const sentinel = state->freePolygons;
			SlipRendererPolygon *const firstFree = sentinel->next;
			sentinel->next = current;
			firstFree->previous = current;
			current->next = firstFree;
			current->previous = sentinel;
			SlipRendererPolygon *const returned = current;
			current = next;
			if (current == returned)
				break;
		} while (true);
		state->activePolygons = NULL;
	}
}

SlipRendererPolygonMasks SlipRenderer_ActivePolygonMasks(const SlipRendererState *state) {
	SlipRendererPolygon *current = state->activePolygons;
	SlipRendererPolygon *const first = current;
	SlipRendererPolygonMasks masks = {SLIP_CLIP_ALL, 0};
	do {
		const uint32_t flags = current->point.flags;
		masks.all &= flags;
		masks.any |= flags;
		current = current->next;
	} while (current != first);
	return masks;
}

bool SlipRenderer_BuildSolidRing(SlipRendererState *state, SlipRendererPolygon *first, const uint8_t *serializedIndices,
                                 uint32_t count, const SlipRendererSolidRingCalls *calls) {
	SlipRendererPolygon *current = first;
	state->polygonAllClipMask = SLIP_CLIP_ALL;
	state->polygonAnyClipMask = 0;
	for (;;) {
		const uint16_t index = (uint16_t)((uint16_t)serializedIndices[0] | (uint16_t)serializedIndices[1] << 8);
		SlipDraw3DVertexRecord *const vertex = state->activeVertices + index;
		const uint32_t flags = calls->projectVertex(calls->context, vertex);
		/* REP MOVSD copies exactly the first seven dwords, leaving shade,
		 * texture coordinates, other payload and links untouched. */
		current->point.world = vertex->world;
		current->point.screenX = vertex->screenX;
		current->point.screenY = vertex->screenY;
		current->point.flags = vertex->flags;
		current->point.depth = vertex->depth;
		state->polygonAllClipMask &= flags;
		state->polygonAnyClipMask |= flags;
		if (--count == 0)
			break;
		SlipRendererPolygon *const previous = current;
		SlipRendererPolygon *const sentinel = state->freePolygons;
		current = sentinel->next;
		SlipRendererPolygon *const following = current->next;
		sentinel->next = following;
		following->previous = sentinel;
		previous->next = current;
		current->previous = previous;
		serializedIndices += SLIP_SERIALIZED_INDEX_BYTES;
	}
	current->next = first;
	first->previous = current;
	uint32_t anyClipMask = state->polygonAnyClipMask;
	if ((anyClipMask & SLIP_CLIP_ALL) != 0) {
		const uint32_t allClipMask = state->polygonAllClipMask;
		if ((allClipMask & SLIP_CLIP_ALL) != 0)
			return true;
		if (calls->clip(calls->context, allClipMask, &anyClipMask))
			return true;
	}
	return calls->finish(calls->context, anyClipMask);
}

bool SlipRenderer_BuildTexturedRing(SlipRendererState *state, uint16_t countAndFlags, uint32_t texture,
                                    const uint8_t *serializedIndices, const SlipRendererTexturedRingCalls *calls) {
	if (calls->reject(calls->ring.context, countAndFlags, serializedIndices))
		return true;
	uint32_t coordinateOffset = (uint32_t)countAndFlags << 1;
	if ((countAndFlags & SLIP_PRIMITIVE_VERTEX_NORMALS) != 0)
		coordinateOffset <<= 2;
	uint32_t count = countAndFlags & SLIP_PRIMITIVE_VERTEX_COUNT_MASK;
	coordinateOffset &= SLIP_PRIMITIVE_VERTEX_COUNT_MASK;
	const uint8_t *coordinates = serializedIndices + coordinateOffset;
	state->polygonTexture = texture;
	state->interpolationFlags = SLIP_INTERPOLATE_TEXTURE_PERSPECTIVE;
	state->polygonDrawMode = SLIP_POLYGON_DRAW_TEXTURED;
	SlipRendererPolygon *const sentinel = state->freePolygons;
	SlipRendererPolygon *const first = sentinel->next;
	SlipRendererPolygon *const following = first->next;
	sentinel->next = following;
	following->previous = sentinel;
	state->activePolygons = first;
	SlipRendererPolygon *current = first;
	state->polygonAllClipMask = SLIP_CLIP_ALL;
	state->polygonAnyClipMask = 0;
	for (;;) {
		const uint16_t index = (uint16_t)((uint16_t)serializedIndices[0] | (uint16_t)serializedIndices[1] << 8);
		SlipDraw3DVertexRecord *const vertex = state->activeVertices + index;
		const uint32_t flags = calls->ring.projectVertex(calls->ring.context, vertex);
		/* REP MOVSD copies exactly the first seven dwords, leaving shade,
		 * texture coordinates, other payload and links untouched. */
		current->point.world = vertex->world;
		current->point.screenX = vertex->screenX;
		current->point.screenY = vertex->screenY;
		current->point.flags = vertex->flags;
		current->point.depth = vertex->depth;
		state->polygonAllClipMask &= flags;
		state->polygonAnyClipMask |= flags;
		current->point.textureU = (uint32_t)coordinates[0] | (uint32_t)coordinates[1] << 8;

		current->point.textureV = (uint32_t)coordinates[2] | (uint32_t)coordinates[3] << 8 |
		                          (uint32_t)coordinates[4] << 16 | (uint32_t)coordinates[5] << 24;
		if (--count == 0)
			break;
		SlipRendererPolygon *const previous = current;
		SlipRendererPolygon *const sentinel = state->freePolygons;
		current = sentinel->next;
		SlipRendererPolygon *const following = current->next;
		sentinel->next = following;
		following->previous = sentinel;
		previous->next = current;
		current->previous = previous;
		serializedIndices += SLIP_SERIALIZED_INDEX_BYTES;
		coordinates += SLIP_SERIALIZED_TEXTURE_COORDINATE_BYTES;
	}
	current->next = first;
	first->previous = current;
	uint32_t anyClipMask = state->polygonAnyClipMask;
	if ((anyClipMask & SLIP_CLIP_ALL) != 0) {
		const uint32_t allClipMask = state->polygonAllClipMask;
		if ((allClipMask & SLIP_CLIP_ALL) != 0)
			return true;
		if (calls->ring.clip(calls->ring.context, allClipMask, &anyClipMask))
			return true;
	}
	return calls->ring.finish(calls->ring.context, anyClipMask);
}

bool SlipRenderer_BuildShadedRing(SlipRendererState *state, SlipRendererPolygon *first,
                                  const uint8_t *serializedIndices, const uint8_t *serializedNormals, uint32_t count,
                                  const SlipRendererSolidRingCalls *calls) {
	SlipRendererPolygon *current = first;
	state->polygonAllClipMask = SLIP_CLIP_ALL;
	state->polygonAnyClipMask = 0;
	for (;;) {
		const uint16_t index = (uint16_t)((uint16_t)serializedIndices[0] | (uint16_t)serializedIndices[1] << 8);
		SlipDraw3DVertexRecord *const vertex = state->activeVertices + index;
		const uint32_t flags = calls->projectVertex(calls->context, vertex);
		/* REP MOVSD copies exactly the first seven dwords, leaving shade,
		 * texture coordinates, other payload and links untouched. */
		current->point.world = vertex->world;
		current->point.screenX = vertex->screenX;
		current->point.screenY = vertex->screenY;
		current->point.flags = vertex->flags;
		current->point.depth = vertex->depth;
		state->polygonAllClipMask &= flags;
		state->polygonAnyClipMask |= flags;

		const int16_t normalZ = (int16_t)((uint16_t)serializedNormals[4] | (uint16_t)serializedNormals[5] << 8);
		const int16_t normalY = (int16_t)((uint16_t)serializedNormals[2] | (uint16_t)serializedNormals[3] << 8);
		const int16_t normalX = (int16_t)((uint16_t)serializedNormals[0] | (uint16_t)serializedNormals[1] << 8);
		const uint32_t shade = calls->shade(calls->context, state->activeMaterial, vertex, normalX, normalY, normalZ);
		current->point.shade = (uint16_t)(((shade & UINT8_MAX) << 8) | ((shade >> 8) & UINT8_MAX));
		if (--count == 0)
			break;
		SlipRendererPolygon *const previous = current;
		SlipRendererPolygon *const sentinel = state->freePolygons;
		current = sentinel->next;
		SlipRendererPolygon *const following = current->next;
		sentinel->next = following;
		following->previous = sentinel;
		previous->next = current;
		current->previous = previous;
		serializedIndices += SLIP_SERIALIZED_INDEX_BYTES;
		serializedNormals += SLIP_SERIALIZED_NORMAL_BYTES;
	}
	current->next = first;
	first->previous = current;
	uint32_t anyClipMask = state->polygonAnyClipMask;
	if ((anyClipMask & SLIP_CLIP_ALL) != 0) {
		const uint32_t allClipMask = state->polygonAllClipMask;
		if ((allClipMask & SLIP_CLIP_ALL) != 0)
			return true;
		if (calls->clip(calls->context, allClipMask, &anyClipMask))
			return true;
	}
	return calls->finish(calls->context, anyClipMask);
}

bool SlipRenderer_RejectPostBounds(const SlipRendererState *state) {
	SlipRendererPolygon *current = state->activePolygons;
	SlipRendererPolygon *const first = current;
	uint32_t all = UINT32_MAX;
	const int32_t minimumX = state->postClipMinX;
	const int32_t maximumX = state->postClipMaxX;
	do {
		uint32_t mask = 0;
		if (current->point.screenX < minimumX)
			mask = SLIP_CLIP_LEFT;
		if (current->point.screenX > maximumX)
			mask = SLIP_CLIP_RIGHT;
		if (current->point.screenY < state->postClipMinY)
			mask |= SLIP_CLIP_TOP;
		if (current->point.screenY > state->postClipMaxY)
			mask |= SLIP_CLIP_BOTTOM;
		all &= mask;
		current = current->next;
	} while (current != first);
	return all != 0;
}

bool SlipRenderer_ClipScreen(SlipRendererState *state, uint32_t anyClipMask, const SlipRendererScreenClipCalls *calls) {
	if ((anyClipMask & SLIP_CLIP_SCREEN) != 0) {
		state->polygonAnyClipMask = anyClipMask;
		if ((anyClipMask & SLIP_CLIP_LEFT) != 0) {
			SlipRendererClipEdges edges;
			if (calls->edges(calls->context, SLIP_CLIP_LEFT, &edges))
				return true;
			calls->horizontal(calls->context, edges.firstTarget, edges.firstInside, state->projection.minX);
			calls->horizontal(calls->context, edges.secondTarget, edges.secondInside, state->projection.minX);
		}
		if ((state->polygonAnyClipMask & SLIP_CLIP_RIGHT) != 0) {
			SlipRendererClipEdges edges;
			if (calls->edges(calls->context, SLIP_CLIP_RIGHT, &edges))
				return true;
			calls->horizontal(calls->context, edges.firstTarget, edges.firstInside, state->projection.maxX);
			calls->horizontal(calls->context, edges.secondTarget, edges.secondInside, state->projection.maxX);
		}
		SlipRendererPolygonMasks masks = SlipRenderer_ActivePolygonMasks(state);
		if ((masks.all & SLIP_CLIP_VERTICAL) != 0)
			return true;
		state->polygonAnyClipMask = masks.any;
		if ((masks.any & SLIP_CLIP_TOP) != 0) {
			SlipRendererClipEdges edges;
			if (calls->edges(calls->context, SLIP_CLIP_TOP, &edges))
				return true;
			calls->vertical(calls->context, edges.firstTarget, edges.firstInside, state->projection.minY);
			calls->vertical(calls->context, edges.secondTarget, edges.secondInside, state->projection.minY);
		}
		if ((state->polygonAnyClipMask & SLIP_CLIP_BOTTOM) != 0) {
			SlipRendererClipEdges edges;
			if (calls->edges(calls->context, SLIP_CLIP_BOTTOM, &edges))
				return true;
			calls->vertical(calls->context, edges.firstTarget, edges.firstInside, state->projection.maxY);
			calls->vertical(calls->context, edges.secondTarget, edges.secondInside, state->projection.maxY);
		}
	}
	if (state->postClipPlanes != NULL) {
		if (SlipRenderer_RejectPostBounds(state))
			return true;
		if (calls->postPlanes(calls->context))
			return true;
	}
	return false;
}

bool SlipRenderer_PrepareClipEdges(SlipRendererState *state, uint32_t planeMask, SlipRendererClipEdges *edges) {
	SlipRendererPolygon *outside = state->activePolygons;
	SlipRendererPolygon *inside;
	for (;;) {
		inside = outside->next;
		if ((outside->point.flags & planeMask) != 0 && (inside->point.flags & planeMask) == 0)
			break;
		outside = inside;
	}
	SlipRendererPolygon *cursor = inside;
	SlipRendererPolygon *following;
	do {
		following = cursor->next;
		if ((cursor->point.flags & planeMask) != 0)
			break;
		cursor = following;
	} while (true);
	do {
		following = cursor->next;
		if ((cursor->point.flags & planeMask) == 0)
			break;
		cursor = following;
	} while (true);
	if (cursor != inside)
		return true;
	SlipRendererPolygon *previous = outside->previous;
	if ((previous->point.flags & planeMask) == 0) {
		SlipRendererPolygon *const sentinel = state->freePolygons;
		SlipRendererPolygon *const borrowed = sentinel->next;
		SlipRendererPolygon *const nextFree = borrowed->next;
		sentinel->next = nextFree;
		nextFree->previous = sentinel;
		state->borrowedClipPoint = borrowed;
		borrowed->next = outside;
		previous = outside->previous;
		outside->previous = borrowed;
		previous->next = borrowed;
		borrowed->previous = previous;
		borrowed->point = outside->point;
		edges->secondInside = previous;
		edges->secondTarget = state->borrowedClipPoint;
	} else {
		SlipRendererPolygon *moving = previous;
		for (;;) {
			previous = moving->previous;
			if ((previous->point.flags & planeMask) == 0)
				break;
			SlipRendererPolygon *const next = moving->next;
			SlipRendererPolygon *const prior = moving->previous;
			prior->next = next;
			next->previous = prior;
			SlipRendererPolygon *const sentinel = state->freePolygons;
			SlipRendererPolygon *const firstFree = sentinel->next;
			sentinel->next = moving;
			firstFree->previous = moving;
			moving->next = firstFree;
			moving->previous = sentinel;
			moving = previous;
		}
		edges->secondTarget = moving;
		edges->secondInside = previous;
	}
	edges->firstTarget = outside;
	edges->firstInside = inside;
	state->activePolygons = inside;
	return false;
}

bool SlipRenderer_ClipPostPlanes(SlipRendererState *state) {
	SlipRendererPolygon *plane = state->postClipPlanes;
	do {
		SlipRendererPolygon *point = state->activePolygons;
		state->polygonAllClipMask = UINT32_MAX;
		state->polygonAnyClipMask = 0;
		do {
			uint32_t mask = 0;
			const int16_t differenceX = (int16_t)((uint32_t)point->point.screenX - (uint32_t)plane->point.screenX);
			const int32_t productX = (int32_t)differenceX * (int16_t)plane->point.planeNormalX;
			const int16_t differenceY = (int16_t)((uint32_t)point->point.screenY - (uint32_t)plane->point.screenY);
			const int32_t productY = (int32_t)differenceY * (int16_t)plane->point.planeNormalY;
			const int32_t distance =
			    (int32_t)(((uint32_t)productY + (uint32_t)productX) << SLIP_POST_PLANE_DISTANCE_SHIFT);
			point->point.screenPlaneDistance = distance;
			if (distance < 0)
				mask = SLIP_CLIP_AUXILIARY;
			state->polygonAllClipMask &= mask;
			state->polygonAnyClipMask |= mask;
			point->point.flags = mask;
			point = point->next;
		} while (point != state->activePolygons);
		if (state->polygonAllClipMask != 0)
			return true;
		if (state->polygonAnyClipMask != 0) {
			SlipRendererClipEdges edges;
			if (SlipRenderer_PrepareClipEdges(state, SLIP_CLIP_AUXILIARY, &edges))
				return true;
			SlipRenderer_InterpolatePostPlane(edges.firstTarget, edges.firstInside);
			SlipRenderer_InterpolatePostPlane(edges.secondTarget, edges.secondInside);
		}
		plane = plane->next;
	} while (plane != state->postClipPlanes);
	return false;
}

bool SlipRenderer_BuildSolidPolygon(SlipRendererState *state, uint32_t countAndFlags, int16_t normalX, int16_t normalY,
                                    uint16_t materialIndex, const uint8_t *serializedIndices,
                                    const SlipRendererSolidBuilderCalls *calls) {
	SlipDraw3DMaterialTable *const table = state->materials->table;
	SlipDraw3DMaterialRecord *const material =
	    table->records + (materialIndex < (uint16_t)table->count ? materialIndex : 0);
	return SlipRenderer_BuildMaterialPolygon(state, material, countAndFlags, normalX, normalY, serializedIndices,
	                                         calls);
}

void SlipRenderer_DrawUnclipped(SlipRendererState *state, uint32_t countAndFlags, int16_t normalX, int16_t normalY,
                                uint16_t materialIndex, const uint8_t *serializedIndices,
                                const SlipRendererUnclippedCalls *calls) {
	SlipDraw3DMaterialTable *const table = state->materials->table;
	SlipDraw3DMaterialRecord *const material =
	    table->records + (materialIndex < (uint16_t)table->count ? materialIndex : 0);
	SlipRenderer_DrawUnclippedMaterial(state, material, countAndFlags, normalX, normalY, serializedIndices, calls);
}

bool SlipRenderer_BuildMaterialPolygon(SlipRendererState *state, SlipDraw3DMaterialRecord *material,
                                       uint32_t countAndFlags, int16_t normalX, int16_t normalY,
                                       const uint8_t *serializedIndices, const SlipRendererSolidBuilderCalls *calls) {
	if (material->skipFlatPolygon != 0)
		return true;
	state->activeMaterial = material;
	if (material->vertexShading != 0 && (state->projection.renderFlags & SLIP_RENDER_DISABLE_VERTEX_SHADING) == 0 &&
	    (countAndFlags & SLIP_PRIMITIVE_VERTEX_NORMALS) != 0) {
		const uint32_t vertexCount = countAndFlags & SLIP_PRIMITIVE_VERTEX_COUNT_MASK;
		const uint8_t *const serializedNormals = serializedIndices + vertexCount * SLIP_SERIALIZED_INDEX_BYTES;
		state->interpolationFlags = SLIP_INTERPOLATE_SHADE;
		state->polygonDrawMode = SLIP_POLYGON_DRAW_SHADED;
		SlipRendererPolygon *const sentinel = state->freePolygons;
		SlipRendererPolygon *const first = sentinel->next;
		SlipRendererPolygon *const following = first->next;
		sentinel->next = following;
		following->previous = sentinel;
		state->activePolygons = first;
		return SlipRenderer_BuildShadedRing(state, first, serializedIndices, serializedNormals, vertexCount,
		                                    &calls->ring);
	}
	state->polygonDrawMode = SLIP_POLYGON_DRAW_FLAT;
	if (material->ditherBits != 0)
		state->polygonDrawMode = SLIP_POLYGON_DRAW_DITHERED;
	const uint32_t vertexCount = (uint16_t)countAndFlags & SLIP_PRIMITIVE_VERTEX_COUNT_MASK;
	SlipRendererPolygonColour colour =
	    calls->colour(calls->ring.context, material, normalX, normalY, vertexCount, countAndFlags, serializedIndices);
	state->polygonColourAuxiliary = colour.shade;
	state->polygonColour = colour.colour;
	state->interpolationFlags = 0;
	SlipRendererPolygon *const sentinel = state->freePolygons;
	SlipRendererPolygon *const first = sentinel->next;
	SlipRendererPolygon *const following = first->next;
	sentinel->next = following;
	following->previous = sentinel;
	state->activePolygons = first;
	return SlipRenderer_BuildSolidRing(state, first, serializedIndices, vertexCount, &calls->ring);
}

SlipRendererPolygonColour SlipRenderer_FlatColour(SlipDraw3DMaterialRecord *material, int16_t normalX, int16_t normalY,
                                                  int16_t normalZ, uint32_t countAndFlags,
                                                  const uint8_t *serializedIndices,
                                                  const SlipDraw3DVertexLighting *lighting,
                                                  const SlipRendererFlatColourCalls *calls) {
	bool useDepthFade = lighting->fadeStart != 0;
	uint32_t depth = 0;
	if (useDepthFade)
		depth = calls->polygonDepth(calls->context, (uint16_t)countAndFlags, serializedIndices);
	uint16_t diffuse = (uint16_t)normalY;
	if (material->fixedShade == 0 && material->diffuseCoefficient != 0) {
		diffuse = 0;
		if (lighting->direct != 0) {
			/* Three signed word products; only the low word after SHRD reaches CWDE. */
			const int64_t productSum = (int64_t)normalX * (int16_t)lighting->light.x +
			                           (int64_t)normalY * (int16_t)lighting->light.y +
			                           (int64_t)normalZ * (int16_t)lighting->light.z;
			const int32_t intensity = -(int32_t)(int16_t)((uint64_t)productSum >> SLIP_NORMAL_FRACTION_BITS);
			if (intensity >= 0)
				diffuse = (uint16_t)(((uint32_t)(uint16_t)intensity * (uint16_t)lighting->direct) >>
				                     SLIP_NORMAL_FRACTION_BITS);
		}
	}
	uint32_t blend = 0;
	if (useDepthFade)
		blend = calls->depthFadeBlend(calls->context, depth);
	const uint32_t shade = calls->lighting(calls->context, material, blend, diffuse, 0);
	uint32_t rampStart, rampEnd;
	if (lighting->overrideRamp == 0) {
		rampStart = material->rampStart;
		rampEnd = material->rampEnd;
	} else {
		rampStart = lighting->rampStart;
		rampEnd = lighting->rampEnd;
	}
	const uint32_t rampDifference = rampEnd - rampStart;
	const uint32_t product = (uint32_t)(uint16_t)rampDifference * (uint16_t)shade;
	const uint32_t scaledRamp =
	    (rampDifference & ~(uint32_t)UINT16_MAX) | (uint16_t)(product >> SLIP_NORMAL_FRACTION_BITS);
	return (SlipRendererPolygonColour){scaledRamp + rampStart, shade};
}

void SlipRenderer_DrawPolygon(SlipRendererState *state, const SlipRendererRasterCalls *calls) {
	bool reverse = state->reverseRasterTraversal != 0;
	const uint32_t mode = state->polygonDrawMode;
	if (mode == SLIP_POLYGON_DRAW_LINE) {
		SlipRendererPolygon *const first = state->activePolygons;
		SlipRendererPolygon *const second = first->next;
		calls->line(calls->context, first->point.screenX, first->point.screenY, second->point.screenX,
		            second->point.screenY, state->polygonColour);
		return;
	}
	if ((mode == SLIP_POLYGON_DRAW_FLAT || mode == SLIP_POLYGON_DRAW_DITHERED) &&
	    (state->projection.renderFlags & SLIP_RENDER_WIREFRAME) != 0) {
		SlipRendererPolygon *const first = state->activePolygons;
		SlipRendererPolygon *current = first;
		do {
			SlipRendererPolygon *const next = current->next;
			calls->line(calls->context, current->point.screenX, current->point.screenY, next->point.screenX,
			            next->point.screenY, state->polygonColour);
			current = next;
		} while (current != first);
		return;
	}
	RasterTexturedPoint *destination = state->points;
	SlipRendererPolygon *const first = state->activePolygons;
	SlipRendererPolygon *current = first;
	uint32_t count = 0;
	if (mode == SLIP_POLYGON_DRAW_SHADED) {
		const uint16_t firstColour = first->point.shade >> SLIP_SHADE_COLOUR_SHIFT;
		bool varied = false;
		uint16_t lastShade;
		do {
			destination->x = current->point.screenX;
			destination->y = current->point.screenY;
			lastShade = current->point.shade;
			destination->reserved08 = lastShade;
			if ((lastShade >> SLIP_SHADE_COLOUR_SHIFT) != firstColour)
				varied = true;
			current = current->next;
			++destination;
			++count;
		} while (current != first);
		if (varied)
			calls->shaded(calls->context, state->points, count);
		else
			calls->flat(calls->context, state->points, count, lastShade >> SLIP_SHADE_COLOUR_SHIFT);
		return;
	}
	if (mode == SLIP_POLYGON_DRAW_FLAT || mode == SLIP_POLYGON_DRAW_DITHERED) {
		do {
			destination->x = current->point.screenX;
			destination->y = current->point.screenY;
			current = reverse ? current->previous : current->next;
			++destination;
			++count;
		} while (current != first);
		const uint32_t colour = state->polygonColour;
		RasterTexturedPoint *const points = state->points;
		if (mode == SLIP_POLYGON_DRAW_DITHERED) {
			calls->dithered(calls->context, points, count, colour, state->activeMaterial->ditherBits);
			return;
		}
		if (count == SLIP_POLYGON_RECTANGLE_VERTICES) {
			bool rectangle;
			if (points[0].x == points[1].x)
				rectangle = points[0].y == points[3].y && points[2].y == points[1].y && points[2].x == points[3].x;
			else
				rectangle = points[0].y == points[1].y && points[0].x == points[3].x && points[2].x == points[1].x &&
				            points[2].y == points[3].y;
			if (rectangle) {
				uint32_t minX = (uint32_t)points[2].x, maxX = (uint32_t)points[0].x;
				uint32_t minY = (uint32_t)points[2].y, maxY = (uint32_t)points[0].y;
				if (minX > maxX) {
					const uint32_t saved = minX;
					minX = maxX;
					maxX = saved;
				}
				if (minY > maxY) {
					const uint32_t saved = minY;
					minY = maxY;
					maxY = saved;
				}
				calls->rectangle(calls->context, minX, minY, maxX, maxY, colour);
				return;
			}
		}
		calls->flat(calls->context, points, count, colour);
		return;
	}
	do {
		destination->x = current->point.screenX;
		destination->y = current->point.screenY;
		destination->u = (uint16_t)current->point.textureU;
		destination->v = (uint16_t)current->point.textureV;
		destination->depth = current->point.world.z;
		current = reverse ? current->previous : current->next;
		++destination;
		++count;
	} while (current != first);
	const uint32_t texture = state->polygonTexture;
	RasterTexturedPoint *const points = state->points;
	if ((state->projection.renderFlags & SLIP_RENDER_ALTERNATE_TEXTURE_RASTER) != 0) {
		if ((state->projection.renderFlags & SLIP_RENDER_MASKED_TEXTURE) == 0)
			calls->opaqueAffine(calls->context, points, count, texture);
		else
			calls->maskedAffine(calls->context, points, count, texture);
	} else {
		if ((state->projection.renderFlags & SLIP_RENDER_MASKED_TEXTURE) == 0)
			calls->opaquePerspective(calls->context, points, count, texture);
		else
			calls->maskedPerspective(calls->context, points, count, texture);
	}
}

void SlipRenderer_DrawUnclippedMaterial(SlipRendererState *state, SlipDraw3DMaterialRecord *material,
                                        uint32_t countAndFlags, int16_t normalX, int16_t normalY,
                                        const uint8_t *serializedIndices, const SlipRendererUnclippedCalls *calls) {
	if (material->vertexShading != 0 && (state->projection.renderFlags & SLIP_RENDER_DISABLE_VERTEX_SHADING) == 0 &&
	    (countAndFlags & SLIP_PRIMITIVE_VERTEX_NORMALS) != 0) {
		state->activeMaterial = material;
		const uint32_t count = countAndFlags & SLIP_PRIMITIVE_VERTEX_COUNT_MASK;
		const uint8_t *serializedNormals = serializedIndices + count * SLIP_SERIALIZED_INDEX_BYTES;
		uint32_t remaining = count;
		RasterTexturedPoint *destination = state->points;
		do {
			const uint16_t index =
			    (uint16_t)((uint16_t)serializedIndices[0] | (uint16_t)serializedIndices[1] << SLIP_BYTE_BITS);
			SlipDraw3DVertexRecord *const vertex = state->activeVertices + index;
			RasterPoint screen = calls->project(calls->context, vertex);
			destination->x = screen.x;
			destination->y = screen.y;
			const int16_t normalZ =
			    (int16_t)((uint16_t)serializedNormals[SLIP_SERIALIZED_NORMAL_Z_OFFSET] |
			              (uint16_t)serializedNormals[SLIP_SERIALIZED_NORMAL_Z_OFFSET + 1] << SLIP_BYTE_BITS);
			const int16_t vertexNormalY =
			    (int16_t)((uint16_t)serializedNormals[SLIP_SERIALIZED_NORMAL_Y_OFFSET] |
			              (uint16_t)serializedNormals[SLIP_SERIALIZED_NORMAL_Y_OFFSET + 1] << SLIP_BYTE_BITS);
			const int16_t vertexNormalX =
			    (int16_t)((uint16_t)serializedNormals[SLIP_SERIALIZED_NORMAL_X_OFFSET] |
			              (uint16_t)serializedNormals[SLIP_SERIALIZED_NORMAL_X_OFFSET + 1] << SLIP_BYTE_BITS);
			const uint32_t colour =
			    calls->shade(calls->context, state->activeMaterial, vertex, vertexNormalX, vertexNormalY, normalZ);
			const uint16_t shade = (uint16_t)colour;
			destination->reserved08 = (colour & ~(uint32_t)UINT16_MAX) | (uint16_t)((shade << SLIP_SHADE_COLOUR_SHIFT) |
			                                                                        (shade >> SLIP_SHADE_COLOUR_SHIFT));
			if (--remaining == 0)
				break;
			++destination;
			serializedIndices += SLIP_SERIALIZED_INDEX_BYTES;
			serializedNormals += SLIP_SERIALIZED_NORMAL_BYTES;
		} while (true);
		calls->raster.shaded(calls->raster.context, state->points, count);
	} else {
		const uint32_t count = countAndFlags & SLIP_PRIMITIVE_VERTEX_COUNT_MASK;
		SlipRendererPolygonColour colour =
		    calls->colour(calls->context, material, normalX, normalY, count, countAndFlags, serializedIndices);
		state->polygonColourAuxiliary = colour.shade;
		state->polygonColour = colour.colour;
		uint32_t remaining = count;
		RasterTexturedPoint *destination = state->points;
		do {
			const uint16_t index =
			    (uint16_t)((uint16_t)serializedIndices[0] | (uint16_t)serializedIndices[1] << SLIP_BYTE_BITS);
			RasterPoint screen = calls->project(calls->context, state->activeVertices + index);
			destination->x = screen.x;
			destination->y = screen.y;
			serializedIndices += SLIP_SERIALIZED_INDEX_BYTES;
			++destination;
		} while (--remaining != 0);
		calls->raster.flat(calls->raster.context, state->points, count, state->polygonColour);
	}
}

bool SlipRenderer_BuildLine(SlipRendererState *state, SlipDraw3DVec32 first, SlipDraw3DVec32 second, uint32_t colour,
                            const SlipRendererLineCalls *calls) {
	state->interpolationFlags = 0;
	state->polygonDrawMode = SLIP_POLYGON_DRAW_LINE;
	state->polygonColour = colour;
	SlipRendererPolygon *const firstPoint = state->freePolygons->next;
	state->freePolygons->next = firstPoint->next;
	firstPoint->next->previous = state->freePolygons;
	state->activePolygons = firstPoint;
	state->polygonAllClipMask = SLIP_CLIP_ALL;
	state->polygonAnyClipMask = 0;
	uint32_t flags = calls->project(calls->context, &firstPoint->point, first);
	state->polygonAllClipMask &= flags;
	state->polygonAnyClipMask |= flags;
	SlipRendererPolygon *const secondPoint = state->freePolygons->next;
	state->freePolygons->next = secondPoint->next;
	secondPoint->next->previous = state->freePolygons;
	firstPoint->next = secondPoint;
	secondPoint->previous = firstPoint;
	flags = calls->project(calls->context, &secondPoint->point, second);
	state->polygonAllClipMask &= flags;
	state->polygonAnyClipMask |= flags;
	secondPoint->next = firstPoint;
	firstPoint->previous = secondPoint;
	if ((state->polygonAnyClipMask & SLIP_CLIP_ALL) == 0)
		return false;
	if ((state->polygonAllClipMask & SLIP_CLIP_ALL) != 0)
		return true;
	return calls->clip(calls->context, state->polygonAllClipMask, state->polygonAnyClipMask);
}

SlipRendererLineEdge SlipRenderer_LineEdge(SlipRendererState *state, uint16_t mask) {
	SlipRendererLineEdge edge = {state->activePolygons, state->activePolygons->next};
	if ((edge.target->point.flags & mask) == 0) {
		SlipRendererPolygon *const temporary = edge.target;
		edge.target = edge.inside;
		edge.inside = temporary;
	}
	return edge;
}

bool SlipRenderer_ClipLine(SlipRendererState *state, uint32_t anyClipMask) {
	SlipRendererLineEdge edge;
	SlipRendererPolygonMasks masks;
	if ((state->shapeFlags & SLIP_SHAPE_CLIP_AUXILIARY) != 0 && (anyClipMask & SLIP_CLIP_AUXILIARY) != 0) {
		edge = SlipRenderer_LineEdge(state, SLIP_CLIP_AUXILIARY);
		SlipRenderer_InterpolateAuxiliary(state, edge.target, edge.inside);
		masks = SlipRenderer_ActivePolygonMasks(state);
		anyClipMask = masks.any;
		if ((masks.all & SLIP_CLIP_ALL) != 0)
			return true;
	}
	if ((anyClipMask & SLIP_CLIP_NEAR) != 0) {
		edge = SlipRenderer_LineEdge(state, SLIP_CLIP_NEAR);
		SlipRenderer_InterpolateDepth(state, edge.target, edge.inside, state->projection.minZ);
	}
	if ((anyClipMask & SLIP_CLIP_FAR) != 0) {
		edge = SlipRenderer_LineEdge(state, SLIP_CLIP_FAR);
		SlipRenderer_InterpolateDepth(state, edge.target, edge.inside, state->projection.maxZ);
	}
	masks = SlipRenderer_ActivePolygonMasks(state);
	if ((masks.all & SLIP_CLIP_SCREEN) != 0)
		return true;
	state->polygonAnyClipMask = masks.any;
	if ((masks.any & SLIP_CLIP_LEFT) != 0) {
		edge = SlipRenderer_LineEdge(state, SLIP_CLIP_LEFT);
		SlipRenderer_InterpolateHorizontal(state, edge.target, edge.inside, state->projection.minX);
	}
	if ((state->polygonAnyClipMask & SLIP_CLIP_RIGHT) != 0) {
		edge = SlipRenderer_LineEdge(state, SLIP_CLIP_RIGHT);
		SlipRenderer_InterpolateHorizontal(state, edge.target, edge.inside, state->projection.maxX);
	}
	masks = SlipRenderer_ActivePolygonMasks(state);
	if ((masks.all & SLIP_CLIP_VERTICAL) != 0)
		return true;
	state->polygonAnyClipMask = masks.any;
	if ((masks.any & SLIP_CLIP_TOP) != 0) {
		edge = SlipRenderer_LineEdge(state, SLIP_CLIP_TOP);
		SlipRenderer_InterpolateVertical(state, edge.target, edge.inside, state->projection.minY);
	}
	if ((state->polygonAnyClipMask & SLIP_CLIP_BOTTOM) != 0) {
		edge = SlipRenderer_LineEdge(state, SLIP_CLIP_BOTTOM);
		SlipRenderer_InterpolateVertical(state, edge.target, edge.inside, state->projection.maxY);
	}
	return false;
}

uint32_t SlipRenderer_ProjectPoint(SlipRendererState *state, SlipRendererPolygonPoint *point, SlipDraw3DVec32 world) {
	uint32_t flags = SLIP_VERTEX_TRANSFORMED;
	if (world.z < state->projection.minZ)
		flags |= SLIP_CLIP_NEAR;
	if (world.z > state->projection.maxZ)
		flags |= SLIP_CLIP_FAR;
	if ((state->shapeFlags & SLIP_SHAPE_CLIP_AUXILIARY) != 0) {
		const int32_t x = (int32_t)((uint32_t)world.x - (uint32_t)state->projection.auxiliaryClipPlaneOrigin.x);
		const int32_t y = (int32_t)((uint32_t)world.y - (uint32_t)state->projection.auxiliaryClipPlaneOrigin.y);
		const int32_t z = (int32_t)((uint32_t)world.z - (uint32_t)state->projection.auxiliaryClipPlaneOrigin.z);
		uint64_t dot = (uint64_t)((int64_t)x * state->projection.auxiliaryClipPlaneNormal.x);
		dot += (uint64_t)((int64_t)y * state->projection.auxiliaryClipPlaneNormal.y);
		dot += (uint64_t)((int64_t)z * state->projection.auxiliaryClipPlaneNormal.z);
		point->depth =
		    (int32_t)((uint32_t)(dot >> SLIP_NORMAL_FRACTION_BITS) + (uint32_t)((dot >> SLIP_NORMAL_ROUND_BIT) & 1u));
		if (point->depth < 0)
			flags |= SLIP_CLIP_AUXILIARY;
	}
	point->world = world;
	if ((flags & SLIP_CLIP_BEFORE_PROJECTION) != 0) {
		point->flags = flags;
		return flags;
	}
	int32_t x, y;
	if ((state->shapeFlags & SLIP_SHAPE_FORCE_SECONDARY_PROJECTION) != 0)
		state->projection.projectSecondary(world, &x, &y, &state->projection);
	else
		state->projection.projectPrimary(world, &x, &y, &state->projection);
	flags = SLIP_VERTEX_PROJECTED;
	if (x < state->projection.minX)
		flags |= SLIP_CLIP_LEFT;
	if (x > state->projection.maxX)
		flags |= SLIP_CLIP_RIGHT;
	if (y < state->projection.minY)
		flags |= SLIP_CLIP_TOP;
	if (y > state->projection.maxY)
		flags |= SLIP_CLIP_BOTTOM;
	if ((flags & SLIP_CLIP_SCREEN) != 0 && x < SLIP_SCREEN_CLIP_COORDINATE_LIMIT &&
	    y < SLIP_SCREEN_CLIP_COORDINATE_LIMIT && x > -SLIP_SCREEN_CLIP_COORDINATE_LIMIT &&
	    y > -SLIP_SCREEN_CLIP_COORDINATE_LIMIT)
		flags |= SLIP_VERTEX_SCREEN_CLIP_IN_RANGE;
	point->flags = flags;
	point->screenX = x;
	point->screenY = y;
	return flags;
}

bool SlipRenderer_DrawLine(SlipRendererState *state, SlipDraw3DVec32 first, SlipDraw3DVec32 second, uint32_t colour,
                           const SlipRendererLineCalls *line, const SlipRendererRasterCalls *raster) {
	SlipRenderer_ReturnActivePolygons(state);
	if (SlipRenderer_BuildLine(state, first, second, colour, line))
		return true;
	SlipRenderer_DrawPolygon(state, raster);
	return false;
}
