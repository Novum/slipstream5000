#ifndef SLIPSTREAM5000_RASTER_H
#define SLIPSTREAM5000_RASTER_H

#include "sprite.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SLIPSTREAM_SCREEN_WIDTH 320
#define SLIPSTREAM_SCREEN_HEIGHT 200

enum { RASTER_PERSPECTIVE_ENTRY_COUNT = 502 };

typedef struct RasterPerspectiveSplit {
	uint16_t screenFraction;
	uint16_t textureFraction;
} RasterPerspectiveSplit;

typedef struct RasterPerspectiveEntry {
	RasterPerspectiveSplit splits[16];
} RasterPerspectiveEntry;

void Raster_BuildPerspectiveTable(RasterPerspectiveEntry table[RASTER_PERSPECTIVE_ENTRY_COUNT]);
extern RasterPerspectiveEntry *Raster_perspectiveTable;

extern uint8_t *g_screenBufferBase;

extern intptr_t g_screenRowOffsets[SLIPSTREAM_SCREEN_HEIGHT];

extern uint8_t *g_screenRowPtrs[SLIPSTREAM_SCREEN_HEIGHT];

extern int32_t g_screenPitch;

extern int16_t g_clipMinX;
extern int16_t g_clipMinY;
extern int16_t g_clipMaxX;
extern int16_t g_clipMaxY;

typedef struct RasterPoint {
	int32_t x;
	int32_t y;
} RasterPoint;

typedef struct RasterSurfaceBinding {
	uint8_t *screenBuffer;
	int32_t screenPitch;
	int16_t minX, minY, maxX, maxY;
} RasterSurfaceBinding;

void Raster_BindSprite(uint8_t *pixels, uint16_t width, uint16_t height, RasterSurfaceBinding *saved);
void Raster_RestoreScreen(const RasterSurfaceBinding *saved);

typedef struct RasterSurfaceBounds {
	int32_t left, top, right, bottom;
} RasterSurfaceBounds;

RasterSurfaceBounds Raster_GetSurfaceBounds(void);

typedef struct RasterTexturedPoint {
	int32_t x, y;
	uint32_t reserved08, u, v;
	int32_t depth;
	uint32_t scaledU, scaledV;
} RasterTexturedPoint;

void Raster_ScaleTextureCoordinates(uint16_t width, uint16_t height, RasterTexturedPoint *points, uint32_t count);

typedef char RasterTexturedPointSize[sizeof(RasterTexturedPoint) == 0x20 ? 1 : -1];

typedef struct RasterShadedPoint {
	int32_t x;
	int32_t y;
	uint16_t shade;
} RasterShadedPoint;

typedef struct RasterTexturedEntryScanVisit {
	uint32_t pointOffset;
	int32_t pointX;
	int32_t pointY;
	int32_t bottomYBefore;
	int32_t topYBefore;
	uint32_t topLeftPointOffsetBefore;
	uint32_t topRightPointOffsetBefore;
	bool updateBottomY;
	bool updateTopPair;
	bool tieUpdateLeft;
	bool tieUpdateRight;
	uint32_t pointOffsetAfterAdd;
	uint32_t remainingCountAfterDec;
	bool loop;
} RasterTexturedEntryScanVisit;

typedef struct RasterTexturedEntrySetup {
	bool pushad;
	bool savedPointCursor;
	uint32_t textureHandle;
	bool calledLockTextureResource;
	uintptr_t lockedPayload;
	uintptr_t storedTexturePayload;
	bool restoredPointCursor;
	uint16_t textureHeaderWord;
	bool branchTextureHeaderMinusOne;
	bool popad;
	bool calledUnlockTextureResource;
	bool jumpMaskedPerspectivePolygon;
	bool calledPrepareTextureUVExtents;
	bool calledBuildTextureRowTable;
	uint32_t pointBufferBase;
	uint32_t inputPointCount;
	uint32_t pointCountAfterDec;
	int32_t topY;
	int32_t bottomY;
	uint32_t firstNextPointOffset;
	uint32_t topLeftPointOffset;
	uint32_t topRightPointOffset;
	size_t scanVisitCount;
	uint32_t endPointOffset;
	bool horizontalReturn;
	uint32_t storedBottomY;
	bool calledStepLeftEdgeTexturedPerspective;
	bool calledStepRightEdgeTexturedPerspective;
	bool calledSetupPerspectiveTexturedSpan;
	bool jumpOpaquePerspectiveScanlineLoop;
} RasterTexturedEntrySetup;

typedef struct RasterTextureRowTable {
	bool pushad;
	uint16_t textureWidth;
	uint16_t textureHeight;
	uint32_t rowScroll;
	uint32_t rotationRowOffset;
	bool directRows;
	bool usedRotatedRows;
	uint32_t firstPassRows;
	uint32_t secondPassRows;
	size_t rowsWritten;
	bool popad;
} RasterTextureRowTable;

typedef struct RasterTextureUVExtentsVisit {
	uint32_t pointOffset;
	uint32_t rawU;
	uint32_t scaledU;
	uint32_t rawV;
	uint32_t scaledV;
	uint32_t remainingAfterDec;
	bool loop;
} RasterTextureUVExtentsVisit;

typedef struct RasterTextureUVExtents {
	bool savedMultiplyLowWorkValue;
	bool savedUScaleWorkValue;
	bool savedPointCount;
	bool savedMultiplyHighWorkValue;
	bool savedVScaleWorkValue;
	bool savedPointCursor;
	uint16_t textureWidth;
	uint16_t textureHeight;
	uint32_t uScale;
	uint32_t vScale;
	size_t visitCount;
	bool restoredPointCursor;
	bool restoredVScaleWorkValue;
	bool restoredMultiplyHighWorkValue;
	bool restoredPointCount;
	bool restoredUScaleWorkValue;
	bool restoredMultiplyLowWorkValue;
	bool ret;
} RasterTextureUVExtents;

typedef struct RasterAffineTexturedEntryScanVisit {
	uint32_t pointOffset;
	int32_t pointX;
	int32_t pointY;
	int32_t bottomYBefore;
	int32_t topYBefore;
	uint32_t topLeftPointOffsetBefore;
	uint32_t topRightPointOffsetBefore;
	bool updateBottomY;
	bool updateTopPair;
	bool tieUpdateLeft;
	bool tieUpdateRight;
	uint32_t pointOffsetAfterAdd;
	uint32_t remainingCountAfterDec;
	bool loop;
} RasterAffineTexturedEntryScanVisit;

typedef struct RasterAffineTexturedEntrySetup {
	bool pushad;
	bool savedPointCursor;
	uint32_t textureHandle;
	bool calledLockTextureResource;
	uintptr_t lockedPayload;
	uintptr_t storedTexturePayload;
	bool restoredPointCursor;
	bool calledPrepareTextureUVExtents;
	bool calledBuildTextureRowTable;
	uint32_t pointBufferBase;
	uint32_t inputPointCount;
	uint32_t pointCountAfterDec;
	int32_t topY;
	int32_t bottomY;
	uint32_t firstNextPointOffset;
	uint32_t topLeftPointOffset;
	uint32_t topRightPointOffset;
	size_t scanVisitCount;
	uint32_t endPointOffset;
	bool horizontalBranch;
	uint32_t storedBottomY;
	bool calledStepLeftEdgeAffine;
	bool calledStepRightEdgeAffine;
	bool spanLoopEntry;
} RasterAffineTexturedEntrySetup;

typedef struct RasterAffineLeftEdgeStep {
	uint32_t pointOffsetIn;
	int32_t currentX;
	uint32_t spanLeftTexU;
	uint32_t spanLeftTexV;
	int32_t currentY;
	int32_t bottomY;
	bool atBottom;
	uint32_t wrappedEndOffset;
	uint32_t candidateOffset;
	int32_t candidateY;
	bool carryCandidateAbove;
	bool horizontalRestart;
	int32_t candidateX;
	uint32_t edgeHeight;
	int32_t texUStep;
	int32_t texVStep;
	uint16_t remaining;
	int32_t xStep;
	uint16_t xFraction;
	uint32_t pointOffsetOut;
	bool carryOut;
} RasterAffineLeftEdgeStep;

typedef struct RasterAffineRightEdgeStep {
	uint32_t pointOffsetIn;
	int32_t currentX;
	uint32_t spanRightTexU;
	uint32_t spanRightTexV;
	int32_t currentY;
	int32_t bottomY;
	bool atBottom;
	uint32_t wrappedBaseOffset;
	uint32_t candidateOffset;
	int32_t candidateY;
	bool carryCandidateAbove;
	bool horizontalRestart;
	int32_t candidateX;
	uint32_t edgeHeight;
	int32_t texUStep;
	int32_t texVStep;
	uint16_t remaining;
	int32_t xStep;
	uint16_t xFraction;
	uint32_t pointOffsetOut;
	bool carryOut;
} RasterAffineRightEdgeStep;

typedef struct RasterTexturedLeftEdgeStep {
	uint32_t pointOffsetIn;
	int32_t currentX;
	uint32_t currentDepth;
	uint32_t currentScaledU;
	uint32_t currentScaledV;
	uint32_t leftBaseDepth;
	uint32_t leftBaseTexU;
	uint32_t leftBaseTexV;
	int32_t currentY;
	int32_t bottomY;
	bool atBottom;
	uint32_t wrappedEndOffset;
	uint32_t candidateOffset;
	int32_t candidateY;
	bool carryCandidateAbove;
	bool horizontalRestart;
	int32_t candidateX;
	uint32_t edgeHeight;
	uint16_t remaining;
	uint32_t accumulatedBaseDepthPerRow;
	uint32_t accumulatedNextPointDepthPerRow;
	uint32_t baseDepthPerRow;
	uint32_t nextPointDepthPerRow;
	int32_t xStep;
	uint16_t xFraction;
	uint32_t pointOffsetOut;
	bool carryOut;
} RasterTexturedLeftEdgeStep;

typedef struct RasterTexturedRightEdgeStep {
	uint32_t pointOffsetIn;
	int32_t currentX;
	uint32_t currentDepth;
	uint32_t currentScaledU;
	uint32_t currentScaledV;
	uint32_t rightBaseDepth;
	uint32_t rightBaseTexU;
	uint32_t rightBaseTexV;
	int32_t currentY;
	int32_t bottomY;
	bool atBottom;
	uint32_t wrappedBaseOffset;
	uint32_t candidateOffset;
	int32_t candidateY;
	bool carryCandidateAbove;
	bool horizontalRestart;
	int32_t candidateX;
	uint32_t edgeHeight;
	uint16_t remaining;
	uint32_t accumulatedBaseDepthPerRow;
	uint32_t accumulatedNextPointDepthPerRow;
	uint32_t baseDepthPerRow;
	uint32_t nextPointDepthPerRow;
	int32_t xStep;
	uint16_t xFraction;
	uint32_t pointOffsetOut;
	bool carryOut;
} RasterTexturedRightEdgeStep;

typedef struct RasterTexturedAdvanceEdgesState {
	int32_t scanY;
	int32_t leftX;
	int32_t rightX;
	uint32_t leftAccumulatedBaseDepthPerRow;
	uint32_t leftBaseDepthPerRow;
	uint32_t leftAccumulatedNextPointDepthPerRow;
	uint32_t leftNextPointDepthPerRow;
	uint32_t rightAccumulatedBaseDepthPerRow;
	uint32_t rightBaseDepthPerRow;
	uint32_t rightAccumulatedNextPointDepthPerRow;
	uint32_t rightNextPointDepthPerRow;
	int32_t leftXStep;
	uint16_t leftXFraction;
	uint16_t leftRemaining;
	int32_t rightXStep;
	uint16_t rightXFraction;
	uint16_t rightRemaining;
} RasterTexturedAdvanceEdgesState;

typedef struct RasterTexturedAdvanceEdges {
	bool resetOneRowFlag;
	bool leftRemainingOne;
	bool rightRemainingOne;
	uint32_t oneRowFlag;
	RasterTexturedAdvanceEdgesState in;
	RasterTexturedAdvanceEdgesState out;
	bool leftFractionCarry;
	bool rightFractionCarry;
	bool calledStepLeftEdgeTexturedPerspective;
	bool carryFromLeftPerspectiveEdge;
	bool jumpAfterLeftCarry;
	bool calledStepRightEdgeTexturedPerspective;
	bool carryFromRightPerspectiveEdge;
	bool jumpAfterRightCarry;
	bool ret;
} RasterTexturedAdvanceEdges;

typedef struct RasterTexturedSpanSetupState {
	int32_t spanLeftX;
	int32_t spanRightX;
	uint32_t scanlineIndex;
	uintptr_t texturePayload;
	uint32_t leftAccumulatedBaseDepthPerRow;
	uint32_t leftAccumulatedNextPointDepthPerRow;
	uint32_t leftBaseDepth;
	uint32_t leftBaseTexU;
	uint32_t leftBaseTexV;
	uint32_t rightAccumulatedBaseDepthPerRow;
	uint32_t rightAccumulatedNextPointDepthPerRow;
	uint32_t rightBaseDepth;
	uint32_t rightBaseTexU;
	uint32_t rightBaseTexV;
	uint32_t leftPointDepth;
	uint32_t leftPointTexU;
	uint32_t leftPointTexV;
	uint32_t rightPointDepth;
	uint32_t rightPointTexU;
	uint32_t rightPointTexV;
	uintptr_t currentRowPointer;
	uintptr_t nextRowPointer;
} RasterTexturedSpanSetupState;

typedef struct RasterTexturedSpanSetup {
	RasterTexturedSpanSetupState in;
	uint32_t leftDenominator;
	int32_t leftQuotient;
	bool leftTexUCarry;
	bool leftTexVCarry;
	bool leftDepthCarry;
	uint32_t spanLeftTexU;
	uint32_t spanLeftTexV;
	uint32_t spanLeftDepth;
	bool rightDenominatorBorrow;
	uint32_t rightDenominator;
	int32_t rightQuotient;
	bool rightTexUCarry;
	bool rightTexVCarry;
	bool rightDepthCarry;
	uint32_t spanRightTexU;
	uint32_t spanRightTexV;
	uint32_t spanRightDepth;
	bool calledDispatchTexturedSpan;
	uintptr_t spanCoreTexturePayload;
	uintptr_t spanCoreEndpointBase;
	int32_t spanCoreLeftX;
	int32_t spanCoreRightX;
	int32_t postCallLeftX;
	int32_t postCallRightX;
	uintptr_t spanStartCurrentRow;
	uint32_t spanPixelCount;
	uintptr_t spanStartNextRow;
} RasterTexturedSpanSetup;

typedef struct RasterTexturedSpanCoreState {
	uint8_t *screenRow;
	size_t screenRowBytes;
	int32_t spanLeftX;
	int32_t spanRightX;
	uint32_t startTexU;
	uint32_t startTexV;
	uint32_t endTexU;
	uint32_t endTexV;
	uint16_t transparentWord;
	const uint8_t *const *textureRows;
	size_t textureRowCount;
	size_t textureRowBytes;
} RasterTexturedSpanCoreState;

typedef struct RasterTexturedSpanCore {
	RasterTexturedSpanCoreState in;
	uintptr_t destination;
	int32_t spanDelta;
	bool zeroDelta;
	bool positiveDeltaReturn;
	uint32_t spanSteps;
	int32_t texUStep;
	int32_t texVStep;
	uint32_t currentTexU;
	uint32_t currentTexV;
	uint32_t pixelCount;
	bool maskedBranch;
	bool oddBytePrologue;
	bool wordAlignPrologue;
	uint32_t dwordLoopCount;
	bool wordTail;
	bool byteTail;
	size_t pixelsTested;
	size_t pixelsWritten;
} RasterTexturedSpanCore;

typedef struct RasterAffineHorizontalSpanState {
	uint8_t *screenRow;
	size_t screenRowBytes;
	const uint8_t *lockedPayload;
	size_t lockedPayloadBytes;
	const uint8_t *pointBuffer;
	size_t pointBufferBytes;
	uint32_t topLeftPointOffset;
	uint32_t topRightPointOffset;
	const uint8_t *const *textureRows;
	size_t textureRowCount;
	size_t textureRowBytes;
} RasterAffineHorizontalSpanState;

typedef struct RasterAffineHorizontalSpan {
	RasterAffineHorizontalSpanState in;
	uint32_t spanLeftTexU;
	uint32_t spanLeftTexV;
	uint32_t spanRightTexU;
	uint32_t spanRightTexV;
	uint32_t endpointBase;
	int32_t spanCoreLeftX;
	int32_t spanCoreRightX;
	uintptr_t spanCoreTexturePayload;
	uint16_t transparentWord;
	bool calledDrawTexturedSpanCore;
	RasterTexturedSpanCore spanCore;
	bool jump;
} RasterAffineHorizontalSpan;

typedef struct RasterAffineScanlineLoopState {
	uint8_t *const *screenRows;
	size_t screenRowCount;
	size_t screenRowBytes;
	const uint8_t *lockedPayload;
	size_t lockedPayloadBytes;
	const uint8_t *pointBuffer;
	uint32_t pointBufferBase;
	uint32_t pointBufferEnd;
	size_t pointBufferBytes;
	const uint8_t *const *textureRows;
	size_t textureRowCount;
	size_t textureRowBytes;
	uint32_t leftPointOffset;
	uint32_t rightPointOffset;
	int32_t scanline;
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
} RasterAffineScanlineLoopState;

typedef struct RasterAffineScanlineLoopVisit {
	int32_t scanline;
	int32_t spanCoreLeftX;
	int32_t spanCoreRightX;
	uint32_t spanLeftTexUBefore;
	uint32_t spanLeftTexVBefore;
	uint32_t spanRightTexUBefore;
	uint32_t spanRightTexVBefore;
	bool savedSpanLeftX;
	bool savedSpanRightX;
	bool savedLeftPointCursor;
	bool savedRightPointCursor;
	bool savedScanline;
	bool calledDrawTexturedSpanCore;
	RasterTexturedSpanCore spanCore;
	uint32_t spanLeftTexUAfter;
	uint32_t spanLeftTexVAfter;
	uint32_t spanRightTexUAfter;
	uint32_t spanRightTexVAfter;
	bool leftFractionCarry;
	bool rightFractionCarry;
	uint16_t leftRemainingAfterDec;
	bool calledStepLeftEdgeAffine;
	bool carryFromLeftAffineEdge;
	uint16_t rightRemainingAfterDec;
	bool calledStepRightEdgeAffine;
	bool carryFromRightAffineEdge;
	bool loop;
} RasterAffineScanlineLoopVisit;

typedef struct RasterAffineScanlineLoop {
	RasterAffineScanlineLoopState in;
	RasterAffineScanlineLoopState out;
	size_t visitCount;
	bool jumpFromLeftCarry;
	bool jumpFromRightCarry;
	bool calledFinalTexturedSpanCore;
	RasterTexturedSpanCore finalSpanCore;
	bool popad;
} RasterAffineScanlineLoop;

int Raster_DrawAffineTexturedPolygon(const uint8_t *payload, size_t payloadBytes, RasterTexturedPoint *points,
                                     uint32_t pointCount, uint32_t rowScroll, RasterAffineScanlineLoopVisit *visits,
                                     size_t visitCapacity, size_t *pixelsWritten);

typedef struct RasterTexturedSpanDispatchState {
	uint8_t *screenRow;
	size_t screenRowBytes;
	uint32_t scanlineIndex;
	int32_t spanLeftX;
	int32_t spanRightX;
	uintptr_t endpointBase;
	uint32_t startTexU;
	uint32_t startTexV;
	uint32_t endTexU;
	uint32_t endTexV;
	uint32_t startDepth;
	uint32_t endDepth;
	uint16_t transparentWord;
	const uint8_t *const *textureRows;
	size_t textureRowCount;
	size_t textureRowBytes;
} RasterTexturedSpanDispatchState;

typedef struct RasterTexturedSpanDispatch {
	RasterTexturedSpanDispatchState in;
	int32_t spanNegatedDelta;
	bool negativeWidthReturn;
	uint32_t inclusiveWidth;
	bool jumpShortSpan;
	bool longSpanStateSaved;
	uint32_t savedWidth;
	uintptr_t savedDestination;
	uint32_t savedScanline;
	int32_t savedLeftX;
	int32_t savedRightX;
	uintptr_t savedEndpointBase;
	uint32_t depthA;
	uint32_t depthB;
	bool equalDepthReturn;
	bool depthSwapped;
	uint32_t reciprocalBucket;
	bool reciprocalTooLargeReturn;
	bool reciprocalClamped;
	bool longSpanBranch;
	bool lowDepthRatioBranch;
	bool fourSegmentBranch;
	bool twoSegmentBranch;
	bool calledSetupLongTexturedSpan;
	bool maskedLongSpan;
	uint16_t longSpanSegment0PixelCount;
	uint16_t longSpanSegment1PixelCount;
	uint16_t longSpanSegment2PixelCount;
	uint16_t longSpanSegment3PixelCount;
	uint32_t longSpanStartTexU;
	uint32_t longSpanStartTexV;
	int32_t longSpanStep0TexU;
	int32_t longSpanStep0TexV;
	int32_t longSpanStep1TexU;
	int32_t longSpanStep1TexV;
	size_t longSpanPixelsWritten;
	uint32_t longSpanFailStage;
	uint32_t longSpanFailRun;
	uint32_t longSpanFailPixel;
	uint32_t longSpanFailTexU;
	uint32_t longSpanFailTexV;
	uint16_t longSpanFailTexX;
	uint16_t longSpanFailTexY;
	RasterTexturedSpanCore shortSpanCore;
} RasterTexturedSpanDispatch;

typedef struct RasterTexturedLongSpanSetupState {
	uint32_t reciprocalBucket;
	uint32_t spanWidth;
	bool depthSwapped;
	uint32_t startTexU;
	uint32_t startTexV;
	uint32_t endTexU;
	uint32_t endTexV;
	uint32_t perspectiveTableBase;
	uint32_t split3PackedFractions;
	uint32_t split7PackedFractions;
	uint32_t split11PackedFractions;
} RasterTexturedLongSpanSetupState;

typedef struct RasterTexturedLongSpanSetup {
	RasterTexturedLongSpanSetupState in;
	uint32_t tableOffset;
	uint16_t segmentCountCoeff0;
	uint16_t segmentCountCoeff1;
	uint16_t segmentCountCoeff2;
	uint16_t segmentInterpCoeff0;
	uint16_t segmentInterpCoeff1;
	uint16_t segmentInterpCoeff2;
	uint32_t startTexU;
	uint32_t startTexV;
	uint32_t endTexU;
	uint32_t endTexV;
	uint16_t segment0PixelCount;
	uint16_t segment1PixelCount;
	uint16_t segment2PixelCount;
	uint16_t segment3PixelCount;
	int32_t texUDelta;
	int32_t texVDelta;
	bool segment0Nonzero;
	bool segment1Nonzero;
	bool segment2Nonzero;
	bool segment3Nonzero;
	int32_t segment0TexUEnd;
	int32_t segment0TexVEnd;
	int32_t segment1TexUEnd;
	int32_t segment1TexVEnd;
	int32_t remainingTexUAfterSegment;
	int32_t remainingTexVAfterSegment;
	int32_t segment0TexUStep;
	int32_t segment0TexVStep;
	int32_t segment1TexUStep;
	int32_t segment1TexVStep;
	int32_t segment2TexUStep;
	int32_t segment2TexVStep;
	int32_t segment3TexUStep;
	int32_t segment3TexVStep;
} RasterTexturedLongSpanSetup;

typedef struct RasterTexturedLongSpanSegment0State {
	uint8_t *destination;
	size_t destinationBytes;
	uint16_t transparentWord;
	uint16_t segment0PixelCount;
	uint32_t currentTexU;
	uint32_t currentTexV;
	int32_t texUStep;
	int32_t texVStep;
	const uint8_t *const *textureRows;
	size_t textureRowCount;
	size_t textureRowBytes;
} RasterTexturedLongSpanSegment0State;

typedef struct RasterTexturedLongSpanSegment0 {
	RasterTexturedLongSpanSegment0State in;
	int32_t loadedTexUStep;
	uint16_t transparentWord;
	bool maskedJump;
	bool zeroCountJump;
	uint16_t segmentPixels;
	int32_t loadedTexVStep;
	uintptr_t pushedDestination;
	bool oddBytePrologue;
	bool wordAlignPrologue;
	uint32_t dwordLoopCount;
	bool wordTail;
	bool byteTail;
	uintptr_t destinationAfterAdd;
	uint32_t currentTexU;
	uint32_t currentTexV;
	size_t pixelsWritten;
} RasterTexturedLongSpanSegment0;

typedef struct RasterTexturedLongSpanSegment1State {
	uint8_t *destination;
	size_t destinationBytes;
	uint16_t segment1PixelCount;
	uint32_t currentTexU;
	uint32_t currentTexV;
	int32_t texUStep;
	int32_t texVStep;
	const uint8_t *const *textureRows;
	size_t textureRowCount;
	size_t textureRowBytes;
} RasterTexturedLongSpanSegment1State;

typedef struct RasterTexturedLongSpanSegment1 {
	RasterTexturedLongSpanSegment1State in;
	uint16_t segmentPixels;
	bool zeroCountJump;
	int32_t loadedTexUStep;
	int32_t loadedTexVStep;
	uintptr_t pushedDestination;
	bool oddBytePrologue;
	bool wordAlignPrologue;
	uint32_t dwordLoopCount;
	bool wordTail;
	bool byteTail;
	uintptr_t destinationAfterAdd;
	uint32_t currentTexU;
	uint32_t currentTexV;
	size_t pixelsWritten;
} RasterTexturedLongSpanSegment1;

typedef struct RasterTexturedLongSpanSegment2State {
	uint8_t *destination;
	size_t destinationBytes;
	uint16_t segment2PixelCount;
	uint32_t currentTexU;
	uint32_t currentTexV;
	int32_t texUStep;
	int32_t texVStep;
	const uint8_t *const *textureRows;
	size_t textureRowCount;
	size_t textureRowBytes;
} RasterTexturedLongSpanSegment2State;

typedef struct RasterTexturedLongSpanSegment2 {
	RasterTexturedLongSpanSegment2State in;
	uint16_t segmentPixels;
	bool zeroCountJump;
	int32_t loadedTexUStep;
	int32_t loadedTexVStep;
	uintptr_t pushedDestination;
	bool oddBytePrologue;
	bool wordAlignPrologue;
	uint32_t dwordLoopCount;
	bool wordTail;
	bool byteTail;
	uintptr_t destinationAfterAdd;
	uint32_t currentTexU;
	uint32_t currentTexV;
	size_t pixelsWritten;
} RasterTexturedLongSpanSegment2;

typedef struct RasterTexturedLongSpanSegment3State {
	uint8_t *destination;
	size_t destinationBytes;
	uint16_t segment3PixelCount;
	uint32_t currentTexU;
	uint32_t currentTexV;
	int32_t texUStep;
	int32_t texVStep;
	const uint8_t *const *textureRows;
	size_t textureRowCount;
	size_t textureRowBytes;
} RasterTexturedLongSpanSegment3State;

typedef struct RasterTexturedLongSpanSegment3 {
	RasterTexturedLongSpanSegment3State in;
	uint16_t segmentPixels;
	bool zeroCountReturn;
	int32_t loadedTexUStep;
	int32_t loadedTexVStep;
	uintptr_t pushedDestination;
	bool oddBytePrologue;
	bool wordAlignPrologue;
	uint32_t dwordLoopCount;
	bool wordTail;
	bool byteTail;
	uintptr_t destinationAfterAdd;
	uint32_t currentTexU;
	uint32_t currentTexV;
	bool ret;
	size_t pixelsWritten;
} RasterTexturedLongSpanSegment3;

typedef struct RasterTexturedMaskedLongSpanSegment0State {
	uint8_t *destination;
	size_t destinationBytes;
	uint8_t transparentByteDl;
	uint16_t segment0PixelCount;
	uint32_t currentTexU;
	uint32_t currentTexV;
	int32_t texUStep;
	int32_t texVStep;
	const uint8_t *const *textureRows;
	size_t textureRowCount;
	size_t textureRowBytes;
} RasterTexturedMaskedLongSpanSegment0State;

typedef struct RasterTexturedMaskedLongSpanSegment0 {
	RasterTexturedMaskedLongSpanSegment0State in;
	bool clearTextureOffsetRegister;
	uint16_t segmentPixels;
	bool zeroCountJump;
	int32_t loadedTexUStep;
	int32_t loadedTexVStep;
	uint32_t currentTexU;
	uint32_t currentTexV;
	uintptr_t destinationAfterLoop;
	size_t pixelsTested;
	size_t pixelsSkipped;
	size_t pixelsWritten;
} RasterTexturedMaskedLongSpanSegment0;

typedef struct RasterTexturedMaskedLongSpanSegment1State {
	uint8_t *destination;
	size_t destinationBytes;
	uint8_t transparentByteDl;
	uint16_t segment1PixelCount;
	uint32_t currentTexU;
	uint32_t currentTexV;
	int32_t texUStep;
	int32_t texVStep;
	const uint8_t *const *textureRows;
	size_t textureRowCount;
	size_t textureRowBytes;
} RasterTexturedMaskedLongSpanSegment1State;

typedef struct RasterTexturedMaskedLongSpanSegment1 {
	RasterTexturedMaskedLongSpanSegment1State in;
	uint16_t segmentPixels;
	bool zeroCountJump;
	int32_t loadedTexUStep;
	int32_t loadedTexVStep;
	uint32_t currentTexU;
	uint32_t currentTexV;
	uintptr_t destinationAfterLoop;
	size_t pixelsTested;
	size_t pixelsSkipped;
	size_t pixelsWritten;
} RasterTexturedMaskedLongSpanSegment1;

typedef struct RasterTexturedMaskedLongSpanSegment2State {
	uint8_t *destination;
	size_t destinationBytes;
	uint8_t transparentByteDl;
	uint16_t segment2PixelCount;
	uint32_t currentTexU;
	uint32_t currentTexV;
	int32_t texUStep;
	int32_t texVStep;
	const uint8_t *const *textureRows;
	size_t textureRowCount;
	size_t textureRowBytes;
} RasterTexturedMaskedLongSpanSegment2State;

typedef struct RasterTexturedMaskedLongSpanSegment2 {
	RasterTexturedMaskedLongSpanSegment2State in;
	uint16_t segmentPixels;
	bool zeroCountJump;
	int32_t loadedTexUStep;
	int32_t loadedTexVStep;
	uint32_t currentTexU;
	uint32_t currentTexV;
	uintptr_t destinationAfterLoop;
	size_t pixelsTested;
	size_t pixelsSkipped;
	size_t pixelsWritten;
} RasterTexturedMaskedLongSpanSegment2;

typedef struct RasterTexturedMaskedLongSpanSegment3State {
	uint8_t *destination;
	size_t destinationBytes;
	uint8_t transparentByteDl;
	uint16_t segment3PixelCount;
	uint32_t currentTexU;
	uint32_t currentTexV;
	int32_t texUStep;
	int32_t texVStep;
	const uint8_t *const *textureRows;
	size_t textureRowCount;
	size_t textureRowBytes;
} RasterTexturedMaskedLongSpanSegment3State;

typedef struct RasterTexturedMaskedLongSpanSegment3 {
	RasterTexturedMaskedLongSpanSegment3State in;
	uint16_t segmentPixels;
	bool zeroCountReturn;
	int32_t loadedTexUStep;
	int32_t loadedTexVStep;
	uint32_t currentTexU;
	uint32_t currentTexV;
	uintptr_t destinationAfterLoop;
	bool ret;
	size_t pixelsTested;
	size_t pixelsSkipped;
	size_t pixelsWritten;
} RasterTexturedMaskedLongSpanSegment3;

typedef struct RasterMaskedPerspectiveTexturedPolygonState {
	uint8_t *const *screenRows;
	size_t screenRowCount;
	size_t screenRowBytes;
	const uint8_t *lockedPayload;
	size_t lockedPayloadBytes;
	uint8_t *pointBuffer;
	uint32_t pointBufferBase;
	size_t pointBufferBytes;
	uint32_t pointCount;
	const uint8_t *const *textureRows;
	size_t textureRowCount;
	size_t textureRowBytes;
} RasterMaskedPerspectiveTexturedPolygonState;

typedef struct RasterMaskedPerspectiveTexturedPolygonVisit {
	int32_t scanline;
	int32_t spanLeftX;
	int32_t spanRightX;
	uint32_t leftPointOffset;
	uint32_t rightPointOffset;
	bool calledSetupPerspectiveTexturedSpan;
	RasterTexturedSpanSetup spanSetup;
	bool calledDispatchTexturedSpan;
	RasterTexturedSpanDispatch spanDispatch;
	bool longSpanBranch;
	size_t pixelsWritten;
	bool calledAdvancePerspectiveTexturedEdges;
	RasterTexturedAdvanceEdges advance;
	bool calledStepLeftEdgeTexturedPerspective;
	RasterTexturedLeftEdgeStep leftEdge;
	bool calledStepRightEdgeTexturedPerspective;
	RasterTexturedRightEdgeStep rightEdge;
} RasterMaskedPerspectiveTexturedPolygonVisit;

typedef struct RasterMaskedPerspectiveTexturedPolygon {
	RasterMaskedPerspectiveTexturedPolygonState in;
	bool pushad;
	bool calledPrepareTextureUVExtents;
	bool calledBuildTextureRowTable;
	uint32_t pointBufferBase;
	uint32_t pointBufferEnd;
	uint32_t inputPointCount;
	int32_t topY;
	int32_t bottomY;
	uint32_t topLeftPointOffset;
	uint32_t topRightPointOffset;
	size_t scanVisitCount;
	bool horizontalBranch;
	uint32_t storedBottomY;
	bool initialCalledStepLeftEdgeTexturedPerspective;
	RasterTexturedLeftEdgeStep initialLeftEdge;
	bool initialCalledStepRightEdgeTexturedPerspective;
	RasterTexturedRightEdgeStep initialRightEdge;
	size_t visitCount;
	size_t spanDispatchCount;
	size_t shortSpanCount;
	size_t longSpanCount;
	size_t pixelsWritten;
	bool failVisitIndexValid;
	size_t failVisitIndex;
	bool popad;
} RasterMaskedPerspectiveTexturedPolygon;

typedef struct RasterOpaquePerspectiveTexturedPolygonState {
	uint8_t *const *screenRows;
	size_t screenRowCount;
	size_t screenRowBytes;
	const uint8_t *lockedPayload;
	size_t lockedPayloadBytes;
	uint8_t *pointBuffer;
	uint32_t pointBufferBase;
	size_t pointBufferBytes;
	uint32_t pointCount;
	const uint8_t *const *textureRows;
	size_t textureRowCount;
	size_t textureRowBytes;
	int32_t topY;
	int32_t bottomY;
	uint32_t topLeftPointOffset;
	uint32_t topRightPointOffset;
	uint32_t pointBufferEnd;
} RasterOpaquePerspectiveTexturedPolygonState;

typedef struct RasterOpaquePerspectiveTexturedCopy {
	uintptr_t source;
	uintptr_t destination;
	int32_t byteCount;
	bool byteCountPositive;
	uint32_t alignByteCount;
	uint32_t dwordCount;
	uint32_t tailByteCount;
} RasterOpaquePerspectiveTexturedCopy;

typedef struct RasterOpaqueAffineTexturedPolygonState {
	uint8_t *const *screenRows;
	size_t screenRowCount;
	size_t screenRowBytes;
	const uint8_t *lockedPayload;
	size_t lockedPayloadBytes;
	uint8_t *pointBuffer;
	uint32_t pointBufferBase;
	size_t pointBufferBytes;
	uint32_t pointCount;
	const uint8_t *const *textureRows;
	size_t textureRowCount;
	size_t textureRowBytes;
	uint32_t rowScroll;
} RasterOpaqueAffineTexturedPolygonState;

typedef struct RasterOpaqueAffineTexturedVisit {
	int32_t scanline;
	bool calledDrawAffineSpan;
	RasterTexturedSpanCore spanCore;
	bool copyCommon;
	RasterOpaquePerspectiveTexturedCopy commonCopy;
	bool copyLeftExtension;
	RasterOpaquePerspectiveTexturedCopy leftCopy;
	bool copyRightExtension;
	RasterOpaquePerspectiveTexturedCopy rightCopy;
	bool calledAdvanceOpaqueAffineEdges;
	bool carryFrom;
	uint32_t spanOverlapFlags;
} RasterOpaqueAffineTexturedVisit;

typedef struct RasterOpaqueAffineTexturedPolygon {
	RasterOpaqueAffineTexturedPolygonState in;
	bool pushad;
	bool calledPrepareTextureUVExtents;
	bool calledBuildTextureRowTable;
	RasterAffineTexturedEntrySetup entryScan;
	bool horizontalBranch;
	bool initialCalledStepLeftEdgeAffine;
	RasterAffineLeftEdgeStep initialLeftEdge;
	bool initialCalledStepRightEdgeAffine;
	RasterAffineRightEdgeStep initialRightEdge;
	size_t visitCount;
	size_t spanCount;
	size_t rowCopyCount;
	size_t rowCopyBytes;
	size_t pixelsWritten;
	bool failVisitIndexValid;
	size_t failVisitIndex;
	bool popad;
} RasterOpaqueAffineTexturedPolygon;

typedef struct RasterOpaquePerspectiveTexturedVisit {
	int32_t scanline;
	bool evenCall;
	bool oneRowFlag;
	bool predictedCarry;
	int32_t predictedLeftX;
	int32_t predictedRightX;
	uint32_t spanOverlapFlags;
	RasterTexturedSpanSetup spanSetup;
	RasterTexturedSpanDispatch spanDispatch;
	size_t spanPixelsWritten;
	bool copyCommon;
	RasterOpaquePerspectiveTexturedCopy commonCopy;
	bool copyLeftExtension;
	RasterOpaquePerspectiveTexturedCopy leftCopy;
	bool copyRightExtension;
	RasterOpaquePerspectiveTexturedCopy rightCopy;
	bool calledAdvancePerspectiveTexturedEdges;
	RasterTexturedAdvanceEdges advance;
	bool terminalDispatch;
	RasterTexturedSpanDispatch terminalSpanDispatch;
	bool calledStepLeftEdgeTexturedPerspective;
	RasterTexturedLeftEdgeStep leftEdge;
	bool calledStepRightEdgeTexturedPerspective;
	RasterTexturedRightEdgeStep rightEdge;
} RasterOpaquePerspectiveTexturedVisit;

typedef struct RasterOpaquePerspectiveTexturedPolygon {
	RasterOpaquePerspectiveTexturedPolygonState in;
	bool initialCalledStepLeftEdgeTexturedPerspective;
	RasterTexturedLeftEdgeStep initialLeftEdge;
	bool initialCalledStepRightEdgeTexturedPerspective;
	RasterTexturedRightEdgeStep initialRightEdge;
	bool initialCalledSetupPerspectiveTexturedSpan;
	RasterTexturedSpanSetup initialSpanSetup;
	RasterTexturedSpanDispatch initialSpanDispatch;
	size_t visitCount;
	size_t spanDispatchCount;
	size_t rowCopyCount;
	size_t rowCopyBytes;
	size_t pixelsWritten;
	bool failVisitIndexValid;
	size_t failVisitIndex;
	bool popad;
} RasterOpaquePerspectiveTexturedPolygon;

void Raster_SetScreenBufferRows(uint8_t *screenBufferBase, int32_t pitch);

void Raster_SetClipRect(int16_t minX, int16_t minY, int16_t maxX, int16_t maxY);

void Raster_GetClipRect(int16_t *minX, int16_t *minY, int16_t *maxX, int16_t *maxY);

bool Raster_ClipLineToViewport(int16_t *x0, int16_t *y0, int16_t *x1, int16_t *y1);

void Raster_PutPixelClipped(uint8_t color, int16_t y, int16_t x);

void Raster_DrawLineSolid(uint8_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1);

void Raster_DrawLineClipped(uint8_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1);

void Raster_FillSolidSpan(uint8_t color, int16_t y, int16_t x0, int16_t x1);

void Raster_FillRectClipped(uint8_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1);

void Raster_FillRectUnchecked(uint16_t color, int16_t x0, int16_t y0, int16_t x1, int16_t y1);

void Raster_DrawSpriteScaled(const uint8_t *record5, size_t recordBytes, const uint8_t *pixels1, size_t pixelBytes,
                             int16_t leftAx, int16_t topBx, int16_t rightCx, int16_t bottomDx);

void Raster_DrawSolidFlatPolygon(uint8_t color, const RasterPoint *points, uint16_t pointCount);

void Raster_DrawShadedFlatPolygon(const RasterShadedPoint *points, uint16_t pointCount);

void Raster_DrawDitheredFlatPolygon(uint8_t color, uint8_t ditherBits, const RasterPoint *points, uint16_t pointCount);

int Raster_BuildTextureRowTable(const uint8_t *texturePayload, size_t texturePayloadBytes, uint32_t rowScroll,
                                const uint8_t **textureRows, size_t rowCapacity, RasterTextureRowTable *result);

int Raster_PrepareTextureUVExtents(const uint8_t *texturePayload, size_t texturePayloadBytes, uint8_t *pointBuffer,
                                   size_t pointBufferBytes, uint32_t pointCount, RasterTextureUVExtentsVisit *visits,
                                   size_t visitCapacity, RasterTextureUVExtents *result);

int Raster_PrepareTexturedEntry(uint32_t textureHandle, const uint8_t *lockedPayload, size_t lockedPayloadBytes,
                                uint8_t *pointBuffer, uint32_t pointBufferBase, size_t pointBufferBytes,
                                uint32_t pointCount, RasterTexturedEntryScanVisit *visits, size_t visitCapacity,
                                RasterTexturedEntrySetup *result);

int Raster_PrepareAffineTexturedEntry(uint32_t textureHandle, const uint8_t *lockedPayload, size_t lockedPayloadBytes,
                                      uint8_t *pointBuffer, uint32_t pointBufferBase, size_t pointBufferBytes,
                                      uint32_t pointCount, RasterAffineTexturedEntryScanVisit *visits,
                                      size_t visitCapacity, RasterAffineTexturedEntrySetup *result);

int Raster_DrawAffineHorizontalSpan(const RasterAffineHorizontalSpanState *state, RasterAffineHorizontalSpan *result);

int Raster_StepLeftEdgeAffine(const uint8_t *pointBuffer, uint32_t pointBufferBase, uint32_t pointBufferEnd,
                              size_t pointBufferBytes, uint32_t initialPointOffset, int32_t currentY, int32_t bottomY,
                              RasterAffineLeftEdgeStep *result);

int Raster_StepRightEdgeAffine(const uint8_t *pointBuffer, uint32_t pointBufferBase, uint32_t pointBufferEnd,
                               size_t pointBufferBytes, uint32_t initialPointOffset, int32_t currentY, int32_t bottomY,
                               RasterAffineRightEdgeStep *result);

int Raster_DrawAffineScanlineLoop(const RasterAffineScanlineLoopState *state, RasterAffineScanlineLoopVisit *visits,
                                  size_t visitCapacity, RasterAffineScanlineLoop *result);

int Raster_DrawOpaqueAffineTexturedPolygon(const RasterOpaqueAffineTexturedPolygonState *state,
                                           RasterOpaqueAffineTexturedVisit *visits, size_t visitCapacity,
                                           RasterOpaqueAffineTexturedPolygon *result);

int Raster_StepLeftEdgeTexturedPerspective(const uint8_t *pointBuffer, uint32_t pointBufferBase,
                                           uint32_t pointBufferEnd, size_t pointBufferBytes,
                                           uint32_t initialPointOffset, int32_t currentY, int32_t bottomY,
                                           RasterTexturedLeftEdgeStep *result);

int Raster_StepRightEdgeTexturedPerspective(const uint8_t *pointBuffer, uint32_t pointBufferBase,
                                            uint32_t pointBufferEnd, size_t pointBufferBytes,
                                            uint32_t initialPointOffset, int32_t currentY, int32_t bottomY,
                                            RasterTexturedRightEdgeStep *result);

int Raster_AdvancePerspectiveTexturedEdges(const RasterTexturedAdvanceEdgesState *state, int leftEdgeCarry,
                                           int rightEdgeCarry, RasterTexturedAdvanceEdges *result);

int Raster_SetupPerspectiveTexturedSpan(const RasterTexturedSpanSetupState *state, RasterTexturedSpanSetup *result);

int Raster_DrawTexturedSpanCore(const RasterTexturedSpanCoreState *state, RasterTexturedSpanCore *result);

int Raster_DispatchTexturedSpan(const RasterTexturedSpanDispatchState *state, RasterTexturedSpanDispatch *result);

int Raster_DrawMaskedPerspectiveTexturedPolygon(const RasterMaskedPerspectiveTexturedPolygonState *state,
                                                RasterMaskedPerspectiveTexturedPolygonVisit *visits,
                                                size_t visitCapacity, RasterMaskedPerspectiveTexturedPolygon *result);

int Raster_DrawOpaquePerspectiveTexturedPolygon(const RasterOpaquePerspectiveTexturedPolygonState *state,
                                                RasterOpaquePerspectiveTexturedVisit *visits, size_t visitCapacity,
                                                RasterOpaquePerspectiveTexturedPolygon *result);

int Raster_SetupLongTexturedSpan(const RasterTexturedLongSpanSetupState *state, RasterTexturedLongSpanSetup *result);

int Raster_DrawOpaqueLongTexturedSpanSegment0(const RasterTexturedLongSpanSegment0State *state,
                                              RasterTexturedLongSpanSegment0 *result);

int Raster_DrawOpaqueLongTexturedSpanSegment1(const RasterTexturedLongSpanSegment1State *state,
                                              RasterTexturedLongSpanSegment1 *result);

int Raster_DrawOpaqueLongTexturedSpanSegment2(const RasterTexturedLongSpanSegment2State *state,
                                              RasterTexturedLongSpanSegment2 *result);

int Raster_DrawOpaqueLongTexturedSpanSegment3(const RasterTexturedLongSpanSegment3State *state,
                                              RasterTexturedLongSpanSegment3 *result);

int Raster_DrawMaskedLongTexturedSpanSegment0(const RasterTexturedMaskedLongSpanSegment0State *state,
                                              RasterTexturedMaskedLongSpanSegment0 *result);

int Raster_DrawMaskedLongTexturedSpanSegment1(const RasterTexturedMaskedLongSpanSegment1State *state,
                                              RasterTexturedMaskedLongSpanSegment1 *result);

int Raster_DrawMaskedLongTexturedSpanSegment2(const RasterTexturedMaskedLongSpanSegment2State *state,
                                              RasterTexturedMaskedLongSpanSegment2 *result);

int Raster_DrawMaskedLongTexturedSpanSegment3(const RasterTexturedMaskedLongSpanSegment3State *state,
                                              RasterTexturedMaskedLongSpanSegment3 *result);

void Raster_Clear(uint8_t color, size_t byteCount);

void Raster_DrawTexturedPolygon(uint16_t texture, RasterTexturedPoint *points, uint32_t count, bool perspective,
                                bool opaque);

#endif
