#include "view3d.h"
#include "byte_order.h"
#include "fixed_point.h"

#include "draw3d.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum {
	SLIP_VIEW_WORD_TABLE_INDEX_MASK = UINT16_MAX & ~1u,
	SLIP_VIEW_SMALL_ANGLE_SINE_SCALE_Q14 = 0x6488,
	SLIP_VIEW_SMALL_ANGLE_LIMIT = 256,
	SLIP_VIEW_HEADING_DIVIDEND_TRUNCATION_BITS = 2,
	/* A Q14 ratio addresses the arctangent words at two-byte aligned offsets. */
	SLIP_VIEW_ARCTANGENT_BYTE_OFFSET_SHIFT = 1,
	SLIP_VIEW_LOCAL_VERTEX_SCALE_SHIFT = 6,
	SLIP_VIEW_TRANSFORMED_VERTEX_SHIFT = SLIP_Q14_FRACTION_BITS - SLIP_VIEW_LOCAL_VERTEX_SCALE_SHIFT,
	SLIP_VIEW_ASIN_DIAGONAL_LIMIT_Q14 = 0x2d42,
	SLIP_VIEW_HEADING_VERTICAL_LIMIT_Q14 = 0x3f00,
	SLIP_VIEW_ANGLE_COMPLETION_TOLERANCE = 0x40,
	SLIP_VIEW_ALIGNMENT_DOT_THRESHOLD_Q14 = SLIP_Q14_ONE - 2,
	SLIP_VIEW_UNIT_LENGTH_SQUARED_Q28 = SLIP_Q14_ONE * SLIP_Q14_ONE,
	SLIP_VIEW_WORD_SIGN_BIT = 1u << 15,
	SLIP_VIEW_DWORD_SHIFT_COUNT_MASK = 31,
	SLIP_VIEW_NORMALIZE_MAXIMUM_COMPONENT_BIT = 14,
	SLIP_VIEW_PACKED_Y_SIGN_MASK = UINT8_MAX,
	SLIP_VIEW_PACKED_Z_SIGN_SHIFT = 8,
	SLIP_VIEW_PACKED_Z_SIGN_MASK = UINT8_MAX << SLIP_VIEW_PACKED_Z_SIGN_SHIFT
};

/* Keep these masks unsigned, including when used in wider expressions. */
static const uint32_t SLIP_VIEW_DWORD_SIGN_BIT = UINT32_C(1) << 31;
static const uint32_t SLIP_VIEW_UPPER_WORD_MASK = UINT32_MAX ^ UINT16_MAX;
static const uint32_t SLIP_VIEW_NORMALIZE_HIGH_COMPONENT_MASK = UINT32_MAX ^ INT16_MAX;
static const uint32_t SLIP_VIEW_Q14_SIGN_EXTENSION_MASK = UINT32_MAX << SLIP_Q14_DWORD_HIGH_SHIFT;

static int16_t SlipView3D_LookupWord(const SlipView3DMaths *maths, uint16_t offset, uint16_t index) {
	const size_t byteOffset = (size_t)offset + (size_t)(index & SLIP_VIEW_WORD_TABLE_INDEX_MASK);

	if (byteOffset + 2u > maths->size) {
		return 0;
	}
	return (int16_t)SlipBytes_ReadLE16(maths->data + byteOffset);
}

static int32_t SlipView3D_DotProductMixedWidthsQ14(int16_t matrixX, int32_t vectorX, int16_t matrixY, int32_t vectorY,
                                                   int16_t matrixZ, int32_t vectorZ) {
	const int64_t productX = (int64_t)matrixX * vectorX;
	const int64_t productY = (int64_t)matrixY * vectorY;
	const int64_t productZ = (int64_t)matrixZ * vectorZ;
	uint32_t sumLow = (uint32_t)productZ;
	uint32_t sumHigh = (uint32_t)(productZ >> 32);
	uint32_t addendLow;
	uint32_t carry;
	uint16_t sumHighWord;

	addendLow = (uint32_t)productY;
	carry = UINT32_MAX - sumLow < addendLow;
	sumLow += addendLow;
	sumHighWord = (uint16_t)sumHigh;
	sumHighWord = (uint16_t)(sumHighWord + (uint16_t)(productY >> 32) + (uint16_t)carry);
	sumHigh = (sumHigh & SLIP_VIEW_UPPER_WORD_MASK) | sumHighWord;

	addendLow = (uint32_t)productX;
	carry = UINT32_MAX - sumLow < addendLow;
	sumLow += addendLow;
	sumHighWord = (uint16_t)sumHigh;
	sumHighWord = (uint16_t)(sumHighWord + (uint16_t)(productX >> 32) + (uint16_t)carry);
	sumHigh = (sumHigh & SLIP_VIEW_UPPER_WORD_MASK) | sumHighWord;

	return (int32_t)((sumLow >> SLIP_Q14_FRACTION_BITS) | (sumHigh << SLIP_Q14_DWORD_HIGH_SHIFT));
}

static int32_t SlipView3D_DotProductSignedWordsQ14(int16_t matrixX, int32_t vectorX, int16_t matrixY, int32_t vectorY,
                                                   int16_t matrixZ, int32_t vectorZ) {
	uint32_t sum = (uint32_t)((int32_t)matrixX * (int16_t)vectorX);

	sum += (uint32_t)((int32_t)matrixY * (int16_t)vectorY);
	sum += (uint32_t)((int32_t)matrixZ * (int16_t)vectorZ);
	return (int32_t)sum >> SLIP_Q14_FRACTION_BITS;
}

static int32_t SlipView3D_MultiplyWordByDwordQ14(int16_t axis, int32_t scale) {
	const int64_t product = (int64_t)axis * (int64_t)scale;
	const uint32_t productLow = (uint32_t)product;
	const uint32_t productHigh = (uint32_t)((uint64_t)product >> 32);

	return (int32_t)((productLow >> SLIP_Q14_FRACTION_BITS) | (productHigh << SLIP_Q14_DWORD_HIGH_SHIFT));
}

SlipView3DVec32 SlipView3D_ScaleVector(int16_t vectorXQ14, int16_t vectorYQ14, int16_t vectorZQ14, int32_t scaleQ14) {
	SlipView3DVec32 result;

	result.x = SlipView3D_MultiplyWordByDwordQ14(vectorXQ14, scaleQ14);
	result.y = SlipView3D_MultiplyWordByDwordQ14(vectorYQ14, scaleQ14);
	result.z = SlipView3D_MultiplyWordByDwordQ14(vectorZQ14, scaleQ14);
	return result;
}

static uint32_t SlipView3D_MultiplyDwordsQ14WithRoundingBit(int32_t lhs, int32_t rhs, uint32_t *carry) {
	const uint64_t product = (uint64_t)((int64_t)lhs * (int64_t)rhs);
	const uint32_t low = (uint32_t)product;
	const uint32_t high = (uint32_t)(product >> 32);

	*carry = (low >> (SLIP_Q14_FRACTION_BITS - 1)) & 1u;
	return (low >> SLIP_Q14_FRACTION_BITS) | (high << SLIP_Q14_DWORD_HIGH_SHIFT);
}

SlipView3DVec32 SlipView3D_ProjectPointToPlane(SlipView3DVec32 point, SlipView3DVec32 planeOrigin,
                                               SlipView3DVec32 planeNormal) {
	uint64_t dot;
	uint32_t dotLow;
	uint32_t dotHigh;
	uint32_t distance;
	int32_t negativeDistance;
	uint32_t carry;
	uint32_t scaled;
	SlipView3DVec32 result = point;

	dot = (uint64_t)((int64_t)(int32_t)((uint32_t)point.x - (uint32_t)planeOrigin.x) * (int64_t)planeNormal.x);
	dot += (uint64_t)((int64_t)(int32_t)((uint32_t)point.y - (uint32_t)planeOrigin.y) * (int64_t)planeNormal.y);
	dot += (uint64_t)((int64_t)(int32_t)((uint32_t)point.z - (uint32_t)planeOrigin.z) * (int64_t)planeNormal.z);
	dotLow = (uint32_t)dot;
	dotHigh = (uint32_t)(dot >> 32);
	distance = (dotLow >> SLIP_Q14_FRACTION_BITS) | (dotHigh << SLIP_Q14_DWORD_HIGH_SHIFT);
	distance += (dotLow >> (SLIP_Q14_FRACTION_BITS - 1)) & 1u;
	negativeDistance = (int32_t)(0u - distance);

	scaled = SlipView3D_MultiplyDwordsQ14WithRoundingBit(planeNormal.x, negativeDistance, &carry);
	result.x = (int32_t)((uint32_t)result.x + scaled + carry);
	scaled = SlipView3D_MultiplyDwordsQ14WithRoundingBit(planeNormal.y, negativeDistance, &carry);
	result.y = (int32_t)((uint32_t)result.y + scaled + carry);
	scaled = SlipView3D_MultiplyDwordsQ14WithRoundingBit(planeNormal.z, negativeDistance, &carry);
	result.z = (int32_t)((uint32_t)result.z + scaled + carry);
	return result;
}

static int16_t SlipView3D_MultiplySignedWordsQ14(int16_t value, int16_t scale) {
	const int32_t product = (int32_t)value * (int32_t)scale;
	const uint16_t productLow = (uint16_t)product;
	const uint16_t productHigh = (uint16_t)((uint32_t)product >> 16);

	return (int16_t)((uint16_t)(productLow >> SLIP_Q14_FRACTION_BITS) |
	                 (uint16_t)(productHigh << SLIP_Q14_WORD_HIGH_SHIFT));
}

uint32_t SlipView3D_DotProductQ14(uint16_t lhsXQ14, uint16_t lhsYQ14, uint16_t lhsZQ14, uint16_t rhsXQ14,
                                  uint16_t rhsYQ14, uint16_t rhsZQ14, SlipView3DDotProductQ14 *result) {
	const int32_t productX = (int32_t)(int16_t)lhsXQ14 * (int32_t)(int16_t)rhsXQ14;
	const int32_t productY = (int32_t)(int16_t)lhsYQ14 * (int32_t)(int16_t)rhsYQ14;
	const int32_t productZ = (int32_t)(int16_t)lhsZQ14 * (int32_t)(int16_t)rhsZQ14;
	const uint32_t sumXY = (uint32_t)productX + (uint32_t)productY;
	const uint32_t sumXYZ = sumXY + (uint32_t)productZ;
	const uint16_t sumLow = (uint16_t)sumXYZ;
	const uint16_t sumHigh = (uint16_t)(sumXYZ >> 16);
	uint16_t out =
	    (uint16_t)((uint16_t)(sumLow >> SLIP_Q14_FRACTION_BITS) | (uint16_t)(sumHigh << SLIP_Q14_WORD_HIGH_SHIFT));

	if ((sumLow & SLIP_Q14_HALF) != 0) {
		++out;
	}
	if (result != NULL) {
		*result = (SlipView3DDotProductQ14){.productXLow = (uint16_t)productX,
		                                    .productXHigh = (uint16_t)((uint32_t)productX >> 16),
		                                    .productYLow = (uint16_t)productY,
		                                    .productYHigh = (uint16_t)((uint32_t)productY >> 16),
		                                    .sumXY = sumXY,
		                                    .productZ = (uint32_t)productZ,
		                                    .sumXYZ = sumXYZ,
		                                    .dotProductBeforeRounding =
		                                        (uint16_t)((uint16_t)(sumLow >> SLIP_Q14_FRACTION_BITS) |
		                                                   (uint16_t)(sumHigh << SLIP_Q14_WORD_HIGH_SHIFT)),
		                                    .dotProductQ14 = (uint32_t)(int32_t)(int16_t)out,
		                                    .xySumHigh = (uint32_t)(int32_t)(int16_t)(sumXY >> 16),
		                                    .inputZ = lhsZQ14,
		                                    .xyzSumHigh = (uint32_t)(int32_t)(int16_t)(sumXYZ >> 16),
		                                    .operationCompleted = 1};
	}
	return (uint32_t)(int32_t)(int16_t)out;
}

static void SlipView3D_RotateRows12(SlipView3DMatrix *matrix, int16_t sine, int16_t cosine) {
	int16_t oldRow1;
	int16_t oldRow2;
	uint32_t rotatedComponentSum;

	oldRow1 = matrix->m[3];
	oldRow2 = matrix->m[6];
	rotatedComponentSum = (uint32_t)((int32_t)oldRow2 * cosine) + (uint32_t)((int32_t)oldRow1 * sine);
	matrix->m[6] = (int16_t)(rotatedComponentSum >> SLIP_Q14_FRACTION_BITS);
	rotatedComponentSum = (uint32_t)((int32_t)oldRow1 * cosine) - (uint32_t)((int32_t)oldRow2 * sine);
	matrix->m[3] = (int16_t)(rotatedComponentSum >> SLIP_Q14_FRACTION_BITS);

	oldRow1 = matrix->m[4];
	oldRow2 = matrix->m[7];
	rotatedComponentSum = (uint32_t)((int32_t)oldRow2 * cosine) + (uint32_t)((int32_t)oldRow1 * sine);
	matrix->m[7] = (int16_t)(rotatedComponentSum >> SLIP_Q14_FRACTION_BITS);
	rotatedComponentSum = (uint32_t)((int32_t)oldRow1 * cosine) - (uint32_t)((int32_t)oldRow2 * sine);
	matrix->m[4] = (int16_t)(rotatedComponentSum >> SLIP_Q14_FRACTION_BITS);

	oldRow1 = matrix->m[5];
	oldRow2 = matrix->m[8];
	rotatedComponentSum = (uint32_t)((int32_t)oldRow2 * cosine) + (uint32_t)((int32_t)oldRow1 * sine);
	matrix->m[8] = (int16_t)(rotatedComponentSum >> SLIP_Q14_FRACTION_BITS);
	rotatedComponentSum = (uint32_t)((int32_t)oldRow1 * cosine) - (uint32_t)((int32_t)oldRow2 * sine);
	matrix->m[5] = (int16_t)(rotatedComponentSum >> SLIP_Q14_FRACTION_BITS);
}

static void SlipView3D_RotateRows02(SlipView3DMatrix *matrix, int16_t sine, int16_t cosine) {
	int16_t oldRow0;
	int16_t oldRow2;
	uint32_t rotatedComponentSum;

	oldRow0 = matrix->m[0];
	oldRow2 = matrix->m[6];
	rotatedComponentSum = (uint32_t)((int32_t)oldRow0 * cosine) - (uint32_t)((int32_t)oldRow2 * sine);
	matrix->m[0] = (int16_t)(rotatedComponentSum >> SLIP_Q14_FRACTION_BITS);
	rotatedComponentSum = (uint32_t)((int32_t)oldRow2 * cosine) + (uint32_t)((int32_t)oldRow0 * sine);
	matrix->m[6] = (int16_t)(rotatedComponentSum >> SLIP_Q14_FRACTION_BITS);

	oldRow0 = matrix->m[1];
	oldRow2 = matrix->m[7];
	rotatedComponentSum = (uint32_t)((int32_t)oldRow0 * cosine) - (uint32_t)((int32_t)oldRow2 * sine);
	matrix->m[1] = (int16_t)(rotatedComponentSum >> SLIP_Q14_FRACTION_BITS);
	rotatedComponentSum = (uint32_t)((int32_t)oldRow2 * cosine) + (uint32_t)((int32_t)oldRow0 * sine);
	matrix->m[7] = (int16_t)(rotatedComponentSum >> SLIP_Q14_FRACTION_BITS);

	oldRow0 = matrix->m[2];
	oldRow2 = matrix->m[8];
	rotatedComponentSum = (uint32_t)((int32_t)oldRow0 * cosine) - (uint32_t)((int32_t)oldRow2 * sine);
	matrix->m[2] = (int16_t)(rotatedComponentSum >> SLIP_Q14_FRACTION_BITS);
	rotatedComponentSum = (uint32_t)((int32_t)oldRow2 * cosine) + (uint32_t)((int32_t)oldRow0 * sine);
	matrix->m[8] = (int16_t)(rotatedComponentSum >> SLIP_Q14_FRACTION_BITS);
}

static void SlipView3D_RotateRows01(SlipView3DMatrix *matrix, int16_t sine, int16_t cosine) {
	int16_t oldRow0;
	int16_t oldRow1;
	uint32_t rotatedComponentSum;

	oldRow0 = matrix->m[0];
	oldRow1 = matrix->m[3];
	rotatedComponentSum = (uint32_t)((int32_t)oldRow0 * cosine) - (uint32_t)((int32_t)oldRow1 * sine);
	matrix->m[0] = (int16_t)(rotatedComponentSum >> SLIP_Q14_FRACTION_BITS);
	rotatedComponentSum = (uint32_t)((int32_t)oldRow1 * cosine) + (uint32_t)((int32_t)oldRow0 * sine);
	matrix->m[3] = (int16_t)(rotatedComponentSum >> SLIP_Q14_FRACTION_BITS);

	oldRow0 = matrix->m[1];
	oldRow1 = matrix->m[4];
	rotatedComponentSum = (uint32_t)((int32_t)oldRow0 * cosine) - (uint32_t)((int32_t)oldRow1 * sine);
	matrix->m[1] = (int16_t)(rotatedComponentSum >> SLIP_Q14_FRACTION_BITS);
	rotatedComponentSum = (uint32_t)((int32_t)oldRow1 * cosine) + (uint32_t)((int32_t)oldRow0 * sine);
	matrix->m[4] = (int16_t)(rotatedComponentSum >> SLIP_Q14_FRACTION_BITS);

	oldRow0 = matrix->m[2];
	oldRow1 = matrix->m[5];
	rotatedComponentSum = (uint32_t)((int32_t)oldRow0 * cosine) - (uint32_t)((int32_t)oldRow1 * sine);
	matrix->m[2] = (int16_t)(rotatedComponentSum >> SLIP_Q14_FRACTION_BITS);
	rotatedComponentSum = (uint32_t)((int32_t)oldRow1 * cosine) + (uint32_t)((int32_t)oldRow0 * sine);
	matrix->m[5] = (int16_t)(rotatedComponentSum >> SLIP_Q14_FRACTION_BITS);
}

static void SlipView3D_RotateColumns02(SlipView3DMatrix *matrix, int16_t sine, int16_t cosine) {
	int16_t oldColumn0;
	int16_t oldColumn2;
	uint32_t rotatedComponentSum;

	oldColumn0 = matrix->m[0];
	oldColumn2 = matrix->m[2];
	rotatedComponentSum = (uint32_t)((int32_t)oldColumn0 * cosine) + (uint32_t)((int32_t)oldColumn2 * sine);
	matrix->m[0] = (int16_t)(rotatedComponentSum >> SLIP_Q14_FRACTION_BITS);
	rotatedComponentSum = (uint32_t)((int32_t)oldColumn2 * cosine) - (uint32_t)((int32_t)oldColumn0 * sine);
	matrix->m[2] = (int16_t)(rotatedComponentSum >> SLIP_Q14_FRACTION_BITS);

	oldColumn0 = matrix->m[3];
	oldColumn2 = matrix->m[5];
	rotatedComponentSum = (uint32_t)((int32_t)oldColumn0 * cosine) + (uint32_t)((int32_t)oldColumn2 * sine);
	matrix->m[3] = (int16_t)(rotatedComponentSum >> SLIP_Q14_FRACTION_BITS);
	rotatedComponentSum = (uint32_t)((int32_t)oldColumn2 * cosine) - (uint32_t)((int32_t)oldColumn0 * sine);
	matrix->m[5] = (int16_t)(rotatedComponentSum >> SLIP_Q14_FRACTION_BITS);

	oldColumn0 = matrix->m[6];
	oldColumn2 = matrix->m[8];
	rotatedComponentSum = (uint32_t)((int32_t)oldColumn0 * cosine) + (uint32_t)((int32_t)oldColumn2 * sine);
	matrix->m[6] = (int16_t)(rotatedComponentSum >> SLIP_Q14_FRACTION_BITS);
	rotatedComponentSum = (uint32_t)((int32_t)oldColumn2 * cosine) - (uint32_t)((int32_t)oldColumn0 * sine);
	matrix->m[8] = (int16_t)(rotatedComponentSum >> SLIP_Q14_FRACTION_BITS);
}

static void SlipView3D_NormalizeVec(int16_t *x, int16_t *y, int16_t *z) {
	SlipView3DNormalizeLength3D length;

	if (!SlipView3D_NormalizeLength3D((uint32_t)(uint16_t)*x, (uint32_t)(uint16_t)*y, (uint32_t)(uint16_t)*z,
	                                  &length)) {
		return;
	}
	*x = (int16_t)(uint16_t)length.unitXQ14;
	*y = (int16_t)(uint16_t)length.unitYQ14;
	*z = (int16_t)(uint16_t)length.unitZQ14;
}

void SlipView3D_FreeMaths(SlipView3DMaths *maths) {
	if (maths != NULL) {
		free(maths->data);
		maths->data = NULL;
		maths->size = 0;
		maths->arctangentTableOffset = 0;
		maths->sineTableOffset = 0;
		maths->arcsineTableOffset = 0;
	}
}

int SlipView3D_InitMathsFromPayload(SlipView3DMaths *maths, const uint8_t *data, size_t size) {
	if (maths == NULL || data == NULL || size < SLIP_MATHS_HEADER_BYTES) {
		return 0;
	}

	memset(maths, 0, sizeof(*maths));
	maths->data = (uint8_t *)malloc(size);
	if (maths->data == NULL) {
		return 0;
	}
	memcpy(maths->data, data, size);
	maths->size = size;
	maths->sineTableOffset = SlipBytes_ReadLE16(data + SLIP_MATHS_SINE_TABLE_OFFSET);
	maths->arcsineTableOffset = SlipBytes_ReadLE16(data + SLIP_MATHS_ARCSINE_TABLE_OFFSET);
	maths->arctangentTableOffset = SlipBytes_ReadLE16(data + SLIP_MATHS_ARCTANGENT_TABLE_OFFSET);
	return 1;
}

int16_t SlipView3D_SinQ14(const SlipView3DMaths *maths, int16_t angle) {
	uint16_t foldedAngle = (uint16_t)angle;
	int negate = 0;
	int16_t sine;

	if ((int16_t)foldedAngle < 0) {
		foldedAngle = (uint16_t)(-(int16_t)foldedAngle);
		negate = 1;
	}
	if (foldedAngle >= SLIP_ANGLE_QUARTER_TURN) {
		foldedAngle = (uint16_t)(SLIP_ANGLE_HALF_TURN - foldedAngle);
	}

	sine = SlipView3D_LookupSineQ14(maths, foldedAngle);
	return negate ? (int16_t)-sine : sine;
}

int16_t SlipView3D_CosQ14(const SlipView3DMaths *maths, int16_t angle) {
	return SlipView3D_SinQ14(maths, (int16_t)((uint16_t)angle + SLIP_ANGLE_QUARTER_TURN));
}

int16_t SlipView3D_SmallAngleSinQ14(int16_t angle) {
	const uint32_t angleScaleProduct = (uint32_t)((int32_t)angle * SLIP_VIEW_SMALL_ANGLE_SINE_SCALE_Q14);

	return (int16_t)(angleScaleProduct >> SLIP_Q14_FRACTION_BITS);
}

int16_t SlipView3D_LookupSineQ14(const SlipView3DMaths *maths, uint16_t index) {
	return SlipView3D_LookupWord(maths, maths->sineTableOffset, index);
}

int16_t SlipView3D_LookupArcsineAngle(const SlipView3DMaths *maths, uint16_t index) {
	return SlipView3D_LookupWord(maths, maths->arcsineTableOffset, index);
}

int16_t SlipView3D_TanQ14(const SlipView3DMaths *maths, int16_t angle) {
	const uint16_t angleBits = (uint16_t)angle;
	int16_t numerator;
	int16_t denominator;
	int32_t dividend;

	if (angleBits == SLIP_ANGLE_EIGHTH_TURN) {
		return SLIP_Q14_ONE;
	}

	numerator = SlipView3D_LookupSineQ14(maths, angleBits);
	denominator = SlipView3D_LookupSineQ14(maths, (uint16_t)(SLIP_ANGLE_QUARTER_TURN - angleBits));
	if (denominator == 0) {
		return numerator < 0 ? INT16_MIN : INT16_MAX;
	}

	dividend = (int32_t)numerator << SLIP_Q14_FRACTION_BITS;
	return (int16_t)(dividend / denominator);
}

void SlipView3D_BuildYawMatrix(const SlipView3DMaths *maths, int16_t angle, SlipView3DMatrix *matrix) {
	const int16_t c = SlipView3D_CosQ14(maths, angle);
	const int16_t s = SlipView3D_SinQ14(maths, angle);

	matrix->m[0] = c;
	matrix->m[1] = 0;
	matrix->m[2] = (int16_t)-s;
	matrix->m[3] = 0;
	matrix->m[4] = SLIP_Q14_ONE;
	matrix->m[5] = 0;
	matrix->m[6] = s;
	matrix->m[7] = 0;
	matrix->m[8] = c;
}

void SlipView3D_ApplyPitchMatrix(const SlipView3DMaths *maths, int16_t angle, SlipView3DMatrix *matrix) {
	int16_t sine;
	int16_t cosine;
	int16_t oldRow1;
	int16_t oldRow2;
	uint32_t sineTimesComponent;

	if (angle == 0) {
		return;
	}

	if (angle >= -SLIP_VIEW_SMALL_ANGLE_LIMIT && angle <= SLIP_VIEW_SMALL_ANGLE_LIMIT) {
		sine = SlipView3D_SmallAngleSinQ14(angle);

		oldRow1 = matrix->m[3];
		oldRow2 = matrix->m[6];
		sineTimesComponent = (uint32_t)((int32_t)oldRow1 * sine);
		matrix->m[6] =
		    (int16_t)(uint16_t)((uint16_t)oldRow2 + (uint16_t)(sineTimesComponent >> SLIP_Q14_FRACTION_BITS));
		sineTimesComponent = (uint32_t)((int32_t)oldRow2 * sine);
		matrix->m[3] =
		    (int16_t)(uint16_t)((uint16_t)oldRow1 - (uint16_t)(sineTimesComponent >> SLIP_Q14_FRACTION_BITS));

		oldRow1 = matrix->m[4];
		oldRow2 = matrix->m[7];
		sineTimesComponent = (uint32_t)((int32_t)oldRow1 * sine);
		matrix->m[7] =
		    (int16_t)(uint16_t)((uint16_t)oldRow2 + (uint16_t)(sineTimesComponent >> SLIP_Q14_FRACTION_BITS));
		sineTimesComponent = (uint32_t)((int32_t)oldRow2 * sine);
		matrix->m[4] =
		    (int16_t)(uint16_t)((uint16_t)oldRow1 - (uint16_t)(sineTimesComponent >> SLIP_Q14_FRACTION_BITS));

		oldRow1 = matrix->m[5];
		oldRow2 = matrix->m[8];
		sineTimesComponent = (uint32_t)((int32_t)oldRow1 * sine);
		matrix->m[8] =
		    (int16_t)(uint16_t)((uint16_t)oldRow2 + (uint16_t)(sineTimesComponent >> SLIP_Q14_FRACTION_BITS));
		sineTimesComponent = (uint32_t)((int32_t)oldRow2 * sine);
		matrix->m[5] =
		    (int16_t)(uint16_t)((uint16_t)oldRow1 - (uint16_t)(sineTimesComponent >> SLIP_Q14_FRACTION_BITS));
		return;
	}

	sine = SlipView3D_SinQ14(maths, angle);
	cosine = SlipView3D_CosQ14(maths, angle);
	SlipView3D_RotateRows12(matrix, sine, cosine);
}

void SlipView3D_ApplyRow0Row2Rotation(const SlipView3DMaths *maths, int16_t angle, SlipView3DMatrix *matrix) {
	int16_t sine;
	int16_t cosine;
	int16_t oldRow0;
	int16_t oldRow2;
	uint32_t sineTimesComponent;

	if (angle == 0) {
		return;
	}

	if (angle >= -SLIP_VIEW_SMALL_ANGLE_LIMIT && angle <= SLIP_VIEW_SMALL_ANGLE_LIMIT) {
		sine = SlipView3D_SmallAngleSinQ14(angle);

		oldRow0 = matrix->m[0];
		oldRow2 = matrix->m[6];
		sineTimesComponent = (uint32_t)((int32_t)oldRow2 * sine);
		matrix->m[0] =
		    (int16_t)(uint16_t)((uint16_t)oldRow0 - (uint16_t)(sineTimesComponent >> SLIP_Q14_FRACTION_BITS));
		sineTimesComponent = (uint32_t)((int32_t)oldRow0 * sine);
		matrix->m[6] =
		    (int16_t)(uint16_t)((uint16_t)oldRow2 + (uint16_t)(sineTimesComponent >> SLIP_Q14_FRACTION_BITS));

		oldRow0 = matrix->m[1];
		oldRow2 = matrix->m[7];
		sineTimesComponent = (uint32_t)((int32_t)oldRow2 * sine);
		matrix->m[1] =
		    (int16_t)(uint16_t)((uint16_t)oldRow0 - (uint16_t)(sineTimesComponent >> SLIP_Q14_FRACTION_BITS));
		sineTimesComponent = (uint32_t)((int32_t)oldRow0 * sine);
		matrix->m[7] =
		    (int16_t)(uint16_t)((uint16_t)oldRow2 + (uint16_t)(sineTimesComponent >> SLIP_Q14_FRACTION_BITS));

		oldRow0 = matrix->m[2];
		oldRow2 = matrix->m[8];
		sineTimesComponent = (uint32_t)((int32_t)oldRow2 * sine);
		matrix->m[2] =
		    (int16_t)(uint16_t)((uint16_t)oldRow0 - (uint16_t)(sineTimesComponent >> SLIP_Q14_FRACTION_BITS));
		sineTimesComponent = (uint32_t)((int32_t)oldRow0 * sine);
		matrix->m[8] =
		    (int16_t)(uint16_t)((uint16_t)oldRow2 + (uint16_t)(sineTimesComponent >> SLIP_Q14_FRACTION_BITS));
		return;
	}

	sine = SlipView3D_SinQ14(maths, angle);
	cosine = SlipView3D_CosQ14(maths, angle);
	SlipView3D_RotateRows02(matrix, sine, cosine);
}

void SlipView3D_ApplyRow0Row1Rotation(const SlipView3DMaths *maths, int16_t angle, SlipView3DMatrix *matrix) {
	int16_t sine;
	int16_t cosine;
	int16_t oldRow0;
	int16_t oldRow1;
	uint32_t sineTimesComponent;

	if (angle == 0) {
		return;
	}

	if (angle >= -SLIP_VIEW_SMALL_ANGLE_LIMIT && angle <= SLIP_VIEW_SMALL_ANGLE_LIMIT) {
		sine = SlipView3D_SmallAngleSinQ14(angle);

		oldRow0 = matrix->m[0];
		oldRow1 = matrix->m[3];
		sineTimesComponent = (uint32_t)((int32_t)oldRow1 * sine);
		matrix->m[0] =
		    (int16_t)(uint16_t)((uint16_t)oldRow0 - (uint16_t)(sineTimesComponent >> SLIP_Q14_FRACTION_BITS));
		sineTimesComponent = (uint32_t)((int32_t)oldRow0 * sine);
		matrix->m[3] =
		    (int16_t)(uint16_t)((uint16_t)oldRow1 + (uint16_t)(sineTimesComponent >> SLIP_Q14_FRACTION_BITS));

		oldRow0 = matrix->m[1];
		oldRow1 = matrix->m[4];
		sineTimesComponent = (uint32_t)((int32_t)oldRow1 * sine);
		matrix->m[1] =
		    (int16_t)(uint16_t)((uint16_t)oldRow0 - (uint16_t)(sineTimesComponent >> SLIP_Q14_FRACTION_BITS));
		sineTimesComponent = (uint32_t)((int32_t)oldRow0 * sine);
		matrix->m[4] =
		    (int16_t)(uint16_t)((uint16_t)oldRow1 + (uint16_t)(sineTimesComponent >> SLIP_Q14_FRACTION_BITS));

		oldRow0 = matrix->m[2];
		oldRow1 = matrix->m[5];
		sineTimesComponent = (uint32_t)((int32_t)oldRow1 * sine);
		matrix->m[2] =
		    (int16_t)(uint16_t)((uint16_t)oldRow0 - (uint16_t)(sineTimesComponent >> SLIP_Q14_FRACTION_BITS));
		sineTimesComponent = (uint32_t)((int32_t)oldRow0 * sine);
		matrix->m[5] =
		    (int16_t)(uint16_t)((uint16_t)oldRow1 + (uint16_t)(sineTimesComponent >> SLIP_Q14_FRACTION_BITS));
		return;
	}

	sine = SlipView3D_SinQ14(maths, angle);
	cosine = SlipView3D_CosQ14(maths, angle);
	SlipView3D_RotateRows01(matrix, sine, cosine);
}

void SlipView3D_ApplyColumn0Column2Rotation(const SlipView3DMaths *maths, int16_t angle, SlipView3DMatrix *matrix) {
	int16_t sine;
	int16_t cosine;
	int16_t oldColumn0;
	int16_t oldColumn2;
	uint32_t sineTimesComponent;

	if (angle == 0) {
		return;
	}

	if (angle >= -SLIP_VIEW_SMALL_ANGLE_LIMIT && angle <= SLIP_VIEW_SMALL_ANGLE_LIMIT) {
		sine = SlipView3D_SmallAngleSinQ14(angle);

		oldColumn0 = matrix->m[0];
		oldColumn2 = matrix->m[2];
		sineTimesComponent = (uint32_t)((int32_t)oldColumn2 * sine);
		matrix->m[0] =
		    (int16_t)(uint16_t)((uint16_t)oldColumn0 + (uint16_t)(sineTimesComponent >> SLIP_Q14_FRACTION_BITS));
		sineTimesComponent = (uint32_t)((int32_t)oldColumn0 * sine);
		matrix->m[2] =
		    (int16_t)(uint16_t)((uint16_t)oldColumn2 - (uint16_t)(sineTimesComponent >> SLIP_Q14_FRACTION_BITS));

		oldColumn0 = matrix->m[3];
		oldColumn2 = matrix->m[5];
		sineTimesComponent = (uint32_t)((int32_t)oldColumn2 * sine);
		matrix->m[3] =
		    (int16_t)(uint16_t)((uint16_t)oldColumn0 + (uint16_t)(sineTimesComponent >> SLIP_Q14_FRACTION_BITS));
		sineTimesComponent = (uint32_t)((int32_t)oldColumn0 * sine);
		matrix->m[5] =
		    (int16_t)(uint16_t)((uint16_t)oldColumn2 - (uint16_t)(sineTimesComponent >> SLIP_Q14_FRACTION_BITS));

		oldColumn0 = matrix->m[6];
		oldColumn2 = matrix->m[8];
		sineTimesComponent = (uint32_t)((int32_t)oldColumn2 * sine);
		matrix->m[6] =
		    (int16_t)(uint16_t)((uint16_t)oldColumn0 + (uint16_t)(sineTimesComponent >> SLIP_Q14_FRACTION_BITS));
		sineTimesComponent = (uint32_t)((int32_t)oldColumn0 * sine);
		matrix->m[8] =
		    (int16_t)(uint16_t)((uint16_t)oldColumn2 - (uint16_t)(sineTimesComponent >> SLIP_Q14_FRACTION_BITS));
		return;
	}

	sine = SlipView3D_SinQ14(maths, angle);
	cosine = SlipView3D_CosQ14(maths, angle);
	SlipView3D_RotateColumns02(matrix, sine, cosine);
}

void SlipView3D_OrthonormalizeForwardBasis(SlipView3DMatrix *matrix) {
	int32_t dot;
	int16_t rightOrUpX;
	int16_t rightOrUpY;
	int16_t rightOrUpZ;
	int16_t forwardX;
	int16_t forwardY;
	int16_t forwardZ;
	int32_t crossX;
	int32_t crossY;
	int32_t crossZ;
	uint32_t forwardProjectionProduct;

	if (matrix == NULL) {
		return;
	}

	forwardX = matrix->m[6];
	forwardY = matrix->m[7];
	forwardZ = matrix->m[8];
	SlipView3D_NormalizeVec(&forwardX, &forwardY, &forwardZ);
	matrix->m[6] = forwardX;
	matrix->m[7] = forwardY;
	matrix->m[8] = forwardZ;

	rightOrUpX = matrix->m[0];
	rightOrUpY = matrix->m[1];
	rightOrUpZ = matrix->m[2];
	dot = (int32_t)(int16_t)SlipView3D_DotProductQ14((uint16_t)forwardX, (uint16_t)forwardY, (uint16_t)forwardZ,
	                                                 (uint16_t)rightOrUpX, (uint16_t)rightOrUpY, (uint16_t)rightOrUpZ,
	                                                 NULL);
	if ((int16_t)dot != 0) {
		forwardProjectionProduct = (uint32_t)(dot * forwardX);
		rightOrUpX =
		    (int16_t)(uint16_t)((uint16_t)rightOrUpX - (uint16_t)(forwardProjectionProduct >> SLIP_Q14_FRACTION_BITS));
		forwardProjectionProduct = (uint32_t)(dot * forwardY);
		rightOrUpY =
		    (int16_t)(uint16_t)((uint16_t)rightOrUpY - (uint16_t)(forwardProjectionProduct >> SLIP_Q14_FRACTION_BITS));
		forwardProjectionProduct = (uint32_t)(dot * forwardZ);
		rightOrUpZ =
		    (int16_t)(uint16_t)((uint16_t)rightOrUpZ - (uint16_t)(forwardProjectionProduct >> SLIP_Q14_FRACTION_BITS));
	}
	SlipView3D_NormalizeVec(&rightOrUpX, &rightOrUpY, &rightOrUpZ);
	matrix->m[0] = rightOrUpX;
	matrix->m[1] = rightOrUpY;
	matrix->m[2] = rightOrUpZ;

	crossX = (int32_t)rightOrUpZ * forwardY - (int32_t)rightOrUpY * forwardZ;
	crossY = (int32_t)rightOrUpX * forwardZ - (int32_t)rightOrUpZ * forwardX;
	crossZ = (int32_t)rightOrUpY * forwardX - (int32_t)rightOrUpX * forwardY;
	rightOrUpX = (int16_t)(crossX >> SLIP_Q14_FRACTION_BITS);
	rightOrUpY = (int16_t)(crossY >> SLIP_Q14_FRACTION_BITS);
	rightOrUpZ = (int16_t)(crossZ >> SLIP_Q14_FRACTION_BITS);
	SlipView3D_NormalizeVec(&rightOrUpX, &rightOrUpY, &rightOrUpZ);
	matrix->m[3] = rightOrUpX;
	matrix->m[4] = rightOrUpY;
	matrix->m[5] = rightOrUpZ;
}

void SlipView3D_OrthonormalizeRightBasis(SlipView3DMatrix *matrix) {
	int32_t dot;
	int16_t rightOrUpX;
	int16_t rightOrUpY;
	int16_t rightOrUpZ;
	int16_t forwardX;
	int16_t forwardY;
	int16_t forwardZ;
	int32_t crossX;
	int32_t crossY;
	int32_t crossZ;

	if (matrix == NULL) {
		return;
	}

	rightOrUpX = matrix->m[0];
	rightOrUpY = matrix->m[1];
	rightOrUpZ = matrix->m[2];
	SlipView3D_NormalizeVec(&rightOrUpX, &rightOrUpY, &rightOrUpZ);
	matrix->m[0] = rightOrUpX;
	matrix->m[1] = rightOrUpY;
	matrix->m[2] = rightOrUpZ;

	forwardX = matrix->m[6];
	forwardY = matrix->m[7];
	forwardZ = matrix->m[8];
	dot = (int32_t)(int16_t)SlipView3D_DotProductQ14((uint16_t)rightOrUpX, (uint16_t)rightOrUpY, (uint16_t)rightOrUpZ,
	                                                 (uint16_t)forwardX, (uint16_t)forwardY, (uint16_t)forwardZ, NULL);
	if ((int16_t)dot != 0) {
		int component;
		int16_t rightAxis[3] = {rightOrUpX, rightOrUpY, rightOrUpZ};

		for (component = 0; component < 3; ++component) {
			const int32_t product = (int32_t)(int16_t)dot * rightAxis[component];
			const uint16_t shifted = (uint16_t)((uint32_t)product >> SLIP_Q14_FRACTION_BITS);
			const uint16_t carry = (uint16_t)(((uint32_t)product >> (SLIP_Q14_FRACTION_BITS - 1)) & 1u);

			matrix->m[6 + component] = (int16_t)(uint16_t)((uint16_t)matrix->m[6 + component] - shifted - carry);
		}
		forwardX = matrix->m[6];
		forwardY = matrix->m[7];
		forwardZ = matrix->m[8];
	}
	SlipView3D_NormalizeVec(&forwardX, &forwardY, &forwardZ);
	matrix->m[6] = forwardX;
	matrix->m[7] = forwardY;
	matrix->m[8] = forwardZ;

	crossX = (int32_t)rightOrUpZ * forwardY - (int32_t)rightOrUpY * forwardZ;
	crossY = (int32_t)rightOrUpX * forwardZ - (int32_t)rightOrUpZ * forwardX;
	crossZ = (int32_t)rightOrUpY * forwardX - (int32_t)rightOrUpX * forwardY;
	rightOrUpX = (int16_t)(crossX >> SLIP_Q14_FRACTION_BITS);
	rightOrUpY = (int16_t)(crossY >> SLIP_Q14_FRACTION_BITS);
	rightOrUpZ = (int16_t)(crossZ >> SLIP_Q14_FRACTION_BITS);
	SlipView3D_NormalizeVec(&rightOrUpX, &rightOrUpY, &rightOrUpZ);
	matrix->m[3] = rightOrUpX;
	matrix->m[4] = rightOrUpY;
	matrix->m[5] = rightOrUpZ;
}

uint32_t SlipView3D_ApproximateLength(int32_t x, int32_t y, int32_t z) {
	uint32_t largest = x < 0 ? 0u - (uint32_t)x : (uint32_t)x;
	uint32_t second = y < 0 ? 0u - (uint32_t)y : (uint32_t)y;
	uint32_t third = z < 0 ? 0u - (uint32_t)z : (uint32_t)z;
	if (largest <= second) {
		const uint32_t exchanged = largest;
		largest = second;
		second = exchanged;
	}
	if (largest <= third) {
		const uint32_t exchanged = largest;
		largest = third;
		third = exchanged;
	}
	return largest + (uint32_t)((int32_t)(second + third) >> 2);
}

uint32_t SlipView3D_VectorLength(int32_t vectorX, int32_t vectorY, int32_t vectorZ) {
	int64_t componentSquare = (int64_t)vectorX * vectorX;
	const uint32_t squareXLow = (uint32_t)componentSquare;
	const uint32_t squareXHigh = (uint32_t)((uint64_t)componentSquare >> 32);
	uint32_t squareSumLow;
	uint32_t squareSumHigh;
	uint32_t addend;
	uint32_t carry;

	componentSquare = (int64_t)vectorY * vectorY;
	addend = (uint32_t)componentSquare;
	squareSumHigh = (uint32_t)((uint64_t)componentSquare >> 32);
	componentSquare = (int64_t)vectorZ * vectorZ;
	squareSumLow = (uint32_t)componentSquare;
	carry = ((uint32_t)(squareSumLow + addend) < squareSumLow) ? 1u : 0u;
	squareSumLow += addend;
	squareSumHigh += (uint32_t)((uint64_t)componentSquare >> 32) + carry;
	addend = squareXLow;
	carry = ((uint32_t)(squareSumLow + addend) < squareSumLow) ? 1u : 0u;
	squareSumLow += addend;
	squareSumHigh += squareXHigh + carry;
	return SlipDraw3D_Root64(squareSumLow, squareSumHigh);
}

uint32_t SlipView3D_BoxRadius(int32_t minX, int32_t minY, int32_t minZ, int32_t maxX, int32_t maxY, int32_t maxZ) {
	const int32_t cornerMinX = minX;
	const int32_t cornerMinY = minY;
	const int32_t cornerMinZ = minZ;
	const int32_t cornerMaxX = maxX;
	const int32_t cornerMaxY = maxY;
	const int32_t cornerMaxZ = maxZ;
	uint32_t cornerRadius;
	uint32_t maximum;

	maximum = SlipView3D_VectorLength(cornerMinX, cornerMinY, cornerMinZ);
	cornerRadius = SlipView3D_VectorLength(cornerMinX, cornerMinY, cornerMaxZ);
	if (cornerRadius > maximum)
		maximum = cornerRadius;
	cornerRadius = SlipView3D_VectorLength(cornerMaxX, cornerMinY, cornerMaxZ);
	if (cornerRadius > maximum)
		maximum = cornerRadius;
	cornerRadius = SlipView3D_VectorLength(cornerMaxX, cornerMinY, cornerMinZ);
	if (cornerRadius > maximum)
		maximum = cornerRadius;
	cornerRadius = SlipView3D_VectorLength(cornerMinX, cornerMaxY, cornerMaxZ);
	if (cornerRadius > maximum)
		maximum = cornerRadius;
	cornerRadius = SlipView3D_VectorLength(cornerMinX, cornerMaxY, cornerMaxZ);
	if (cornerRadius > maximum)
		maximum = cornerRadius;
	cornerRadius = SlipView3D_VectorLength(cornerMaxX, cornerMaxY, cornerMaxZ);
	if (cornerRadius > maximum)
		maximum = cornerRadius;
	cornerRadius = SlipView3D_VectorLength(cornerMaxX, cornerMaxY, cornerMinZ);
	if (cornerRadius > maximum)
		maximum = cornerRadius;
	return maximum;
}

int16_t SlipView3D_HeadingFromMatrix(const SlipView3DMaths *maths, const SlipView3DMatrix *matrix) {
	int16_t x;
	int16_t z;
	uint16_t absX;
	uint16_t absZ;
	uint16_t angle;
	uint16_t ratio;

	if (maths == NULL || matrix == NULL) {
		return 0;
	}
	x = matrix->m[1];
	z = matrix->m[4];
	if (z == 0) {
		angle = SLIP_ANGLE_QUARTER_TURN;
	} else {
		absZ = z < 0 ? (uint16_t)(-(uint16_t)z) : (uint16_t)z;
		absX = x < 0 ? (uint16_t)(-(uint16_t)x) : (uint16_t)x;
		if (absX == absZ) {
			angle = SLIP_ANGLE_EIGHTH_TURN;
		} else if (absX < absZ) {
			ratio = (uint16_t)(((uint32_t)absX << SLIP_Q14_FRACTION_BITS) / absZ);
			angle = (uint16_t)SlipView3D_LookupWord(
			    maths, maths->arctangentTableOffset,
			    (uint16_t)((ratio >> SLIP_VIEW_ARCTANGENT_BYTE_OFFSET_SHIFT) & SLIP_VIEW_WORD_TABLE_INDEX_MASK));
		} else {
			/* Preserve the original numerator truncation before forming the Q14 ratio. */
			ratio = (uint16_t)(((uint32_t)(absZ >> SLIP_VIEW_HEADING_DIVIDEND_TRUNCATION_BITS)
			                    << (SLIP_Q14_FRACTION_BITS + SLIP_VIEW_HEADING_DIVIDEND_TRUNCATION_BITS)) /
			                   absX);
			angle = (uint16_t)SlipView3D_LookupWord(
			    maths, maths->arctangentTableOffset,
			    (uint16_t)((ratio >> SLIP_VIEW_ARCTANGENT_BYTE_OFFSET_SHIFT) & SLIP_VIEW_WORD_TABLE_INDEX_MASK));
			angle = (uint16_t)((angle ^ (SLIP_ANGLE_QUARTER_TURN - 1u)) + 1u);
		}
	}
	if (x >= 0) {
		angle = (uint16_t)(0u - angle);
	}
	if (z < 0) {
		angle = (uint16_t)(SLIP_ANGLE_HALF_TURN - angle);
	}
	return (int16_t)angle;
}

static int16_t SlipView3D_AsinQ14(const SlipView3DMaths *maths, int16_t value) {
	const int16_t original = value;
	const uint16_t magnitude = value < 0 ? (uint16_t)(0u - (uint16_t)value) : (uint16_t)value;
	uint16_t angle;

	if (magnitude <= SLIP_VIEW_ASIN_DIAGONAL_LIMIT_Q14) {
		angle = (uint16_t)SlipView3D_LookupArcsineAngle(maths, magnitude);
	} else {
		const uint32_t square = (uint32_t)((int32_t)(int16_t)magnitude * (int32_t)(int16_t)magnitude);
		const uint16_t complement = (uint16_t)SlipDraw3D_Root32(SLIP_VIEW_UNIT_LENGTH_SQUARED_Q28 - square);

		angle = (uint16_t)(SLIP_ANGLE_QUARTER_TURN - (uint16_t)SlipView3D_LookupArcsineAngle(maths, complement));
	}
	if (original < 0) {
		angle = (uint16_t)(0u - angle);
	}
	return (int16_t)angle;
}

static int16_t SlipView3D_PairAngle(const SlipView3DMaths *maths, int16_t sineComponent, int16_t cosineComponent) {
	const int16_t originalSine = sineComponent;
	const int16_t originalCosine = cosineComponent;
	const uint16_t sineMagnitude = originalSine < 0 ? (uint16_t)(0u - (uint16_t)originalSine) : (uint16_t)originalSine;
	uint16_t angle;

	if (sineMagnitude <= SLIP_VIEW_ASIN_DIAGONAL_LIMIT_Q14) {
		angle = (uint16_t)SlipView3D_LookupArcsineAngle(maths, sineMagnitude);
	} else {
		const uint16_t cosineMagnitude =
		    originalCosine < 0 ? (uint16_t)(0u - (uint16_t)originalCosine) : (uint16_t)originalCosine;

		angle = (uint16_t)(SLIP_ANGLE_QUARTER_TURN - (uint16_t)SlipView3D_LookupArcsineAngle(maths, cosineMagnitude));
	}
	if (originalCosine < 0) {
		angle = (uint16_t)(SLIP_ANGLE_HALF_TURN - angle);
	}
	if (originalSine < 0) {
		angle = (uint16_t)(0u - angle);
	}
	return (int16_t)angle;
}

int16_t SlipView3D_PitchFromMatrix(const SlipView3DMaths *maths, const SlipView3DMatrix *matrix) {
	uint16_t angle;

	if (maths == NULL || matrix == NULL)
		return 0;
	angle = (uint16_t)SlipView3D_AsinQ14(maths, matrix->m[7]);
	if (matrix->m[4] < 0) {
		angle = (uint16_t)(SLIP_ANGLE_HALF_TURN - angle);
	}
	return (int16_t)angle;
}

int16_t SlipView3D_RollFromMatrix(const SlipView3DMaths *maths, const SlipView3DMatrix *matrix) {
	SlipDraw3DNormalizeVector2D normalized;
	int16_t sineComponent;
	int16_t cosineComponent;

	if (maths == NULL || matrix == NULL)
		return 0;
	if (matrix->m[7] <= SLIP_VIEW_HEADING_VERTICAL_LIMIT_Q14 &&
	    matrix->m[7] >= (int16_t)(-SLIP_VIEW_HEADING_VERTICAL_LIMIT_Q14)) {
		sineComponent = matrix->m[6];
		cosineComponent = matrix->m[8];
	} else {
		sineComponent = matrix->m[3];
		cosineComponent = matrix->m[5];
		if (((uint16_t)matrix->m[7] ^ (uint16_t)matrix->m[4]) < SLIP_VIEW_WORD_SIGN_BIT) {
			sineComponent = (int16_t)(uint16_t)(0u - (uint16_t)sineComponent);
			cosineComponent = (int16_t)(uint16_t)(0u - (uint16_t)cosineComponent);
		}
	}
	if (!SlipDraw3D_NormalizeVector2D((uint16_t)sineComponent, (uint16_t)cosineComponent, &normalized)) {
		return 0;
	}
	return SlipView3D_PairAngle(maths, normalized.unitXQ14, normalized.unitYQ14);
}

bool SlipView3D_LevelHeading(const SlipView3DMaths *maths, SlipView3DMatrix *matrix, uint32_t maximumStep) {
	uint32_t step = maximumStep;
	int16_t angle;
	bool carry = false;

	if (maths == NULL || matrix == NULL)
		return false;
	if ((uint16_t)step > INT16_MAX)
		step = INT16_MAX;
	angle = SlipView3D_HeadingFromMatrix(maths, matrix);
	if (angle < 0) {
		const int16_t next = (int16_t)(uint16_t)((uint16_t)angle + (uint16_t)step);

		if (next >= 0) {
			angle = 0;
			carry = true;
		} else {
			angle = next;
		}
	} else {
		const int16_t next = (int16_t)(uint16_t)((uint16_t)angle - (uint16_t)step);

		if (next < 0) {
			angle = 0;
			carry = true;
		} else {
			angle = next;
		}
	}
	(void)SlipView3D_BuildMatrixFromVector(matrix, matrix->m[6], matrix->m[7], matrix->m[8]);
	SlipView3D_ApplyRow0Row1Rotation(maths, angle, matrix);
	SlipView3D_OrthonormalizeForwardBasis(matrix);
	return carry;
}

static uint16_t SlipView3D_ApproachAngle(uint16_t current, uint16_t target, int32_t step, bool *complete) {
	const int16_t difference = (int16_t)(uint16_t)(target - current);
	const uint16_t magnitude = difference < 0 ? (uint16_t)(0u - (uint16_t)difference) : (uint16_t)difference;
	uint32_t sum;
	bool carry;

	*complete = false;
	if ((int16_t)magnitude <= SLIP_VIEW_ANGLE_COMPLETION_TOLERANCE) {
		*complete = true;
		return target;
	}
	if (step < 0) {
		if (step <= -SLIP_ANGLE_FULL_TURN) {
			*complete = true;
			return target;
		}
		sum = (uint32_t)(uint16_t)(current - target) + (uint16_t)step;
		carry = sum > UINT16_MAX;
		if (!carry) {
			*complete = true;
			return target;
		}
	} else {
		if (step >= SLIP_ANGLE_FULL_TURN) {
			*complete = true;
			return target;
		}
		sum = (uint32_t)(uint16_t)(current - target) + (uint16_t)step;
		carry = sum > UINT16_MAX;
		if (carry) {
			*complete = true;
			return target;
		}
	}
	return (uint16_t)((uint16_t)sum + target);
}

bool SlipView3D_ApproachAngles(const SlipView3DMaths *maths, SlipView3DMatrix *matrix, uint16_t targetRoll,
                               uint16_t targetPitch, int32_t rollStep, int32_t pitchStep) {
	uint16_t currentPitch;
	uint16_t currentRoll;
	uint16_t nextRoll;
	uint16_t nextPitch;
	bool rollComplete;
	bool pitchComplete;

	if (maths == NULL || matrix == NULL)
		return false;
	currentPitch = (uint16_t)SlipView3D_PitchFromMatrix(maths, matrix);
	currentRoll = (uint16_t)SlipView3D_RollFromMatrix(maths, matrix);
	if (matrix->m[4] < 0)
		currentRoll ^= SLIP_ANGLE_HALF_TURN;
	nextRoll = SlipView3D_ApproachAngle(currentRoll, targetRoll, rollStep, &rollComplete);
	nextPitch = SlipView3D_ApproachAngle(currentPitch, targetPitch, pitchStep, &pitchComplete);
	SlipView3D_BuildYawMatrix(maths, (int16_t)nextRoll, matrix);
	SlipView3D_ApplyPitchMatrix(maths, (int16_t)nextPitch, matrix);
	SlipView3D_OrthonormalizeForwardBasis(matrix);
	return rollComplete && pitchComplete;
}

SlipView3DRotateVectorTowards SlipView3D_RotateVectorTowards(const SlipView3DMaths *maths, int16_t currentX,
                                                             int16_t currentY, int16_t currentZ, int16_t targetX,
                                                             int16_t targetY, int16_t targetZ, uint32_t maximumStep) {
	SlipView3DRotateVectorTowards result;
	SlipView3DCrossProduct cross;
	SlipView3DNormalizeLength3D normalized;
	SlipView3DMatrix rotation;
	SlipView3DVec32 rotated;
	int16_t dot;
	int16_t sine;
	int16_t cosineStep;
	uint16_t step = (uint16_t)maximumStep;
	uint32_t dotSquare;

	result.vector = (SlipView3DVec32){targetX, targetY, targetZ};
	result.carry = true;
	if (maths == NULL)
		return result;
	if (step > SLIP_ANGLE_HALF_TURN)
		step = SLIP_ANGLE_HALF_TURN;
	dot = (int16_t)SlipView3D_DotProductQ14((uint16_t)currentX, (uint16_t)currentY, (uint16_t)currentZ,
	                                        (uint16_t)targetX, (uint16_t)targetY, (uint16_t)targetZ, NULL);
	if (dot > SLIP_VIEW_ALIGNMENT_DOT_THRESHOLD_Q14)
		return result;
	dotSquare = (uint32_t)((int32_t)dot * (int32_t)dot);
	sine = (int16_t)SlipDraw3D_Root32(SLIP_VIEW_UNIT_LENGTH_SQUARED_Q28 - dotSquare);
	SlipView3D_CrossProduct((uint16_t)currentX, (uint16_t)currentY, (uint16_t)currentZ, (uint16_t)targetX,
	                        (uint16_t)targetY, (uint16_t)targetZ, &cross);
	cosineStep = SlipView3D_CosQ14(maths, (int16_t)step);
	if (dot >= cosineStep)
		return result;
	SlipView3D_BuildAxisRotation((uint16_t)SlipView3D_SinQ14(maths, (int16_t)step), (uint16_t)cosineStep,
	                             (uint16_t)((int32_t)cross.crossX >> SLIP_Q14_FRACTION_BITS),
	                             (uint16_t)((int32_t)cross.crossY >> SLIP_Q14_FRACTION_BITS),
	                             (uint16_t)((int32_t)cross.crossZ >> SLIP_Q14_FRACTION_BITS), &rotation);
	rotated = SlipView3D_TransformPosition16(&rotation, (SlipView3DVec32){currentX, currentY, currentZ});
	(void)SlipView3D_NormalizeLength3D((uint32_t)rotated.x, (uint32_t)rotated.y, (uint32_t)rotated.z, &normalized);
	result.vector = (SlipView3DVec32){(int16_t)(uint16_t)normalized.unitXQ14, (int16_t)(uint16_t)normalized.unitYQ14,
	                                  (int16_t)(uint16_t)normalized.unitZQ14};
	result.carry = false;
	(void)sine;
	return result;
}

static uint16_t SlipView3D_MulRound16(int16_t lhs, int16_t rhs) {
	const int32_t product = (int32_t)lhs * (int32_t)rhs;
	uint16_t out = (uint16_t)((uint32_t)product >> SLIP_Q14_FRACTION_BITS);

	if (((uint32_t)product >> (SLIP_Q14_FRACTION_BITS - 1)) & 1u) {
		++out;
	}
	return out;
}

void SlipView3D_BuildAxisRotation(uint16_t sineBits, uint16_t cosineBits, uint16_t axisXBits, uint16_t axisYBits,
                                  uint16_t axisZBits, SlipView3DMatrix *matrix) {
	const int16_t sine = (int16_t)sineBits;
	const int16_t x = (int16_t)axisXBits;
	const int16_t y = (int16_t)axisYBits;
	const int16_t z = (int16_t)axisZBits;
	const int16_t oneMinusCos = (int16_t)(uint16_t)(SLIP_Q14_ONE - cosineBits);
	uint16_t rotationXY;
	uint16_t rotationXZ;
	uint16_t yzHigh;
	uint16_t sineTimesX;
	uint16_t sineTimesY;
	uint16_t sineTimesZ;
	int32_t yzProduct;
	int component;

	if (matrix == NULL) {
		return;
	}
	rotationXY = SlipView3D_MulRound16((int16_t)SlipView3D_MulRound16(oneMinusCos, x), y);
	rotationXZ = SlipView3D_MulRound16((int16_t)SlipView3D_MulRound16(oneMinusCos, x), z);
	yzProduct = (int32_t)(int16_t)SlipView3D_MulRound16(oneMinusCos, z) * (int32_t)y;
	yzHigh = (uint16_t)((uint32_t)yzProduct >> 16);
	for (component = 0; component < 3; ++component) {
		const int16_t axisWord = component == 0 ? x : (component == 1 ? y : z);
		const int32_t square = (int32_t)(int16_t)SlipView3D_MulRound16(axisWord, axisWord) * (int32_t)oneMinusCos;
		uint16_t diagonal = (uint16_t)((uint32_t)square >> SLIP_Q14_FRACTION_BITS);

		diagonal =
		    (uint16_t)(diagonal + cosineBits + (uint16_t)(((uint32_t)square >> (SLIP_Q14_FRACTION_BITS - 1)) & 1u));
		matrix->m[component * 4] = (int16_t)diagonal;
	}
	sineTimesZ = SlipView3D_MulRound16(sine, z);
	matrix->m[1] = (int16_t)(uint16_t)(rotationXY + sineTimesZ);
	matrix->m[3] = (int16_t)(uint16_t)(rotationXY - sineTimesZ);
	sineTimesY = SlipView3D_MulRound16(sine, y);
	matrix->m[6] = (int16_t)(uint16_t)(rotationXZ + sineTimesY);
	matrix->m[2] = (int16_t)(uint16_t)(rotationXZ - sineTimesY);
	sineTimesX = SlipView3D_MulRound16(sine, x);
	matrix->m[5] = (int16_t)(uint16_t)(yzHigh + sineTimesX);
	matrix->m[7] = (int16_t)(uint16_t)(yzHigh - sineTimesX);
}

static void SlipView3D_ApplyForwardRotation(SlipView3DMatrix *matrix, uint16_t sine, uint16_t cosine, uint16_t axisX,
                                            uint16_t axisY, uint16_t axisZ) {
	SlipView3DMatrix rotation;
	SlipView3DMatrix rotated;

	SlipView3D_BuildAxisRotation(sine, cosine, axisX, axisY, axisZ, &rotation);
	SlipView3D_MultiplyMatrix(matrix, &rotation, &rotated);
	*matrix = rotated;
	SlipView3D_OrthonormalizeForwardBasis(matrix);
}

static void SlipView3D_ApplyRightRotation(SlipView3DMatrix *matrix, uint16_t sine, uint16_t cosine, uint16_t axisX,
                                          uint16_t axisY, uint16_t axisZ) {
	SlipView3DMatrix rotation;
	SlipView3DMatrix rotated;

	SlipView3D_BuildAxisRotation(sine, cosine, axisX, axisY, axisZ, &rotation);
	SlipView3D_MultiplyMatrix(matrix, &rotation, &rotated);
	*matrix = rotated;
	SlipView3D_OrthonormalizeForwardBasis(matrix);
}

bool SlipView3D_RotateForwardTowards(const SlipView3DMaths *maths, SlipView3DMatrix *matrix, int16_t targetX,
                                     int16_t targetY, int16_t targetZ, uint32_t maximumStep) {
	uint32_t step;
	int16_t dot;
	int16_t sineFromDot;
	int16_t cosStep;
	uint16_t axisX;
	uint16_t axisY;
	uint16_t axisZ;
	SlipView3DCrossProduct cross;
	SlipView3DNormalizeVector3D normalized;

	step = maximumStep;
	if (step > SLIP_ANGLE_HALF_TURN) {
		step = SLIP_ANGLE_HALF_TURN;
	}
	dot =
	    (int16_t)SlipView3D_DotProductQ14((uint16_t)targetX, (uint16_t)targetY, (uint16_t)targetZ,
	                                      (uint16_t)matrix->m[6], (uint16_t)matrix->m[7], (uint16_t)matrix->m[8], NULL);
	if (dot <= SLIP_VIEW_ALIGNMENT_DOT_THRESHOLD_Q14) {
		sineFromDot =
		    (int16_t)SlipDraw3D_Root32(SLIP_VIEW_UNIT_LENGTH_SQUARED_Q28 - (uint32_t)((int32_t)dot * (int32_t)dot));
		SlipView3D_CrossProduct((uint16_t)matrix->m[6], (uint16_t)matrix->m[7], (uint16_t)matrix->m[8],
		                        (uint16_t)targetX, (uint16_t)targetY, (uint16_t)targetZ, &cross);
		SlipView3D_NormalizeVector3D(cross.crossX, cross.crossY, cross.crossZ, &normalized);
		axisX = (uint16_t)normalized.unitXQ14;
		axisY = (uint16_t)normalized.unitYQ14;
		axisZ = (uint16_t)normalized.unitZQ14;
		if ((uint16_t)(axisX | axisY | axisZ) == 0) {
			SlipView3D_CrossProduct((uint16_t)matrix->m[0], (uint16_t)matrix->m[1], (uint16_t)matrix->m[2],
			                        (uint16_t)targetX, (uint16_t)targetY, (uint16_t)targetZ, &cross);
			SlipView3D_NormalizeVector3D(cross.crossX, cross.crossY, cross.crossZ, &normalized);
			axisX = (uint16_t)normalized.unitXQ14;
			axisY = (uint16_t)normalized.unitYQ14;
			axisZ = (uint16_t)normalized.unitZQ14;
		}
		cosStep = SlipView3D_CosQ14(maths, (int16_t)step);
		if (dot < cosStep) {
			SlipView3D_ApplyForwardRotation(matrix, (uint16_t)SlipView3D_SinQ14(maths, (int16_t)step),
			                                (uint16_t)cosStep, axisX, axisY, axisZ);
			return false;
		}
		SlipView3D_ApplyForwardRotation(matrix, (uint16_t)sineFromDot, (uint16_t)dot, axisX, axisY, axisZ);
	}
	matrix->m[6] = targetX;
	matrix->m[7] = targetY;
	matrix->m[8] = targetZ;
	SlipView3D_OrthonormalizeForwardBasis(matrix);
	return true;
}

bool SlipView3D_RotateRightTowards(const SlipView3DMaths *maths, SlipView3DMatrix *matrix, int16_t targetX,
                                   int16_t targetY, int16_t targetZ, uint32_t maximumStep) {
	uint32_t step;
	int16_t dot;
	int16_t sineFromDot;
	int16_t cosStep;
	uint16_t axisX;
	uint16_t axisY;
	uint16_t axisZ;
	SlipView3DCrossProduct cross;
	SlipView3DNormalizeVector3D normalized;

	step = maximumStep;
	if ((uint16_t)step > SLIP_ANGLE_HALF_TURN) {
		step = SLIP_ANGLE_HALF_TURN;
	}
	dot =
	    (int16_t)SlipView3D_DotProductQ14((uint16_t)targetX, (uint16_t)targetY, (uint16_t)targetZ,
	                                      (uint16_t)matrix->m[0], (uint16_t)matrix->m[1], (uint16_t)matrix->m[2], NULL);
	if (dot <= SLIP_VIEW_ALIGNMENT_DOT_THRESHOLD_Q14) {
		sineFromDot =
		    (int16_t)SlipDraw3D_Root32(SLIP_VIEW_UNIT_LENGTH_SQUARED_Q28 - (uint32_t)((int32_t)dot * (int32_t)dot));
		SlipView3D_CrossProduct((uint16_t)matrix->m[0], (uint16_t)matrix->m[1], (uint16_t)matrix->m[2],
		                        (uint16_t)targetX, (uint16_t)targetY, (uint16_t)targetZ, &cross);
		SlipView3D_NormalizeVector3D(cross.crossX, cross.crossY, cross.crossZ, &normalized);
		axisX = (uint16_t)normalized.unitXQ14;
		axisY = (uint16_t)normalized.unitYQ14;
		axisZ = (uint16_t)normalized.unitZQ14;
		if ((uint16_t)(axisX | axisY | axisZ) == 0) {
			SlipView3D_CrossProduct((uint16_t)matrix->m[6], (uint16_t)matrix->m[7], (uint16_t)matrix->m[8],
			                        (uint16_t)targetX, (uint16_t)targetY, (uint16_t)targetZ, &cross);
			SlipView3D_NormalizeVector3D(cross.crossX, cross.crossY, cross.crossZ, &normalized);
			axisX = (uint16_t)normalized.unitXQ14;
			axisY = (uint16_t)normalized.unitYQ14;
			axisZ = (uint16_t)normalized.unitZQ14;
		}
		cosStep = SlipView3D_CosQ14(maths, (int16_t)step);
		if (dot < cosStep) {
			SlipView3D_ApplyRightRotation(matrix, (uint16_t)SlipView3D_SinQ14(maths, (int16_t)step), (uint16_t)cosStep,
			                              axisX, axisY, axisZ);
			return false;
		}
		SlipView3D_ApplyRightRotation(matrix, (uint16_t)sineFromDot, (uint16_t)dot, axisX, axisY, axisZ);
	}
	matrix->m[0] = targetX;
	matrix->m[1] = targetY;
	matrix->m[2] = targetZ;
	SlipView3D_OrthonormalizeRightBasis(matrix);
	return true;
}

static int16_t SlipView3D_Dot3Q14(int16_t lhsX, int16_t lhsY, int16_t lhsZ, int16_t rhsX, int16_t rhsY, int16_t rhsZ) {
	uint32_t sum = (uint32_t)((int32_t)lhsX * (int32_t)rhsX);

	sum += (uint32_t)((int32_t)lhsY * (int32_t)rhsY);
	sum += (uint32_t)((int32_t)lhsZ * (int32_t)rhsZ);
	sum = (sum >> SLIP_Q14_FRACTION_BITS) |
	      ((sum & SLIP_VIEW_DWORD_SIGN_BIT) != 0 ? SLIP_VIEW_Q14_SIGN_EXTENSION_MASK : 0);
	return (int16_t)sum;
}

int SlipView3D_ScaleVector2D(uint32_t inputX, uint32_t inputY, SlipView3DScaleVector2D *result) {
	uint32_t componentX;
	uint32_t componentY;
	uint32_t highBitsOrShift;
	uint32_t negativeYMask;
	uint32_t negativeXMask;

	if (result == NULL) {
		return 0;
	}
	componentX = inputX;
	componentY = inputY;
	negativeXMask = 0;
	negativeYMask = 0;
	*result = (SlipView3DScaleVector2D){inputX, inputY, 0, componentX, 0, componentY, 0, 0, 0, 0, 0, 0, 0};
	if ((int32_t)componentX < 0) {
		componentX = 0u - componentX;
		negativeXMask = UINT32_MAX;
		result->negativeX = 1;
	}
	result->absoluteX = componentX;
	if ((int32_t)componentY < 0) {
		componentY = 0u - componentY;
		negativeYMask = UINT32_MAX;
		result->negativeY = 1;
	}
	result->absoluteY = componentY;
	highBitsOrShift = (componentX | componentY) & SLIP_VIEW_NORMALIZE_HIGH_COMPONENT_MASK;
	result->highComponentBits = highBitsOrShift;
	if (highBitsOrShift != 0) {
		uint32_t bitIndex = 0;
		uint32_t scan = highBitsOrShift;

		while (scan >>= 1) {
			++bitIndex;
		}
		highBitsOrShift = bitIndex - SLIP_VIEW_NORMALIZE_MAXIMUM_COMPONENT_BIT;
		result->shifted = 1;
		result->shiftCount = highBitsOrShift;
		if ((componentX & SLIP_VIEW_DWORD_SIGN_BIT) == 0) {
			componentX >>= highBitsOrShift;
		} else {
			componentX = (componentX >> highBitsOrShift) | (UINT32_MAX << (32u - highBitsOrShift));
		}
		if ((componentY & SLIP_VIEW_DWORD_SIGN_BIT) == 0) {
			componentY >>= highBitsOrShift;
		} else {
			componentY = (componentY >> highBitsOrShift) | (UINT32_MAX << (32u - highBitsOrShift));
		}
	}
	if (negativeXMask != 0) {
		componentX = 0u - componentX;
	}
	if (negativeYMask != 0) {
		componentY = 0u - componentY;
	}
	result->scaledX = componentX;
	result->scaledY = componentY;
	result->appliedShift = highBitsOrShift;
	result->operationCompleted = 1;
	return 1;
}

int SlipView3D_NormalizeScaledVector2D(uint32_t inputX, uint32_t inputY, SlipView3DNormalizeScaledVector2D *result) {
	SlipDraw3DNormalizeVector2D normalize;

	if (result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	if (!SlipView3D_ScaleVector2D(inputX, inputY, &result->scale)) {
		return 0;
	}
	result->normalize2DCalled = 1;
	if (!SlipDraw3D_NormalizeVector2D(result->scale.scaledX, result->scale.scaledY, &normalize)) {
		return 0;
	}
	result->unitXQ14 = normalize.unitXQ14;
	result->unitYQ14 = normalize.unitYQ14;
	result->length = normalize.length;
	result->operationCompleted = 1;
	return 1;
}

static uint32_t SlipView3D_ArithmeticShiftRight32(uint32_t value, uint32_t shift) {
	shift &= SLIP_VIEW_DWORD_SHIFT_COUNT_MASK;
	if (shift == 0) {
		return value;
	}
	return (value >> shift) | ((value & SLIP_VIEW_DWORD_SIGN_BIT) != 0 ? (UINT32_MAX << (32u - shift)) : 0);
}

static uint16_t SlipView3D_HalveSignedWord(uint16_t value) {
	return (uint16_t)((value >> 1) | (value & SLIP_VIEW_WORD_SIGN_BIT));
}

static uint16_t SlipView3D_DivideComponentQ14(uint16_t component, uint16_t divisor) {
	const int32_t numerator = (int32_t)(int16_t)component * SLIP_Q14_ONE;

	return (uint16_t)(int16_t)(numerator / (int32_t)(int16_t)divisor);
}

void SlipView3D_DotProduct32(uint32_t lhsX, uint32_t lhsY, uint32_t lhsZ, uint32_t rhsX, uint32_t rhsY, uint32_t rhsZ,
                             SlipView3DDotProduct32 *result) {
	const uint64_t productX = (uint64_t)((int64_t)(int32_t)lhsX * (int64_t)(int32_t)rhsX);
	const uint64_t productY = (uint64_t)((int64_t)(int32_t)lhsY * (int64_t)(int32_t)rhsY);
	const uint64_t sumXY = productX + productY;
	const uint64_t productZ = (uint64_t)((int64_t)(int32_t)lhsZ * (int64_t)(int32_t)rhsZ);
	const uint64_t sumXYZ = sumXY + productZ;

	if (result != NULL) {
		*result = (SlipView3DDotProduct32){.productX = productX,
		                                   .productY = productY,
		                                   .sumXY = sumXY,
		                                   .productZ = productZ,
		                                   .sumXYZ = sumXYZ,
		                                   .dotProductLow = (uint32_t)sumXYZ,
		                                   .dotProductHigh = (uint32_t)(sumXYZ >> 32),
		                                   .operationCompleted = 1};
	}
}

void SlipView3D_DotProduct32By16(uint32_t lhsX, uint32_t lhsY, uint32_t lhsZ, uint16_t rhsX, uint16_t rhsY,
                                 uint16_t rhsZ, SlipView3DDotProduct32By16 *result) {
	const uint64_t productX = (uint64_t)((int64_t)(int32_t)lhsX * (int64_t)(int32_t)(int16_t)rhsX);
	const uint64_t productY = (uint64_t)((int64_t)(int32_t)lhsY * (int64_t)(int32_t)(int16_t)rhsY);
	const uint64_t sumXY = productX + productY;
	const uint64_t productZ = (uint64_t)((int64_t)(int32_t)lhsZ * (int64_t)(int32_t)(int16_t)rhsZ);
	const uint64_t sumXYZ = sumXY + productZ;

	if (result != NULL) {
		*result = (SlipView3DDotProduct32By16){.productX = productX,
		                                       .productY = productY,
		                                       .sumXY = sumXY,
		                                       .productZ = productZ,
		                                       .sumXYZ = sumXYZ,
		                                       .dotProductLow = (uint32_t)sumXYZ,
		                                       .dotProductHigh = (uint32_t)(sumXYZ >> 32)};
	}
}

void SlipView3D_CrossProduct(uint32_t lhsX, uint32_t lhsY, uint32_t lhsZ, uint32_t rhsX, uint32_t rhsY, uint32_t rhsZ,
                             SlipView3DCrossProduct *result) {
	const int32_t xOut =
	    (int32_t)(int16_t)rhsZ * (int32_t)(int16_t)lhsY - (int32_t)(int16_t)rhsY * (int32_t)(int16_t)lhsZ;
	const int32_t yOut =
	    (int32_t)(int16_t)rhsX * (int32_t)(int16_t)lhsZ - (int32_t)(int16_t)rhsZ * (int32_t)(int16_t)lhsX;
	const int32_t zOut =
	    (int32_t)(int16_t)rhsY * (int32_t)(int16_t)lhsX - (int32_t)(int16_t)rhsX * (int32_t)(int16_t)lhsY;

	if (result != NULL) {
		*result = (SlipView3DCrossProduct){.inputRhsYBits = rhsY,
		                                   .xOut = (uint32_t)xOut,
		                                   .yOut = (uint32_t)yOut,
		                                   .zOut = (uint32_t)zOut,
		                                   .crossX = (uint32_t)xOut,
		                                   .crossY = (uint32_t)yOut,
		                                   .crossZ = (uint32_t)zOut,
		                                   .operationCompleted = 1};
	}
}

int SlipView3D_ScaleVector3D(uint32_t inputX, uint32_t inputY, uint32_t inputZ, SlipView3DScaleVector3D *result) {
	uint32_t componentX = inputX;
	uint32_t componentY = inputY;
	uint32_t componentZOrShift = inputZ;
	uint32_t componentZ;
	uint32_t negativeYZMasks = 0;
	uint32_t negativeXMask = 0;
	uint32_t scaleMask;

	if (result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->inputX = inputX;
	result->inputY = inputY;
	result->inputZ = inputZ;
	if ((int32_t)componentX < 0) {
		componentX = 0u - componentX;
		--negativeXMask;
		result->negativeX = 1;
	}
	if ((int32_t)componentY < 0) {
		componentY = 0u - componentY;
		negativeYZMasks = (negativeYZMasks & (UINT32_MAX ^ SLIP_VIEW_PACKED_Y_SIGN_MASK)) |
		                  ((negativeYZMasks - 1u) & SLIP_VIEW_PACKED_Y_SIGN_MASK);
		result->negativeY = 1;
	}
	if ((int32_t)componentZOrShift < 0) {
		componentZOrShift = 0u - componentZOrShift;
		negativeYZMasks = (negativeYZMasks & (UINT32_MAX ^ SLIP_VIEW_PACKED_Z_SIGN_MASK)) |
		                  ((negativeYZMasks - (1u << SLIP_VIEW_PACKED_Z_SIGN_SHIFT)) & SLIP_VIEW_PACKED_Z_SIGN_MASK);
		result->negativeZ = 1;
	}
	componentZ = componentZOrShift;
	scaleMask = (componentX | componentY | componentZOrShift) & SLIP_VIEW_NORMALIZE_HIGH_COMPONENT_MASK;
	result->scaleMask = scaleMask;
	if (scaleMask != 0) {
		uint32_t shiftCount = 0;
		uint32_t scan = scaleMask;

		while (scan >>= 1) {
			++shiftCount;
		}
		shiftCount -= SLIP_VIEW_NORMALIZE_MAXIMUM_COMPONENT_BIT;
		componentX = SlipView3D_ArithmeticShiftRight32(componentX, shiftCount);
		componentY = SlipView3D_ArithmeticShiftRight32(componentY, shiftCount);
		componentZ = SlipView3D_ArithmeticShiftRight32(componentZ, shiftCount);
		componentZOrShift = shiftCount;
		result->shiftCount = shiftCount;
	} else {
		componentZOrShift = 0;
	}
	if (negativeXMask != 0) {
		componentX = 0u - componentX;
	}
	if ((negativeYZMasks & SLIP_VIEW_PACKED_Y_SIGN_MASK) != 0) {
		componentY = 0u - componentY;
	}
	if ((negativeYZMasks & SLIP_VIEW_PACKED_Z_SIGN_MASK) != 0) {
		componentZ = 0u - componentZ;
	}
	result->scaledX = componentX;
	result->scaledY = componentY;
	result->scaledZ = componentZ;
	result->appliedShift = componentZOrShift;
	result->operationCompleted = 1;
	return 1;
}

int SlipView3D_NormalizeLength3D(uint32_t inputX, uint32_t inputY, uint32_t inputZ,
                                 SlipView3DNormalizeLength3D *result) {
	uint16_t componentX = (uint16_t)inputX;
	uint16_t componentY = (uint16_t)inputY;
	uint16_t componentZ = (uint16_t)inputZ;
	uint32_t squareX;
	uint32_t squareY;
	uint32_t squareZ;
	uint32_t squareSum;
	uint16_t root;
	uint16_t divisor;
	uint16_t normalizedX = componentX;
	uint16_t normalizedY = componentY;
	uint16_t normalizedZ = componentZ;

	if (result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	squareX = (uint32_t)((int32_t)(int16_t)componentX * (int32_t)(int16_t)componentX);
	squareY = (uint32_t)((int32_t)(int16_t)componentY * (int32_t)(int16_t)componentY);
	squareZ = (uint32_t)((int32_t)(int16_t)componentZ * (int32_t)(int16_t)componentZ);
	squareSum = squareZ + squareY + squareX;
	root = SlipDraw3D_Root32(squareSum);
	divisor = root;
	result->inputX = inputX;
	result->inputY = inputY;
	result->inputZ = inputZ;
	result->squareX = squareX;
	result->squareY = squareY;
	result->squareZ = squareZ;
	result->squareSum = squareSum;
	result->lengthRootBeforeDivision = root;
	result->rootIsUnitLength = root == SLIP_Q14_ONE;
	result->rootIsZero = root == 0;
	if (root != SLIP_Q14_ONE && root != 0) {
		result->rootHasSignBit = (int16_t)root < 0;
		if ((int16_t)root < 0) {
			componentX = SlipView3D_HalveSignedWord(componentX);
			componentY = SlipView3D_HalveSignedWord(componentY);
			componentZ = SlipView3D_HalveSignedWord(componentZ);
			divisor = (uint16_t)(divisor >> 1);
		}
		normalizedX = SlipView3D_DivideComponentQ14(componentX, divisor);
		normalizedY = SlipView3D_DivideComponentQ14(componentY, divisor);
		normalizedZ = SlipView3D_DivideComponentQ14(componentZ, divisor);
	}
	result->divisor = divisor;
	result->unitXQ14 = normalizedX;

	result->unitYQ14 = (root == SLIP_Q14_ONE || root == 0) ? inputY : normalizedY;
	result->unitZQ14 = (root == SLIP_Q14_ONE || root == 0) ? inputZ : normalizedZ;
	result->lengthRoot = root;
	result->operationCompleted = 1;
	return 1;
}

int SlipView3D_NormalizeVector3D(uint32_t inputX, uint32_t inputY, uint32_t inputZ,
                                 SlipView3DNormalizeVector3D *result) {
	SlipView3DScaleVector3D scale;
	SlipView3DNormalizeLength3D length;
	uint32_t savedShift;
	uint32_t normalizedZ;
	uint32_t restoredVectorLength;

	if (result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	if (!SlipView3D_ScaleVector3D(inputX, inputY, inputZ, &scale)) {
		return 0;
	}
	savedShift = scale.appliedShift;
	result->scale = scale;
	result->savedShift = savedShift;
	result->normalizeLength3DCalled = 1;
	if (!SlipView3D_NormalizeLength3D(scale.scaledX, scale.scaledY, scale.scaledZ, &length)) {
		return 0;
	}

	if (length.rootIsUnitLength || length.rootIsZero) {
		length.unitXQ14 = (scale.scaledZ & SLIP_VIEW_UPPER_WORD_MASK) | (uint16_t)scale.scaledX;
	}
	restoredVectorLength = (uint32_t)(uint16_t)length.lengthRoot;
	normalizedZ = length.unitZQ14;
	restoredVectorLength <<= (savedShift & SLIP_VIEW_DWORD_SHIFT_COUNT_MASK);
	result->length = length;
	result->scaledVectorLength = (uint32_t)(uint16_t)length.lengthRoot;
	result->vectorLength = restoredVectorLength;
	result->unitXQ14 = length.unitXQ14;
	result->unitYQ14 = length.unitYQ14;
	result->unitZQ14 = normalizedZ;
	result->operationCompleted = 1;
	return 1;
}

void SlipView3D_MultiplyMatrix(const SlipView3DMatrix *lhs, const SlipView3DMatrix *rhs, SlipView3DMatrix *out) {
	int row;

	for (row = 0; row < 3; ++row) {
		const int base = row * 3;

		out->m[base + 0] =
		    SlipView3D_Dot3Q14(lhs->m[base + 0], lhs->m[base + 1], lhs->m[base + 2], rhs->m[0], rhs->m[3], rhs->m[6]);
		out->m[base + 1] =
		    SlipView3D_Dot3Q14(lhs->m[base + 0], lhs->m[base + 1], lhs->m[base + 2], rhs->m[1], rhs->m[4], rhs->m[7]);
		out->m[base + 2] =
		    SlipView3D_Dot3Q14(lhs->m[base + 0], lhs->m[base + 1], lhs->m[base + 2], rhs->m[2], rhs->m[5], rhs->m[8]);
	}
}

void SlipView3D_ComposeMatrix(const SlipView3DMatrix *lhs, const SlipView3DMatrix *rhs, SlipView3DMatrix *out) {
	int row;
	int col;

	if (lhs == NULL || rhs == NULL || out == NULL) {
		return;
	}

	for (row = 0; row < 3; ++row) {
		for (col = 0; col < 3; ++col) {
			const int lhsBase = row * 3;
			const int rhsBase = col * 3;

			out->m[lhsBase + col] = (int16_t)(((int32_t)lhs->m[lhsBase + 0] * rhs->m[rhsBase + 0] +
			                                   (int32_t)lhs->m[lhsBase + 1] * rhs->m[rhsBase + 1] +
			                                   (int32_t)lhs->m[lhsBase + 2] * rhs->m[rhsBase + 2]) >>
			                                  SLIP_Q14_FRACTION_BITS);
		}
	}
}

int SlipView3D_CopyMatrixWords(uint8_t *destination, size_t destBytesRemaining, const uint8_t *source,
                               size_t srcBytesRemaining) {
	enum {
		MATRIX_BYTES = sizeof(SlipView3DMatrix),
		MATRIX_DWORDS = MATRIX_BYTES / sizeof(uint32_t),
		MATRIX_TAIL_OFFSET = MATRIX_DWORDS * sizeof(uint32_t)
	};

	int i;

	if (destination == NULL || source == NULL || destBytesRemaining < MATRIX_BYTES ||
	    srcBytesRemaining < MATRIX_BYTES) {
		return 0;
	}
	for (i = 0; i < MATRIX_DWORDS; ++i) {
		const size_t offset = (size_t)i * sizeof(uint32_t);
		uint32_t value;

		memcpy(&value, source + offset, sizeof(value));
		memcpy(destination + offset, &value, sizeof(value));
	}
	{
		uint16_t value;

		memcpy(&value, source + MATRIX_TAIL_OFFSET, sizeof(value));
		memcpy(destination + MATRIX_TAIL_OFFSET, &value, sizeof(value));
	}
	return 1;
}

int SlipView3D_BuildMatrixFromVector(SlipView3DMatrix *matrix, int16_t directionX, int16_t directionY,
                                     int16_t directionZ) {
	static const SlipView3DMatrix positiveVerticalBasis = {
	    {SLIP_Q14_ONE, 0, 0, 0, 0, -SLIP_Q14_ONE, 0, SLIP_Q14_ONE, 0}};
	static const SlipView3DMatrix negativeVerticalBasis = {
	    {SLIP_Q14_ONE, 0, 0, 0, 0, SLIP_Q14_ONE, 0, -SLIP_Q14_ONE, 0}};
	SlipDraw3DNormalizeVector2D normalizedRight;
	SlipView3DScaleVector3D scale;
	SlipView3DNormalizeLength3D normalizedUp;
	SlipView3DCrossProduct cross;
	int16_t firstRowX;
	int16_t firstRowZ;
	int16_t upX;
	int16_t upY;
	int16_t upZ;

	if (matrix == NULL) {
		return 0;
	}
	if (directionY >= SLIP_Q14_ONE) {
		*matrix = positiveVerticalBasis;
		return 1;
	}
	if (directionY <= (int16_t)(-SLIP_Q14_ONE)) {
		*matrix = negativeVerticalBasis;
		return 1;
	}

	matrix->m[6] = directionX;
	matrix->m[7] = directionY;
	matrix->m[8] = directionZ;
	if (!SlipDraw3D_NormalizeVector2D((uint16_t)directionZ, (uint16_t)(int16_t)-directionX, &normalizedRight)) {
		return 0;
	}
	firstRowX = normalizedRight.unitXQ14;
	firstRowZ = normalizedRight.unitYQ14;
	matrix->m[0] = firstRowX;
	matrix->m[1] = 0;
	matrix->m[2] = firstRowZ;

	SlipView3D_CrossProduct((uint32_t)(uint16_t)directionX, (uint32_t)(uint16_t)directionY,
	                        (uint32_t)(uint16_t)directionZ, (uint32_t)(uint16_t)firstRowX, 0,
	                        (uint32_t)(uint16_t)firstRowZ, &cross);
	if (!SlipView3D_ScaleVector3D(cross.crossX, cross.crossY, cross.crossZ, &scale)) {
		return 0;
	}
	if (!SlipView3D_NormalizeLength3D(scale.scaledX, scale.scaledY, scale.scaledZ, &normalizedUp)) {
		return 0;
	}
	upX = (int16_t)(uint16_t)normalizedUp.unitXQ14;
	upY = (int16_t)(uint16_t)normalizedUp.unitYQ14;
	upZ = (int16_t)(uint16_t)normalizedUp.unitZQ14;
	matrix->m[3] = upX;
	matrix->m[4] = upY;
	matrix->m[5] = upZ;
	return 1;
}

void SlipView3D_BuildFacingBasis(SlipView3DMatrix *basis, int16_t directionX, int16_t directionY, int16_t directionZ,
                                 int16_t referenceX, int16_t referenceY, int16_t referenceZ) {
	basis->m[6] = directionX;
	basis->m[7] = directionY;
	basis->m[8] = directionZ;
	SlipView3DCrossProduct cross;
	SlipView3DNormalizeVector3D normalized;

	SlipView3D_CrossProduct((uint16_t)directionX, (uint16_t)directionY, (uint16_t)directionZ, (uint16_t)referenceX,
	                        (uint16_t)referenceY, (uint16_t)referenceZ, &cross);
	SlipView3D_NormalizeVector3D(cross.crossX, cross.crossY, cross.crossZ, &normalized);
	const int16_t rightX = (int16_t)(0u - normalized.unitXQ14);
	const int16_t rightY = (int16_t)(0u - normalized.unitYQ14);
	const int16_t rightZ = (int16_t)(0u - normalized.unitZQ14);
	if ((uint16_t)((uint16_t)rightX | (uint16_t)rightY | (uint16_t)rightZ) == 0) {
		*basis = (SlipView3DMatrix){{SLIP_Q14_ONE, 0, 0, 0, SLIP_Q14_ONE, 0, 0, 0, SLIP_Q14_ONE}};
		return;
	}
	basis->m[0] = rightX;
	basis->m[1] = rightY;
	basis->m[2] = rightZ;
	SlipView3D_CrossProduct((uint16_t)directionX, (uint16_t)directionY, (uint16_t)directionZ, (uint16_t)rightX,
	                        (uint16_t)rightY, (uint16_t)rightZ, &cross);

	SlipView3DNormalizeLength3D up;
	SlipView3D_NormalizeLength3D((uint32_t)((int32_t)cross.crossX >> SLIP_Q14_FRACTION_BITS),
	                             (uint32_t)((int32_t)cross.crossY >> SLIP_Q14_FRACTION_BITS),
	                             (uint32_t)((int32_t)cross.crossZ >> SLIP_Q14_FRACTION_BITS), &up);
	basis->m[3] = (int16_t)up.unitXQ14;
	basis->m[4] = (int16_t)up.unitYQ14;
	basis->m[5] = (int16_t)up.unitZQ14;
}

int SlipView3D_BuildMatrixFromVector32(SlipView3DMatrix *matrix, uint32_t directionX, uint32_t directionY,
                                       uint32_t directionZ) {
	SlipView3DNormalizeVector3D normalized;

	if (!SlipView3D_NormalizeVector3D(directionX, directionY, directionZ, &normalized)) {
		return 0;
	}
	return SlipView3D_BuildMatrixFromVector(matrix, (int16_t)(uint16_t)normalized.unitXQ14,
	                                        (int16_t)(uint16_t)normalized.unitYQ14,
	                                        (int16_t)(uint16_t)normalized.unitZQ14);
}

int SlipView3D_ReflectMatrixRows(SlipView3DMatrix *matrix, int16_t normalX, int16_t normalY, int16_t normalZ) {
	int row;

	if (matrix == NULL) {
		return 0;
	}
	for (row = 0; row < 3; ++row) {
		const size_t rowOffset = (size_t)row * 3u;
		const int16_t rowX = matrix->m[rowOffset + 0];
		const int16_t rowY = matrix->m[rowOffset + 1];
		const int16_t rowZ = matrix->m[rowOffset + 2];
		const int16_t dotNegated =
		    (int16_t)-SlipView3D_DotProductQ14((uint16_t)normalX, (uint16_t)normalY, (uint16_t)normalZ, (uint16_t)rowX,
		                                       (uint16_t)rowY, (uint16_t)rowZ, NULL);
		const int16_t scaledX = SlipView3D_MultiplySignedWordsQ14(normalX, dotNegated);
		const int16_t scaledY = SlipView3D_MultiplySignedWordsQ14(normalY, dotNegated);
		const int16_t scaledZ = SlipView3D_MultiplySignedWordsQ14(normalZ, dotNegated);

		matrix->m[rowOffset + 0] = (int16_t)(uint16_t)((uint16_t)rowX + (uint16_t)((uint16_t)scaledX << 1));
		matrix->m[rowOffset + 1] = (int16_t)(uint16_t)((uint16_t)rowY + (uint16_t)((uint16_t)scaledY << 1));
		matrix->m[rowOffset + 2] = (int16_t)(uint16_t)((uint16_t)rowZ + (uint16_t)((uint16_t)scaledZ << 1));
	}
	return 1;
}

void SlipView3D_TransposeMatrix(SlipView3DMatrix *matrix) {
	int16_t savedOffDiagonal;

	if (matrix == NULL) {
		return;
	}

	savedOffDiagonal = matrix->m[1];
	matrix->m[1] = matrix->m[3];
	matrix->m[3] = savedOffDiagonal;
	savedOffDiagonal = matrix->m[2];
	matrix->m[2] = matrix->m[6];
	matrix->m[6] = savedOffDiagonal;
	savedOffDiagonal = matrix->m[5];
	matrix->m[5] = matrix->m[7];
	matrix->m[7] = savedOffDiagonal;
}

SlipView3DScaledNormalized2D SlipView3D_ScaleNormalizedVector2D(uint32_t inputX, uint32_t inputY, int32_t scale) {
	SlipDraw3DNormalizeVector2D normalized;
	int64_t scaledComponentProduct;
	int32_t scaledX;
	int32_t scaledY;

	SlipDraw3D_NormalizeVector2D(inputX, inputY, &normalized);
	scaledComponentProduct = (int64_t)normalized.unitXQ14 * scale;
	scaledX = (int32_t)((uint64_t)scaledComponentProduct >> SLIP_Q14_FRACTION_BITS);
	scaledComponentProduct = (int64_t)normalized.unitYQ14 * scale;
	scaledY = (int32_t)((uint64_t)scaledComponentProduct >> SLIP_Q14_FRACTION_BITS);
	return (SlipView3DScaledNormalized2D){scaledX, scaledY};
}

int32_t SlipView3D_ProjectColumn0(const SlipView3DMatrix *matrix, SlipView3DVec16 vector) {
	uint32_t columnDotProduct = (uint32_t)((int32_t)vector.x * matrix->m[0]);

	columnDotProduct += (uint32_t)((int32_t)vector.y * matrix->m[3]);
	columnDotProduct += (uint32_t)((int32_t)vector.z * matrix->m[6]);
	return (int32_t)((columnDotProduct >> SLIP_Q14_FRACTION_BITS) |
	                 (((columnDotProduct & SLIP_VIEW_DWORD_SIGN_BIT) != 0) ? SLIP_VIEW_Q14_SIGN_EXTENSION_MASK : 0));
}

int32_t SlipView3D_ProjectColumn1(const SlipView3DMatrix *matrix, SlipView3DVec16 vector) {
	uint32_t columnDotProduct = (uint32_t)((int32_t)vector.x * matrix->m[1]);

	columnDotProduct += (uint32_t)((int32_t)vector.y * matrix->m[4]);
	columnDotProduct += (uint32_t)((int32_t)vector.z * matrix->m[7]);
	return (int32_t)((columnDotProduct >> SLIP_Q14_FRACTION_BITS) |
	                 (((columnDotProduct & SLIP_VIEW_DWORD_SIGN_BIT) != 0) ? SLIP_VIEW_Q14_SIGN_EXTENSION_MASK : 0));
}

int32_t SlipView3D_ProjectColumn2(const SlipView3DMatrix *matrix, SlipView3DVec16 vector) {
	uint32_t columnDotProduct = (uint32_t)((int32_t)vector.x * matrix->m[2]);

	columnDotProduct += (uint32_t)((int32_t)vector.y * matrix->m[5]);
	columnDotProduct += (uint32_t)((int32_t)vector.z * matrix->m[8]);
	return (int32_t)((columnDotProduct >> SLIP_Q14_FRACTION_BITS) |
	                 (((columnDotProduct & SLIP_VIEW_DWORD_SIGN_BIT) != 0) ? SLIP_VIEW_Q14_SIGN_EXTENSION_MASK : 0));
}

SlipView3DVec32 SlipView3D_TransformPositionByColumns(const SlipView3DMatrix *matrix, SlipView3DVec32 vertex) {
	SlipView3DVec32 out;

	out.x = SlipView3D_DotProductMixedWidthsQ14(matrix->m[0], vertex.x, matrix->m[3], vertex.y, matrix->m[6], vertex.z);
	out.y = SlipView3D_DotProductMixedWidthsQ14(matrix->m[1], vertex.x, matrix->m[4], vertex.y, matrix->m[7], vertex.z);
	out.z = SlipView3D_DotProductMixedWidthsQ14(matrix->m[2], vertex.x, matrix->m[5], vertex.y, matrix->m[8], vertex.z);
	return out;
}

SlipView3DVec32 SlipView3D_TransformPosition16(const SlipView3DMatrix *matrix, SlipView3DVec32 vertex) {
	SlipView3DVec32 out;

	out.x = SlipView3D_DotProductSignedWordsQ14(matrix->m[0], vertex.x, matrix->m[3], vertex.y, matrix->m[6], vertex.z);
	out.y = SlipView3D_DotProductSignedWordsQ14(matrix->m[1], vertex.x, matrix->m[4], vertex.y, matrix->m[7], vertex.z);
	out.z = SlipView3D_DotProductSignedWordsQ14(matrix->m[2], vertex.x, matrix->m[5], vertex.y, matrix->m[8], vertex.z);
	return out;
}

SlipView3DVec32 SlipView3D_ScaleAxesQ14(int16_t xAxis, int16_t yAxis, int16_t zAxis, int32_t scale) {
	SlipView3DVec32 out;

	out.x = SlipView3D_MultiplyWordByDwordQ14(xAxis, scale);
	out.y = SlipView3D_MultiplyWordByDwordQ14(yAxis, scale);
	out.z = SlipView3D_MultiplyWordByDwordQ14(zAxis, scale);
	return out;
}

SlipView3DVec32 SlipView3D_TransformPositionByRows(const SlipView3DMatrix *matrix, SlipView3DVec32 vertex) {
	SlipView3DVec32 out;

	out.x = SlipView3D_DotProductMixedWidthsQ14(matrix->m[0], vertex.x, matrix->m[1], vertex.y, matrix->m[2], vertex.z);
	out.y = SlipView3D_DotProductMixedWidthsQ14(matrix->m[3], vertex.x, matrix->m[4], vertex.y, matrix->m[5], vertex.z);
	out.z = SlipView3D_DotProductMixedWidthsQ14(matrix->m[6], vertex.x, matrix->m[7], vertex.y, matrix->m[8], vertex.z);
	return out;
}

SlipView3DVec32 SlipView3D_TransformVector(const SlipView3DMatrix *matrix, SlipView3DVec32 vector) {
	SlipView3DVec32 out;

	out.x = SlipView3D_DotProductSignedWordsQ14(matrix->m[0], vector.x, matrix->m[1], vector.y, matrix->m[2], vector.z);
	out.y = SlipView3D_DotProductSignedWordsQ14(matrix->m[3], vector.x, matrix->m[4], vector.y, matrix->m[5], vector.z);
	out.z = SlipView3D_DotProductSignedWordsQ14(matrix->m[6], vector.x, matrix->m[7], vector.y, matrix->m[8], vector.z);
	return out;
}

void SlipView3D_BuildBoxCorners(const SlipView3DMatrix *matrix,
                                int32_t boundsAndCorners[SLIP_VIEW_BOX_BOUNDS_AND_CORNERS_COUNT],
                                SlipView3DVec32 translation) {
	SlipView3DVec32 xMin;
	SlipView3DVec32 xMax;
	SlipView3DVec32 yMin;
	SlipView3DVec32 yMax;
	SlipView3DVec32 zMin;
	SlipView3DVec32 zMax;
	uint32_t value;

	xMin.x = SlipView3D_MultiplyWordByDwordQ14(matrix->m[0], boundsAndCorners[SLIP_VIEW_BOX_MIN_X]);
	xMin.y = SlipView3D_MultiplyWordByDwordQ14(matrix->m[1], boundsAndCorners[SLIP_VIEW_BOX_MIN_X]);
	xMin.z = SlipView3D_MultiplyWordByDwordQ14(matrix->m[2], boundsAndCorners[SLIP_VIEW_BOX_MIN_X]);
	if ((int32_t)(0u - (uint32_t)boundsAndCorners[SLIP_VIEW_BOX_MAX_X]) == boundsAndCorners[SLIP_VIEW_BOX_MIN_X]) {
		xMax.x = (int32_t)(0u - (uint32_t)xMin.x);
		xMax.y = (int32_t)(0u - (uint32_t)xMin.y);
		xMax.z = (int32_t)(0u - (uint32_t)xMin.z);
	} else {
		xMax.x = SlipView3D_MultiplyWordByDwordQ14(matrix->m[0], boundsAndCorners[SLIP_VIEW_BOX_MAX_X]);
		xMax.y = SlipView3D_MultiplyWordByDwordQ14(matrix->m[1], boundsAndCorners[SLIP_VIEW_BOX_MAX_X]);
		xMax.z = SlipView3D_MultiplyWordByDwordQ14(matrix->m[2], boundsAndCorners[SLIP_VIEW_BOX_MAX_X]);
	}
	yMin.x = SlipView3D_MultiplyWordByDwordQ14(matrix->m[3], boundsAndCorners[SLIP_VIEW_BOX_MIN_Y]);
	yMin.y = SlipView3D_MultiplyWordByDwordQ14(matrix->m[4], boundsAndCorners[SLIP_VIEW_BOX_MIN_Y]);
	yMin.z = SlipView3D_MultiplyWordByDwordQ14(matrix->m[5], boundsAndCorners[SLIP_VIEW_BOX_MIN_Y]);
	if ((int32_t)(0u - (uint32_t)boundsAndCorners[SLIP_VIEW_BOX_MAX_Y]) == boundsAndCorners[SLIP_VIEW_BOX_MIN_Y]) {
		yMax.x = (int32_t)(0u - (uint32_t)yMin.x);
		yMax.y = (int32_t)(0u - (uint32_t)yMin.y);
		yMax.z = (int32_t)(0u - (uint32_t)yMin.z);
	} else {
		yMax.x = SlipView3D_MultiplyWordByDwordQ14(matrix->m[3], boundsAndCorners[SLIP_VIEW_BOX_MAX_Y]);
		yMax.y = SlipView3D_MultiplyWordByDwordQ14(matrix->m[4], boundsAndCorners[SLIP_VIEW_BOX_MAX_Y]);
		yMax.z = SlipView3D_MultiplyWordByDwordQ14(matrix->m[5], boundsAndCorners[SLIP_VIEW_BOX_MAX_Y]);
	}
	zMin.x = SlipView3D_MultiplyWordByDwordQ14(matrix->m[6], boundsAndCorners[SLIP_VIEW_BOX_MIN_Z]);
	zMin.y = SlipView3D_MultiplyWordByDwordQ14(matrix->m[7], boundsAndCorners[SLIP_VIEW_BOX_MIN_Z]);
	zMin.z = SlipView3D_MultiplyWordByDwordQ14(matrix->m[8], boundsAndCorners[SLIP_VIEW_BOX_MIN_Z]);
	if ((int32_t)(0u - (uint32_t)boundsAndCorners[SLIP_VIEW_BOX_MAX_Z]) == boundsAndCorners[SLIP_VIEW_BOX_MIN_Z]) {
		zMax.x = (int32_t)(0u - (uint32_t)zMin.x);
		zMax.y = (int32_t)(0u - (uint32_t)zMin.y);
		zMax.z = (int32_t)(0u - (uint32_t)zMin.z);
	} else {
		zMax.x = SlipView3D_MultiplyWordByDwordQ14(matrix->m[6], boundsAndCorners[SLIP_VIEW_BOX_MAX_Z]);
		zMax.y = SlipView3D_MultiplyWordByDwordQ14(matrix->m[7], boundsAndCorners[SLIP_VIEW_BOX_MAX_Z]);
		zMax.z = SlipView3D_MultiplyWordByDwordQ14(matrix->m[8], boundsAndCorners[SLIP_VIEW_BOX_MAX_Z]);
	}
	value = (uint32_t)xMin.x + (uint32_t)yMin.x + (uint32_t)zMax.x + (uint32_t)translation.x;
	boundsAndCorners[SLIP_VIEW_BOX_MIN_MIN_MAX + 0] = (int32_t)value;
	value = (uint32_t)xMin.y + (uint32_t)yMin.y + (uint32_t)zMax.y + (uint32_t)translation.y;
	boundsAndCorners[SLIP_VIEW_BOX_MIN_MIN_MAX + 1] = (int32_t)value;
	value = (uint32_t)xMin.z + (uint32_t)yMin.z + (uint32_t)zMax.z + (uint32_t)translation.z;
	boundsAndCorners[SLIP_VIEW_BOX_MIN_MIN_MAX + 2] = (int32_t)value;
	value = (uint32_t)xMax.x + (uint32_t)yMin.x + (uint32_t)zMax.x + (uint32_t)translation.x;
	boundsAndCorners[SLIP_VIEW_BOX_MAX_MIN_MAX + 0] = (int32_t)value;
	value = (uint32_t)xMax.y + (uint32_t)yMin.y + (uint32_t)zMax.y + (uint32_t)translation.y;
	boundsAndCorners[SLIP_VIEW_BOX_MAX_MIN_MAX + 1] = (int32_t)value;
	value = (uint32_t)xMax.z + (uint32_t)yMin.z + (uint32_t)zMax.z + (uint32_t)translation.z;
	boundsAndCorners[SLIP_VIEW_BOX_MAX_MIN_MAX + 2] = (int32_t)value;
	value = (uint32_t)xMax.x + (uint32_t)yMax.x + (uint32_t)zMax.x + (uint32_t)translation.x;
	boundsAndCorners[SLIP_VIEW_BOX_MAX_MAX_MAX + 0] = (int32_t)value;
	value = (uint32_t)xMax.y + (uint32_t)yMax.y + (uint32_t)zMax.y + (uint32_t)translation.y;
	boundsAndCorners[SLIP_VIEW_BOX_MAX_MAX_MAX + 1] = (int32_t)value;
	value = (uint32_t)xMax.z + (uint32_t)yMax.z + (uint32_t)zMax.z + (uint32_t)translation.z;
	boundsAndCorners[SLIP_VIEW_BOX_MAX_MAX_MAX + 2] = (int32_t)value;
	value = (uint32_t)xMin.x + (uint32_t)yMax.x + (uint32_t)zMax.x + (uint32_t)translation.x;
	boundsAndCorners[SLIP_VIEW_BOX_MIN_MAX_MAX + 0] = (int32_t)value;
	value = (uint32_t)xMin.y + (uint32_t)yMax.y + (uint32_t)zMax.y + (uint32_t)translation.y;
	boundsAndCorners[SLIP_VIEW_BOX_MIN_MAX_MAX + 1] = (int32_t)value;
	value = (uint32_t)xMin.z + (uint32_t)yMax.z + (uint32_t)zMax.z + (uint32_t)translation.z;
	boundsAndCorners[SLIP_VIEW_BOX_MIN_MAX_MAX + 2] = (int32_t)value;
	value = (uint32_t)xMin.x + (uint32_t)yMin.x + (uint32_t)zMin.x + (uint32_t)translation.x;
	boundsAndCorners[SLIP_VIEW_BOX_MIN_MIN_MIN + 0] = (int32_t)value;
	value = (uint32_t)xMin.y + (uint32_t)yMin.y + (uint32_t)zMin.y + (uint32_t)translation.y;
	boundsAndCorners[SLIP_VIEW_BOX_MIN_MIN_MIN + 1] = (int32_t)value;
	value = (uint32_t)xMin.z + (uint32_t)yMin.z + (uint32_t)zMin.z + (uint32_t)translation.z;
	boundsAndCorners[SLIP_VIEW_BOX_MIN_MIN_MIN + 2] = (int32_t)value;
	value = (uint32_t)xMax.x + (uint32_t)yMin.x + (uint32_t)zMin.x + (uint32_t)translation.x;
	boundsAndCorners[SLIP_VIEW_BOX_MAX_MIN_MIN + 0] = (int32_t)value;
	value = (uint32_t)xMax.y + (uint32_t)yMin.y + (uint32_t)zMin.y + (uint32_t)translation.y;
	boundsAndCorners[SLIP_VIEW_BOX_MAX_MIN_MIN + 1] = (int32_t)value;
	value = (uint32_t)xMax.z + (uint32_t)yMin.z + (uint32_t)zMin.z + (uint32_t)translation.z;
	boundsAndCorners[SLIP_VIEW_BOX_MAX_MIN_MIN + 2] = (int32_t)value;
	value = (uint32_t)xMax.x + (uint32_t)yMax.x + (uint32_t)zMin.x + (uint32_t)translation.x;
	boundsAndCorners[SLIP_VIEW_BOX_MAX_MAX_MIN + 0] = (int32_t)value;
	value = (uint32_t)xMax.y + (uint32_t)yMax.y + (uint32_t)zMin.y + (uint32_t)translation.y;
	boundsAndCorners[SLIP_VIEW_BOX_MAX_MAX_MIN + 1] = (int32_t)value;
	value = (uint32_t)xMax.z + (uint32_t)yMax.z + (uint32_t)zMin.z + (uint32_t)translation.z;
	boundsAndCorners[SLIP_VIEW_BOX_MAX_MAX_MIN + 2] = (int32_t)value;
	value = (uint32_t)xMin.x + (uint32_t)yMax.x + (uint32_t)zMin.x + (uint32_t)translation.x;
	boundsAndCorners[SLIP_VIEW_BOX_MIN_MAX_MIN + 0] = (int32_t)value;
	value = (uint32_t)xMin.y + (uint32_t)yMax.y + (uint32_t)zMin.y + (uint32_t)translation.y;
	boundsAndCorners[SLIP_VIEW_BOX_MIN_MAX_MIN + 1] = (int32_t)value;
	value = (uint32_t)xMin.z + (uint32_t)yMax.z + (uint32_t)zMin.z + (uint32_t)translation.z;
	boundsAndCorners[SLIP_VIEW_BOX_MIN_MAX_MIN + 2] = (int32_t)value;
}

void SlipView3D_BuildBoxCornersThunk(const SlipView3DMatrix *matrix,
                                     int32_t boundsAndCorners[SLIP_VIEW_BOX_BOUNDS_AND_CORNERS_COUNT],
                                     SlipView3DVec32 translation) {
	SlipView3D_BuildBoxCorners(matrix, boundsAndCorners, translation);
}

SlipView3DVec32 SlipView3D_TransformVertex(const SlipView3DMatrix *matrix, SlipView3DVec32 translation,
                                           SlipView3DVec16 vertex) {
	SlipView3DVec32 out;
	const int32_t x = vertex.x;
	const int32_t y = vertex.y;
	const int32_t z = vertex.z;
	const int32_t transformedX =
	    (int32_t)((uint32_t)(x * matrix->m[0]) + (uint32_t)(y * matrix->m[3]) + (uint32_t)(z * matrix->m[6]));
	const int32_t transformedY =
	    (int32_t)((uint32_t)(x * matrix->m[1]) + (uint32_t)(y * matrix->m[4]) + (uint32_t)(z * matrix->m[7]));
	const int32_t transformedZ =
	    (int32_t)((uint32_t)(x * matrix->m[2]) + (uint32_t)(y * matrix->m[5]) + (uint32_t)(z * matrix->m[8]));

	out.x = (transformedX >> SLIP_VIEW_TRANSFORMED_VERTEX_SHIFT) + translation.x;
	out.y = (transformedY >> SLIP_VIEW_TRANSFORMED_VERTEX_SHIFT) + translation.y;
	out.z = (transformedZ >> SLIP_VIEW_TRANSFORMED_VERTEX_SHIFT) + translation.z;
	return out;
}

SlipView3DVec32 SlipView3D_LocalVertex(SlipView3DVec32 localOffset, SlipView3DVec16 vertex) {
	SlipView3DVec32 out;

	out.x = ((int32_t)vertex.x << SLIP_VIEW_LOCAL_VERTEX_SCALE_SHIFT) + localOffset.x;
	out.y = ((int32_t)vertex.y << SLIP_VIEW_LOCAL_VERTEX_SCALE_SHIFT) + localOffset.y;
	out.z = ((int32_t)vertex.z << SLIP_VIEW_LOCAL_VERTEX_SCALE_SHIFT) + localOffset.z;
	return out;
}
