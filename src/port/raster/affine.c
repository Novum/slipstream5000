#include "affine.h"

uint32_t Raster_textureRowScroll;

void Raster_SetTextureRowScroll(uint16_t scroll) {
	Raster_textureRowScroll = (Raster_textureRowScroll & ~(uint32_t)UINT16_MAX) | scroll;
}

void Raster_ClearTextureRowScroll(void) { Raster_textureRowScroll = 0; }

void Raster_DrawAffine(RasterAffineDrawState *state, uint16_t texture, RasterTexturedPoint *points, uint32_t count,
                       const RasterAffineDrawCalls *calls) {
	state->texture = calls->lock(calls->context, texture);
	Raster_ScaleTextureCoordinates(state->texture->width, state->texture->height, points, count);
	calls->textureRows(calls->context, state->texture);
	state->begin = points;
	uint32_t remaining = count - 1u;
	int32_t scanline = points->y;
	int32_t bottom = scanline;
	const RasterTexturedPoint *next = points + 1;
	state->left.point = points;
	state->right.point = points;
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
				state->left.point = next;
				state->right.point = next;
				scanline = next->y;
			}
		}
		++next;
	} while (--remaining != 0);
	state->end = next;
	if (bottom == scanline) {
		state->left.u = state->left.point->scaledU;
		state->left.v = state->left.point->scaledV;
		state->right.u = state->right.point->scaledU;
		state->right.v = state->right.point->scaledV;
		state->left.x = state->left.point->x;
		state->right.x = state->right.point->x;
		calls->span(calls->context, state, scanline);
	} else {
		state->bottom = bottom;
		(void)Raster_StepAffineLeft(&state->left, state->begin, state->end, scanline, state->bottom);
		(void)Raster_StepAffineRight(&state->right, state->begin, state->end, scanline, state->bottom);
		for (;;) {
			calls->span(calls->context, state, scanline);
			scanline = (int32_t)((uint32_t)scanline + 1u);
			state->left.u += (uint32_t)state->left.uStep;
			state->left.v += (uint32_t)state->left.vStep;
			state->right.u += (uint32_t)state->right.uStep;
			state->right.v += (uint32_t)state->right.vStep;
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
			    Raster_StepAffineLeft(&state->left, state->begin, state->end, scanline, state->bottom))
				break;
			if (--state->right.remainingScanlines == 0 &&
			    Raster_StepAffineRight(&state->right, state->begin, state->end, scanline, state->bottom))
				break;
			if (scanline >= state->bottom) {
				calls->span(calls->context, state, scanline);
				break;
			}
		}
	}
	calls->unlock(calls->context, texture);
}

enum { RASTER_SCANLINE_PARITY = 1u };

static bool Raster_AdvanceOpaqueAffine(RasterAffineDrawState *state, RasterOpaqueAffineState *opaque,
                                       int32_t *scanline) {
	opaque->edgeChanged = state->left.remainingScanlines == 1 || state->right.remainingScanlines == 1;
	*scanline = (int32_t)((uint32_t)*scanline + 1u);
	state->left.u += (uint32_t)state->left.uStep;
	state->left.v += (uint32_t)state->left.vStep;
	state->right.u += (uint32_t)state->right.uStep;
	state->right.v += (uint32_t)state->right.vStep;
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
	    Raster_StepAffineLeft(&state->left, state->begin, state->end, *scanline, state->bottom))
		return true;
	if (--state->right.remainingScanlines == 0 &&
	    Raster_StepAffineRight(&state->right, state->begin, state->end, *scanline, state->bottom))
		return true;
	return false;
}

static void Raster_DrawOpaqueAffineRun(RasterAffineDrawState *state, RasterOpaqueAffineState *opaque, int32_t scanline,
                                       uint8_t *const *screenRows, const RasterAffineDrawCalls *calls) {
	calls->span(calls->context, state, scanline);
	opaque->previousLeft = state->left.x;
	opaque->previousRight = state->right.x;
	opaque->currentRun = screenRows[scanline] + state->left.x;
	opaque->runLength = (int32_t)((uint32_t)state->right.x - (uint32_t)state->left.x + 1u);
	opaque->nextRun = screenRows[scanline + 1] + state->left.x;
}

void Raster_DrawOpaqueAffine(RasterAffineDrawState *state, RasterOpaqueAffineState *opaque, uint16_t texture,
                             RasterTexturedPoint *points, uint32_t count, uint8_t *const *screenRows,
                             const RasterAffineDrawCalls *calls) {
	state->texture = calls->lock(calls->context, texture);

	if (state->texture->transparentColor != -1) {
		calls->unlock(calls->context, texture);
		Raster_DrawAffine(state, texture, points, count, calls);
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
		state->left.u = state->left.point->scaledU;
		state->left.v = state->left.point->scaledV;
		state->right.u = state->right.point->scaledU;
		state->right.v = state->right.point->scaledV;
		state->left.x = state->left.point->x;
		state->right.x = state->right.point->x;
		calls->span(calls->context, state, scanline);
	} else {
		state->bottom = bottom;
		(void)Raster_StepAffineLeft(&state->left, state->begin, state->end, scanline, bottom);
		(void)Raster_StepAffineRight(&state->right, state->begin, state->end, scanline, bottom);
		Raster_DrawOpaqueAffineRun(state, opaque, scanline, screenRows, calls);
		for (;;) {

			if (Raster_AdvanceOpaqueAffine(state, opaque, &scanline))
				break;
			if (scanline >= bottom) {
				calls->span(calls->context, state, scanline);
				break;
			}
			if (((uint32_t)scanline & RASTER_SCANLINE_PARITY) == 0 || opaque->edgeChanged) {
				Raster_DrawOpaqueAffineRun(state, opaque, scanline, screenRows, calls);
				continue;
			}
			const uint32_t leftFraction = (uint32_t)state->left.xFraction + (uint16_t)state->left.xStep;
			const uint32_t rightFraction = (uint32_t)state->right.xFraction + (uint16_t)state->right.xStep;

			opaque->predictedLeft =
			    (int16_t)((uint32_t)state->left.x + ((uint32_t)state->left.xStep >> RASTER_FRACTION_BITS) +
			              (leftFraction >> RASTER_FRACTION_BITS));
			opaque->predictedRight =
			    (int16_t)((uint32_t)state->right.x + ((uint32_t)state->right.xStep >> RASTER_FRACTION_BITS) +
			              (rightFraction >> RASTER_FRACTION_BITS));
			if (opaque->predictedRight < (int16_t)state->left.x || opaque->predictedLeft > (int16_t)state->right.x) {
				Raster_DrawOpaqueAffineRun(state, opaque, scanline, screenRows, calls);
				continue;
			}
			opaque->extensions = 0;
			if ((uint32_t)state->left.x < (uint32_t)opaque->previousLeft)
				opaque->extensions = RASTER_SPAN_EXTENDS_LEFT;
			if ((uint32_t)state->right.x > (uint32_t)opaque->previousRight)
				opaque->extensions |= RASTER_SPAN_EXTENDS_RIGHT;
			uint8_t *rightSource = NULL, *rightDestination = NULL;
			int32_t rightLength = 0;
			if ((opaque->extensions & RASTER_SPAN_EXTENDS_RIGHT) != 0) {
				if ((int16_t)state->right.x > opaque->predictedRight ||
				    (int16_t)state->left.x < opaque->predictedLeft) {
					Raster_DrawOpaqueAffineRun(state, opaque, scanline, screenRows, calls);
					continue;
				}

				rightLength = (int32_t)((uint32_t)state->right.x - (uint32_t)opaque->previousRight);
				rightDestination = screenRows[scanline] + opaque->previousRight + 1;
				rightSource = screenRows[scanline + 1] + opaque->previousRight + 1;
			} else {
				opaque->runLength = (int32_t)((uint32_t)opaque->runLength -
				                              ((uint32_t)opaque->previousRight - (uint32_t)state->right.x));
			}
			if ((opaque->extensions & RASTER_SPAN_EXTENDS_LEFT) != 0) {
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
				Raster_DrawOpaqueAffineRun(state, opaque, scanline, screenRows, calls);
				continue;
			}

			for (int32_t pixel = 0; pixel < opaque->runLength; ++pixel)
				opaque->nextRun[pixel] = opaque->currentRun[pixel];
			if (opaque->extensions != 0) {
				if (Raster_AdvanceOpaqueAffine(state, opaque, &scanline))
					break;
				Raster_DrawOpaqueAffineRun(state, opaque, scanline, screenRows, calls);
				if ((opaque->extensions & RASTER_SPAN_EXTENDS_LEFT) != 0) {

					for (int32_t pixel = 0; pixel < opaque->extensionLength; ++pixel)
						opaque->extensionDestination[pixel] = opaque->extensionSource[pixel];
				}
				if ((opaque->extensions & RASTER_SPAN_EXTENDS_RIGHT) != 0) {
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
