#include "raster.h"
#include "../sprite_format.h"
#include "fixed_point.h"
#include "random_sequence.h"

enum {
	RASTER_AFFINE_EDGE_FAILED = 0,
	RASTER_AFFINE_EDGE_ADVANCED = 1,
	RASTER_AFFINE_EDGE_FINISHED = 2,
	RASTER_EDGE_HALF_PIXEL_Q16 = 1 << (RASTER_FRACTION_BITS - 1),
	RASTER_FRACTION_ONE_Q16 = 1 << RASTER_FRACTION_BITS,
	RASTER_PERSPECTIVE_BUCKET_ROUND_BIAS = 64,
	RASTER_SHADE_FRACTION_BITS = 8,
	RASTER_GRADIENT_HALF_FRACTION_Q8 = 1 << (RASTER_SHADE_FRACTION_BITS - 1),
	RASTER_DITHER_BITS_MASK = 31,
	RASTER_DITHER_INITIAL_SEED = 0x5a4a,
	RASTER_DEPTH_RATIO_NUMERATOR_SHIFT = 31,
	RASTER_DEPTH_RATIO_OUTPUT_SHIFT = RASTER_DEPTH_RATIO_NUMERATOR_SHIFT - RASTER_FRACTION_BITS,
	RASTER_TEXTURE_SPAN_ENDPOINT_BASE_TOKEN = 0x2c44c,
	RASTER_PERSPECTIVE_SPLIT_SAMPLE_COUNT = 64,
	/* Ordered subdivision slots; the adaptive split fractions are not evenly spaced. */
	RASTER_PERSPECTIVE_FIRST_SIXTEENTH_SPLIT = 0,
	RASTER_PERSPECTIVE_THIRD_SIXTEENTH_SPLIT = 2,
	RASTER_PERSPECTIVE_FIFTH_SIXTEENTH_SPLIT = 4,
	RASTER_PERSPECTIVE_SEVENTH_SIXTEENTH_SPLIT = 6,
	RASTER_PERSPECTIVE_EIGHTH_SPLIT_STRIDE = RASTER_PERSPECTIVE_SPLIT_CAPACITY / 8,
	RASTER_PERSPECTIVE_FIRST_EIGHTH_SPLIT = RASTER_PERSPECTIVE_EIGHTH_SPLIT_STRIDE - 1,
	RASTER_PERSPECTIVE_THIRD_EIGHTH_SPLIT = 3 * RASTER_PERSPECTIVE_EIGHTH_SPLIT_STRIDE - 1,
	RASTER_PERSPECTIVE_LAST_SPLIT = RASTER_PERSPECTIVE_SPLIT_CAPACITY - 2,
	RASTER_PERSPECTIVE_FIRST_QUARTER_SPLIT = RASTER_PERSPECTIVE_SPLIT_CAPACITY / 4 - 1,
	RASTER_PERSPECTIVE_MIDDLE_SPLIT = RASTER_PERSPECTIVE_SPLIT_CAPACITY / 2 - 1,
	RASTER_PERSPECTIVE_LAST_QUARTER_SPLIT = 3 * RASTER_PERSPECTIVE_SPLIT_CAPACITY / 4 - 1,
	RASTER_COPY_DWORD_BYTES = sizeof(uint32_t),
	RASTER_COPY_ALIGNMENT_MASK = RASTER_COPY_DWORD_BYTES - 1,
	RASTER_COPY_MINIMUM_DWORD_BYTES = 2 * RASTER_COPY_DWORD_BYTES,
	RASTER_PERSPECTIVE_MINIMUM_DEPTH_RATIO = 32,
	RASTER_PERSPECTIVE_DEPTH_RATIO_STEP = 128,
	RASTER_PERSPECTIVE_MAXIMUM_TABLE_DEPTH_RATIO = 64128,
	RASTER_PERSPECTIVE_ENTRY_BUCKET_SHIFT = 7,
	RASTER_PERSPECTIVE_TWO_SEGMENT_BUCKET_BIAS = 128,
	RASTER_PERSPECTIVE_MULTI_SEGMENT_BUCKET_BIAS = 64,
	RASTER_PERSPECTIVE_SHORT_SPAN_WIDTH = 20,
	RASTER_PERSPECTIVE_AFFINE_DEPTH_RATIO_MINIMUM = 64000,
	RASTER_PERSPECTIVE_TWO_SEGMENT_DEPTH_RATIO_MINIMUM = 49152,
	RASTER_PERSPECTIVE_EIGHT_SEGMENT_DEPTH_RATIO_MAXIMUM = 32768,
	RASTER_PERSPECTIVE_EIGHT_SEGMENT_WIDTH_MINIMUM = 80,
	RASTER_PERSPECTIVE_FOUR_SEGMENT_WIDTH_MINIMUM = 40,
	RASTER_PERSPECTIVE_TABLE_BASE_TOKEN = 0x33310,
	RASTER_AFFINE_POLYGON_POINT_CAPACITY = 64,
	RASTER_TEXTURE_UV_VISIT_CAPACITY = 64,
	RASTER_NORMALIZE_COMPONENT_HIGH_MASK = UINT32_MAX ^ INT16_MAX,
	RASTER_NORMALIZE_MAXIMUM_COMPONENT_BIT = 14,
	RASTER_NORMALIZE_OUTPUT_SHIFT = RASTER_FRACTION_BITS - SLIP_Q14_FRACTION_BITS
};

#include "byte_order.h"
#include "software.h"

#include <stdlib.h>
#include <string.h>

uint8_t *g_screenBufferBase;
intptr_t g_screenRowOffsets[SLIPSTREAM_SCREEN_HEIGHT];
uint8_t *g_screenRowPtrs[SLIPSTREAM_SCREEN_HEIGHT];
int32_t g_screenPitch;
static bool spriteSurfaceActive;
static uint16_t spriteSurfaceWidth, spriteSurfaceHeight;

static uint32_t rasterSpanLeftDepth;
static uint32_t rasterSpanRightDepth;

int16_t g_clipMinX;
int16_t g_clipMinY;
int16_t g_clipMaxX;
int16_t g_clipMaxY;

enum { CLIP_LEFT = 1, CLIP_RIGHT = 2, CLIP_TOP = 4, CLIP_BOTTOM = 8 };

static int Raster_TexturedPointOffsetValid(uint32_t pointBufferBase, size_t pointBufferBytes, uint32_t offset) {
	return offset >= pointBufferBase &&
	       (size_t)(offset - pointBufferBase) + sizeof(RasterTexturedPoint) <= pointBufferBytes;
}

static uint16_t Raster_AddU16WithCarry(uint16_t a, uint16_t b, bool *carryOut) {
	const uint32_t sum = (uint32_t)a + (uint32_t)b;

	*carryOut = sum > UINT16_MAX;
	return (uint16_t)sum;
}

static int32_t Raster_DivideSignedScaled14(int32_t numeratorSource, int32_t denominator) {
	const int64_t numerator = (int64_t)numeratorSource << SLIP_Q14_FRACTION_BITS;

	return (int32_t)(numerator / denominator);
}

static uint32_t Raster_MultiplyShift14RoundAdd(int32_t valueDelta, int32_t quotient, uint32_t addend, bool *carryOut) {
	const int64_t product = (int64_t)valueDelta * (int64_t)quotient;
	const uint32_t productLow = (uint32_t)product;
	const uint32_t productHigh = (uint32_t)((uint64_t)product >> 32);
	const uint32_t shifted = (productLow >> SLIP_Q14_FRACTION_BITS) | (productHigh << SLIP_Q14_DWORD_HIGH_SHIFT);

	*carryOut = (productLow & SLIP_Q14_HALF) != 0u;
	return shifted + addend + (*carryOut ? 1u : 0u);
}

static int Raster_SampleTextureByte(const uint8_t *const *textureRows, size_t textureRowCount, size_t textureRowBytes,
                                    uint32_t texU, uint32_t texV, uint8_t *sampleOut) {
	const uint16_t texY = (uint16_t)(texV >> RASTER_FRACTION_BITS);
	const uint16_t texX = (uint16_t)(texU >> RASTER_FRACTION_BITS);

	if (textureRows == NULL || texY >= textureRowCount || textureRows[texY] == NULL || texX >= textureRowBytes ||
	    sampleOut == NULL) {
		return 0;
	}
	*sampleOut = textureRows[texY][texX];
	return 1;
}

static void Raster_WriteLE32(uint8_t *p, uint32_t value) {
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
	p[2] = (uint8_t)(value >> 16);
	p[3] = (uint8_t)(value >> 24);
}

static uint32_t Raster_MultiplyUnsignedShift14(uint32_t left, uint32_t right) {
	const uint64_t product = (uint64_t)left * (uint64_t)right;

	return (uint32_t)(product >> SLIP_Q14_FRACTION_BITS);
}

static uint32_t Raster_DepthRatioBucket(uint32_t depthMin, uint32_t depthMax) {
	const int64_t numerator = (int64_t)(int32_t)depthMin * (INT64_C(1) << RASTER_DEPTH_RATIO_NUMERATOR_SHIFT);
	const int32_t quotient = (int32_t)(numerator / (int32_t)depthMax);

	return (uint32_t)quotient >> RASTER_DEPTH_RATIO_OUTPUT_SHIFT;
}

static uint16_t Raster_MultiplyUnsigned16High(uint16_t a, uint16_t b) {
	return (uint16_t)(((uint32_t)a * (uint32_t)b) >> RASTER_FRACTION_BITS);
}

static int32_t Raster_MultiplySignedShift16(uint16_t coefficient, int32_t delta) {
	const int64_t product = (int64_t)(int32_t)(uint32_t)coefficient * (int64_t)delta;

	return (int32_t)(product >> RASTER_FRACTION_BITS);
}

static int32_t Raster_DivideWrappedFixed16(int32_t delta, uint32_t divisor) {
	const uint32_t shifted = (uint32_t)delta << RASTER_FRACTION_BITS;

	return (int32_t)shifted / (int32_t)divisor;
}

static int16_t Raster_MinI16(int16_t a, int16_t b) { return a < b ? a : b; }

static int16_t Raster_MaxI16(int16_t a, int16_t b) { return a > b ? a : b; }

static uint8_t Raster_Outcode(int16_t x, int16_t y) {
	uint8_t code = 0;

	if (x < g_clipMinX) {
		code |= CLIP_LEFT;
	} else if (x > g_clipMaxX) {
		code |= CLIP_RIGHT;
	}

	if (y < g_clipMinY) {
		code |= CLIP_TOP;
	} else if (y > g_clipMaxY) {
		code |= CLIP_BOTTOM;
	}

	return code;
}

typedef struct FlatEdgeState {
	const RasterPoint *points;
	int pointCount;
	int firstIndex;
	int currentIndex;
	int bottomY;
	int scanlinesRemaining;
	int32_t xStep;
	int32_t xAccumulator;
} FlatEdgeState;

typedef struct ShadedEdgeState {
	const RasterShadedPoint *points;
	int pointCount;
	int firstIndex;
	int currentIndex;
	int bottomY;
	int scanlinesRemaining;
	int32_t xStep;
	int32_t xAccumulator;
	int16_t shadeStep;
	uint16_t shadeAccumulator;
} ShadedEdgeState;

static int Raster_FixedXInt(int32_t acc) { return acc >> RASTER_FRACTION_BITS; }

enum { RASTER_FLAT_EDGE_FRACTION_BITS = RASTER_FRACTION_BITS, RASTER_FLAT_EDGE_HALF_PIXEL = 0x8000u };

static int Raster_StepLeftFlat(FlatEdgeState *edge, int currentY) {
	if (currentY == edge->bottomY) {
		edge->xAccumulator =
		    (int32_t)(((uint32_t)edge->points[edge->currentIndex].x << RASTER_FLAT_EDGE_FRACTION_BITS) |
		              (uint16_t)edge->xAccumulator);
		return 0;
	}

	for (;;) {
		const int oldIndex = edge->currentIndex;
		const int nextIndex = oldIndex == edge->firstIndex ? edge->pointCount - 1 : oldIndex - 1;
		const int oldX = (int)edge->points[oldIndex].x;
		const int nextX = (int)edge->points[nextIndex].x;
		const int nextY = (int)edge->points[nextIndex].y;
		int dy;

		edge->currentIndex = nextIndex;
		if (currentY > nextY) {
			edge->currentIndex = oldIndex;
			return 1;
		}
		if (currentY == nextY) {
			continue;
		}

		dy = nextY - currentY;
		edge->scanlinesRemaining = dy;
		edge->xStep = Raster_DivideWrappedFixed16(nextX - oldX, (uint32_t)dy);
		edge->xAccumulator =
		    (int32_t)(((uint32_t)oldX << RASTER_FLAT_EDGE_FRACTION_BITS) + RASTER_FLAT_EDGE_HALF_PIXEL);
		return 0;
	}
}

static int Raster_StepRightFlat(FlatEdgeState *edge, int currentY) {
	if (currentY == edge->bottomY) {
		edge->xAccumulator =
		    (int32_t)(((uint32_t)edge->points[edge->currentIndex].x << RASTER_FLAT_EDGE_FRACTION_BITS) |
		              (uint16_t)edge->xAccumulator);
		return 0;
	}

	for (;;) {
		const int oldIndex = edge->currentIndex;
		int nextIndex = oldIndex + 1;
		const int oldX = (int)edge->points[oldIndex].x;
		int nextX;
		int nextY;
		int dy;

		if (nextIndex == edge->pointCount) {
			nextIndex = edge->firstIndex;
		}
		nextX = (int)edge->points[nextIndex].x;
		nextY = (int)edge->points[nextIndex].y;
		edge->currentIndex = nextIndex;
		if (currentY > nextY) {
			edge->currentIndex = oldIndex;
			return 1;
		}
		if (currentY == nextY) {
			continue;
		}

		dy = nextY - currentY;
		edge->scanlinesRemaining = dy;
		edge->xStep = Raster_DivideWrappedFixed16(nextX - oldX, (uint32_t)dy);
		edge->xAccumulator =
		    (int32_t)(((uint32_t)oldX << RASTER_FLAT_EDGE_FRACTION_BITS) + RASTER_FLAT_EDGE_HALF_PIXEL);
		return 0;
	}
}

static void Raster_AdvanceFlatEdge(FlatEdgeState *edge) {
	edge->xAccumulator += edge->xStep;
	--edge->scanlinesRemaining;
}

static int Raster_StepLeftShaded(ShadedEdgeState *edge, int currentY) {
	if (currentY == edge->bottomY) {
		edge->xAccumulator = (int32_t)((uint32_t)edge->points[edge->currentIndex].x << RASTER_FRACTION_BITS) |
		                     (uint16_t)edge->xAccumulator;
		edge->shadeAccumulator = edge->points[edge->currentIndex].shade;
		return 0;
	}

	for (;;) {
		const int oldIndex = edge->currentIndex;
		const int nextIndex = oldIndex == edge->firstIndex ? edge->pointCount - 1 : oldIndex - 1;
		const int oldX = (int)edge->points[oldIndex].x;
		const int nextX = (int)edge->points[nextIndex].x;
		const int nextY = (int)edge->points[nextIndex].y;
		const uint16_t oldShade = edge->points[oldIndex].shade;
		const uint16_t nextShade = edge->points[nextIndex].shade;
		int dy;

		edge->currentIndex = nextIndex;
		if (currentY > nextY) {
			edge->currentIndex = oldIndex;
			return 1;
		}
		if (currentY == nextY) {
			continue;
		}

		dy = nextY - currentY;
		edge->scanlinesRemaining = dy;
		edge->xStep = Raster_DivideWrappedFixed16(nextX - oldX, (uint32_t)dy);
		edge->xAccumulator = ((int32_t)oldX << RASTER_FRACTION_BITS) + RASTER_EDGE_HALF_PIXEL_Q16;
		edge->shadeAccumulator = oldShade;
		edge->shadeStep = (int16_t)((int16_t)(nextShade - oldShade) / dy);
		return 0;
	}
}

static int Raster_StepRightShaded(ShadedEdgeState *edge, int currentY) {
	if (currentY == edge->bottomY) {
		edge->xAccumulator = (int32_t)((uint32_t)edge->points[edge->currentIndex].x << RASTER_FRACTION_BITS) |
		                     (uint16_t)edge->xAccumulator;
		edge->shadeAccumulator = edge->points[edge->currentIndex].shade;
		return 0;
	}

	for (;;) {
		const int oldIndex = edge->currentIndex;
		int nextIndex = oldIndex + 1;
		const int oldX = (int)edge->points[oldIndex].x;
		int nextX;
		int nextY;
		const uint16_t oldShade = edge->points[oldIndex].shade;
		uint16_t nextShade;
		int dy;

		if (nextIndex == edge->pointCount) {
			nextIndex = edge->firstIndex;
		}
		nextX = (int)edge->points[nextIndex].x;
		nextY = (int)edge->points[nextIndex].y;
		nextShade = edge->points[nextIndex].shade;
		edge->currentIndex = nextIndex;
		if (currentY > nextY) {
			edge->currentIndex = oldIndex;
			return 1;
		}
		if (currentY == nextY) {
			continue;
		}

		dy = nextY - currentY;
		edge->scanlinesRemaining = dy;
		edge->xStep = Raster_DivideWrappedFixed16(nextX - oldX, (uint32_t)dy);
		edge->xAccumulator = ((int32_t)oldX << RASTER_FRACTION_BITS) + RASTER_EDGE_HALF_PIXEL_Q16;
		edge->shadeAccumulator = oldShade;
		edge->shadeStep = (int16_t)((int16_t)(nextShade - oldShade) / dy);
		return 0;
	}
}

static void Raster_AdvanceShadedEdge(ShadedEdgeState *edge) {
	edge->xAccumulator += edge->xStep;
	edge->shadeAccumulator = (uint16_t)(edge->shadeAccumulator + (uint16_t)edge->shadeStep);
	--edge->scanlinesRemaining;
}

static void Raster_FillShadedSpan(uint8_t leftColor, uint8_t rightColor, int16_t y, int16_t x0, int16_t x1) {
	uint8_t *dst;
	int32_t span;
	uint16_t count;
	int colorStep;
	uint16_t colorDeltaPlusOne;

	if (x1 < x0) {
		return;
	}
	span = (int32_t)x1 - (int32_t)x0;
	if (span < 0) {
		return;
	}
	if (leftColor == rightColor) {
		Raster_FillSolidSpan(leftColor, y, x0, x1);
		return;
	}

	dst = g_screenRowPtrs[y] + x0;
	count = (uint16_t)(span + 1);
	colorStep = 1;
	if (rightColor >= leftColor) {
		colorDeltaPlusOne = (uint16_t)(rightColor - leftColor + 1u);
	} else {
		colorDeltaPlusOne = (uint16_t)(leftColor - rightColor + 1u);
		colorStep = -1;
	}

	if (colorDeltaPlusOne <= count) {
		const uint16_t quotient = (uint16_t)(((uint32_t)count << RASTER_SHADE_FRACTION_BITS) / colorDeltaPlusOne);
		const uint8_t stepWhole = (uint8_t)(quotient >> RASTER_SHADE_FRACTION_BITS);
		const uint8_t stepFrac = (uint8_t)quotient;
		uint8_t frac = RASTER_GRADIENT_HALF_FRACTION_Q8;
		int remaining = count;
		uint8_t color = leftColor;

		while (remaining > 0) {
			const uint16_t fracSum = (uint16_t)frac + stepFrac;
			const int run = stepWhole + (fracSum > UINT8_MAX ? 1 : 0);

			frac = (uint8_t)fracSum;

			remaining -= run;
			memset(dst, color, (size_t)run);
			dst += run;
			color = (uint8_t)(color + colorStep);
		}
	} else {
		const uint16_t quotient = (uint16_t)(((uint32_t)colorDeltaPlusOne << RASTER_SHADE_FRACTION_BITS) / count);
		const uint8_t stepWhole = (uint8_t)(quotient >> RASTER_SHADE_FRACTION_BITS);
		const uint8_t stepFrac = (uint8_t)quotient;
		uint8_t frac = RASTER_GRADIENT_HALF_FRACTION_Q8;
		uint8_t color = leftColor;
		uint16_t i;

		for (i = 0; i < count; ++i) {
			uint16_t fracSum;

			*dst++ = color;
			if (colorStep > 0) {
				fracSum = (uint16_t)frac + stepFrac;
				frac = (uint8_t)fracSum;
				color = (uint8_t)(color + stepWhole + (fracSum > UINT8_MAX ? 1u : 0u));
			} else {
				const uint8_t oldFrac = frac;

				frac = (uint8_t)(frac - stepFrac);
				color = (uint8_t)(color - stepWhole - (oldFrac < stepFrac ? 1u : 0u));
			}
		}
	}
}

static uint16_t Raster_DitherLfsrStep(uint16_t seed) {
	uint16_t next = (uint16_t)((seed + 1u) >> 1);

	if (((seed + 1u) & 1u) != 0) {
		next ^= SLIP_RANDOM_LFSR_FEEDBACK_MASK;
	}
	return next;
}

static uint8_t Raster_DitheredSpanPixel(uint8_t color, uint8_t ditherMask, uint16_t *seed) {
	*seed = Raster_DitherLfsrStep(*seed);
	return (uint8_t)((uint8_t)(*seed & ditherMask) + color);
}

static uint8_t Raster_DitherMaskFromBl(uint8_t ditherBits) {
	const uint8_t maskedBits = (uint8_t)(ditherBits & RASTER_DITHER_BITS_MASK);
	uint32_t mask;

	if (maskedBits == 0) {
		mask = 0;
	} else {
		mask = (1u << maskedBits) - 1u;
	}
	return (uint8_t)mask;
}

static void Raster_FillDitheredSpanCore(uint8_t color, uint8_t ditherMask, uint16_t *seed, int16_t y, int16_t x0,
                                        int16_t x1) {
	uint8_t *dst;
	int32_t count;

	if (x1 <= x0) {
		return;
	}

	dst = g_screenRowPtrs[y] + x0;
	count = (int32_t)x1 - (int32_t)x0 + 1;
	while (count != 0) {
		*dst++ = Raster_DitheredSpanPixel(color, ditherMask, seed);
		--count;
	}
}

void Raster_SetScreenBufferRows(uint8_t *screenBufferBase, int32_t pitch) {
	int y;
	intptr_t rowOffset = 0;

	g_screenBufferBase = screenBufferBase;
	g_screenPitch = pitch;

	for (y = 0; y < SLIPSTREAM_SCREEN_HEIGHT; ++y) {
		g_screenRowOffsets[y] = rowOffset;
		g_screenRowPtrs[y] = screenBufferBase + rowOffset;
		rowOffset += pitch;
	}
}

void Raster_BindSprite(uint8_t *pixels, uint16_t width, uint16_t height, RasterSurfaceBinding *saved) {
	spriteSurfaceActive = true;
	saved->screenBuffer = g_screenBufferBase;
	saved->screenPitch = g_screenPitch;
	spriteSurfaceWidth = width;
	spriteSurfaceHeight = height;
	Raster_SetScreenBufferRows(pixels, width);
	Raster_GetClipRect(&saved->minX, &saved->minY, &saved->maxX, &saved->maxY);
	RasterSurfaceBounds bounds = Raster_GetSurfaceBounds();
	Raster_SetClipRect((int16_t)bounds.left, (int16_t)bounds.top, (int16_t)bounds.right, (int16_t)bounds.bottom);
}

void Raster_RestoreScreen(const RasterSurfaceBinding *saved) {
	Raster_SetClipRect(saved->minX, saved->minY, saved->maxX, saved->maxY);
	Raster_SetScreenBufferRows(saved->screenBuffer, saved->screenPitch);
	spriteSurfaceActive = false;
}

RasterSurfaceBounds Raster_GetSurfaceBounds(void) {
	if (spriteSurfaceActive)
		return (RasterSurfaceBounds){0, 0, (int32_t)spriteSurfaceWidth - 1, (int32_t)spriteSurfaceHeight - 1};
	return (RasterSurfaceBounds){0, 0, SLIPSTREAM_SCREEN_WIDTH - 1, SLIPSTREAM_SCREEN_HEIGHT - 1};
}

void Raster_SetClipRect(int16_t minX, int16_t minY, int16_t maxX, int16_t maxY) {
	g_clipMinX = minX;
	g_clipMinY = minY;
	g_clipMaxX = maxX;
	g_clipMaxY = maxY;
}

void Raster_GetClipRect(int16_t *minX, int16_t *minY, int16_t *maxX, int16_t *maxY) {
	if (minX != NULL) {
		*minX = g_clipMinX;
	}
	if (minY != NULL) {
		*minY = g_clipMinY;
	}
	if (maxX != NULL) {
		*maxX = g_clipMaxX;
	}
	if (maxY != NULL) {
		*maxY = g_clipMaxY;
	}
}

bool Raster_ClipLineToViewport(int16_t *x0, int16_t *y0, int16_t *x1, int16_t *y1) {
	int32_t startX = *x0;
	int32_t startY = *y0;
	int32_t endX = *x1;
	int32_t endY = *y1;
	uint8_t startClipMask = Raster_Outcode((int16_t)startX, (int16_t)startY);
	uint8_t endClipMask = Raster_Outcode((int16_t)endX, (int16_t)endY);

	while ((startClipMask | endClipMask) != 0) {
		int32_t intersectionX;
		int32_t intersectionY;
		const uint8_t selectedClipMask = startClipMask != 0 ? startClipMask : endClipMask;

		if ((startClipMask & endClipMask) != 0) {
			return false;
		}

		if ((selectedClipMask & CLIP_LEFT) != 0) {
			intersectionY = startY + (endY - startY) * (g_clipMinX - startX) / (endX - startX);
			intersectionX = g_clipMinX;
		} else if ((selectedClipMask & CLIP_RIGHT) != 0) {
			intersectionY = startY + (endY - startY) * (g_clipMaxX - startX) / (endX - startX);
			intersectionX = g_clipMaxX;
		} else if ((selectedClipMask & CLIP_BOTTOM) != 0) {
			intersectionX = startX + (endX - startX) * (g_clipMaxY - startY) / (endY - startY);
			intersectionY = g_clipMaxY;
		} else {
			intersectionX = startX + (endX - startX) * (g_clipMinY - startY) / (endY - startY);
			intersectionY = g_clipMinY;
		}

		if (selectedClipMask == startClipMask) {
			startX = intersectionX;
			startY = intersectionY;
			startClipMask = Raster_Outcode((int16_t)startX, (int16_t)startY);
		} else {
			endX = intersectionX;
			endY = intersectionY;
			endClipMask = Raster_Outcode((int16_t)endX, (int16_t)endY);
		}
	}

	*x0 = (int16_t)startX;
	*y0 = (int16_t)startY;
	*x1 = (int16_t)endX;
	*y1 = (int16_t)endY;
	return true;
}

void Raster_PutPixelClipped(uint8_t color, int16_t y, int16_t x) {
	if (x >= g_clipMinX && x <= g_clipMaxX && y >= g_clipMinY && y <= g_clipMaxY) {
		g_screenRowPtrs[y][x] = color;
	}
}

void RasterSoftware_DrawLineSolid(uint8_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
	uint16_t startX = (uint16_t)x0;
	uint16_t endX = (uint16_t)x1;
	uint16_t startY = (uint16_t)y0;
	uint16_t endY = (uint16_t)y1;
	int32_t rowStep;
	uint8_t *dst;
	uint16_t deltaX;
	uint16_t deltaY;
	uint16_t error;
	uint32_t count;

	if (x0 < x1) {
		const uint16_t tmpX = startX;
		const uint16_t tmpY = startY;
		startX = endX;
		endX = tmpX;
		startY = endY;
		endY = tmpY;
	}

	if (startY == endY) {
		uint16_t x;
		uint8_t *const row = g_screenRowPtrs[endY];
		for (x = endX; x <= startX; ++x) {
			row[x] = color;
			if (x == startX) {
				break;
			}
		}
		return;
	}

	deltaX = (uint16_t)(startX - endX);
	deltaY = (uint16_t)(startY - endY);
	rowStep = g_screenPitch;
	if (startY < endY) {
		rowStep = -g_screenPitch;
		deltaY = (uint16_t)(-deltaY);
	}
	dst = g_screenRowPtrs[endY] + endX;

	if (deltaX == 0) {
		count = (uint32_t)deltaY + 1u;
		for (uint32_t pixelIndex = 0; pixelIndex < count; ++pixelIndex) {
			*dst = color;
			dst += rowStep;
		}
		return;
	}

	if (deltaX < deltaY) {
		count = deltaY;
		error = (uint16_t)((deltaY >> 1) - deltaX);
		for (uint32_t pixelIndex = 0; pixelIndex < count; ++pixelIndex) {
			if ((int16_t)error >= 0) {
				*dst = color;
				dst += rowStep;
			} else {
				error = (uint16_t)(error + deltaY);
				*dst = color;
				++dst;
				dst += rowStep;
			}
			error = (uint16_t)(error - deltaX);
		}
		*dst = color;
		return;
	}

	if (deltaX == deltaY) {
		count = deltaY;
		++rowStep;
		for (uint32_t pixelIndex = 0; pixelIndex < count; ++pixelIndex) {
			*dst = color;
			dst += rowStep;
		}
		*dst = color;
		return;
	}

	count = (uint32_t)deltaX + 1u;
	error = (uint16_t)(deltaX >> 1);
	for (uint32_t pixelIndex = 0; pixelIndex < count; ++pixelIndex) {
		*dst = color;
		++dst;
		error = (uint16_t)(error - deltaY);
		if ((int16_t)error < 0) {
			error = (uint16_t)(error + deltaX);
			dst += rowStep;
		}
	}
}

void RasterSoftware_DrawLineClipped(uint8_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
	if (Raster_ClipLineToViewport(&x0, &y0, &x1, &y1)) {
		RasterSoftware_DrawLineSolid(color, x0, y0, x1, y1);
	}
}

void Raster_FillSolidSpan(uint8_t color, int16_t y, int16_t x0, int16_t x1) {
	if (x1 < x0) {
		return;
	}

	memset(g_screenRowPtrs[y] + x0, color, (size_t)(x1 - x0 + 1));
}

void RasterSoftware_FillRectClipped(uint8_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
	int16_t y;

	if (x1 < x0) {
		const int16_t tmp = x0;
		x0 = x1;
		x1 = tmp;
	}
	if (y1 < y0) {
		const int16_t tmp = y0;
		y0 = y1;
		y1 = tmp;
	}

	if (x0 > g_clipMaxX || x1 < g_clipMinX || y0 > g_clipMaxY || y1 < g_clipMinY) {
		return;
	}

	x0 = Raster_MaxI16(x0, g_clipMinX);
	y0 = Raster_MaxI16(y0, g_clipMinY);
	x1 = Raster_MinI16(x1, g_clipMaxX);
	y1 = Raster_MinI16(y1, g_clipMaxY);

	for (y = y0; y <= y1; ++y) {
		Raster_FillSolidSpan(color, y, x0, x1);
	}
}

void RasterSoftware_FillRectUnchecked(uint16_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
	int16_t y;

	if ((color & RASTER_RECTANGLE_OUTLINE_FLAG) != 0) {
		color &= UINT16_MAX ^ RASTER_RECTANGLE_OUTLINE_FLAG;
		RasterSoftware_DrawLineSolid((uint8_t)color, x1, y0, x1, y1);
		RasterSoftware_DrawLineSolid((uint8_t)color, x0, y1, x1, y1);
		RasterSoftware_DrawLineSolid((uint8_t)color, x0, y0, x0, y1);
		RasterSoftware_DrawLineSolid((uint8_t)color, x0, y0, x1, y0);
		return;
	}

	for (y = y0; y <= y1; ++y) {
		Raster_FillSolidSpan((uint8_t)color, y, x0, x1);
	}
}

static uint32_t scaledSpriteHorizontalStep, scaledSpriteVerticalStep;
static uint32_t scaledSpriteSourceWidth;
static uint16_t scaledSpriteColumns, scaledSpriteTransparent;

void RasterSoftware_DrawSpriteScaled(const uint8_t *record, size_t recordBytes, const uint8_t *pixels,
                                     size_t pixelBytes, int16_t left, int16_t top, int16_t right, int16_t bottom) {
	(void)recordBytes;
	(void)pixelBytes;

	if (right < g_clipMinX || left > g_clipMaxX || top > g_clipMaxY || bottom < g_clipMinY)
		return;
	uint16_t columns = (uint16_t)(right - left);
	if (columns == 0) {
		++columns;
	} else {
		++columns;
		const uint32_t source = (uint32_t)(int32_t)(int16_t)SlipBytes_ReadLE16(record + SLIP_SPRITE_WIDTH_OFFSET);
		const int32_t numerator = (int32_t)((source >> 16) | (source << 16));
		scaledSpriteHorizontalStep = (uint32_t)(numerator / (int16_t)columns);
	}
	scaledSpriteColumns = columns;
	uint16_t rows = 1;
	const uint16_t heightDifference = (uint16_t)(bottom - top);
	if (heightDifference != 0) {
		rows = (uint16_t)(heightDifference + 1);
		const uint32_t source = (uint32_t)(int32_t)(int16_t)SlipBytes_ReadLE16(record + SLIP_SPRITE_HEIGHT_OFFSET);
		const int32_t numerator = (int32_t)((source >> 16) | (source << 16));
		const uint32_t step = (uint32_t)(numerator / (int16_t)rows);

		const uint16_t wholeBytes = (uint16_t)((int16_t)(step >> RASTER_FRACTION_BITS) *
		                                       (int16_t)SlipBytes_ReadLE16(record + SLIP_SPRITE_WIDTH_OFFSET));
		scaledSpriteVerticalStep = (step & UINT16_MAX) | ((uint32_t)wholeBytes << 16);
	}
	scaledSpriteSourceWidth = SlipBytes_ReadLE16(record + SLIP_SPRITE_WIDTH_OFFSET);
	scaledSpriteTransparent = SlipBytes_ReadLE16(record + SLIP_SPRITE_TRANSPARENT_COLOUR_OFFSET);
	uint32_t sourceOffset = 0;
	if (left < g_clipMinX) {
		const uint16_t skipped = (uint16_t)(g_clipMinX - left);
		scaledSpriteColumns = (uint16_t)(scaledSpriteColumns - skipped);
		const uint32_t product = (uint32_t)(int32_t)(int16_t)skipped * scaledSpriteHorizontalStep;
		sourceOffset += (uint16_t)(product >> RASTER_FRACTION_BITS);
		left = g_clipMinX;
	}
	if (right > g_clipMaxX)
		scaledSpriteColumns = (uint16_t)(scaledSpriteColumns - (uint16_t)(right - g_clipMaxX));
	if (top < g_clipMinY) {
		const uint16_t skipped = (uint16_t)(g_clipMinY - top);
		rows = (uint16_t)(rows - skipped);
		const uint16_t wholeBytes = (uint16_t)(skipped * (uint16_t)(scaledSpriteVerticalStep >> RASTER_FRACTION_BITS));
		const uint16_t carriedRows =
		    (uint16_t)(((uint32_t)skipped * (uint16_t)scaledSpriteVerticalStep) >> RASTER_FRACTION_BITS);
		sourceOffset += (uint16_t)(wholeBytes + (uint16_t)(scaledSpriteSourceWidth * carriedRows));
		top = g_clipMinY;
	}
	if (bottom > g_clipMaxY)
		rows = (uint16_t)(rows - (uint16_t)(bottom - g_clipMaxY));
	uint8_t *destination = g_screenRowPtrs[(uint16_t)top] + (uint16_t)left;
	uint16_t verticalFraction = 0;

	if (scaledSpriteTransparent == UINT16_MAX) {
		do {
			uint32_t sampleOffset = sourceOffset;
			uint16_t horizontalFraction = 0;
			uint32_t remaining = scaledSpriteColumns;
			uint8_t *pixel = destination;
			do {
				*pixel++ = pixels[sampleOffset];
				const uint32_t sum = (uint32_t)horizontalFraction + (uint16_t)scaledSpriteHorizontalStep;
				horizontalFraction = (uint16_t)sum;
				sampleOffset += (scaledSpriteHorizontalStep >> RASTER_FRACTION_BITS) + (sum >> RASTER_FRACTION_BITS);
			} while (--remaining != 0);
			const uint32_t sum = (uint32_t)verticalFraction + (uint16_t)scaledSpriteVerticalStep;
			verticalFraction = (uint16_t)sum;
			if (sum > UINT16_MAX)
				sourceOffset += scaledSpriteSourceWidth;
			sourceOffset += scaledSpriteVerticalStep >> RASTER_FRACTION_BITS;
			destination += g_screenPitch;
		} while (--rows != 0);
	} else {
		do {
			uint32_t sampleOffset = sourceOffset;
			uint16_t horizontalFraction = 0;
			uint32_t remaining = scaledSpriteColumns;
			uint8_t *pixel = destination;
			do {
				const uint8_t sample = pixels[sampleOffset];
				if (sample != (uint8_t)scaledSpriteTransparent)
					*pixel = sample;
				++pixel;
				const uint32_t sum = (uint32_t)horizontalFraction + (uint16_t)scaledSpriteHorizontalStep;
				horizontalFraction = (uint16_t)sum;
				sampleOffset += (scaledSpriteHorizontalStep >> RASTER_FRACTION_BITS) + (sum >> RASTER_FRACTION_BITS);
			} while (--remaining != 0);
			const uint32_t sum = (uint32_t)verticalFraction + (uint16_t)scaledSpriteVerticalStep;
			verticalFraction = (uint16_t)sum;
			if (sum > UINT16_MAX)
				sourceOffset += scaledSpriteSourceWidth;
			sourceOffset += scaledSpriteVerticalStep >> RASTER_FRACTION_BITS;
			destination += g_screenPitch;
		} while (--rows != 0);
	}
}

void RasterSoftware_DrawSolidFlatPolygon(uint8_t color, const RasterPoint *points, uint16_t pointCount) {
	int i;
	int topY;
	int bottomY;
	int leftTop;
	int rightTop;
	FlatEdgeState left;
	FlatEdgeState right;
	int y;

	if (points == NULL || pointCount == 0) {
		return;
	}

	topY = (int)points[0].y;
	bottomY = topY;
	leftTop = 0;
	rightTop = 0;
	for (i = 1; i < pointCount; ++i) {
		const int x = (int)points[i].x;
		const int yPoint = (int)points[i].y;

		if (bottomY < yPoint) {
			bottomY = yPoint;
		}
		if (topY > yPoint) {
			topY = yPoint;
			leftTop = i;
			rightTop = i;
		} else if (topY == yPoint) {
			if (x < points[leftTop].x) {
				leftTop = i;
			}
			if (x > points[rightTop].x) {
				rightTop = i;
			}
		}
	}

	if (bottomY == topY) {
		Raster_FillSolidSpan(color, (int16_t)topY, (int16_t)points[leftTop].x, (int16_t)points[rightTop].x);
		return;
	}

	memset(&left, 0, sizeof(left));
	memset(&right, 0, sizeof(right));
	left.points = points;
	left.pointCount = pointCount;
	left.firstIndex = 0;
	left.currentIndex = leftTop;
	left.bottomY = bottomY;
	right.points = points;
	right.pointCount = pointCount;
	right.firstIndex = 0;
	right.currentIndex = rightTop;
	right.bottomY = bottomY;

	y = topY;
	if (Raster_StepLeftFlat(&left, y) || Raster_StepRightFlat(&right, y)) {
		return;
	}

	while (y < bottomY) {
		Raster_FillSolidSpan(color, (int16_t)y, (int16_t)Raster_FixedXInt(left.xAccumulator),
		                     (int16_t)Raster_FixedXInt(right.xAccumulator));
		++y;
		Raster_AdvanceFlatEdge(&left);
		Raster_AdvanceFlatEdge(&right);
		if (left.scanlinesRemaining == 0 && Raster_StepLeftFlat(&left, y)) {
			return;
		}
		if (right.scanlinesRemaining == 0 && Raster_StepRightFlat(&right, y)) {
			return;
		}
	}

	Raster_FillSolidSpan(color, (int16_t)y, (int16_t)Raster_FixedXInt(left.xAccumulator),
	                     (int16_t)Raster_FixedXInt(right.xAccumulator));
}

void RasterSoftware_DrawShadedFlatPolygon(const RasterShadedPoint *points, uint16_t pointCount) {
	int i;
	int topY;
	int bottomY;
	int leftTop;
	int rightTop;
	ShadedEdgeState left;
	ShadedEdgeState right;
	int y;

	if (points == NULL || pointCount == 0) {
		return;
	}

	topY = (int)points[0].y;
	bottomY = topY;
	leftTop = 0;
	rightTop = 0;
	for (i = 1; i < pointCount; ++i) {
		const int x = (int)points[i].x;
		const int yPoint = (int)points[i].y;

		if (bottomY < yPoint) {
			bottomY = yPoint;
		}
		if (topY > yPoint) {
			topY = yPoint;
			leftTop = i;
			rightTop = i;
		} else if (topY == yPoint) {
			if (x < points[leftTop].x) {
				leftTop = i;
			}
			if (x > points[rightTop].x) {
				rightTop = i;
			}
		}
	}

	if (bottomY == topY) {
		Raster_FillShadedSpan((uint8_t)(points[leftTop].shade >> RASTER_SHADE_FRACTION_BITS),
		                      (uint8_t)(points[rightTop].shade >> RASTER_SHADE_FRACTION_BITS), (int16_t)topY,
		                      (int16_t)points[leftTop].x, (int16_t)points[rightTop].x);
		return;
	}

	memset(&left, 0, sizeof(left));
	memset(&right, 0, sizeof(right));
	left.points = points;
	left.pointCount = pointCount;
	left.firstIndex = 0;
	left.currentIndex = leftTop;
	left.bottomY = bottomY;
	right.points = points;
	right.pointCount = pointCount;
	right.firstIndex = 0;
	right.currentIndex = rightTop;
	right.bottomY = bottomY;

	y = topY;
	if (Raster_StepLeftShaded(&left, y) || Raster_StepRightShaded(&right, y)) {
		return;
	}

	while (y < bottomY) {
		Raster_FillShadedSpan((uint8_t)(left.shadeAccumulator >> RASTER_SHADE_FRACTION_BITS),
		                      (uint8_t)(right.shadeAccumulator >> RASTER_SHADE_FRACTION_BITS), (int16_t)y,
		                      (int16_t)Raster_FixedXInt(left.xAccumulator),
		                      (int16_t)Raster_FixedXInt(right.xAccumulator));
		++y;
		Raster_AdvanceShadedEdge(&left);
		Raster_AdvanceShadedEdge(&right);
		if (left.scanlinesRemaining == 0 && Raster_StepLeftShaded(&left, y)) {
			return;
		}
		if (right.scanlinesRemaining == 0 && Raster_StepRightShaded(&right, y)) {
			return;
		}
	}

	Raster_FillShadedSpan((uint8_t)(left.shadeAccumulator >> RASTER_SHADE_FRACTION_BITS),
	                      (uint8_t)(right.shadeAccumulator >> RASTER_SHADE_FRACTION_BITS), (int16_t)y,
	                      (int16_t)Raster_FixedXInt(left.xAccumulator), (int16_t)Raster_FixedXInt(right.xAccumulator));
}

void RasterSoftware_DrawDitheredFlatPolygon(uint8_t color, uint8_t ditherBits, const RasterPoint *points,
                                            uint16_t pointCount) {
	int i;
	int topY;
	int bottomY;
	int leftTop;
	int rightTop;
	FlatEdgeState left;
	FlatEdgeState right;
	uint8_t ditherMask;
	uint16_t seed;
	int y;

	if (points == NULL || pointCount == 0) {
		return;
	}

	ditherMask = Raster_DitherMaskFromBl(ditherBits);
	seed = RASTER_DITHER_INITIAL_SEED;
	topY = (int)points[0].y;
	bottomY = topY;
	leftTop = 0;
	rightTop = 0;
	for (i = 1; i < pointCount; ++i) {
		const int x = (int)points[i].x;
		const int yPoint = (int)points[i].y;

		if (bottomY < yPoint) {
			bottomY = yPoint;
		}
		if (topY > yPoint) {
			topY = yPoint;
			leftTop = i;
			rightTop = i;
		} else if (topY == yPoint) {
			if (x < points[leftTop].x) {
				leftTop = i;
			}
			if (x > points[rightTop].x) {
				rightTop = i;
			}
		}
	}

	if (bottomY == topY) {
		Raster_FillDitheredSpanCore(color, ditherMask, &seed, (int16_t)topY, (int16_t)points[leftTop].x,
		                            (int16_t)points[rightTop].x);
		return;
	}

	memset(&left, 0, sizeof(left));
	memset(&right, 0, sizeof(right));
	left.points = points;
	left.pointCount = pointCount;
	left.firstIndex = 0;
	left.currentIndex = leftTop;
	left.bottomY = bottomY;
	right.points = points;
	right.pointCount = pointCount;
	right.firstIndex = 0;
	right.currentIndex = rightTop;
	right.bottomY = bottomY;

	y = topY;
	if (Raster_StepLeftFlat(&left, y) || Raster_StepRightFlat(&right, y)) {
		return;
	}

	while (y < bottomY) {
		Raster_FillDitheredSpanCore(color, ditherMask, &seed, (int16_t)y, (int16_t)Raster_FixedXInt(left.xAccumulator),
		                            (int16_t)Raster_FixedXInt(right.xAccumulator));
		++y;
		Raster_AdvanceFlatEdge(&left);
		Raster_AdvanceFlatEdge(&right);
		if (left.scanlinesRemaining == 0 && Raster_StepLeftFlat(&left, y)) {
			return;
		}
		if (right.scanlinesRemaining == 0 && Raster_StepRightFlat(&right, y)) {
			return;
		}
	}

	Raster_FillDitheredSpanCore(color, ditherMask, &seed, (int16_t)y, (int16_t)Raster_FixedXInt(left.xAccumulator),
	                            (int16_t)Raster_FixedXInt(right.xAccumulator));
}

int Raster_BuildTextureRowTable(const uint8_t *texturePayload, size_t texturePayloadBytes, uint32_t rowScroll,
                                const uint8_t **textureRows, size_t rowCapacity, RasterTextureRowTable *result) {
	uint16_t textureWidth;
	uint16_t textureHeight;
	uint32_t rotatedRows;
	size_t rowsWritten;

	if (result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->pushad = true;
	result->rowScroll = rowScroll;
	if (texturePayload == NULL || texturePayloadBytes < SLIP_SPRITE_HEADER_BYTES || textureRows == NULL) {
		return 0;
	}
	textureWidth = SlipBytes_ReadLE16(texturePayload);
	textureHeight = SlipBytes_ReadLE16(texturePayload + SLIP_SPRITE_HEIGHT_OFFSET);
	result->textureWidth = textureWidth;
	result->textureHeight = textureHeight;
	if (textureWidth == 0u || textureHeight == 0u || textureHeight > rowCapacity ||
	    (size_t)textureHeight > (SIZE_MAX - SLIP_SPRITE_HEADER_BYTES) / (size_t)textureWidth ||
	    texturePayloadBytes < SLIP_SPRITE_HEADER_BYTES + (size_t)textureWidth * textureHeight) {
		return 0;
	}
	rotatedRows = 0;
	if (rowScroll != 0u) {
		rotatedRows = Raster_MultiplyUnsignedShift14(rowScroll, textureHeight);
	}
	result->rotationRowOffset = rotatedRows;
	rowsWritten = 0;
	if (rowScroll == 0u || rotatedRows == 0u || rotatedRows == textureHeight) {
		const uint8_t *row = texturePayload + SLIP_SPRITE_HEADER_BYTES;

		result->directRows = true;
		while (textureHeight != 0u) {
			textureRows[rowsWritten] = row;
			row += textureWidth;
			++rowsWritten;
			--textureHeight;
		}
	} else {
		const uint8_t *row = texturePayload + SLIP_SPRITE_HEADER_BYTES + (size_t)rotatedRows * textureWidth;
		uint32_t firstPassRows = (uint32_t)textureHeight - rotatedRows;
		uint32_t secondPassRows = rotatedRows;

		result->usedRotatedRows = true;
		result->firstPassRows = firstPassRows;
		while (firstPassRows != 0u) {
			textureRows[rowsWritten] = row;
			row += textureWidth;
			++rowsWritten;
			--firstPassRows;
		}
		row = texturePayload + SLIP_SPRITE_HEADER_BYTES;
		result->secondPassRows = secondPassRows;
		while (secondPassRows != 0u) {
			textureRows[rowsWritten] = row;
			row += textureWidth;
			++rowsWritten;
			--secondPassRows;
		}
	}
	result->rowsWritten = rowsWritten;
	result->popad = true;
	return 1;
}

void Raster_ScaleTextureCoordinates(uint16_t width, uint16_t height, RasterTexturedPoint *points, uint32_t count) {
	const unsigned textureFractionBits = 14;
	const unsigned dimensionFractionBits = 16;
	const uint32_t horizontalExtent = (uint32_t)(uint16_t)(width - 1u) << dimensionFractionBits | UINT16_MAX;
	const uint32_t verticalExtent = (uint32_t)(uint16_t)(height - 1u) << dimensionFractionBits | UINT16_MAX;
	do {
		points->scaledU = (uint32_t)(((uint64_t)points->u * horizontalExtent) >> textureFractionBits);
		points->scaledV = (uint32_t)(((uint64_t)points->v * verticalExtent) >> textureFractionBits);
		++points;
	} while (--count != 0);
}

int Raster_PrepareTextureUVExtents(const uint8_t *texturePayload, size_t texturePayloadBytes, uint8_t *pointBuffer,
                                   size_t pointBufferBytes, uint32_t pointCount, RasterTextureUVExtentsVisit *visits,
                                   size_t visitCapacity, RasterTextureUVExtents *result) {
	uint16_t textureWidth;
	uint16_t textureHeight;
	uint32_t uScale;
	uint32_t vScale;
	uint32_t remaining;
	uint32_t pointOffset;
	size_t visitCount;

	if (result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->savedMultiplyLowWorkValue = true;
	result->savedUScaleWorkValue = true;
	result->savedPointCount = true;
	result->savedMultiplyHighWorkValue = true;
	result->savedVScaleWorkValue = true;
	result->savedPointCursor = true;
	if (texturePayload == NULL || texturePayloadBytes < SLIP_SPRITE_DIMENSIONS_END || pointBuffer == NULL ||
	    pointCount == 0u || (size_t)pointCount > SIZE_MAX / sizeof(RasterTexturedPoint) ||
	    pointBufferBytes < (size_t)pointCount * sizeof(RasterTexturedPoint) || visits == NULL ||
	    visitCapacity < pointCount) {
		return 0;
	}
	textureWidth = SlipBytes_ReadLE16(texturePayload);
	textureHeight = SlipBytes_ReadLE16(texturePayload + SLIP_SPRITE_HEIGHT_OFFSET);
	if (textureWidth == 0u || textureHeight == 0u) {
		return 0;
	}
	uScale = ((uint32_t)(uint16_t)(textureWidth - 1u) << RASTER_FRACTION_BITS) | UINT16_MAX;
	vScale = ((uint32_t)(uint16_t)(textureHeight - 1u) << RASTER_FRACTION_BITS) | UINT16_MAX;
	result->textureWidth = textureWidth;
	result->textureHeight = textureHeight;
	result->uScale = uScale;
	result->vScale = vScale;

	remaining = pointCount;
	pointOffset = 0;
	visitCount = 0;
	while (remaining != 0u) {
		uint8_t *const point = pointBuffer + pointOffset;
		const uint32_t rawU = SlipBytes_ReadLE32(point + offsetof(RasterTexturedPoint, u));
		const uint32_t rawV = SlipBytes_ReadLE32(point + offsetof(RasterTexturedPoint, v));
		const uint32_t scaledU = Raster_MultiplyUnsignedShift14(rawU, uScale);
		const uint32_t scaledV = Raster_MultiplyUnsignedShift14(rawV, vScale);

		Raster_WriteLE32(point + offsetof(RasterTexturedPoint, scaledU), scaledU);
		Raster_WriteLE32(point + offsetof(RasterTexturedPoint, scaledV), scaledV);
		--remaining;
		visits[visitCount] =
		    (RasterTextureUVExtentsVisit){pointOffset, rawU, scaledU, rawV, scaledV, remaining, remaining != 0u};
		pointOffset += sizeof(RasterTexturedPoint);
		++visitCount;
	}
	result->visitCount = visitCount;
	result->restoredPointCursor = true;
	result->restoredVScaleWorkValue = true;
	result->restoredMultiplyHighWorkValue = true;
	result->restoredPointCount = true;
	result->restoredUScaleWorkValue = true;
	result->restoredMultiplyLowWorkValue = true;
	result->ret = true;
	return 1;
}

int Raster_PrepareTexturedEntry(uint32_t textureHandle, const uint8_t *lockedPayload, size_t lockedPayloadBytes,
                                uint8_t *pointBuffer, uint32_t pointBufferBase, size_t pointBufferBytes,
                                uint32_t pointCount, RasterTexturedEntryScanVisit *visits, size_t visitCapacity,
                                RasterTexturedEntrySetup *result) {
	uint16_t textureHeaderWord;
	uint32_t remainingCount;
	uint32_t pointOffset;
	int32_t topY;
	int32_t bottomY;
	uint32_t topLeftPointOffset;
	uint32_t topRightPointOffset;
	size_t visitCount;

	if (result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->pushad = true;
	result->savedPointCursor = true;
	result->textureHandle = textureHandle;
	result->calledLockTextureResource = true;
	result->lockedPayload = (uintptr_t)lockedPayload;
	result->storedTexturePayload = (uintptr_t)lockedPayload;
	result->restoredPointCursor = true;
	if (lockedPayload == NULL || lockedPayloadBytes < SLIP_SPRITE_TRANSPARENT_COLOUR_END) {
		return 0;
	}
	textureHeaderWord = SlipBytes_ReadLE16(lockedPayload + SLIP_SPRITE_TRANSPARENT_COLOUR_OFFSET);
	result->textureHeaderWord = textureHeaderWord;
	result->branchTextureHeaderMinusOne = textureHeaderWord == SLIP_SPRITE_NO_TRANSPARENT_COLOUR;
	if (!result->branchTextureHeaderMinusOne) {
		result->popad = true;
		result->calledUnlockTextureResource = true;
		result->jumpMaskedPerspectivePolygon = true;
		return 1;
	}

	result->calledPrepareTextureUVExtents = true;
	result->calledBuildTextureRowTable = true;
	result->pointBufferBase = pointBufferBase;
	result->inputPointCount = pointCount;
	if (pointBuffer == NULL || pointCount == 0u || (size_t)pointCount > SIZE_MAX / sizeof(RasterTexturedPoint) ||
	    pointBufferBytes < (size_t)pointCount * sizeof(RasterTexturedPoint) ||
	    (pointCount > 1u && (visits == NULL || visitCapacity < (size_t)pointCount - 1u))) {
		return 0;
	}
	{
		RasterTextureUVExtentsVisit uvVisits[RASTER_TEXTURE_UV_VISIT_CAPACITY];
		RasterTextureUVExtents uvExtents;

		if (pointCount > sizeof(uvVisits) / sizeof(uvVisits[0]) ||
		    !Raster_PrepareTextureUVExtents(lockedPayload, lockedPayloadBytes, pointBuffer, pointBufferBytes,
		                                    pointCount, uvVisits, sizeof(uvVisits) / sizeof(uvVisits[0]), &uvExtents)) {
			return 0;
		}
	}

	remainingCount = pointCount - 1u;
	result->pointCountAfterDec = remainingCount;
	topY = SlipBytes_ReadLEI32(pointBuffer + offsetof(RasterPoint, y));
	bottomY = topY;
	pointOffset = sizeof(RasterTexturedPoint);
	topLeftPointOffset = 0;
	topRightPointOffset = 0;
	visitCount = 0;
	result->topY = topY;
	result->bottomY = bottomY;
	result->firstNextPointOffset = pointOffset;
	result->topLeftPointOffset = topLeftPointOffset;
	result->topRightPointOffset = topRightPointOffset;

	while (remainingCount != 0u) {
		const uint8_t *const point = pointBuffer + pointOffset;
		const int32_t pointX = SlipBytes_ReadLEI32(point);
		const int32_t pointY = SlipBytes_ReadLEI32(point + offsetof(RasterPoint, y));
		RasterTexturedEntryScanVisit visit;

		memset(&visit, 0, sizeof(visit));
		visit.pointOffset = pointOffset;
		visit.pointX = pointX;
		visit.pointY = pointY;
		visit.bottomYBefore = bottomY;
		visit.topYBefore = topY;
		visit.topLeftPointOffsetBefore = topLeftPointOffset;
		visit.topRightPointOffsetBefore = topRightPointOffset;
		if (bottomY < pointY) {
			bottomY = pointY;
			visit.updateBottomY = true;
		}
		if (topY >= pointY) {
			if (topY == pointY) {
				const int32_t leftX = SlipBytes_ReadLEI32(pointBuffer + topLeftPointOffset);
				const int32_t rightX = SlipBytes_ReadLEI32(pointBuffer + topRightPointOffset);

				if (pointX < leftX) {
					topLeftPointOffset = pointOffset;
					visit.tieUpdateLeft = true;
				}
				if (pointX > rightX) {
					topRightPointOffset = pointOffset;
					visit.tieUpdateRight = true;
				}
			} else {
				topY = pointY;
				topLeftPointOffset = pointOffset;
				topRightPointOffset = pointOffset;
				visit.updateTopPair = true;
			}
		}
		pointOffset += sizeof(RasterTexturedPoint);
		--remainingCount;
		visit.pointOffsetAfterAdd = pointOffset;
		visit.remainingCountAfterDec = remainingCount;
		visit.loop = remainingCount != 0u;
		visits[visitCount] = visit;
		++visitCount;
	}

	result->topY = topY;
	result->bottomY = bottomY;
	result->topLeftPointOffset = topLeftPointOffset;
	result->topRightPointOffset = topRightPointOffset;
	result->scanVisitCount = visitCount;
	result->endPointOffset = pointOffset;
	result->horizontalReturn = bottomY == topY;
	if (result->horizontalReturn) {
		return 1;
	}
	result->storedBottomY = (uint32_t)bottomY;
	result->calledStepLeftEdgeTexturedPerspective = true;
	result->calledStepRightEdgeTexturedPerspective = true;
	result->calledSetupPerspectiveTexturedSpan = true;
	result->jumpOpaquePerspectiveScanlineLoop = true;
	return 1;
}

int Raster_PrepareAffineTexturedEntry(uint32_t textureHandle, const uint8_t *lockedPayload, size_t lockedPayloadBytes,
                                      uint8_t *pointBuffer, uint32_t pointBufferBase, size_t pointBufferBytes,
                                      uint32_t pointCount, RasterAffineTexturedEntryScanVisit *visits,
                                      size_t visitCapacity, RasterAffineTexturedEntrySetup *result) {
	uint32_t remainingCount;
	uint32_t pointOffset;
	int32_t topY;
	int32_t bottomY;
	uint32_t topLeftPointOffset;
	uint32_t topRightPointOffset;
	size_t visitCount;

	if (result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->pushad = true;
	result->savedPointCursor = true;
	result->textureHandle = textureHandle;
	result->calledLockTextureResource = true;
	result->lockedPayload = (uintptr_t)lockedPayload;
	result->storedTexturePayload = (uintptr_t)lockedPayload;
	result->restoredPointCursor = true;
	result->calledPrepareTextureUVExtents = true;
	result->calledBuildTextureRowTable = true;
	result->pointBufferBase = pointBufferBase;
	result->inputPointCount = pointCount;
	if (pointBuffer == NULL || pointCount == 0u || (size_t)pointCount > SIZE_MAX / sizeof(RasterTexturedPoint) ||
	    pointBufferBytes < (size_t)pointCount * sizeof(RasterTexturedPoint) ||
	    (pointCount > 1u && (visits == NULL || visitCapacity < (size_t)pointCount - 1u))) {
		return 0;
	}
	{
		RasterTextureUVExtentsVisit uvVisits[RASTER_TEXTURE_UV_VISIT_CAPACITY];
		RasterTextureUVExtents uvExtents;

		if (pointCount > sizeof(uvVisits) / sizeof(uvVisits[0]) ||
		    !Raster_PrepareTextureUVExtents(lockedPayload, lockedPayloadBytes, pointBuffer, pointBufferBytes,
		                                    pointCount, uvVisits, sizeof(uvVisits) / sizeof(uvVisits[0]), &uvExtents)) {
			return 0;
		}
	}

	remainingCount = pointCount - 1u;
	result->pointCountAfterDec = remainingCount;
	topY = SlipBytes_ReadLEI32(pointBuffer + offsetof(RasterPoint, y));
	bottomY = topY;
	pointOffset = sizeof(RasterTexturedPoint);
	topLeftPointOffset = 0;
	topRightPointOffset = 0;
	visitCount = 0;
	result->topY = topY;
	result->bottomY = bottomY;
	result->firstNextPointOffset = pointOffset;
	result->topLeftPointOffset = topLeftPointOffset;
	result->topRightPointOffset = topRightPointOffset;

	while (remainingCount != 0u) {
		const uint8_t *const point = pointBuffer + pointOffset;
		const int32_t pointX = SlipBytes_ReadLEI32(point);
		const int32_t pointY = SlipBytes_ReadLEI32(point + offsetof(RasterPoint, y));
		RasterAffineTexturedEntryScanVisit visit;

		memset(&visit, 0, sizeof(visit));
		visit.pointOffset = pointOffset;
		visit.pointX = pointX;
		visit.pointY = pointY;
		visit.bottomYBefore = bottomY;
		visit.topYBefore = topY;
		visit.topLeftPointOffsetBefore = topLeftPointOffset;
		visit.topRightPointOffsetBefore = topRightPointOffset;
		if (bottomY < pointY) {
			bottomY = pointY;
			visit.updateBottomY = true;
		}
		if (topY >= pointY) {
			if (topY == pointY) {
				const int32_t leftX = SlipBytes_ReadLEI32(pointBuffer + topLeftPointOffset);
				const int32_t rightX = SlipBytes_ReadLEI32(pointBuffer + topRightPointOffset);

				if (pointX < leftX) {
					topLeftPointOffset = pointOffset;
					visit.tieUpdateLeft = true;
				}
				if (pointX > rightX) {
					topRightPointOffset = pointOffset;
					visit.tieUpdateRight = true;
				}
			} else {
				topY = pointY;
				topLeftPointOffset = pointOffset;
				topRightPointOffset = pointOffset;
				visit.updateTopPair = true;
			}
		}
		pointOffset += sizeof(RasterTexturedPoint);
		--remainingCount;
		visit.pointOffsetAfterAdd = pointOffset;
		visit.remainingCountAfterDec = remainingCount;
		visit.loop = remainingCount != 0u;
		visits[visitCount] = visit;
		++visitCount;
	}

	result->topY = topY;
	result->bottomY = bottomY;
	result->topLeftPointOffset = topLeftPointOffset;
	result->topRightPointOffset = topRightPointOffset;
	result->scanVisitCount = visitCount;
	result->endPointOffset = pointOffset;
	result->horizontalBranch = bottomY == topY;
	if (result->horizontalBranch) {
		return 1;
	}
	result->storedBottomY = (uint32_t)bottomY;
	result->calledStepLeftEdgeAffine = true;
	result->calledStepRightEdgeAffine = true;
	result->spanLoopEntry = true;
	return 1;
}

int Raster_StepLeftEdgeAffine(const uint8_t *pointBuffer, uint32_t pointBufferBase, uint32_t pointBufferEnd,
                              size_t pointBufferBytes, uint32_t initialPointOffset, int32_t currentY, int32_t bottomY,
                              RasterAffineLeftEdgeStep *result) {
	uint32_t currentOffset;

	if (result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	if (pointBuffer == NULL || pointBufferEnd < pointBufferBase ||
	    ((pointBufferEnd - pointBufferBase) % sizeof(RasterTexturedPoint)) != 0u) {
		return 0;
	}
	currentOffset = initialPointOffset;
	for (;;) {
		const uint8_t *current;
		uint32_t candidateOffset;
		const uint8_t *candidate;
		uint32_t edgeHeight;
		int32_t currentX;
		int32_t candidateX;
		int32_t candidateY;

		if (!Raster_TexturedPointOffsetValid(pointBufferBase, pointBufferBytes, currentOffset)) {
			return 0;
		}
		current = pointBuffer + (currentOffset - pointBufferBase);
		result->pointOffsetIn = currentOffset;
		currentX = SlipBytes_ReadLEI32(current);
		result->currentX = currentX;
		result->spanLeftTexU = SlipBytes_ReadLE32(current + offsetof(RasterTexturedPoint, scaledU));
		result->spanLeftTexV = SlipBytes_ReadLE32(current + offsetof(RasterTexturedPoint, scaledV));
		result->currentY = currentY;
		result->bottomY = bottomY;
		result->atBottom = currentY == bottomY;
		if (result->atBottom) {
			result->pointOffsetOut = currentOffset;
			result->carryOut = false;
			return 1;
		}

		candidateOffset = currentOffset;
		if (candidateOffset == pointBufferBase) {
			candidateOffset = pointBufferEnd;
			result->wrappedEndOffset = candidateOffset;
		}
		if (candidateOffset < pointBufferBase + (uint32_t)sizeof(RasterTexturedPoint)) {
			return 0;
		}
		candidateOffset -= sizeof(RasterTexturedPoint);
		if (!Raster_TexturedPointOffsetValid(pointBufferBase, pointBufferBytes, candidateOffset)) {
			return 0;
		}
		candidate = pointBuffer + (candidateOffset - pointBufferBase);
		candidateY = SlipBytes_ReadLEI32(candidate + offsetof(RasterPoint, y));
		result->candidateOffset = candidateOffset;
		result->candidateY = candidateY;
		result->carryCandidateAbove = currentY > candidateY;
		if (result->carryCandidateAbove) {
			result->pointOffsetOut = currentOffset;
			result->carryOut = true;
			return 1;
		}
		candidateX = SlipBytes_ReadLEI32(candidate);
		result->candidateX = candidateX;
		if (currentY == candidateY) {
			result->horizontalRestart = true;
			currentOffset = candidateOffset;
			continue;
		}

		edgeHeight = (uint32_t)(candidateY - currentY);
		if (edgeHeight == 0u) {
			return 0;
		}
		result->edgeHeight = edgeHeight;
		result->remaining = (uint16_t)edgeHeight;
		result->texUStep =
		    (int32_t)(SlipBytes_ReadLE32(candidate + offsetof(RasterTexturedPoint, scaledU)) - result->spanLeftTexU) /
		    (int32_t)edgeHeight;
		result->texVStep =
		    (int32_t)(SlipBytes_ReadLE32(candidate + offsetof(RasterTexturedPoint, scaledV)) - result->spanLeftTexV) /
		    (int32_t)edgeHeight;
		result->xStep = Raster_DivideWrappedFixed16(candidateX - currentX, edgeHeight);
		result->xFraction = RASTER_EDGE_HALF_PIXEL_Q16;
		result->pointOffsetOut = candidateOffset;
		result->carryOut = false;
		return 1;
	}
}

int Raster_StepRightEdgeAffine(const uint8_t *pointBuffer, uint32_t pointBufferBase, uint32_t pointBufferEnd,
                               size_t pointBufferBytes, uint32_t initialPointOffset, int32_t currentY, int32_t bottomY,
                               RasterAffineRightEdgeStep *result) {
	uint32_t currentOffset;

	if (result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	if (pointBuffer == NULL || pointBufferEnd < pointBufferBase ||
	    ((pointBufferEnd - pointBufferBase) % sizeof(RasterTexturedPoint)) != 0u) {
		return 0;
	}
	currentOffset = initialPointOffset;
	for (;;) {
		const uint8_t *current;
		uint32_t candidateOffset;
		const uint8_t *candidate;
		uint32_t edgeHeight;
		int32_t currentX;
		int32_t candidateX;
		int32_t candidateY;

		if (!Raster_TexturedPointOffsetValid(pointBufferBase, pointBufferBytes, currentOffset)) {
			return 0;
		}
		current = pointBuffer + (currentOffset - pointBufferBase);
		result->pointOffsetIn = currentOffset;
		currentX = SlipBytes_ReadLEI32(current);
		result->currentX = currentX;
		result->spanRightTexU = SlipBytes_ReadLE32(current + offsetof(RasterTexturedPoint, scaledU));
		result->spanRightTexV = SlipBytes_ReadLE32(current + offsetof(RasterTexturedPoint, scaledV));
		result->currentY = currentY;
		result->bottomY = bottomY;
		result->atBottom = currentY == bottomY;
		if (result->atBottom) {
			result->pointOffsetOut = currentOffset;
			result->carryOut = false;
			return 1;
		}

		candidateOffset = currentOffset + (uint32_t)sizeof(RasterTexturedPoint);
		if (candidateOffset == pointBufferEnd) {
			candidateOffset = pointBufferBase;
			result->wrappedBaseOffset = candidateOffset;
		}
		if (!Raster_TexturedPointOffsetValid(pointBufferBase, pointBufferBytes, candidateOffset)) {
			return 0;
		}
		candidate = pointBuffer + (candidateOffset - pointBufferBase);
		candidateY = SlipBytes_ReadLEI32(candidate + offsetof(RasterPoint, y));
		result->candidateOffset = candidateOffset;
		result->candidateY = candidateY;
		result->carryCandidateAbove = currentY > candidateY;
		if (result->carryCandidateAbove) {
			result->pointOffsetOut = currentOffset;
			result->carryOut = true;
			return 1;
		}
		candidateX = SlipBytes_ReadLEI32(candidate);
		result->candidateX = candidateX;
		if (currentY == candidateY) {
			result->horizontalRestart = true;
			currentOffset = candidateOffset;
			continue;
		}

		edgeHeight = (uint32_t)(candidateY - currentY);
		if (edgeHeight == 0u) {
			return 0;
		}
		result->edgeHeight = edgeHeight;
		result->remaining = (uint16_t)edgeHeight;
		result->texUStep =
		    (int32_t)(SlipBytes_ReadLE32(candidate + offsetof(RasterTexturedPoint, scaledU)) - result->spanRightTexU) /
		    (int32_t)edgeHeight;
		result->texVStep =
		    (int32_t)(SlipBytes_ReadLE32(candidate + offsetof(RasterTexturedPoint, scaledV)) - result->spanRightTexV) /
		    (int32_t)edgeHeight;
		result->xStep = Raster_DivideWrappedFixed16(candidateX - currentX, edgeHeight);
		result->xFraction = RASTER_EDGE_HALF_PIXEL_Q16;
		result->pointOffsetOut = candidateOffset;
		result->carryOut = false;
		return 1;
	}
}

int Raster_StepLeftEdgeTexturedPerspective(const uint8_t *pointBuffer, uint32_t pointBufferBase,
                                           uint32_t pointBufferEnd, size_t pointBufferBytes,
                                           uint32_t initialPointOffset, int32_t currentY, int32_t bottomY,
                                           RasterTexturedLeftEdgeStep *result) {
	uint32_t currentOffset;

	if (result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	if (pointBuffer == NULL || pointBufferEnd < pointBufferBase ||
	    ((pointBufferEnd - pointBufferBase) % sizeof(RasterTexturedPoint)) != 0u) {
		return 0;
	}
	currentOffset = initialPointOffset;
	for (;;) {
		const uint8_t *current;
		uint32_t candidateOffset;
		const uint8_t *candidate;
		uint32_t edgeHeight;
		int32_t currentX;
		int32_t candidateX;
		int32_t candidateY;

		if (!Raster_TexturedPointOffsetValid(pointBufferBase, pointBufferBytes, currentOffset)) {
			return 0;
		}
		current = pointBuffer + (currentOffset - pointBufferBase);
		result->pointOffsetIn = currentOffset;
		currentX = SlipBytes_ReadLEI32(current);
		result->currentX = currentX;
		result->currentDepth = SlipBytes_ReadLE32(current + offsetof(RasterTexturedPoint, depth));
		result->currentScaledU = SlipBytes_ReadLE32(current + offsetof(RasterTexturedPoint, scaledU));
		result->currentScaledV = SlipBytes_ReadLE32(current + offsetof(RasterTexturedPoint, scaledV));
		result->leftBaseDepth = result->currentDepth;
		result->leftBaseTexU = result->currentScaledU;
		result->leftBaseTexV = result->currentScaledV;
		result->currentY = currentY;
		result->bottomY = bottomY;
		result->atBottom = currentY == bottomY;
		if (result->atBottom) {
			result->pointOffsetOut = currentOffset;
			result->carryOut = false;
			return 1;
		}

		candidateOffset = currentOffset;
		if (candidateOffset == pointBufferBase) {
			candidateOffset = pointBufferEnd;
			result->wrappedEndOffset = candidateOffset;
		}
		if (candidateOffset < pointBufferBase + (uint32_t)sizeof(RasterTexturedPoint)) {
			return 0;
		}
		candidateOffset -= sizeof(RasterTexturedPoint);
		if (!Raster_TexturedPointOffsetValid(pointBufferBase, pointBufferBytes, candidateOffset)) {
			return 0;
		}
		candidate = pointBuffer + (candidateOffset - pointBufferBase);
		candidateY = SlipBytes_ReadLEI32(candidate + offsetof(RasterPoint, y));
		result->candidateOffset = candidateOffset;
		result->candidateY = candidateY;
		result->carryCandidateAbove = currentY > candidateY;
		if (result->carryCandidateAbove) {
			result->pointOffsetOut = currentOffset;
			result->carryOut = true;
			return 1;
		}
		candidateX = SlipBytes_ReadLEI32(candidate);
		result->candidateX = candidateX;
		if (currentY == candidateY) {
			result->horizontalRestart = true;
			currentOffset = candidateOffset;
			continue;
		}

		edgeHeight = (uint32_t)(candidateY - currentY);
		if (edgeHeight == 0u) {
			return 0;
		}
		result->edgeHeight = edgeHeight;
		result->remaining = (uint16_t)edgeHeight;
		result->accumulatedBaseDepthPerRow = 0;
		result->accumulatedNextPointDepthPerRow = 0;
		result->baseDepthPerRow = result->leftBaseDepth / edgeHeight;
		result->nextPointDepthPerRow =
		    SlipBytes_ReadLE32(candidate + offsetof(RasterTexturedPoint, depth)) / edgeHeight;
		result->xStep = Raster_DivideWrappedFixed16(candidateX - currentX, edgeHeight);
		result->xFraction = RASTER_EDGE_HALF_PIXEL_Q16;
		result->pointOffsetOut = candidateOffset;
		result->carryOut = false;
		return 1;
	}
}

int Raster_StepRightEdgeTexturedPerspective(const uint8_t *pointBuffer, uint32_t pointBufferBase,
                                            uint32_t pointBufferEnd, size_t pointBufferBytes,
                                            uint32_t initialPointOffset, int32_t currentY, int32_t bottomY,
                                            RasterTexturedRightEdgeStep *result) {
	uint32_t currentOffset;

	if (result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	if (pointBuffer == NULL || pointBufferEnd < pointBufferBase ||
	    ((pointBufferEnd - pointBufferBase) % sizeof(RasterTexturedPoint)) != 0u) {
		return 0;
	}
	currentOffset = initialPointOffset;
	for (;;) {
		const uint8_t *current;
		uint32_t candidateOffset;
		const uint8_t *candidate;
		uint32_t edgeHeight;
		int32_t currentX;
		int32_t candidateX;
		int32_t candidateY;

		if (!Raster_TexturedPointOffsetValid(pointBufferBase, pointBufferBytes, currentOffset)) {
			return 0;
		}
		current = pointBuffer + (currentOffset - pointBufferBase);
		result->pointOffsetIn = currentOffset;
		currentX = SlipBytes_ReadLEI32(current);
		result->currentX = currentX;
		result->currentDepth = SlipBytes_ReadLE32(current + offsetof(RasterTexturedPoint, depth));
		result->currentScaledU = SlipBytes_ReadLE32(current + offsetof(RasterTexturedPoint, scaledU));
		result->currentScaledV = SlipBytes_ReadLE32(current + offsetof(RasterTexturedPoint, scaledV));
		result->rightBaseDepth = result->currentDepth;
		result->rightBaseTexU = result->currentScaledU;
		result->rightBaseTexV = result->currentScaledV;
		result->currentY = currentY;
		result->bottomY = bottomY;
		result->atBottom = currentY == bottomY;
		if (result->atBottom) {
			result->pointOffsetOut = currentOffset;
			result->carryOut = false;
			return 1;
		}

		candidateOffset = currentOffset + (uint32_t)sizeof(RasterTexturedPoint);
		if (candidateOffset == pointBufferEnd) {
			candidateOffset = pointBufferBase;
			result->wrappedBaseOffset = candidateOffset;
		}
		if (!Raster_TexturedPointOffsetValid(pointBufferBase, pointBufferBytes, candidateOffset)) {
			return 0;
		}
		candidate = pointBuffer + (candidateOffset - pointBufferBase);
		candidateY = SlipBytes_ReadLEI32(candidate + offsetof(RasterPoint, y));
		result->candidateOffset = candidateOffset;
		result->candidateY = candidateY;
		result->carryCandidateAbove = currentY > candidateY;
		if (result->carryCandidateAbove) {
			result->pointOffsetOut = currentOffset;
			result->carryOut = true;
			return 1;
		}
		candidateX = SlipBytes_ReadLEI32(candidate);
		result->candidateX = candidateX;
		if (currentY == candidateY) {
			result->horizontalRestart = true;
			currentOffset = candidateOffset;
			continue;
		}

		edgeHeight = (uint32_t)(candidateY - currentY);
		if (edgeHeight == 0u) {
			return 0;
		}
		result->edgeHeight = edgeHeight;
		result->remaining = (uint16_t)edgeHeight;
		result->accumulatedBaseDepthPerRow = 0;
		result->accumulatedNextPointDepthPerRow = 0;
		result->baseDepthPerRow = result->rightBaseDepth / edgeHeight;
		result->nextPointDepthPerRow =
		    SlipBytes_ReadLE32(candidate + offsetof(RasterTexturedPoint, depth)) / edgeHeight;
		result->xStep = Raster_DivideWrappedFixed16(candidateX - currentX, edgeHeight);
		result->xFraction = RASTER_EDGE_HALF_PIXEL_Q16;
		result->pointOffsetOut = candidateOffset;
		result->carryOut = false;
		return 1;
	}
}

int Raster_AdvancePerspectiveTexturedEdges(const RasterTexturedAdvanceEdgesState *state, int leftEdgeCarry,
                                           int rightEdgeCarry, RasterTexturedAdvanceEdges *result) {
	bool leftCarry;
	bool rightCarry;
	uint16_t leftStepLo;
	uint16_t leftStepHi;
	uint16_t rightStepLo;
	uint16_t rightStepHi;

	if (state == NULL || result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->in = *state;
	result->resetOneRowFlag = true;
	result->leftRemainingOne = state->leftRemaining == 1u;
	result->rightRemainingOne = state->rightRemaining == 1u;
	if (result->leftRemainingOne || result->rightRemainingOne) {
		result->oneRowFlag = UINT32_MAX;
	}

	result->out = *state;
	++result->out.scanY;
	result->out.leftAccumulatedBaseDepthPerRow += state->leftBaseDepthPerRow;
	result->out.leftAccumulatedNextPointDepthPerRow += state->leftNextPointDepthPerRow;
	result->out.rightAccumulatedBaseDepthPerRow += state->rightBaseDepthPerRow;
	result->out.rightAccumulatedNextPointDepthPerRow += state->rightNextPointDepthPerRow;

	leftStepLo = (uint16_t)state->leftXStep;
	leftStepHi = (uint16_t)((uint32_t)state->leftXStep >> RASTER_FRACTION_BITS);
	result->out.leftXFraction = Raster_AddU16WithCarry(state->leftXFraction, leftStepLo, &leftCarry);
	result->leftFractionCarry = leftCarry;
	result->out.leftX = (int16_t)((uint16_t)state->leftX + leftStepHi + (leftCarry ? 1u : 0u));

	rightStepLo = (uint16_t)state->rightXStep;
	rightStepHi = (uint16_t)((uint32_t)state->rightXStep >> RASTER_FRACTION_BITS);
	result->out.rightXFraction = Raster_AddU16WithCarry(state->rightXFraction, rightStepLo, &rightCarry);
	result->rightFractionCarry = rightCarry;
	result->out.rightX = (int16_t)((uint16_t)state->rightX + rightStepHi + (rightCarry ? 1u : 0u));

	--result->out.leftRemaining;
	result->calledStepLeftEdgeTexturedPerspective = result->out.leftRemaining == 0u;
	result->carryFromLeftPerspectiveEdge = result->calledStepLeftEdgeTexturedPerspective && leftEdgeCarry != 0;
	result->jumpAfterLeftCarry = result->carryFromLeftPerspectiveEdge;
	if (result->jumpAfterLeftCarry) {
		return 1;
	}
	--result->out.rightRemaining;
	result->calledStepRightEdgeTexturedPerspective = result->out.rightRemaining == 0u;
	result->carryFromRightPerspectiveEdge = result->calledStepRightEdgeTexturedPerspective && rightEdgeCarry != 0;
	result->jumpAfterRightCarry = result->carryFromRightPerspectiveEdge;
	result->ret = !result->jumpAfterRightCarry;
	return 1;
}

int Raster_SetupPerspectiveTexturedSpan(const RasterTexturedSpanSetupState *state, RasterTexturedSpanSetup *result) {
	uint32_t leftDenominator;
	int32_t leftQuotient;
	uint32_t rightSubResult;
	uint32_t rightDenominator;
	int32_t rightQuotient;

	if (state == NULL || result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->in = *state;

	leftDenominator =
	    state->leftAccumulatedBaseDepthPerRow - state->leftAccumulatedNextPointDepthPerRow + state->leftPointDepth;
	if (leftDenominator == 0u) {
		return 0;
	}
	result->leftDenominator = leftDenominator;
	leftQuotient =
	    Raster_DivideSignedScaled14((int32_t)state->leftAccumulatedBaseDepthPerRow, (int32_t)leftDenominator);
	result->leftQuotient = leftQuotient;
	result->spanLeftTexU = Raster_MultiplyShift14RoundAdd((int32_t)(state->leftPointTexU - state->leftBaseTexU),
	                                                      leftQuotient, state->leftBaseTexU, &result->leftTexUCarry);
	result->spanLeftTexV = Raster_MultiplyShift14RoundAdd((int32_t)(state->leftPointTexV - state->leftBaseTexV),
	                                                      leftQuotient, state->leftBaseTexV, &result->leftTexVCarry);
	result->spanLeftDepth = Raster_MultiplyShift14RoundAdd((int32_t)(state->leftPointDepth - state->leftBaseDepth),
	                                                       leftQuotient, state->leftBaseDepth, &result->leftDepthCarry);
	rasterSpanLeftDepth = result->spanLeftDepth;

	rightSubResult = state->rightAccumulatedBaseDepthPerRow - state->rightAccumulatedNextPointDepthPerRow;
	result->rightDenominatorBorrow =
	    state->rightAccumulatedBaseDepthPerRow < state->rightAccumulatedNextPointDepthPerRow;
	rightDenominator = rightSubResult + state->rightPointDepth + (result->rightDenominatorBorrow ? 1u : 0u);
	if (rightDenominator == 0u) {
		return 0;
	}
	result->rightDenominator = rightDenominator;
	rightQuotient =
	    Raster_DivideSignedScaled14((int32_t)state->rightAccumulatedBaseDepthPerRow, (int32_t)rightDenominator);
	result->rightQuotient = rightQuotient;
	result->spanRightTexU =
	    Raster_MultiplyShift14RoundAdd((int32_t)(state->rightPointTexU - state->rightBaseTexU), rightQuotient,
	                                   state->rightBaseTexU, &result->rightTexUCarry);
	result->spanRightTexV =
	    Raster_MultiplyShift14RoundAdd((int32_t)(state->rightPointTexV - state->rightBaseTexV), rightQuotient,
	                                   state->rightBaseTexV, &result->rightTexVCarry);
	result->spanRightDepth =
	    Raster_MultiplyShift14RoundAdd((int32_t)(state->rightPointDepth - state->rightBaseDepth), rightQuotient,
	                                   state->rightBaseDepth, &result->rightDepthCarry);
	rasterSpanRightDepth = result->spanRightDepth;

	result->calledDispatchTexturedSpan = true;
	result->spanCoreTexturePayload = state->texturePayload;
	result->spanCoreEndpointBase = RASTER_TEXTURE_SPAN_ENDPOINT_BASE_TOKEN;
	result->spanCoreLeftX = state->spanLeftX;
	result->spanCoreRightX = state->spanRightX;

	result->postCallLeftX = state->spanLeftX;
	result->postCallRightX = state->spanRightX;
	result->spanStartCurrentRow = state->currentRowPointer + (uintptr_t)(uint32_t)state->spanLeftX;
	result->spanPixelCount = (uint32_t)state->spanRightX - (uint32_t)state->spanLeftX + 1u;
	result->spanStartNextRow = state->nextRowPointer + (uintptr_t)(uint32_t)state->spanLeftX;
	return 1;
}

int Raster_DrawTexturedSpanCore(const RasterTexturedSpanCoreState *state, RasterTexturedSpanCore *result) {
	uint32_t pixelCount;
	uint32_t remainingForAlignment;
	uint32_t currentTexU;
	uint32_t currentTexV;
	int32_t texUStep = 0;
	int32_t texVStep = 0;
	size_t i;
	bool masked;

	if (state == NULL || result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->in = *state;
	if (state->screenRow == NULL || state->spanLeftX < 0 || state->spanRightX < 0 ||
	    (size_t)state->spanLeftX >= state->screenRowBytes || (size_t)state->spanRightX >= state->screenRowBytes) {
		return 0;
	}

	result->destination = (uintptr_t)(state->screenRow + state->spanLeftX);
	result->spanDelta = state->spanLeftX - state->spanRightX;
	result->zeroDelta = result->spanDelta == 0;
	if (!result->zeroDelta) {
		result->positiveDeltaReturn = result->spanDelta > 0;
		if (result->positiveDeltaReturn) {
			return 1;
		}
		result->spanSteps = (uint32_t)-result->spanDelta;
		texUStep = (int32_t)((int32_t)(state->endTexU - state->startTexU) / (int32_t)result->spanSteps);
		texVStep = (int32_t)((int32_t)(state->endTexV - state->startTexV) / (int32_t)result->spanSteps);
		result->texUStep = texUStep;
		result->texVStep = texVStep;
	}

	pixelCount = result->spanSteps + 1u;
	result->pixelCount = pixelCount;
	currentTexU = state->startTexU;
	currentTexV = state->startTexV;
	result->currentTexU = currentTexU;
	result->currentTexV = currentTexV;
	masked = state->transparentWord != SLIP_SPRITE_NO_TRANSPARENT_COLOUR;
	result->maskedBranch = masked;

	remainingForAlignment = pixelCount;
	if (!masked) {
		uintptr_t destination = result->destination;

		result->oddBytePrologue = (destination & 1u) != 0u;
		if (result->oddBytePrologue && remainingForAlignment != 0u) {
			++destination;
			--remainingForAlignment;
		}
		result->wordAlignPrologue = (destination & 2u) != 0u && remainingForAlignment > 1u;
		if (result->wordAlignPrologue) {
			destination += 2u;
			remainingForAlignment -= 2u;
		}
		result->dwordLoopCount = remainingForAlignment / RASTER_COPY_DWORD_BYTES;
		result->wordTail = (remainingForAlignment & 2u) != 0u;
		result->byteTail = (remainingForAlignment & 1u) != 0u;
	}

	for (i = 0; i < (size_t)pixelCount; ++i) {
		uint8_t sample;

		if (!Raster_SampleTextureByte(state->textureRows, state->textureRowCount, state->textureRowBytes, currentTexU,
		                              currentTexV, &sample)) {
			return 0;
		}
		++result->pixelsTested;
		if (!masked || sample != (uint8_t)state->transparentWord) {
			state->screenRow[(size_t)state->spanLeftX + i] = sample;
			++result->pixelsWritten;
		}
		currentTexU += (uint32_t)texUStep;
		currentTexV += (uint32_t)texVStep;
	}
	result->currentTexU = currentTexU;
	result->currentTexV = currentTexV;
	return 1;
}

int Raster_DrawAffineHorizontalSpan(const RasterAffineHorizontalSpanState *state, RasterAffineHorizontalSpan *result) {
	const uint8_t *topLeftPoint;
	const uint8_t *topRightPoint;
	RasterTexturedSpanCoreState spanCoreState;

	if (state == NULL || result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->in = *state;
	if (state->pointBuffer == NULL || state->lockedPayload == NULL ||
	    state->lockedPayloadBytes < SLIP_SPRITE_TRANSPARENT_COLOUR_END ||
	    state->topLeftPointOffset > state->pointBufferBytes || state->topRightPointOffset > state->pointBufferBytes ||
	    state->pointBufferBytes - state->topLeftPointOffset < sizeof(RasterTexturedPoint) ||
	    state->pointBufferBytes - state->topRightPointOffset < sizeof(RasterTexturedPoint)) {
		return 0;
	}

	topLeftPoint = state->pointBuffer + state->topLeftPointOffset;
	topRightPoint = state->pointBuffer + state->topRightPointOffset;
	result->spanLeftTexU = SlipBytes_ReadLE32(topLeftPoint + offsetof(RasterTexturedPoint, scaledU));
	result->spanLeftTexV = SlipBytes_ReadLE32(topLeftPoint + offsetof(RasterTexturedPoint, scaledV));
	result->spanRightTexU = SlipBytes_ReadLE32(topRightPoint + offsetof(RasterTexturedPoint, scaledU));
	result->spanRightTexV = SlipBytes_ReadLE32(topRightPoint + offsetof(RasterTexturedPoint, scaledV));
	result->endpointBase = RASTER_TEXTURE_SPAN_ENDPOINT_BASE_TOKEN;
	result->spanCoreLeftX = SlipBytes_ReadLEI32(topLeftPoint);
	result->spanCoreRightX = SlipBytes_ReadLEI32(topRightPoint);
	result->spanCoreTexturePayload = (uintptr_t)state->lockedPayload;
	result->transparentWord = SlipBytes_ReadLE16(state->lockedPayload + SLIP_SPRITE_TRANSPARENT_COLOUR_OFFSET);

	memset(&spanCoreState, 0, sizeof(spanCoreState));
	spanCoreState.screenRow = state->screenRow;
	spanCoreState.screenRowBytes = state->screenRowBytes;
	spanCoreState.spanLeftX = result->spanCoreLeftX;
	spanCoreState.spanRightX = result->spanCoreRightX;
	spanCoreState.startTexU = result->spanLeftTexU;
	spanCoreState.startTexV = result->spanLeftTexV;
	spanCoreState.endTexU = result->spanRightTexU;
	spanCoreState.endTexV = result->spanRightTexV;
	spanCoreState.transparentWord = result->transparentWord;
	spanCoreState.textureRows = state->textureRows;
	spanCoreState.textureRowCount = state->textureRowCount;
	spanCoreState.textureRowBytes = state->textureRowBytes;
	result->calledDrawTexturedSpanCore = true;
	if (!Raster_DrawTexturedSpanCore(&spanCoreState, &result->spanCore)) {
		return 0;
	}
	result->jump = true;
	return 1;
}

int Raster_DrawAffineScanlineLoop(const RasterAffineScanlineLoopState *state, RasterAffineScanlineLoopVisit *visits,
                                  size_t visitCapacity, RasterAffineScanlineLoop *result) {
	RasterAffineScanlineLoopState current;
	uint16_t leftStepLo;
	uint16_t leftStepHi;
	uint16_t rightStepLo;
	uint16_t rightStepHi;

	if (state == NULL || result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->in = *state;
	result->out = *state;
	if (state->screenRows == NULL || state->lockedPayload == NULL ||
	    state->lockedPayloadBytes < SLIP_SPRITE_TRANSPARENT_COLOUR_END || state->pointBuffer == NULL ||
	    state->pointBufferEnd < state->pointBufferBase ||
	    ((state->pointBufferEnd - state->pointBufferBase) % sizeof(RasterTexturedPoint)) != 0u ||
	    (state->scanline < state->bottomY && (visits == NULL || visitCapacity == 0u))) {
		return 0;
	}

	current = *state;
	leftStepLo = (uint16_t)current.leftXStep;
	leftStepHi = (uint16_t)((uint32_t)current.leftXStep >> RASTER_FRACTION_BITS);
	rightStepLo = (uint16_t)current.rightXStep;
	rightStepHi = (uint16_t)((uint32_t)current.rightXStep >> RASTER_FRACTION_BITS);
	while (current.scanline < current.bottomY) {
		RasterAffineScanlineLoopVisit *visit;
		RasterTexturedSpanCoreState spanCoreState;
		bool leftCarry;
		bool rightCarry;

		if (result->visitCount >= visitCapacity || current.scanline < 0 ||
		    (size_t)current.scanline >= current.screenRowCount || current.screenRows[current.scanline] == NULL) {
			return 0;
		}
		visit = visits + result->visitCount;
		memset(visit, 0, sizeof(*visit));
		visit->scanline = current.scanline;
		visit->spanCoreLeftX = current.leftX;
		visit->spanCoreRightX = current.rightX;
		visit->spanLeftTexUBefore = current.spanLeftTexU;
		visit->spanLeftTexVBefore = current.spanLeftTexV;
		visit->spanRightTexUBefore = current.spanRightTexU;
		visit->spanRightTexVBefore = current.spanRightTexV;
		visit->savedSpanLeftX = true;
		visit->savedSpanRightX = true;
		visit->savedLeftPointCursor = true;
		visit->savedRightPointCursor = true;
		visit->savedScanline = true;

		memset(&spanCoreState, 0, sizeof(spanCoreState));
		spanCoreState.screenRow = current.screenRows[current.scanline];
		spanCoreState.screenRowBytes = current.screenRowBytes;
		spanCoreState.spanLeftX = current.leftX;
		spanCoreState.spanRightX = current.rightX;
		spanCoreState.startTexU = current.spanLeftTexU;
		spanCoreState.startTexV = current.spanLeftTexV;
		spanCoreState.endTexU = current.spanRightTexU;
		spanCoreState.endTexV = current.spanRightTexV;
		spanCoreState.transparentWord =
		    SlipBytes_ReadLE16(current.lockedPayload + SLIP_SPRITE_TRANSPARENT_COLOUR_OFFSET);
		spanCoreState.textureRows = current.textureRows;
		spanCoreState.textureRowCount = current.textureRowCount;
		spanCoreState.textureRowBytes = current.textureRowBytes;
		visit->calledDrawTexturedSpanCore = true;
		if (!Raster_DrawTexturedSpanCore(&spanCoreState, &visit->spanCore)) {
			return 0;
		}

		++current.scanline;
		current.spanLeftTexU += (uint32_t)current.leftTexUStep;
		current.spanLeftTexV += (uint32_t)current.leftTexVStep;
		current.spanRightTexU += (uint32_t)current.rightTexUStep;
		current.spanRightTexV += (uint32_t)current.rightTexVStep;
		visit->spanLeftTexUAfter = current.spanLeftTexU;
		visit->spanLeftTexVAfter = current.spanLeftTexV;
		visit->spanRightTexUAfter = current.spanRightTexU;
		visit->spanRightTexVAfter = current.spanRightTexV;

		current.leftXFraction = Raster_AddU16WithCarry(current.leftXFraction, leftStepLo, &leftCarry);
		visit->leftFractionCarry = leftCarry;
		current.leftX = (int16_t)((uint16_t)current.leftX + leftStepHi + (leftCarry ? 1u : 0u));
		current.rightXFraction = Raster_AddU16WithCarry(current.rightXFraction, rightStepLo, &rightCarry);
		visit->rightFractionCarry = rightCarry;
		current.rightX = (int16_t)((uint16_t)current.rightX + rightStepHi + (rightCarry ? 1u : 0u));

		--current.leftRemaining;
		visit->leftRemainingAfterDec = current.leftRemaining;
		if (current.leftRemaining == 0u) {
			RasterAffineLeftEdgeStep leftStep;

			visit->calledStepLeftEdgeAffine = true;
			if (!Raster_StepLeftEdgeAffine(current.pointBuffer, current.pointBufferBase, current.pointBufferEnd,
			                               current.pointBufferBytes, current.leftPointOffset, current.scanline,
			                               current.bottomY, &leftStep)) {
				return 0;
			}
			visit->carryFromLeftAffineEdge = leftStep.carryOut;
			if (leftStep.carryOut) {
				result->jumpFromLeftCarry = true;
				++result->visitCount;
				result->out = current;
				result->popad = true;
				return 1;
			}
			current.leftPointOffset = leftStep.pointOffsetOut;
			current.leftX = leftStep.currentX;
			current.spanLeftTexU = leftStep.spanLeftTexU;
			current.spanLeftTexV = leftStep.spanLeftTexV;
			current.leftTexUStep = leftStep.texUStep;
			current.leftTexVStep = leftStep.texVStep;
			current.leftXStep = leftStep.xStep;
			current.leftXFraction = leftStep.xFraction;
			current.leftRemaining = leftStep.remaining;
			leftStepLo = (uint16_t)current.leftXStep;
			leftStepHi = (uint16_t)((uint32_t)current.leftXStep >> RASTER_FRACTION_BITS);
		}

		--current.rightRemaining;
		visit->rightRemainingAfterDec = current.rightRemaining;
		if (current.rightRemaining == 0u) {
			RasterAffineRightEdgeStep rightStep;

			visit->calledStepRightEdgeAffine = true;
			if (!Raster_StepRightEdgeAffine(current.pointBuffer, current.pointBufferBase, current.pointBufferEnd,
			                                current.pointBufferBytes, current.rightPointOffset, current.scanline,
			                                current.bottomY, &rightStep)) {
				return 0;
			}
			visit->carryFromRightAffineEdge = rightStep.carryOut;
			if (rightStep.carryOut) {
				result->jumpFromRightCarry = true;
				++result->visitCount;
				result->out = current;
				result->popad = true;
				return 1;
			}
			current.rightPointOffset = rightStep.pointOffsetOut;
			current.rightX = rightStep.currentX;
			current.spanRightTexU = rightStep.spanRightTexU;
			current.spanRightTexV = rightStep.spanRightTexV;
			current.rightTexUStep = rightStep.texUStep;
			current.rightTexVStep = rightStep.texVStep;
			current.rightXStep = rightStep.xStep;
			current.rightXFraction = rightStep.xFraction;
			current.rightRemaining = rightStep.remaining;
			rightStepLo = (uint16_t)current.rightXStep;
			rightStepHi = (uint16_t)((uint32_t)current.rightXStep >> RASTER_FRACTION_BITS);
		}

		visit->loop = current.scanline < current.bottomY;
		++result->visitCount;
	}

	if (current.scanline < 0 || (size_t)current.scanline >= current.screenRowCount ||
	    current.screenRows[current.scanline] == NULL) {
		return 0;
	}
	{
		RasterTexturedSpanCoreState finalSpanState;

		memset(&finalSpanState, 0, sizeof(finalSpanState));
		finalSpanState.screenRow = current.screenRows[current.scanline];
		finalSpanState.screenRowBytes = current.screenRowBytes;
		finalSpanState.spanLeftX = current.leftX;
		finalSpanState.spanRightX = current.rightX;
		finalSpanState.startTexU = current.spanLeftTexU;
		finalSpanState.startTexV = current.spanLeftTexV;
		finalSpanState.endTexU = current.spanRightTexU;
		finalSpanState.endTexV = current.spanRightTexV;
		finalSpanState.transparentWord =
		    SlipBytes_ReadLE16(current.lockedPayload + SLIP_SPRITE_TRANSPARENT_COLOUR_OFFSET);
		finalSpanState.textureRows = current.textureRows;
		finalSpanState.textureRowCount = current.textureRowCount;
		finalSpanState.textureRowBytes = current.textureRowBytes;
		result->calledFinalTexturedSpanCore = true;
		if (!Raster_DrawTexturedSpanCore(&finalSpanState, &result->finalSpanCore)) {
			return 0;
		}
	}
	result->out = current;
	result->popad = true;
	return 1;
}

/* Largest power of four representable in uint64_t, for the base-four square root. */
static const uint64_t RASTER_ISQRT_INITIAL_BIT = UINT64_C(1) << 62;

static uint32_t Raster_IsqrtU64(uint64_t value) {
	uint64_t bit = RASTER_ISQRT_INITIAL_BIT;
	uint64_t root = 0;

	while (bit > value) {
		bit >>= 2;
	}
	while (bit != 0) {
		if (value >= root + bit) {
			value -= root + bit;
			root = (root >> 1) + bit;
		} else {
			root >>= 1;
		}
		bit >>= 2;
	}
	return (uint32_t)root;
}

static void Raster_NormalizePair(int32_t componentX, int32_t componentY, int32_t *resultX, int32_t *resultY) {
	int32_t absX = componentX;
	int32_t absY = componentY;
	int signX = 0;
	int signY = 0;
	uint32_t highBits;
	uint64_t squared;
	uint32_t length;

	if (absX < 0) {
		absX = -absX;
		signX = 1;
	}
	if (absY < 0) {
		absY = -absY;
		signY = 1;
	}
	highBits = ((uint32_t)absX | (uint32_t)absY) & RASTER_NORMALIZE_COMPONENT_HIGH_MASK;
	if (highBits != 0u) {
		uint32_t scan = highBits;
		int shift = 0;

		while (scan >>= 1) {
			++shift;
		}
		shift -= RASTER_NORMALIZE_MAXIMUM_COMPONENT_BIT;
		absX >>= shift;
		absY >>= shift;
	}
	if (signX) {
		absX = -absX;
	}
	if (signY) {
		absY = -absY;
	}
	squared = (uint64_t)(uint32_t)((int16_t)absX * (int16_t)absX) + (uint64_t)(uint32_t)((int16_t)absY * (int16_t)absY);
	length = Raster_IsqrtU64(squared << 2);
	length = (length >> 1) + (length & 1u);
	if (length == 0u) {
		*resultX = 0;
		*resultY = 0;
		return;
	}

	*resultX = (int16_t)((((int32_t)(int16_t)absX << RASTER_FRACTION_BITS) >> RASTER_NORMALIZE_OUTPUT_SHIFT) /
	                     (int32_t)(int16_t)length);
	*resultY = (int16_t)((((int32_t)(int16_t)absY << RASTER_FRACTION_BITS) >> RASTER_NORMALIZE_OUTPUT_SHIFT) /
	                     (int32_t)(int16_t)length);
}

static void Raster_FindPerspectiveSplit(uint32_t firstScreenFraction, uint32_t firstTextureFraction,
                                        uint32_t secondScreenFraction, uint32_t secondTextureFraction, uint32_t depth,
                                        uint32_t *splitScreenFractionOut, uint32_t *splitTextureFractionOut) {
	static uint32_t bestScreenFraction;
	static uint32_t bestTextureFraction;
	uint32_t startScreenFraction = firstScreenFraction;
	uint32_t startTextureFraction = firstTextureFraction;
	uint32_t endScreenFraction = secondScreenFraction;
	uint32_t endTextureFraction = secondTextureFraction;
	uint32_t currentScreenFraction;
	uint32_t screenFractionStep;
	int32_t normalizedTextureDelta;
	int32_t negativeNormalizedScreenDelta;
	int32_t bestScore = 0;
	int i;

	if (startScreenFraction > endScreenFraction) {
		uint32_t tmp = startScreenFraction;

		startScreenFraction = endScreenFraction;
		endScreenFraction = tmp;
		tmp = startTextureFraction;
		startTextureFraction = endTextureFraction;
		endTextureFraction = tmp;
	}
	if (endScreenFraction == startScreenFraction) {
		*splitScreenFractionOut = startScreenFraction;
		*splitTextureFractionOut = (uint32_t)((int32_t)(startTextureFraction + endTextureFraction) >> 1);
		return;
	}
	{
		int32_t normalizedScreenDelta;
		int32_t normalizedPairTextureDelta;

		Raster_NormalizePair((int32_t)(endScreenFraction - startScreenFraction),
		                     (int32_t)(endTextureFraction - startTextureFraction), &normalizedScreenDelta,
		                     &normalizedPairTextureDelta);
		normalizedTextureDelta = normalizedPairTextureDelta;
		negativeNormalizedScreenDelta = -normalizedScreenDelta;
	}
	currentScreenFraction = startScreenFraction;
	screenFractionStep = (endScreenFraction - startScreenFraction) / RASTER_PERSPECTIVE_SPLIT_SAMPLE_COUNT;
	for (i = 0; i < RASTER_PERSPECTIVE_SPLIT_SAMPLE_COUNT; ++i) {
		const uint64_t product = (uint64_t)currentScreenFraction * depth;
		const uint32_t projected = (uint32_t)(product >> RASTER_FRACTION_BITS);
		const uint32_t denominator = projected - currentScreenFraction + RASTER_FRACTION_ONE_Q16;
		const uint32_t currentTextureFraction =
		    denominator == 0u ? 0u : (uint32_t)(((uint64_t)projected << RASTER_FRACTION_BITS) / denominator);
		const int64_t scoreWide =
		    (int64_t)(int32_t)(currentScreenFraction - startScreenFraction) * normalizedTextureDelta +
		    (int64_t)(int32_t)(currentTextureFraction - startTextureFraction) * negativeNormalizedScreenDelta;
		const int32_t score = (int32_t)(scoreWide >> RASTER_FRACTION_BITS);

		if (bestScore < score) {
			bestScore = score;
			bestScreenFraction = currentScreenFraction;
			bestTextureFraction = currentTextureFraction;
		}
		currentScreenFraction += screenFractionStep;
	}
	*splitScreenFractionOut = bestScreenFraction;
	*splitTextureFractionOut = bestTextureFraction;
}

void Raster_BuildPerspectiveTable(RasterPerspectiveEntry table[RASTER_PERSPECTIVE_ENTRY_COUNT]) {
	RasterPerspectiveEntry *entry = table;
	uint32_t depth = RASTER_PERSPECTIVE_MINIMUM_DEPTH_RATIO;
	for (;;) {
		uint32_t screenFraction;
		uint32_t textureFraction;
		uint32_t endScreenFraction;
		uint32_t endTextureFraction;

		screenFraction = 0;
		textureFraction = 0;
		endScreenFraction = RASTER_FRACTION_ONE_Q16;
		endTextureFraction = RASTER_FRACTION_ONE_Q16;
		Raster_FindPerspectiveSplit(screenFraction, textureFraction, endScreenFraction, endTextureFraction, depth,
		                            &screenFraction, &textureFraction);
		entry->splits[RASTER_PERSPECTIVE_MIDDLE_SPLIT] =
		    (RasterPerspectiveSplit){(uint16_t)screenFraction, (uint16_t)textureFraction};

		endScreenFraction = 0;
		endTextureFraction = 0;
		Raster_FindPerspectiveSplit(screenFraction, textureFraction, endScreenFraction, endTextureFraction, depth,
		                            &screenFraction, &textureFraction);
		entry->splits[RASTER_PERSPECTIVE_FIRST_QUARTER_SPLIT] =
		    (RasterPerspectiveSplit){(uint16_t)screenFraction, (uint16_t)textureFraction};

		endScreenFraction = 0;
		endTextureFraction = 0;
		Raster_FindPerspectiveSplit(screenFraction, textureFraction, endScreenFraction, endTextureFraction, depth,
		                            &screenFraction, &textureFraction);
		entry->splits[RASTER_PERSPECTIVE_FIRST_EIGHTH_SPLIT] =
		    (RasterPerspectiveSplit){(uint16_t)screenFraction, (uint16_t)textureFraction};

		endScreenFraction = 0;
		endTextureFraction = 0;
		Raster_FindPerspectiveSplit(screenFraction, textureFraction, endScreenFraction, endTextureFraction, depth,
		                            &screenFraction, &textureFraction);
		entry->splits[RASTER_PERSPECTIVE_FIRST_SIXTEENTH_SPLIT] =
		    (RasterPerspectiveSplit){(uint16_t)screenFraction, (uint16_t)textureFraction};

		screenFraction = entry->splits[RASTER_PERSPECTIVE_FIRST_EIGHTH_SPLIT].screenFraction;
		textureFraction = entry->splits[RASTER_PERSPECTIVE_FIRST_EIGHTH_SPLIT].textureFraction;
		endScreenFraction = entry->splits[RASTER_PERSPECTIVE_FIRST_QUARTER_SPLIT].screenFraction;
		endTextureFraction = entry->splits[RASTER_PERSPECTIVE_FIRST_QUARTER_SPLIT].textureFraction;
		Raster_FindPerspectiveSplit(screenFraction, textureFraction, endScreenFraction, endTextureFraction, depth,
		                            &screenFraction, &textureFraction);
		entry->splits[RASTER_PERSPECTIVE_THIRD_SIXTEENTH_SPLIT] =
		    (RasterPerspectiveSplit){(uint16_t)screenFraction, (uint16_t)textureFraction};

		screenFraction = entry->splits[RASTER_PERSPECTIVE_MIDDLE_SPLIT].screenFraction;
		textureFraction = entry->splits[RASTER_PERSPECTIVE_MIDDLE_SPLIT].textureFraction;
		endScreenFraction = entry->splits[RASTER_PERSPECTIVE_FIRST_QUARTER_SPLIT].screenFraction;
		endTextureFraction = entry->splits[RASTER_PERSPECTIVE_FIRST_QUARTER_SPLIT].textureFraction;
		Raster_FindPerspectiveSplit(screenFraction, textureFraction, endScreenFraction, endTextureFraction, depth,
		                            &screenFraction, &textureFraction);
		entry->splits[RASTER_PERSPECTIVE_THIRD_EIGHTH_SPLIT] =
		    (RasterPerspectiveSplit){(uint16_t)screenFraction, (uint16_t)textureFraction};

		screenFraction = entry->splits[RASTER_PERSPECTIVE_THIRD_EIGHTH_SPLIT].screenFraction;
		textureFraction = entry->splits[RASTER_PERSPECTIVE_THIRD_EIGHTH_SPLIT].textureFraction;
		endScreenFraction = entry->splits[RASTER_PERSPECTIVE_FIRST_QUARTER_SPLIT].screenFraction;
		endTextureFraction = entry->splits[RASTER_PERSPECTIVE_FIRST_QUARTER_SPLIT].textureFraction;
		Raster_FindPerspectiveSplit(screenFraction, textureFraction, endScreenFraction, endTextureFraction, depth,
		                            &screenFraction, &textureFraction);
		entry->splits[RASTER_PERSPECTIVE_FIFTH_SIXTEENTH_SPLIT] =
		    (RasterPerspectiveSplit){(uint16_t)screenFraction, (uint16_t)textureFraction};

		screenFraction = entry->splits[RASTER_PERSPECTIVE_THIRD_EIGHTH_SPLIT].screenFraction;
		textureFraction = entry->splits[RASTER_PERSPECTIVE_THIRD_EIGHTH_SPLIT].textureFraction;
		endScreenFraction = entry->splits[RASTER_PERSPECTIVE_MIDDLE_SPLIT].screenFraction;
		endTextureFraction = entry->splits[RASTER_PERSPECTIVE_MIDDLE_SPLIT].textureFraction;
		Raster_FindPerspectiveSplit(screenFraction, textureFraction, endScreenFraction, endTextureFraction, depth,
		                            &screenFraction, &textureFraction);
		entry->splits[RASTER_PERSPECTIVE_SEVENTH_SIXTEENTH_SPLIT] =
		    (RasterPerspectiveSplit){(uint16_t)screenFraction, (uint16_t)textureFraction};

		entry->splits[RASTER_PERSPECTIVE_LAST_SPLIT - RASTER_PERSPECTIVE_FIRST_SIXTEENTH_SPLIT] =
		    (RasterPerspectiveSplit){
		        (uint16_t)(RASTER_FRACTION_ONE_Q16 -
		                   entry->splits[RASTER_PERSPECTIVE_FIRST_SIXTEENTH_SPLIT].textureFraction),
		        (uint16_t)(RASTER_FRACTION_ONE_Q16 -
		                   entry->splits[RASTER_PERSPECTIVE_FIRST_SIXTEENTH_SPLIT].screenFraction)};
		entry->splits[RASTER_PERSPECTIVE_LAST_SPLIT - RASTER_PERSPECTIVE_FIRST_EIGHTH_SPLIT] = (RasterPerspectiveSplit){
		    (uint16_t)(RASTER_FRACTION_ONE_Q16 - entry->splits[RASTER_PERSPECTIVE_FIRST_EIGHTH_SPLIT].textureFraction),
		    (uint16_t)(RASTER_FRACTION_ONE_Q16 - entry->splits[RASTER_PERSPECTIVE_FIRST_EIGHTH_SPLIT].screenFraction)};
		entry->splits[RASTER_PERSPECTIVE_LAST_SPLIT - RASTER_PERSPECTIVE_THIRD_SIXTEENTH_SPLIT] =
		    (RasterPerspectiveSplit){
		        (uint16_t)(RASTER_FRACTION_ONE_Q16 -
		                   entry->splits[RASTER_PERSPECTIVE_THIRD_SIXTEENTH_SPLIT].textureFraction),
		        (uint16_t)(RASTER_FRACTION_ONE_Q16 -
		                   entry->splits[RASTER_PERSPECTIVE_THIRD_SIXTEENTH_SPLIT].screenFraction)};
		entry->splits[RASTER_PERSPECTIVE_LAST_QUARTER_SPLIT] = (RasterPerspectiveSplit){
		    (uint16_t)(RASTER_FRACTION_ONE_Q16 - entry->splits[RASTER_PERSPECTIVE_FIRST_QUARTER_SPLIT].textureFraction),
		    (uint16_t)(RASTER_FRACTION_ONE_Q16 - entry->splits[RASTER_PERSPECTIVE_FIRST_QUARTER_SPLIT].screenFraction)};
		entry->splits[RASTER_PERSPECTIVE_LAST_SPLIT - RASTER_PERSPECTIVE_FIFTH_SIXTEENTH_SPLIT] =
		    (RasterPerspectiveSplit){
		        (uint16_t)(RASTER_FRACTION_ONE_Q16 -
		                   entry->splits[RASTER_PERSPECTIVE_FIFTH_SIXTEENTH_SPLIT].textureFraction),
		        (uint16_t)(RASTER_FRACTION_ONE_Q16 -
		                   entry->splits[RASTER_PERSPECTIVE_FIFTH_SIXTEENTH_SPLIT].screenFraction)};
		entry->splits[RASTER_PERSPECTIVE_LAST_SPLIT - RASTER_PERSPECTIVE_THIRD_EIGHTH_SPLIT] = (RasterPerspectiveSplit){
		    (uint16_t)(RASTER_FRACTION_ONE_Q16 - entry->splits[RASTER_PERSPECTIVE_THIRD_EIGHTH_SPLIT].textureFraction),
		    (uint16_t)(RASTER_FRACTION_ONE_Q16 - entry->splits[RASTER_PERSPECTIVE_THIRD_EIGHTH_SPLIT].screenFraction)};
		entry->splits[RASTER_PERSPECTIVE_LAST_SPLIT - RASTER_PERSPECTIVE_SEVENTH_SIXTEENTH_SPLIT] =
		    (RasterPerspectiveSplit){
		        (uint16_t)(RASTER_FRACTION_ONE_Q16 -
		                   entry->splits[RASTER_PERSPECTIVE_SEVENTH_SIXTEENTH_SPLIT].textureFraction),
		        (uint16_t)(RASTER_FRACTION_ONE_Q16 -
		                   entry->splits[RASTER_PERSPECTIVE_SEVENTH_SIXTEENTH_SPLIT].screenFraction)};

		++entry;
		if (depth == RASTER_PERSPECTIVE_MINIMUM_DEPTH_RATIO) {
			depth = 0;
		}
		depth += RASTER_PERSPECTIVE_DEPTH_RATIO_STEP;
		if (depth > RASTER_PERSPECTIVE_MAXIMUM_TABLE_DEPTH_RATIO) {
			break;
		}
	}
}

RasterPerspectiveEntry *Raster_perspectiveTable;

static const RasterPerspectiveEntry *Raster_PerspectiveEntry(uint32_t bucket, uint32_t bias) {
	return &Raster_perspectiveTable[(bucket + bias) >> RASTER_PERSPECTIVE_ENTRY_BUCKET_SHIFT];
}

static int Raster_DrawTexturedRun(uint8_t *destination, size_t destinationBytes, uint32_t pixelCount,
                                  uint32_t *currentTexU, uint32_t *currentTexV, int32_t texUStep, int32_t texVStep,
                                  uint16_t transparentWord, const uint8_t *const *textureRows, size_t textureRowCount,
                                  size_t textureRowBytes, size_t *pixelsWritten, uint32_t runIndex,
                                  RasterTexturedSpanDispatch *diagnostic) {
	uint32_t i;
	const int masked = transparentWord != SLIP_SPRITE_NO_TRANSPARENT_COLOUR;

	if (currentTexU == NULL || currentTexV == NULL || pixelsWritten == NULL || destination == NULL ||
	    destinationBytes < pixelCount) {
		if (diagnostic != NULL) {
			diagnostic->longSpanFailStage = RASTER_LONG_SPAN_INVALID_RUN;
			diagnostic->longSpanFailRun = runIndex;
			diagnostic->longSpanFailPixel = pixelCount;
		}
		return 0;
	}
	for (i = 0; i < pixelCount; ++i) {
		uint8_t sample;

		if (!Raster_SampleTextureByte(textureRows, textureRowCount, textureRowBytes, *currentTexU, *currentTexV,
		                              &sample)) {
			if (diagnostic != NULL) {
				diagnostic->longSpanFailRun = runIndex;
				diagnostic->longSpanFailStage = RASTER_LONG_SPAN_INVALID_TEXTURE_SAMPLE;
				diagnostic->longSpanFailPixel = i;
				diagnostic->longSpanFailTexU = *currentTexU;
				diagnostic->longSpanFailTexV = *currentTexV;
				diagnostic->longSpanFailTexX = (uint16_t)(*currentTexU >> RASTER_FRACTION_BITS);
				diagnostic->longSpanFailTexY = (uint16_t)(*currentTexV >> RASTER_FRACTION_BITS);
			}
			return 0;
		}
		if (!masked || sample != (uint8_t)transparentWord) {
			destination[i] = sample;
			++*pixelsWritten;
		}
		*currentTexU += (uint32_t)texUStep;
		*currentTexV += (uint32_t)texVStep;
	}
	return 1;
}

static int Raster_DispatchTwoSegmentTexturedSpan(const RasterTexturedSpanDispatchState *state, uint32_t ratioBucket,
                                                 RasterTexturedSpanDispatch *result) {
	const RasterPerspectiveEntry *perspectiveEntry;
	uint16_t segmentCoeff;
	uint16_t interpCoeff;
	uint16_t segment0;
	uint32_t segment1;
	int32_t texUDelta;
	int32_t texVDelta;
	int32_t segment0TexUDelta;
	int32_t segment0TexVDelta;
	int32_t segment1TexUDelta;
	int32_t segment1TexVDelta;
	int32_t segment0TexUStep;
	int32_t segment0TexVStep;
	int32_t segment1TexUStep;
	int32_t segment1TexVStep;
	uint32_t currentTexU;
	uint32_t currentTexV;
	uint8_t *destination;
	size_t destinationBytes;
	RasterTexturedSpanCoreState fallbackCoreState;

	if (state == NULL || result == NULL || state->screenRow == NULL || state->spanLeftX < 0 || state->spanRightX < 0 ||
	    (size_t)state->spanRightX >= state->screenRowBytes) {
		if (result != NULL) {
			result->longSpanFailStage = RASTER_LONG_SPAN_INVALID_SPAN;
		}
		return 0;
	}
	perspectiveEntry = Raster_PerspectiveEntry(ratioBucket, RASTER_PERSPECTIVE_TWO_SEGMENT_BUCKET_BIAS);
	segmentCoeff = perspectiveEntry->splits[RASTER_PERSPECTIVE_MIDDLE_SPLIT].screenFraction;
	interpCoeff = perspectiveEntry->splits[RASTER_PERSPECTIVE_MIDDLE_SPLIT].textureFraction;
	if (result->depthSwapped) {
		const uint16_t tmp = segmentCoeff;

		segmentCoeff = interpCoeff;
		interpCoeff = tmp;
	}
	segment0 = Raster_MultiplyUnsigned16High((uint16_t)result->inclusiveWidth, segmentCoeff);
	result->longSpanSegment0PixelCount = segment0;
	if (segment0 == 0u) {
		memset(&fallbackCoreState, 0, sizeof(fallbackCoreState));
		fallbackCoreState.screenRow = state->screenRow;
		fallbackCoreState.screenRowBytes = state->screenRowBytes;
		fallbackCoreState.spanLeftX = state->spanLeftX;
		fallbackCoreState.spanRightX = state->spanRightX;
		fallbackCoreState.startTexU = state->startTexU;
		fallbackCoreState.startTexV = state->startTexV;
		fallbackCoreState.endTexU = state->endTexU;
		fallbackCoreState.endTexV = state->endTexV;
		fallbackCoreState.transparentWord = state->transparentWord;
		fallbackCoreState.textureRows = state->textureRows;
		fallbackCoreState.textureRowCount = state->textureRowCount;
		fallbackCoreState.textureRowBytes = state->textureRowBytes;
		if (!Raster_DrawTexturedSpanCore(&fallbackCoreState, &result->shortSpanCore)) {
			return 0;
		}
		result->longSpanPixelsWritten = result->shortSpanCore.pixelsWritten;
		return 1;
	}
	segment1 = result->inclusiveWidth - segment0;
	result->longSpanSegment1PixelCount = (uint16_t)segment1;
	if (segment1 == 0u) {
		memset(&fallbackCoreState, 0, sizeof(fallbackCoreState));
		fallbackCoreState.screenRow = state->screenRow;
		fallbackCoreState.screenRowBytes = state->screenRowBytes;
		fallbackCoreState.spanLeftX = state->spanLeftX;
		fallbackCoreState.spanRightX = state->spanRightX;
		fallbackCoreState.startTexU = state->startTexU;
		fallbackCoreState.startTexV = state->startTexV;
		fallbackCoreState.endTexU = state->endTexU;
		fallbackCoreState.endTexV = state->endTexV;
		fallbackCoreState.transparentWord = state->transparentWord;
		fallbackCoreState.textureRows = state->textureRows;
		fallbackCoreState.textureRowCount = state->textureRowCount;
		fallbackCoreState.textureRowBytes = state->textureRowBytes;
		if (!Raster_DrawTexturedSpanCore(&fallbackCoreState, &result->shortSpanCore)) {
			return 0;
		}
		result->longSpanPixelsWritten = result->shortSpanCore.pixelsWritten;
		return 1;
	}
	texUDelta = (int32_t)(state->endTexU - state->startTexU);
	segment0TexUDelta = Raster_MultiplySignedShift16(interpCoeff, texUDelta);
	segment1TexUDelta = texUDelta - segment0TexUDelta;
	segment0TexUStep = segment0TexUDelta / (int32_t)segment0;
	segment1TexUStep = segment1TexUDelta / (int32_t)segment1;

	texVDelta = (int32_t)(state->endTexV - state->startTexV);
	segment0TexVDelta = Raster_MultiplySignedShift16(interpCoeff, texVDelta);
	segment1TexVDelta = texVDelta - segment0TexVDelta;
	segment0TexVStep = segment0TexVDelta / (int32_t)segment0;
	segment1TexVStep = segment1TexVDelta / (int32_t)segment1;
	result->longSpanStartTexU = state->startTexU;
	result->longSpanStartTexV = state->startTexV;
	result->longSpanStep0TexU = segment0TexUStep;
	result->longSpanStep0TexV = segment0TexVStep;
	result->longSpanStep1TexU = segment1TexUStep;
	result->longSpanStep1TexV = segment1TexVStep;

	destination = state->screenRow + state->spanLeftX;
	destinationBytes = state->screenRowBytes - (size_t)state->spanLeftX;
	currentTexU = state->startTexU;
	currentTexV = state->startTexV;
	result->maskedLongSpan = state->transparentWord != SLIP_SPRITE_NO_TRANSPARENT_COLOUR;
	if (!Raster_DrawTexturedRun(destination, destinationBytes, segment0, &currentTexU, &currentTexV, segment0TexUStep,
	                            segment0TexVStep, state->transparentWord, state->textureRows, state->textureRowCount,
	                            state->textureRowBytes, &result->longSpanPixelsWritten, 0u, result)) {
		return 0;
	}
	destination += segment0;
	destinationBytes -= segment0;
	if (!Raster_DrawTexturedRun(destination, destinationBytes, segment1, &currentTexU, &currentTexV, segment1TexUStep,
	                            segment1TexVStep, state->transparentWord, state->textureRows, state->textureRowCount,
	                            state->textureRowBytes, &result->longSpanPixelsWritten, 1u, result)) {
		return 0;
	}
	return 1;
}

static int Raster_DispatchEightSegmentTexturedSpan(const RasterTexturedSpanDispatchState *state, uint32_t ratioBucket,
                                                   RasterTexturedSpanDispatch *result) {
	enum {
		SEGMENT_COUNT = 8,
		SPLIT_COUNT = SEGMENT_COUNT - 1,
		LAST_SPLIT = SPLIT_COUNT - 1,
		FINAL_SEGMENT = SEGMENT_COUNT - 1
	};

	const RasterPerspectiveEntry *perspectiveEntry;
	uint16_t segmentCountCoeff[SPLIT_COUNT];
	uint16_t segmentInterpCoeff[SPLIT_COUNT];
	uint16_t segmentPixels[SEGMENT_COUNT];
	int32_t texUSteps[SEGMENT_COUNT] = {0};
	int32_t texVSteps[SEGMENT_COUNT] = {0};
	uint16_t spanWidth;
	uint16_t cumulative;
	int16_t remainingPixels;
	int32_t texUDelta;
	int32_t texVDelta;
	int32_t previousTexUEnd = 0;
	int32_t previousTexVEnd = 0;
	int32_t remainingTexU;
	int32_t remainingTexV;
	uint32_t currentTexU;
	uint32_t currentTexV;
	uint8_t *destination;
	size_t destinationBytes;
	size_t i;

	if (state == NULL || result == NULL || state->screenRow == NULL || state->spanLeftX < 0 || state->spanRightX < 0 ||
	    (size_t)state->spanRightX >= state->screenRowBytes) {
		if (result != NULL) {
			result->longSpanFailStage = RASTER_LONG_SPAN_INVALID_SPAN;
		}
		return 0;
	}

	perspectiveEntry = Raster_PerspectiveEntry(ratioBucket, RASTER_PERSPECTIVE_MULTI_SEGMENT_BUCKET_BIAS);
	for (i = 0; i < SPLIT_COUNT; ++i) {
		const RasterPerspectiveSplit *const split =
		    &perspectiveEntry
		         ->splits[RASTER_PERSPECTIVE_FIRST_EIGHTH_SPLIT + i * RASTER_PERSPECTIVE_EIGHTH_SPLIT_STRIDE];
		const uint16_t lowWord = split->screenFraction;
		const uint16_t highWord = split->textureFraction;

		if (!result->depthSwapped) {
			segmentCountCoeff[i] = lowWord;
			segmentInterpCoeff[i] = highWord;
		} else {
			segmentCountCoeff[i] = highWord;
			segmentInterpCoeff[i] = lowWord;
		}
	}

	spanWidth = (uint16_t)result->inclusiveWidth;
	segmentPixels[0] = Raster_MultiplyUnsigned16High(segmentCountCoeff[0], spanWidth);
	cumulative = segmentPixels[0];
	for (i = 1u; i < SPLIT_COUNT; ++i) {
		segmentPixels[i] = (uint16_t)(Raster_MultiplyUnsigned16High(segmentCountCoeff[i], spanWidth) - cumulative);
		cumulative = (uint16_t)(cumulative + segmentPixels[i]);
	}
	remainingPixels = (int16_t)(spanWidth - cumulative);
	segmentPixels[FINAL_SEGMENT] = remainingPixels < 0 ? 0u : (uint16_t)remainingPixels;

	result->longSpanSegment0PixelCount = segmentPixels[0];
	result->longSpanSegment1PixelCount = segmentPixels[1];
	result->longSpanSegment2PixelCount = segmentPixels[2];
	result->longSpanSegment3PixelCount = segmentPixels[3];

	texUDelta = (int32_t)(state->endTexU - state->startTexU);
	texVDelta = (int32_t)(state->endTexV - state->startTexV);
	remainingTexU = texUDelta;
	remainingTexV = texVDelta;
	for (i = 0; i < LAST_SPLIT; ++i) {
		if (segmentPixels[i] != 0u) {
			const int32_t texUEnd = Raster_MultiplySignedShift16(segmentInterpCoeff[i], texUDelta);
			const int32_t texVEnd = Raster_MultiplySignedShift16(segmentInterpCoeff[i], texVDelta);

			texUSteps[i] = (texUEnd - previousTexUEnd) / (int32_t)segmentPixels[i];
			texVSteps[i] = (texVEnd - previousTexVEnd) / (int32_t)segmentPixels[i];
			previousTexUEnd = texUEnd;
			previousTexVEnd = texVEnd;
		}
	}
	if (segmentPixels[LAST_SPLIT] != 0u) {
		const int32_t texUEnd = Raster_MultiplySignedShift16(segmentInterpCoeff[LAST_SPLIT], texUDelta);
		const int32_t texVEnd = Raster_MultiplySignedShift16(segmentInterpCoeff[LAST_SPLIT], texVDelta);

		remainingTexU = texUDelta - texUEnd;
		remainingTexV = texVDelta - texVEnd;
		texUSteps[LAST_SPLIT] = (texUEnd - previousTexUEnd) / (int32_t)segmentPixels[LAST_SPLIT];
		texVSteps[LAST_SPLIT] = (texVEnd - previousTexVEnd) / (int32_t)segmentPixels[LAST_SPLIT];
	}
	if (segmentPixels[FINAL_SEGMENT] != 0u) {
		texUSteps[FINAL_SEGMENT] = remainingTexU / (int32_t)segmentPixels[FINAL_SEGMENT];
		texVSteps[FINAL_SEGMENT] = remainingTexV / (int32_t)segmentPixels[FINAL_SEGMENT];
	}

	result->longSpanStartTexU = state->startTexU;
	result->longSpanStartTexV = state->startTexV;
	result->longSpanStep0TexU = texUSteps[0];
	result->longSpanStep0TexV = texVSteps[0];
	result->longSpanStep1TexU = texUSteps[1];
	result->longSpanStep1TexV = texVSteps[1];

	destination = state->screenRow + state->spanLeftX;
	destinationBytes = state->screenRowBytes - (size_t)state->spanLeftX;
	currentTexU = state->startTexU;
	currentTexV = state->startTexV;
	result->maskedLongSpan = state->transparentWord != SLIP_SPRITE_NO_TRANSPARENT_COLOUR;
	for (i = 0; i < SEGMENT_COUNT; ++i) {
		if (!Raster_DrawTexturedRun(destination, destinationBytes, segmentPixels[i], &currentTexU, &currentTexV,
		                            texUSteps[i], texVSteps[i], state->transparentWord, state->textureRows,
		                            state->textureRowCount, state->textureRowBytes, &result->longSpanPixelsWritten,
		                            (uint32_t)i, result)) {
			return 0;
		}
		destination += segmentPixels[i];
		destinationBytes -= segmentPixels[i];
	}
	return 1;
}

static int Raster_DispatchSavedTexturedSpanCore(const RasterTexturedSpanDispatchState *state,
                                                RasterTexturedSpanDispatch *result) {
	RasterTexturedSpanCoreState coreState;

	if (state == NULL || result == NULL) {
		return 0;
	}
	memset(&coreState, 0, sizeof(coreState));
	coreState.screenRow = state->screenRow;
	coreState.screenRowBytes = state->screenRowBytes;
	coreState.spanLeftX = state->spanLeftX;
	coreState.spanRightX = state->spanRightX;
	coreState.startTexU = state->startTexU;
	coreState.startTexV = state->startTexV;
	coreState.endTexU = state->endTexU;
	coreState.endTexV = state->endTexV;
	coreState.transparentWord = state->transparentWord;
	coreState.textureRows = state->textureRows;
	coreState.textureRowCount = state->textureRowCount;
	coreState.textureRowBytes = state->textureRowBytes;
	if (!Raster_DrawTexturedSpanCore(&coreState, &result->shortSpanCore)) {
		return 0;
	}
	result->longSpanPixelsWritten = result->shortSpanCore.pixelsWritten;
	return 1;
}

int Raster_DispatchTexturedSpan(const RasterTexturedSpanDispatchState *state, RasterTexturedSpanDispatch *result) {
	uint32_t depthLow;
	uint32_t depthHigh;
	uint32_t ratioBucket;
	RasterTexturedSpanCoreState coreState;

	if (state == NULL || result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->in = *state;

	result->spanNegatedDelta = -(state->spanLeftX - state->spanRightX);
	result->negativeWidthReturn = result->spanNegatedDelta < 0;
	if (result->negativeWidthReturn) {
		return 1;
	}
	result->inclusiveWidth = (uint32_t)result->spanNegatedDelta + 1u;
	result->jumpShortSpan = result->inclusiveWidth < RASTER_PERSPECTIVE_SHORT_SPAN_WIDTH;
	if (result->jumpShortSpan) {
		memset(&coreState, 0, sizeof(coreState));
		coreState.screenRow = state->screenRow;
		coreState.screenRowBytes = state->screenRowBytes;
		coreState.spanLeftX = state->spanLeftX;
		coreState.spanRightX = state->spanRightX;
		coreState.startTexU = state->startTexU;
		coreState.startTexV = state->startTexV;
		coreState.endTexU = state->endTexU;
		coreState.endTexV = state->endTexV;
		coreState.transparentWord = state->transparentWord;
		coreState.textureRows = state->textureRows;
		coreState.textureRowCount = state->textureRowCount;
		coreState.textureRowBytes = state->textureRowBytes;
		return Raster_DrawTexturedSpanCore(&coreState, &result->shortSpanCore);
	}

	if (state->screenRow == NULL || state->spanLeftX < 0 || (size_t)state->spanLeftX >= state->screenRowBytes) {
		return 0;
	}
	result->longSpanStateSaved = true;
	result->savedWidth = result->inclusiveWidth;
	result->savedDestination = (uintptr_t)(state->screenRow + state->spanLeftX);
	result->savedScanline = state->scanlineIndex;
	result->savedLeftX = state->spanLeftX;
	result->savedRightX = state->spanRightX;
	result->savedEndpointBase = state->endpointBase;
	result->depthA = state->startDepth;
	result->depthB = state->endDepth;
	result->equalDepthReturn = state->startDepth == state->endDepth;
	if (result->equalDepthReturn) {
		return Raster_DispatchSavedTexturedSpanCore(state, result);
	}

	depthLow = state->startDepth;
	depthHigh = state->endDepth;

	result->depthSwapped = (int32_t)depthLow >= (int32_t)depthHigh;
	if (result->depthSwapped) {
		const uint32_t tmp = depthLow;

		depthLow = depthHigh;
		depthHigh = tmp;
	}
	if (depthHigh == 0u) {
		return 0;
	}
	ratioBucket = Raster_DepthRatioBucket(depthLow, depthHigh);
	result->reciprocalBucket = ratioBucket;
	result->reciprocalTooLargeReturn = ratioBucket >= RASTER_PERSPECTIVE_AFFINE_DEPTH_RATIO_MINIMUM;
	if (result->reciprocalTooLargeReturn) {
		return Raster_DispatchSavedTexturedSpanCore(state, result);
	}
	result->reciprocalClamped = ratioBucket < RASTER_PERSPECTIVE_MINIMUM_DEPTH_RATIO;
	if (result->reciprocalClamped) {
		ratioBucket = RASTER_PERSPECTIVE_MINIMUM_DEPTH_RATIO;
		result->reciprocalBucket = ratioBucket;
	}
	result->longSpanBranch = true;
	if (ratioBucket >= RASTER_PERSPECTIVE_TWO_SEGMENT_DEPTH_RATIO_MINIMUM) {
		result->twoSegmentBranch = true;
		return Raster_DispatchTwoSegmentTexturedSpan(state, ratioBucket, result);
	}
	if (ratioBucket <= RASTER_PERSPECTIVE_EIGHT_SEGMENT_DEPTH_RATIO_MAXIMUM) {
		result->lowDepthRatioBranch = true;
		if (result->inclusiveWidth >= RASTER_PERSPECTIVE_EIGHT_SEGMENT_WIDTH_MINIMUM) {
			return Raster_DispatchEightSegmentTexturedSpan(state, ratioBucket, result);
		}
	}
	if (result->inclusiveWidth < RASTER_PERSPECTIVE_FOUR_SEGMENT_WIDTH_MINIMUM) {
		result->twoSegmentBranch = true;
		return Raster_DispatchTwoSegmentTexturedSpan(state, ratioBucket, result);
	}
	result->fourSegmentBranch = true;
	{
		const RasterPerspectiveEntry *perspectiveEntry;
		RasterTexturedLongSpanSetupState setupState;
		RasterTexturedLongSpanSetup setup;
		uint8_t *destination;
		size_t destinationBytes;
		uint32_t currentTexU;
		uint32_t currentTexV;

		if ((size_t)state->spanRightX >= state->screenRowBytes) {
			return 0;
		}
		perspectiveEntry = Raster_PerspectiveEntry(ratioBucket, RASTER_PERSPECTIVE_MULTI_SEGMENT_BUCKET_BIAS);
		memset(&setupState, 0, sizeof(setupState));
		setupState.reciprocalBucket = ratioBucket;
		setupState.spanWidth = result->inclusiveWidth;
		setupState.depthSwapped = result->depthSwapped;
		setupState.startTexU = state->startTexU;
		setupState.startTexV = state->startTexV;
		setupState.endTexU = state->endTexU;
		setupState.endTexV = state->endTexV;
		setupState.perspectiveTableBase = RASTER_PERSPECTIVE_TABLE_BASE_TOKEN;
		setupState.split3PackedFractions =
		    ((uint32_t)perspectiveEntry->splits[RASTER_PERSPECTIVE_FIRST_QUARTER_SPLIT].textureFraction << 16) |
		    perspectiveEntry->splits[RASTER_PERSPECTIVE_FIRST_QUARTER_SPLIT].screenFraction;
		setupState.split7PackedFractions =
		    ((uint32_t)perspectiveEntry->splits[RASTER_PERSPECTIVE_MIDDLE_SPLIT].textureFraction << 16) |
		    perspectiveEntry->splits[RASTER_PERSPECTIVE_MIDDLE_SPLIT].screenFraction;
		setupState.split11PackedFractions =
		    ((uint32_t)perspectiveEntry->splits[RASTER_PERSPECTIVE_LAST_QUARTER_SPLIT].textureFraction << 16) |
		    perspectiveEntry->splits[RASTER_PERSPECTIVE_LAST_QUARTER_SPLIT].screenFraction;
		result->calledSetupLongTexturedSpan = true;
		if (!Raster_SetupLongTexturedSpan(&setupState, &setup)) {
			return 0;
		}
		result->longSpanSegment0PixelCount = setup.segment0PixelCount;
		result->longSpanSegment1PixelCount = setup.segment1PixelCount;
		result->longSpanSegment2PixelCount = setup.segment2PixelCount;
		result->longSpanSegment3PixelCount = setup.segment3PixelCount;

		destination = state->screenRow + state->spanLeftX;
		destinationBytes = state->screenRowBytes - (size_t)state->spanLeftX;
		currentTexU = setup.startTexU;
		currentTexV = setup.startTexV;
		result->maskedLongSpan = state->transparentWord != SLIP_SPRITE_NO_TRANSPARENT_COLOUR;
		if (!result->maskedLongSpan) {
			RasterTexturedLongSpanSegment0State segment0State;
			RasterTexturedLongSpanSegment0 segment0;
			RasterTexturedLongSpanSegment1State segment1State;
			RasterTexturedLongSpanSegment1 segment1;
			RasterTexturedLongSpanSegment2State segment2State;
			RasterTexturedLongSpanSegment2 segment2;
			RasterTexturedLongSpanSegment3State segment3State;
			RasterTexturedLongSpanSegment3 segment3;

			memset(&segment0State, 0, sizeof(segment0State));
			segment0State.destination = destination;
			segment0State.destinationBytes = destinationBytes;
			segment0State.transparentWord = state->transparentWord;
			segment0State.segment0PixelCount = setup.segment0PixelCount;
			segment0State.currentTexU = currentTexU;
			segment0State.currentTexV = currentTexV;
			segment0State.texUStep = setup.segment0TexUStep;
			segment0State.texVStep = setup.segment0TexVStep;
			segment0State.textureRows = state->textureRows;
			segment0State.textureRowCount = state->textureRowCount;
			segment0State.textureRowBytes = state->textureRowBytes;
			if (!Raster_DrawOpaqueLongTexturedSpanSegment0(&segment0State, &segment0)) {
				return 0;
			}
			currentTexU = segment0.currentTexU;
			currentTexV = segment0.currentTexV;
			destination += setup.segment0PixelCount;
			destinationBytes -= setup.segment0PixelCount;
			result->longSpanPixelsWritten += segment0.pixelsWritten;

			memset(&segment1State, 0, sizeof(segment1State));
			segment1State.destination = destination;
			segment1State.destinationBytes = destinationBytes;
			segment1State.segment1PixelCount = setup.segment1PixelCount;
			segment1State.currentTexU = currentTexU;
			segment1State.currentTexV = currentTexV;
			segment1State.texUStep = setup.segment1TexUStep;
			segment1State.texVStep = setup.segment1TexVStep;
			segment1State.textureRows = state->textureRows;
			segment1State.textureRowCount = state->textureRowCount;
			segment1State.textureRowBytes = state->textureRowBytes;
			if (!Raster_DrawOpaqueLongTexturedSpanSegment1(&segment1State, &segment1)) {
				return 0;
			}
			currentTexU = segment1.currentTexU;
			currentTexV = segment1.currentTexV;
			destination += setup.segment1PixelCount;
			destinationBytes -= setup.segment1PixelCount;
			result->longSpanPixelsWritten += segment1.pixelsWritten;

			memset(&segment2State, 0, sizeof(segment2State));
			segment2State.destination = destination;
			segment2State.destinationBytes = destinationBytes;
			segment2State.segment2PixelCount = setup.segment2PixelCount;
			segment2State.currentTexU = currentTexU;
			segment2State.currentTexV = currentTexV;
			segment2State.texUStep = setup.segment2TexUStep;
			segment2State.texVStep = setup.segment2TexVStep;
			segment2State.textureRows = state->textureRows;
			segment2State.textureRowCount = state->textureRowCount;
			segment2State.textureRowBytes = state->textureRowBytes;
			if (!Raster_DrawOpaqueLongTexturedSpanSegment2(&segment2State, &segment2)) {
				return 0;
			}
			currentTexU = segment2.currentTexU;
			currentTexV = segment2.currentTexV;
			destination += setup.segment2PixelCount;
			destinationBytes -= setup.segment2PixelCount;
			result->longSpanPixelsWritten += segment2.pixelsWritten;

			memset(&segment3State, 0, sizeof(segment3State));
			segment3State.destination = destination;
			segment3State.destinationBytes = destinationBytes;
			segment3State.segment3PixelCount = setup.segment3PixelCount;
			segment3State.currentTexU = currentTexU;
			segment3State.currentTexV = currentTexV;
			segment3State.texUStep = setup.segment3TexUStep;
			segment3State.texVStep = setup.segment3TexVStep;
			segment3State.textureRows = state->textureRows;
			segment3State.textureRowCount = state->textureRowCount;
			segment3State.textureRowBytes = state->textureRowBytes;
			if (!Raster_DrawOpaqueLongTexturedSpanSegment3(&segment3State, &segment3)) {
				return 0;
			}
			result->longSpanPixelsWritten += segment3.pixelsWritten;
		} else {
			RasterTexturedMaskedLongSpanSegment0State segment0State;
			RasterTexturedMaskedLongSpanSegment0 segment0;
			RasterTexturedMaskedLongSpanSegment1State segment1State;
			RasterTexturedMaskedLongSpanSegment1 segment1;
			RasterTexturedMaskedLongSpanSegment2State segment2State;
			RasterTexturedMaskedLongSpanSegment2 segment2;
			RasterTexturedMaskedLongSpanSegment3State segment3State;
			RasterTexturedMaskedLongSpanSegment3 segment3;

			memset(&segment0State, 0, sizeof(segment0State));
			segment0State.destination = destination;
			segment0State.destinationBytes = destinationBytes;
			segment0State.transparentByteDl = (uint8_t)state->transparentWord;
			segment0State.segment0PixelCount = setup.segment0PixelCount;
			segment0State.currentTexU = currentTexU;
			segment0State.currentTexV = currentTexV;
			segment0State.texUStep = setup.segment0TexUStep;
			segment0State.texVStep = setup.segment0TexVStep;
			segment0State.textureRows = state->textureRows;
			segment0State.textureRowCount = state->textureRowCount;
			segment0State.textureRowBytes = state->textureRowBytes;
			if (!Raster_DrawMaskedLongTexturedSpanSegment0(&segment0State, &segment0)) {
				return 0;
			}
			currentTexU = segment0.currentTexU;
			currentTexV = segment0.currentTexV;
			destination += setup.segment0PixelCount;
			destinationBytes -= setup.segment0PixelCount;
			result->longSpanPixelsWritten += segment0.pixelsWritten;

			memset(&segment1State, 0, sizeof(segment1State));
			segment1State.destination = destination;
			segment1State.destinationBytes = destinationBytes;
			segment1State.transparentByteDl = (uint8_t)state->transparentWord;
			segment1State.segment1PixelCount = setup.segment1PixelCount;
			segment1State.currentTexU = currentTexU;
			segment1State.currentTexV = currentTexV;
			segment1State.texUStep = setup.segment1TexUStep;
			segment1State.texVStep = setup.segment1TexVStep;
			segment1State.textureRows = state->textureRows;
			segment1State.textureRowCount = state->textureRowCount;
			segment1State.textureRowBytes = state->textureRowBytes;
			if (!Raster_DrawMaskedLongTexturedSpanSegment1(&segment1State, &segment1)) {
				return 0;
			}
			currentTexU = segment1.currentTexU;
			currentTexV = segment1.currentTexV;
			destination += setup.segment1PixelCount;
			destinationBytes -= setup.segment1PixelCount;
			result->longSpanPixelsWritten += segment1.pixelsWritten;

			memset(&segment2State, 0, sizeof(segment2State));
			segment2State.destination = destination;
			segment2State.destinationBytes = destinationBytes;
			segment2State.transparentByteDl = (uint8_t)state->transparentWord;
			segment2State.segment2PixelCount = setup.segment2PixelCount;
			segment2State.currentTexU = currentTexU;
			segment2State.currentTexV = currentTexV;
			segment2State.texUStep = setup.segment2TexUStep;
			segment2State.texVStep = setup.segment2TexVStep;
			segment2State.textureRows = state->textureRows;
			segment2State.textureRowCount = state->textureRowCount;
			segment2State.textureRowBytes = state->textureRowBytes;
			if (!Raster_DrawMaskedLongTexturedSpanSegment2(&segment2State, &segment2)) {
				return 0;
			}
			currentTexU = segment2.currentTexU;
			currentTexV = segment2.currentTexV;
			destination += setup.segment2PixelCount;
			destinationBytes -= setup.segment2PixelCount;
			result->longSpanPixelsWritten += segment2.pixelsWritten;

			memset(&segment3State, 0, sizeof(segment3State));
			segment3State.destination = destination;
			segment3State.destinationBytes = destinationBytes;
			segment3State.transparentByteDl = (uint8_t)state->transparentWord;
			segment3State.segment3PixelCount = setup.segment3PixelCount;
			segment3State.currentTexU = currentTexU;
			segment3State.currentTexV = currentTexV;
			segment3State.texUStep = setup.segment3TexUStep;
			segment3State.texVStep = setup.segment3TexVStep;
			segment3State.textureRows = state->textureRows;
			segment3State.textureRowCount = state->textureRowCount;
			segment3State.textureRowBytes = state->textureRowBytes;
			if (!Raster_DrawMaskedLongTexturedSpanSegment3(&segment3State, &segment3)) {
				return 0;
			}
			result->longSpanPixelsWritten += segment3.pixelsWritten;
		}
	}
	return 1;
}

int Raster_SetupLongTexturedSpan(const RasterTexturedLongSpanSetupState *state, RasterTexturedLongSpanSetup *result) {
	uint16_t spanWidth;
	uint16_t cumulative;
	int16_t remainingPixels;
	int32_t texUDelta;
	int32_t texVDelta;
	int32_t currentTexUEnd = 0;
	int32_t currentTexVEnd = 0;
	int32_t segmentDelta;

	if (state == NULL || result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->in = *state;
	result->tableOffset =
	    ((state->reciprocalBucket + RASTER_PERSPECTIVE_BUCKET_ROUND_BIAS) >> RASTER_PERSPECTIVE_ENTRY_BUCKET_SHIFT) *
	    sizeof(RasterPerspectiveEntry);
	result->startTexU = state->startTexU;
	result->startTexV = state->startTexV;
	result->endTexU = state->endTexU;
	result->endTexV = state->endTexV;

	if (!state->depthSwapped) {
		result->segmentCountCoeff0 = (uint16_t)state->split3PackedFractions;
		result->segmentCountCoeff1 = (uint16_t)state->split7PackedFractions;
		result->segmentCountCoeff2 = (uint16_t)state->split11PackedFractions;
		result->segmentInterpCoeff0 = (uint16_t)(state->split3PackedFractions >> 16);
		result->segmentInterpCoeff1 = (uint16_t)(state->split7PackedFractions >> 16);
		result->segmentInterpCoeff2 = (uint16_t)(state->split11PackedFractions >> 16);
	} else {
		result->segmentCountCoeff0 = (uint16_t)(state->split3PackedFractions >> 16);
		result->segmentCountCoeff1 = (uint16_t)(state->split7PackedFractions >> 16);
		result->segmentCountCoeff2 = (uint16_t)(state->split11PackedFractions >> 16);
		result->segmentInterpCoeff0 = (uint16_t)state->split3PackedFractions;
		result->segmentInterpCoeff1 = (uint16_t)state->split7PackedFractions;
		result->segmentInterpCoeff2 = (uint16_t)state->split11PackedFractions;
	}

	spanWidth = (uint16_t)state->spanWidth;
	result->segment0PixelCount = Raster_MultiplyUnsigned16High(result->segmentCountCoeff0, spanWidth);
	cumulative = result->segment0PixelCount;
	result->segment1PixelCount =
	    (uint16_t)(Raster_MultiplyUnsigned16High(result->segmentCountCoeff1, spanWidth) - cumulative);
	cumulative = (uint16_t)(cumulative + result->segment1PixelCount);
	result->segment2PixelCount =
	    (uint16_t)(Raster_MultiplyUnsigned16High(result->segmentCountCoeff2, spanWidth) - cumulative);
	cumulative = (uint16_t)(cumulative + result->segment2PixelCount);
	remainingPixels = (int16_t)(spanWidth - cumulative);
	result->segment3PixelCount = remainingPixels < 0 ? 0u : (uint16_t)remainingPixels;

	texUDelta = (int32_t)(state->endTexU - state->startTexU);
	texVDelta = (int32_t)(state->endTexV - state->startTexV);
	result->texUDelta = texUDelta;
	result->texVDelta = texVDelta;
	result->remainingTexUAfterSegment = texUDelta;
	result->remainingTexVAfterSegment = texVDelta;

	result->segment0Nonzero = result->segment0PixelCount != 0u;
	if (result->segment0Nonzero) {
		result->segment0TexUEnd = Raster_MultiplySignedShift16(result->segmentInterpCoeff0, texUDelta);
		currentTexUEnd = result->segment0TexUEnd;
		result->segment0TexUStep = result->segment0TexUEnd / (int32_t)result->segment0PixelCount;
		result->segment0TexVEnd = Raster_MultiplySignedShift16(result->segmentInterpCoeff0, texVDelta);
		currentTexVEnd = result->segment0TexVEnd;
		result->segment0TexVStep = result->segment0TexVEnd / (int32_t)result->segment0PixelCount;
	}

	result->segment1Nonzero = result->segment1PixelCount != 0u;
	if (result->segment1Nonzero) {
		result->segment1TexUEnd = Raster_MultiplySignedShift16(result->segmentInterpCoeff1, texUDelta);
		segmentDelta = result->segment1TexUEnd - currentTexUEnd;
		currentTexUEnd = result->segment1TexUEnd;
		result->segment1TexUStep = segmentDelta / (int32_t)result->segment1PixelCount;
		result->segment1TexVEnd = Raster_MultiplySignedShift16(result->segmentInterpCoeff1, texVDelta);
		segmentDelta = result->segment1TexVEnd - currentTexVEnd;
		currentTexVEnd = result->segment1TexVEnd;
		result->segment1TexVStep = segmentDelta / (int32_t)result->segment1PixelCount;
	}

	result->segment2Nonzero = result->segment2PixelCount != 0u;
	if (result->segment2Nonzero) {
		const int32_t segment2TexUEnd = Raster_MultiplySignedShift16(result->segmentInterpCoeff2, texUDelta);
		const int32_t segment2TexVEnd = Raster_MultiplySignedShift16(result->segmentInterpCoeff2, texVDelta);

		result->remainingTexUAfterSegment = texUDelta - segment2TexUEnd;
		segmentDelta = segment2TexUEnd - currentTexUEnd;
		currentTexUEnd = segment2TexUEnd;
		result->segment2TexUStep = segmentDelta / (int32_t)result->segment2PixelCount;
		result->remainingTexVAfterSegment = texVDelta - segment2TexVEnd;
		segmentDelta = segment2TexVEnd - currentTexVEnd;
		currentTexVEnd = segment2TexVEnd;
		result->segment2TexVStep = segmentDelta / (int32_t)result->segment2PixelCount;
	}

	result->segment3Nonzero = result->segment3PixelCount != 0u;
	if (result->segment3Nonzero) {
		result->segment3TexUStep = result->remainingTexUAfterSegment / (int32_t)result->segment3PixelCount;
		result->segment3TexVStep = result->remainingTexVAfterSegment / (int32_t)result->segment3PixelCount;
	}
	return 1;
}

int Raster_DrawOpaqueLongTexturedSpanSegment0(const RasterTexturedLongSpanSegment0State *state,
                                              RasterTexturedLongSpanSegment0 *result) {
	uint32_t currentTexU;
	uint32_t currentTexV;
	uint32_t remainingForAlignment;
	size_t i;

	if (state == NULL || result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->in = *state;
	result->loadedTexUStep = state->texUStep;
	result->transparentWord = state->transparentWord;
	result->maskedJump = state->transparentWord != SLIP_SPRITE_NO_TRANSPARENT_COLOUR;
	if (result->maskedJump) {
		result->currentTexU = state->currentTexU;
		result->currentTexV = state->currentTexV;
		return 1;
	}

	result->segmentPixels = state->segment0PixelCount;
	result->zeroCountJump = state->segment0PixelCount == 0u;
	if (result->zeroCountJump) {
		result->currentTexU = state->currentTexU;
		result->currentTexV = state->currentTexV;
		return 1;
	}
	if (state->destination == NULL || state->destinationBytes < state->segment0PixelCount) {
		return 0;
	}

	result->loadedTexVStep = state->texVStep;
	result->pushedDestination = (uintptr_t)state->destination;
	remainingForAlignment = state->segment0PixelCount;
	result->oddBytePrologue = ((uintptr_t)state->destination & 1u) != 0u;
	if (result->oddBytePrologue) {
		--remainingForAlignment;
	}
	result->wordAlignPrologue = (((uintptr_t)state->destination + (result->oddBytePrologue ? 1u : 0u)) & 2u) != 0u &&
	                            remainingForAlignment > 1u;
	if (result->wordAlignPrologue) {
		remainingForAlignment -= 2u;
	}
	result->dwordLoopCount = remainingForAlignment / RASTER_COPY_DWORD_BYTES;
	result->wordTail = (remainingForAlignment & 2u) != 0u;
	result->byteTail = (remainingForAlignment & 1u) != 0u;

	currentTexU = state->currentTexU;
	currentTexV = state->currentTexV;
	for (i = 0; i < state->segment0PixelCount; ++i) {
		uint8_t sample;

		if (!Raster_SampleTextureByte(state->textureRows, state->textureRowCount, state->textureRowBytes, currentTexU,
		                              currentTexV, &sample)) {
			return 0;
		}
		currentTexU += (uint32_t)state->texUStep;
		currentTexV += (uint32_t)state->texVStep;
		state->destination[i] = sample;
		++result->pixelsWritten;
	}
	result->destinationAfterAdd = (uintptr_t)(state->destination + state->segment0PixelCount);
	result->currentTexU = currentTexU;
	result->currentTexV = currentTexV;
	return 1;
}

int Raster_DrawOpaqueLongTexturedSpanSegment1(const RasterTexturedLongSpanSegment1State *state,
                                              RasterTexturedLongSpanSegment1 *result) {
	uint32_t currentTexU;
	uint32_t currentTexV;
	uint32_t remainingForAlignment;
	size_t i;

	if (state == NULL || result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->in = *state;
	result->segmentPixels = state->segment1PixelCount;
	result->zeroCountJump = state->segment1PixelCount == 0u;
	if (result->zeroCountJump) {
		result->currentTexU = state->currentTexU;
		result->currentTexV = state->currentTexV;
		return 1;
	}
	if (state->destination == NULL || state->destinationBytes < state->segment1PixelCount) {
		return 0;
	}

	result->loadedTexUStep = state->texUStep;
	result->loadedTexVStep = state->texVStep;
	result->pushedDestination = (uintptr_t)state->destination;
	remainingForAlignment = state->segment1PixelCount;
	result->oddBytePrologue = ((uintptr_t)state->destination & 1u) != 0u;
	if (result->oddBytePrologue) {
		--remainingForAlignment;
	}
	result->wordAlignPrologue = (((uintptr_t)state->destination + (result->oddBytePrologue ? 1u : 0u)) & 2u) != 0u &&
	                            remainingForAlignment > 1u;
	if (result->wordAlignPrologue) {
		remainingForAlignment -= 2u;
	}
	result->dwordLoopCount = remainingForAlignment / RASTER_COPY_DWORD_BYTES;
	result->wordTail = (remainingForAlignment & 2u) != 0u;
	result->byteTail = (remainingForAlignment & 1u) != 0u;

	currentTexU = state->currentTexU;
	currentTexV = state->currentTexV;
	for (i = 0; i < state->segment1PixelCount; ++i) {
		uint8_t sample;

		if (!Raster_SampleTextureByte(state->textureRows, state->textureRowCount, state->textureRowBytes, currentTexU,
		                              currentTexV, &sample)) {
			return 0;
		}
		currentTexU += (uint32_t)state->texUStep;
		currentTexV += (uint32_t)state->texVStep;
		state->destination[i] = sample;
		++result->pixelsWritten;
	}
	result->destinationAfterAdd = (uintptr_t)(state->destination + state->segment1PixelCount);
	result->currentTexU = currentTexU;
	result->currentTexV = currentTexV;
	return 1;
}

int Raster_DrawOpaqueLongTexturedSpanSegment2(const RasterTexturedLongSpanSegment2State *state,
                                              RasterTexturedLongSpanSegment2 *result) {
	uint32_t currentTexU;
	uint32_t currentTexV;
	uint32_t remainingForAlignment;
	size_t i;

	if (state == NULL || result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->in = *state;
	result->segmentPixels = state->segment2PixelCount;
	result->zeroCountJump = state->segment2PixelCount == 0u;
	if (result->zeroCountJump) {
		result->currentTexU = state->currentTexU;
		result->currentTexV = state->currentTexV;
		return 1;
	}
	if (state->destination == NULL || state->destinationBytes < state->segment2PixelCount) {
		return 0;
	}

	result->loadedTexUStep = state->texUStep;
	result->loadedTexVStep = state->texVStep;
	result->pushedDestination = (uintptr_t)state->destination;
	remainingForAlignment = state->segment2PixelCount;
	result->oddBytePrologue = ((uintptr_t)state->destination & 1u) != 0u;
	if (result->oddBytePrologue) {
		--remainingForAlignment;
	}
	result->wordAlignPrologue = (((uintptr_t)state->destination + (result->oddBytePrologue ? 1u : 0u)) & 2u) != 0u &&
	                            remainingForAlignment > 1u;
	if (result->wordAlignPrologue) {
		remainingForAlignment -= 2u;
	}
	result->dwordLoopCount = remainingForAlignment / RASTER_COPY_DWORD_BYTES;
	result->wordTail = (remainingForAlignment & 2u) != 0u;
	result->byteTail = (remainingForAlignment & 1u) != 0u;

	currentTexU = state->currentTexU;
	currentTexV = state->currentTexV;
	for (i = 0; i < state->segment2PixelCount; ++i) {
		uint8_t sample;

		if (!Raster_SampleTextureByte(state->textureRows, state->textureRowCount, state->textureRowBytes, currentTexU,
		                              currentTexV, &sample)) {
			return 0;
		}
		currentTexU += (uint32_t)state->texUStep;
		currentTexV += (uint32_t)state->texVStep;
		state->destination[i] = sample;
		++result->pixelsWritten;
	}
	result->destinationAfterAdd = (uintptr_t)(state->destination + state->segment2PixelCount);
	result->currentTexU = currentTexU;
	result->currentTexV = currentTexV;
	return 1;
}

int Raster_DrawOpaqueLongTexturedSpanSegment3(const RasterTexturedLongSpanSegment3State *state,
                                              RasterTexturedLongSpanSegment3 *result) {
	uint32_t currentTexU;
	uint32_t currentTexV;
	uint32_t remainingForAlignment;
	size_t i;

	if (state == NULL || result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->in = *state;
	result->segmentPixels = state->segment3PixelCount;
	result->zeroCountReturn = state->segment3PixelCount == 0u;
	if (result->zeroCountReturn) {
		result->currentTexU = state->currentTexU;
		result->currentTexV = state->currentTexV;
		result->ret = true;
		return 1;
	}
	if (state->destination == NULL || state->destinationBytes < state->segment3PixelCount) {
		return 0;
	}

	result->loadedTexUStep = state->texUStep;
	result->loadedTexVStep = state->texVStep;
	result->pushedDestination = (uintptr_t)state->destination;
	remainingForAlignment = state->segment3PixelCount;
	result->oddBytePrologue = ((uintptr_t)state->destination & 1u) != 0u;
	if (result->oddBytePrologue) {
		--remainingForAlignment;
	}
	result->wordAlignPrologue = (((uintptr_t)state->destination + (result->oddBytePrologue ? 1u : 0u)) & 2u) != 0u &&
	                            remainingForAlignment > 1u;
	if (result->wordAlignPrologue) {
		remainingForAlignment -= 2u;
	}
	result->dwordLoopCount = remainingForAlignment / RASTER_COPY_DWORD_BYTES;
	result->wordTail = (remainingForAlignment & 2u) != 0u;
	result->byteTail = (remainingForAlignment & 1u) != 0u;

	currentTexU = state->currentTexU;
	currentTexV = state->currentTexV;
	for (i = 0; i < state->segment3PixelCount; ++i) {
		uint8_t sample;

		if (!Raster_SampleTextureByte(state->textureRows, state->textureRowCount, state->textureRowBytes, currentTexU,
		                              currentTexV, &sample)) {
			return 0;
		}
		currentTexU += (uint32_t)state->texUStep;
		currentTexV += (uint32_t)state->texVStep;
		state->destination[i] = sample;
		++result->pixelsWritten;
	}
	result->destinationAfterAdd = (uintptr_t)(state->destination + state->segment3PixelCount);
	result->currentTexU = currentTexU;
	result->currentTexV = currentTexV;
	result->ret = true;
	return 1;
}

int Raster_DrawMaskedLongTexturedSpanSegment0(const RasterTexturedMaskedLongSpanSegment0State *state,
                                              RasterTexturedMaskedLongSpanSegment0 *result) {
	uint32_t currentTexU;
	uint32_t currentTexV;
	size_t i;

	if (state == NULL || result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->in = *state;
	result->clearTextureOffsetRegister = true;
	result->segmentPixels = state->segment0PixelCount;
	result->zeroCountJump = state->segment0PixelCount == 0u;
	if (result->zeroCountJump) {
		result->currentTexU = state->currentTexU;
		result->currentTexV = state->currentTexV;
		result->destinationAfterLoop = (uintptr_t)state->destination;
		return 1;
	}
	if (state->destination == NULL || state->destinationBytes < state->segment0PixelCount) {
		return 0;
	}

	result->loadedTexUStep = state->texUStep;
	result->loadedTexVStep = state->texVStep;
	currentTexU = state->currentTexU;
	currentTexV = state->currentTexV;
	for (i = 0; i < state->segment0PixelCount; ++i) {
		uint8_t sample;

		if (!Raster_SampleTextureByte(state->textureRows, state->textureRowCount, state->textureRowBytes, currentTexU,
		                              currentTexV, &sample)) {
			return 0;
		}
		++result->pixelsTested;
		if (sample == state->transparentByteDl) {
			++result->pixelsSkipped;
		} else {
			state->destination[i] = sample;
			++result->pixelsWritten;
		}
		currentTexU += (uint32_t)state->texUStep;
		currentTexV += (uint32_t)state->texVStep;
	}
	result->currentTexU = currentTexU;
	result->currentTexV = currentTexV;
	result->destinationAfterLoop = (uintptr_t)(state->destination + state->segment0PixelCount);
	return 1;
}

int Raster_DrawMaskedLongTexturedSpanSegment1(const RasterTexturedMaskedLongSpanSegment1State *state,
                                              RasterTexturedMaskedLongSpanSegment1 *result) {
	uint32_t currentTexU;
	uint32_t currentTexV;
	size_t i;

	if (state == NULL || result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->in = *state;
	result->segmentPixels = state->segment1PixelCount;
	result->zeroCountJump = state->segment1PixelCount == 0u;
	if (result->zeroCountJump) {
		result->currentTexU = state->currentTexU;
		result->currentTexV = state->currentTexV;
		result->destinationAfterLoop = (uintptr_t)state->destination;
		return 1;
	}
	if (state->destination == NULL || state->destinationBytes < state->segment1PixelCount) {
		return 0;
	}

	result->loadedTexUStep = state->texUStep;
	result->loadedTexVStep = state->texVStep;
	currentTexU = state->currentTexU;
	currentTexV = state->currentTexV;
	for (i = 0; i < state->segment1PixelCount; ++i) {
		uint8_t sample;

		if (!Raster_SampleTextureByte(state->textureRows, state->textureRowCount, state->textureRowBytes, currentTexU,
		                              currentTexV, &sample)) {
			return 0;
		}
		++result->pixelsTested;
		if (sample == state->transparentByteDl) {
			++result->pixelsSkipped;
		} else {
			state->destination[i] = sample;
			++result->pixelsWritten;
		}
		currentTexU += (uint32_t)state->texUStep;
		currentTexV += (uint32_t)state->texVStep;
	}
	result->currentTexU = currentTexU;
	result->currentTexV = currentTexV;
	result->destinationAfterLoop = (uintptr_t)(state->destination + state->segment1PixelCount);
	return 1;
}

int Raster_DrawMaskedLongTexturedSpanSegment2(const RasterTexturedMaskedLongSpanSegment2State *state,
                                              RasterTexturedMaskedLongSpanSegment2 *result) {
	uint32_t currentTexU;
	uint32_t currentTexV;
	size_t i;

	if (state == NULL || result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->in = *state;
	result->segmentPixels = state->segment2PixelCount;
	result->zeroCountJump = state->segment2PixelCount == 0u;
	if (result->zeroCountJump) {
		result->currentTexU = state->currentTexU;
		result->currentTexV = state->currentTexV;
		result->destinationAfterLoop = (uintptr_t)state->destination;
		return 1;
	}
	if (state->destination == NULL || state->destinationBytes < state->segment2PixelCount) {
		return 0;
	}

	result->loadedTexUStep = state->texUStep;
	result->loadedTexVStep = state->texVStep;
	currentTexU = state->currentTexU;
	currentTexV = state->currentTexV;
	for (i = 0; i < state->segment2PixelCount; ++i) {
		uint8_t sample;

		if (!Raster_SampleTextureByte(state->textureRows, state->textureRowCount, state->textureRowBytes, currentTexU,
		                              currentTexV, &sample)) {
			return 0;
		}
		++result->pixelsTested;
		if (sample == state->transparentByteDl) {
			++result->pixelsSkipped;
		} else {
			state->destination[i] = sample;
			++result->pixelsWritten;
		}
		currentTexU += (uint32_t)state->texUStep;
		currentTexV += (uint32_t)state->texVStep;
	}
	result->currentTexU = currentTexU;
	result->currentTexV = currentTexV;
	result->destinationAfterLoop = (uintptr_t)(state->destination + state->segment2PixelCount);
	return 1;
}

int Raster_DrawMaskedLongTexturedSpanSegment3(const RasterTexturedMaskedLongSpanSegment3State *state,
                                              RasterTexturedMaskedLongSpanSegment3 *result) {
	uint32_t currentTexU;
	uint32_t currentTexV;
	size_t i;

	if (state == NULL || result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->in = *state;
	result->segmentPixels = state->segment3PixelCount;
	result->zeroCountReturn = state->segment3PixelCount == 0u;
	if (result->zeroCountReturn) {
		result->currentTexU = state->currentTexU;
		result->currentTexV = state->currentTexV;
		result->destinationAfterLoop = (uintptr_t)state->destination;
		result->ret = true;
		return 1;
	}
	if (state->destination == NULL || state->destinationBytes < state->segment3PixelCount) {
		return 0;
	}

	result->loadedTexUStep = state->texUStep;
	result->loadedTexVStep = state->texVStep;
	currentTexU = state->currentTexU;
	currentTexV = state->currentTexV;
	for (i = 0; i < state->segment3PixelCount; ++i) {
		uint8_t sample;

		if (!Raster_SampleTextureByte(state->textureRows, state->textureRowCount, state->textureRowBytes, currentTexU,
		                              currentTexV, &sample)) {
			return 0;
		}
		++result->pixelsTested;
		if (sample == state->transparentByteDl) {
			++result->pixelsSkipped;
		} else {
			state->destination[i] = sample;
			++result->pixelsWritten;
		}
		currentTexU += (uint32_t)state->texUStep;
		currentTexV += (uint32_t)state->texVStep;
	}
	result->currentTexU = currentTexU;
	result->currentTexV = currentTexV;
	result->destinationAfterLoop = (uintptr_t)(state->destination + state->segment3PixelCount);
	result->ret = true;
	return 1;
}

static int Raster_PointAbsOffsetValid(uint32_t pointBufferBase, size_t pointBufferBytes, uint32_t pointOffset) {
	return pointOffset >= pointBufferBase &&
	       (size_t)(pointOffset - pointBufferBase) + sizeof(RasterTexturedPoint) <= pointBufferBytes;
}

static int Raster_DispatchPerspectiveTexturedSpanRows(
    uint8_t *const *screenRows, size_t screenRowCount, size_t screenRowBytes, const uint8_t *lockedPayload,
    size_t lockedPayloadBytes, const uint8_t *const *textureRows, size_t textureRowCount, size_t textureRowBytes,
    int32_t scanY, int32_t leftX, int32_t rightX, uint32_t leftTexU, uint32_t leftTexV, uint32_t rightTexU,
    uint32_t rightTexV, uint32_t leftDepth, uint32_t rightDepth, RasterTexturedSpanDispatch *dispatch);

static int Raster_DispatchPerspectiveTexturedSpan(const RasterMaskedPerspectiveTexturedPolygonState *state,
                                                  int32_t scanY, int32_t leftX, int32_t rightX, uint32_t leftTexU,
                                                  uint32_t leftTexV, uint32_t rightTexU, uint32_t rightTexV,
                                                  uint32_t leftDepth, uint32_t rightDepth,
                                                  RasterTexturedSpanDispatch *dispatch) {
	if (state == NULL || dispatch == NULL || state->lockedPayload == NULL) {
		return 0;
	}
	return Raster_DispatchPerspectiveTexturedSpanRows(
	    state->screenRows, state->screenRowCount, state->screenRowBytes, state->lockedPayload,
	    state->lockedPayloadBytes, state->textureRows, state->textureRowCount, state->textureRowBytes, scanY, leftX,
	    rightX, leftTexU, leftTexV, rightTexU, rightTexV, leftDepth, rightDepth, dispatch);
}

static int Raster_DispatchPerspectiveTexturedSpanRows(
    uint8_t *const *screenRows, size_t screenRowCount, size_t screenRowBytes, const uint8_t *lockedPayload,
    size_t lockedPayloadBytes, const uint8_t *const *textureRows, size_t textureRowCount, size_t textureRowBytes,
    int32_t scanY, int32_t leftX, int32_t rightX, uint32_t leftTexU, uint32_t leftTexV, uint32_t rightTexU,
    uint32_t rightTexV, uint32_t leftDepth, uint32_t rightDepth, RasterTexturedSpanDispatch *dispatch) {
	RasterTexturedSpanDispatchState dispatchState;

	if (dispatch == NULL || lockedPayload == NULL || lockedPayloadBytes < SLIP_SPRITE_TRANSPARENT_COLOUR_END ||
	    scanY < 0 || (size_t)scanY >= screenRowCount || screenRows == NULL || screenRows[scanY] == NULL) {
		return 0;
	}
	memset(&dispatchState, 0, sizeof(dispatchState));
	dispatchState.screenRow = screenRows[scanY];
	dispatchState.screenRowBytes = screenRowBytes;
	dispatchState.scanlineIndex = (uint32_t)scanY;
	dispatchState.spanLeftX = leftX;
	dispatchState.spanRightX = rightX;
	dispatchState.endpointBase = RASTER_TEXTURE_SPAN_ENDPOINT_BASE_TOKEN;
	dispatchState.startTexU = leftTexU;
	dispatchState.startTexV = leftTexV;
	dispatchState.endTexU = rightTexU;
	dispatchState.endTexV = rightTexV;
	dispatchState.startDepth = leftDepth;
	dispatchState.endDepth = rightDepth;
	dispatchState.transparentWord = SlipBytes_ReadLE16(lockedPayload + SLIP_SPRITE_TRANSPARENT_COLOUR_OFFSET);
	dispatchState.textureRows = textureRows;
	dispatchState.textureRowCount = textureRowCount;
	dispatchState.textureRowBytes = textureRowBytes;
	return Raster_DispatchTexturedSpan(&dispatchState, dispatch);
}

static int Raster_SetupAndDispatchPerspectiveSpan(
    const RasterMaskedPerspectiveTexturedPolygonState *state, const RasterTexturedAdvanceEdgesState *edgeState,
    uint32_t leftPointOffset, uint32_t rightPointOffset, uint32_t leftBaseDepth, uint32_t leftBaseTexU,
    uint32_t leftBaseTexV, uint32_t rightBaseDepth, uint32_t rightBaseTexU, uint32_t rightBaseTexV,
    uint32_t *spanLeftTexU, uint32_t *spanLeftTexV, uint32_t *spanLeftDepth, uint32_t *spanRightTexU,
    uint32_t *spanRightTexV, uint32_t *spanRightDepth, RasterMaskedPerspectiveTexturedPolygonVisit *visit) {
	const uint8_t *leftPoint;
	const uint8_t *rightPoint;
	RasterTexturedSpanSetupState spanState;

	if (state == NULL || edgeState == NULL || visit == NULL || spanLeftTexU == NULL || spanLeftTexV == NULL ||
	    spanLeftDepth == NULL || spanRightTexU == NULL || spanRightTexV == NULL || spanRightDepth == NULL ||
	    edgeState->scanY < 0 || (size_t)edgeState->scanY + 1u >= state->screenRowCount ||
	    !Raster_PointAbsOffsetValid(state->pointBufferBase, state->pointBufferBytes, leftPointOffset) ||
	    !Raster_PointAbsOffsetValid(state->pointBufferBase, state->pointBufferBytes, rightPointOffset)) {
		return 0;
	}
	leftPoint = state->pointBuffer + (leftPointOffset - state->pointBufferBase);
	rightPoint = state->pointBuffer + (rightPointOffset - state->pointBufferBase);
	memset(&spanState, 0, sizeof(spanState));
	spanState.spanLeftX = edgeState->leftX;
	spanState.spanRightX = edgeState->rightX;
	spanState.scanlineIndex = (uint32_t)edgeState->scanY;
	spanState.texturePayload = (uintptr_t)state->lockedPayload;
	spanState.leftAccumulatedBaseDepthPerRow = edgeState->leftAccumulatedBaseDepthPerRow;
	spanState.leftAccumulatedNextPointDepthPerRow = edgeState->leftAccumulatedNextPointDepthPerRow;
	spanState.leftBaseDepth = leftBaseDepth;
	spanState.leftBaseTexU = leftBaseTexU;
	spanState.leftBaseTexV = leftBaseTexV;
	spanState.rightAccumulatedBaseDepthPerRow = edgeState->rightAccumulatedBaseDepthPerRow;
	spanState.rightAccumulatedNextPointDepthPerRow = edgeState->rightAccumulatedNextPointDepthPerRow;
	spanState.rightBaseDepth = rightBaseDepth;
	spanState.rightBaseTexU = rightBaseTexU;
	spanState.rightBaseTexV = rightBaseTexV;
	spanState.leftPointDepth = SlipBytes_ReadLE32(leftPoint + offsetof(RasterTexturedPoint, depth));
	spanState.leftPointTexU = SlipBytes_ReadLE32(leftPoint + offsetof(RasterTexturedPoint, scaledU));
	spanState.leftPointTexV = SlipBytes_ReadLE32(leftPoint + offsetof(RasterTexturedPoint, scaledV));
	spanState.rightPointDepth = SlipBytes_ReadLE32(rightPoint + offsetof(RasterTexturedPoint, depth));
	spanState.rightPointTexU = SlipBytes_ReadLE32(rightPoint + offsetof(RasterTexturedPoint, scaledU));
	spanState.rightPointTexV = SlipBytes_ReadLE32(rightPoint + offsetof(RasterTexturedPoint, scaledV));
	spanState.currentRowPointer = (uintptr_t)state->screenRows[edgeState->scanY];
	spanState.nextRowPointer = (uintptr_t)state->screenRows[edgeState->scanY + 1];

	visit->scanline = edgeState->scanY;
	visit->spanLeftX = edgeState->leftX;
	visit->spanRightX = edgeState->rightX;
	visit->leftPointOffset = leftPointOffset;
	visit->rightPointOffset = rightPointOffset;
	visit->calledSetupPerspectiveTexturedSpan = true;
	if (!Raster_SetupPerspectiveTexturedSpan(&spanState, &visit->spanSetup)) {
		return 0;
	}
	*spanLeftTexU = visit->spanSetup.spanLeftTexU;
	*spanLeftTexV = visit->spanSetup.spanLeftTexV;
	*spanLeftDepth = visit->spanSetup.spanLeftDepth;
	*spanRightTexU = visit->spanSetup.spanRightTexU;
	*spanRightTexV = visit->spanSetup.spanRightTexV;
	*spanRightDepth = visit->spanSetup.spanRightDepth;

	visit->calledDispatchTexturedSpan = true;
	if (!Raster_DispatchPerspectiveTexturedSpan(state, edgeState->scanY, edgeState->leftX, edgeState->rightX,
	                                            *spanLeftTexU, *spanLeftTexV, *spanRightTexU, *spanRightTexV,
	                                            *spanLeftDepth, *spanRightDepth, &visit->spanDispatch)) {
		return 0;
	}
	visit->longSpanBranch = visit->spanDispatch.longSpanBranch;
	visit->pixelsWritten = visit->spanDispatch.jumpShortSpan ? visit->spanDispatch.shortSpanCore.pixelsWritten
	                                                         : visit->spanDispatch.longSpanPixelsWritten;
	return 1;
}

int Raster_DrawMaskedPerspectiveTexturedPolygon(const RasterMaskedPerspectiveTexturedPolygonState *state,
                                                RasterMaskedPerspectiveTexturedPolygonVisit *visits,
                                                size_t visitCapacity, RasterMaskedPerspectiveTexturedPolygon *result) {
	RasterTexturedAdvanceEdgesState edgeState;
	RasterTexturedLeftEdgeStep leftEdge;
	RasterTexturedRightEdgeStep rightEdge;
	RasterTextureUVExtentsVisit uvVisits[RASTER_TEXTURE_UV_VISIT_CAPACITY];
	RasterTextureUVExtents uvExtents;
	uint32_t pointBufferEnd;
	uint32_t topLeftOffset;
	uint32_t topRightOffset;
	int32_t topY;
	int32_t bottomY;
	uint32_t spanLeftTexU = 0;
	uint32_t spanLeftTexV = 0;
	uint32_t spanLeftDepth = 0;
	uint32_t spanRightTexU = 0;
	uint32_t spanRightTexV = 0;
	uint32_t spanRightDepth = 0;
	uint32_t leftBaseDepth;
	uint32_t leftBaseTexU;
	uint32_t leftBaseTexV;
	uint32_t rightBaseDepth;
	uint32_t rightBaseTexU;
	uint32_t rightBaseTexV;
	uint32_t leftPointOffset;
	uint32_t rightPointOffset;
	size_t visitCount = 0;
	uint32_t i;

	if (state == NULL || result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->in = *state;
	result->pushad = true;
	result->calledPrepareTextureUVExtents = true;
	result->calledBuildTextureRowTable = true;
	result->pointBufferBase = state->pointBufferBase;
	result->inputPointCount = state->pointCount;
	if (state->pointBuffer == NULL || state->pointCount == 0u ||
	    state->pointBufferBytes < (size_t)state->pointCount * sizeof(RasterTexturedPoint) || visits == NULL ||
	    visitCapacity == 0u) {
		return 0;
	}
	if (state->pointCount > sizeof(uvVisits) / sizeof(uvVisits[0]) ||
	    !Raster_PrepareTextureUVExtents(state->lockedPayload, state->lockedPayloadBytes, state->pointBuffer,
	                                    state->pointBufferBytes, state->pointCount, uvVisits,
	                                    sizeof(uvVisits) / sizeof(uvVisits[0]), &uvExtents)) {
		return 0;
	}
	pointBufferEnd = state->pointBufferBase + state->pointCount * (uint32_t)sizeof(RasterTexturedPoint);
	result->pointBufferEnd = pointBufferEnd;

	topY = SlipBytes_ReadLEI32(state->pointBuffer + offsetof(RasterPoint, y));
	bottomY = topY;
	topLeftOffset = state->pointBufferBase;
	topRightOffset = state->pointBufferBase;
	result->topY = topY;
	result->bottomY = bottomY;
	for (i = 1u; i < state->pointCount; ++i) {
		const uint32_t pointOffset = state->pointBufferBase + i * (uint32_t)sizeof(RasterTexturedPoint);
		const uint8_t *const point = state->pointBuffer + i * (uint32_t)sizeof(RasterTexturedPoint);
		const int32_t pointX = SlipBytes_ReadLEI32(point);
		const int32_t pointY = SlipBytes_ReadLEI32(point + offsetof(RasterPoint, y));

		if (bottomY < pointY) {
			bottomY = pointY;
		}
		if (topY >= pointY) {
			if (topY == pointY) {
				const uint8_t *const leftPoint = state->pointBuffer + (topLeftOffset - state->pointBufferBase);
				const uint8_t *const rightPoint = state->pointBuffer + (topRightOffset - state->pointBufferBase);

				if (pointX < SlipBytes_ReadLEI32(leftPoint)) {
					topLeftOffset = pointOffset;
				}
				if (pointX > SlipBytes_ReadLEI32(rightPoint)) {
					topRightOffset = pointOffset;
				}
			} else {
				topY = pointY;
				topLeftOffset = pointOffset;
				topRightOffset = pointOffset;
			}
		}
		++result->scanVisitCount;
	}
	result->topY = topY;
	result->bottomY = bottomY;
	result->topLeftPointOffset = topLeftOffset;
	result->topRightPointOffset = topRightOffset;
	result->horizontalBranch = bottomY == topY;
	if (result->horizontalBranch) {
		const uint8_t *const leftPoint = state->pointBuffer + (topLeftOffset - state->pointBufferBase);
		const uint8_t *const rightPoint = state->pointBuffer + (topRightOffset - state->pointBufferBase);

		visits[0].scanline = topY;
		visits[0].spanLeftX = SlipBytes_ReadLEI32(leftPoint);
		visits[0].spanRightX = SlipBytes_ReadLEI32(rightPoint);
		visits[0].leftPointOffset = topLeftOffset;
		visits[0].rightPointOffset = topRightOffset;
		visits[0].calledDispatchTexturedSpan = true;
		if (!Raster_DispatchPerspectiveTexturedSpan(
		        state, topY, SlipBytes_ReadLEI32(leftPoint), SlipBytes_ReadLEI32(rightPoint),
		        SlipBytes_ReadLE32(leftPoint + offsetof(RasterTexturedPoint, scaledU)),
		        SlipBytes_ReadLE32(leftPoint + offsetof(RasterTexturedPoint, scaledV)),
		        SlipBytes_ReadLE32(rightPoint + offsetof(RasterTexturedPoint, scaledU)),
		        SlipBytes_ReadLE32(rightPoint + offsetof(RasterTexturedPoint, scaledV)), rasterSpanLeftDepth,
		        rasterSpanRightDepth, &visits[0].spanDispatch)) {
			return 0;
		}
		visits[0].longSpanBranch = visits[0].spanDispatch.longSpanBranch;
		visits[0].pixelsWritten = visits[0].spanDispatch.jumpShortSpan
		                              ? visits[0].spanDispatch.shortSpanCore.pixelsWritten
		                              : visits[0].spanDispatch.longSpanPixelsWritten;
		result->visitCount = 1u;
		result->spanDispatchCount = 1u;
		result->shortSpanCount = visits[0].spanDispatch.jumpShortSpan ? 1u : 0u;
		result->longSpanCount = visits[0].spanDispatch.longSpanBranch ? 1u : 0u;
		result->pixelsWritten = visits[0].pixelsWritten;
		result->popad = true;
		return 1;
	}

	result->storedBottomY = (uint32_t)bottomY;
	result->initialCalledStepLeftEdgeTexturedPerspective = true;
	if (!Raster_StepLeftEdgeTexturedPerspective(state->pointBuffer, state->pointBufferBase, pointBufferEnd,
	                                            state->pointBufferBytes, topLeftOffset, topY, bottomY, &leftEdge)) {
		return 0;
	}
	result->initialLeftEdge = leftEdge;
	if (leftEdge.carryOut) {
		result->popad = true;
		return 1;
	}
	result->initialCalledStepRightEdgeTexturedPerspective = true;
	if (!Raster_StepRightEdgeTexturedPerspective(state->pointBuffer, state->pointBufferBase, pointBufferEnd,
	                                             state->pointBufferBytes, topRightOffset, topY, bottomY, &rightEdge)) {
		return 0;
	}
	result->initialRightEdge = rightEdge;
	if (rightEdge.carryOut) {
		result->popad = true;
		return 1;
	}

	leftPointOffset = leftEdge.pointOffsetOut;
	rightPointOffset = rightEdge.pointOffsetOut;
	leftBaseDepth = leftEdge.leftBaseDepth;
	leftBaseTexU = leftEdge.leftBaseTexU;
	leftBaseTexV = leftEdge.leftBaseTexV;
	rightBaseDepth = rightEdge.rightBaseDepth;
	rightBaseTexU = rightEdge.rightBaseTexU;
	rightBaseTexV = rightEdge.rightBaseTexV;
	memset(&edgeState, 0, sizeof(edgeState));
	edgeState.scanY = topY;
	edgeState.leftX = leftEdge.currentX;
	edgeState.rightX = rightEdge.currentX;
	edgeState.leftAccumulatedBaseDepthPerRow = leftEdge.accumulatedBaseDepthPerRow;
	edgeState.leftBaseDepthPerRow = leftEdge.baseDepthPerRow;
	edgeState.leftAccumulatedNextPointDepthPerRow = leftEdge.accumulatedNextPointDepthPerRow;
	edgeState.leftNextPointDepthPerRow = leftEdge.nextPointDepthPerRow;
	edgeState.rightAccumulatedBaseDepthPerRow = rightEdge.accumulatedBaseDepthPerRow;
	edgeState.rightBaseDepthPerRow = rightEdge.baseDepthPerRow;
	edgeState.rightAccumulatedNextPointDepthPerRow = rightEdge.accumulatedNextPointDepthPerRow;
	edgeState.rightNextPointDepthPerRow = rightEdge.nextPointDepthPerRow;
	edgeState.leftXStep = leftEdge.xStep;
	edgeState.leftXFraction = leftEdge.xFraction;
	edgeState.leftRemaining = leftEdge.remaining;
	edgeState.rightXStep = rightEdge.xStep;
	edgeState.rightXFraction = rightEdge.xFraction;
	edgeState.rightRemaining = rightEdge.remaining;

	for (;;) {
		RasterMaskedPerspectiveTexturedPolygonVisit *visit;
		RasterTexturedAdvanceEdges advance;

		if (visitCount >= visitCapacity) {
			result->visitCount = visitCount;
			return 0;
		}
		visit = &visits[visitCount];
		memset(visit, 0, sizeof(*visit));
		if (!Raster_SetupAndDispatchPerspectiveSpan(state, &edgeState, leftPointOffset, rightPointOffset, leftBaseDepth,
		                                            leftBaseTexU, leftBaseTexV, rightBaseDepth, rightBaseTexU,
		                                            rightBaseTexV, &spanLeftTexU, &spanLeftTexV, &spanLeftDepth,
		                                            &spanRightTexU, &spanRightTexV, &spanRightDepth, visit)) {
			result->failVisitIndexValid = true;
			result->failVisitIndex = visitCount;
			result->visitCount = visitCount + 1u;
			return 0;
		}
		++visitCount;
		++result->spanDispatchCount;
		result->shortSpanCount += visit->spanDispatch.jumpShortSpan ? 1u : 0u;
		result->longSpanCount += visit->longSpanBranch ? 1u : 0u;
		result->pixelsWritten += visit->pixelsWritten;

		if (visitCount >= visitCapacity) {
			result->visitCount = visitCount;
			return 0;
		}
		visit = &visits[visitCount];
		memset(visit, 0, sizeof(*visit));
		visit->calledAdvancePerspectiveTexturedEdges = true;
		if (!Raster_AdvancePerspectiveTexturedEdges(&edgeState, 0, 0, &advance)) {
			result->failVisitIndexValid = true;
			result->failVisitIndex = visitCount;
			result->visitCount = visitCount + 1u;
			return 0;
		}
		visit->advance = advance;
		edgeState = advance.out;
		if (advance.calledStepLeftEdgeTexturedPerspective) {
			visit->calledStepLeftEdgeTexturedPerspective = true;
			if (!Raster_StepLeftEdgeTexturedPerspective(state->pointBuffer, state->pointBufferBase, pointBufferEnd,
			                                            state->pointBufferBytes, leftPointOffset, edgeState.scanY,
			                                            bottomY, &leftEdge)) {
				result->failVisitIndexValid = true;
				result->failVisitIndex = visitCount;
				result->visitCount = visitCount + 1u;
				return 0;
			}
			visit->leftEdge = leftEdge;
			if (leftEdge.carryOut) {
				++visitCount;
				break;
			}
			leftPointOffset = leftEdge.pointOffsetOut;
			edgeState.leftX = leftEdge.currentX;
			edgeState.leftAccumulatedBaseDepthPerRow = leftEdge.accumulatedBaseDepthPerRow;
			edgeState.leftBaseDepthPerRow = leftEdge.baseDepthPerRow;
			edgeState.leftAccumulatedNextPointDepthPerRow = leftEdge.accumulatedNextPointDepthPerRow;
			edgeState.leftNextPointDepthPerRow = leftEdge.nextPointDepthPerRow;
			edgeState.leftXStep = leftEdge.xStep;
			edgeState.leftXFraction = leftEdge.xFraction;
			edgeState.leftRemaining = leftEdge.remaining;
			leftBaseDepth = leftEdge.leftBaseDepth;
			leftBaseTexU = leftEdge.leftBaseTexU;
			leftBaseTexV = leftEdge.leftBaseTexV;
		}
		if (advance.calledStepRightEdgeTexturedPerspective) {
			visit->calledStepRightEdgeTexturedPerspective = true;
			if (!Raster_StepRightEdgeTexturedPerspective(state->pointBuffer, state->pointBufferBase, pointBufferEnd,
			                                             state->pointBufferBytes, rightPointOffset, edgeState.scanY,
			                                             bottomY, &rightEdge)) {
				result->failVisitIndexValid = true;
				result->failVisitIndex = visitCount;
				result->visitCount = visitCount + 1u;
				return 0;
			}
			visit->rightEdge = rightEdge;
			if (rightEdge.carryOut) {
				++visitCount;
				break;
			}
			rightPointOffset = rightEdge.pointOffsetOut;
			edgeState.rightX = rightEdge.currentX;
			edgeState.rightAccumulatedBaseDepthPerRow = rightEdge.accumulatedBaseDepthPerRow;
			edgeState.rightBaseDepthPerRow = rightEdge.baseDepthPerRow;
			edgeState.rightAccumulatedNextPointDepthPerRow = rightEdge.accumulatedNextPointDepthPerRow;
			edgeState.rightNextPointDepthPerRow = rightEdge.nextPointDepthPerRow;
			edgeState.rightXStep = rightEdge.xStep;
			edgeState.rightXFraction = rightEdge.xFraction;
			edgeState.rightRemaining = rightEdge.remaining;
			rightBaseDepth = rightEdge.rightBaseDepth;
			rightBaseTexU = rightEdge.rightBaseTexU;
			rightBaseTexV = rightEdge.rightBaseTexV;
		}
		++visitCount;
		if (edgeState.scanY >= bottomY) {
			if (visitCount >= visitCapacity) {
				result->visitCount = visitCount;
				return 0;
			}
			visit = &visits[visitCount];
			memset(visit, 0, sizeof(*visit));
			visit->scanline = edgeState.scanY;
			visit->spanLeftX = edgeState.leftX;
			visit->spanRightX = edgeState.rightX;
			visit->leftPointOffset = leftPointOffset;
			visit->rightPointOffset = rightPointOffset;
			visit->calledDispatchTexturedSpan = true;
			if (!Raster_DispatchPerspectiveTexturedSpan(state, edgeState.scanY, edgeState.leftX, edgeState.rightX,
			                                            spanLeftTexU, spanLeftTexV, spanRightTexU, spanRightTexV,
			                                            spanLeftDepth, spanRightDepth, &visit->spanDispatch)) {
				result->failVisitIndexValid = true;
				result->failVisitIndex = visitCount;
				result->visitCount = visitCount + 1u;
				return 0;
			}
			visit->longSpanBranch = visit->spanDispatch.longSpanBranch;
			visit->pixelsWritten = visit->spanDispatch.jumpShortSpan ? visit->spanDispatch.shortSpanCore.pixelsWritten
			                                                         : visit->spanDispatch.longSpanPixelsWritten;
			++visitCount;
			++result->spanDispatchCount;
			result->shortSpanCount += visit->spanDispatch.jumpShortSpan ? 1u : 0u;
			result->longSpanCount += visit->longSpanBranch ? 1u : 0u;
			result->pixelsWritten += visit->pixelsWritten;
			break;
		}
	}
	result->visitCount = visitCount;
	result->popad = true;
	return 1;
}

static int32_t Raster_PredictTexturedXAfterFractionStep(int32_t currentX, int32_t fixedStep, uint16_t fraction) {
	const uint32_t fractionSum = (uint32_t)(uint16_t)fixedStep + (uint32_t)fraction;
	const int32_t integerStep = (int16_t)(uint16_t)((uint32_t)fixedStep >> RASTER_FRACTION_BITS);

	return currentX + integerStep + (int32_t)(fractionSum >> RASTER_FRACTION_BITS);
}

static int Raster_ForwardCopy(uint8_t *source, uint8_t *destination, int32_t byteCount,
                              RasterOpaquePerspectiveTexturedCopy *copy) {
	uint32_t count;
	uint32_t remainingForAlignment;
	uint32_t i;

	if (copy == NULL) {
		return 0;
	}
	memset(copy, 0, sizeof(*copy));
	copy->source = (uintptr_t)source;
	copy->destination = (uintptr_t)destination;
	copy->byteCount = byteCount;
	copy->byteCountPositive = byteCount > 0;
	if (byteCount <= 0) {
		return 1;
	}
	if (source == NULL || destination == NULL) {
		return 0;
	}

	count = (uint32_t)byteCount;
	remainingForAlignment = count;
	if (remainingForAlignment >= RASTER_COPY_MINIMUM_DWORD_BYTES) {
		uint32_t alignBytes = (uint32_t)((uintptr_t)source & RASTER_COPY_ALIGNMENT_MASK);

		if (alignBytes != 0u) {
			alignBytes = (alignBytes ^ RASTER_COPY_ALIGNMENT_MASK) + 1u;
			if (alignBytes > remainingForAlignment) {
				return 0;
			}
			remainingForAlignment -= alignBytes;
			copy->alignByteCount = alignBytes;
		}
		copy->dwordCount = remainingForAlignment / RASTER_COPY_DWORD_BYTES;
		copy->tailByteCount = remainingForAlignment & RASTER_COPY_ALIGNMENT_MASK;
	} else {
		copy->tailByteCount = remainingForAlignment;
	}

	for (i = 0; i < count; ++i) {
		destination[i] = source[i];
	}
	return 1;
}

static int Raster_DrawAffineSpanCoreAt(const RasterOpaqueAffineTexturedPolygonState *state, int32_t scanY,
                                       int32_t leftX, int32_t rightX, uint32_t leftTexU, uint32_t leftTexV,
                                       uint32_t rightTexU, uint32_t rightTexV, RasterTexturedSpanCore *spanCore) {
	RasterTexturedSpanCoreState spanState;

	if (state == NULL || spanCore == NULL || scanY < 0 || (size_t)scanY >= state->screenRowCount ||
	    state->screenRows == NULL || state->screenRows[scanY] == NULL || state->lockedPayload == NULL ||
	    state->lockedPayloadBytes < SLIP_SPRITE_TRANSPARENT_COLOUR_END) {
		return 0;
	}
	memset(&spanState, 0, sizeof(spanState));
	spanState.screenRow = state->screenRows[scanY];
	spanState.screenRowBytes = state->screenRowBytes;
	spanState.spanLeftX = leftX;
	spanState.spanRightX = rightX;
	spanState.startTexU = leftTexU;
	spanState.startTexV = leftTexV;
	spanState.endTexU = rightTexU;
	spanState.endTexV = rightTexV;
	spanState.transparentWord = SlipBytes_ReadLE16(state->lockedPayload + SLIP_SPRITE_TRANSPARENT_COLOUR_OFFSET);
	spanState.textureRows = state->textureRows;
	spanState.textureRowCount = state->textureRowCount;
	spanState.textureRowBytes = state->textureRowBytes;
	return Raster_DrawTexturedSpanCore(&spanState, spanCore);
}

static int Raster_EmitAffineSpanRun(const RasterOpaqueAffineTexturedPolygonState *state, int32_t scanY, int32_t leftX,
                                    int32_t rightX, uint32_t leftTexU, uint32_t leftTexV, uint32_t rightTexU,
                                    uint32_t rightTexV, int32_t *previousLeftX, int32_t *previousRightX,
                                    uintptr_t *currentRowRun, uintptr_t *nextRowRun, int32_t *runPixelCount,
                                    RasterOpaqueAffineTexturedVisit *visit, size_t *pixelsWritten) {
	if (state == NULL || previousLeftX == NULL || previousRightX == NULL || currentRowRun == NULL ||
	    nextRowRun == NULL || runPixelCount == NULL || visit == NULL || pixelsWritten == NULL || scanY < 0 ||
	    (size_t)scanY + 1u >= state->screenRowCount || state->screenRows == NULL || state->screenRows[scanY] == NULL ||
	    state->screenRows[scanY + 1] == NULL) {
		return 0;
	}
	visit->calledDrawAffineSpan = true;
	if (!Raster_DrawAffineSpanCoreAt(state, scanY, leftX, rightX, leftTexU, leftTexV, rightTexU, rightTexV,
	                                 &visit->spanCore)) {
		return 0;
	}
	*previousLeftX = leftX;
	*previousRightX = rightX;
	*currentRowRun = (uintptr_t)state->screenRows[scanY] + (uintptr_t)(uint32_t)leftX;
	*runPixelCount = rightX - leftX + 1;
	*nextRowRun = (uintptr_t)state->screenRows[scanY + 1] + (uintptr_t)(uint32_t)leftX;
	*pixelsWritten += visit->spanCore.pixelsWritten;
	return 1;
}

static int Raster_AdvanceOpaqueAffineEdges(const RasterOpaqueAffineTexturedPolygonState *state, uint32_t pointBufferEnd,
                                           int32_t bottomY, uint32_t *leftPointOffset, uint32_t *rightPointOffset,
                                           int32_t *scanY, int32_t *leftX, int32_t *rightX, uint32_t *spanLeftTexU,
                                           uint32_t *spanLeftTexV, uint32_t *spanRightTexU, uint32_t *spanRightTexV,
                                           int32_t *leftTexUStep, int32_t *leftTexVStep, int32_t *rightTexUStep,
                                           int32_t *rightTexVStep, int32_t *leftXStep, uint16_t *leftXFraction,
                                           uint16_t *leftRemaining, int32_t *rightXStep, uint16_t *rightXFraction,
                                           uint16_t *rightRemaining, uint32_t *oneRowFlag) {
	bool leftCarry;
	bool rightCarry;
	uint16_t leftStepLo;
	uint16_t rightStepLo;

	if (state == NULL || leftPointOffset == NULL || rightPointOffset == NULL || scanY == NULL || leftX == NULL ||
	    rightX == NULL || spanLeftTexU == NULL || spanLeftTexV == NULL || spanRightTexU == NULL ||
	    spanRightTexV == NULL || leftTexUStep == NULL || leftTexVStep == NULL || rightTexUStep == NULL ||
	    rightTexVStep == NULL || leftXStep == NULL || leftXFraction == NULL || leftRemaining == NULL ||
	    rightXStep == NULL || rightXFraction == NULL || rightRemaining == NULL || oneRowFlag == NULL) {
		return RASTER_AFFINE_EDGE_FAILED;
	}
	*oneRowFlag = 0;
	if (*leftRemaining == 1u || *rightRemaining == 1u) {
		*oneRowFlag = UINT32_MAX;
	}

	++*scanY;
	*spanLeftTexU += (uint32_t)*leftTexUStep;
	*spanLeftTexV += (uint32_t)*leftTexVStep;
	*spanRightTexU += (uint32_t)*rightTexUStep;
	*spanRightTexV += (uint32_t)*rightTexVStep;

	leftStepLo = (uint16_t)*leftXStep;
	*leftXFraction = Raster_AddU16WithCarry(*leftXFraction, leftStepLo, &leftCarry);
	*leftX =
	    (int16_t)((uint16_t)*leftX + (uint16_t)((uint32_t)*leftXStep >> RASTER_FRACTION_BITS) + (leftCarry ? 1u : 0u));
	rightStepLo = (uint16_t)*rightXStep;
	*rightXFraction = Raster_AddU16WithCarry(*rightXFraction, rightStepLo, &rightCarry);
	*rightX = (int16_t)((uint16_t)*rightX + (uint16_t)((uint32_t)*rightXStep >> RASTER_FRACTION_BITS) +
	                    (rightCarry ? 1u : 0u));

	--*leftRemaining;
	if (*leftRemaining == 0u) {
		RasterAffineLeftEdgeStep leftStep;

		if (!Raster_StepLeftEdgeAffine(state->pointBuffer, state->pointBufferBase, pointBufferEnd,
		                               state->pointBufferBytes, *leftPointOffset, *scanY, bottomY, &leftStep)) {
			return RASTER_AFFINE_EDGE_FAILED;
		}
		if (leftStep.carryOut) {
			return RASTER_AFFINE_EDGE_FINISHED;
		}
		*leftPointOffset = leftStep.pointOffsetOut;
		*leftX = leftStep.currentX;
		*spanLeftTexU = leftStep.spanLeftTexU;
		*spanLeftTexV = leftStep.spanLeftTexV;
		*leftTexUStep = leftStep.texUStep;
		*leftTexVStep = leftStep.texVStep;
		*leftXStep = leftStep.xStep;
		*leftXFraction = leftStep.xFraction;
		*leftRemaining = leftStep.remaining;
	}

	--*rightRemaining;
	if (*rightRemaining == 0u) {
		RasterAffineRightEdgeStep rightStep;

		if (!Raster_StepRightEdgeAffine(state->pointBuffer, state->pointBufferBase, pointBufferEnd,
		                                state->pointBufferBytes, *rightPointOffset, *scanY, bottomY, &rightStep)) {
			return RASTER_AFFINE_EDGE_FAILED;
		}
		if (rightStep.carryOut) {
			return RASTER_AFFINE_EDGE_FINISHED;
		}
		*rightPointOffset = rightStep.pointOffsetOut;
		*rightX = rightStep.currentX;
		*spanRightTexU = rightStep.spanRightTexU;
		*spanRightTexV = rightStep.spanRightTexV;
		*rightTexUStep = rightStep.texUStep;
		*rightTexVStep = rightStep.texVStep;
		*rightXStep = rightStep.xStep;
		*rightXFraction = rightStep.xFraction;
		*rightRemaining = rightStep.remaining;
	}
	return RASTER_AFFINE_EDGE_ADVANCED;
}

int Raster_DrawAffineTexturedPolygon(const uint8_t *payload, size_t payloadBytes, RasterTexturedPoint *points,
                                     uint32_t pointCount, uint32_t rowScroll, RasterAffineScanlineLoopVisit *visits,
                                     size_t visitCapacity, size_t *pixelsWritten) {
	if (payload == NULL || payloadBytes < SLIP_SPRITE_HEADER_BYTES || points == NULL || pointCount == 0 ||
	    pointCount > RASTER_AFFINE_POLYGON_POINT_CAPACITY || visits == NULL || pixelsWritten == NULL)
		return 0;
	*pixelsWritten = 0;
	RasterAffineTexturedEntryScanVisit entryVisits[RASTER_AFFINE_POLYGON_POINT_CAPACITY];
	RasterAffineTexturedEntrySetup entry;
	uint8_t *const pointBytes = (uint8_t *)(void *)points;
	const size_t pointBufferBytes = (size_t)pointCount * sizeof(*points);
	if (!Raster_PrepareAffineTexturedEntry(0, payload, payloadBytes, pointBytes, 0, pointBufferBytes, pointCount,
	                                       entryVisits, RASTER_AFFINE_POLYGON_POINT_CAPACITY, &entry))
		return 0;
	const uint16_t width = SlipBytes_ReadLE16(payload + SLIP_SPRITE_WIDTH_OFFSET);
	const uint16_t height = SlipBytes_ReadLE16(payload + SLIP_SPRITE_HEIGHT_OFFSET);
	const uint8_t **const rows = calloc(height, sizeof(*rows));
	RasterTextureRowTable rowTable;
	if (rows == NULL || !Raster_BuildTextureRowTable(payload, payloadBytes, rowScroll, rows, height, &rowTable)) {
		free(rows);
		return 0;
	}
	int ok;
	if (entry.horizontalBranch) {
		if (entry.topY < 0 || entry.topY >= SLIPSTREAM_SCREEN_HEIGHT) {
			free(rows);
			return 0;
		}
		RasterAffineHorizontalSpanState horizontal = {.screenRow = g_screenRowPtrs[entry.topY],
		                                              .screenRowBytes = (size_t)g_screenPitch,
		                                              .lockedPayload = payload,
		                                              .lockedPayloadBytes = payloadBytes,
		                                              .pointBuffer = pointBytes,
		                                              .pointBufferBytes = pointBufferBytes,
		                                              .topLeftPointOffset = entry.topLeftPointOffset,
		                                              .topRightPointOffset = entry.topRightPointOffset,
		                                              .textureRows = rows,
		                                              .textureRowCount = height,
		                                              .textureRowBytes = width};
		RasterAffineHorizontalSpan result;
		ok = Raster_DrawAffineHorizontalSpan(&horizontal, &result);
		if (ok && result.calledDrawTexturedSpanCore)
			*pixelsWritten = result.spanCore.pixelsWritten;
	} else {
		RasterAffineLeftEdgeStep left;
		RasterAffineRightEdgeStep right;
		if (!Raster_StepLeftEdgeAffine(pointBytes, 0, entry.endPointOffset, pointBufferBytes, entry.topLeftPointOffset,
		                               entry.topY, entry.bottomY, &left) ||
		    !Raster_StepRightEdgeAffine(pointBytes, 0, entry.endPointOffset, pointBufferBytes,
		                                entry.topRightPointOffset, entry.topY, entry.bottomY, &right)) {
			free(rows);
			return 0;
		}
		RasterAffineScanlineLoopState loop = {.screenRows = g_screenRowPtrs,
		                                      .screenRowCount = SLIPSTREAM_SCREEN_HEIGHT,
		                                      .screenRowBytes = (size_t)g_screenPitch,
		                                      .lockedPayload = payload,
		                                      .lockedPayloadBytes = payloadBytes,
		                                      .pointBuffer = pointBytes,
		                                      .pointBufferEnd = entry.endPointOffset,
		                                      .pointBufferBytes = pointBufferBytes,
		                                      .textureRows = rows,
		                                      .textureRowCount = height,
		                                      .textureRowBytes = width,
		                                      .leftPointOffset = left.pointOffsetOut,
		                                      .rightPointOffset = right.pointOffsetOut,
		                                      .scanline = entry.topY,
		                                      .bottomY = entry.bottomY,
		                                      .leftX = left.currentX,
		                                      .rightX = right.currentX,
		                                      .spanLeftTexU = left.spanLeftTexU,
		                                      .spanLeftTexV = left.spanLeftTexV,
		                                      .spanRightTexU = right.spanRightTexU,
		                                      .spanRightTexV = right.spanRightTexV,
		                                      .leftTexUStep = left.texUStep,
		                                      .leftTexVStep = left.texVStep,
		                                      .rightTexUStep = right.texUStep,
		                                      .rightTexVStep = right.texVStep,
		                                      .leftXStep = left.xStep,
		                                      .leftXFraction = left.xFraction,
		                                      .leftRemaining = left.remaining,
		                                      .rightXStep = right.xStep,
		                                      .rightXFraction = right.xFraction,
		                                      .rightRemaining = right.remaining};
		RasterAffineScanlineLoop result;
		ok = Raster_DrawAffineScanlineLoop(&loop, visits, visitCapacity, &result);
		if (ok) {
			for (size_t i = 0; i < result.visitCount; ++i)
				if (visits[i].calledDrawTexturedSpanCore)
					*pixelsWritten += visits[i].spanCore.pixelsWritten;
			if (result.calledFinalTexturedSpanCore)
				*pixelsWritten += result.finalSpanCore.pixelsWritten;
		}
	}
	free(rows);
	return ok;
}

int Raster_DrawOpaqueAffineTexturedPolygon(const RasterOpaqueAffineTexturedPolygonState *state,
                                           RasterOpaqueAffineTexturedVisit *visits, size_t visitCapacity,
                                           RasterOpaqueAffineTexturedPolygon *result) {
	RasterAffineTexturedEntryScanVisit *entryVisits;
	uint32_t pointBufferEnd;
	uint32_t leftPointOffset;
	uint32_t rightPointOffset;
	int32_t scanY;
	int32_t bottomY;
	int32_t leftX;
	int32_t rightX;
	uint32_t spanLeftTexU;
	uint32_t spanLeftTexV;
	uint32_t spanRightTexU;
	uint32_t spanRightTexV;
	int32_t leftTexUStep;
	int32_t leftTexVStep;
	int32_t rightTexUStep;
	int32_t rightTexVStep;
	int32_t leftXStep;
	uint16_t leftXFraction;
	uint16_t leftRemaining;
	int32_t rightXStep;
	uint16_t rightXFraction;
	uint16_t rightRemaining;
	int32_t previousLeftX = 0;
	int32_t previousRightX = 0;
	uintptr_t currentRowRun = 0;
	uintptr_t nextRowRun = 0;
	int32_t runPixelCount = 0;
	uint32_t oneRowFlag = 0;
	size_t visitCount = 0;
	int continueAt = 0;

	if (state == NULL || result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->in = *state;
	result->pushad = true;
	result->calledPrepareTextureUVExtents = true;
	result->calledBuildTextureRowTable = true;
	if (state->screenRows == NULL || state->lockedPayload == NULL ||
	    state->lockedPayloadBytes < SLIP_SPRITE_TRANSPARENT_COLOUR_END ||
	    SlipBytes_ReadLE16(state->lockedPayload + SLIP_SPRITE_TRANSPARENT_COLOUR_OFFSET) !=
	        SLIP_SPRITE_NO_TRANSPARENT_COLOUR ||
	    state->pointBuffer == NULL || state->pointCount == 0u ||
	    state->pointBufferBytes < (size_t)state->pointCount * sizeof(RasterTexturedPoint) || visits == NULL ||
	    visitCapacity == 0u) {
		return 0;
	}

	entryVisits = (RasterAffineTexturedEntryScanVisit *)calloc(state->pointCount, sizeof(*entryVisits));
	if (entryVisits == NULL) {
		return 0;
	}
	if (!Raster_PrepareAffineTexturedEntry(0, state->lockedPayload, state->lockedPayloadBytes, state->pointBuffer,
	                                       state->pointBufferBase, state->pointBufferBytes, state->pointCount,
	                                       entryVisits, state->pointCount, &result->entryScan)) {
		free(entryVisits);
		return 0;
	}
	free(entryVisits);

	pointBufferEnd = state->pointBufferBase + result->entryScan.endPointOffset;
	result->horizontalBranch = result->entryScan.horizontalBranch;
	if (result->horizontalBranch) {
		RasterAffineHorizontalSpanState horizontalState;
		RasterAffineHorizontalSpan horizontalResult;

		if (result->entryScan.topY < 0 || (size_t)result->entryScan.topY >= state->screenRowCount) {
			return 0;
		}
		memset(&horizontalState, 0, sizeof(horizontalState));
		horizontalState.screenRow = state->screenRows[result->entryScan.topY];
		horizontalState.screenRowBytes = state->screenRowBytes;
		horizontalState.lockedPayload = state->lockedPayload;
		horizontalState.lockedPayloadBytes = state->lockedPayloadBytes;
		horizontalState.pointBuffer = state->pointBuffer;
		horizontalState.pointBufferBytes = state->pointBufferBytes;
		horizontalState.topLeftPointOffset = result->entryScan.topLeftPointOffset;
		horizontalState.topRightPointOffset = result->entryScan.topRightPointOffset;
		horizontalState.textureRows = state->textureRows;
		horizontalState.textureRowCount = state->textureRowCount;
		horizontalState.textureRowBytes = state->textureRowBytes;
		visits[0].scanline = result->entryScan.topY;
		visits[0].calledDrawAffineSpan = true;
		if (!Raster_DrawAffineHorizontalSpan(&horizontalState, &horizontalResult)) {
			return 0;
		}
		result->visitCount = 1u;
		result->spanCount = 1u;
		visits[0].spanCore = horizontalResult.spanCore;
		result->pixelsWritten = horizontalResult.spanCore.pixelsWritten;
		result->popad = true;
		return 1;
	}

	bottomY = result->entryScan.bottomY;
	scanY = result->entryScan.topY;
	result->initialCalledStepLeftEdgeAffine = true;
	if (!Raster_StepLeftEdgeAffine(state->pointBuffer, state->pointBufferBase, pointBufferEnd, state->pointBufferBytes,
	                               state->pointBufferBase + result->entryScan.topLeftPointOffset, scanY, bottomY,
	                               &result->initialLeftEdge)) {
		return 0;
	}
	if (result->initialLeftEdge.carryOut) {
		result->popad = true;
		return 1;
	}
	result->initialCalledStepRightEdgeAffine = true;
	if (!Raster_StepRightEdgeAffine(state->pointBuffer, state->pointBufferBase, pointBufferEnd, state->pointBufferBytes,
	                                state->pointBufferBase + result->entryScan.topRightPointOffset, scanY, bottomY,
	                                &result->initialRightEdge)) {
		return 0;
	}
	if (result->initialRightEdge.carryOut) {
		result->popad = true;
		return 1;
	}

	leftPointOffset = result->initialLeftEdge.pointOffsetOut;
	rightPointOffset = result->initialRightEdge.pointOffsetOut;
	leftX = result->initialLeftEdge.currentX;
	rightX = result->initialRightEdge.currentX;
	spanLeftTexU = result->initialLeftEdge.spanLeftTexU;
	spanLeftTexV = result->initialLeftEdge.spanLeftTexV;
	spanRightTexU = result->initialRightEdge.spanRightTexU;
	spanRightTexV = result->initialRightEdge.spanRightTexV;
	leftTexUStep = result->initialLeftEdge.texUStep;
	leftTexVStep = result->initialLeftEdge.texVStep;
	rightTexUStep = result->initialRightEdge.texUStep;
	rightTexVStep = result->initialRightEdge.texVStep;
	leftXStep = result->initialLeftEdge.xStep;
	leftXFraction = result->initialLeftEdge.xFraction;
	leftRemaining = result->initialLeftEdge.remaining;
	rightXStep = result->initialRightEdge.xStep;
	rightXFraction = result->initialRightEdge.xFraction;
	rightRemaining = result->initialRightEdge.remaining;

	if (!Raster_EmitAffineSpanRun(state, scanY, leftX, rightX, spanLeftTexU, spanLeftTexV, spanRightTexU, spanRightTexV,
	                              &previousLeftX, &previousRightX, &currentRowRun, &nextRowRun, &runPixelCount,
	                              &visits[visitCount], &result->pixelsWritten)) {
		return 0;
	}
	visits[visitCount].scanline = scanY;
	++visitCount;
	++result->spanCount;
	continueAt = 1;

	for (;;) {
		RasterOpaqueAffineTexturedVisit *visit;
		int advanceResult;
		int32_t predictedLeftX;
		int32_t predictedRightX;
		uint32_t spanOverlapFlags = 0;
		uintptr_t leftCopySource = 0;
		uintptr_t leftCopyDestination = 0;
		int32_t leftCopyCount = 0;
		uintptr_t rightCopySource = 0;
		uintptr_t rightCopyDestination = 0;
		int32_t rightCopyCount = 0;

		if (visitCount >= visitCapacity) {
			result->visitCount = visitCount;
			return 0;
		}
		visit = &visits[visitCount];
		memset(visit, 0, sizeof(*visit));
		visit->scanline = scanY;

		if (continueAt) {
			visit->calledAdvanceOpaqueAffineEdges = true;
			advanceResult = Raster_AdvanceOpaqueAffineEdges(
			    state, pointBufferEnd, bottomY, &leftPointOffset, &rightPointOffset, &scanY, &leftX, &rightX,
			    &spanLeftTexU, &spanLeftTexV, &spanRightTexU, &spanRightTexV, &leftTexUStep, &leftTexVStep,
			    &rightTexUStep, &rightTexVStep, &leftXStep, &leftXFraction, &leftRemaining, &rightXStep,
			    &rightXFraction, &rightRemaining, &oneRowFlag);
			if (advanceResult == RASTER_AFFINE_EDGE_FAILED) {
				result->failVisitIndexValid = true;
				result->failVisitIndex = visitCount;
				result->visitCount = visitCount + 1u;
				return 0;
			}
			visit->carryFrom = advanceResult == RASTER_AFFINE_EDGE_FINISHED;
			if (visit->carryFrom) {
				++visitCount;
				result->popad = true;
				break;
			}
			if (scanY >= bottomY) {
				visit->calledDrawAffineSpan = true;
				if (!Raster_DrawAffineSpanCoreAt(state, scanY, leftX, rightX, spanLeftTexU, spanLeftTexV, spanRightTexU,
				                                 spanRightTexV, &visit->spanCore)) {
					result->failVisitIndexValid = true;
					result->failVisitIndex = visitCount;
					result->visitCount = visitCount + 1u;
					return 0;
				}
				result->pixelsWritten += visit->spanCore.pixelsWritten;
				++visitCount;
				++result->spanCount;
				result->popad = true;
				break;
			}
			continueAt = 0;
			++visitCount;
			continue;
		}

		if ((scanY & 1) == 0 || oneRowFlag != 0u) {
			if (!Raster_EmitAffineSpanRun(state, scanY, leftX, rightX, spanLeftTexU, spanLeftTexV, spanRightTexU,
			                              spanRightTexV, &previousLeftX, &previousRightX, &currentRowRun, &nextRowRun,
			                              &runPixelCount, visit, &result->pixelsWritten)) {
				result->failVisitIndexValid = true;
				result->failVisitIndex = visitCount;
				result->visitCount = visitCount + 1u;
				return 0;
			}
			++result->spanCount;
			continueAt = 1;
			continue;
		}

		predictedLeftX = Raster_PredictTexturedXAfterFractionStep(leftX, leftXStep, leftXFraction);
		predictedRightX = Raster_PredictTexturedXAfterFractionStep(rightX, rightXStep, rightXFraction);
		if ((int16_t)predictedRightX < (int16_t)leftX || (int16_t)predictedLeftX > (int16_t)rightX) {
			if (!Raster_EmitAffineSpanRun(state, scanY, leftX, rightX, spanLeftTexU, spanLeftTexV, spanRightTexU,
			                              spanRightTexV, &previousLeftX, &previousRightX, &currentRowRun, &nextRowRun,
			                              &runPixelCount, visit, &result->pixelsWritten)) {
				result->failVisitIndexValid = true;
				result->failVisitIndex = visitCount;
				result->visitCount = visitCount + 1u;
				return 0;
			}
			++result->spanCount;
			continueAt = 1;
			continue;
		}

		if ((uint32_t)leftX < (uint32_t)previousLeftX) {
			spanOverlapFlags |= RASTER_SPAN_EXTENDS_LEFT;
		}
		if ((uint32_t)rightX > (uint32_t)previousRightX) {
			spanOverlapFlags |= RASTER_SPAN_EXTENDS_RIGHT;
		}
		visit->spanOverlapFlags = spanOverlapFlags;

		if ((spanOverlapFlags & RASTER_SPAN_EXTENDS_RIGHT) != 0u) {
			if ((int16_t)rightX > (int16_t)predictedRightX || (int16_t)leftX < (int16_t)predictedLeftX) {
				if (!Raster_EmitAffineSpanRun(state, scanY, leftX, rightX, spanLeftTexU, spanLeftTexV, spanRightTexU,
				                              spanRightTexV, &previousLeftX, &previousRightX, &currentRowRun,
				                              &nextRowRun, &runPixelCount, visit, &result->pixelsWritten)) {
					result->failVisitIndexValid = true;
					result->failVisitIndex = visitCount;
					result->visitCount = visitCount + 1u;
					return 0;
				}
				++result->spanCount;
				continueAt = 1;
				continue;
			}
			rightCopyCount = rightX - previousRightX;
			rightCopySource = (uintptr_t)state->screenRows[scanY + 1] + (uintptr_t)(uint32_t)(previousRightX + 1);
			rightCopyDestination = (uintptr_t)state->screenRows[scanY] + (uintptr_t)(uint32_t)(previousRightX + 1);
		} else {
			runPixelCount -= previousRightX - rightX;
		}

		if ((spanOverlapFlags & RASTER_SPAN_EXTENDS_LEFT) != 0u) {
			leftCopyCount = previousLeftX - leftX;
			leftCopySource = (uintptr_t)state->screenRows[scanY + 1] + (uintptr_t)(uint32_t)leftX;
			leftCopyDestination = (uintptr_t)state->screenRows[scanY] + (uintptr_t)(uint32_t)leftX;
		} else {
			const int32_t leftDelta = leftX - previousLeftX;

			currentRowRun += (uintptr_t)leftDelta;
			nextRowRun += (uintptr_t)leftDelta;
			runPixelCount -= leftDelta;
		}

		if (runPixelCount <= 0) {
			if (!Raster_EmitAffineSpanRun(state, scanY, leftX, rightX, spanLeftTexU, spanLeftTexV, spanRightTexU,
			                              spanRightTexV, &previousLeftX, &previousRightX, &currentRowRun, &nextRowRun,
			                              &runPixelCount, visit, &result->pixelsWritten)) {
				result->failVisitIndexValid = true;
				result->failVisitIndex = visitCount;
				result->visitCount = visitCount + 1u;
				return 0;
			}
			++result->spanCount;
			continueAt = 1;
			continue;
		}

		visit->copyCommon = true;
		if (!Raster_ForwardCopy((uint8_t *)currentRowRun, (uint8_t *)nextRowRun, runPixelCount, &visit->commonCopy)) {
			result->failVisitIndexValid = true;
			result->failVisitIndex = visitCount;
			result->visitCount = visitCount + 1u;
			return 0;
		}
		++result->rowCopyCount;
		result->rowCopyBytes += (size_t)runPixelCount;

		if (spanOverlapFlags != 0u) {
			visit->calledAdvanceOpaqueAffineEdges = true;
			advanceResult = Raster_AdvanceOpaqueAffineEdges(
			    state, pointBufferEnd, bottomY, &leftPointOffset, &rightPointOffset, &scanY, &leftX, &rightX,
			    &spanLeftTexU, &spanLeftTexV, &spanRightTexU, &spanRightTexV, &leftTexUStep, &leftTexVStep,
			    &rightTexUStep, &rightTexVStep, &leftXStep, &leftXFraction, &leftRemaining, &rightXStep,
			    &rightXFraction, &rightRemaining, &oneRowFlag);
			if (advanceResult == RASTER_AFFINE_EDGE_FAILED) {
				result->failVisitIndexValid = true;
				result->failVisitIndex = visitCount;
				result->visitCount = visitCount + 1u;
				return 0;
			}
			visit->carryFrom = advanceResult == RASTER_AFFINE_EDGE_FINISHED;
			if (visit->carryFrom) {
				++visitCount;
				result->popad = true;
				break;
			}
			if (!Raster_EmitAffineSpanRun(state, scanY, leftX, rightX, spanLeftTexU, spanLeftTexV, spanRightTexU,
			                              spanRightTexV, &previousLeftX, &previousRightX, &currentRowRun, &nextRowRun,
			                              &runPixelCount, visit, &result->pixelsWritten)) {
				result->failVisitIndexValid = true;
				result->failVisitIndex = visitCount;
				result->visitCount = visitCount + 1u;
				return 0;
			}
			++result->spanCount;
			if ((spanOverlapFlags & RASTER_SPAN_EXTENDS_LEFT) != 0u) {
				visit->copyLeftExtension = true;
				if (!Raster_ForwardCopy((uint8_t *)leftCopySource, (uint8_t *)leftCopyDestination, leftCopyCount,
				                        &visit->leftCopy)) {
					result->failVisitIndexValid = true;
					result->failVisitIndex = visitCount;
					result->visitCount = visitCount + 1u;
					return 0;
				}
				++result->rowCopyCount;
				if (leftCopyCount > 0) {
					result->rowCopyBytes += (size_t)leftCopyCount;
				}
			}
			if ((spanOverlapFlags & RASTER_SPAN_EXTENDS_RIGHT) != 0u) {
				visit->copyRightExtension = true;
				if (!Raster_ForwardCopy((uint8_t *)rightCopySource, (uint8_t *)rightCopyDestination, rightCopyCount,
				                        &visit->rightCopy)) {
					result->failVisitIndexValid = true;
					result->failVisitIndex = visitCount;
					result->visitCount = visitCount + 1u;
					return 0;
				}
				++result->rowCopyCount;
				if (rightCopyCount > 0) {
					result->rowCopyBytes += (size_t)rightCopyCount;
				}
			}
			if (scanY == bottomY) {
				++visitCount;
				result->popad = true;
				break;
			}
			continueAt = 1;
			++visitCount;
			continue;
		}

		if (scanY == bottomY) {
			++visitCount;
			result->popad = true;
			break;
		}
		continueAt = 1;
		++visitCount;
	}

	result->visitCount = visitCount;
	return 1;
}

static int Raster_OpaqueSetupAndDispatchPerspectiveSpan(
    const RasterOpaquePerspectiveTexturedPolygonState *state, const RasterTexturedAdvanceEdgesState *edgeState,
    uint32_t leftPointOffset, uint32_t rightPointOffset, uint32_t leftBaseDepth, uint32_t leftBaseTexU,
    uint32_t leftBaseTexV, uint32_t rightBaseDepth, uint32_t rightBaseTexU, uint32_t rightBaseTexV,
    uint32_t *spanLeftTexU, uint32_t *spanLeftTexV, uint32_t *spanLeftDepth, uint32_t *spanRightTexU,
    uint32_t *spanRightTexV, uint32_t *spanRightDepth, uintptr_t *spanStartCurrentRow, uintptr_t *spanStartNextRow,
    int32_t *spanPixelCount, int32_t *previousLeftX, int32_t *previousRightX, RasterTexturedSpanSetup *spanSetup,
    RasterTexturedSpanDispatch *spanDispatch, size_t *pixelsWritten) {
	const uint8_t *leftPoint;
	const uint8_t *rightPoint;
	RasterTexturedSpanSetupState spanState;

	if (state == NULL || edgeState == NULL || spanLeftTexU == NULL || spanLeftTexV == NULL || spanLeftDepth == NULL ||
	    spanRightTexU == NULL || spanRightTexV == NULL || spanRightDepth == NULL || spanStartCurrentRow == NULL ||
	    spanStartNextRow == NULL || spanPixelCount == NULL || previousLeftX == NULL || previousRightX == NULL ||
	    spanSetup == NULL || spanDispatch == NULL || pixelsWritten == NULL || edgeState->scanY < 0 ||
	    (size_t)edgeState->scanY + 1u >= state->screenRowCount ||
	    !Raster_PointAbsOffsetValid(state->pointBufferBase, state->pointBufferBytes, leftPointOffset) ||
	    !Raster_PointAbsOffsetValid(state->pointBufferBase, state->pointBufferBytes, rightPointOffset)) {
		return 0;
	}
	leftPoint = state->pointBuffer + (leftPointOffset - state->pointBufferBase);
	rightPoint = state->pointBuffer + (rightPointOffset - state->pointBufferBase);
	memset(&spanState, 0, sizeof(spanState));
	spanState.spanLeftX = edgeState->leftX;
	spanState.spanRightX = edgeState->rightX;
	spanState.scanlineIndex = (uint32_t)edgeState->scanY;
	spanState.texturePayload = (uintptr_t)state->lockedPayload;
	spanState.leftAccumulatedBaseDepthPerRow = edgeState->leftAccumulatedBaseDepthPerRow;
	spanState.leftAccumulatedNextPointDepthPerRow = edgeState->leftAccumulatedNextPointDepthPerRow;
	spanState.leftBaseDepth = leftBaseDepth;
	spanState.leftBaseTexU = leftBaseTexU;
	spanState.leftBaseTexV = leftBaseTexV;
	spanState.rightAccumulatedBaseDepthPerRow = edgeState->rightAccumulatedBaseDepthPerRow;
	spanState.rightAccumulatedNextPointDepthPerRow = edgeState->rightAccumulatedNextPointDepthPerRow;
	spanState.rightBaseDepth = rightBaseDepth;
	spanState.rightBaseTexU = rightBaseTexU;
	spanState.rightBaseTexV = rightBaseTexV;
	spanState.leftPointDepth = SlipBytes_ReadLE32(leftPoint + offsetof(RasterTexturedPoint, depth));
	spanState.leftPointTexU = SlipBytes_ReadLE32(leftPoint + offsetof(RasterTexturedPoint, scaledU));
	spanState.leftPointTexV = SlipBytes_ReadLE32(leftPoint + offsetof(RasterTexturedPoint, scaledV));
	spanState.rightPointDepth = SlipBytes_ReadLE32(rightPoint + offsetof(RasterTexturedPoint, depth));
	spanState.rightPointTexU = SlipBytes_ReadLE32(rightPoint + offsetof(RasterTexturedPoint, scaledU));
	spanState.rightPointTexV = SlipBytes_ReadLE32(rightPoint + offsetof(RasterTexturedPoint, scaledV));
	spanState.currentRowPointer = (uintptr_t)state->screenRows[edgeState->scanY];
	spanState.nextRowPointer = (uintptr_t)state->screenRows[edgeState->scanY + 1];

	if (!Raster_SetupPerspectiveTexturedSpan(&spanState, spanSetup)) {
		return 0;
	}
	*spanLeftTexU = spanSetup->spanLeftTexU;
	*spanLeftTexV = spanSetup->spanLeftTexV;
	*spanLeftDepth = spanSetup->spanLeftDepth;
	*spanRightTexU = spanSetup->spanRightTexU;
	*spanRightTexV = spanSetup->spanRightTexV;
	*spanRightDepth = spanSetup->spanRightDepth;
	*previousLeftX = spanSetup->postCallLeftX;
	*previousRightX = spanSetup->postCallRightX;
	*spanStartCurrentRow = spanSetup->spanStartCurrentRow;
	*spanStartNextRow = spanSetup->spanStartNextRow;
	*spanPixelCount = (int32_t)spanSetup->spanPixelCount;

	if (!Raster_DispatchPerspectiveTexturedSpanRows(
	        state->screenRows, state->screenRowCount, state->screenRowBytes, state->lockedPayload,
	        state->lockedPayloadBytes, state->textureRows, state->textureRowCount, state->textureRowBytes,
	        edgeState->scanY, edgeState->leftX, edgeState->rightX, *spanLeftTexU, *spanLeftTexV, *spanRightTexU,
	        *spanRightTexV, *spanLeftDepth, *spanRightDepth, spanDispatch)) {
		return 0;
	}
	*pixelsWritten =
	    spanDispatch->jumpShortSpan ? spanDispatch->shortSpanCore.pixelsWritten : spanDispatch->longSpanPixelsWritten;
	return 1;
}

int Raster_DrawOpaquePerspectiveTexturedPolygon(const RasterOpaquePerspectiveTexturedPolygonState *state,
                                                RasterOpaquePerspectiveTexturedVisit *visits, size_t visitCapacity,
                                                RasterOpaquePerspectiveTexturedPolygon *result) {
	RasterTexturedAdvanceEdgesState edgeState;
	RasterTexturedLeftEdgeStep leftEdge;
	RasterTexturedRightEdgeStep rightEdge;
	uint32_t leftPointOffset;
	uint32_t rightPointOffset;
	uint32_t leftBaseDepth;
	uint32_t leftBaseTexU;
	uint32_t leftBaseTexV;
	uint32_t rightBaseDepth;
	uint32_t rightBaseTexU;
	uint32_t rightBaseTexV;
	uint32_t spanLeftTexU = 0;
	uint32_t spanLeftTexV = 0;
	uint32_t spanLeftDepth = 0;
	uint32_t spanRightTexU = 0;
	uint32_t spanRightTexV = 0;
	uint32_t spanRightDepth = 0;
	uintptr_t spanCurrentRow = 0;
	uintptr_t spanNextRow = 0;
	int32_t spanPixelCount = 0;
	int32_t previousLeftX = 0;
	int32_t previousRightX = 0;
	uint32_t oneRowFlag = 0;
	int advanceAfterInitialSpan = 1;
	size_t visitCount = 0;

	if (state == NULL || result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->in = *state;
	if (visits == NULL || visitCapacity == 0u || state->screenRows == NULL || state->lockedPayload == NULL ||
	    state->pointBuffer == NULL || state->pointCount == 0u || state->pointBufferEnd < state->pointBufferBase) {
		return 0;
	}

	result->initialCalledStepLeftEdgeTexturedPerspective = true;
	if (!Raster_StepLeftEdgeTexturedPerspective(state->pointBuffer, state->pointBufferBase, state->pointBufferEnd,
	                                            state->pointBufferBytes, state->topLeftPointOffset, state->topY,
	                                            state->bottomY, &leftEdge)) {
		return 0;
	}
	result->initialLeftEdge = leftEdge;
	if (leftEdge.carryOut) {
		result->popad = true;
		return 1;
	}

	result->initialCalledStepRightEdgeTexturedPerspective = true;
	if (!Raster_StepRightEdgeTexturedPerspective(state->pointBuffer, state->pointBufferBase, state->pointBufferEnd,
	                                             state->pointBufferBytes, state->topRightPointOffset, state->topY,
	                                             state->bottomY, &rightEdge)) {
		return 0;
	}
	result->initialRightEdge = rightEdge;
	if (rightEdge.carryOut) {
		result->popad = true;
		return 1;
	}

	leftPointOffset = leftEdge.pointOffsetOut;
	rightPointOffset = rightEdge.pointOffsetOut;
	leftBaseDepth = leftEdge.leftBaseDepth;
	leftBaseTexU = leftEdge.leftBaseTexU;
	leftBaseTexV = leftEdge.leftBaseTexV;
	rightBaseDepth = rightEdge.rightBaseDepth;
	rightBaseTexU = rightEdge.rightBaseTexU;
	rightBaseTexV = rightEdge.rightBaseTexV;
	memset(&edgeState, 0, sizeof(edgeState));
	edgeState.scanY = state->topY;
	edgeState.leftX = leftEdge.currentX;
	edgeState.rightX = rightEdge.currentX;
	edgeState.leftAccumulatedBaseDepthPerRow = leftEdge.accumulatedBaseDepthPerRow;
	edgeState.leftBaseDepthPerRow = leftEdge.baseDepthPerRow;
	edgeState.leftAccumulatedNextPointDepthPerRow = leftEdge.accumulatedNextPointDepthPerRow;
	edgeState.leftNextPointDepthPerRow = leftEdge.nextPointDepthPerRow;
	edgeState.rightAccumulatedBaseDepthPerRow = rightEdge.accumulatedBaseDepthPerRow;
	edgeState.rightBaseDepthPerRow = rightEdge.baseDepthPerRow;
	edgeState.rightAccumulatedNextPointDepthPerRow = rightEdge.accumulatedNextPointDepthPerRow;
	edgeState.rightNextPointDepthPerRow = rightEdge.nextPointDepthPerRow;
	edgeState.leftXStep = leftEdge.xStep;
	edgeState.leftXFraction = leftEdge.xFraction;
	edgeState.leftRemaining = leftEdge.remaining;
	edgeState.rightXStep = rightEdge.xStep;
	edgeState.rightXFraction = rightEdge.xFraction;
	edgeState.rightRemaining = rightEdge.remaining;

	result->initialCalledSetupPerspectiveTexturedSpan = true;
	if (!Raster_OpaqueSetupAndDispatchPerspectiveSpan(
	        state, &edgeState, leftPointOffset, rightPointOffset, leftBaseDepth, leftBaseTexU, leftBaseTexV,
	        rightBaseDepth, rightBaseTexU, rightBaseTexV, &spanLeftTexU, &spanLeftTexV, &spanLeftDepth, &spanRightTexU,
	        &spanRightTexV, &spanRightDepth, &spanCurrentRow, &spanNextRow, &spanPixelCount, &previousLeftX,
	        &previousRightX, &result->initialSpanSetup, &result->initialSpanDispatch, &result->pixelsWritten)) {
		return 0;
	}
	++result->spanDispatchCount;

	for (;;) {
		RasterOpaquePerspectiveTexturedVisit *visit;
		RasterTexturedAdvanceEdges advance;
		bool terminatePolygon = false;

		if (visitCount >= visitCapacity) {
			result->visitCount = visitCount;
			return 0;
		}
		visit = &visits[visitCount];
		memset(visit, 0, sizeof(*visit));
		visit->scanline = edgeState.scanY;
		do {
			if (advanceAfterInitialSpan) {
				advanceAfterInitialSpan = 0;
				break;
			}

			if ((edgeState.scanY & 1) == 0) {
				visit->evenCall = true;
				if (!Raster_OpaqueSetupAndDispatchPerspectiveSpan(
				        state, &edgeState, leftPointOffset, rightPointOffset, leftBaseDepth, leftBaseTexU, leftBaseTexV,
				        rightBaseDepth, rightBaseTexU, rightBaseTexV, &spanLeftTexU, &spanLeftTexV, &spanLeftDepth,
				        &spanRightTexU, &spanRightTexV, &spanRightDepth, &spanCurrentRow, &spanNextRow, &spanPixelCount,
				        &previousLeftX, &previousRightX, &visit->spanSetup, &visit->spanDispatch,
				        &visit->spanPixelsWritten)) {
					result->failVisitIndexValid = true;
					result->failVisitIndex = visitCount;
					result->visitCount = visitCount + 1u;
					return 0;
				}
				++result->spanDispatchCount;
				result->pixelsWritten += visit->spanPixelsWritten;
				break;
			}

			if (edgeState.scanY >= state->bottomY) {
				visit->terminalDispatch = true;
				if (!Raster_DispatchPerspectiveTexturedSpanRows(
				        state->screenRows, state->screenRowCount, state->screenRowBytes, state->lockedPayload,
				        state->lockedPayloadBytes, state->textureRows, state->textureRowCount, state->textureRowBytes,
				        edgeState.scanY, edgeState.leftX, edgeState.rightX, spanLeftTexU, spanLeftTexV, spanRightTexU,
				        spanRightTexV, spanLeftDepth, spanRightDepth, &visit->terminalSpanDispatch)) {
					result->failVisitIndexValid = true;
					result->failVisitIndex = visitCount;
					result->visitCount = visitCount + 1u;
					return 0;
				}
				visit->spanPixelsWritten = visit->terminalSpanDispatch.jumpShortSpan
				                               ? visit->terminalSpanDispatch.shortSpanCore.pixelsWritten
				                               : visit->terminalSpanDispatch.longSpanPixelsWritten;
				result->pixelsWritten += visit->spanPixelsWritten;
				++result->spanDispatchCount;
				++visitCount;
				terminatePolygon = true;
				break;
			}

			visit->oneRowFlag = oneRowFlag != 0u;
			if (oneRowFlag != 0u) {
				visit->evenCall = true;
				if (!Raster_OpaqueSetupAndDispatchPerspectiveSpan(
				        state, &edgeState, leftPointOffset, rightPointOffset, leftBaseDepth, leftBaseTexU, leftBaseTexV,
				        rightBaseDepth, rightBaseTexU, rightBaseTexV, &spanLeftTexU, &spanLeftTexV, &spanLeftDepth,
				        &spanRightTexU, &spanRightTexV, &spanRightDepth, &spanCurrentRow, &spanNextRow, &spanPixelCount,
				        &previousLeftX, &previousRightX, &visit->spanSetup, &visit->spanDispatch,
				        &visit->spanPixelsWritten)) {
					result->failVisitIndexValid = true;
					result->failVisitIndex = visitCount;
					result->visitCount = visitCount + 1u;
					return 0;
				}
				++result->spanDispatchCount;
				result->pixelsWritten += visit->spanPixelsWritten;
				break;
			}

			visit->predictedLeftX =
			    Raster_PredictTexturedXAfterFractionStep(edgeState.leftX, edgeState.leftXStep, edgeState.leftXFraction);
			visit->predictedRightX = Raster_PredictTexturedXAfterFractionStep(edgeState.rightX, edgeState.rightXStep,
			                                                                  edgeState.rightXFraction);
			visit->predictedCarry = (int16_t)visit->predictedRightX < (int16_t)edgeState.leftX ||
			                        (int16_t)visit->predictedLeftX > (int16_t)edgeState.rightX;
			if (visit->predictedCarry) {
				visit->evenCall = true;
				if (!Raster_OpaqueSetupAndDispatchPerspectiveSpan(
				        state, &edgeState, leftPointOffset, rightPointOffset, leftBaseDepth, leftBaseTexU, leftBaseTexV,
				        rightBaseDepth, rightBaseTexU, rightBaseTexV, &spanLeftTexU, &spanLeftTexV, &spanLeftDepth,
				        &spanRightTexU, &spanRightTexV, &spanRightDepth, &spanCurrentRow, &spanNextRow, &spanPixelCount,
				        &previousLeftX, &previousRightX, &visit->spanSetup, &visit->spanDispatch,
				        &visit->spanPixelsWritten)) {
					result->failVisitIndexValid = true;
					result->failVisitIndex = visitCount;
					result->visitCount = visitCount + 1u;
					return 0;
				}
				++result->spanDispatchCount;
				result->pixelsWritten += visit->spanPixelsWritten;
				break;
			}

			if ((uint32_t)edgeState.leftX < (uint32_t)previousLeftX) {
				visit->spanOverlapFlags |= RASTER_SPAN_EXTENDS_LEFT;
			}
			if ((uint32_t)edgeState.rightX > (uint32_t)previousRightX) {
				visit->spanOverlapFlags |= RASTER_SPAN_EXTENDS_RIGHT;
			}

			if ((visit->spanOverlapFlags & RASTER_SPAN_EXTENDS_RIGHT) != 0u) {
				if ((int16_t)edgeState.rightX > (int16_t)visit->predictedRightX ||
				    (int16_t)edgeState.leftX < (int16_t)visit->predictedLeftX) {
					visit->evenCall = true;
					if (!Raster_OpaqueSetupAndDispatchPerspectiveSpan(
					        state, &edgeState, leftPointOffset, rightPointOffset, leftBaseDepth, leftBaseTexU,
					        leftBaseTexV, rightBaseDepth, rightBaseTexU, rightBaseTexV, &spanLeftTexU, &spanLeftTexV,
					        &spanLeftDepth, &spanRightTexU, &spanRightTexV, &spanRightDepth, &spanCurrentRow,
					        &spanNextRow, &spanPixelCount, &previousLeftX, &previousRightX, &visit->spanSetup,
					        &visit->spanDispatch, &visit->spanPixelsWritten)) {
						result->failVisitIndexValid = true;
						result->failVisitIndex = visitCount;
						result->visitCount = visitCount + 1u;
						return 0;
					}
					++result->spanDispatchCount;
					result->pixelsWritten += visit->spanPixelsWritten;
					break;
				}
			} else {
				spanPixelCount -= previousRightX - edgeState.rightX;
			}

			if ((visit->spanOverlapFlags & RASTER_SPAN_EXTENDS_LEFT) != 0u) {
				visit->copyLeftExtension = true;
			} else {
				const int32_t leftDelta = edgeState.leftX - previousLeftX;

				spanCurrentRow += (uintptr_t)leftDelta;
				spanNextRow += (uintptr_t)leftDelta;
				spanPixelCount -= leftDelta;
			}

			if (spanPixelCount <= 0) {
				visit->evenCall = true;
				if (!Raster_OpaqueSetupAndDispatchPerspectiveSpan(
				        state, &edgeState, leftPointOffset, rightPointOffset, leftBaseDepth, leftBaseTexU, leftBaseTexV,
				        rightBaseDepth, rightBaseTexU, rightBaseTexV, &spanLeftTexU, &spanLeftTexV, &spanLeftDepth,
				        &spanRightTexU, &spanRightTexV, &spanRightDepth, &spanCurrentRow, &spanNextRow, &spanPixelCount,
				        &previousLeftX, &previousRightX, &visit->spanSetup, &visit->spanDispatch,
				        &visit->spanPixelsWritten)) {
					result->failVisitIndexValid = true;
					result->failVisitIndex = visitCount;
					result->visitCount = visitCount + 1u;
					return 0;
				}
				++result->spanDispatchCount;
				result->pixelsWritten += visit->spanPixelsWritten;
				break;
			}

			visit->copyCommon = true;
			if (!Raster_ForwardCopy((uint8_t *)spanCurrentRow, (uint8_t *)spanNextRow, spanPixelCount,
			                        &visit->commonCopy)) {
				result->failVisitIndexValid = true;
				result->failVisitIndex = visitCount;
				result->visitCount = visitCount + 1u;
				return 0;
			}
			++result->rowCopyCount;
			result->rowCopyBytes += (size_t)spanPixelCount;

			if (visit->spanOverlapFlags != 0u) {
				uintptr_t leftCopySource = 0;
				uintptr_t leftCopyDestination = 0;
				int32_t leftCopyCount = 0;
				uintptr_t rightCopySource = 0;
				uintptr_t rightCopyDestination = 0;
				int32_t rightCopyCount = 0;

				if ((visit->spanOverlapFlags & RASTER_SPAN_EXTENDS_LEFT) != 0u) {
					leftCopySource = (uintptr_t)(state->screenRows[edgeState.scanY + 1]) + (uintptr_t)edgeState.leftX;
					leftCopyDestination = (uintptr_t)(state->screenRows[edgeState.scanY]) + (uintptr_t)edgeState.leftX;
					leftCopyCount = previousLeftX - edgeState.leftX;
				}
				if ((visit->spanOverlapFlags & RASTER_SPAN_EXTENDS_RIGHT) != 0u) {
					rightCopyCount = edgeState.rightX - previousRightX;
					rightCopySource =
					    (uintptr_t)(state->screenRows[edgeState.scanY + 1]) + (uintptr_t)(previousRightX + 1);
					rightCopyDestination =
					    (uintptr_t)(state->screenRows[edgeState.scanY]) + (uintptr_t)(previousRightX + 1);
				}

				visit->calledAdvancePerspectiveTexturedEdges = true;
				if (!Raster_AdvancePerspectiveTexturedEdges(&edgeState, 0, 0, &advance)) {
					result->failVisitIndexValid = true;
					result->failVisitIndex = visitCount;
					result->visitCount = visitCount + 1u;
					return 0;
				}
				visit->advance = advance;
				edgeState = advance.out;
				oneRowFlag = advance.oneRowFlag;
				if (advance.calledStepLeftEdgeTexturedPerspective) {
					visit->calledStepLeftEdgeTexturedPerspective = true;
					if (!Raster_StepLeftEdgeTexturedPerspective(
					        state->pointBuffer, state->pointBufferBase, state->pointBufferEnd, state->pointBufferBytes,
					        leftPointOffset, edgeState.scanY, state->bottomY, &leftEdge)) {
						result->failVisitIndexValid = true;
						result->failVisitIndex = visitCount;
						result->visitCount = visitCount + 1u;
						return 0;
					}
					visit->leftEdge = leftEdge;
					if (leftEdge.carryOut) {
						++visitCount;
						terminatePolygon = true;
						break;
					}
					leftPointOffset = leftEdge.pointOffsetOut;
					edgeState.leftX = leftEdge.currentX;
					edgeState.leftAccumulatedBaseDepthPerRow = leftEdge.accumulatedBaseDepthPerRow;
					edgeState.leftBaseDepthPerRow = leftEdge.baseDepthPerRow;
					edgeState.leftAccumulatedNextPointDepthPerRow = leftEdge.accumulatedNextPointDepthPerRow;
					edgeState.leftNextPointDepthPerRow = leftEdge.nextPointDepthPerRow;
					edgeState.leftXStep = leftEdge.xStep;
					edgeState.leftXFraction = leftEdge.xFraction;
					edgeState.leftRemaining = leftEdge.remaining;
					leftBaseDepth = leftEdge.leftBaseDepth;
					leftBaseTexU = leftEdge.leftBaseTexU;
					leftBaseTexV = leftEdge.leftBaseTexV;
				}
				if (advance.calledStepRightEdgeTexturedPerspective) {
					visit->calledStepRightEdgeTexturedPerspective = true;
					if (!Raster_StepRightEdgeTexturedPerspective(
					        state->pointBuffer, state->pointBufferBase, state->pointBufferEnd, state->pointBufferBytes,
					        rightPointOffset, edgeState.scanY, state->bottomY, &rightEdge)) {
						result->failVisitIndexValid = true;
						result->failVisitIndex = visitCount;
						result->visitCount = visitCount + 1u;
						return 0;
					}
					visit->rightEdge = rightEdge;
					if (rightEdge.carryOut) {
						++visitCount;
						terminatePolygon = true;
						break;
					}
					rightPointOffset = rightEdge.pointOffsetOut;
					edgeState.rightX = rightEdge.currentX;
					edgeState.rightAccumulatedBaseDepthPerRow = rightEdge.accumulatedBaseDepthPerRow;
					edgeState.rightBaseDepthPerRow = rightEdge.baseDepthPerRow;
					edgeState.rightAccumulatedNextPointDepthPerRow = rightEdge.accumulatedNextPointDepthPerRow;
					edgeState.rightNextPointDepthPerRow = rightEdge.nextPointDepthPerRow;
					edgeState.rightXStep = rightEdge.xStep;
					edgeState.rightXFraction = rightEdge.xFraction;
					edgeState.rightRemaining = rightEdge.remaining;
					rightBaseDepth = rightEdge.rightBaseDepth;
					rightBaseTexU = rightEdge.rightBaseTexU;
					rightBaseTexV = rightEdge.rightBaseTexV;
				}

				if (!Raster_OpaqueSetupAndDispatchPerspectiveSpan(
				        state, &edgeState, leftPointOffset, rightPointOffset, leftBaseDepth, leftBaseTexU, leftBaseTexV,
				        rightBaseDepth, rightBaseTexU, rightBaseTexV, &spanLeftTexU, &spanLeftTexV, &spanLeftDepth,
				        &spanRightTexU, &spanRightTexV, &spanRightDepth, &spanCurrentRow, &spanNextRow, &spanPixelCount,
				        &previousLeftX, &previousRightX, &visit->spanSetup, &visit->spanDispatch,
				        &visit->spanPixelsWritten)) {
					result->failVisitIndexValid = true;
					result->failVisitIndex = visitCount;
					result->visitCount = visitCount + 1u;
					return 0;
				}
				++result->spanDispatchCount;
				result->pixelsWritten += visit->spanPixelsWritten;
				if ((visit->spanOverlapFlags & RASTER_SPAN_EXTENDS_LEFT) != 0u) {
					if (!Raster_ForwardCopy((uint8_t *)leftCopySource, (uint8_t *)leftCopyDestination, leftCopyCount,
					                        &visit->leftCopy)) {
						result->failVisitIndexValid = true;
						result->failVisitIndex = visitCount;
						result->visitCount = visitCount + 1u;
						return 0;
					}
					++result->rowCopyCount;
					if (leftCopyCount > 0) {
						result->rowCopyBytes += (size_t)leftCopyCount;
					}
				}
				if ((visit->spanOverlapFlags & RASTER_SPAN_EXTENDS_RIGHT) != 0u) {
					visit->copyRightExtension = true;
					if (!Raster_ForwardCopy((uint8_t *)rightCopySource, (uint8_t *)rightCopyDestination, rightCopyCount,
					                        &visit->rightCopy)) {
						result->failVisitIndexValid = true;
						result->failVisitIndex = visitCount;
						result->visitCount = visitCount + 1u;
						return 0;
					}
					++result->rowCopyCount;
					if (rightCopyCount > 0) {
						result->rowCopyBytes += (size_t)rightCopyCount;
					}
				}
				if (edgeState.scanY == state->bottomY) {
					++visitCount;
					terminatePolygon = true;
				}
				break;
			}
		} while (false);
		if (terminatePolygon) {
			break;
		}

		visit->calledAdvancePerspectiveTexturedEdges = true;
		if (!Raster_AdvancePerspectiveTexturedEdges(&edgeState, 0, 0, &advance)) {
			result->failVisitIndexValid = true;
			result->failVisitIndex = visitCount;
			result->visitCount = visitCount + 1u;
			return 0;
		}
		visit->advance = advance;
		edgeState = advance.out;
		oneRowFlag = advance.oneRowFlag;
		if (advance.calledStepLeftEdgeTexturedPerspective) {
			visit->calledStepLeftEdgeTexturedPerspective = true;
			if (!Raster_StepLeftEdgeTexturedPerspective(state->pointBuffer, state->pointBufferBase,
			                                            state->pointBufferEnd, state->pointBufferBytes, leftPointOffset,
			                                            edgeState.scanY, state->bottomY, &leftEdge)) {
				result->failVisitIndexValid = true;
				result->failVisitIndex = visitCount;
				result->visitCount = visitCount + 1u;
				return 0;
			}
			visit->leftEdge = leftEdge;
			if (leftEdge.carryOut) {
				++visitCount;
				break;
			}
			leftPointOffset = leftEdge.pointOffsetOut;
			edgeState.leftX = leftEdge.currentX;
			edgeState.leftAccumulatedBaseDepthPerRow = leftEdge.accumulatedBaseDepthPerRow;
			edgeState.leftBaseDepthPerRow = leftEdge.baseDepthPerRow;
			edgeState.leftAccumulatedNextPointDepthPerRow = leftEdge.accumulatedNextPointDepthPerRow;
			edgeState.leftNextPointDepthPerRow = leftEdge.nextPointDepthPerRow;
			edgeState.leftXStep = leftEdge.xStep;
			edgeState.leftXFraction = leftEdge.xFraction;
			edgeState.leftRemaining = leftEdge.remaining;
			leftBaseDepth = leftEdge.leftBaseDepth;
			leftBaseTexU = leftEdge.leftBaseTexU;
			leftBaseTexV = leftEdge.leftBaseTexV;
		}
		if (advance.calledStepRightEdgeTexturedPerspective) {
			visit->calledStepRightEdgeTexturedPerspective = true;
			if (!Raster_StepRightEdgeTexturedPerspective(
			        state->pointBuffer, state->pointBufferBase, state->pointBufferEnd, state->pointBufferBytes,
			        rightPointOffset, edgeState.scanY, state->bottomY, &rightEdge)) {
				result->failVisitIndexValid = true;
				result->failVisitIndex = visitCount;
				result->visitCount = visitCount + 1u;
				return 0;
			}
			visit->rightEdge = rightEdge;
			if (rightEdge.carryOut) {
				++visitCount;
				break;
			}
			rightPointOffset = rightEdge.pointOffsetOut;
			edgeState.rightX = rightEdge.currentX;
			edgeState.rightAccumulatedBaseDepthPerRow = rightEdge.accumulatedBaseDepthPerRow;
			edgeState.rightBaseDepthPerRow = rightEdge.baseDepthPerRow;
			edgeState.rightAccumulatedNextPointDepthPerRow = rightEdge.accumulatedNextPointDepthPerRow;
			edgeState.rightNextPointDepthPerRow = rightEdge.nextPointDepthPerRow;
			edgeState.rightXStep = rightEdge.xStep;
			edgeState.rightXFraction = rightEdge.xFraction;
			edgeState.rightRemaining = rightEdge.remaining;
			rightBaseDepth = rightEdge.rightBaseDepth;
			rightBaseTexU = rightEdge.rightBaseTexU;
			rightBaseTexV = rightEdge.rightBaseTexV;
		}
		if (edgeState.scanY >= state->bottomY) {
			visit->terminalDispatch = true;
			if (!Raster_DispatchPerspectiveTexturedSpanRows(
			        state->screenRows, state->screenRowCount, state->screenRowBytes, state->lockedPayload,
			        state->lockedPayloadBytes, state->textureRows, state->textureRowCount, state->textureRowBytes,
			        edgeState.scanY, edgeState.leftX, edgeState.rightX, spanLeftTexU, spanLeftTexV, spanRightTexU,
			        spanRightTexV, spanLeftDepth, spanRightDepth, &visit->terminalSpanDispatch)) {
				result->failVisitIndexValid = true;
				result->failVisitIndex = visitCount;
				result->visitCount = visitCount + 1u;
				return 0;
			}
			visit->spanPixelsWritten = visit->terminalSpanDispatch.jumpShortSpan
			                               ? visit->terminalSpanDispatch.shortSpanCore.pixelsWritten
			                               : visit->terminalSpanDispatch.longSpanPixelsWritten;
			result->pixelsWritten += visit->spanPixelsWritten;
			++result->spanDispatchCount;
			++visitCount;
			break;
		}
		++visitCount;
	}

	result->visitCount = visitCount;
	result->popad = true;
	return 1;
}

void Raster_Clear(uint8_t color, size_t byteCount) { memset(g_screenBufferBase, color, byteCount); }

const RasterDrawBackend RasterSoftware_backend = {
    .drawLineSolid = RasterSoftware_DrawLineSolid,
    .drawLineClipped = RasterSoftware_DrawLineClipped,
    .fillRectClipped = RasterSoftware_FillRectClipped,
    .fillRectUnchecked = RasterSoftware_FillRectUnchecked,
    .drawSolidFlatPolygon = RasterSoftware_DrawSolidFlatPolygon,
    .drawShadedFlatPolygon = RasterSoftware_DrawShadedFlatPolygon,
    .drawDitheredFlatPolygon = RasterSoftware_DrawDitheredFlatPolygon,
    .drawSpriteScaled = RasterSoftware_DrawSpriteScaled,
    .drawTexturedPolygon = RasterSoftware_DrawTexturedPolygon,
};
