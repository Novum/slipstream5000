#ifndef SLIPSTREAM5000_VIEW3D_H
#define SLIPSTREAM5000_VIEW3D_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* MATHS.BIN starts with three little-endian offsets to its lookup tables. */
enum {
	SLIP_MATHS_SINE_TABLE_OFFSET = 0,
	SLIP_MATHS_ARCSINE_TABLE_OFFSET = 2,
	SLIP_MATHS_ARCTANGENT_TABLE_OFFSET = 4,
	SLIP_MATHS_HEADER_BYTES = 6
};

typedef struct SlipView3DMaths {
	uint8_t *data;
	size_t size;
	uint16_t arctangentTableOffset;
	uint16_t sineTableOffset;
	uint16_t arcsineTableOffset;
} SlipView3DMaths;

typedef struct SlipView3DMatrix {
	int16_t m[9];
} SlipView3DMatrix;

typedef struct SlipView3DVec16 {
	int16_t x;
	int16_t y;
	int16_t z;
} SlipView3DVec16;

typedef struct SlipView3DVec32 {
	int32_t x;
	int32_t y;
	int32_t z;
} SlipView3DVec32;

typedef struct SlipView3DRotateVectorTowards {
	SlipView3DVec32 vector;
	bool carry;
} SlipView3DRotateVectorTowards;

uint32_t SlipView3D_ApproximateLength(int32_t x, int32_t y, int32_t z);
uint32_t SlipView3D_VectorLength(int32_t vectorX, int32_t vectorY, int32_t vectorZ);

uint32_t SlipView3D_BoxRadius(int32_t minX, int32_t minY, int32_t minZ, int32_t maxX, int32_t maxY, int32_t maxZ);

SlipView3DVec32 SlipView3D_ScaleVector(int16_t vectorXQ14, int16_t vectorYQ14, int16_t vectorZQ14, int32_t scaleQ14);

SlipView3DVec32 SlipView3D_ProjectPointToPlane(SlipView3DVec32 point, SlipView3DVec32 planeOrigin,
                                               SlipView3DVec32 planeNormal);

typedef struct SlipView3DScaleVector2D {
	uint32_t inputX;
	uint32_t inputY;
	int negativeX;
	uint32_t absoluteX;
	int negativeY;
	uint32_t absoluteY;
	uint32_t highComponentBits;
	int shifted;
	uint32_t shiftCount;
	uint32_t scaledX;
	uint32_t scaledY;
	uint32_t appliedShift;
	int operationCompleted;
} SlipView3DScaleVector2D;

typedef struct SlipView3DNormalizeScaledVector2D {
	SlipView3DScaleVector2D scale;
	int normalize2DCalled;
	int16_t unitXQ14;
	int16_t unitYQ14;
	uint16_t length;
	int operationCompleted;
} SlipView3DNormalizeScaledVector2D;

typedef struct SlipView3DScaledNormalized2D {
	int32_t scaledNormalizedX;
	int32_t scaledNormalizedY;
} SlipView3DScaledNormalized2D;

typedef struct SlipView3DScaleVector3D {
	uint32_t inputX;
	uint32_t inputY;
	uint32_t inputZ;
	int negativeX;
	int negativeY;
	int negativeZ;
	uint32_t scaleMask;
	uint32_t shiftCount;
	uint32_t scaledX;
	uint32_t scaledY;
	uint32_t scaledZ;
	uint32_t appliedShift;
	int operationCompleted;
} SlipView3DScaleVector3D;

typedef struct SlipView3DNormalizeLength3D {
	uint32_t inputX;
	uint32_t inputY;
	uint32_t inputZ;
	uint32_t squareX;
	uint32_t squareY;
	uint32_t squareZ;
	uint32_t squareSum;
	uint16_t lengthRootBeforeDivision;
	int rootIsUnitLength;
	int rootIsZero;
	int rootHasSignBit;
	uint16_t divisor;
	uint32_t unitXQ14;
	uint32_t unitYQ14;
	uint32_t unitZQ14;
	uint32_t lengthRoot;
	int operationCompleted;
} SlipView3DNormalizeLength3D;

typedef struct SlipView3DNormalizeVector3D {
	SlipView3DScaleVector3D scale;
	int normalizeLength3DCalled;
	SlipView3DNormalizeLength3D length;
	uint32_t savedShift;
	uint32_t scaledVectorLength;
	uint32_t vectorLength;
	uint32_t unitXQ14;
	uint32_t unitYQ14;
	uint32_t unitZQ14;
	int operationCompleted;
} SlipView3DNormalizeVector3D;

typedef struct SlipView3DDotProduct32 {
	uint64_t productX;
	uint64_t productY;
	uint64_t sumXY;
	uint64_t productZ;
	uint64_t sumXYZ;
	uint32_t dotProductLow;
	uint32_t dotProductHigh;
	int operationCompleted;
} SlipView3DDotProduct32;

typedef struct SlipView3DDotProduct32By16 {
	uint64_t productX;
	uint64_t productY;
	uint64_t sumXY;
	uint64_t productZ;
	uint64_t sumXYZ;
	uint32_t dotProductLow;
	uint32_t dotProductHigh;
} SlipView3DDotProduct32By16;

typedef struct SlipView3DCrossProduct {
	uint32_t inputRhsYBits;
	uint32_t xOut;
	uint32_t yOut;
	uint32_t zOut;
	uint32_t crossX;
	uint32_t crossY;
	uint32_t crossZ;
	int operationCompleted;
} SlipView3DCrossProduct;

typedef struct SlipView3DDotProductQ14 {
	uint16_t productXLow;
	uint16_t productXHigh;
	uint16_t productYLow;
	uint16_t productYHigh;
	uint32_t sumXY;
	uint32_t productZ;
	uint32_t sumXYZ;
	uint16_t dotProductBeforeRounding;
	uint32_t dotProductQ14;
	uint32_t xySumHigh;
	uint32_t inputZ;
	uint32_t xyzSumHigh;
	int operationCompleted;
} SlipView3DDotProductQ14;

void SlipView3D_FreeMaths(SlipView3DMaths *maths);
int SlipView3D_InitMathsFromPayload(SlipView3DMaths *maths, const uint8_t *data, size_t size);
int SlipView3D_LoadMathsFromArchives(SlipView3DMaths *maths, const char *const *resPaths, size_t resPathCount);

int16_t SlipView3D_SinQ14(const SlipView3DMaths *maths, int16_t angle);
int16_t SlipView3D_CosQ14(const SlipView3DMaths *maths, int16_t angle);
int16_t SlipView3D_SmallAngleSinQ14(int16_t angle);
int16_t SlipView3D_LookupSineQ14(const SlipView3DMaths *maths, uint16_t index);
int16_t SlipView3D_LookupArcsineAngle(const SlipView3DMaths *maths, uint16_t index);
int16_t SlipView3D_TanQ14(const SlipView3DMaths *maths, int16_t angle);

void SlipView3D_BuildYawMatrix(const SlipView3DMaths *maths, int16_t angle, SlipView3DMatrix *matrix);

void SlipView3D_ApplyPitchMatrix(const SlipView3DMaths *maths, int16_t angle, SlipView3DMatrix *matrix);

void SlipView3D_ApplyRow0Row2Rotation(const SlipView3DMaths *maths, int16_t angle, SlipView3DMatrix *matrix);

void SlipView3D_ApplyRow0Row1Rotation(const SlipView3DMaths *maths, int16_t angle, SlipView3DMatrix *matrix);

void SlipView3D_ApplyColumn0Column2Rotation(const SlipView3DMaths *maths, int16_t angle, SlipView3DMatrix *matrix);

void SlipView3D_OrthonormalizeForwardBasis(SlipView3DMatrix *matrix);
void SlipView3D_OrthonormalizeRightBasis(SlipView3DMatrix *matrix);
int16_t SlipView3D_HeadingFromMatrix(const SlipView3DMaths *maths, const SlipView3DMatrix *matrix);
int16_t SlipView3D_PitchFromMatrix(const SlipView3DMaths *maths, const SlipView3DMatrix *matrix);
int16_t SlipView3D_RollFromMatrix(const SlipView3DMaths *maths, const SlipView3DMatrix *matrix);
bool SlipView3D_LevelHeading(const SlipView3DMaths *maths, SlipView3DMatrix *matrix, uint32_t maximumStep);
bool SlipView3D_ApproachAngles(const SlipView3DMaths *maths, SlipView3DMatrix *matrix, uint16_t targetRoll,
                               uint16_t targetPitch, int32_t rollStep, int32_t pitchStep);
SlipView3DRotateVectorTowards SlipView3D_RotateVectorTowards(const SlipView3DMaths *maths, int16_t currentX,
                                                             int16_t currentY, int16_t currentZ, int16_t targetX,
                                                             int16_t targetY, int16_t targetZ, uint32_t maximumStep);
void SlipView3D_BuildAxisRotation(uint16_t sineBits, uint16_t cosineBits, uint16_t axisXBits, uint16_t axisYBits,
                                  uint16_t axisZBits, SlipView3DMatrix *matrix);
bool SlipView3D_RotateForwardTowards(const SlipView3DMaths *maths, SlipView3DMatrix *matrix, int16_t targetX,
                                     int16_t targetY, int16_t targetZ, uint32_t maximumStep);
bool SlipView3D_RotateRightTowards(const SlipView3DMaths *maths, SlipView3DMatrix *matrix, int16_t targetX,
                                   int16_t targetY, int16_t targetZ, uint32_t maximumStep);

void SlipView3D_MultiplyMatrix(const SlipView3DMatrix *lhs, const SlipView3DMatrix *rhs, SlipView3DMatrix *out);

int SlipView3D_ScaleVector2D(uint32_t inputX, uint32_t inputY, SlipView3DScaleVector2D *result);

int SlipView3D_NormalizeScaledVector2D(uint32_t inputX, uint32_t inputY, SlipView3DNormalizeScaledVector2D *result);

SlipView3DScaledNormalized2D SlipView3D_ScaleNormalizedVector2D(uint32_t inputX, uint32_t inputY, int32_t scale);

int SlipView3D_ScaleVector3D(uint32_t inputX, uint32_t inputY, uint32_t inputZ, SlipView3DScaleVector3D *result);

int SlipView3D_NormalizeLength3D(uint32_t inputX, uint32_t inputY, uint32_t inputZ,
                                 SlipView3DNormalizeLength3D *result);

int SlipView3D_NormalizeVector3D(uint32_t inputX, uint32_t inputY, uint32_t inputZ,
                                 SlipView3DNormalizeVector3D *result);

void SlipView3D_DotProduct32(uint32_t lhsX, uint32_t lhsY, uint32_t lhsZ, uint32_t rhsX, uint32_t rhsY, uint32_t rhsZ,
                             SlipView3DDotProduct32 *result);

void SlipView3D_DotProduct32By16(uint32_t lhsX, uint32_t lhsY, uint32_t lhsZ, uint16_t rhsX, uint16_t rhsY,
                                 uint16_t rhsZ, SlipView3DDotProduct32By16 *result);

void SlipView3D_CrossProduct(uint32_t lhsX, uint32_t lhsY, uint32_t lhsZ, uint32_t rhsX, uint32_t rhsY, uint32_t rhsZ,
                             SlipView3DCrossProduct *result);

uint32_t SlipView3D_DotProductQ14(uint16_t lhsXQ14, uint16_t lhsYQ14, uint16_t lhsZQ14, uint16_t rhsXQ14,
                                  uint16_t rhsYQ14, uint16_t rhsZQ14, SlipView3DDotProductQ14 *result);

void SlipView3D_ComposeMatrix(const SlipView3DMatrix *lhs, const SlipView3DMatrix *rhs, SlipView3DMatrix *out);

int SlipView3D_CopyMatrixWords(uint8_t *destination, size_t destBytesRemaining, const uint8_t *source,
                               size_t srcBytesRemaining);

int SlipView3D_BuildMatrixFromVector(SlipView3DMatrix *matrix, int16_t directionX, int16_t directionY,
                                     int16_t directionZ);

void SlipView3D_BuildFacingBasis(SlipView3DMatrix *basis, int16_t directionX, int16_t directionY, int16_t directionZ,
                                 int16_t referenceX, int16_t referenceY, int16_t referenceZ);

int SlipView3D_BuildMatrixFromVector32(SlipView3DMatrix *matrix, uint32_t directionX, uint32_t directionY,
                                       uint32_t directionZ);

int SlipView3D_ReflectMatrixRows(SlipView3DMatrix *matrix, int16_t normalX, int16_t normalY, int16_t normalZ);

void SlipView3D_TransposeMatrix(SlipView3DMatrix *matrix);

SlipView3DVec32 SlipView3D_TransformPositionByColumns(const SlipView3DMatrix *matrix, SlipView3DVec32 vertex);

int32_t SlipView3D_ProjectColumn0(const SlipView3DMatrix *matrix, SlipView3DVec16 vector);

int32_t SlipView3D_ProjectColumn1(const SlipView3DMatrix *matrix, SlipView3DVec16 vector);

int32_t SlipView3D_ProjectColumn2(const SlipView3DMatrix *matrix, SlipView3DVec16 vector);

SlipView3DVec32 SlipView3D_TransformPosition16(const SlipView3DMatrix *matrix, SlipView3DVec32 vertex);

SlipView3DVec32 SlipView3D_ScaleAxesQ14(int16_t xAxis, int16_t yAxis, int16_t zAxis, int32_t scale);

SlipView3DVec32 SlipView3D_TransformPositionByRows(const SlipView3DMatrix *matrix, SlipView3DVec32 vertex);

SlipView3DVec32 SlipView3D_TransformVector(const SlipView3DMatrix *matrix, SlipView3DVec32 vector);

/* Six input bounds followed by eight output XYZ triples. Corner names
 * specify the selected X, Y and Z bounds, in that order. */
enum {
	SLIP_VIEW_BOX_MIN_X = 0,
	SLIP_VIEW_BOX_MIN_Y = 1,
	SLIP_VIEW_BOX_MIN_Z = 2,
	SLIP_VIEW_BOX_MAX_X = 3,
	SLIP_VIEW_BOX_MAX_Y = 4,
	SLIP_VIEW_BOX_MAX_Z = 5,
	SLIP_VIEW_BOX_MIN_MIN_MAX = 6,
	SLIP_VIEW_BOX_MIN_MAX_MAX = 9,
	SLIP_VIEW_BOX_MAX_MAX_MAX = 12,
	SLIP_VIEW_BOX_MAX_MIN_MAX = 15,
	SLIP_VIEW_BOX_MIN_MIN_MIN = 18,
	SLIP_VIEW_BOX_MIN_MAX_MIN = 21,
	SLIP_VIEW_BOX_MAX_MAX_MIN = 24,
	SLIP_VIEW_BOX_MAX_MIN_MIN = 27,
	SLIP_VIEW_BOX_BOUNDS_AND_CORNERS_COUNT = 30
};

void SlipView3D_BuildBoxCorners(const SlipView3DMatrix *matrix,
                                int32_t boundsAndCorners[SLIP_VIEW_BOX_BOUNDS_AND_CORNERS_COUNT],
                                SlipView3DVec32 translation);

void SlipView3D_BuildBoxCornersThunk(const SlipView3DMatrix *matrix,
                                     int32_t boundsAndCorners[SLIP_VIEW_BOX_BOUNDS_AND_CORNERS_COUNT],
                                     SlipView3DVec32 translation);

SlipView3DVec32 SlipView3D_TransformVertex(const SlipView3DMatrix *matrix, SlipView3DVec32 translation,
                                           SlipView3DVec16 vertex);

SlipView3DVec32 SlipView3D_LocalVertex(SlipView3DVec32 localOffset, SlipView3DVec16 vertex);

#endif
