#include "renderer_bounds.h"

void SlipRenderer_SetBounds(SlipRendererState *state, SlipView3DVec32 minimum, SlipView3DVec32 maximum) {
	state->boundingBox[0] = minimum.x;
	state->boundingBox[1] = minimum.y;
	state->boundingBox[2] = minimum.z;
	state->boundingBox[3] = maximum.x;
	state->boundingBox[4] = maximum.y;
	state->boundingBox[5] = maximum.z;
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

	SlipRenderer_ClipBoxCorner(
	    state, projection, (SlipDraw3DVec32){state->boundingBox[18], state->boundingBox[19], state->boundingBox[20]});
	SlipRenderer_ClipBoxCorner(
	    state, projection, (SlipDraw3DVec32){state->boundingBox[27], state->boundingBox[28], state->boundingBox[29]});
	SlipRenderer_ClipBoxCorner(
	    state, projection, (SlipDraw3DVec32){state->boundingBox[24], state->boundingBox[25], state->boundingBox[26]});
	SlipRenderer_ClipBoxCorner(
	    state, projection, (SlipDraw3DVec32){state->boundingBox[21], state->boundingBox[22], state->boundingBox[23]});
	SlipRenderer_ClipBoxCorner(state, projection,
	                           (SlipDraw3DVec32){state->boundingBox[6], state->boundingBox[7], state->boundingBox[8]});
	SlipRenderer_ClipBoxCorner(
	    state, projection, (SlipDraw3DVec32){state->boundingBox[15], state->boundingBox[16], state->boundingBox[17]});
	SlipRenderer_ClipBoxCorner(
	    state, projection, (SlipDraw3DVec32){state->boundingBox[12], state->boundingBox[13], state->boundingBox[14]});
	SlipRenderer_ClipBoxCorner(
	    state, projection, (SlipDraw3DVec32){state->boundingBox[9], state->boundingBox[10], state->boundingBox[11]});
	if (state->boxAllClipMask != 0)
		return SLIP_ACTOR_SHAPE_OUTSIDE;
	if (state->boxAnyClipMask != 0)
		return SLIP_ACTOR_SHAPE_INTERSECTS;
	return SLIP_ACTOR_SHAPE_INSIDE;
}
