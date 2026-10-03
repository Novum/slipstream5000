#include "clip.h"
#include "renderer_flags.h"
#include <math.h>

/* Screen-edge clipping must interpolate reciprocal depth, not depth itself. */
void SlipRaceGpu_ClipAttributes(SlipDraw3DDrawRecord *target, const SlipDraw3DDrawRecord *other, uint32_t mode,
                                double screenRatio) {
	double ratio = screenRatio;
	if ((mode & SLIP_INTERPOLATE_PERSPECTIVE_DEPTH) && target->world.z > 0 && other->world.z > 0) {
		double a = target->world.z, b = other->world.z;
		double inverseDepth = (1 - ratio) / a + ratio / b;
		ratio = (ratio / b) / inverseDepth;
		target->world.z = (int32_t)llround(1 / inverseDepth);
	}
	if (mode & SLIP_INTERPOLATE_TEXTURE) {
		target->textureU =
		    (uint32_t)llround((double)target->textureU + ((double)other->textureU - target->textureU) * ratio);
		target->textureV =
		    (uint32_t)llround((double)target->textureV + ((double)other->textureV - target->textureV) * ratio);
	}
	if (mode & SLIP_INTERPOLATE_SHADE)
		target->shade = (uint16_t)llround(target->shade + ((double)other->shade - target->shade) * screenRatio);
}

int SlipRaceGpu_SplitScreenXRecord(uint8_t *recordBase, size_t recordBytes, uint32_t targetOffset, uint32_t otherOffset,
                                   uint32_t projectionMode, int32_t clipPlaneX, int32_t limitYMin, int32_t limitYMax,
                                   SlipDraw3DSplitScreenX *result) {
	SlipDraw3DDrawRecord *target;
	const SlipDraw3DDrawRecord *other;
	uint32_t targetFlags;
	uint32_t otherFlags;
	uint32_t xDeltaToPlane;
	uint32_t xDeltaBetweenRecords;
	int32_t yAfter;
	uint32_t flags;

	if (recordBase == NULL || result == NULL || (size_t)targetOffset > recordBytes ||
	    recordBytes - (size_t)targetOffset < SLIP_DRAW3D_DRAW_RECORD_SIZE || (size_t)otherOffset > recordBytes ||
	    recordBytes - (size_t)otherOffset < SLIP_DRAW3D_DRAW_RECORD_SIZE) {
		return 0;
	}
	target = (SlipDraw3DDrawRecord *)(void *)(recordBase + targetOffset);
	other = (const SlipDraw3DDrawRecord *)(const void *)(recordBase + otherOffset);
	targetFlags = target->flags;
	otherFlags = other->flags;
	xDeltaToPlane = (uint32_t)clipPlaneX - (uint32_t)target->screenX;
	xDeltaBetweenRecords = (uint32_t)other->screenX - (uint32_t)target->screenX;
	*result = (SlipDraw3DSplitScreenX){.highPrecisionPath =
	                                       ((targetFlags & SLIP_VERTEX_SCREEN_CLIP_IN_RANGE) & otherFlags) != 0,
	                                   .targetOffset = targetOffset,
	                                   .otherOffset = otherOffset,
	                                   .clipPlaneX = clipPlaneX,
	                                   .xDeltaToPlane = (int32_t)xDeltaToPlane,
	                                   .xDeltaBetweenRecords = (int32_t)xDeltaBetweenRecords,
	                                   .returned = true};
	double denominator = (double)other->screenX - target->screenX;
	if (!denominator)
		return 0;
	double t = ((double)clipPlaneX - target->screenX) / denominator;
	target->screenY = (int32_t)llround(target->screenY + ((double)other->screenY - target->screenY) * t);
	SlipRaceGpu_ClipAttributes(target, other, projectionMode, t);
	target->screenX = clipPlaneX;
	result->wroteClipPlaneX = true;
	yAfter = target->screenY;
	flags = target->flags;
	result->flagsBefore = flags;
	flags &= ~SLIP_VERTEX_VERTICAL_CLIP_STATUS;
	result->flagsAfterClear = flags;
	result->yBelow = yAfter < limitYMin;
	if (result->yBelow) {
		flags |= SLIP_CLIP_TOP;
	}
	result->yAbove = yAfter > limitYMax;
	if (result->yAbove) {
		flags |= SLIP_CLIP_BOTTOM;
	}
	result->yInsideFiniteSentinel =
	    yAfter < SLIP_SCREEN_CLIP_COORDINATE_LIMIT && yAfter > -SLIP_SCREEN_CLIP_COORDINATE_LIMIT;
	if (result->yInsideFiniteSentinel) {
		flags |= SLIP_VERTEX_SCREEN_CLIP_IN_RANGE;
	}
	target->flags = flags;
	result->flagsOut = flags;
	return 1;
}

int SlipRaceGpu_SplitScreenYRecord(uint8_t *recordBase, size_t recordBytes, uint32_t targetOffset, uint32_t otherOffset,
                                   uint32_t projectionMode, int32_t clipPlaneY, SlipDraw3DSplitScreenY *result) {
	SlipDraw3DDrawRecord *target;
	const SlipDraw3DDrawRecord *other;
	uint32_t targetFlags;
	uint32_t otherFlags;
	uint32_t yDeltaToPlane;
	uint32_t yDeltaBetweenRecords;

	if (recordBase == NULL || result == NULL || (size_t)targetOffset > recordBytes ||
	    recordBytes - (size_t)targetOffset < SLIP_DRAW3D_DRAW_RECORD_SIZE || (size_t)otherOffset > recordBytes ||
	    recordBytes - (size_t)otherOffset < SLIP_DRAW3D_DRAW_RECORD_SIZE) {
		return 0;
	}
	target = (SlipDraw3DDrawRecord *)(void *)(recordBase + targetOffset);
	other = (const SlipDraw3DDrawRecord *)(const void *)(recordBase + otherOffset);
	targetFlags = target->flags;
	otherFlags = other->flags;
	yDeltaToPlane = (uint32_t)clipPlaneY - (uint32_t)target->screenY;
	yDeltaBetweenRecords = (uint32_t)other->screenY - (uint32_t)target->screenY;
	*result = (SlipDraw3DSplitScreenY){.highPrecisionPath =
	                                       ((targetFlags & SLIP_VERTEX_SCREEN_CLIP_IN_RANGE) & otherFlags) != 0,
	                                   .targetOffset = targetOffset,
	                                   .otherOffset = otherOffset,
	                                   .clipPlaneY = clipPlaneY,
	                                   .yDeltaToPlane = (int32_t)yDeltaToPlane,
	                                   .yDeltaBetweenRecords = (int32_t)yDeltaBetweenRecords,
	                                   .returned = true};
	double denominator = (double)other->screenY - target->screenY;
	if (!denominator)
		return 0;
	double t = ((double)clipPlaneY - target->screenY) / denominator;
	target->screenX = (int32_t)llround(target->screenX + ((double)other->screenX - target->screenX) * t);
	SlipRaceGpu_ClipAttributes(target, other, projectionMode, t);
	target->screenY = clipPlaneY;
	result->wroteClipPlaneY = true;
	return 1;
}
