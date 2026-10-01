#include "renderer_interpolation.h"

void SlipRenderer_InterpolateTexture14(SlipRendererState *state, SlipRendererPolygon *target,
                                       const SlipRendererPolygon *inside, uint16_t fraction) {
	if ((state->interpolationFlags & SLIP_INTERPOLATE_TEXTURE) != 0) {
		uint32_t difference = inside->point.textureU - target->point.textureU;
		int32_t product = (int32_t)(int16_t)difference * (int16_t)fraction;
		uint32_t step = (difference & ~(uint32_t)UINT16_MAX) | (uint16_t)((uint32_t)product >> SLIP_CLIP_FRACTION_BITS);
		target->point.textureU += step;
		difference = inside->point.textureV - target->point.textureV;
		product = (int32_t)(int16_t)difference * (int16_t)fraction;
		step = (difference & ~(uint32_t)UINT16_MAX) | (uint16_t)((uint32_t)product >> SLIP_CLIP_FRACTION_BITS);
		target->point.textureV += step;
	}
}

void SlipRenderer_InterpolateTexture30(SlipRendererState *state, SlipRendererPolygon *target,
                                       const SlipRendererPolygon *inside, uint32_t fraction) {
	if ((state->interpolationFlags & SLIP_INTERPOLATE_TEXTURE) != 0) {
		const uint16_t textureFraction = (uint16_t)(fraction >> SLIP_WORD_BITS);
		uint32_t difference = inside->point.textureU - target->point.textureU;
		int32_t product = (int32_t)(int16_t)difference * (int16_t)textureFraction;
		uint32_t step = (difference & ~(uint32_t)UINT16_MAX) | (uint16_t)((uint32_t)product >> SLIP_CLIP_FRACTION_BITS);
		target->point.textureU += step;
		difference = inside->point.textureV - target->point.textureV;
		product = (int32_t)(int16_t)difference * (int16_t)textureFraction;
		step = (difference & ~(uint32_t)UINT16_MAX) | (uint16_t)((uint32_t)product >> SLIP_CLIP_FRACTION_BITS);
		target->point.textureV += step;
	}
}

void SlipRenderer_InterpolateDepthTexture14(SlipRendererState *state, SlipRendererPolygon *target,
                                            const SlipRendererPolygon *inside, uint16_t fraction) {
	uint32_t adjustedFraction = fraction;
	if ((state->interpolationFlags & SLIP_INTERPOLATE_PERSPECTIVE_DEPTH) != 0) {
		int32_t difference = (int32_t)((uint32_t)target->point.world.z - (uint32_t)inside->point.world.z);
		int64_t product = (int64_t)difference * (int32_t)adjustedFraction;
		const int32_t depth =
		    (int32_t)((uint32_t)((uint64_t)product >> SLIP_CLIP_FRACTION_BITS) + (uint32_t)inside->point.world.z +
		              (uint32_t)(((uint64_t)product >> SLIP_CLIP_ROUND_BIT) & 1u));
		product = (int64_t)target->point.world.z * (int32_t)adjustedFraction;
		adjustedFraction = (uint32_t)(int32_t)(product / depth);
		difference = (int32_t)((uint32_t)inside->point.world.z - (uint32_t)target->point.world.z);
		product = (int64_t)difference * (int32_t)adjustedFraction;
		target->point.world.z =
		    (int32_t)((uint32_t)target->point.world.z + (uint32_t)((uint64_t)product >> SLIP_CLIP_FRACTION_BITS) +
		              (uint32_t)(((uint64_t)product >> SLIP_CLIP_ROUND_BIT) & 1u));
	}
	SlipRenderer_InterpolateTexture14(state, target, inside, (uint16_t)adjustedFraction);
}

void SlipRenderer_InterpolateDepthTexture30(SlipRendererState *state, SlipRendererPolygon *target,
                                            const SlipRendererPolygon *inside, uint32_t fraction) {
	if ((state->interpolationFlags & SLIP_INTERPOLATE_PERSPECTIVE_DEPTH) == 0) {
		SlipRenderer_InterpolateTexture30(state, target, inside, fraction);
		return;
	}
	int32_t difference = (int32_t)((uint32_t)target->point.world.z - (uint32_t)inside->point.world.z);
	int64_t product = (int64_t)difference * (int32_t)fraction;
	const int32_t depth =
	    (int32_t)((uint32_t)((uint64_t)product >> SLIP_CLIP_PRECISE_FRACTION_BITS) + (uint32_t)inside->point.world.z +
	              (uint32_t)(((uint64_t)product >> SLIP_CLIP_PRECISE_ROUND_BIT) & 1u));
	product = (int64_t)target->point.world.z * (int32_t)fraction;
	fraction = (uint32_t)(int32_t)(product / depth);
	difference = (int32_t)((uint32_t)inside->point.world.z - (uint32_t)target->point.world.z);
	product = (int64_t)difference * (int32_t)fraction;
	target->point.world.z =
	    (int32_t)((uint32_t)target->point.world.z + (uint32_t)((uint64_t)product >> SLIP_CLIP_PRECISE_FRACTION_BITS) +
	              (uint32_t)(((uint64_t)product >> SLIP_CLIP_PRECISE_ROUND_BIT) & 1u));
	SlipRenderer_InterpolateTexture30(state, target, inside, fraction);
}

void SlipRenderer_InterpolateHorizontal(SlipRendererState *state, SlipRendererPolygon *target,
                                        const SlipRendererPolygon *inside, int32_t limit) {
	if ((target->point.flags & inside->point.flags & SLIP_VERTEX_SCREEN_CLIP_IN_RANGE) != 0) {
		uint32_t distance = (uint32_t)limit - (uint32_t)target->point.screenX;
		uint32_t extent = (uint32_t)inside->point.screenX - (uint32_t)target->point.screenX;
		if ((int32_t)extent < 0) {
			extent = 0u - extent;
			distance = 0u - distance;
		}

		const uint32_t numerator = (uint32_t)((int32_t)(int16_t)distance * (INT32_C(1) << (SLIP_WORD_BITS - 1)));
		const uint16_t quotient = (uint16_t)(numerator / (uint16_t)extent);
		const uint16_t fraction = (uint16_t)((quotient >> 1) + (quotient & 1u));
		const int16_t difference = (int16_t)((uint32_t)inside->point.screenY - (uint32_t)target->point.screenY);
		int32_t product = (int32_t)difference * (int16_t)fraction;
		target->point.screenY =
		    (int32_t)((uint32_t)target->point.screenY + (uint32_t)(product >> SLIP_CLIP_FRACTION_BITS) +
		              (((uint32_t)product >> SLIP_CLIP_ROUND_BIT) & 1u));
		if ((state->interpolationFlags & SLIP_INTERPOLATE_SHADE) != 0) {
			const int16_t shadeDifference = (int16_t)(inside->point.shade - target->point.shade);
			product = (int32_t)shadeDifference * (int16_t)fraction;
			target->point.shade =
			    (uint16_t)(target->point.shade + (uint16_t)((uint32_t)product >> SLIP_CLIP_FRACTION_BITS));
		}
		SlipRenderer_InterpolateDepthTexture14(state, target, inside, fraction);
	} else {
		uint32_t distance = (uint32_t)limit - (uint32_t)target->point.screenX;
		uint32_t extent = (uint32_t)inside->point.screenX - (uint32_t)target->point.screenX;
		if ((int32_t)extent < 0) {
			extent = 0u - extent;
			distance = 0u - distance;
		}
		const uint64_t numerator =
		    (uint64_t)((int64_t)(int32_t)distance * (INT64_C(1) << SLIP_CLIP_PRECISE_FRACTION_BITS));
		const uint32_t fraction = (uint32_t)(numerator / extent);
		const int32_t difference = (int32_t)((uint32_t)inside->point.screenY - (uint32_t)target->point.screenY);
		const int64_t product = (int64_t)difference * (int32_t)fraction;
		target->point.screenY = (int32_t)((uint32_t)target->point.screenY +
		                                  (uint32_t)((uint64_t)product >> SLIP_CLIP_PRECISE_FRACTION_BITS) +
		                                  (uint32_t)(((uint64_t)product >> SLIP_CLIP_PRECISE_ROUND_BIT) & 1u));
		if ((state->interpolationFlags & SLIP_INTERPOLATE_SHADE) != 0) {
			const int16_t shadeDifference = (int16_t)(inside->point.shade - target->point.shade);
			const int32_t shadeProduct = (int32_t)shadeDifference * (int16_t)(fraction >> SLIP_WORD_BITS);
			target->point.shade =
			    (uint16_t)(target->point.shade + (uint16_t)((uint32_t)shadeProduct >> SLIP_CLIP_FRACTION_BITS));
		}
		SlipRenderer_InterpolateDepthTexture30(state, target, inside, fraction);
	}
	target->point.screenX = limit;

	uint32_t flags = target->point.flags & ~(SLIP_CLIP_VERTICAL | SLIP_VERTEX_SCREEN_CLIP_IN_RANGE);
	const int32_t y = target->point.screenY;
	if (y < state->projection.minY)
		flags |= SLIP_CLIP_TOP;
	if (y > state->projection.maxY)
		flags |= SLIP_CLIP_BOTTOM;
	if (y < SLIP_SCREEN_CLIP_COORDINATE_LIMIT && y > -SLIP_SCREEN_CLIP_COORDINATE_LIMIT)
		flags |= SLIP_VERTEX_SCREEN_CLIP_IN_RANGE;
	target->point.flags = flags;
}

void SlipRenderer_InterpolateVertical(SlipRendererState *state, SlipRendererPolygon *target,
                                      const SlipRendererPolygon *inside, int32_t limit) {
	if ((target->point.flags & inside->point.flags & SLIP_VERTEX_SCREEN_CLIP_IN_RANGE) != 0) {
		uint32_t distance = (uint32_t)limit - (uint32_t)target->point.screenY;
		uint32_t extent = (uint32_t)inside->point.screenY - (uint32_t)target->point.screenY;
		if ((int32_t)extent < 0) {
			extent = 0u - extent;
			distance = 0u - distance;
		}

		const uint32_t numerator = (uint32_t)((int32_t)(int16_t)distance * (INT32_C(1) << (SLIP_WORD_BITS - 1)));
		const uint16_t quotient = (uint16_t)(numerator / (uint16_t)extent);
		const uint16_t fraction = (uint16_t)((quotient >> 1) + (quotient & 1u));
		const int16_t difference = (int16_t)((uint32_t)inside->point.screenX - (uint32_t)target->point.screenX);
		int32_t product = (int32_t)difference * (int16_t)fraction;
		target->point.screenX =
		    (int32_t)((uint32_t)target->point.screenX + (uint32_t)(product >> SLIP_CLIP_FRACTION_BITS) +
		              (((uint32_t)product >> SLIP_CLIP_ROUND_BIT) & 1u));
		if ((state->interpolationFlags & SLIP_INTERPOLATE_SHADE) != 0) {
			const int16_t shadeDifference = (int16_t)(inside->point.shade - target->point.shade);
			product = (int32_t)shadeDifference * (int16_t)fraction;
			target->point.shade =
			    (uint16_t)(target->point.shade + (uint16_t)((uint32_t)product >> SLIP_CLIP_FRACTION_BITS));
		}
		SlipRenderer_InterpolateDepthTexture14(state, target, inside, fraction);
	} else {
		uint32_t distance = (uint32_t)limit - (uint32_t)target->point.screenY;
		uint32_t extent = (uint32_t)inside->point.screenY - (uint32_t)target->point.screenY;
		if ((int32_t)extent < 0) {
			extent = 0u - extent;
			distance = 0u - distance;
		}
		const uint64_t numerator =
		    (uint64_t)((int64_t)(int32_t)distance * (INT64_C(1) << SLIP_CLIP_PRECISE_FRACTION_BITS));
		const uint32_t fraction = (uint32_t)(numerator / extent);
		const int32_t difference = (int32_t)((uint32_t)inside->point.screenX - (uint32_t)target->point.screenX);
		const int64_t product = (int64_t)difference * (int32_t)fraction;
		target->point.screenX = (int32_t)((uint32_t)target->point.screenX +
		                                  (uint32_t)((uint64_t)product >> SLIP_CLIP_PRECISE_FRACTION_BITS) +
		                                  (uint32_t)(((uint64_t)product >> SLIP_CLIP_PRECISE_ROUND_BIT) & 1u));
		if ((state->interpolationFlags & SLIP_INTERPOLATE_SHADE) != 0) {
			const int16_t shadeDifference = (int16_t)(inside->point.shade - target->point.shade);
			const int32_t shadeProduct = (int32_t)shadeDifference * (int16_t)(fraction >> SLIP_WORD_BITS);
			target->point.shade =
			    (uint16_t)(target->point.shade + (uint16_t)((uint32_t)shadeProduct >> SLIP_CLIP_FRACTION_BITS));
		}
		SlipRenderer_InterpolateDepthTexture30(state, target, inside, fraction);
	}
	target->point.screenY = limit;
}

void SlipRenderer_ProjectClipped(SlipRendererState *state, SlipRendererPolygon *target, SlipDraw3DVec32 world) {
	int32_t x, y;
	if ((state->shapeFlags & SLIP_SHAPE_SECONDARY_PROJECTION) != 0)
		state->projection.projectSecondary(world, &x, &y, &state->projection);
	else
		state->projection.projectPrimary(world, &x, &y, &state->projection);
	target->point.screenX = x;
	target->point.screenY = y;
	uint32_t flags = SLIP_VERTEX_PROJECTED;
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
	target->point.flags |= flags;
}

void SlipRenderer_InterpolateDepth(SlipRendererState *state, SlipRendererPolygon *target,
                                   const SlipRendererPolygon *inside, int32_t limit) {
	if ((state->shapeFlags & SLIP_SHAPE_SHORT_COORDINATES) != 0) {
		uint32_t distance = (uint32_t)limit - (uint32_t)target->point.world.z;
		uint32_t extent = (uint32_t)inside->point.world.z - (uint32_t)target->point.world.z;
		if ((int32_t)extent < 0) {
			extent = 0u - extent;
			distance = 0u - distance;
		}
		const uint32_t numerator = (uint32_t)((int32_t)(int16_t)distance * (INT32_C(1) << SLIP_CLIP_FRACTION_BITS));
		const uint16_t fraction = (uint16_t)(numerator / (uint16_t)extent);
		const int16_t differenceX = (int16_t)((uint32_t)inside->point.world.x - (uint32_t)target->point.world.x);
		const int32_t productX = (int32_t)differenceX * (int16_t)fraction;
		const int16_t differenceY = (int16_t)((uint32_t)inside->point.world.y - (uint32_t)target->point.world.y);
		const int32_t productY = (int32_t)differenceY * (int16_t)fraction;
		target->point.world.y =
		    (int32_t)((uint32_t)target->point.world.y + (uint32_t)(productY >> SLIP_CLIP_FRACTION_BITS));
		target->point.world.x =
		    (int32_t)((uint32_t)target->point.world.x + (uint32_t)(productX >> SLIP_CLIP_FRACTION_BITS));
		if ((state->interpolationFlags & SLIP_INTERPOLATE_SHADE) != 0) {
			const int16_t shadeDifference = (int16_t)(inside->point.shade - target->point.shade);
			const int32_t product = (int32_t)shadeDifference * (int16_t)fraction;
			target->point.shade =
			    (uint16_t)(target->point.shade + (uint16_t)((uint32_t)product >> SLIP_CLIP_FRACTION_BITS));
		}
		SlipRenderer_InterpolateTexture14(state, target, inside, fraction);
	} else {
		uint32_t distance = (uint32_t)limit - (uint32_t)target->point.world.z;
		uint32_t extent = (uint32_t)inside->point.world.z - (uint32_t)target->point.world.z;
		if ((int32_t)extent < 0) {
			extent = 0u - extent;
			distance = 0u - distance;
		}
		const uint64_t numerator =
		    (uint64_t)((int64_t)(int32_t)distance * (INT64_C(1) << SLIP_CLIP_PRECISE_FRACTION_BITS));
		const uint32_t fraction = (uint32_t)(numerator / extent);
		int32_t difference = (int32_t)((uint32_t)inside->point.world.x - (uint32_t)target->point.world.x);
		int64_t product = (int64_t)difference * (int32_t)fraction;
		target->point.world.x = (int32_t)((uint32_t)target->point.world.x +
		                                  (uint32_t)((uint64_t)product >> SLIP_CLIP_PRECISE_FRACTION_BITS));
		difference = (int32_t)((uint32_t)inside->point.world.y - (uint32_t)target->point.world.y);
		product = (int64_t)difference * (int32_t)fraction;
		target->point.world.y = (int32_t)((uint32_t)target->point.world.y +
		                                  (uint32_t)((uint64_t)product >> SLIP_CLIP_PRECISE_FRACTION_BITS));
		if ((state->interpolationFlags & SLIP_INTERPOLATE_SHADE) != 0) {
			const int16_t shadeDifference = (int16_t)(inside->point.shade - target->point.shade);
			const int32_t shadeProduct = (int32_t)shadeDifference * (int16_t)(fraction >> SLIP_WORD_BITS);
			target->point.shade =
			    (uint16_t)(target->point.shade + (uint16_t)((uint32_t)shadeProduct >> SLIP_CLIP_FRACTION_BITS));
		}
		SlipRenderer_InterpolateTexture30(state, target, inside, fraction);
	}
	target->point.world.z = limit;
	SlipRenderer_ProjectClipped(state, target, target->point.world);
}

SlipRendererPlaneIntersection SlipRenderer_IntersectPlaneQ30(SlipDraw3DVec32 delta, int32_t targetDepth,
                                                             int32_t insideDepth) {
	const uint32_t distance = 0u - (uint32_t)targetDepth;
	const uint32_t extent = (uint32_t)insideDepth + distance;
	const uint64_t numerator = (uint64_t)((int64_t)(int32_t)distance * (INT64_C(1) << SLIP_CLIP_PRECISE_FRACTION_BITS));
	const uint32_t fraction = (uint32_t)(numerator / extent);
	SlipRendererPlaneIntersection result;
	result.fraction = fraction;
	int64_t product = (int64_t)(int32_t)fraction * delta.y;
	result.step.y = (int32_t)((uint32_t)((uint64_t)product >> SLIP_CLIP_PRECISE_FRACTION_BITS) +
	                          (uint32_t)(((uint64_t)product >> SLIP_CLIP_PRECISE_ROUND_BIT) & 1u));
	product = (int64_t)(int32_t)fraction * delta.z;
	result.step.z = (int32_t)((uint32_t)((uint64_t)product >> SLIP_CLIP_PRECISE_FRACTION_BITS) +
	                          (uint32_t)(((uint64_t)product >> SLIP_CLIP_PRECISE_ROUND_BIT) & 1u));
	product = (int64_t)delta.x * (int32_t)fraction;
	const uint32_t low = (uint32_t)product + (uint32_t)(product < INT32_MIN || product > INT32_MAX);
	const uint64_t adjusted = ((uint64_t)product & ~(uint64_t)UINT32_MAX) | low;
	result.step.x = (int32_t)(uint32_t)(adjusted >> SLIP_CLIP_PRECISE_FRACTION_BITS);
	return result;
}

SlipRendererPlaneIntersection SlipRenderer_IntersectPlaneQ14(SlipDraw3DVec32 delta, int32_t targetDepth,
                                                             int32_t insideDepth) {
	const uint32_t distance = 0u - (uint32_t)targetDepth;
	const uint32_t extent = (uint32_t)insideDepth + distance;
	const uint32_t numerator = (uint32_t)((int32_t)(int16_t)distance * (INT32_C(1) << SLIP_CLIP_FRACTION_BITS));
	const uint16_t fraction = (uint16_t)(numerator / (uint16_t)extent);
	SlipRendererPlaneIntersection result;
	result.fraction = fraction;
	int32_t product = (int32_t)(int16_t)fraction * (int16_t)delta.y;
	result.step.y =
	    (int16_t)(((uint32_t)product >> SLIP_CLIP_FRACTION_BITS) + (((uint32_t)product >> SLIP_CLIP_ROUND_BIT) & 1u));
	product = (int32_t)(int16_t)fraction * (int16_t)delta.z;
	result.step.z =
	    (int16_t)(((uint32_t)product >> SLIP_CLIP_FRACTION_BITS) + (((uint32_t)product >> SLIP_CLIP_ROUND_BIT) & 1u));
	product = (int32_t)(int16_t)delta.x * (int16_t)fraction;
	result.step.x =
	    (int16_t)(((uint32_t)product >> SLIP_CLIP_FRACTION_BITS) + (((uint32_t)product >> SLIP_CLIP_ROUND_BIT) & 1u));
	return result;
}

void SlipRenderer_InterpolateAuxiliary(SlipRendererState *state, SlipRendererPolygon *target,
                                       const SlipRendererPolygon *inside) {
	if ((state->shapeFlags & SLIP_SHAPE_SHORT_COORDINATES) != 0) {
		SlipDraw3DVec32 delta = {(int32_t)((uint32_t)inside->point.world.x - (uint32_t)target->point.world.x),
		                         (int32_t)((uint32_t)inside->point.world.y - (uint32_t)target->point.world.y),
		                         (int32_t)((uint32_t)inside->point.world.z - (uint32_t)target->point.world.z)};
		SlipRendererPlaneIntersection intersection =
		    SlipRenderer_IntersectPlaneQ14(delta, target->point.depth, inside->point.depth);
		target->point.world.x = (int32_t)((uint32_t)target->point.world.x + (uint32_t)intersection.step.x);
		target->point.world.y = (int32_t)((uint32_t)target->point.world.y + (uint32_t)intersection.step.y);
		target->point.world.z = (int32_t)((uint32_t)target->point.world.z + (uint32_t)intersection.step.z);
		if ((state->interpolationFlags & SLIP_INTERPOLATE_SHADE) != 0) {
			const int16_t difference = (int16_t)(inside->point.shade - target->point.shade);
			const int32_t product = (int32_t)difference * (int16_t)intersection.fraction;
			target->point.shade =
			    (uint16_t)(target->point.shade + (uint16_t)((uint32_t)product >> SLIP_CLIP_FRACTION_BITS));
		}
		SlipRenderer_InterpolateTexture14(state, target, inside, (uint16_t)intersection.fraction);
	} else {
		SlipDraw3DVec32 delta = {(int32_t)((uint32_t)inside->point.world.x - (uint32_t)target->point.world.x),
		                         (int32_t)((uint32_t)inside->point.world.y - (uint32_t)target->point.world.y),
		                         (int32_t)((uint32_t)inside->point.world.z - (uint32_t)target->point.world.z)};
		SlipRendererPlaneIntersection intersection =
		    SlipRenderer_IntersectPlaneQ30(delta, target->point.depth, inside->point.depth);
		target->point.world.x = (int32_t)((uint32_t)target->point.world.x + (uint32_t)intersection.step.x);
		target->point.world.y = (int32_t)((uint32_t)target->point.world.y + (uint32_t)intersection.step.y);
		target->point.world.z = (int32_t)((uint32_t)target->point.world.z + (uint32_t)intersection.step.z);
		if ((state->interpolationFlags & SLIP_INTERPOLATE_SHADE) != 0) {
			const int16_t difference = (int16_t)(inside->point.shade - target->point.shade);
			const int32_t product = (int32_t)difference * (int16_t)(intersection.fraction >> SLIP_WORD_BITS);
			target->point.shade =
			    (uint16_t)(target->point.shade + (uint16_t)((uint32_t)product >> SLIP_CLIP_FRACTION_BITS));
		}
		SlipRenderer_InterpolateTexture30(state, target, inside, intersection.fraction);
	}
	target->point.flags &= ~SLIP_CLIP_BEFORE_PROJECTION;
	if (target->point.world.z < state->projection.minZ) {
		target->point.flags |= SLIP_CLIP_NEAR;
		return;
	}
	if (target->point.world.z > state->projection.maxZ) {
		target->point.flags |= SLIP_CLIP_FAR;
		return;
	}
	SlipRenderer_ProjectClipped(state, target, target->point.world);
}

void SlipRenderer_InterpolatePostPlane(SlipRendererPolygon *target, const SlipRendererPolygon *inside) {
	const uint32_t distance = 0u - (uint32_t)target->point.screenPlaneDistance;
	const uint32_t extent = distance + (uint32_t)inside->point.screenPlaneDistance;
	const uint64_t numerator = (uint64_t)distance << SLIP_CLIP_PRECISE_FRACTION_BITS;
	const uint32_t fraction = (uint32_t)(numerator / extent);
	int32_t difference = (int32_t)((uint32_t)inside->point.screenX - (uint32_t)target->point.screenX);
	int64_t product = (int64_t)difference * (int32_t)fraction;
	target->point.screenX =
	    (int32_t)((uint32_t)target->point.screenX + (uint32_t)((uint64_t)product >> SLIP_CLIP_PRECISE_FRACTION_BITS) +
	              (uint32_t)(((uint64_t)product >> SLIP_CLIP_PRECISE_ROUND_BIT) & 1u));
	difference = (int32_t)((uint32_t)inside->point.screenY - (uint32_t)target->point.screenY);
	product = (int64_t)difference * (int32_t)fraction;
	target->point.screenY =
	    (int32_t)((uint32_t)target->point.screenY + (uint32_t)((uint64_t)product >> SLIP_CLIP_PRECISE_FRACTION_BITS) +
	              (uint32_t)(((uint64_t)product >> SLIP_CLIP_PRECISE_ROUND_BIT) & 1u));
}
