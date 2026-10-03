#include "renderer_bounds.h"

void SlipRenderer_SetBounds(SlipRendererState *state, SlipView3DVec32 minimum, SlipView3DVec32 maximum) {
	state->boundingBox[SLIP_VIEW_BOX_MIN_X] = minimum.x;
	state->boundingBox[SLIP_VIEW_BOX_MIN_Y] = minimum.y;
	state->boundingBox[SLIP_VIEW_BOX_MIN_Z] = minimum.z;
	state->boundingBox[SLIP_VIEW_BOX_MAX_X] = maximum.x;
	state->boundingBox[SLIP_VIEW_BOX_MAX_Y] = maximum.y;
	state->boundingBox[SLIP_VIEW_BOX_MAX_Z] = maximum.z;
}

void SlipRenderer_ClipBoxCorner(SlipRendererState *state, const SlipDraw3DProjectState *projection,
                                SlipDraw3DVec32 point) {
	uint32_t mask = projection->projectMask(point, projection);
	if (point.z < projection->minZ)
		mask |= SLIP_BOX_CLIP_NEAR;
	if (point.z > projection->maxZ)
		mask |= SLIP_BOX_CLIP_FAR;
	if (projection->auxiliaryClipPlaneEnabled != 0) {
		SlipDraw3DVec32 origin = projection->auxiliaryClipPlaneOrigin;
		SlipDraw3DVec32 normal = projection->auxiliaryClipPlaneNormal;
		const int32_t x = (int32_t)((uint32_t)point.x - (uint32_t)origin.x);
		const int32_t y = (int32_t)((uint32_t)point.y - (uint32_t)origin.y);
		const int32_t z = (int32_t)((uint32_t)point.z - (uint32_t)origin.z);
		uint64_t sum = (uint64_t)((int64_t)x * normal.x);
		sum += (uint64_t)((int64_t)y * normal.y);
		sum += (uint64_t)((int64_t)z * normal.z);
		/* SHRD 14 followed by ADC 0, retaining the low dword. */
		const int32_t distance =
		    (int32_t)((uint32_t)(sum >> SLIP_NORMAL_FRACTION_BITS) + (uint32_t)((sum >> SLIP_NORMAL_ROUND_BIT) & 1u));
		if (distance < 0)
			mask |= SLIP_BOX_CLIP_AUXILIARY;
	}
	state->boxAllClipMask &= mask;
	state->boxAnyClipMask |= mask;
}

SlipActorShapeBounds SlipRenderer_ProjectBounds(SlipRendererState *state, SlipView3DVec32 translation,
                                                const SlipView3DMatrix *matrix,
                                                const SlipDraw3DProjectState *projection) {
	state->boxAllClipMask = UINT32_MAX;
	SlipView3D_BuildBoxCorners(matrix, state->boundingBox, translation);

	SlipRenderer_ClipBoxCorner(state, projection,
	                           (SlipDraw3DVec32){state->boundingBox[SLIP_VIEW_BOX_MIN_MIN_MIN],
	                                             state->boundingBox[SLIP_VIEW_BOX_MIN_MIN_MIN + 1],
	                                             state->boundingBox[SLIP_VIEW_BOX_MIN_MIN_MIN + 2]});
	SlipRenderer_ClipBoxCorner(state, projection,
	                           (SlipDraw3DVec32){state->boundingBox[SLIP_VIEW_BOX_MAX_MIN_MIN],
	                                             state->boundingBox[SLIP_VIEW_BOX_MAX_MIN_MIN + 1],
	                                             state->boundingBox[SLIP_VIEW_BOX_MAX_MIN_MIN + 2]});
	SlipRenderer_ClipBoxCorner(state, projection,
	                           (SlipDraw3DVec32){state->boundingBox[SLIP_VIEW_BOX_MAX_MAX_MIN],
	                                             state->boundingBox[SLIP_VIEW_BOX_MAX_MAX_MIN + 1],
	                                             state->boundingBox[SLIP_VIEW_BOX_MAX_MAX_MIN + 2]});
	SlipRenderer_ClipBoxCorner(state, projection,
	                           (SlipDraw3DVec32){state->boundingBox[SLIP_VIEW_BOX_MIN_MAX_MIN],
	                                             state->boundingBox[SLIP_VIEW_BOX_MIN_MAX_MIN + 1],
	                                             state->boundingBox[SLIP_VIEW_BOX_MIN_MAX_MIN + 2]});
	SlipRenderer_ClipBoxCorner(state, projection,
	                           (SlipDraw3DVec32){state->boundingBox[SLIP_VIEW_BOX_MIN_MIN_MAX],
	                                             state->boundingBox[SLIP_VIEW_BOX_MIN_MIN_MAX + 1],
	                                             state->boundingBox[SLIP_VIEW_BOX_MIN_MIN_MAX + 2]});
	SlipRenderer_ClipBoxCorner(state, projection,
	                           (SlipDraw3DVec32){state->boundingBox[SLIP_VIEW_BOX_MAX_MIN_MAX],
	                                             state->boundingBox[SLIP_VIEW_BOX_MAX_MIN_MAX + 1],
	                                             state->boundingBox[SLIP_VIEW_BOX_MAX_MIN_MAX + 2]});
	SlipRenderer_ClipBoxCorner(state, projection,
	                           (SlipDraw3DVec32){state->boundingBox[SLIP_VIEW_BOX_MAX_MAX_MAX],
	                                             state->boundingBox[SLIP_VIEW_BOX_MAX_MAX_MAX + 1],
	                                             state->boundingBox[SLIP_VIEW_BOX_MAX_MAX_MAX + 2]});
	SlipRenderer_ClipBoxCorner(state, projection,
	                           (SlipDraw3DVec32){state->boundingBox[SLIP_VIEW_BOX_MIN_MAX_MAX],
	                                             state->boundingBox[SLIP_VIEW_BOX_MIN_MAX_MAX + 1],
	                                             state->boundingBox[SLIP_VIEW_BOX_MIN_MAX_MAX + 2]});
	if (state->boxAllClipMask != 0)
		return SLIP_ACTOR_SHAPE_OUTSIDE;
	if (state->boxAnyClipMask != 0)
		return SLIP_ACTOR_SHAPE_INTERSECTS;
	return SLIP_ACTOR_SHAPE_INSIDE;
}
