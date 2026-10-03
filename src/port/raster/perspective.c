#include "perspective.h"

enum {
	TEXTURE_INTERPOLATION_BITS = 14,
	TEXTURE_INTERPOLATION_UNIT = 1 << TEXTURE_INTERPOLATION_BITS,
	TEXTURE_INTERPOLATION_CARRY_MASK = 1u
};

void Raster_DrawPerspective(RasterPerspectiveDrawState *state, uint16_t texture, RasterTexturedPoint *points,
                            uint32_t count, const RasterPerspectiveDrawCalls *calls) {
	state->texture = calls->lock(calls->context, texture);
	Raster_ScaleTextureCoordinates(state->texture->width, state->texture->height, points, count);
	calls->textureRows(calls->context, state->texture);
	state->begin = points;
	uint32_t remaining = count - 1u;
	int32_t scanline = points->y;
	int32_t bottom = scanline;
	const RasterTexturedPoint *next = points + 1;
	state->left.point = state->right.point = points;
	do {
		if (bottom < next->y)
			bottom = next->y;
		if (scanline >= next->y) {
			if (scanline == next->y) {
				if (next->x < state->left.point->x)
					state->left.point = next;
				if (next->x > state->right.point->x)
					state->right.point = next;
			} else {
				state->left.point = state->right.point = next;
				scanline = next->y;
			}
		}
		++next;
	} while (--remaining != 0);
	state->end = next;
	if (bottom == scanline) {
		state->leftU = state->left.point->scaledU;
		state->leftV = state->left.point->scaledV;
		state->rightU = state->right.point->scaledU;
		state->rightV = state->right.point->scaledV;
		state->left.x = state->left.point->x;
		state->right.x = state->right.point->x;

		calls->span(calls->context, state, scanline);
	} else {
		state->bottom = bottom;
		(void)Raster_StepPerspectiveLeft(&state->left, state->begin, state->end, scanline, bottom);
		(void)Raster_StepPerspectiveRight(&state->right, state->begin, state->end, scanline, bottom);
		for (;;) {
			int32_t denominator =
			    (int32_t)(state->left.startDepthSum - state->left.endDepthSum + (uint32_t)state->left.point->depth);
			int32_t factor =
			    (int32_t)((int64_t)(int32_t)state->left.startDepthSum * TEXTURE_INTERPOLATION_UNIT / denominator);
			int64_t product = (int64_t)(int32_t)(state->left.point->scaledU - state->left.u) * factor;
			state->leftU =
			    (uint32_t)(product >> TEXTURE_INTERPOLATION_BITS) + state->left.u +
			    (uint32_t)(((uint64_t)product >> (TEXTURE_INTERPOLATION_BITS - 1)) & TEXTURE_INTERPOLATION_CARRY_MASK);
			product = (int64_t)(int32_t)(state->left.point->scaledV - state->left.v) * factor;
			state->leftV =
			    (uint32_t)(product >> TEXTURE_INTERPOLATION_BITS) + state->left.v +
			    (uint32_t)(((uint64_t)product >> (TEXTURE_INTERPOLATION_BITS - 1)) & TEXTURE_INTERPOLATION_CARRY_MASK);
			product = (int64_t)(int32_t)((uint32_t)state->left.point->depth - state->left.depth) * factor;
			state->leftDepth =
			    (uint32_t)(product >> TEXTURE_INTERPOLATION_BITS) + state->left.depth +
			    (uint32_t)(((uint64_t)product >> (TEXTURE_INTERPOLATION_BITS - 1)) & TEXTURE_INTERPOLATION_CARRY_MASK);
			denominator =
			    (int32_t)(state->right.startDepthSum - state->right.endDepthSum + (uint32_t)state->right.point->depth +
			              (state->right.startDepthSum < state->right.endDepthSum));
			factor = (int32_t)((int64_t)(int32_t)state->right.startDepthSum * TEXTURE_INTERPOLATION_UNIT / denominator);
			product = (int64_t)(int32_t)(state->right.point->scaledU - state->right.u) * factor;
			state->rightU =
			    (uint32_t)(product >> TEXTURE_INTERPOLATION_BITS) + state->right.u +
			    (uint32_t)(((uint64_t)product >> (TEXTURE_INTERPOLATION_BITS - 1)) & TEXTURE_INTERPOLATION_CARRY_MASK);
			product = (int64_t)(int32_t)(state->right.point->scaledV - state->right.v) * factor;
			state->rightV =
			    (uint32_t)(product >> TEXTURE_INTERPOLATION_BITS) + state->right.v +
			    (uint32_t)(((uint64_t)product >> (TEXTURE_INTERPOLATION_BITS - 1)) & TEXTURE_INTERPOLATION_CARRY_MASK);
			product = (int64_t)(int32_t)((uint32_t)state->right.point->depth - state->right.depth) * factor;
			state->rightDepth =
			    (uint32_t)(product >> TEXTURE_INTERPOLATION_BITS) + state->right.depth +
			    (uint32_t)(((uint64_t)product >> (TEXTURE_INTERPOLATION_BITS - 1)) & TEXTURE_INTERPOLATION_CARRY_MASK);
			calls->span(calls->context, state, scanline);
			scanline = (int32_t)((uint32_t)scanline + 1u);
			state->left.startDepthSum += state->left.startDepthStep;
			state->left.endDepthSum += state->left.endDepthStep;
			state->right.startDepthSum += state->right.startDepthStep;
			state->right.endDepthSum += state->right.endDepthStep;
			const uint32_t leftFraction = (uint32_t)state->left.xFraction + (uint16_t)state->left.xStep;
			state->left.xFraction = (uint16_t)leftFraction;
			state->left.x =
			    (int32_t)(((uint32_t)state->left.x & ~(uint32_t)UINT16_MAX) |
			              (uint16_t)((uint32_t)state->left.x + ((uint32_t)state->left.xStep >> RASTER_FRACTION_BITS) +
			                         (leftFraction >> RASTER_FRACTION_BITS)));
			const uint32_t rightFraction = (uint32_t)state->right.xFraction + (uint16_t)state->right.xStep;
			state->right.xFraction = (uint16_t)rightFraction;
			state->right.x =
			    (int32_t)(((uint32_t)state->right.x & ~(uint32_t)UINT16_MAX) |
			              (uint16_t)((uint32_t)state->right.x + ((uint32_t)state->right.xStep >> RASTER_FRACTION_BITS) +
			                         (rightFraction >> RASTER_FRACTION_BITS)));
			if (--state->left.remainingScanlines == 0 &&
			    Raster_StepPerspectiveLeft(&state->left, state->begin, state->end, scanline, bottom))
				break;
			if (--state->right.remainingScanlines == 0 &&
			    Raster_StepPerspectiveRight(&state->right, state->begin, state->end, scanline, bottom))
				break;
			if (scanline >= bottom) {
				/* The final call uses the last interpolated endpoints unchanged. */
				calls->span(calls->context, state, scanline);
				break;
			}
		}
	}
	calls->unlock(calls->context, texture);
}

enum { RASTER_SCANLINE_PARITY = 1u };

static bool Raster_AdvanceOpaquePerspective(RasterPerspectiveDrawState *state, RasterOpaquePerspectiveState *opaque,
                                            int32_t *scanline) {
	opaque->edgeEndsOnNextScanline = state->left.remainingScanlines == 1 || state->right.remainingScanlines == 1;
	*scanline = (int32_t)((uint32_t)*scanline + 1u);
	state->left.startDepthSum += state->left.startDepthStep;
	state->left.endDepthSum += state->left.endDepthStep;
	state->right.startDepthSum += state->right.startDepthStep;
	state->right.endDepthSum += state->right.endDepthStep;
	const uint32_t leftFraction = (uint32_t)state->left.xFraction + (uint16_t)state->left.xStep;
	state->left.xFraction = (uint16_t)leftFraction;
	state->left.x =
	    (int32_t)(((uint32_t)state->left.x & ~(uint32_t)UINT16_MAX) |
	              (uint16_t)((uint32_t)state->left.x + ((uint32_t)state->left.xStep >> RASTER_FRACTION_BITS) +
	                         (leftFraction >> RASTER_FRACTION_BITS)));
	const uint32_t rightFraction = (uint32_t)state->right.xFraction + (uint16_t)state->right.xStep;
	state->right.xFraction = (uint16_t)rightFraction;
	state->right.x =
	    (int32_t)(((uint32_t)state->right.x & ~(uint32_t)UINT16_MAX) |
	              (uint16_t)((uint32_t)state->right.x + ((uint32_t)state->right.xStep >> RASTER_FRACTION_BITS) +
	                         (rightFraction >> RASTER_FRACTION_BITS)));
	if (--state->left.remainingScanlines == 0 &&
	    Raster_StepPerspectiveLeft(&state->left, state->begin, state->end, *scanline, state->bottom))
		return true;
	if (--state->right.remainingScanlines == 0 &&
	    Raster_StepPerspectiveRight(&state->right, state->begin, state->end, *scanline, state->bottom))
		return true;
	return false;
}

static void Raster_DrawOpaquePerspectiveRun(RasterPerspectiveDrawState *state, RasterOpaquePerspectiveState *opaque,
                                            int32_t scanline, uint8_t *const *screenRows,
                                            const RasterPerspectiveDrawCalls *calls) {
	int32_t denominator =
	    (int32_t)(state->left.startDepthSum - state->left.endDepthSum + (uint32_t)state->left.point->depth);
	int32_t factor = (int32_t)((int64_t)(int32_t)state->left.startDepthSum * TEXTURE_INTERPOLATION_UNIT / denominator);
	int64_t product = (int64_t)(int32_t)(state->left.point->scaledU - state->left.u) * factor;
	state->leftU =
	    (uint32_t)(product >> TEXTURE_INTERPOLATION_BITS) + state->left.u +
	    (uint32_t)(((uint64_t)product >> (TEXTURE_INTERPOLATION_BITS - 1)) & TEXTURE_INTERPOLATION_CARRY_MASK);
	product = (int64_t)(int32_t)(state->left.point->scaledV - state->left.v) * factor;
	state->leftV =
	    (uint32_t)(product >> TEXTURE_INTERPOLATION_BITS) + state->left.v +
	    (uint32_t)(((uint64_t)product >> (TEXTURE_INTERPOLATION_BITS - 1)) & TEXTURE_INTERPOLATION_CARRY_MASK);
	product = (int64_t)(int32_t)((uint32_t)state->left.point->depth - state->left.depth) * factor;
	state->leftDepth =
	    (uint32_t)(product >> TEXTURE_INTERPOLATION_BITS) + state->left.depth +
	    (uint32_t)(((uint64_t)product >> (TEXTURE_INTERPOLATION_BITS - 1)) & TEXTURE_INTERPOLATION_CARRY_MASK);
	denominator =
	    (int32_t)(state->right.startDepthSum - state->right.endDepthSum + (uint32_t)state->right.point->depth +
	              (state->right.startDepthSum < state->right.endDepthSum));
	factor = (int32_t)((int64_t)(int32_t)state->right.startDepthSum * TEXTURE_INTERPOLATION_UNIT / denominator);
	product = (int64_t)(int32_t)(state->right.point->scaledU - state->right.u) * factor;
	state->rightU =
	    (uint32_t)(product >> TEXTURE_INTERPOLATION_BITS) + state->right.u +
	    (uint32_t)(((uint64_t)product >> (TEXTURE_INTERPOLATION_BITS - 1)) & TEXTURE_INTERPOLATION_CARRY_MASK);
	product = (int64_t)(int32_t)(state->right.point->scaledV - state->right.v) * factor;
	state->rightV =
	    (uint32_t)(product >> TEXTURE_INTERPOLATION_BITS) + state->right.v +
	    (uint32_t)(((uint64_t)product >> (TEXTURE_INTERPOLATION_BITS - 1)) & TEXTURE_INTERPOLATION_CARRY_MASK);
	product = (int64_t)(int32_t)((uint32_t)state->right.point->depth - state->right.depth) * factor;
	state->rightDepth =
	    (uint32_t)(product >> TEXTURE_INTERPOLATION_BITS) + state->right.depth +
	    (uint32_t)(((uint64_t)product >> (TEXTURE_INTERPOLATION_BITS - 1)) & TEXTURE_INTERPOLATION_CARRY_MASK);
	calls->span(calls->context, state, scanline);
	opaque->previousLeft = state->left.x;
	opaque->previousRight = state->right.x;
	opaque->currentRun = screenRows[scanline] + state->left.x;
	opaque->runLength = (int32_t)((uint32_t)state->right.x - (uint32_t)state->left.x + 1u);
	opaque->nextRun = screenRows[scanline + 1] + state->left.x;
}

void Raster_DrawOpaquePerspective(RasterPerspectiveDrawState *state, RasterOpaquePerspectiveState *opaque,
                                  uint16_t texture, RasterTexturedPoint *points, uint32_t count,
                                  uint8_t *const *screenRows, const RasterPerspectiveDrawCalls *calls) {
	state->texture = calls->lock(calls->context, texture);

	if (state->texture->transparentColor != -1) {
		calls->unlock(calls->context, texture);
		Raster_DrawPerspective(state, texture, points, count, calls);
		return;
	}
	Raster_ScaleTextureCoordinates(state->texture->width, state->texture->height, points, count);
	calls->textureRows(calls->context, state->texture);
	state->begin = points;
	uint32_t remaining = count - 1u;
	int32_t scanline = points->y;
	int32_t bottom = scanline;
	const RasterTexturedPoint *next = points + 1;
	state->left.point = state->right.point = points;
	do {
		if (bottom < next->y)
			bottom = next->y;
		if (scanline >= next->y) {
			if (scanline == next->y) {
				if (next->x < state->left.point->x)
					state->left.point = next;
				if (next->x > state->right.point->x)
					state->right.point = next;
			} else {
				state->left.point = state->right.point = next;
				scanline = next->y;
			}
		}
		++next;
	} while (--remaining != 0);
	state->end = next;
	if (bottom == scanline) {
		state->leftU = state->left.point->scaledU;
		state->leftV = state->left.point->scaledV;
		state->rightU = state->right.point->scaledU;
		state->rightV = state->right.point->scaledV;
		state->left.x = state->left.point->x;
		state->right.x = state->right.point->x;
		calls->span(calls->context, state, scanline);
	} else {
		state->bottom = bottom;
		(void)Raster_StepPerspectiveLeft(&state->left, state->begin, state->end, scanline, bottom);
		(void)Raster_StepPerspectiveRight(&state->right, state->begin, state->end, scanline, bottom);
		Raster_DrawOpaquePerspectiveRun(state, opaque, scanline, screenRows, calls);
		for (;;) {
			if (Raster_AdvanceOpaquePerspective(state, opaque, &scanline))
				break;
			if (scanline >= bottom) {
				calls->span(calls->context, state, scanline);
				break;
			}
			if (((uint32_t)scanline & RASTER_SCANLINE_PARITY) == 0 || opaque->edgeEndsOnNextScanline) {
				Raster_DrawOpaquePerspectiveRun(state, opaque, scanline, screenRows, calls);
				continue;
			}
			const uint32_t leftFraction = (uint32_t)state->left.xFraction + (uint16_t)state->left.xStep;
			const uint32_t rightFraction = (uint32_t)state->right.xFraction + (uint16_t)state->right.xStep;

			opaque->nextScanlineLeft =
			    (int16_t)((uint32_t)state->left.x + ((uint32_t)state->left.xStep >> RASTER_FRACTION_BITS) +
			              (leftFraction >> RASTER_FRACTION_BITS));
			opaque->nextScanlineRight =
			    (int16_t)((uint32_t)state->right.x + ((uint32_t)state->right.xStep >> RASTER_FRACTION_BITS) +
			              (rightFraction >> RASTER_FRACTION_BITS));
			if (opaque->nextScanlineRight < (int16_t)state->left.x ||
			    opaque->nextScanlineLeft > (int16_t)state->right.x) {
				Raster_DrawOpaquePerspectiveRun(state, opaque, scanline, screenRows, calls);
				continue;
			}
			opaque->extensionMask = 0;
			if ((uint32_t)state->left.x < (uint32_t)opaque->previousLeft)
				opaque->extensionMask = RASTER_SPAN_EXTENDS_LEFT;
			if ((uint32_t)state->right.x > (uint32_t)opaque->previousRight)
				opaque->extensionMask |= RASTER_SPAN_EXTENDS_RIGHT;
			uint8_t *rightSource = NULL, *rightDestination = NULL;
			int32_t rightLength = 0;
			if ((opaque->extensionMask & RASTER_SPAN_EXTENDS_RIGHT) != 0) {
				if ((int16_t)state->right.x > opaque->nextScanlineRight ||
				    (int16_t)state->left.x < opaque->nextScanlineLeft) {
					Raster_DrawOpaquePerspectiveRun(state, opaque, scanline, screenRows, calls);
					continue;
				}

				rightLength = (int32_t)((uint32_t)state->right.x - (uint32_t)opaque->previousRight);
				rightDestination = screenRows[scanline] + opaque->previousRight + 1;
				rightSource = screenRows[scanline + 1] + opaque->previousRight + 1;
			} else {
				opaque->runLength = (int32_t)((uint32_t)opaque->runLength -
				                              ((uint32_t)opaque->previousRight - (uint32_t)state->right.x));
			}
			if ((opaque->extensionMask & RASTER_SPAN_EXTENDS_LEFT) != 0) {
				opaque->extensionLength = (int32_t)((uint32_t)opaque->previousLeft - (uint32_t)state->left.x);
				opaque->extensionDestination = screenRows[scanline] + state->left.x;
				opaque->extensionSource = screenRows[scanline + 1] + state->left.x;
			} else {
				const int32_t delta = (int32_t)((uint32_t)state->left.x - (uint32_t)opaque->previousLeft);
				opaque->currentRun += delta;
				opaque->nextRun += delta;
				opaque->runLength = (int32_t)((uint32_t)opaque->runLength - (uint32_t)delta);
			}
			if (opaque->runLength <= 0) {
				Raster_DrawOpaquePerspectiveRun(state, opaque, scanline, screenRows, calls);
				continue;
			}

			for (int32_t pixel = 0; pixel < opaque->runLength; ++pixel)
				opaque->nextRun[pixel] = opaque->currentRun[pixel];
			if (opaque->extensionMask != 0) {
				if (Raster_AdvanceOpaquePerspective(state, opaque, &scanline))
					break;
				Raster_DrawOpaquePerspectiveRun(state, opaque, scanline, screenRows, calls);
				if ((opaque->extensionMask & RASTER_SPAN_EXTENDS_LEFT) != 0) {
					for (int32_t pixel = 0; pixel < opaque->extensionLength; ++pixel)
						opaque->extensionDestination[pixel] = opaque->extensionSource[pixel];
				}
				if ((opaque->extensionMask & RASTER_SPAN_EXTENDS_RIGHT) != 0) {
					opaque->extensionSource = rightSource;
					opaque->extensionDestination = rightDestination;
					opaque->extensionLength = rightLength;

					for (int32_t pixel = 0; pixel < opaque->extensionLength; ++pixel)
						opaque->extensionDestination[pixel] = opaque->extensionSource[pixel];
				}
			}
			if (scanline == bottom)
				break;
		}
	}
	calls->unlock(calls->context, texture);
}
