#ifndef SLIPSTREAM_TRACK_WORLD_H
#define SLIPSTREAM_TRACK_WORLD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "animated_effects.h"
#include "cross_effects.h"
#include "draw3d.h"
#include "race_effects.h"
#include "resource.h"
#include "shape3d.h"
#include "timed_effects.h"
#include "track_format.h"
#include "view3d.h"

typedef struct SlipTrackBeamRequest {
	SlipView3DVec32 start, end;
	uint32_t material;
} SlipTrackBeamRequest;

enum {
	SLIP_TRACK_BEAM_BLASTER = 0,
	SLIP_TRACK_BEAM_REFUEL = UINT16_MAX,
	SLIP_TRACK_BEAM_RECORD_CAPACITY = 0x180,
	SLIP_TRACK_BEAM_QUEUE_CAPACITY = 48,
	SLIP_TRACK_SLOT_RECORD_BYTES = 280,
	SLIP_TRACK_SLOT_SPARE_COUNT = 8,
	SLIP_TRACK_SLOT_SENTINEL_COUNT = 2,
	SLIP_TRACK_BOUNDING_CORNER_COUNT = 8,
	/* Object-list entry increments the nesting counter before drawing. */
	SLIP_TRACK_OUTER_OBJECT_DEPTH = 1,
	SLIP_TRACK_REPLAY_OBJECT_CAPACITY = 10,
	SLIP_TRACK_BOUND_COORDINATE_COUNT = 6,
	SLIP_TRACK_CORNER_COORDINATE_COUNT = 3,
	SLIP_TRACK_SLOT_BOX_COLLISION = 1,
	SLIP_TRACK_SLOT_POINT_COLLISION = 2,
	SLIP_TRACK_SLOT_ARTICULATED_BOUNDS = 4,
	SLIP_TRACK_SLOT_DRONE = 64
};

typedef struct SlipTrackBeamRecord {
	uint32_t section;
	SlipView3DVec32 midpoint, start, end;
	uint16_t type;
	uint32_t material;
	uint32_t continuation;
} SlipTrackBeamRecord;

typedef char SlipTrackBeamRecordDosStride[(sizeof(SlipTrackBeamRecord) == 0x34) ? 1 : -1];

typedef struct SlipTrackBeamState {
	uint32_t built;
	uint32_t recordCount;
	uint32_t queueCount;
	SlipTrackBeamRequest queue[SLIP_TRACK_BEAM_QUEUE_CAPACITY];
	SlipTrackBeamRecord records[SLIP_TRACK_BEAM_RECORD_CAPACITY];
	SlipTrackBeamRecord *resourceRecords;
} SlipTrackBeamState;

extern SlipTrackBeamState SlipTrackWorld_beams;

enum {
	SLIP_TRACK_WORLD_CELL_TABLE_BYTES = 0x0f00,
	SLIP_TRACK_WORLD_OBJECT_LIST_BYTES = 0x8404,
	SLIP_TRACK_WORLD_DEFERRED_LIST_BYTES = 0x0c04,
	SLIP_TRACK_WORLD_DEFERRED_SCAN_BYTES = 0x0c00,
	SLIP_TRACK_WORLD_AXIS_RAMP_BYTES = 0x01d4,
	SLIP_TRACK_WORLD_AXIS_TEST_BYTES = 0x0042,
	SLIP_TRACK_WORLD_AXIS_RAMP_X_LAST_INDEX = 12,
	SLIP_TRACK_WORLD_AXIS_RAMP_Y_LAST_INDEX = 4,
	SLIP_TRACK_WORLD_AXIS_RAMP_Z_LAST_INDEX = 20,
	SLIP_TRACK_WORLD_AXIS_RAMP_POINT_BYTES = sizeof(SlipView3DVec32),
	SLIP_TRACK_WORLD_AXIS_RAMP_POINT_COUNT = SLIP_TRACK_WORLD_AXIS_RAMP_X_LAST_INDEX +
	                                         SLIP_TRACK_WORLD_AXIS_RAMP_Y_LAST_INDEX +
	                                         SLIP_TRACK_WORLD_AXIS_RAMP_Z_LAST_INDEX + 3,
	SLIP_TRACK_WORLD_AXIS_RAMP_Y_OFFSET =
	    (SLIP_TRACK_WORLD_AXIS_RAMP_X_LAST_INDEX + 1) * SLIP_TRACK_WORLD_AXIS_RAMP_POINT_BYTES,
	SLIP_TRACK_WORLD_AXIS_RAMP_Z_OFFSET =
	    SLIP_TRACK_WORLD_AXIS_RAMP_Y_OFFSET +
	    (SLIP_TRACK_WORLD_AXIS_RAMP_Y_LAST_INDEX + 1) * SLIP_TRACK_WORLD_AXIS_RAMP_POINT_BYTES,
	SLIP_TRACK_WORLD_AXIS_TEST_X_COUNT = SLIP_TRACK_WORLD_AXIS_RAMP_X_LAST_INDEX - 1,
	SLIP_TRACK_WORLD_AXIS_TEST_Y_COUNT = SLIP_TRACK_WORLD_AXIS_RAMP_Y_LAST_INDEX - 1,
	SLIP_TRACK_WORLD_AXIS_TEST_Z_COUNT = SLIP_TRACK_WORLD_AXIS_RAMP_Z_LAST_INDEX - 1,
	SLIP_TRACK_WORLD_AXIS_TEST_Y_OFFSET = SLIP_TRACK_WORLD_AXIS_TEST_X_COUNT * sizeof(uint16_t),
	SLIP_TRACK_WORLD_AXIS_TEST_Z_OFFSET =
	    SLIP_TRACK_WORLD_AXIS_TEST_Y_OFFSET + SLIP_TRACK_WORLD_AXIS_TEST_Y_COUNT * sizeof(uint16_t),
	SLIP_TRACK_WORLD_AXIS_TEST_COUNT =
	    SLIP_TRACK_WORLD_AXIS_TEST_X_COUNT + SLIP_TRACK_WORLD_AXIS_TEST_Y_COUNT + SLIP_TRACK_WORLD_AXIS_TEST_Z_COUNT
};

extern uint32_t SlipTrackWorld_lastRecord;
extern uint16_t SlipTrackWorld_slotObject;

typedef struct SlipTrackWorldHostBuffers {
	uint8_t *cellTable;
	size_t cellTableBytes;
	uint8_t *objectList;
	size_t objectListBytes;
	uint8_t *deferredList;
	size_t deferredListBytes;
	uint8_t *deferredScan;
	size_t deferredScanBytes;
	bool cellTableAllocationReturned;
	bool listAllocationsReturned;
} SlipTrackWorldHostBuffers;

typedef struct SlipTrackWorldAxisRampWorkspace {
	uint16_t rampPointCount;
	uint16_t pointStrideBytes;
	uint32_t allocationBytes;
	bool callResourceAllocateAnonymous;
	uint16_t allocationHandle;
	bool callLockResource;
	uint8_t *base;
	uint8_t *rampX;
	uint8_t *rampY;
	uint8_t *rampZ;
	bool clearsCarry;
	bool ret;
} SlipTrackWorldAxisRampWorkspace;

typedef struct SlipTrackWorldAxisTestWorkspace {
	uint16_t testWordCount;
	uint16_t testWordBytes;
	uint32_t allocationBytes;
	bool callResourceAllocateAnonymous;
	uint16_t allocationHandle;
	bool callLockResource;
	uint8_t *tableBase;
	uint8_t *testTableX;
	uint8_t *testTableY;
	uint8_t *testTableZ;
	bool clearsCarry;
	bool ret;
} SlipTrackWorldAxisTestWorkspace;

typedef struct SlipTrackWorldNodeTest {
	uint16_t nodeWordOffset;
	uint16_t classificationWord;
	bool carryFromSar;
} SlipTrackWorldNodeTest;

typedef struct SlipTrackWorldTraversalEntry {
	bool callClearRecordCache;
	uint16_t clearedCount;
	uint16_t rootListOffset;
	bool zeroRootBranch;
	uint32_t rootRecordOffset;
	const uint8_t *rootRecord;
	bool callTraverseRecords;
	bool executed;
	uint16_t traversalVisitCount;
	uint16_t traversalMaxDepth;
	bool traversalHitVisitCapacity;
	bool traversalHitStackCapacity;
	bool ret;
} SlipTrackWorldTraversalEntry;

typedef struct SlipTrackWorldChunkSetup {
	bool skippedByFilter;
	uint32_t chunkCounter;
	const uint8_t *currentChunk;
	SlipView3DVec32 currentChunkOrigin;
	const uint8_t *savedChunkPointer;
} SlipTrackWorldChunkSetup;

typedef enum SlipTrackWorldRecordScanBranch {
	SLIP_TRACK_WORLD_RECORD_MASK_CLEAR,
	SLIP_TRACK_WORLD_RECORD_SCAN_CONTINUE,
	SLIP_TRACK_WORLD_RECORD_MASK_REJECT
} SlipTrackWorldRecordScanBranch;

typedef uint32_t (*SlipTrackWorldProjectMask)(SlipView3DVec32 point, void *userData);

typedef bool (*SlipTrackWorldDrawStateLoad)(uint32_t recordIndex, void *userData);

typedef struct SlipTrackWorldDlTest {
	uint8_t combinedMask;
	uintptr_t scanPosition;
	uint32_t remainingRecordCount;
	SlipTrackWorldRecordScanBranch branch;
} SlipTrackWorldDlTest;

enum { SLIP_TRACK_WORLD_RECORD_SCAN_VISITS = 8 };

typedef struct SlipTrackWorldClassifyChild {
	const uint8_t *childRecord;
	bool callTrackWorldSumThreePoints;
	SlipView3DVec32 point;
	bool callTrackWorldClassifyPoint;
	uint32_t projectedMask;
	uint32_t classificationMask;
	uint8_t classificationByte;
} SlipTrackWorldClassifyChild;

typedef struct SlipTrackWorldRecordScanVisit {
	uintptr_t scanPosition;
	uint16_t childRecordOffset;
	uint32_t childRecordPointer;
	uint8_t cacheValidByte;
	bool callTrackWorldSumThreePoints;
	bool callTrackWorldClassifyPoint;
	uint8_t classificationByte;
	SlipTrackWorldClassifyChild classifyChild;
	bool callTrackWorldStoreRecordCacheResult;
	uint8_t cachedClassificationByte;
	SlipTrackWorldDlTest dlTest;
} SlipTrackWorldRecordScanVisit;

typedef struct SlipTrackWorldRecordScan {
	uint32_t recordOffset;
	uint32_t scanOffset;
	uint8_t initialCombinedMask;
	uint32_t initialChildCount;
	uint16_t visitCount;
	SlipTrackWorldRecordScanVisit visits[SLIP_TRACK_WORLD_RECORD_SCAN_VISITS];
	SlipTrackWorldRecordScanBranch branch;
	uint8_t combinedMask;
	uintptr_t scanPosition;
	uint32_t remainingChildCount;
	bool ret;
} SlipTrackWorldRecordScan;

typedef enum SlipTrackWorldRecordMaskTestBranch {
	SLIP_TRACK_WORLD_NODE_POSITIVE,
	SLIP_TRACK_WORLD_NODE_NEGATIVE,
	SLIP_TRACK_WORLD_NODE_SENTINEL
} SlipTrackWorldRecordMaskTestBranch;

typedef struct SlipTrackWorldNodeBranch {
	uint16_t nodeOffset;
	SlipTrackWorldNodeTest nodeTest;
	SlipTrackWorldRecordMaskTestBranch branch;
} SlipTrackWorldNodeBranch;

typedef struct SlipTrackWorldPositiveNodeChildDispatch {
	uint16_t firstOffset;
	bool firstCall;
	uint32_t firstChildPointer;
	uint16_t secondOffset;
	bool secondCall;
	uint32_t secondChildPointer;
} SlipTrackWorldPositiveNodeChildDispatch;

typedef struct SlipTrackWorldNegativeNodeChildDispatch {
	uint16_t firstOffset;
	bool firstCall;
	uint32_t firstChildPointer;
	uint16_t secondOffset;
	bool secondCall;
	uint32_t secondChildPointer;
} SlipTrackWorldNegativeNodeChildDispatch;

typedef struct SlipTrackWorldRecordChunk {
	uint16_t pointRecordOffset;
	const uint8_t *pointRecord;
	SlipView3DVec32 offset;
	uint16_t chunkOffset;
	const uint8_t *chunkPointer;
	const uint8_t *chunkPointerForSetup;
	bool callSetupChunk;
	SlipTrackWorldChunkSetup chunkSetup;
	bool chunkPointListCall;
	bool chunkBspProcessCall;
	bool chunkVertexBuildCall;
	bool chunkBspTraversalCall;
	uint16_t chunkProcessRawBspNodeCount;
	uint16_t chunkProcessRawBspClassifyCount;
	uint16_t chunkProcessRawBspCallbackCount;
	bool chunkProcessRet;
} SlipTrackWorldRecordChunk;

typedef enum SlipTrackWorldNodeDispatchBranch {
	SLIP_TRACK_WORLD_NODE_DISPATCH_BRANCH_POSITIVE_CHILD,
	SLIP_TRACK_WORLD_NODE_DISPATCH_BRANCH_NEGATIVE_CHILD,
	SLIP_TRACK_WORLD_NODE_DISPATCH_BRANCH_CHUNK,
	SLIP_TRACK_WORLD_NODE_DISPATCH_BRANCH_SKIP
} SlipTrackWorldNodeDispatchBranch;

typedef struct SlipTrackWorldNodeDispatch {
	uint32_t recordOffset;
	SlipTrackWorldNodeBranch nodeBranch;
	bool callDispatchPositiveChildren;
	SlipTrackWorldPositiveNodeChildDispatch positiveChildDispatch;
	bool returnedAfterPositiveChildren;
	bool callDispatchNegativeChildren;
	SlipTrackWorldNegativeNodeChildDispatch negativeChildDispatch;
	bool returnedAfterNegativeChildren;
	bool callGetDrawStateBeforeChunk;
	uint32_t drawStateIndexBeforeChunk;
	uint32_t drawStateIndexForChunk;
	bool callLoadDrawStateForChunk;
	bool callProcessRecordChunk;
	SlipTrackWorldRecordChunk recordChunk;
	bool callGetDrawStateAfterChunk;
	uint32_t drawStateIndexAfterChunk;
	uint32_t restoredDrawStateIndex;
	bool callRestoreDrawStateAfterChunk;
	bool ret;
	SlipTrackWorldNodeDispatchBranch branch;
} SlipTrackWorldNodeDispatch;

typedef enum SlipTrackWorldRecordStepBranch {
	SLIP_TRACK_WORLD_RECORD_STEP_BRANCH_DISPATCHED,
	SLIP_TRACK_WORLD_RECORD_STEP_BRANCH_REJECTED
} SlipTrackWorldRecordStepBranch;

typedef struct SlipTrackWorldRecordStep {
	uint32_t recordOffset;
	bool callRecordScan;
	SlipTrackWorldRecordScan recordScan;
	bool callNodeDispatch;
	SlipTrackWorldNodeDispatch nodeDispatch;
	bool ret;
	SlipTrackWorldRecordStepBranch branch;
} SlipTrackWorldRecordStep;

typedef struct SlipTrackWorldTraversalVisit {
	uint16_t depth;
	uint32_t recordOffset;
	SlipTrackWorldRecordStep step;
} SlipTrackWorldTraversalVisit;

typedef struct SlipTrackWorldTraversal {
	uint32_t rootRecordOffset;
	uint16_t visitCount;
	uint16_t maxDepth;
	bool hitVisitCapacity;
	bool hitStackCapacity;
	uint32_t finalChunkCounter;
	bool ret;
} SlipTrackWorldTraversal;

typedef struct SlipTrackWorldChunkProcessExecution SlipTrackWorldChunkProcessExecution;

typedef enum SlipTrackWorldTraversalCallback {
	SLIP_TRACK_WORLD_TRAVERSAL_CALLBACK_NONE,
	SLIP_TRACK_WORLD_TRAVERSAL_CALLBACK_COMPONENT,
	SLIP_TRACK_WORLD_TRAVERSAL_CALLBACK_DEFERRED_GATE,
} SlipTrackWorldTraversalCallback;

typedef enum SlipTrackWorldPrimitiveCallback {
	SLIP_TRACK_WORLD_PRIMITIVE_CALLBACK_NONE,
	SLIP_TRACK_WORLD_PRIMITIVE_CALLBACK_DRAW,
} SlipTrackWorldPrimitiveCallback;

typedef enum SlipTrackWorldRecordCallback {
	SLIP_TRACK_WORLD_RECORD_CALLBACK_NONE,
	SLIP_TRACK_WORLD_RECORD_CALLBACK_SCENERY,
	SLIP_TRACK_WORLD_RECORD_CALLBACK_DEFERRED_HEADER_ENTRY,
	SLIP_TRACK_WORLD_RECORD_CALLBACK_DEFERRED_HEADER,
} SlipTrackWorldRecordCallback;

typedef struct SlipTrackWorldTraversalContext {
	uint32_t trkBasePointer;
	const uint8_t *tableBase;
	size_t tableSize;
	const uint8_t *pointBase;
	size_t pointBaseSize;
	SlipView3DVec32 origin;
	int32_t minZ;
	int32_t maxZ;
	SlipTrackWorldProjectMask projectMask;
	void *projectMaskUserData;
	const uint8_t *chunkBase;
	size_t chunkBaseSize;
	const uint8_t *filter;
	uint32_t chunkCounter;
	SlipTrackWorldChunkProcessExecution *chunkProcessExecution;
	uint32_t *defaultTraversalGate;
	uint16_t *mask;
	uint32_t *renderContextCount;
	uint32_t *primaryLeft;
	uint32_t *primaryTop;
	uint32_t *primaryRight;
	uint32_t *primaryBottom;
	SlipTrackWorldTraversalCallback *traversalCallback;
	SlipTrackWorldRecordCallback *recordCallback;
	uint32_t *deferredEntryActive;
	uint32_t drawStateIndex;
	SlipTrackWorldTraversalVisit *visits;
	uint16_t visitCapacity;
	uint32_t *stackOffsets;
	uint16_t *stackDepths;
	uint16_t stackCapacity;

	SlipDraw3DProjectState *projectState;
	struct SlipTrackWorldProjectFrustum *frustum;
	bool (*storeClipBounds)(uint32_t, uint32_t, uint32_t, uint32_t, void *);
	void *storeClipBoundsUserData;
	uint32_t useFullObjectViewport;
	const struct SlipTrackWorldComponentRefuelCalls *refuelCalls;
} SlipTrackWorldTraversalContext;

typedef enum SlipTrackWorldChunkPointListBranch {
	SLIP_TRACK_WORLD_CHUNK_POINTS_PRESENT,
	SLIP_TRACK_WORLD_CHUNK_POINTS_ABSENT
} SlipTrackWorldChunkPointListBranch;

typedef struct SlipTrackWorldChunkPointList {
	uint16_t pointListOffset;
	bool callBuildVertexRecords;
	const uint8_t *pointListEntries;
	uint32_t pointListCount;
	uint32_t pointRecordStride;
	SlipTrackWorldChunkPointListBranch branch;
} SlipTrackWorldChunkPointList;

typedef struct SlipTrackWorldChunkBsp {
	uint16_t bspOffset;
	bool callTraverseRawBsp;
	const uint8_t *bspTreePointer;
	const uint8_t *currentChunkAfterBsp;
	bool callChunkFallback;
	bool callRestoreVertexBuffer;
} SlipTrackWorldChunkBsp;

typedef struct SlipTrackWorldChunkProcess {
	bool callSetupChunk;
	SlipTrackWorldChunkSetup chunkSetup;
	bool callPrepareChunkPointList;
	SlipTrackWorldChunkPointList pointListSetup;
	bool callProcessChunkBsp;
	SlipTrackWorldChunkBsp bspSetup;
	bool ret;
} SlipTrackWorldChunkProcess;

typedef struct SlipTrackWorldChunkProcessVertexCache {
	bool callSetupChunk;
	SlipTrackWorldChunkSetup chunkSetup;
	bool callPrepareChunkPointList;
	SlipTrackWorldChunkPointList pointListSetup;
	bool callBuildVertexRecords;
	SlipDraw3DBuildVertexRecords buildVertexRecords;
	bool callProcessChunkBsp;
	SlipTrackWorldChunkBsp bspSetup;
	bool callTraverseRawBsp;
	uint16_t rawBspNodeCount;
	uint16_t rawBspClassifyCount;
	uint16_t rawBspCallbackCount;
	bool rawBspHitDepthCapacity;
	bool ret;
} SlipTrackWorldChunkProcessVertexCache;

typedef bool (*SlipTrackWorldRawBspClassify)(const uint8_t *node, uint16_t normalX, uint16_t normalY, uint16_t normalZ,
                                             uint16_t nodeFlags, void *userData, bool *classificationCarry);

typedef bool (*SlipTrackWorldRawBspCallback)(const uint8_t *node, uint32_t callbackValue, uint16_t recordKind,
                                             int32_t classificationValue, void *userData);

typedef bool (*SlipTrackWorldBuildVertexRecords)(const uint8_t *source, size_t sourceBytes, uint16_t vertexCount,
                                                 int16_t sourceStride, void *userData);

typedef bool (*SlipTrackWorldRestoreVertexBuffer)(void *userData);

typedef SlipView3DVec32 (*SlipTrackWorldSourcePoint)(int16_t positionX, int16_t positionY, int16_t positionZ,
                                                     void *userData);

typedef bool (*SlipTrackWorldObjectContinuation)(const uint8_t *object, size_t objectBytesRemaining,
                                                 uint32_t objectAddress, uint32_t counter, uint8_t *objectListEntry,
                                                 size_t objectListEntryBytes, SlipView3DVec32 objectOffset,
                                                 SlipView3DVec32 transformedObjectOffset, void *userData);

typedef struct SlipTrackWorldRawBsp {
	bool pushOldBase;
	bool pushOldCallback;
	const uint8_t *base;
	uint32_t rootRelativeOffset;
	uint16_t nodeCount;
	uint16_t classifyCount;
	uint16_t callbackCount;
	bool hitDepthCapacity;
	bool restoreCallback;
	bool restoreBase;
	bool ret;
} SlipTrackWorldRawBsp;

struct SlipTrackWorldChunkProcessExecution {
	SlipDraw3DVertexRecord *vertexRecords;
	size_t vertexRecordCapacity;
	SlipTrackWorldBuildVertexRecords buildVertexRecords;
	void *buildVertexRecordsUserData;
	SlipTrackWorldRestoreVertexBuffer restoreVertexBuffer;
	void *restoreVertexBufferUserData;
	SlipView3DVec32 *offset;
	SlipView3DVec32 *currentChunkOrigin;
	SlipTrackWorldRawBspClassify classify;
	SlipTrackWorldRawBspCallback callback;
	void *rawBspUserData;
	uint16_t rawBspDepthCapacity;
	bool (*chunkFallback)(const uint8_t *chunk, size_t chunkBytesRemaining, void *userData);
	void *chunkFallbackUserData;
	SlipTrackWorldObjectContinuation objectContinuation;
	void *objectContinuationUserData;
	SlipTrackWorldDrawStateLoad drawStateLoad;
	void *drawStateLoadUserData;
};

typedef struct SlipTrackWorldPlaneClassify {
	uint32_t projectionMode;
	bool matrixFacingMode;
	uint16_t vertexIndex;
	uint32_t vertexRecordOffset;
	int16_t sourceX;
	int16_t sourceY;
	int16_t sourceZ;
	bool callTransformSourcePoint;
	SlipView3DVec32 transformedSourcePoint;
	SlipView3DVec32 pointMinusOrigin;
	int16_t planeX;
	int16_t planeY;
	int16_t planeZ;
	int64_t dotProduct;
	bool carry;
} SlipTrackWorldPlaneClassify;

typedef struct SlipTrackWorldAxisPlaneClassify {
	uint32_t projectionMode;
	bool perspectiveBranch;
	uint32_t pointX;
	uint32_t pointY;
	uint32_t pointZ;
	uint16_t axisX;
	uint16_t axisY;
	uint16_t axisZ;
	int64_t dotProductX;
	int64_t dotProductY;
	int64_t dotProductZ;
	uint16_t dotProductHighWord;
	bool dotProductNegative;
	bool clearsCarry;
	bool setsCarry;
	bool carry;
} SlipTrackWorldAxisPlaneClassify;

typedef struct SlipTrackWorldChunkAlternatePaths {
	uint16_t recordCallbackOffset;
	const uint8_t *recordCallbackRecord;
	uint16_t vertexIndex;
	bool callGetVertexPosition;
	bool callRecordCallback;
	uint16_t traversalCallbackOffset;
	const uint8_t *traversalCallbackRecord;
	bool callTrackWorldDeferredMembership;
	bool trackWorldDeferredMembershipCarry;
	bool callTraversalCallback;
} SlipTrackWorldChunkAlternatePaths;

typedef struct SlipTrackWorldChunkDispatch {
	uint16_t recordKind;
	uint16_t recordOffset;
	const uint8_t *callbackRecord;
	bool callTrackWorldDeferredMembership;
	bool trackWorldDeferredMembershipCarry;
	bool savedChunkBeforeTraversalCallback;
	bool callTraversalCallback;
	bool savedChunkBeforeRecordCallback;
	uint16_t vertexIndex;
	bool callGetVertexPosition;
	bool callRecordCallback;
} SlipTrackWorldChunkDispatch;

typedef struct SlipTrackWorldRecordVisibility {
	uint16_t recordMaskWord;
	uint16_t maskedRecordWord;
	bool skippedByMask;
	uint32_t radius;
	uint32_t cachedRadius;
	uint32_t viewPositionX;
	uint32_t viewPositionY;
	uint32_t viewPositionZ;
	bool callDraw3DDetailValue;
	int32_t detailValue;
	int32_t detailThreshold;
	bool skippedByDetail;
	bool continues;
} SlipTrackWorldRecordVisibility;

typedef enum SlipTrackWorldRecordTransformBranch {
	SLIP_TRACK_WORLD_RECORD_TRANSLATION,
	SLIP_TRACK_WORLD_RECORD_MATRIX
} SlipTrackWorldRecordTransformBranch;

typedef struct SlipTrackWorldRecordTransformSetup {
	uint16_t shapeHandle;
	bool savedRecordPointer;
	bool callGetDrawStateIndex;
	uint32_t drawStateIndexAfterAdvance;
	bool callLoadDrawState;
	uint32_t centerY;
	uint32_t cachedCenterY;
	uint32_t positionY;
	uint32_t positionYWithCenter;
	uint32_t savedPositionX;
	uint32_t savedPositionYWithCenter;
	uint32_t savedPositionZ;
	uint16_t facingTransformFlag;
	SlipTrackWorldRecordTransformBranch branch;
} SlipTrackWorldRecordTransformSetup;

typedef struct SlipTrackWorldRecordMatrixTransform {
	uint32_t facingModeFlag;
	bool savedShapeHandle;
	const uint8_t *recordMatrix;
	uint32_t worldMatrixAddress;
	bool callView3DCopyMatrixWords;
	uint32_t worldMatrixAddressAfterCopy;
	uint32_t worldMatrixSourceAddress;
	uint32_t viewMatrixDestinationAddress;
	uint32_t cameraMatrixAddress;
	bool callMultiplyMatrix;
	uint32_t continuationAddress;
} SlipTrackWorldRecordMatrixTransform;

typedef struct SlipTrackWorldRecordFacingTransform {
	uint32_t facingModeFlag;
	bool savedShapeHandle;
	uint32_t positionX;
	uint32_t positionZ;
	uint32_t cameraDeltaX;
	uint32_t cameraDeltaZ;
	bool callView3DNormalizeScaledVector2D;
	SlipView3DNormalizeScaledVector2D normalize;
	uint32_t facingAxisX;
	uint32_t facingAxisZ;
	uint32_t zeroMatrixEntry;
	uint32_t worldMatrixAddress;
	uint16_t facingMatrixZX;
	uint16_t facingMatrixZY;
	uint16_t facingMatrixZZ;
	uint32_t negatedFacingAxisX;
	uint16_t facingMatrixXX;
	uint16_t facingMatrixXY;
	uint16_t facingMatrixXZ;
	uint16_t facingMatrixYX;
	uint16_t facingMatrixYY;
	uint16_t facingMatrixYZ;
	uint32_t viewMatrixDestinationAddress;
	uint32_t cameraMatrixAddress;
	bool callMultiplyMatrix;
	uint32_t viewMatrixAddressAfterMultiply;
	uint16_t planarViewMatrixXZ;
	uint16_t planarViewMatrixYZ;
	uint16_t planarViewMatrixZX;
	uint16_t planarViewMatrixZY;
	uint16_t planarViewMatrixZZ;
} SlipTrackWorldRecordFacingTransform;

typedef struct SlipTrackWorldRecordScaledCenter {
	bool restoredShapeHandle;
	uint32_t viewMatrixAddress;
	uint32_t centerY;
	int16_t matrixYZ;
	uint32_t centerOffsetZ;
	uint32_t centerOffsetZCopy;
	int16_t matrixYY;
	uint32_t centerOffsetY;
	uint32_t centerOffsetYCopy;
	int16_t matrixYX;
	uint32_t centerOffsetX;
	uint32_t viewCenterX;
	uint32_t viewCenterY;
	uint32_t viewCenterZ;
	uint32_t cachedViewCenterZ;
	bool restorePositionZ;
	uint32_t restoredPositionZ;
	bool restorePositionYWithCenter;
	uint32_t restoredPositionYWithCenter;
	bool restorePositionX;
	uint32_t restoredPositionX;
	bool callDrawSetup;
	SlipShape3DDrawSetup drawSetup;
} SlipTrackWorldRecordScaledCenter;

typedef enum SlipTrackWorldRecordCullBranch {
	SLIP_TRACK_WORLD_RECORD_DRAW_READY,
	SLIP_TRACK_WORLD_RECORD_CULLED
} SlipTrackWorldRecordCullBranch;

typedef bool (*SlipTrackWorldSphereCull)(SlipView3DVec32 center, int32_t radius, void *userData);

typedef bool (*SlipTrackWorldComponentActorDraw)(uint32_t objectListOffset, const uint8_t *sectionRecord,
                                                 uint32_t sectionRenderFlags, SlipView3DVec32 componentViewOrigin,
                                                 void *userData);

typedef bool (*SlipTrackWorldShapeDraw)(uint32_t recordAddress,

                                        uint16_t resourceHandleIndex,

                                        const uint8_t *record, size_t recordBytes,

                                        const SlipView3DMatrix *worldMatrix,

                                        const SlipView3DMatrix *objectViewMatrix, SlipView3DVec32 objectPosition,

                                        SlipView3DVec32 viewPosition, uint32_t renderFlags, void *userData);

typedef struct SlipTrackWorldIndirectCull {
	SlipView3DVec32 center;
	int32_t radius;
	bool callSphereCull;
	bool sphereCullCarry;
	bool ret;
} SlipTrackWorldIndirectCull;

typedef struct SlipTrackWorldRecordSphereCull {
	SlipView3DVec32 center;
	uint32_t cullingRadius;
	bool callTrackWorldIndirectCull;
	SlipTrackWorldIndirectCull indirectCull;
	bool trackWorldIndirectCullCarry;
	uint32_t visibleRecordCount;
	uint32_t facingMatrixToken;
	uint32_t planarViewMatrixToken;
	uint32_t viewDepth;
	bool callTrackWorldUpdateDrawFlags;
	SlipTrackWorldRecordCullBranch branch;
} SlipTrackWorldRecordSphereCull;

typedef struct SlipTrackWorldRecordDrawDispatch {
	uint16_t recordMask;
	bool skipMaskFlagUpdate;
	bool callGetRenderFlagsForMask;
	uint32_t renderFlagsBeforeMask;
	uint32_t renderFlagsAfterMask;
	bool callSetRenderFlagsAfterMask;
	uint32_t renderFlagsToStore;
	uint32_t facingModeFlag;
	bool drawWithoutFacingFlagTest;
	bool callGetRenderFlagsForFacing;
	uint32_t renderFlagsForFacing;
	bool facingSkipFlagSet;
	bool skipShapeDraw;
	bool callDrawShape;
	uint32_t restoredFrameRenderFlags;
	bool callRestoreFrameRenderFlags;
} SlipTrackWorldRecordDrawDispatch;

typedef struct SlipTrackWorldRecordDrawRestore {
	bool callGetDrawStateIndex;
	uint32_t drawStateIndexAfterRestore;
	bool callLoadDrawState;
	bool restoredRecordPointer;
	bool ret;
} SlipTrackWorldRecordDrawRestore;

typedef enum SlipTrackWorldObjectRecordLookupBranch {
	SLIP_TRACK_WORLD_OBJECT_ATTACHMENT_FOUND,
	SLIP_TRACK_WORLD_OBJECT_LIST_READY,
	SLIP_TRACK_WORLD_OBJECT_LIST_EMPTY
} SlipTrackWorldObjectRecordLookupBranch;

typedef struct SlipTrackWorldObjectRecordLookup {
	bool savedComponentContext;
	uint32_t attachmentListOffset;
	bool attachmentListAbsent;
	uint32_t objectListCount;
	bool callDraw3DListPushFrame;
	uint32_t attachmentListAddress;
	SlipTrackWorldObjectRecordLookupBranch branch;
} SlipTrackWorldObjectRecordLookup;

typedef enum SlipTrackWorldObjectAttachmentDrawBranch {
	SLIP_TRACK_WORLD_OBJECT_ATTACHMENT_DRAWN,
	SLIP_TRACK_WORLD_OBJECT_ATTACHMENT_SKIPPED
} SlipTrackWorldObjectAttachmentDrawBranch;

typedef struct SlipTrackWorldObjectAttachmentDraw {
	const uint8_t *savedAttachmentListHead;
	bool savedComponentContext;
	const uint8_t *drawRecord;
	const uint8_t *attachmentListHead;
	uint32_t objectOffset;
	const uint8_t *currentDrawRecord;
	bool savedRecordForSlotLookup;
	bool callSelectSlotListEntry;
	uint32_t slotAttachmentReference;
	bool restoredRecordAfterSlotLookup;
	bool callDraw3DListPushFrame;
	bool savedRecordForCallbackRead;
	bool callObjectGetSlotDrawCallback;
	bool savedDrawCallback;
	uint32_t schedulingCallbackAddress;
	bool callInstallSchedulingCallback;
	bool callDrawObject;
	bool callDraw3DListTraverse;
	bool callDraw3DListPopFrame;
	bool restoredDrawCallback;
	bool callRestoreDrawCallback;
	bool restoredDrawRecord;
	SlipTrackWorldObjectAttachmentDrawBranch branch;
} SlipTrackWorldObjectAttachmentDraw;

typedef enum SlipTrackWorldObjectAttachmentMatchBranch {
	SLIP_TRACK_WORLD_OBJECT_ATTACHMENT_DIFFERENT,
	SLIP_TRACK_WORLD_OBJECT_ATTACHMENT_MATCHED
} SlipTrackWorldObjectAttachmentMatchBranch;

typedef struct SlipTrackWorldObjectAttachmentMatch {
	uint32_t nextDrawRecordAddress;
	uint32_t attachmentListHeadAddress;
	bool restoredComponentContext;
	bool restoredAttachmentListHead;
	bool callDraw3DListPushFrame;
	SlipTrackWorldObjectAttachmentMatchBranch branch;
} SlipTrackWorldObjectAttachmentMatch;

typedef enum SlipTrackWorldObjectCallbackDrawBranch {
	SLIP_TRACK_WORLD_OBJECT_CALLBACK_DRAWN,
	SLIP_TRACK_WORLD_OBJECT_CALLBACK_SKIPPED
} SlipTrackWorldObjectCallbackDrawBranch;

typedef struct SlipTrackWorldObjectCallbackDraw {
	const uint8_t *drawRecord;
	const uint8_t *attachmentListHead;
	uint32_t objectOffset;
	const uint8_t *currentDrawRecord;
	bool savedRecordForSlotLookup;
	bool callSelectSlotListEntry;
	uint32_t slotAttachmentReference;
	bool restoredRecordAfterSlotLookup;
	bool savedRecordForCallbackRead;
	bool callObjectGetSlotDrawCallback;
	bool savedDrawCallback;
	uint32_t schedulingCallbackAddress;
	bool callInstallSchedulingCallback;
	bool callDrawObject;
	bool restoredDrawCallback;
	bool callRestoreDrawCallback;
	bool restoredDrawRecord;
	uint32_t drawnObjectCount;
	SlipTrackWorldObjectCallbackDrawBranch branch;
} SlipTrackWorldObjectCallbackDraw;

typedef enum SlipTrackWorldObjectCallbackMatchBranch {
	SLIP_TRACK_WORLD_OBJECT_CALLBACK_DIFFERENT,
	SLIP_TRACK_WORLD_OBJECT_CALLBACK_MATCHED
} SlipTrackWorldObjectCallbackMatchBranch;

typedef struct SlipTrackWorldObjectCallbackMatch {
	uint32_t nextDrawRecordAddress;
	uint32_t attachmentListHeadAddress;
	SlipTrackWorldObjectCallbackMatchBranch branch;
} SlipTrackWorldObjectCallbackMatch;

typedef enum SlipTrackWorldObjectListHeadBranch {
	SLIP_TRACK_WORLD_OBJECT_LIST_HEAD_MATCHED,
	SLIP_TRACK_WORLD_OBJECT_LIST_HEAD_DIFFERENT,
	SLIP_TRACK_WORLD_OBJECT_LIST_HEAD_EMPTY
} SlipTrackWorldObjectListHeadBranch;

typedef struct SlipTrackWorldObjectListHead {
	uint32_t objectListCount;
	bool objectListEmpty;
	const uint8_t *objectListRecord;
	bool savedRemainingCount;
	bool savedObjectListRecord;
	uint32_t recordComponentAddress;
	uint32_t currentComponentAddress;
	SlipTrackWorldObjectListHeadBranch branch;
} SlipTrackWorldObjectListHead;

typedef struct SlipTrackWorldObjectRelativePosition {
	uint32_t worldX;
	uint32_t worldY;
	uint32_t worldZ;
	uint32_t relativeX;
	uint32_t relativeY;
	uint32_t relativeZ;
	uint32_t cameraObjectOffset;
	bool savedObjectListRecord;
	bool callCopyCameraMatrix;
	bool callTransformRelativePosition;
	uint32_t drawDepth;
	bool restoredObjectListRecord;
	const uint8_t *callbackData;
	uint32_t drawCallbackAddress;
	bool callDraw3DListInsert;
} SlipTrackWorldObjectRelativePosition;

typedef enum SlipTrackWorldObjectListAdvanceBranch {
	SLIP_TRACK_WORLD_OBJECT_LIST_CONTINUE,
	SLIP_TRACK_WORLD_OBJECT_LIST_FINISHED
} SlipTrackWorldObjectListAdvanceBranch;

typedef struct SlipTrackWorldObjectListAdvance {
	uintptr_t currentRecordAddress;
	uint32_t remainingCountBefore;
	uintptr_t nextRecordAddress;
	uint32_t remainingCountAfter;
	SlipTrackWorldObjectListAdvanceBranch branch;
} SlipTrackWorldObjectListAdvance;

typedef enum SlipTrackWorldObjectListFinalizeBranch {
	SLIP_TRACK_WORLD_OBJECT_LIST_CURRENT_MATCHED,
	SLIP_TRACK_WORLD_OBJECT_LIST_CURRENT_DIFFERENT
} SlipTrackWorldObjectListFinalizeBranch;

typedef struct SlipTrackWorldObjectListFinalize {
	uint32_t currentComponentAddress;
	uint32_t limitComponentAddress;
	SlipTrackWorldObjectListFinalizeBranch branch;
	uint32_t limitLower;
	uint32_t limitUpper;
	bool callDraw3DSetLimitState;
	bool callDraw3DListTraverse;
	bool callDraw3DListPopFrame;
	bool callDraw3DClearLimitState;
	bool restoredComponentContext;
	bool returned;
} SlipTrackWorldObjectListFinalize;

typedef struct SlipObjectMatrixBindingResult {
	uint32_t objectHandleBeforeMask;
	uint32_t objectOffset;
	uint32_t objectAddress;
	uint32_t matrixAddress;
	uint32_t destinationMatrixAddress;
	bool callView3DCopyMatrixWords;
	bool restoredObjectHandle;
	bool returned;
} SlipObjectMatrixBindingResult;

typedef struct SlipObjectMatrixCopy {
	uint32_t objectHandleBeforeMask;
	size_t objectOffset;
	const SlipView3DMatrix *sourceMatrix;
	SlipView3DMatrix *destinationMatrix;
	bool callView3DCopyMatrixWords;
	bool restoredObjectHandle;
	bool returned;
} SlipObjectMatrixCopy;

typedef struct SlipObjectRotate {
	size_t objectOffset;
	bool callTrackWorldSlipObjectInvalidateViewPositions;
	bool callView3DApplyPitchMatrix;
	bool callView3DApplyRow0Row1Rotation;
	bool callView3DApplyRow0Row2Rotation;
	bool callView3DApplyColumn0Column2Rotation;
	bool callView3DOrthonormalizeForwardBasis;
} SlipObjectRotate;

typedef struct SlipObjectSetDirection {
	size_t objectOffset;
	bool zeroVector;
	bool callView3DNormalizeVector3D;
	uint16_t directionX;
	uint16_t directionY;
	uint16_t directionZ;
	uint32_t magnitude;
} SlipObjectSetDirection;

typedef struct SlipObjectDirection {
	int16_t directionXQ14;
	int16_t directionYQ14;
	int16_t directionZQ14;
} SlipObjectDirection;

typedef struct SlipObjectMatrixInstall {
	uint32_t objectHandleBeforeMask;
	const SlipView3DMatrix *savedSourceMatrix;
	bool callTrackWorldSlipObjectInvalidateViewPositions;
	uint16_t clearedFlagSlots;
	size_t objectOffset;
	const SlipView3DMatrix *sourceMatrix;
	SlipView3DMatrix *destinationMatrix;
	bool callView3DCopyMatrixWords;
	bool restoredSourceMatrix;
	bool restoredObjectHandle;
	bool returned;
} SlipObjectMatrixInstall;

typedef struct SlipObjectPosition {
	uint32_t objectId;
	size_t objectOffset;
	uint32_t positionX;
	uint32_t positionY;
	uint32_t positionZ;
	bool returned;
} SlipObjectPosition;

typedef struct SlipObjectSetPosition {
	uint32_t objectHandleBeforeMask;
	uint32_t maskedObjectHandle;
	size_t objectOffset;
	uint32_t positionX;
	uint32_t positionY;
	uint32_t positionZ;
	uint16_t flagsBefore;
	uint16_t flagsAfter;
	bool callTrackWorldSlipObjectInvalidateViewPositions;
	bool restoredObjectHandle;
	bool returned;
} SlipObjectSetPosition;

typedef struct SlipObjectResetActiveList {
	uint32_t savedAllocationInput;
	uint16_t objectCount;
	size_t clearedActiveRecords;
	bool callAllocateObject;
	size_t allocatedObjectOffset;
	size_t activeHeadOffset;
	size_t activeTailOffset;
	bool carryOut;
	bool returned;
} SlipObjectResetActiveList;

typedef struct SlipTrackStartRecord {
	size_t recordOffset;
	uint32_t startPositionX;
	uint32_t startPositionY;
	uint32_t startPositionZ;
	bool returned;
} SlipTrackStartRecord;

typedef struct SlipTrackStartHeading {
	uint16_t headingXQ14;
	uint16_t headingYQ14;
	uint16_t headingZQ14;
	bool returned;
} SlipTrackStartHeading;

typedef struct SlipTrackWorldCurrentSlot {
	uint32_t slotRecordAddress;
	uint32_t trackRecordAddress;
	bool carryOut;
} SlipTrackWorldCurrentSlot;

typedef enum SlipObjectEvent {

	SLIP_OBJECT_EVENT_INITIALIZE = 0x0101,
	SLIP_OBJECT_EVENT_FREE = 0x0102,
	SLIP_OBJECT_EVENT_FREED = 0x0103,
	SLIP_OBJECT_EVENT_UPDATE = 0x0104,
	SLIP_OBJECT_EVENT_HANDLE_ACTION = 0x0105,
	SLIP_OBJECT_EVENT_COLLISION_STOP = 0x0106,
	SLIP_OBJECT_EVENT_COLLISION_BOUNCE = 0x0107,
	SLIP_OBJECT_EVENT_RESET_MOTION = 0x0108,
	SLIP_OBJECT_EVENT_SET_CONTROLLER = 0x0200,
	SLIP_OBJECT_EVENT_BIND_RACER = 0x0201,
	SLIP_OBJECT_EVENT_APPLY_DAMAGE = 0x0202
} SlipObjectEvent;

/* Release servers receive a separate event code from per-object callbacks. */
enum { SLIP_OBJECT_SERVER_EVENT_FREE = 1u, SLIP_OBJECT_RELEASE_SERVER_ID = 1u };

enum { SLIP_OBJECT_EVENT_UPPER_WORD_MASK = 0xffff0000u };

typedef uint32_t (*SlipObjectEventCallback)(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue,
                                            uint32_t eventFlags, uint16_t objectOffset, uintptr_t dispatchData,
                                            uint32_t dispatchFrame);

struct TrackViewRawBspContext;

typedef SlipDraw3DListCallback SlipObjectDrawCallback;

typedef struct SlipObjectSetCallback {
	size_t objectOffset;
	SlipObjectDrawCallback drawCallback;
	uint32_t drawCallbackData;
	bool returned;
} SlipObjectSetCallback;

enum {
	SLIP_OBJECT_COUNT = 100,
	SLIP_OBJECT_VIEW_POSITION_VALID = 1u,
	SLIP_OBJECT_DOS_STRIDE = 0xae,
	SLIP_OBJECT_DOS_TRACK_SLOT_END = 16,
	SLIP_OBJECT_PRIVATE_STATE_BYTES = 0x4e,
	SLIP_OBJECT_TABLE_DOS_BYTES = SLIP_OBJECT_COUNT * SLIP_OBJECT_DOS_STRIDE,
	SLIP_TRACK_SLOT_DRAW_RECORD_COUNT = 0x40 * 2 + 1
};

typedef struct SlipObject SlipObject;

extern SlipObject *SlipObject_table;
extern uint16_t SlipObject_count;

void SlipObject_DrawVisible(struct TrackViewRawBspContext *context);

typedef struct SlipRaceWreckState {
	SlipObjectEventCallback nextEvent;
	int32_t explosionCountdown;
	uint32_t explosionRange;
	uint16_t remainingLifetime;
	int16_t maximumSpeed;
	uint16_t spinning, remainingBounces, debrisCount;
} SlipRaceWreckState;

struct SlipObject {
	uint16_t allocated;
	uint16_t flags;
	SlipObject *next;
	SlipObject *previous;
	uint16_t physicsRecordOffset;
	uint16_t trackSlotOffset;
	uint32_t actorHandle;
	SlipView3DVec32 position;
	SlipView3DVec32 viewPosition;
	int32_t speed;

	union {
		uintptr_t drawData;
		struct SlipActorRecord *actorData;
	};

	uint32_t drawExtent;
	SlipObjectEventCallback eventCallback;
	SlipObjectDrawCallback slotDrawCallback;
	SlipObjectDrawCallback drawCallback;
	uint32_t drawCallbackData;
	SlipView3DMatrix matrix;
	SlipView3DVec16 direction;

	union {
		uint8_t privateState[SLIP_OBJECT_PRIVATE_STATE_BYTES];
		SlipTimedEffectObjectState timedEffect;
		SlipCrossEffectState crossEffect;
		SlipAnimatedState animatedEffect;
		SlipRaceDebrisState debrisEffect;
		SlipRaceWreckState wreckEffect;
	};
};

typedef struct SlipObjectActorHandleWriteResult {
	size_t objectOffset;
	uint32_t actorHandle;
	bool returned;
} SlipObjectActorHandleWriteResult;

typedef struct SlipObjectSlotAllocate {
	uint16_t scannedSlots;
	bool exhausted;
	uint32_t errorCode;
	size_t objectOffset;
	bool firstRecord;
	size_t previousTailOffset;
	bool carryOut;
	bool returned;
} SlipObjectSlotAllocate;

typedef struct SlipObjectSlotFill {
	const SlipView3DMatrix *savedTemplate;
	bool callAllocateObject;
	SlipObjectSlotAllocate allocate;
	size_t objectOffset;
	uint32_t positionX;
	uint32_t positionY;
	uint32_t positionZ;
	SlipObjectEventCallback eventCallback;
	uint32_t drawData;
	SlipObjectDrawCallback slotDrawCallback;
	uint16_t directionXQ14;
	uint16_t directionYQ14;
	uint16_t directionZQ14;
	bool copyMatrix;
	uint16_t initializeEventCode;
	bool callObjectDispatchEvent;
	bool carryOut;
	bool returned;
} SlipObjectSlotFill;

typedef struct SlipObjectInitTable {
	uint16_t objectCount;
	size_t tableBytes;
	size_t zeroedRecords;
	bool callResetActiveList;
	SlipObjectResetActiveList reset;
	bool returned;
} SlipObjectInitTable;

typedef struct SlipObjectDrawCallbackWriteResult {
	uint32_t objectHandleBeforeMask;
	uint32_t maskedObjectHandle;
	size_t objectOffset;
	SlipObjectDrawCallback slotDrawCallback;
	bool restoredObjectHandle;
	bool returned;
} SlipObjectDrawCallbackWriteResult;

typedef struct SlipObjectDrawCallbackReadResult {
	uint32_t maskedObjectHandle;
	size_t objectOffset;
	SlipObjectDrawCallback slotDrawCallback;
	bool returned;
} SlipObjectDrawCallbackReadResult;

typedef struct SlipObjectSlotDataWriteResult {
	uint32_t objectHandleBeforeMask;
	uint32_t maskedObjectHandle;
	size_t objectOffset;
	uint32_t drawData;
	bool restoredObjectHandle;
	bool returned;
} SlipObjectSlotDataWriteResult;

typedef struct SlipObjectExtentWriteResult {
	uint32_t objectHandleBeforeMask;
	uint32_t maskedObjectHandle;
	size_t objectOffset;
	uint32_t drawExtent;
	bool restoredObjectHandle;
	bool returned;
} SlipObjectExtentWriteResult;

typedef struct SlipObjectEventCallbackWriteResult {
	uint32_t objectHandleBeforeMask;
	uint32_t maskedObjectHandle;
	size_t objectOffset;
	SlipObjectEventCallback eventCallback;
	bool restoredObjectHandle;
	bool returned;
} SlipObjectEventCallbackWriteResult;

typedef struct SlipObjectSlotDataReadResult {
	uint32_t maskedObjectHandle;
	size_t objectOffset;
	uint32_t drawData;
	bool returned;
} SlipObjectSlotDataReadResult;

typedef struct SlipObjectExtentReadResult {
	uint32_t maskedObjectHandle;
	size_t objectOffset;
	uint32_t drawExtent;
	bool returned;
} SlipObjectExtentReadResult;

typedef struct SlipTrackWorldDrawSchedule {
	bool callGetObjectViewPosition;
	uint32_t drawDepth;
	uint32_t drawRecordAddress;
	uint32_t drawRecordOffset;
	SlipDraw3DListCallback callback;
	bool callDraw3DListInsert;
	bool carryOut;
	bool returned;
} SlipTrackWorldDrawSchedule;

typedef enum SlipTrackWorldObjectDrawDispatchBranch {
	SLIP_TRACK_WORLD_OBJECT_EDGE_DEFAULT_VALUE,
	SLIP_TRACK_WORLD_OBJECT_EDGE_EXPLICIT_VALUE,
	SLIP_TRACK_WORLD_OBJECT_EDGE_ABSENT,
	SLIP_TRACK_WORLD_OBJECT_DRAW_CALLBACK_ABSENT
} SlipTrackWorldObjectDrawDispatchBranch;

typedef struct SlipTrackWorldDrawCallbackHeader {
	bool savedDispatchContext;
	uint16_t drawRecordOffset;
	const uint8_t *drawRecord;
	uint32_t edgeReference;
	uint32_t edgeAddress;
	uint32_t attachmentState;
	SlipTrackWorldObjectDrawDispatchBranch branch;
} SlipTrackWorldDrawCallbackHeader;

typedef struct SlipTrackWorldBuildAttachment {
	uint32_t pairedDrawRecordAddress;
	uint32_t pairedAttachmentState;
	bool savedPairedDrawRecord;
	bool savedDrawRecord;
	SlipView3DVec32 source;
	uint32_t attachmentTransformParameter;
	bool callTransformAttachmentVector;
	SlipView3DVec32 transformed;
	uint32_t restoredTransformParameter;
	bool callTransformAttachmentOrigin;
	bool callDraw3DSetAuxiliaryClipPlane;
	SlipView3DVec32 pairedAttachmentOrigin;
	uint32_t pairedNormalX;
	uint32_t pairedNormalY;
	uint32_t pairedNormalZ;
	bool continueToDrawCallback;
} SlipTrackWorldBuildAttachment;

typedef struct SlipTrackWorldUseAttachment {
	uint32_t attachmentState;
	uint32_t originX;
	uint32_t originY;
	uint32_t originZ;
	uint32_t normalX;
	uint32_t normalY;
	bool savedDrawRecord;
	uint32_t normalZ;
	bool callDraw3DSetAuxiliaryClipPlane;
	bool restoredDrawRecord;
	bool continueToDrawCallback;
} SlipTrackWorldUseAttachment;

typedef struct SlipTrackWorldInvokeDrawCallback {
	uint32_t objectOffset;
	bool callGetObjectViewPosition;
	bool callTrackWorldUpdateDrawFlags;
	bool callLoadRecordIndexBefore;
	uint32_t recordIndexDuringCallback;
	bool callStoreRecordIndexBefore;
	bool savedModeContext;
	bool callSelectSlotListEntry;
	bool callActorPoolGetMode;
	uint32_t defaultActorMode;
	uint32_t renderMode;
	uint32_t actorModeFlagMask;
	uint32_t actorMode;
	bool callActorPoolSetMode;
	bool restoredModeContext;
	bool callDrawCallback;
	uint32_t frameRenderFlags;
	bool callRendererSetFlags;
	bool callLoadRecordIndexAfter;
	uint32_t recordIndexAfterCallback;
	bool callStoreRecordIndexAfter;
	bool callDraw3DClearAuxiliaryClipPlane;
	bool continueToDispatchExit;
} SlipTrackWorldInvokeDrawCallback;

typedef enum SlipTrackWorldComponentVisibilityBranch {
	SLIP_TRACK_WORLD_COMPONENT_BRANCH_VISIBLE,
	SLIP_TRACK_WORLD_COMPONENT_BRANCH_SKIPPED
} SlipTrackWorldComponentVisibilityBranch;

typedef struct SlipTrackWorldComponentGate {
	uint16_t componentOffset;
	const uint8_t *componentRecord;
	uint32_t flagsMergedWithInput;
	uint32_t maskedFlagsMergedWithInput;
	uint16_t maskedFlags;
	uint16_t maskBit8;
	uint32_t maskedFlagsWithComponentBase;
	uint16_t componentBit8;
	uint16_t sourcePointIndex;
	SlipTrackWorldComponentVisibilityBranch branch;
} SlipTrackWorldComponentGate;

typedef enum SlipTrackWorldComponentProjectionBranch {
	SLIP_TRACK_WORLD_COMPONENT_BRANCH_PROJECTED,
	SLIP_TRACK_WORLD_COMPONENT_BRANCH_REJECTED
} SlipTrackWorldComponentProjectionBranch;

typedef struct SlipTrackWorldComponentProject {
	uint16_t sourcePointIndex;
	bool calledTransformPoint;
	SlipView3DVec32 viewPosition;
	bool callTrackWorldCullBounds;
	bool trackWorldCullBoundsCarry;
	SlipTrackWorldComponentProjectionBranch branch;
} SlipTrackWorldComponentProject;

typedef enum SlipTrackWorldCullBranch {
	SLIP_TRACK_WORLD_CULL_BRANCH_OUTSIDE_SPHERE,
	SLIP_TRACK_WORLD_CULL_BRANCH_VISIBLE,
	SLIP_TRACK_WORLD_CULL_BRANCH_OUTSIDE_PLANES
} SlipTrackWorldCullBranch;

typedef struct SlipTrackWorldCullBounds {
	int32_t radius;
	SlipView3DVec32 center;
	bool callTrackWorldIndirectCull;
	bool trackWorldIndirectCullCarry;
	SlipView3DVec32 points[SLIP_TRACK_BOUNDING_CORNER_COUNT];
	uint32_t cornerClipMasks[SLIP_TRACK_BOUNDING_CORNER_COUNT];
	uint32_t mask;
	SlipTrackWorldCullBranch branch;
	SlipView3DVec32 lastPoint;
} SlipTrackWorldCullBounds;

typedef struct SlipTrackWorldProjectFrustum {
	int32_t maxXStep;
	int32_t minXStep;
	int32_t minYStep;
	int32_t maxYStep;
	int16_t minXPlaneDepthQ14;
	int16_t minXPlaneNegXQ14;
	int16_t maxXPlaneNegDepthQ14;
	int16_t maxXPlaneXQ14;
	int16_t maxYPlaneDepthQ14;
	int16_t maxYPlaneYQ14;
	int16_t minYPlaneNegDepthQ14;
	int16_t minYPlaneNegYQ14;
	int32_t minZ;
	int32_t maxZ;
} SlipTrackWorldProjectFrustum;

typedef struct SlipTrackWorldDrawFlags {
	bool loadedRenderFlags;
	uint32_t renderFlagsValue;
	uint32_t flagsWithTextureBit;
	uint32_t textureMode;
	uint32_t flagsAfterTextureModeAndClearBit8;
	uint32_t flagsWithBit2;
	uint32_t rendererSetFlagsTarget;
	uint32_t shading;
	uint32_t componentDistance;
	uint32_t flagsAfterShadingGate;
	uint32_t flagsWithBit4;
	uint32_t secondaryShading;
	uint32_t componentRadius;
	uint32_t flagsAfterSecondaryShadingGate;
	bool callRendererSetFlags;
	uint32_t rendererFlags;
} SlipTrackWorldDrawFlags;

typedef struct SlipTrackWorldComponentRefuelCalls {
	void *context;
	void (*build)(void *, const uint8_t *section, uint32_t incomingValue, const SlipTrackWorldDrawFlags *,
	              uint32_t *active);
	uint16_t (*random)(void *);
} SlipTrackWorldComponentRefuelCalls;

typedef struct SlipTrackWorldFrameDrawState {
	bool loadedRenderFlags;
	uint32_t drawFlagsFrom;
	uint32_t drawFlagsAfterClearBit8;
	uint32_t drawFlagsAfterClearBit2;
	uint32_t bit2Gate;
	uint32_t drawFlagsAfterOptionalBit2;
	uint32_t drawFlagsAfterClearBit4;
	uint32_t bit4Gate;
	uint32_t drawFlagsAfterOptionalBit4;
	uint32_t storedFrameFlags;
	bool callRendererSetFlags;
	uint32_t drawFlagsTo;
	bool returned;
} SlipTrackWorldFrameDrawState;

typedef struct SlipTrackWorldComponentSetup {
	uint32_t componentViewZ;
	bool callTrackWorldUpdateDrawFlags;
	SlipTrackWorldDrawFlags drawFlags;
	bool callRefuelBuildBeams;
	uint16_t recordShade;
	bool specialRecord;
	bool callTrackWorldRandomStep;
	uint16_t randomShade;
	uint16_t shade;
	uint32_t attachmentListOffset;
	uint16_t replayCount;
	bool calledReplayListBuild;
	SlipView3DVec32 objectWorldPosition;
	const uint8_t *currentRecord;
	bool savedRecordPointer;
	uint32_t processedComponentCount;
	bool callGetDrawStateIndex;
	uint32_t drawStateIndexBefore;
	uint32_t drawStateIndexAfter;
	bool callLoadDrawState;

	uint16_t replayList[SLIP_TRACK_REPLAY_OBJECT_CAPACITY];
} SlipTrackWorldComponentSetup;

typedef struct SlipTrackWorldComponentTailVisit {
	const uint8_t *primitiveRecord;
	SlipTrackWorldPrimitiveCallback primitiveCallback;
	uint16_t remainingCountBefore;
	uint16_t remainingCountAfter;
	bool readAdvance;
	uint16_t descriptor;
	bool highBit;
	uint32_t advance;
} SlipTrackWorldComponentTailVisit;

typedef struct SlipTrackWorldComponentTail {
	uint16_t childOffset;
	bool noChildList;
	const uint8_t *childRecord;
	uint16_t vertexCount;
	bool savedComponentForVertexBuild;
	const uint8_t *vertexSource;
	uint32_t vertexSourceStride;
	bool callBuildVertexRecords;
	bool restoredComponentAfterVertexBuild;
	uint16_t shade;
	bool calledScaleLight;
	bool callTrackWorldPrimitiveWalker;
	bool loadedRenderFlags;
	uint32_t renderFlagsValue;
	bool savedRenderFlags;
	bool calledComponentActorDraw;
	uint32_t restoredRenderFlags;
	bool callRendererSetFlags;
	uint16_t replayCount;
	bool calledReplayObjects;
	const uint8_t *replayRecord;
	uint32_t ambientLightScaleQ14;
	bool callDraw3DSetAmbientLight;
	uint32_t scaledLightX;
	uint32_t scaledLightY;
	uint32_t scaledLightZ;
	uint32_t directLightScaleQ14;
	bool callDraw3DSetLightVector;
	uint32_t renderContextCount;
	uint16_t nestedOffset;
	const uint8_t *nestedList;
	uint16_t loopCount;
	bool savedComponentForNestedList;
	bool restoredComponentAfterNestedList;
	uint16_t childOffsetForVertexRestore;
	bool calledRestoreVertexBufferCursor;
	bool callGetDrawStateIndex;
	uint32_t drawStateIndexBefore;
	uint32_t drawStateIndexAfter;
	bool callLoadDrawState;
	bool restoredRecordPointer;
	bool returned;
} SlipTrackWorldComponentTail;

typedef bool (*SlipTrackWorldComponentLight)(void *context, const SlipTrackWorldComponentTail *tail);

typedef enum SlipTrackWorldListSetupBranch {
	SLIP_TRACK_WORLD_LIST_SETUP_BRANCH_READY,
	SLIP_TRACK_WORLD_LIST_SETUP_BRANCH_INITIALIZED
} SlipTrackWorldListSetupBranch;

typedef struct SlipTrackWorldListHeadClear {
	uint8_t *objectListHead;
	bool zeroObjectCount;
	uint8_t *objectListWriteCursor;
	uint8_t *deferredListHead;
	bool zeroDeferredCount;
} SlipTrackWorldListHeadClear;

typedef struct SlipTrackWorldListSetup {
	uint8_t *objectListHead;
	bool zeroedObjectCount;
	uint8_t *objectListWriteCursor;
	uint8_t *deferredListHead;
	bool zeroedDeferredCount;
	uint32_t listState;
	bool callGetDrawStateIndex;
	uint32_t drawStateIndexBefore;
	uint32_t drawStateIndexAfter;
	bool callLoadDrawState;
	uint16_t objectOffset;
	bool calledObjectPosition;
	bool calledMatrixInstall;
	uint32_t viewportMinX;
	uint32_t viewportMinY;
	uint32_t viewportMaxX;
	uint32_t viewportMaxY;
	uint32_t primaryLeft;
	uint32_t primaryTop;
	uint32_t primaryRight;
	uint32_t primaryBottom;
	uint32_t renderContextCount;
	uint16_t componentMask;
	uint32_t storedDefaultTraversalGate;
	bool calledObjectPositionForSearch;
	SlipObjectPosition objectPosition;
	bool calledRecordSearch;
	uint32_t selectedRecordAddressOr;
	const uint8_t *selectedRecord;
	bool callTrackWorldObjectSelect;
	bool noSelectedRecord;
	bool noRelatedRecords;
	uint16_t componentFlags;
	bool calledObjectDraw;
	bool calledTraversalEntry;
	SlipTrackWorldTraversalEntry traversalEntry;
	SlipTrackWorldListSetupBranch branch;
} SlipTrackWorldListSetup;

typedef enum SlipTrackWorldObjectSelectBranch {
	SLIP_TRACK_WORLD_OBJECT_SELECT_BRANCH_SELECTED,
	SLIP_TRACK_WORLD_OBJECT_SELECT_BRANCH_SKIPPED
} SlipTrackWorldObjectSelectBranch;

typedef struct SlipTrackWorldObjectSelect {
	uint16_t objectOffset;
	bool calledObjectPosition;
	const uint8_t *searchedRecord;
	bool callTrackWorldRecordSearch;
	const uint8_t *storedRecord;
	bool noSelectedRecord;
	uint32_t defaultTraversalGate;
	uint16_t firstRelatedOffset;
	uint16_t firstTwoRelatedOffsets;
	uint16_t relatedOffsets;
	bool noRelatedRecords;
	uint32_t renderContextCount;
	uint32_t primaryLeft;
	uint32_t primaryTop;
	uint32_t primaryRight;
	uint32_t primaryBottom;
	const uint8_t *selectedRecord;
	uint16_t componentOffset;
	const uint8_t *componentRecord;
	uint16_t componentFlags;
	uint16_t componentMask;
	uint32_t rangeMode;
	uint32_t rangeFlag;
	uint32_t processedComponentCount;
	SlipTrackWorldObjectSelectBranch branch;
} SlipTrackWorldObjectSelect;

typedef enum SlipTrackWorldComponentBoundsBranch {
	SLIP_TRACK_WORLD_COMPONENT_BOUNDS_BRANCH_NO_CHILD_LIST,
	SLIP_TRACK_WORLD_COMPONENT_BOUNDS_BRANCH_X_BELOW,
	SLIP_TRACK_WORLD_COMPONENT_BOUNDS_BRANCH_X_ABOVE,
	SLIP_TRACK_WORLD_COMPONENT_BOUNDS_BRANCH_Y_BELOW,
	SLIP_TRACK_WORLD_COMPONENT_BOUNDS_BRANCH_Y_ABOVE,
	SLIP_TRACK_WORLD_COMPONENT_BOUNDS_BRANCH_Z_BELOW,
	SLIP_TRACK_WORLD_COMPONENT_BOUNDS_BRANCH_Z_ABOVE,
	SLIP_TRACK_WORLD_COMPONENT_BOUNDS_BRANCH_CHILD_LIST
} SlipTrackWorldComponentBoundsBranch;

typedef struct SlipTrackWorldComponentBounds {
	uint16_t componentOffsetWord;
	size_t componentOffset;
	uint16_t childListOffset;
	int32_t localX;
	int32_t localY;
	int32_t localZ;
	int32_t minX;
	int32_t maxX;
	int32_t minY;
	int32_t maxY;
	int32_t minZ;
	int32_t maxZ;
	uint32_t storedLocalX;
	uint32_t storedLocalY;
	uint32_t storedLocalZ;
	uint16_t childListCount;
	size_t firstChildRecordOffset;
	bool rejected;
	SlipTrackWorldComponentBoundsBranch branch;
} SlipTrackWorldComponentBounds;

typedef struct SlipTrackWorldPointLookup {
	uint16_t pointListOffset;
	uint16_t pointCount;
	uint16_t pointIndex;
	uint16_t pointByteOffset;
	bool carry;
	uint32_t pointXOrInput;
	uint32_t pointYOrInput;
	uint32_t pointZOrCountMergedWithInput;
	bool returned;
} SlipTrackWorldPointLookup;

typedef struct SlipTrackWorldRangePlane {
	SlipView3DVec32 origin;
	SlipView3DVec32 normal;
	bool returned;
} SlipTrackWorldRangePlane;

typedef enum SlipTrackWorldPreFrameScaleBranch {
	SLIP_TRACK_WORLD_PRE_FRAME_SCALE_BRANCH_QUOTIENT,
	SLIP_TRACK_WORLD_PRE_FRAME_SCALE_BRANCH_DOT_PRODUCT
} SlipTrackWorldPreFrameScaleBranch;

typedef struct SlipTrackWorldPreFrameScale {
	SlipView3DVec32 input;
	SlipView3DVec32 storedInput;
	SlipView3DVec32 nodeDelta;
	SlipView3DVec32 rangeDelta;
	uint32_t dotRounded;
	uint32_t storedDot;
	uint32_t shiftedLow;
	uint32_t shiftedHigh;
	uint32_t divisor;
	bool dividendHighAboveDivisor;
	bool negativeQuotient;
	uint32_t scale;
	SlipView3DVec32 scaled;
	SlipView3DVec32 output;
	SlipTrackWorldPreFrameScaleBranch branch;
	bool returned;
} SlipTrackWorldPreFrameScale;

typedef struct SlipTrackWorldTrackSlotPlaneDistance {
	SlipView3DVec32 trackRecordOrigin;
	uint16_t componentListOffset;
	SlipView3DDotProductQ14 facingDot;
	int16_t facingDotLowWord;
	SlipTrackWorldPointLookup point;
	SlipTrackWorldRangePlane rangePlane;
	SlipView3DVec32 queryDelta;
	uint64_t dotBits;
	uint32_t planeDistance;
	bool rejected;
	bool returned;
} SlipTrackWorldTrackSlotPlaneDistance;

typedef struct SlipTrackWorldCollisionQuery {
	int32_t surfaceOffset;
	int32_t bestDistance;
	uint32_t hitFractionDivisor;
	SlipView3DVec32 hitPoint;
	int32_t hitFraction;
	int16_t directionX;
	int16_t directionY;
	int16_t directionZ;
	int16_t hitNormalX;
	int16_t hitNormalY;
	int16_t hitNormalZ;
	uint16_t hitPrimitiveValue;
} SlipTrackWorldCollisionQuery;

typedef struct SlipTrackWorldSegmentCollision {
	uint16_t hitPrimitive;
	int16_t hitNormalX;
	int16_t hitNormalY;
	int16_t hitNormalZ;
	SlipView3DVec32 outputPosition;
	bool transitionBlocked;
	bool returned;
} SlipTrackWorldSegmentCollision;

typedef struct SlipTrackWorldSideTestVisit {
	uint16_t remainingCount;
	uint16_t firstIndex;
	SlipTrackWorldPointLookup firstPoint;
	uint16_t secondIndex;
	SlipTrackWorldPointLookup secondPoint;
	SlipView3DVec32 edgeDelta;
	SlipView3DNormalizeVector3D edgeNormalize;
	SlipView3DCrossProduct cross;
	SlipView3DVec32 negatedCross;
	SlipView3DVec32 candidateDelta;
	SlipView3DDotProduct32 dot;
	bool negativeSideDot;
} SlipTrackWorldSideTestVisit;

typedef struct SlipTrackWorldSideTest {
	SlipView3DVec32 candidate;
	SlipView3DVec32 normal;
	uint16_t vertexCount;
	uint16_t visitsStored;
	bool hitVisitCapacity;
	bool outside;
	bool returned;
} SlipTrackWorldSideTest;

typedef enum SlipTrackWorldComponentChildRangeBranch {
	SLIP_TRACK_WORLD_COMPONENT_CHILD_RANGE_BRANCH_ALL_PASSED,
	SLIP_TRACK_WORLD_COMPONENT_CHILD_RANGE_BRANCH_DEPTH_REJECT
} SlipTrackWorldComponentChildRangeBranch;

typedef struct SlipTrackWorldComponentChildRangeVisit {
	uint16_t recordIndex;
	size_t childRecordOffset;
	uint8_t recordFlags;
	bool skipRangeTest;
	uint16_t pointIndex;
	bool callTrackWorldPointLookup;
	SlipTrackWorldPointLookup pointLookup;
	uint16_t normalX;
	uint16_t normalY;
	uint16_t normalZ;
	bool callTrackWorldStoreRangePlane;
	SlipTrackWorldRangePlane rangePlane;
	SlipView3DVec32 rangeDelta;
	uint32_t rangeDotRounded;
	bool depthRejected;
	uint16_t strideWord;
	size_t nextChildRecordOffset;
} SlipTrackWorldComponentChildRangeVisit;

typedef struct SlipTrackWorldComponentChildRangeScan {
	uint16_t childRecordCount;
	size_t firstChildRecordOffset;
	uint16_t visitsStored;
	bool hitVisitCapacity;
	bool carryOut;
	SlipTrackWorldComponentChildRangeBranch branch;
} SlipTrackWorldComponentChildRangeScan;

typedef struct SlipTrackWorldRecordComponentTest {
	uint32_t recordAddress;
	size_t recordOffset;
	uint16_t componentOffset;
	int32_t localX;
	int32_t localY;
	int32_t localZ;
	bool callTrackWorldComponentBoundsGate;
	SlipTrackWorldComponentBounds bounds;
	bool callTrackWorldComponentChildRangeScan;
	SlipTrackWorldComponentChildRangeScan childRangeScan;
	bool carryOut;
} SlipTrackWorldRecordComponentTest;

typedef enum SlipTrackWorldCellRecordBoundsScanBranch {
	SLIP_TRACK_WORLD_CELL_RECORD_BOUNDS_SCAN_BRANCH_NO_CELL,
	SLIP_TRACK_WORLD_CELL_RECORD_BOUNDS_SCAN_BRANCH_NO_LIST,
	SLIP_TRACK_WORLD_CELL_RECORD_BOUNDS_SCAN_BRANCH_EXHAUSTED,
	SLIP_TRACK_WORLD_CELL_RECORD_BOUNDS_SCAN_BRANCH_COMPONENT_PASS
} SlipTrackWorldCellRecordBoundsScanBranch;

typedef struct SlipTrackWorldCellRecordBoundsVisit {
	uint16_t recordIndex;
	size_t recordOffset;
	uint32_t recordAddress;
	uint16_t componentOffset;
	int32_t localX;
	int32_t localY;
	int32_t localZ;
	uint16_t orChildOffsets;
	bool callTrackWorldComponentBoundsGate;
	SlipTrackWorldRecordComponentTest componentTest;
	bool componentPassed;
} SlipTrackWorldCellRecordBoundsVisit;

typedef struct SlipTrackWorldCellRecordBoundsScan {
	uint32_t cellDescriptorAddress;
	size_t cellDescriptorOffset;
	uint16_t recordListOffset;
	uint16_t recordCount;
	size_t firstRecordOffset;
	uint16_t visitsStored;
	bool hitVisitCapacity;
	size_t selectedRecordOffset;
	uint32_t selectedRecordAddress;
	SlipTrackWorldCellRecordBoundsScanBranch branch;
} SlipTrackWorldCellRecordBoundsScan;

typedef struct SlipTrackWorldObjectDraw {
	const uint8_t *listCursor;
	uint32_t unlimitedMaximumDepth;
	bool calledSetUnlimitedMaximumDepth;
	bool callTrackWorldObjectListEntry;
	bool callTrackWorldLoadClipRegisters;
	uint32_t savedClipMinX;
	uint32_t savedClipMinY;
	uint32_t savedClipMaxX;
	uint32_t savedClipMaxY;
	uint32_t viewportMinX;
	uint32_t viewportMinY;
	uint32_t viewportMaxX;
	uint32_t viewportMaxY;
	bool calledSetViewportClip;
	SlipTrackWorldTraversalCallback savedTraversalCallback;
	SlipTrackWorldRecordCallback savedRecordCallback;
	SlipTrackWorldTraversalCallback installedTraversalCallback;
	SlipTrackWorldRecordCallback installedRecordCallback;
	uint32_t componentMask;
	uint32_t defaultTraversalGate;
	bool callTrackWorldTraversalEntry;
	SlipTrackWorldTraversalEntry traversalEntry;
	SlipTrackWorldRecordCallback restoredRecordCallback;
	SlipTrackWorldTraversalCallback restoredTraversalCallback;
	uint32_t restoredMaximumDepth;
	bool calledRestoreMaximumDepth;
	uint32_t restoredClipMaxY;
	uint32_t restoredClipMaxX;
	uint32_t restoredClipMinY;
	uint32_t restoredClipMinX;
	bool calledRestoreClipBounds;
	bool callGetDrawStateIndex;
	uint32_t drawStateIndexBefore;
	uint32_t drawStateIndexAfter;
	bool callLoadDrawState;
	bool returned;
} SlipTrackWorldObjectDraw;

typedef struct SlipTrackWorldStateSave {
	bool callDraw3DLoadClipAndCenter;
	uint32_t clipMinX;
	uint32_t clipMinY;
	uint32_t clipMaxX;
	uint32_t clipMaxY;
	uint32_t primaryLeft;
	uint32_t primaryTop;
	uint32_t primaryRight;
	uint32_t primaryBottom;
	uint32_t viewportMinX;
	uint32_t viewportMinY;
	uint32_t viewportMaxX;
	uint32_t viewportMaxY;
	uint32_t renderContextCount;
	bool returned;
} SlipTrackWorldStateSave;

typedef struct SlipTrackVisibilityEntry {
	SlipView3DVec32 viewPosition;
	uint32_t callbackFlag;
	uint32_t recordAddress;
	uint32_t minX, maxX, minY, maxY;
	uint32_t useClipBounds;
	uint32_t resetMaximumDepth;
} SlipTrackVisibilityEntry;

enum {
	SLIP_TRACK_VISIBILITY_ENTRY_BYTES = sizeof(SlipTrackVisibilityEntry),
	SLIP_TRACK_VISIBILITY_POSITION_END = offsetof(SlipTrackVisibilityEntry, callbackFlag),
	SLIP_TRACK_VISIBILITY_BOUNDS_END = offsetof(SlipTrackVisibilityEntry, maxY) + sizeof(uint32_t),
	SLIP_TRACK_VISIBILITY_CLIP_BOUNDS_END = offsetof(SlipTrackVisibilityEntry, useClipBounds) + sizeof(uint32_t),
	SLIP_TRACK_DEFERRED_LIST_HEADER_BYTES = sizeof(uint32_t),
	SLIP_TRACK_DEFERRED_REFERENCE_BYTES = sizeof(uint32_t),
	SLIP_TRACK_DEFERRED_REFERENCE_SHIFT = 2,
	SLIP_TRACK_VISIBILITY_LIST_HEADER_BYTES = sizeof(uint32_t),
	SLIP_TRACK_VISIBILITY_ENTRY_CAPACITY =
	    (SLIP_TRACK_WORLD_OBJECT_LIST_BYTES - SLIP_TRACK_VISIBILITY_LIST_HEADER_BYTES) /
	    SLIP_TRACK_VISIBILITY_ENTRY_BYTES
};

typedef char SlipTrackVisibilityEntrySize[sizeof(SlipTrackVisibilityEntry) == 0x2c ? 1 : -1];

typedef enum SlipTrackWorldObjectListEntryBranch {
	SLIP_TRACK_WORLD_OBJECT_LIST_ENTRY_BRANCH_READY,
	SLIP_TRACK_WORLD_OBJECT_LIST_ENTRY_BRANCH_CAPACITY_REACHED
} SlipTrackWorldObjectListEntryBranch;

typedef struct SlipTrackWorldObjectListEntryResult {
	uint32_t counter;
	bool savedObjectPointer;
	const uint8_t *currentObject;
	uint32_t currentObjectAddress;
	uint32_t count;
	bool full;
	uint32_t scanIndex;
	bool foundExisting;
	uint32_t countAfter;
	uint32_t entryIndex;
	uint8_t *entry;
	uint32_t entryAddress;
	uint32_t listCursorAddressBefore;
	uint32_t listCursorAddressAfter;
	uint32_t advance;
	const uint8_t *entryObject;
	uint32_t entryObjectAddress;
	uint32_t callbackFlag;
	uint32_t resetMaximumDepth;
	uint32_t defaultTraversalGate;
	uint32_t rangeFlag;
	bool callTrackWorldLoadClipRegisters;
	uint32_t useFullObjectViewport;
	bool useState;
	uint32_t clipMinX;
	uint32_t clipMaxX;
	uint32_t clipMinY;
	uint32_t clipMaxY;
	uint32_t useClipBounds;
	SlipTrackWorldObjectListEntryBranch branch;
} SlipTrackWorldObjectListEntryResult;

typedef struct SlipTrackWorldObjectTransform {
	uint32_t objectWorldX;
	uint32_t objectWorldY;
	uint32_t objectWorldZ;
	uint32_t storedObjectWorldX;
	uint32_t storedObjectWorldY;
	uint32_t storedObjectWorldZ;
	uint32_t cameraRelativeX;
	uint32_t cameraRelativeY;
	uint32_t cameraRelativeZ;
	bool savedObjectPointer;
	uint32_t transformMatrixToken;
	bool calledTransformPoint;
	uint32_t viewX;
	uint32_t viewY;
	uint32_t viewZ;
	uint32_t storedViewX;
	uint32_t storedViewY;
	uint32_t storedViewZ;
	uint32_t entryViewX;
	uint32_t entryViewY;
	uint32_t entryViewZ;
	uint8_t *entry;
} SlipTrackWorldObjectTransform;

typedef enum SlipTrackWorldComponentListBranch {
	SLIP_TRACK_WORLD_COMPONENT_LIST_BRANCH_READY,
	SLIP_TRACK_WORLD_COMPONENT_LIST_BRANCH_EMPTY
} SlipTrackWorldComponentListBranch;

typedef struct SlipTrackWorldComponentList {
	const uint8_t *currentObject;
	uint16_t componentOffset;
	const uint8_t *componentRecord;
	const uint8_t *storedComponentRecord;
	uint16_t childOffset;
	bool childZero;
	const uint8_t *childList;
	bool callTrackWorldChildListDispatch;
	uint16_t listOffset;
	const uint8_t *primitiveList;
	bool savedPrimitiveListPointer;
	SlipTrackWorldComponentListBranch branch;
} SlipTrackWorldComponentList;

typedef struct SlipTrackWorldPrimitiveBoundsCall {
	bool planeRejected;
	bool primitiveRejected;
	uint32_t minX;
	uint32_t minY;
	uint32_t maxX;
	uint32_t maxY;
} SlipTrackWorldPrimitiveBoundsCall;

typedef struct SlipTrackWorldPrimitiveBoundsVisit {
	const uint8_t *primitiveRecord;
	uint16_t remainingCountBefore;
	uint8_t recordFlags;
	uint8_t boundsBit;
	bool skippedBounds;
	uint16_t descriptorBeforeMask;
	uint16_t vertexCount;
	const uint8_t *vertexIndexList;
	uint16_t pointIndex;
	uint16_t normalX;
	uint16_t normalY;
	uint16_t normalZ;
	bool calledPlaneClassify;
	bool planeRejected;
	bool callDraw3DPrimitivePath;
	bool primitiveRejected;
	uint32_t minX;
	uint32_t minY;
	uint32_t maxX;
	uint32_t maxY;
	uint32_t renderContextCount;
	uint32_t primaryLeft;
	uint32_t primaryTop;
	uint32_t primaryRight;
	uint32_t primaryBottom;
	uint16_t descriptor;
	bool highBit;
	uint32_t advance;
	size_t recordOffsetAfterAdvance;
	uint16_t remainingCountAfter;
} SlipTrackWorldPrimitiveBoundsVisit;

typedef struct SlipTrackWorldPrimitiveBounds {
	uint16_t count;
	const uint8_t *firstRecord;
	size_t finalRecordOffset;
	uint32_t renderContextCount;
	uint32_t primaryLeft;
	uint32_t primaryTop;
	uint32_t primaryRight;
	uint32_t primaryBottom;
	bool restoredPrimitiveListPointer;
} SlipTrackWorldPrimitiveBounds;

typedef struct SlipTrackWorldPrimitiveBoundsEvaluatedVisit {
	const uint8_t *primitiveRecord;
	uint8_t recordFlags;
	uint8_t boundsBit;
	bool calledPlaneClassify;
	SlipTrackWorldPlaneClassify plane;
	bool callDraw3DPrimitivePath;
	SlipDraw3DPrimitivePath primitive;
	uint16_t descriptor;
	uint32_t advance;
	size_t recordOffsetAfterAdvance;
} SlipTrackWorldPrimitiveBoundsEvaluatedVisit;

typedef struct SlipTrackWorldPrimitiveBoundsEvaluatedExecuteVisit {
	const uint8_t *primitiveRecord;
	uint8_t recordFlags;
	uint8_t boundsBit;
	bool calledPlaneClassify;
	SlipTrackWorldPlaneClassify plane;
	bool callDraw3DPrimitivePath;
	SlipDraw3DPrimitivePathExecute primitive;
	uint16_t descriptor;
	uint32_t advance;
	size_t recordOffsetAfterAdvance;
} SlipTrackWorldPrimitiveBoundsEvaluatedExecuteVisit;

typedef struct SlipTrackWorldPrimitiveBoundsEvaluated {
	SlipTrackWorldPrimitiveBounds bounds;
	size_t evaluatedVisitCount;
} SlipTrackWorldPrimitiveBoundsEvaluated;

typedef struct SlipTrackWorldPrimitiveBoundsEvaluatedExecute {
	SlipTrackWorldPrimitiveBounds bounds;
	size_t evaluatedVisitCount;
} SlipTrackWorldPrimitiveBoundsEvaluatedExecute;

typedef enum SlipTrackWorldPrimitivePreGateBranch {
	SLIP_TRACK_WORLD_PRIMITIVE_PRE_GATE_BRANCH_SKIP,
	SLIP_TRACK_WORLD_PRIMITIVE_PRE_GATE_BRANCH_DRAW,
	SLIP_TRACK_WORLD_PRIMITIVE_PRE_GATE_BRANCH_RANGE_REJECT
} SlipTrackWorldPrimitivePreGateBranch;

typedef struct SlipTrackWorldPrimitivePreGate {
	uint8_t recordFlags;
	uint8_t drawableBit;
	uint32_t rangeMode;
	bool storeRangeFlagZero;
	uint32_t rangeFlag;
	const uint8_t *componentList;
	uint16_t pointIndex;
	bool callTrackWorldPointLookup;
	SlipTrackWorldPointLookup pointLookup;
	SlipView3DVec32 pointAfterObjectOffset;
	uint16_t normalX;
	uint16_t normalY;
	uint16_t normalZ;
	bool savedPrimitivePointer;
	bool callTrackWorldStoreRangePlane;
	SlipTrackWorldRangePlane rangePlane;
	SlipView3DVec32 transformedPoint;
	SlipView3DVec32 rangeDelta;
	uint32_t rangeDotRounded;
	bool greaterThanUpper;
	bool lessThanLower;
	bool storeRangeFlagMinusOne;
	SlipTrackWorldPrimitivePreGateBranch branch;
} SlipTrackWorldPrimitivePreGate;

typedef enum SlipTrackWorldPrimitiveDrawGateBranch {
	SLIP_TRACK_WORLD_PRIMITIVE_DRAW_GATE_BRANCH_DRAW,
	SLIP_TRACK_WORLD_PRIMITIVE_DRAW_GATE_BRANCH_SKIP
} SlipTrackWorldPrimitiveDrawGateBranch;

typedef struct SlipTrackWorldPrimitiveDrawGateCall {
	bool planeRejected;
	bool polygonRejected;
} SlipTrackWorldPrimitiveDrawGateCall;

typedef struct SlipTrackWorldPrimitiveDrawGate {
	const uint8_t *vertexIndexList;
	uint16_t pointIndex;
	uint16_t normalX;
	uint16_t normalY;
	uint16_t normalZ;
	bool calledPlaneClassify;
	bool planeRejected;
	uint16_t descriptor;
	bool calledPolygonStatus;
	bool polygonRejected;
	uint32_t objectListCountToken;
	uint32_t objectListCount;
	bool full;
	SlipTrackWorldPrimitiveDrawGateBranch branch;
} SlipTrackWorldPrimitiveDrawGate;

typedef struct SlipTrackWorldPrimitiveDrawGateEvaluated {
	SlipTrackWorldPrimitiveDrawGate gate;
	SlipTrackWorldPlaneClassify plane;
	SlipDraw3DPolygonStatus polygonStatus;
} SlipTrackWorldPrimitiveDrawGateEvaluated;

typedef enum SlipTrackWorldPrimitiveRelatedScanBranch {
	SLIP_TRACK_WORLD_PRIMITIVE_RELATED_SCAN_BRANCH_MATCHED,
	SLIP_TRACK_WORLD_PRIMITIVE_RELATED_SCAN_BRANCH_UNMATCHED
} SlipTrackWorldPrimitiveRelatedScanBranch;

typedef struct SlipTrackWorldPrimitiveRelatedScanCall {
	uint32_t clipMinX;
	uint32_t clipMinY;
	uint32_t clipMaxX;
	uint32_t clipMaxY;
} SlipTrackWorldPrimitiveRelatedScanCall;

typedef struct SlipTrackWorldLoadClipRegisters {
	uint32_t clipMinX;
	uint32_t clipMinY;
	uint32_t clipMaxX;
	uint32_t clipMaxY;
	bool returned;
} SlipTrackWorldLoadClipRegisters;

typedef struct SlipTrackWorldPrimitiveRelatedScanVisit {
	const uint8_t *relatedRecord;
	uint16_t componentOffset;
	uint32_t componentAddress;
	bool matchesComponent;
	uint16_t objectOffset;
	uint32_t objectAddress;
	bool matchesCurrentObject;
	bool enterObjectListScan;
	uint32_t objectListCount;
	uint32_t scannedObjectEntries;
	bool foundObjectEntry;
	uint32_t foundObjectEntryIndex;
	bool skippedRelatedRecord;
} SlipTrackWorldPrimitiveRelatedScanVisit;

typedef struct SlipTrackWorldPrimitiveRelatedScan {
	bool savedComponentPointer;
	bool savedPrimitivePointer;
	bool callTrackWorldLoadClipRegisters;
	uint32_t clipMinX;
	uint32_t clipMinY;
	uint32_t clipMaxX;
	uint32_t clipMaxY;
	uint32_t savedRangeMinX;
	uint32_t savedRangeMinY;
	uint32_t savedRangeMaxX;
	uint32_t savedRangeMaxY;
	const uint8_t *componentRecord;
	const uint8_t *firstRelatedRecord;
	uint32_t relatedRecordCount;
	uint32_t visitCount;
	uint8_t *matchedEntry;
	bool savedObjectAddress;
	bool savedMatchedPrimitivePointer;
	uint32_t entryMinX;
	uint32_t entryMinY;
	uint32_t entryMaxX;
	uint32_t entryMaxY;
	bool calledRestoreClipBounds;
	bool calledRestoreVertexBufferCursor;
	const uint8_t *dispatchComponent;
	bool callTrackWorldChildListDispatch;
	SlipTrackWorldPrimitiveRelatedScanBranch branch;
} SlipTrackWorldPrimitiveRelatedScan;

typedef struct SlipTrackWorldPrimitiveRelatedScanExecute {
	SlipTrackWorldLoadClipRegisters load;
	SlipTrackWorldPrimitiveRelatedScan scan;
} SlipTrackWorldPrimitiveRelatedScanExecute;

typedef enum SlipTrackWorldPrimitiveRangeStateBranch {
	SLIP_TRACK_WORLD_PRIMITIVE_RANGE_STATE_BRANCH_RESTORED,
	SLIP_TRACK_WORLD_PRIMITIVE_RANGE_STATE_BRANCH_CONTINUE
} SlipTrackWorldPrimitiveRangeStateBranch;

typedef struct SlipTrackWorldPrimitiveRangeStateCall {
	bool primitiveRejected;
	uint32_t minX;
	uint32_t minY;
	uint32_t maxX;
	uint32_t maxY;
} SlipTrackWorldPrimitiveRangeStateCall;

typedef struct SlipTrackWorldPrimitiveRangeState {
	const uint8_t *storedPrimitiveRecord;
	uint32_t rangeMode;
	uint32_t rangeFlag;
	uint32_t restoredPrimitiveToken;
	bool savedPrimitiveInRangeMode;
	bool savedPrimitiveInOtherMode;
	const uint8_t *primitiveRecord;
	uint16_t descriptor;
	uint16_t vertexCount;
	const uint8_t *vertexIndexList;
	bool callDraw3DPrimitivePath;
	bool primitiveRejected;
	uint32_t minX;
	uint32_t minY;
	uint32_t maxX;
	uint32_t maxY;
	uint32_t rangeMinX;
	uint32_t rangeMinY;
	uint32_t rangeMaxX;
	uint32_t rangeMaxY;
	bool calledRestoreClipBounds;
	bool calledRestoreVertexBufferCursor;
	SlipTrackWorldPrimitiveRangeStateBranch branch;
} SlipTrackWorldPrimitiveRangeState;

typedef struct SlipTrackWorldPrimitiveRangeStateEvaluated {
	SlipTrackWorldPrimitiveRangeState range;
	SlipDraw3DPrimitivePath primitive;
} SlipTrackWorldPrimitiveRangeStateEvaluated;

typedef struct SlipTrackWorldPrimitiveRangeStateEvaluatedExecute {
	SlipTrackWorldPrimitiveRangeState range;
	SlipDraw3DPrimitivePathExecute primitive;
} SlipTrackWorldPrimitiveRangeStateEvaluatedExecute;

typedef struct SlipTrackWorldPrimitiveNestedObjectCall {
	uint32_t savedComponentViewX;
	uint32_t savedComponentViewY;
	uint32_t savedComponentViewZ;
	uint32_t savedObjectWorldX;
	uint32_t savedObjectWorldY;
	uint32_t savedObjectWorldZ;
	uint32_t savedComponentRecord;
	uint32_t savedCurrentComponentToken;
	uint32_t savedCurrentObjectAddress;
	uint32_t savedRangeFlag;
	uint32_t renderContextCount;
	uint32_t storedRenderContextCount;
	uint32_t componentRecord;
	uint32_t storedComponentRecord;
	bool callTrackWorldObjectListEntry;
	uint32_t restoredRangeFlag;
	uint32_t restoredCurrentObjectAddress;
	uint32_t restoredCurrentComponentToken;
	uint32_t restoredComponentRecord;
	uint32_t restoredObjectWorldZ;
	uint32_t restoredObjectWorldY;
	uint32_t restoredObjectWorldX;
	uint32_t restoredComponentViewZ;
	uint32_t restoredComponentViewY;
	uint32_t restoredComponentViewX;
	uint32_t dispatchComponentToken;
	bool callTrackWorldChildListDispatch;
} SlipTrackWorldPrimitiveNestedObjectCall;

typedef struct SlipTrackWorldPrimitiveDrawEpilogue {
	uint32_t restoredRangeMaxY;
	uint32_t restoredRangeMaxX;
	uint32_t restoredRangeMinY;
	uint32_t restoredRangeMinX;
	uint32_t restoredClipMaxY;
	uint32_t restoredClipMaxX;
	uint32_t restoredClipMinY;
	uint32_t restoredClipMinX;
	bool calledRestoreClipBounds;
	uint32_t restoredPrimitiveToken;
	uint32_t restoredComponentToken;
	bool continuedPrimitiveAdvance;
} SlipTrackWorldPrimitiveDrawEpilogue;

typedef enum SlipTrackWorldPrimitiveDrawAdvanceBranch {
	SLIP_TRACK_WORLD_PRIMITIVE_DRAW_ADVANCE_BRANCH_CONTINUE,
	SLIP_TRACK_WORLD_PRIMITIVE_DRAW_ADVANCE_BRANCH_FINISHED
} SlipTrackWorldPrimitiveDrawAdvanceBranch;

typedef struct SlipTrackWorldPrimitiveDrawAdvance {
	uint16_t remainingCountBefore;
	bool savedDescriptorAccumulator;
	uint16_t descriptor;
	bool highBit;
	bool savedStrideAccumulator;
	uint32_t vertexSourceStride;
	uint32_t advance;
	size_t recordOffsetAfterAdvance;
	bool restoredStrideAccumulator;
	bool restoredDescriptorAccumulator;
	uint16_t remainingCountAfter;
	SlipTrackWorldPrimitiveDrawAdvanceBranch branch;
} SlipTrackWorldPrimitiveDrawAdvance;

typedef struct SlipTrackWorldPrimitiveOuterTail {
	bool calledRestoreVertexBufferCursor;
	uint32_t restoredPrimitiveToken;
	uint32_t rangeModeBefore;
	uint32_t rangeModeAfter;
	bool returned;
} SlipTrackWorldPrimitiveOuterTail;

typedef struct SlipTrackWorldPrimitiveOuterTailExecute {
	SlipDraw3DRestoreVertexBufferCursor restore;
	SlipTrackWorldPrimitiveOuterTail tail;
} SlipTrackWorldPrimitiveOuterTailExecute;

typedef struct SlipTrackWorldChildListDispatch {
	uint16_t childOffset;
	const uint8_t *childList;
	uint16_t vertexCount;
	bool savedComponentPointer;
	const uint8_t *componentBefore;
	const uint8_t *vertexSource;
	uint32_t vertexSourceStride;
	bool callBuildVertexRecords;
	const uint8_t *restoredComponent;
	bool returned;
} SlipTrackWorldChildListDispatch;

typedef struct SlipTrackWorldChildListDispatchExecute {
	SlipTrackWorldChildListDispatch dispatch;
	bool callBuildVertexRecords;
	bool returned;
} SlipTrackWorldChildListDispatchExecute;

typedef enum SlipTrackWorldDeferredListSetupBranch {
	SLIP_TRACK_WORLD_DEFERRED_LIST_SETUP_BRANCH_READY,
	SLIP_TRACK_WORLD_DEFERRED_LIST_SETUP_BRANCH_SKIPPED
} SlipTrackWorldDeferredListSetupBranch;

typedef struct SlipTrackWorldDeferredListSetup {
	uint32_t deferredListState;
	bool disabled;
	uint32_t clipMinX;
	uint32_t clipMinY;
	uint32_t clipMaxX;
	uint32_t clipMaxY;
	bool callStoreClipBounds;
	const uint8_t *deferredList;
	uint32_t entryCount;
	bool zeroCount;
	const uint8_t *firstCursor;
	bool returned;
	SlipTrackWorldDeferredListSetupBranch branch;
} SlipTrackWorldDeferredListSetup;

typedef enum SlipTrackWorldDeferredItemPrologueBranch {
	SLIP_TRACK_WORLD_DEFERRED_ITEM_PROLOGUE_BRANCH_DIRECT,
	SLIP_TRACK_WORLD_DEFERRED_ITEM_PROLOGUE_BRANCH_CALLBACK
} SlipTrackWorldDeferredItemPrologueBranch;

typedef struct SlipTrackWorldDeferredItemPrologue {
	uint32_t savedRemainingCount;
	const uint8_t *savedCursor;
	uint32_t entryAddress;
	const uint8_t *entry;
	uint32_t recordAddress;
	uint32_t resetMaximumDepth;
	uint32_t restoredMaximumDepth;
	bool callDraw3DSetMaximumDepth;
	uint32_t useClipBounds;
	bool useEntryTransform;
	uint32_t clipMinX;
	uint32_t clipMinY;
	uint32_t clipMaxX;
	uint32_t clipMaxY;
	bool callStoreClipBounds;
	uint32_t viewX;
	uint32_t viewY;
	uint32_t viewZ;
	uint32_t callbackFlag;
	bool callTestRecordVisibility;
	SlipTrackWorldDeferredItemPrologueBranch branch;
} SlipTrackWorldDeferredItemPrologue;

typedef enum SlipTrackWorldDeferredItemDirectBranch {
	SLIP_TRACK_WORLD_DEFERRED_ITEM_DIRECT_BRANCH_CONTINUE,
	SLIP_TRACK_WORLD_DEFERRED_ITEM_DIRECT_BRANCH_FINISHED
} SlipTrackWorldDeferredItemDirectBranch;

typedef struct SlipTrackWorldDeferredItemDirect {
	uint32_t viewX;
	uint32_t viewY;
	uint32_t viewZ;
	uint16_t recordDepthFadeThreshold;
	uint32_t disabledFadeStart;
	bool callDisableDepthFade;
	uint16_t componentOffset;
	uint32_t componentAddress;
	uint16_t componentDrawMask;
	uint16_t enabledComponentDrawMask;
	bool callTrackWorldComponentSetup;
	SlipTrackWorldComponentSetup componentSetup;
	bool childDrawStateLoaded;
	bool callTrackWorldComponentTail;
	SlipTrackWorldComponentTail componentTail;
	size_t componentTailVisitCount;
	bool vertexRecordsBuilt;
	SlipDraw3DBuildVertexRecords buildVertexRecords;
	bool callTrackWorldPrimitiveWalker;
	uint16_t primitiveWalkerChildOffset;
	bool primitiveWalkerDirectCallbackBranch;
	bool callTrackWorldDirectCallbackLoop;
	uint16_t directCallbackChildOffset;
	uint16_t directCallbackCount;
	size_t directCallbackFirstRecordOffset;
	bool directCallbackLoopExecuted;
	size_t directCallbackLoopVisitCount;
	bool vertexBufferRestored;
	bool parentDrawStateLoaded;
	uint32_t restoredMaximumDepth;
	bool callDraw3DSetMaximumDepth;
	uint32_t restoredFadeStart;
	uint32_t restoredFadeEnd;
	uint32_t restoredFadeColour;
	bool callRestoreDepthFade;
	const uint8_t *restoredCursor;
	uint32_t restoredRemainingCount;
	const uint8_t *nextCursor;
	uint32_t remainingCount;
	bool returned;
	SlipTrackWorldDeferredItemDirectBranch branch;
} SlipTrackWorldDeferredItemDirect;

typedef struct SlipTrackWorldDeferredItemTail {
	uint32_t restoredMaximumDepth;
	bool callDraw3DSetMaximumDepth;
	uint32_t restoredFadeStart;
	uint32_t restoredFadeEnd;
	uint32_t restoredFadeColour;
	bool callRestoreDepthFade;
	const uint8_t *restoredCursor;
	uint32_t restoredRemainingCount;
	const uint8_t *nextCursor;
	uint32_t remainingCount;
	bool returned;
	SlipTrackWorldDeferredItemDirectBranch branch;
} SlipTrackWorldDeferredItemTail;

typedef struct SlipTrackWorldDosAddressMap {
	uint32_t dosAddress;
	const uint8_t *host;
	size_t bytes;
} SlipTrackWorldDosAddressMap;

typedef struct SlipTrackWorldDeferredListDirectExecuteVisit {
	uint32_t entryAddress;
	uint32_t recordAddress;
	SlipTrackWorldDeferredItemPrologue prologue;
	bool callTestRecordVisibility;
	SlipTrackWorldRecordVisibility visibility;
	bool callPrepareRecordTransform;
	SlipTrackWorldRecordTransformSetup transformSetup;
	bool callTransformRecordMatrix;
	SlipTrackWorldRecordMatrixTransform matrixTransform;
	bool callBuildRecordFacingMatrix;
	SlipTrackWorldRecordFacingTransform facingTransform;
	bool callTransformRecordCenter;
	SlipTrackWorldRecordScaledCenter scaledCenter;
	bool callCullRecordSphere;
	SlipTrackWorldRecordSphereCull sphereCull;
	bool callTrackWorldUpdateDrawFlags;
	SlipTrackWorldDrawFlags drawFlags;
	bool callDispatchRecordDraw;
	SlipTrackWorldRecordDrawDispatch drawDispatch;

	bool callDrawShape;
	bool callRestoreRecordDrawState;
	SlipTrackWorldRecordDrawRestore drawRestore;
	SlipTrackWorldDeferredItemTail tail;
	bool callTrackWorldDeferredItemDirect;
	SlipTrackWorldDeferredItemDirect direct;
} SlipTrackWorldDeferredListDirectExecuteVisit;

typedef enum SlipTrackWorldDeferredListDirectExecuteBranch {
	SLIP_TRACK_WORLD_DEFERRED_LIST_DIRECT_EXECUTE_BRANCH_DISABLED,
	SLIP_TRACK_WORLD_DEFERRED_LIST_DIRECT_EXECUTE_BRANCH_EMPTY,
	SLIP_TRACK_WORLD_DEFERRED_LIST_DIRECT_EXECUTE_BRANCH_RETURN,
	SLIP_TRACK_WORLD_DEFERRED_LIST_DIRECT_EXECUTE_BRANCH_SPHERE_CULL_REQUIRED,
	SLIP_TRACK_WORLD_DEFERRED_LIST_DIRECT_EXECUTE_BRANCH_SHAPE_DRAW_REQUIRED
} SlipTrackWorldDeferredListDirectExecuteBranch;

typedef struct SlipTrackWorldDeferredListDirectExecute {
	SlipTrackWorldDeferredListSetup setup;
	size_t visitCount;
	bool returned;
	bool stoppedAtSphereCull;
	bool stoppedAtShapeDraw;
	SlipTrackWorldDeferredListDirectExecuteBranch branch;
} SlipTrackWorldDeferredListDirectExecute;

typedef struct SlipTrackWorldDeferredMembership {
	bool savedRegisters;
	const uint8_t *deferredList;
	uint32_t entryCount;
	bool zeroCount;
	uint32_t recordPointerToFind;
	const uint8_t *scanEntries;
	uint32_t scannedDwords;
	bool recordFound;
	bool clearsCarry;
	bool setsCarry;
	bool restoredRegisters;
	bool returned;
} SlipTrackWorldDeferredMembership;

typedef enum SlipTrackWorldDeferredCallbackGateBranch {
	SLIP_TRACK_WORLD_DEFERRED_CALLBACK_GATE_BRANCH_NEW_ENTRY,
	SLIP_TRACK_WORLD_DEFERRED_CALLBACK_GATE_BRANCH_EXISTING_DIRECT,
	SLIP_TRACK_WORLD_DEFERRED_CALLBACK_GATE_BRANCH_EXISTING_CONTINUATION,
	SLIP_TRACK_WORLD_DEFERRED_CALLBACK_GATE_BRANCH_SKIPPED
} SlipTrackWorldDeferredCallbackGateBranch;

typedef struct SlipTrackWorldDeferredCallbackGate {
	uint16_t recordCoordinateOffset;
	bool callDecodeRecordPosition;
	SlipView3DVec32 viewPosition;
	const uint8_t *objectList;
	uint32_t entryCount;
	bool zeroCount;
	const uint8_t *firstEntry;
	uint32_t scannedEntries;
	bool recordFound;
	const uint8_t *matchedEntry;
	uint32_t matchedDeferredEntryActive;
	uint32_t unmatchedDeferredEntryActive;
	uint32_t defaultTraversalGate;
	uint16_t specialModeBits;
	uint16_t componentOffset;
	uint32_t componentAddress;
	uint16_t componentSpecialModeBits;
	uint32_t renderContextCount;
	SlipTrackWorldDeferredCallbackGateBranch branch;
} SlipTrackWorldDeferredCallbackGate;

typedef enum SlipTrackWorldDeferredContinuationGateBranch {
	SLIP_TRACK_WORLD_DEFERRED_CONTINUATION_GATE_BRANCH_CULL,
	SLIP_TRACK_WORLD_DEFERRED_CONTINUATION_GATE_BRANCH_SKIPPED
} SlipTrackWorldDeferredContinuationGateBranch;

typedef struct SlipTrackWorldDeferredContinuationGate {
	uint16_t drawMode;
	bool isDirectOnlyMode;
	const uint8_t *objectList;
	uint32_t objectListCount;
	bool objectListFull;
	uint32_t clipMinX;
	uint32_t clipMinY;
	uint32_t clipMaxX;
	uint32_t clipMaxY;
	bool callStoreClipBounds;
	SlipTrackWorldDeferredContinuationGateBranch branch;
} SlipTrackWorldDeferredContinuationGate;

typedef enum SlipTrackWorldDeferredCullGateBranch {
	SLIP_TRACK_WORLD_DEFERRED_CULL_GATE_BRANCH_VISIBLE,
	SLIP_TRACK_WORLD_DEFERRED_CULL_GATE_BRANCH_REJECTED
} SlipTrackWorldDeferredCullGateBranch;

typedef struct SlipTrackWorldDeferredCullGate {
	uint32_t cullMaximumDepth;
	bool callSetCullMaximumDepth;
	const uint8_t *savedEntry;
	uint16_t componentOffset;
	uint32_t componentAddress;
	const uint8_t *component;
	bool callTrackWorldCullBounds;
	bool trackWorldCullBoundsCarry;
	const uint8_t *restoredEntry;
	uint32_t restoredMaximumDepth;
	bool savedCullFlags;
	bool callRestoreMaximumDepth;
	bool restoredCullFlags;
	uint32_t clipMinX;
	uint32_t clipMinY;
	uint32_t clipMaxX;
	uint32_t clipMaxY;
	bool callStoreClipBounds;
	bool returned;
	SlipTrackWorldDeferredCullGateBranch branch;
} SlipTrackWorldDeferredCullGate;

typedef struct SlipTrackWorldDeferredEntryWrite {
	uint32_t viewX;
	uint32_t viewY;
	uint32_t viewZ;
	uint32_t storedViewX;
	uint32_t storedViewY;
	uint32_t storedViewZ;
	uint32_t storedRecordAddress;
	uint32_t storedCallbackFlag;
	uint32_t clipMinX;
	uint32_t clipMinY;
	uint32_t clipMaxX;
	uint32_t clipMaxY;
	uint32_t storedClipMinX;
	uint32_t storedClipMinY;
	uint32_t storedClipMaxX;
	uint32_t storedClipMaxY;
	uint32_t storedUseClipBounds;
	uint32_t storedResetMaximumDepth;
	uint32_t objectListCursorBefore;
	uint32_t objectListCursorAfter;
	const uint8_t *objectList;
	uint32_t objectListCountBefore;
	uint32_t objectListCountAfter;
	bool continuesToAppend;
} SlipTrackWorldDeferredEntryWrite;

typedef struct SlipTrackWorldDeferredAppendTail {
	uint8_t *deferredList;
	uint32_t deferredCountBefore;
	uint32_t deferredCountAfter;
	uint32_t deferredEntryByteOffset;
	uint32_t storedEntryAddress;
	uint32_t recordReferenceAddress;
	uint32_t storedRecordAddress;
	bool returned;
} SlipTrackWorldDeferredAppendTail;

typedef enum SlipTrackWorldDeferredExistingEntryBranch {
	SLIP_TRACK_WORLD_DEFERRED_EXISTING_ENTRY_BRANCH_UNCHANGED,
	SLIP_TRACK_WORLD_DEFERRED_EXISTING_ENTRY_BRANCH_UNCLIPPED
} SlipTrackWorldDeferredExistingEntryBranch;

typedef struct SlipTrackWorldDeferredExistingEntry {
	bool enter;
	uint32_t storedDeferredEntryActive;
	const uint8_t *objectList;
	uint32_t objectListCount;
	uint32_t storedObjectListCount;
	uint32_t entryUseClipBounds;
	uint32_t renderContextCount;
	uint32_t cullClipMinX;
	uint32_t cullClipMinY;
	uint32_t cullClipMaxX;
	uint32_t cullClipMaxY;
	bool callStoreCullClipBounds;
	uint8_t *savedEntry;
	uint16_t componentOffset;
	uint32_t componentAddress;
	const uint8_t *component;
	bool callTrackWorldCullBounds;
	bool trackWorldCullBoundsCarry;
	uint8_t *restoredEntry;
	bool savedCullFlags;
	uint32_t restoredClipMinX;
	uint32_t restoredClipMinY;
	uint32_t restoredClipMaxX;
	uint32_t restoredClipMaxY;
	bool callRestoreClipBounds;
	bool restoredCullFlags;
	uint32_t storedUseClipBounds;
	SlipTrackWorldDeferredExistingEntryBranch branch;
} SlipTrackWorldDeferredExistingEntry;

typedef enum SlipTrackWorldDeferredCallbackHeaderBranch {
	SLIP_TRACK_WORLD_DEFERRED_CALLBACK_HEADER_BRANCH_CULL,
	SLIP_TRACK_WORLD_DEFERRED_CALLBACK_HEADER_BRANCH_SKIPPED
} SlipTrackWorldDeferredCallbackHeaderBranch;

typedef struct SlipTrackWorldDeferredCallbackHeader {
	uint32_t deferredEntryActive;
	uint32_t renderContextCount;
	uint32_t viewX;
	uint32_t viewY;
	uint32_t viewZ;
	const uint8_t *objectList;
	uint32_t objectListCount;
	bool objectListFull;
	uint32_t clipMinX;
	uint32_t clipMinY;
	uint32_t clipMaxX;
	uint32_t clipMaxY;
	bool callStoreClipBounds;
	SlipTrackWorldDeferredCallbackHeaderBranch branch;
} SlipTrackWorldDeferredCallbackHeader;

typedef enum SlipTrackWorldDeferredCallbackCullBranch {
	SLIP_TRACK_WORLD_DEFERRED_CALLBACK_CULL_BRANCH_VISIBLE,
	SLIP_TRACK_WORLD_DEFERRED_CALLBACK_CULL_BRANCH_REJECTED
} SlipTrackWorldDeferredCallbackCullBranch;

typedef struct SlipTrackWorldDeferredCallbackCull {
	uint32_t cullMaximumDepth;
	bool callSetCullMaximumDepth;
	uint32_t viewX;
	uint32_t viewY;
	uint32_t viewZ;
	uint32_t recordIndirectCullParameter;
	bool callTrackWorldIndirectCull;
	bool trackWorldIndirectCullCarry;
	uint32_t restoredMaximumDepth;
	bool savedCullFlags;
	bool callRestoreMaximumDepth;
	bool restoredCullFlags;
	uint32_t clipMinX;
	uint32_t clipMinY;
	uint32_t clipMaxX;
	uint32_t clipMaxY;
	bool callStoreClipBounds;
	bool returned;
	SlipTrackWorldDeferredCallbackCullBranch branch;
} SlipTrackWorldDeferredCallbackCull;

typedef struct SlipTrackWorldDeferredCallbackWrite {
	uint32_t entryAddress;
	uint8_t *entry;
	uint32_t viewX;
	uint32_t viewY;
	uint32_t viewZ;
	uint32_t storedViewX;
	uint32_t storedViewY;
	uint32_t storedViewZ;
	uint32_t storedRecordAddress;
	uint32_t storedCallbackFlag;
	uint32_t clipMinX;
	uint32_t clipMinY;
	uint32_t clipMaxX;
	uint32_t clipMaxY;
	uint32_t storedClipMinX;
	uint32_t storedClipMinY;
	uint32_t storedClipMaxX;
	uint32_t storedClipMaxY;
	uint32_t storedUseClipBounds;
	uint32_t storedResetMaximumDepth;
	uint8_t *objectList;
	uint32_t objectListCountBefore;
	uint32_t objectListCountAfter;
	uint32_t objectListCursorBefore;
	uint32_t objectListCursorAfter;
	uint8_t *deferredList;
	uint32_t deferredCountBefore;
	uint32_t deferredCountAfter;
	uint32_t deferredEntryByteOffset;
	uint32_t storedEntryAddress;
	uint32_t recordReferenceAddress;
	uint32_t storedRecordReference;
	bool returned;
} SlipTrackWorldDeferredCallbackWrite;

typedef enum SlipTrackWorldTableLookupBranch {
	SLIP_TRACK_WORLD_TABLE_LOOKUP_BRANCH_VALID,
	SLIP_TRACK_WORLD_TABLE_LOOKUP_BRANCH_ZERO
} SlipTrackWorldTableLookupBranch;

typedef struct SlipTrackWorldTableLookup {
	uint16_t cellX;
	uint16_t cellY;
	uint16_t cellZ;
	bool xOutsideTable;
	bool yOutsideTable;
	bool zOutsideTable;
	uint16_t xIndex;
	uint16_t yStride;
	uint16_t yIndex;
	uint16_t xyIndex;
	uint16_t zStride;
	uint16_t zIndex;
	uint16_t cellIndex;
	uint16_t cellByteOffsetWord;
	uint32_t cellByteOffset;
	const uint8_t *table;
	uint32_t recordAddress;
	SlipTrackWorldTableLookupBranch branch;
} SlipTrackWorldTableLookup;

typedef struct SlipTrackWorldOrientedComponentTest {
	int32_t distance;
	uint32_t faceAddress;
	bool carryOut;
} SlipTrackWorldOrientedComponentTest;

typedef struct SlipTrackWorldOrientedRecordSearch {
	int32_t distance;
	uint32_t faceAddress;
	uint32_t recordAddress;
} SlipTrackWorldOrientedRecordSearch;

typedef struct SlipTrackWorldPositiveRecordSearch {
	int32_t distance;
	uint32_t faceAddress;
	uint32_t recordAddress;
} SlipTrackWorldPositiveRecordSearch;

typedef enum SlipTrackWorldRecordSearchBranch {
	SLIP_TRACK_WORLD_RECORD_SEARCH_BRANCH_EXISTING_RECORD,
	SLIP_TRACK_WORLD_RECORD_SEARCH_BRANCH_LINK_04,
	SLIP_TRACK_WORLD_RECORD_SEARCH_BRANCH_LINK_08,
	SLIP_TRACK_WORLD_RECORD_SEARCH_BRANCH_LINK_0C,
	SLIP_TRACK_WORLD_RECORD_SEARCH_BRANCH_CELL_RECORD,
	SLIP_TRACK_WORLD_RECORD_SEARCH_BRANCH_ZERO
} SlipTrackWorldRecordSearchBranch;

typedef struct SlipTrackWorldRecordSearch {
	uint32_t searchX;
	uint32_t searchY;
	uint32_t searchZ;
	uint32_t initialRecordAddress;
	uint32_t storedInitialRecordAddress;
	uint32_t currentRecordAddress;
	SlipTrackWorldRecordComponentTest currentRecordTest;
	uint16_t link04Offset;
	uint32_t link04RecordAddress;
	SlipTrackWorldRecordComponentTest link04Test;
	uint16_t link08Offset;
	uint32_t link08RecordAddress;
	SlipTrackWorldRecordComponentTest link08Test;
	uint16_t link0cOffset;
	uint32_t link0cRecordAddress;
	SlipTrackWorldRecordComponentTest link0cTest;
	int32_t cellX;
	int32_t cellY;
	int32_t cellZ;
	bool callTrackWorldTableLookup;
	SlipTrackWorldTableLookup tableLookup;
	bool callTrackWorldCellRecordBoundsScan;
	SlipTrackWorldCellRecordBoundsScan cellRecordScan;
	uint32_t selectedRecordAddress;
	bool carryOut;
	SlipTrackWorldRecordSearchBranch branch;
} SlipTrackWorldRecordSearch;

typedef struct SlipTrackWorldTableStore {
	uint32_t tableValue;
	uint32_t initialCellIndex;
	uint16_t yStride;
	uint16_t yIndex;
	uint16_t xyIndex;
	uint16_t zStride;
	uint16_t zIndex;
	uint16_t cellIndexWord;
	uint32_t cellIndex;
	uint32_t cellByteOffsetFull;
	uint32_t cellByteOffset;
	uint8_t *table;
	uint32_t storedTableValue;
	bool returned;
} SlipTrackWorldTableStore;

typedef struct SlipTrackWorldRecordVector {
	uint32_t savedParameter;
	uint32_t coordinateOffset;
	const uint8_t *coordinateData;
	uint16_t encodedX;
	uint16_t xDividendHigh;
	uint16_t xDivisor;
	uint16_t xQuotient;
	uint32_t signedX;
	uint32_t scaledX;
	uint32_t savedScaledX;
	uint16_t encodedY;
	uint16_t yDividendHigh;
	uint16_t yDivisor;
	uint16_t yQuotient;
	uint16_t yAfterBias;
	uint16_t yAfterDecrement;
	uint32_t signedY;
	uint32_t scaledY;
	uint32_t savedScaledY;
	uint16_t encodedZ;
	uint16_t zDividendHigh;
	uint16_t zDivisor;
	uint16_t zQuotient;
	uint16_t zAfterBias;
	uint32_t signedZ;
	uint32_t scaledZ;
	uint32_t coordinateZ;
	uint32_t coordinateY;
	uint32_t coordinateX;
	uint32_t restoredParameter;
	bool returned;
} SlipTrackWorldRecordVector;

typedef struct SlipTrackWorldCellTableVisit {
	const uint8_t *record;
	uint32_t recordOffsetFromTrackBase;
	uint16_t pushedLoopCount;
	uint16_t tableValueOffsetField;
	bool skippedByZeroTableValue;
	uint16_t coordinateOffsetField;
	SlipTrackWorldRecordVector decodedCoordinate;
	uint32_t cellX;
	uint32_t cellY;
	uint32_t cellZ;
	uint16_t tableValueOffset;
	uint32_t tableValueAddress;
	SlipTrackWorldTableStore tableStore;
} SlipTrackWorldCellTableVisit;

typedef enum SlipTrackWorldCellTableBuildBranch {
	SLIP_TRACK_WORLD_CELL_TABLE_BUILD_BRANCH_EMPTY,
	SLIP_TRACK_WORLD_CELL_TABLE_BUILD_BRANCH_LIST
} SlipTrackWorldCellTableBuildBranch;

typedef struct SlipTrackWorldCellTableBuild {
	uint8_t *table;
	uint32_t clearWordCount;
	uint32_t clearValue;
	const uint8_t *trackBase;
	uint16_t recordListOffset;
	const uint8_t *recordList;
	uint16_t recordCount;
	uint16_t visitsStored;
	SlipTrackWorldCellTableBuildBranch branch;
	bool returned;
} SlipTrackWorldCellTableBuild;

typedef struct SlipRaceTrackBackgroundState {
	bool savedCallerState;
	uint16_t cameraObjectOffset;
	bool callObjectPosition;
	bool callObjectMatrix;
	uint32_t cameraHeight;
	uint32_t skyHeight;
	uint32_t backgroundColour;
	uint32_t stripCurvature;
	uint16_t skyMaterial;
	uint8_t fixedStripCount;
	uint8_t materialStripCount;
	uint16_t groundMaterial;
	bool callDraw3DBackgroundDispatch;
	uint32_t savedBackgroundColour;
	bool callConfigCloudsEnabled;
	uint32_t cloudSetting;
	bool callCloudHook;
	bool ret;
} SlipRaceTrackBackgroundState;

typedef struct SlipRaceTrackFrameCallbackExecute {
	SlipRaceTrackBackgroundState frame;
	bool callDraw3DBackgroundDispatch;
	SlipDraw3DBackgroundDispatchSetupExecute dispatch;
	uint32_t savedBackgroundColour;
	bool callReadDetailLevel;
	uint32_t detailLevel;
	bool callConfigCloudsEnabled;
	bool callCloudHook;
	bool callLoadClipAndCenter;
	bool callFillClipRect;
	bool ret;
} SlipRaceTrackFrameCallbackExecute;

typedef void (*SlipRaceTrackCloudHook)(void *userData);

typedef struct SlipRaceTrackFrameCallbackExecuteArgs {
	SlipRaceTrackCloudHook cloudHook;
	void *cloudHookUserData;
	const SlipView3DMaths *maths;
	const SlipDraw3DProjectState *projectState;
	SlipDraw3DRecordPool *pool;
	uint32_t cameraHeight;
	uint16_t skyMaterial;
	uint16_t groundMaterial;
	uint32_t cloudSetting;
	uint32_t detailLevel;
	uint8_t *materialStripTable;
	size_t materialStripTableBytes;
	uint8_t *fixedStripTable;
	size_t fixedStripTableBytes;
	const uint8_t *materialTable;
	size_t materialTableBytes;
	const SlipView3DMatrix *viewMatrix;
	uint32_t projectionScale;
	uint32_t cachedProjectionScale;
	uint16_t projectionRevision;
	uint8_t cachedFixedStripCount;
	uint8_t cachedMaterialEndValue;
	uint16_t cachedStripCurvature;
	uint32_t viewportX;
	uint32_t viewportY;
	uint32_t detailScale;
	const SlipDraw3DStateRecord *stateRecord;
	uint32_t fadeStart;
	uint32_t fadeEnd;
	uint32_t fadeRange;
	uint32_t ambientLight;
	uint32_t fadeColour;
	uint32_t limitEnabled;
	uint32_t limitStart;
	uint32_t limitEnd;
	uint32_t renderFlags;
	uint32_t materialFlagsWithPreservedHighWord;
	int32_t minX;
	int32_t maxX;
	int32_t minY;
	int32_t maxY;
	int depthClipCarry;
	int screenClipCarry;
	SlipDraw3DReturnActiveVisit *returnVisits;
	size_t returnVisitCapacity;
	SlipDraw3DPointPointerRingVisit *pointRingVisits;
	size_t pointRingVisitCapacity;
	SlipDraw3DStripDispatchVisit *stripVisits;
	size_t stripVisitCapacity;
	SlipDraw3DBackgroundStripTableVisitFixed *fixedStripVisits;
	size_t fixedStripVisitCapacity;
	SlipDraw3DBackgroundStripTableVisitMaterial *materialStripVisits;
	size_t materialStripVisitCapacity;
} SlipRaceTrackFrameCallbackExecuteArgs;

typedef bool (*SlipRaceTrackFrameCallback)(const SlipRaceTrackFrameCallbackExecuteArgs *args,
                                           SlipRaceTrackFrameCallbackExecute *result);

typedef enum SlipTrackWorldFrameDispatchBranch {
	SLIP_TRACK_WORLD_FRAME_DISPATCH_BRANCH_NO_CONTEXT,
	SLIP_TRACK_WORLD_FRAME_DISPATCH_BRANCH_SPECIAL_MODE_10,
	SLIP_TRACK_WORLD_FRAME_DISPATCH_BRANCH_CALLBACK_PATH
} SlipTrackWorldFrameDispatchBranch;

typedef struct SlipTrackWorldFrameDispatch {
	uint32_t renderContext;
	bool skippedByNoContext;
	uint32_t restoredClipLeft;
	uint32_t restoredClipTop;
	uint32_t restoredClipRight;
	uint32_t restoredClipBottom;
	bool callRestoreClipBounds;
	uint16_t mode;
	uint32_t fillClipLeft;
	uint32_t fillClipTop;
	uint32_t fillClipRight;
	uint32_t fillClipBottom;
	uint32_t fillColour;
	bool callFillClipRect;
	uint32_t lightDirectionX;
	uint32_t lightDirectionZ;
	uint32_t lightDirectionY;
	uint32_t directLightScaleQ14;
	bool callDraw3DSetLightVector;
	uint32_t ambientLightScaleQ14;
	bool callDraw3DSetAmbientLight;
	SlipTrackWorldTraversalCallback savedCallback;
	uint32_t specialTraversalCallbackAddress;
	uint32_t specialFadeStart;
	uint32_t specialFadeEnd;
	uint32_t specialFadeColour;
	bool callDisableDepthFade;
	uint32_t specialMaximumDepth;
	bool callSetSpecialMaximumDepth;
	bool callSpecialTraversal;
	uint32_t restoredMaximumDepth;
	bool callRestoreMaximumDepth;
	uint32_t restoredFadeStart;
	uint32_t restoredFadeEnd;
	uint32_t restoredFadeColour;
	bool callRestoreDepthFade;
	SlipTrackWorldTraversalCallback restoredCallback;
	SlipRaceTrackFrameCallback frameCallback;
	bool callCallback;
	bool callbackSupported;
	bool callbackExecuted;
	bool unsupportedCallback;
	SlipRaceTrackFrameCallbackExecute callback;
	uint32_t defaultTraversalGate;
	bool callFallback;
	SlipTrackWorldTraversalEntry traversalEntry;
	SlipTrackWorldFrameDispatchBranch branch;
	bool ret;
} SlipTrackWorldFrameDispatch;

typedef struct SlipTrackWorldClearGlobals {
	uint32_t objectCallbackCount;
	uint32_t chunkCount;
	uint32_t componentCount;
	uint32_t visibleRecordCount;
	bool ret;
} SlipTrackWorldClearGlobals;

typedef struct SlipTrackWorldSlotDrawClearVisit {
	uint16_t slotIndex;
	uint32_t clearOffset;
	uint32_t previousValue;
} SlipTrackWorldSlotDrawClearVisit;

typedef struct SlipTrackWorldSlotDrawClear {
	uint16_t slotDrawCount;
	uint32_t firstSlotOffset;
	uint32_t clearStride;
	uint16_t clearedCount;
	bool hitVisitCapacity;
	bool ret;
} SlipTrackWorldSlotDrawClear;

typedef struct SlipTrackWorldSlotDrawRingVisit {
	uint16_t linkIndex;
	uint32_t currentOffset;
	uint32_t nextOffset;
	uint32_t nextAddress;
	uint32_t currentAddress;
} SlipTrackWorldSlotDrawRingVisit;

typedef struct SlipTrackWorldSlotDrawRing {
	uint16_t slotDrawCount;
	uint32_t baseAddress;
	uint16_t linkCount;
	uint32_t finalOffset;
	uint32_t finalNextAddress;
	uint32_t basePrevAddress;
	bool hitVisitCapacity;
	bool ret;
} SlipTrackWorldSlotDrawRing;

typedef enum SlipTrackWorldSlotDrawAllocBranch {
	SLIP_TRACK_WORLD_SLOT_DRAW_ALLOC_BRANCH_NEW_RING,
	SLIP_TRACK_WORLD_SLOT_DRAW_ALLOC_BRANCH_EXISTING_RING
} SlipTrackWorldSlotDrawAllocBranch;

typedef struct SlipTrackWorldSlotDrawAlloc {
	uint32_t freeListAddress;
	uint32_t freeListOffset;
	uint32_t allocatedAddress;
	uint32_t allocatedOffset;
	uint32_t nextFreeAddress;
	uint32_t nextFreeOffset;
	uint32_t slotListEntryAddress;
	uint16_t slotDrawOffsetWord;
	SlipTrackWorldSlotDrawAllocBranch branch;
	uint32_t existingRingAddress;
	uint32_t existingRingOffset;
	uint32_t existingRingNextAddress;
	uint32_t existingRingNextOffset;
	uint16_t storedSlotDrawOffset;
	uint32_t storedOwnerAddress;
	bool emptyFreeList;
	bool ret;
} SlipTrackWorldSlotDrawAlloc;

typedef struct SlipTrackWorldSlotDrawFree {
	uint32_t drawRecordAddress;
	uint32_t drawRecordOffset;
	uint32_t ownerAddress;
	uint32_t ownerOffset;
	uint32_t nextAddress;
	uint32_t nextOffset;
	uint32_t prevAddress;
	uint32_t prevOffset;
	uint32_t ownerSlotNextAddress;
	uint16_t ownerSlotOffsetWord;
	uint32_t freeListAddress;
	uint32_t freeListOffset;
	uint32_t freeNextAddress;
	uint32_t freeNextOffset;
	bool ret;
} SlipTrackWorldSlotDrawFree;

typedef struct SlipTrackWorldOwnerDrawLinksClear {
	uint16_t trackHandle;
	bool skipped;
	uint32_t firstDrawAddress;
	bool callFreeFirstDraw;
	bool clearedFirst;
	SlipTrackWorldSlotDrawFree firstFree;
	uint32_t secondDrawAddress;
	bool callFreeSecondDraw;
	bool clearedSecond;
	SlipTrackWorldSlotDrawFree secondFree;
	bool ret;
} SlipTrackWorldOwnerDrawLinksClear;

typedef struct SlipTrackDoorRecord {
	uint32_t speed;
	uint32_t direction;
	uint16_t object;
	uint16_t shapeHandle;
	uint32_t trackSlotAddress;
	uint32_t firstTrackRecord;
	uint32_t secondTrackRecord;
	SlipView3DVec32 planeOrigin;
	SlipView3DVec32 openEndpoint;
	SlipView3DVec32 closedEndpoint;
	int32_t halfWidth;
	uint32_t endpointDelay;
	uint32_t endpointDelayRemaining;
	int32_t halfHeight;
	SlipView3DMatrix matrix;
	int16_t directionX;
	int16_t directionY;
	int16_t directionZ;
} SlipTrackDoorRecord;

enum {
	SLIP_TRACK_DOOR_RECORD_BYTES = sizeof(SlipTrackDoorRecord),
	SLIP_TRACK_DOOR_TABLE_DOS_TOKEN = 0x339a6,
	SLIP_TRACK_DOOR_DRAW_DOS_TOKEN = 0x3c22a,
	SLIP_TRACK_DOOR_DRAW_REVERSE_DOS_TOKEN = 0x3c2ea
};

typedef char SlipTrackDoorSize[sizeof(SlipTrackDoorRecord) == 0x64 ? 1 : -1];
typedef char SlipTrackDoorMatrixOffset[offsetof(SlipTrackDoorRecord, matrix) == 0x4c ? 1 : -1];
typedef char SlipTrackDoorDirectionOffset[offsetof(SlipTrackDoorRecord, directionX) == 0x5e ? 1 : -1];

enum { SLIP_TRACK_DOOR_CAPACITY = 8 };

extern uint32_t SlipTrackWorld_doorsInitialized;
extern uint16_t SlipTrackWorld_doorCount;
extern SlipTrackDoorRecord SlipTrackWorld_doors[SLIP_TRACK_DOOR_CAPACITY];

extern SlipResourcePayload SlipTrackWorld_doorShapes[SLIP_TRACK_DOOR_CAPACITY];
bool SlipTrackWorld_FindDoors(uint16_t trackHandle, const uint8_t *trd, size_t trdBytes, uint32_t trdAddress,
                              const uint8_t *trc, size_t trcBytes);

typedef struct SlipTrackSectionDrawLinks {
	uint16_t projectionPointIndex;
	uint16_t componentOffset;

	struct {
		uint16_t sectionOffset;
		uint16_t primitiveOffset;
	} exits[SLIP_TRD_SECTION_EXIT_COUNT];

	uint16_t firstDrawOffset;
} SlipTrackSectionDrawLinks;

typedef char SlipTrackSectionDrawOffsetCheck[offsetof(SlipTrackSectionDrawLinks, firstDrawOffset) == 0x10 ? 1 : -1];

typedef struct SlipTrackDrawRecord {
	uint32_t nextAddress;
	uint32_t previousAddress;
	SlipView3DVec32 attachmentOrigin;
	uint32_t callbackAddress;
	uint32_t pairedDrawAddress;
	uint32_t ownerTrackRecordAddress;
	uint32_t edgeReference;
	uint32_t attachmentTransformReady;
	SlipView3DVec32 attachmentNormal;
	uint32_t objectOffset;
} SlipTrackDrawRecord;

typedef char SlipTrackDrawRecordSize[sizeof(SlipTrackDrawRecord) == 0x38u ? 1 : -1];
typedef char SlipTrackDrawRecordOwnerOffset[offsetof(SlipTrackDrawRecord, ownerTrackRecordAddress) == 0x1cu ? 1 : -1];

typedef struct SlipTrackSlotRecord {
	uint32_t cornerTrackRecords[SLIP_TRACK_BOUNDING_CORNER_COUNT];
	int32_t boundsAndCorners[SLIP_TRACK_BOUND_COORDINATE_COUNT +
	                         SLIP_TRACK_BOUNDING_CORNER_COUNT * SLIP_TRACK_CORNER_COORDINATE_COUNT];
	uint32_t doorAddress;
	uint32_t trackBranch;
	uint32_t flags;
	uint32_t nextSlotAddress;
	uint32_t previousSlotAddress;
	uint32_t boundingRadius;
	SlipView3DVec32 cachedPosition;
	SlipView3DVec32 projectedPosition;
	uint32_t firstDrawAddress;
	uint32_t secondDrawAddress;
	uint32_t currentTrackRecordAddress;
	uint32_t ownerObjectOffset;
	SlipView3DMatrix cachedObjectMatrix;
	uint16_t matrixPadding;
	uint32_t unused;
	uint32_t recoveryTurnRate;
	uint32_t recoveryInitialTrackRecord;
	int32_t recoverySpeed;
	SlipView3DVec32 recoveryTarget;
	uint16_t recoveryRollSeed;
	uint16_t recoveryPitchSeed;
	uint16_t recoveryUnusedSeed;
	uint16_t recoveryTimer;
	int16_t recoveryDirectionX;
	int16_t recoveryDirectionY;
	int16_t recoveryDirectionZ;
	uint16_t recoveryFlags;
} SlipTrackSlotRecord;

/* Field ends used when reading partial draw and slot records. Keep these
 * offsets as integer constants so DOS address additions retain their width. */
enum {
	SLIP_TRACK_DRAW_RECORD_BYTES = sizeof(SlipTrackDrawRecord),
	SLIP_TRACK_DRAW_CALLBACK_END = offsetof(SlipTrackDrawRecord, callbackAddress) + sizeof(uint32_t),
	SLIP_TRACK_DRAW_ATTACHMENT_READY_END = offsetof(SlipTrackDrawRecord, attachmentTransformReady) + sizeof(uint32_t),
	SLIP_TRACK_DRAW_LINKS_END = offsetof(SlipTrackDrawRecord, previousAddress) + sizeof(uint32_t),
	SLIP_TRACK_DRAW_OWNER_END = offsetof(SlipTrackDrawRecord, ownerTrackRecordAddress) + sizeof(uint32_t),
	SLIP_TRACK_DRAW_ATTACHMENT_READY_OFFSET = offsetof(SlipTrackDrawRecord, attachmentTransformReady),
	SLIP_TRACK_DRAW_PAIRED_ADDRESS_END = offsetof(SlipTrackDrawRecord, pairedDrawAddress) + sizeof(uint32_t),
	SLIP_TRACK_DRAW_ATTACHMENT_NORMAL_END = offsetof(SlipTrackDrawRecord, attachmentNormal) + sizeof(SlipView3DVec32),
	SLIP_TRACK_DRAW_NO_SLOT_TEST_RECORD_INDEX = 2,
	SLIP_TRACK_DRAW_NO_SLOT_TEST_END = SLIP_TRACK_DRAW_NO_SLOT_TEST_RECORD_INDEX * SLIP_TRACK_DRAW_RECORD_BYTES +
	                                   offsetof(SlipTrackDrawRecord, attachmentNormal.z) + sizeof(int32_t),
	SLIP_TRACK_SLOT_DOOR_ADDRESS_OFFSET = offsetof(SlipTrackSlotRecord, doorAddress),
	SLIP_TRACK_SLOT_DOOR_ADDRESS_END = offsetof(SlipTrackSlotRecord, doorAddress) + sizeof(uint32_t),
	SLIP_TRACK_SLOT_NEXT_ADDRESS_END = offsetof(SlipTrackSlotRecord, nextSlotAddress) + sizeof(uint32_t),
	SLIP_TRACK_SLOT_DRAW_ADDRESSES_END = offsetof(SlipTrackSlotRecord, secondDrawAddress) + sizeof(uint32_t),
	SLIP_TRACK_SLOT_CURRENT_RECORD_END = offsetof(SlipTrackSlotRecord, currentTrackRecordAddress) + sizeof(uint32_t),
	SLIP_TRACK_SLOT_LINKS_END = offsetof(SlipTrackSlotRecord, previousSlotAddress) + sizeof(uint32_t),
	SLIP_TRACK_SLOT_OWNER_END = offsetof(SlipTrackSlotRecord, ownerObjectOffset) + sizeof(uint32_t),
	SLIP_TRACK_SLOT_FLAGS_END = offsetof(SlipTrackSlotRecord, flags) + sizeof(uint32_t),
	SLIP_TRACK_SLOT_DISABLE_ACTOR_MODE = 0x08,
	SLIP_TRACK_ACTOR_MODE_DISABLED_DETAIL_MINIMUM = 3
};

extern SlipView3DVec32 SlipTrackWorld_doorPosition;
extern SlipView3DVec16 SlipTrackWorld_doorDirection;
void SlipTrackWorld_DoorDirection(const SlipTrackDoorRecord *door);
bool SlipTrackWorld_MoveDoor(SlipTrackDoorRecord *door, uint16_t objectOffset, SlipObject *objects, size_t objectBytes);
uint32_t SlipTrackWorld_DoorEvent(uint32_t eventCode, uint16_t objectOffset, uint16_t otherObject,
                                  SlipTrackDoorRecord *door, SlipObject *objects, size_t objectBytes,
                                  const SlipTrackSlotRecord *slots, size_t slotBytes, uint32_t slotAddress);

typedef char SlipTrackSlotRecordSize[sizeof(SlipTrackSlotRecord) == SLIP_TRACK_SLOT_RECORD_BYTES ? 1 : -1];
typedef char
    SlipTrackSlotRecordCurrentTrackOffset[offsetof(SlipTrackSlotRecord, currentTrackRecordAddress) == 0xd0u ? 1 : -1];
typedef char SlipTrackSlotRecordUnusedOffset[offsetof(SlipTrackSlotRecord, unused) == 0xecu ? 1 : -1];

typedef struct SlipTrackWorldSlotListSelect {
	uint32_t callerValue;
	uint32_t slotListBaseAddress;
	bool skippedNoSlotList;
	bool callObjectTrackSlot;
	uint16_t objectOffset;
	uint16_t slotOffset;
	bool zeroSlotOffset;
	uint32_t slotAddress;
	bool carry;
	uint32_t restoredCallerValue;
	bool ret;
} SlipTrackWorldSlotListSelect;

typedef struct SlipTrackWorldObjectAttachmentDrawExecution {
	SlipTrackWorldObjectAttachmentDraw block;
	SlipTrackWorldSlotListSelect select;
	uint32_t callerValue;
	uint16_t objectOffset;
	uint32_t comparisonAddress;
	uint32_t comparisonOffset;
	bool cmpUsesSlotListEntry;
	bool ret;
} SlipTrackWorldObjectAttachmentDrawExecution;

typedef struct SlipTrackWorldObjectCallbackDrawExecution {
	SlipTrackWorldObjectCallbackDraw block;
	SlipTrackWorldSlotListSelect select;
	uint32_t callerValue;
	uint16_t objectOffset;
	uint32_t comparisonAddress;
	uint32_t comparisonOffset;
	bool cmpUsesSlotListEntry;
	bool ret;
} SlipTrackWorldObjectCallbackDrawExecution;

typedef struct SlipTrackWorldInvokeDrawCallbackExecute {
	SlipTrackWorldInvokeDrawCallback block;
	SlipTrackWorldSlotListSelect select;
	uint32_t callerValue;
	uint16_t objectOffset;
	uint32_t testedAddress;
	uint32_t testedOffset;
	bool testUsesSlotListEntry;
} SlipTrackWorldInvokeDrawCallbackExecute;

typedef struct SlipTrackWorldSlotDrawInstall {
	uint16_t requestedDrawCount;
	uint16_t slotDrawCount;
	uint16_t drawCountWithSentinel;
	uint16_t recordBytes;
	uint32_t allocationBytes;
	bool callResourceAllocateAnonymous;
	uint16_t allocationHandle;
	bool callLockResource;
	uint32_t baseAddress;
	bool callTrackWorldInitSlotDrawRing;
	SlipTrackWorldSlotDrawRing ring;
	bool ret;
} SlipTrackWorldSlotDrawInstall;

typedef struct SlipTrackWorldSlotListVisit {
	uint16_t listIndex;
	uint32_t currentOffset;
	uint32_t nextOffset;
	uint32_t nextAddress;
	uint32_t currentAddress;
} SlipTrackWorldSlotListVisit;

typedef struct SlipTrackWorldSlotListInstall {
	uint16_t requestedSlotCount;
	uint32_t countPlusEight;
	uint16_t slotListCount;
	uint32_t countPlusTen;
	uint32_t recordBytes;
	uint32_t allocationBytes;
	bool callResourceAllocateAnonymous;
	uint16_t allocationHandle;
	bool callLockResource;
	uint32_t baseAddress;
	uint32_t activeListAddress;
	uint32_t freeListAddress;
	uint16_t linkCount;
	uint32_t finalOffset;
	uint32_t finalNextAddress;
	uint32_t firstPrevAddress;
	bool hitVisitCapacity;
	bool ret;
} SlipTrackWorldSlotListInstall;

typedef struct SlipTrackWorldSlotListAlloc {
	uint32_t freeListAddress;
	uint32_t freeListOffset;
	uint32_t allocatedAddress;
	uint32_t allocatedOffset;
	uint32_t nextFreeAddress;
	uint32_t nextFreeOffset;
	uint32_t activeListAddress;
	uint32_t activeListOffset;
	uint32_t activeNextAddress;
	uint32_t activeNextOffset;
	bool clearedTail;
	uint16_t zeroedDwords;
	bool carry;
	bool ret;
} SlipTrackWorldSlotListAlloc;

typedef struct SlipTrackWorldAddSlot {
	uint8_t *slot;
	uint32_t slotAddress;
	bool carryOut;
} SlipTrackWorldAddSlot;

typedef struct SlipTrackWorldStateResetVisit {
	uint32_t loopCountBefore;
	uint32_t stateIndexTo;
	bool callLoadDrawState;
	uint32_t matrixAddress;
	uint32_t originX;
	uint32_t originY;
	uint32_t originZ;
	uint32_t drawStateFlags;
	bool callStoreDrawState;
	uint32_t stateIndexAfterInc;
	uint32_t loopCountAfterDec;
} SlipTrackWorldStateResetVisit;

enum { SLIP_TRACK_STATE_RESET_COUNT = 3 };

typedef struct SlipTrackWorldStateReset {
	bool callGetDrawStateIndex;
	uint32_t initialStateIndex;
	uint32_t loopCountInitial;
	SlipTrackWorldStateResetVisit visits[SLIP_TRACK_STATE_RESET_COUNT];
	uint32_t restoredStateIndex;
	bool callRestoreDrawState;
	bool ret;
} SlipTrackWorldStateReset;

typedef struct SlipTrackWorldAxisRamp {
	uint16_t directionX;
	uint16_t directionY;
	uint16_t directionZ;
	uint16_t lastTripletIndex;
	uint16_t loopCount;
	uint32_t stepX;
	uint32_t stepY;
	uint32_t stepZ;
	size_t tripletsStored;
	bool ret;
} SlipTrackWorldAxisRamp;

typedef struct SlipTrackWorldBuildAxisRamps {
	const uint8_t *viewMatrix;
	uint8_t *rampX;
	uint8_t *rampY;
	uint8_t *rampZ;
	bool callBuildXRamp;
	SlipTrackWorldAxisRamp xRamp;
	bool callBuildYRamp;
	SlipTrackWorldAxisRamp yRamp;
	bool callBuildZRamp;
	SlipTrackWorldAxisRamp zRamp;
	bool ret;
} SlipTrackWorldBuildAxisRamps;

typedef struct SlipTrackWorldAxisTestWord {
	bool callTrackWorldClassifyAxisPlane;
	SlipTrackWorldAxisPlaneClassify classify;
	bool trackWorldClassifyAxisPlaneCarry;
	uint16_t classificationMask;
	bool ret;
} SlipTrackWorldAxisTestWord;

typedef struct SlipTrackWorldAxisTestVisit {
	uint32_t loopCountBefore;
	const uint8_t *rampTriplet;
	uint8_t *tableEntry;
	uint32_t rampX;
	uint32_t rampY;
	uint32_t rampZ;
	uint32_t positionX;
	uint32_t positionY;
	uint32_t positionZ;
	uint16_t normalX;
	uint16_t normalY;
	uint16_t normalZ;
	bool callTrackWorldAxisTestWord;
	SlipTrackWorldAxisTestWord word;
	uint16_t classificationMask;
} SlipTrackWorldAxisTestVisit;

typedef struct SlipTrackWorldBuildAxisTests {
	uint32_t mode;
	const uint8_t *viewMatrix;
	const uint8_t *rampX;
	const uint8_t *rampY;
	const uint8_t *rampZ;
	uint8_t *xTestTable;
	uint8_t *yTestTable;
	uint8_t *zTestTable;
	uint32_t addX;
	uint32_t addY;
	uint32_t addZ;
	size_t visitsStored;
	bool ret;
} SlipTrackWorldBuildAxisTests;

typedef struct SlipTrackWorldFrameCaller {
	bool savedCallerState;
	uint16_t overlayEnable;
	uint16_t actorReplayMode;
	uint16_t secondaryActorDrawParameter;
	uint16_t auxiliaryActorDrawParameter;
	bool callGetRasterClip;
	uint32_t savedClipLeft;
	uint32_t savedClipTop;
	uint32_t savedClipRight;
	uint32_t savedClipBottom;
	bool callTrackWorldLoadClipRegisters;
	bool callSetFrameRasterClip;
	bool callTrackWorldInitRefuel;
	bool callBuildReplaySources;
	uint32_t drawStateIndexForChunk;
	bool callLoadDrawStateForChunk;
	int32_t trackHeaderGate;
	bool callTrackWorldPreFrameBuild;
	uint32_t cameraDrawStateIndex;
	bool callTrackWorldPreFrameCameraPrefix;
	uint32_t restoredDrawStateIndex;
	bool callRestoreDrawStateAfterChunk;
	SlipRaceTrackFrameCallback frameCallback;
	bool callTrackWorldFrameEntry;
	uint32_t restoredClipBottom;
	uint32_t restoredClipRight;
	uint32_t restoredClipTop;
	uint32_t restoredClipLeft;
	bool callRestoreRasterClip;
	bool restoredCallerState;
	bool ret;
} SlipTrackWorldFrameCaller;

typedef enum SlipTrackWorldPreFrameCameraBranch {
	SLIP_TRACK_WORLD_PRE_FRAME_CAMERA_BRANCH_GLOBAL_DISABLED,
	SLIP_TRACK_WORLD_PRE_FRAME_CAMERA_BRANCH_MATERIAL_MISSING,
	SLIP_TRACK_WORLD_PRE_FRAME_CAMERA_BRANCH_ACTIVE_PREFIX
} SlipTrackWorldPreFrameCameraBranch;

typedef struct SlipTrackWorldPreFrameCameraPrefix {
	bool savedCallerState;
	uint32_t reflectionEnabled;
	uint32_t savedFrameCallback;
	bool callDraw3DLoadClipAndCenter;
	uint32_t savedViewportLeft;
	uint32_t savedViewportTop;
	uint32_t savedViewportRight;
	uint32_t savedViewportBottom;
	uint32_t savedViewportCenterX;
	uint32_t savedViewportCenterY;
	uint32_t materialNameAddress;
	bool callLookupMaterial;
	bool callMaterialGetFrame;
	uint16_t materialFrameOffset;
	bool callReadMaterialDimensions;
	uint32_t viewportMaxX;
	uint32_t viewportMaxY;
	int32_t viewportCenterX;
	int32_t viewportCenterY;
	bool callSetMaterialViewport;
	uint16_t materialViewportWidth;
	uint16_t viewportReadMode;
	bool callReadMaximumDepth;
	uint32_t savedDepth;
	uint32_t reflectionMaximumDepth;
	bool callDraw3DSetMaximumDepth;
	uint16_t rangePlaneObjectOffset;
	bool callReadRangePlaneObjectPosition;
	uint32_t rangeOriginXTo;
	uint32_t rangeOriginYTo;
	uint32_t rangeOriginZTo;
	uint16_t rangeNormalXTo;
	uint16_t rangeNormalYTo;
	uint16_t rangeNormalZTo;
	bool callTrackWorldStoreRangePlane;
	SlipTrackWorldRangePlane rangePlane;
	uint16_t reflectedObjectOffset;
	bool callReadReflectedObjectPosition;
	SlipView3DVec32 objectPosition;
	SlipView3DVec32 rangeDelta;
	uint32_t rangeDotRounded;
	uint32_t rangeDotNegated;
	int32_t forwardScale;
	SlipView3DVec32 scaledForward;
	bool callScaleReflectionDisplacement;
	SlipView3DVec32 adjustedObjectPositionTo;
	uint16_t positionInstallObjectOffset;
	bool callObjectSetPosition;
	bool callReadObjectWorldMatrix;
	uint32_t matrixSaveSource;
	uint32_t matrixSaveDestination;
	bool callView3DCopyMatrixWords;
	SlipTrackWorldPreFrameCameraBranch branch;
	bool restoredCallerState;
	bool ret;
} SlipTrackWorldPreFrameCameraPrefix;

typedef struct SlipTrackWorldPreFrameCameraSuffix {
	uint16_t reflectionMatrixObjectOffset;
	bool callReadReflectionObjectMatrix;
	uint16_t reflectionNormalX;
	uint16_t reflectionNormalY;
	uint16_t reflectionNormalZ;
	bool callView3DReflectMatrixRows;
	bool callEnablePostPlaneMode;
	uint16_t reflectionMatrixInstallObjectOffset;
	bool callInstallReflectionObjectMatrix;
	uint16_t reflectionCameraObjectOffset;
	bool callReadReflectionCameraMatrix;
	bool callReadReflectionCameraPosition;
	bool callSetReflectionCameraOrigin;
	uint32_t frameCallbackTo;
	bool callTrackWorldFrameEntry;
	SlipView3DVec32 restoreObjectPositionTo;
	uint16_t positionRestoreObjectOffset;
	bool callRestoreObjectPosition;
	uint32_t restoreDepthTo;
	bool callRestoreMaximumDepth;
	uint32_t matrixRestoreAddress;
	uint16_t matrixRestoreObjectOffset;
	bool callRestoreObjectMatrix;
	uint16_t restoredCameraObjectOffset;
	bool callReadRestoredCameraMatrix;
	bool callReadRestoredCameraPosition;
	bool callRestoreCameraOrigin;
	bool callDisablePostPlaneMode;
	uint32_t restoredViewportLeft;
	uint32_t restoredViewportTop;
	uint32_t restoredViewportRight;
	uint32_t restoredViewportBottom;
	uint32_t restoredViewportCenterX;
	uint32_t restoredViewportCenterY;
	bool callDraw3DSetViewport;
	bool restoredCallerState;
	bool ret;
} SlipTrackWorldPreFrameCameraSuffix;

typedef enum SlipTrackWorldCameraFrameBranch {
	SLIP_TRACK_WORLD_CAMERA_FRAME_BRANCH_MISSING_OBJECT,
	SLIP_TRACK_WORLD_CAMERA_FRAME_BRANCH_CALLED_FRAME
} SlipTrackWorldCameraFrameBranch;

typedef struct SlipTrackWorldCameraFrame {
	bool savedCallerState;
	uint16_t cameraObjectOffset;
	bool missingCameraObject;
	bool callObjectHide;
	uint32_t cameraViewportSelector;
	bool callGetCameraViewport;
	uint32_t viewportLeft;
	uint32_t viewportTop;
	uint32_t viewportRight;
	uint32_t viewportBottom;
	int32_t viewportCenterX;
	int32_t viewportCenterY;
	bool callDraw3DSetViewport;
	bool callRendererBegin;
	bool callReadProjectionScale;
	uint32_t pushedProjection;
	uint32_t projectionScaleTo;
	bool callSetProjectionScale;
	bool callReadCameraObjectPosition;
	bool callReadCameraObjectMatrix;
	uint16_t positionInstallObjectOffset;
	bool callObjectSetPosition;
	uint16_t matrixInstallObjectOffset;
	bool callObjectMatrixInstall;
	bool callRendererSetCamera;
	SlipRaceTrackFrameCallback frameCallbackFrom;
	uint16_t frameOverlayEnable;
	uint16_t frameActorReplayMode;
	uint16_t frameSecondaryActorDrawParameter;
	uint16_t frameAuxiliaryActorDrawParameter;
	bool callFrameCaller;
	SlipTrackWorldFrameCaller frameCaller;
	bool callAfterFrame;
	bool savedLabelTop;
	uint16_t weaponLabelResource;
	bool callReadWeaponLabelDimensions;
	uint32_t labelHeight;
	uint32_t labelWidth;
	uint32_t textStyle;
	uint32_t textBackgroundColour;
	bool callTextSetStyle;
	uint32_t textColour;
	bool callTextSetColor;
	uint32_t labelTop;
	bool callRacePlayerProjectileWeaponIndex;
	bool callRacePlayerBuildWeaponLabel;
	bool callDrawWeaponLabel;
	uint32_t restoredProjection;
	bool callRestoreProjectionScale;
	uint16_t shownObjectOffset;
	bool callObjectShow;
	bool clearCarry;
	bool setCarry;
	bool restoredCallerState;
	bool ret;
	SlipTrackWorldCameraFrameBranch branch;
} SlipTrackWorldCameraFrame;

typedef struct SlipTrackWorldFrameEntry {
	SlipRaceTrackFrameCallback frameCallback;
	SlipRaceTrackFrameCallback storedFrameCallback;
	bool callTrackWorldFrameDrawState;
	SlipTrackWorldFrameDrawState frameDrawState;
	bool callDraw3DLoadClipAndCenter;
	uint32_t savedClipLeft;
	uint32_t savedClipTop;
	uint32_t savedClipRight;
	uint32_t savedClipBottom;
	bool callReadMaximumDepth;
	uint32_t savedDepth;
	bool callReadDepthFade;
	uint32_t savedFadeStart;
	uint32_t savedFadeEnd;
	uint32_t savedFadeColour;
	bool callTrackWorldCameraSetup;
	bool callTrackWorldStateReset;
	SlipTrackWorldStateReset stateReset;
	bool callReadShapeFlags;
	uint16_t maskedShapeFlags;
	bool callRendererSetShapeFlags;
	bool callReadStateToken;
	uint32_t pushedStateToken;
	uint32_t temporaryStateToken;
	bool callInstallTemporaryStateToken;
	bool callTrackWorldBuildAxisRamps;
	bool callTrackWorldBuildAxisTests;
	bool callSetupObjectList;
	SlipTrackWorldListSetup listSetup;
	bool callTrackWorldClearSlotDrawLinks;
	bool callTrackWorldClearGlobals;
	SlipTrackWorldClearGlobals clear;
	bool callTrackWorldFrameDispatch;
	SlipTrackWorldFrameDispatch frameDispatch;
	bool callExecuteDeferredList;
	SlipTrackWorldDeferredListDirectExecute deferredDirect;
	uint32_t restoredClipLeft;
	uint32_t restoredClipTop;
	uint32_t restoredClipRight;
	uint32_t restoredClipBottom;
	bool callRestoreClipBounds;
	uint32_t restoredStateToken;
	bool callRestoreStateToken;
	bool callTrackWorldPostFrameOverlay;
	bool ret;
} SlipTrackWorldFrameEntry;

typedef struct SlipRaceTrackMaterialGlobals {
	uint16_t skyMaterial;
	uint16_t groundMaterial;
	uint32_t skyNameAddress;
	uint32_t groundNameAddress;
	bool callSkyLookup;
	SlipDraw3DMaterialNumber skyLookup;
	bool skyLookupCarry;
	bool storeSky;
	bool callGroundLookup;
	SlipDraw3DMaterialNumber groundLookup;
	bool groundLookupCarry;
	bool storeGround;
	bool ret;
} SlipRaceTrackMaterialGlobals;

typedef struct SlipTrackWorldCameraSetup {
	uint16_t cameraObjectOffset;
	bool callObjectPosition;
	uint32_t cameraPositionX;
	uint32_t cameraPositionY;
	uint32_t cameraPositionZ;
	uint32_t negatedPositionX;
	uint32_t negatedPositionY;
	uint32_t negatedPositionZ;
	bool callObjectMatrix;
	bool callTransformPosition;
	uint32_t viewOriginX;
	uint32_t viewOriginY;
	uint32_t viewOriginZ;
	bool callTrackWorldMatrixInstall;
	bool ret;
} SlipTrackWorldCameraSetup;

typedef struct SlipTrackWorldMatrixInstall {
	const SlipView3DMatrix *sourceMatrix;
	SlipView3DMatrix *viewMatrix;
	uint32_t matrixWordCount;
	bool copiedMatrixWords;
	bool callView3DTransposeMatrix;
	bool ret;
} SlipTrackWorldMatrixInstall;

typedef struct SlipTrackWorldCameraSetupExecute {
	uint16_t cameraObjectOffset;
	SlipObjectPosition objectPosition;
	uint32_t cameraPositionX;
	uint32_t cameraPositionY;
	uint32_t cameraPositionZ;
	uint32_t negatedPositionX;
	uint32_t negatedPositionY;
	uint32_t negatedPositionZ;
	SlipObjectMatrixCopy objectMatrixCopy;
	SlipView3DVec32 origin;
	SlipTrackWorldMatrixInstall matrixInstall;
	bool ret;
} SlipTrackWorldCameraSetupExecute;

typedef struct SlipTrackWorldOverlayLineCall {
	uint32_t startX;
	uint32_t startY;
	uint32_t endX;
	uint32_t endY;
	bool callLineDraw;
} SlipTrackWorldOverlayLineCall;

typedef enum SlipTrackWorldPostFrameOverlayBranch {
	SLIP_TRACK_WORLD_POST_FRAME_OVERLAY_BRANCH_DISABLED,
	SLIP_TRACK_WORLD_POST_FRAME_OVERLAY_BRANCH_ENABLED_NO_LINES,
	SLIP_TRACK_WORLD_POST_FRAME_OVERLAY_BRANCH_PRIMARY_ONLY,
	SLIP_TRACK_WORLD_POST_FRAME_OVERLAY_BRANCH_PRIMARY_AND_SECONDARY
} SlipTrackWorldPostFrameOverlayBranch;

typedef struct SlipTrackWorldPostFrameOverlay {
	uint16_t overlayEnable;
	bool skippedByZeroEnable;
	uint32_t renderContextCount;
	bool drawsPrimary;
	SlipTrackWorldOverlayLineCall primaryLines[4];
	bool drawsSecondary;
	SlipTrackWorldOverlayLineCall secondaryLines[4];
	SlipTrackWorldPostFrameOverlayBranch branch;
	bool restoredCallerState;
	bool ret;
} SlipTrackWorldPostFrameOverlay;

typedef struct SlipTrackWorldScaledCallSetup {
	uint16_t lightMultiplierQ14;
	uint32_t ambientLightScaleQ14;
	uint32_t ambientLightProduct;
	uint32_t ambientLightWithPreservedHighWord;
	uint32_t ambientLightTo;
	bool callDraw3DSetAmbientLight;
	uint32_t directLightScaleQ14;
	uint32_t directLightProduct;
	uint16_t directLightScaledQ14;
	uint16_t directLightScaleTo;
	uint32_t lightDirectionX;
	uint32_t lightDirectionY;
	uint32_t lightDirectionZ;
	bool callDraw3DSetLightVector;
	bool ret;
} SlipTrackWorldScaledCallSetup;

typedef enum SlipTrackWorldRecordAdvanceBranch {
	SLIP_TRACK_WORLD_RECORD_ADVANCE_BRANCH_LOW_DESCRIPTOR,
	SLIP_TRACK_WORLD_RECORD_ADVANCE_BRANCH_HIGH_DESCRIPTOR
} SlipTrackWorldRecordAdvanceBranch;

typedef struct SlipTrackWorldRecordAdvance {
	size_t recordOffsetBefore;
	uint32_t savedCallbackValue;
	uint16_t vertexDescriptor;
	bool wideIndices;
	bool savedMultiplierState;
	uint32_t indexStride;
	uint32_t recordAdvanceBytes;
	size_t recordOffsetAfter;
	bool restoredMultiplierState;
	uint32_t restoredCallbackValue;
	SlipTrackWorldRecordAdvanceBranch branch;
} SlipTrackWorldRecordAdvance;

typedef struct SlipTrackWorldPrimitiveWalkerVisit {
	size_t recordOffset;
	uint16_t remainingPrimitiveCount;
	uint8_t flags;
	bool skipOnFlags;
	uint16_t materialIndex;
	bool materialIndexHighBitCleared;
	uint16_t vertexCountAndFlags;
	uint16_t normalX;
	uint16_t normalY;
	uint16_t normalZ;
	size_t indexStreamOffset;
	bool callPlaneVisible;
	bool specialPlaneFlag;
	bool callTrackWorldGlobalCarryGate;
	bool texturedPath;
	bool callSolidEmitWithoutMaterial;
	bool callSolidEmitWithMaterial;
	bool callPolygonStatusOptional;
	bool callPolygonStatusMaterial;
	bool callMaterialDispatchRegular;
	bool callMaterialDispatchStatus;
	SlipTrackWorldRecordAdvance advance;
	bool loop;
} SlipTrackWorldPrimitiveWalkerVisit;

typedef struct SlipTrackWorldPrimitiveWalker {
	uint16_t childOffset;
	bool childZeroBranch;
	uint16_t count;
	bool directCallbackBranch;
	size_t listOffset;
	uint16_t primitiveCount;
	size_t firstRecordOffset;
	size_t visitCount;
	bool restoredSourcePointer;
	bool ret;
} SlipTrackWorldPrimitiveWalker;

typedef struct SlipTrackWorldPrimitiveCallbackDispatch {
	uint32_t callbackValueEntry;
	uint8_t flags;
	uint32_t callbackValueWithFlags;
	bool skipOnFlags;
	uint16_t materialIndex;
	bool materialIndexHighBitCleared;
	uint16_t vertexCountAndFlags;
	uint16_t normalX;
	uint16_t normalY;
	uint16_t normalZ;
	size_t indexStreamOffset;
	uint32_t callbackValueWithNormalX;
	bool callPlaneVisible;
	bool planeVisibleCarry;
	bool specialPlaneFlag;
	bool callTrackWorldGlobalCarryGate;
	bool trackWorldGlobalCarryGateCarry;
	bool texturedPath;
	bool pushIndexStream;
	bool callMaterialFramePointer;
	bool testMaterialFrame;
	bool popIndexStream;
	uint16_t materialFrameOffset;
	bool materialFrameZeroFallback;
	bool savedCallbackValueBeforeDepth;
	bool callPerspectiveDepth;
	uint32_t perspectiveDepth;
	uint32_t farTextureDepth;
	bool farDepthFallback;
	uint32_t nearTextureDepth;
	bool depthGreaterThanNear;
	bool callRenderFlagsRead;
	uint32_t renderFlagsFrom;
	uint32_t savedRenderFlags;
	uint32_t renderFlagsTo;
	bool callRenderFlagsWrite;
	bool restoredCallbackValueBeforeEmit;
	bool callTextureRowScroll;
	bool callTexturedEmit;
	bool zeroTextureScroll;
	bool callTextureScrollReset;
	bool restoreRenderFlags;
	bool callRestoreRenderFlags;
	bool callSolidEmitWithoutMaterial;
	bool callSolidEmitWithMaterial;
	bool solidEmitCarry;
	bool callPolygonStatus;
	bool polygonStatusSign;
	uint32_t polygonStatusValue;
	bool callMaterialDispatchRegular;
	bool callMaterialDispatchStatus;
	bool ret;
	bool returnStateKnown;
	uint32_t callbackValueResult;
	bool carryOut;
} SlipTrackWorldPrimitiveCallbackDispatch;

typedef struct SlipTrackWorldDirectCallbackInput {
	uint32_t callbackValueResult;
	bool carryFromCallback;
} SlipTrackWorldDirectCallbackInput;

typedef struct SlipTrackWorldDirectCallbackEnvironment {
	const SlipTrackWorldComponentSetup *componentSetup;
	const SlipTrackWorldComponentTail *componentTail;
	const SlipTrackWorldPrimitiveWalker *primitiveWalker;
	SlipView3DVec32 transformedObjectOffset;

	bool inlineWalkMode;
} SlipTrackWorldDirectCallbackEnvironment;

typedef bool (*SlipTrackWorldDirectCallbackFunction)(const uint8_t *record, size_t recordBytesRemaining,
                                                     size_t recordOffset,
                                                     const SlipTrackWorldDirectCallbackEnvironment *environment,
                                                     uint32_t callbackValue, void *userData, uint32_t *callbackValueOut,
                                                     bool *carryFromCallback);

typedef bool (*SlipTrackWorldStoreClipBoundsFunction)(uint32_t clipMinX, uint32_t clipMinY, uint32_t clipMaxX,
                                                      uint32_t clipMaxY, void *userData);

typedef struct SlipTrackWorldDirectCallbackVisit {
	size_t recordOffset;
	uint16_t remainingBeforeCallback;
	bool savedRemainingCount;
	SlipTrackWorldPrimitiveCallback primitiveCallback;
	bool callCallback;
	bool carryFromCallback;
	bool restoredRemainingCount;
	uint16_t remainingAfterCallback;
	bool callbackTerminatedLoop;
	bool savedCallbackValue;
	SlipTrackWorldRecordAdvance advance;
	bool advanceToNextRecord;
} SlipTrackWorldDirectCallbackVisit;

typedef struct SlipTrackWorldDirectCallbackLoop {
	uint16_t childOffset;
	const uint8_t *primitiveList;
	uint16_t primitiveCount;
	const uint8_t *firstPrimitive;
	size_t visitCount;
	bool restoredSourcePointer;
	bool ret;
} SlipTrackWorldDirectCallbackLoop;

typedef enum SlipTrackWorldGlobalCarryGateBranch {
	SLIP_TRACK_WORLD_GLOBAL_CARRY_GATE_BRANCH_CLEAR,
	SLIP_TRACK_WORLD_GLOBAL_CARRY_GATE_BRANCH_SET
} SlipTrackWorldGlobalCarryGateBranch;

typedef struct SlipTrackWorldGlobalCarryGate {
	bool savedCallerState;
	uint32_t reflectionEnabled;
	bool zeroBranch;
	bool setCarry;
	bool clearCarry;
	bool carryOut;
	bool restoredCallerState;
	bool ret;
	SlipTrackWorldGlobalCarryGateBranch branch;
} SlipTrackWorldGlobalCarryGate;

typedef struct SlipTrackWorldReplayListVisit {
	size_t listOffset;
	uint16_t objectOffset;
	bool callObjectDraw;
	size_t nextListOffset;
	uint16_t remainingObjectCount;
	bool loop;
} SlipTrackWorldReplayListVisit;

typedef enum SlipTrackWorldReplayListBranch {
	SLIP_TRACK_WORLD_REPLAY_LIST_BRANCH_EMPTY,
	SLIP_TRACK_WORLD_REPLAY_LIST_BRANCH_LOOP
} SlipTrackWorldReplayListBranch;

typedef struct SlipTrackWorldReplayList {
	uint16_t count;
	bool emptyBranch;
	bool callGetDrawStateBeforeChunk;
	uint32_t drawStateIndexBeforeChunk;
	uint32_t drawStateIndexForChunk;
	bool callLoadDrawStateForChunk;
	bool savedSourcePointer;
	uint32_t listAddress;
	size_t visitCount;
	bool restoredSourcePointer;
	bool callGetDrawStateAfterChunk;
	uint32_t drawStateIndexAfterChunk;
	uint32_t restoredDrawStateIndex;
	bool callRestoreDrawStateAfterChunk;
	bool ret;
	SlipTrackWorldReplayListBranch branch;
} SlipTrackWorldReplayList;

typedef enum SlipTrackWorldOptionalRecordBranch {
	SLIP_TRACK_WORLD_OPTIONAL_RECORD_BRANCH_SKIPPED,
	SLIP_TRACK_WORLD_OPTIONAL_RECORD_BRANCH_CARRY_RETURN,
	SLIP_TRACK_WORLD_OPTIONAL_RECORD_BRANCH_REPLAY
} SlipTrackWorldOptionalRecordBranch;

typedef struct SlipTrackWorldOptionalRecord {
	uint8_t flags;
	bool optionalPlaneDisabled;
	uint16_t materialIndex;
	bool callGetMaterialFrameAddress;
	uint32_t materialFrameAddress;
	uint32_t materialFrameSource;
	bool savedRecordPointer;
	uint32_t pushedDword;
	uint16_t planeNormalX;
	uint16_t planeNormalY;
	uint16_t planeNormalZ;
	uint32_t viewMatrixAddress;
	bool callTransformPlaneNormal;
	uint32_t transformedNormalX;
	uint32_t transformedNormalY;
	uint32_t transformedNormalZ;
	uint32_t planePointDescriptor;
	uint32_t savedTransformedNormalX;
	uint32_t savedTransformedNormalY;
	uint32_t savedTransformedNormalZ;
	bool callPointViewPosition;
	uint32_t postPlaneNormalZ;
	uint32_t postPlaneNormalY;
	uint32_t postPlaneNormalX;
	bool callDraw3DCapturePostPlaneRing;
	bool draw3DCapturePostPlaneRingCarry;
	bool restoredRecordPointer;
	bool callReplayObjects;
	SlipTrackWorldReplayList replayList;
	bool callDraw3DReleasePostPlaneRing;
	bool ret;
	SlipTrackWorldOptionalRecordBranch branch;
} SlipTrackWorldOptionalRecord;

typedef struct SlipTrackWorldReplaySourceChild {
	uint16_t offset;
	bool zeroBranch;
	const uint8_t *childRecord;
	bool callScanReplaySources;
} SlipTrackWorldReplaySourceChild;

typedef struct SlipTrackWorldReplaySourceDispatch {
	bool savedSourcePointer;
	const uint8_t *initialRecord;
	bool callScanInitialRecord;
	bool savedRecordForFirstExit;
	SlipTrackWorldReplaySourceChild firstExit;
	bool restoredRecordAfterFirstExit;
	bool savedRecordForSecondExit;
	SlipTrackWorldReplaySourceChild secondExit;
	bool restoredRecordAfterSecondExit;
	bool savedRecordForThirdExit;
	SlipTrackWorldReplaySourceChild thirdExit;
	bool restoredRecordAfterThirdExit;
	bool restoredSourcePointer;
	bool ret;
} SlipTrackWorldReplaySourceDispatch;

typedef enum SlipTrackWorldReplaySourceScanVisitBranch {
	SLIP_TRACK_WORLD_REPLAY_SOURCE_SCAN_VISIT_BRANCH_ZERO_EAX_AFTER_MOV,
	SLIP_TRACK_WORLD_REPLAY_SOURCE_SCAN_VISIT_BRANCH_LIST_FULL,
	SLIP_TRACK_WORLD_REPLAY_SOURCE_SCAN_VISIT_BRANCH_DUPLICATE,
	SLIP_TRACK_WORLD_REPLAY_SOURCE_SCAN_VISIT_BRANCH_APPENDED
} SlipTrackWorldReplaySourceScanVisitBranch;

typedef struct SlipTrackWorldReplaySourceScanVisit {
	size_t nodeOffset;
	uint32_t nodeAddress;
	uint16_t objectOffset;
	bool savedRecordPointer;
	bool callReadObjectDrawCallback;
	uint32_t objectDrawCallback;
	uint32_t testedObjectDrawCallback;
	bool restoredRecordPointer;
	uint16_t countBefore;
	bool listFull;
	bool duplicate;
	uint16_t countAfter;
	size_t listStoreOffset;
	bool storedObjectOffset;
	size_t nextNodeOffset;
	uint32_t nextNodeAddress;
	bool loop;
	SlipTrackWorldReplaySourceScanVisitBranch branch;
} SlipTrackWorldReplaySourceScanVisit;

typedef enum SlipTrackWorldReplaySourceScanBranch {
	SLIP_TRACK_WORLD_REPLAY_SOURCE_SCAN_BRANCH_EMPTY,
	SLIP_TRACK_WORLD_REPLAY_SOURCE_SCAN_BRANCH_VISITED
} SlipTrackWorldReplaySourceScanBranch;

typedef struct SlipTrackWorldReplaySourceScan {
	bool savedRecordPointer;
	uint16_t objectListOffset;
	size_t firstNodeOffset;
	size_t visitCount;
	uint16_t finalCount;
	bool restoredRecordPointer;
	bool ret;
	SlipTrackWorldReplaySourceScanBranch branch;
} SlipTrackWorldReplaySourceScan;

typedef struct SlipTrackWorldMaterialStateStore {
	uint32_t planeNormalX;
	uint32_t planeNormalY;
	uint32_t planeNormalZ;
	uint32_t planeOriginX;
	uint32_t planeOriginY;
	uint32_t planeOriginZ;
	bool ret;
} SlipTrackWorldMaterialStateStore;

typedef struct SlipTrackWorldMaterialHandlerVisit {
	size_t entryOffset;
	uint16_t remainingBefore;
	uint32_t defaultShade;
	uint16_t selector;
	uint32_t selectedShade;
	bool callShadeEmit;
	size_t nextEntryOffset;
	uint16_t remainingAfter;
	bool loop;
} SlipTrackWorldMaterialHandlerVisit;

typedef enum SlipTrackWorldMaterialHandlerBranch {
	SLIP_TRACK_WORLD_MATERIAL_HANDLER_BRANCH_THRESHOLD_RET,
	SLIP_TRACK_WORLD_MATERIAL_HANDLER_BRANCH_SETUP_CARRY_RET,
	SLIP_TRACK_WORLD_MATERIAL_HANDLER_BRANCH_LOOP
} SlipTrackWorldMaterialHandlerBranch;

typedef struct SlipTrackWorldMaterialHandler {
	uint32_t perspectiveDepth;
	bool thresholdReturn;
	uint32_t positiveShadeOffset;
	bool callReadPositiveShade;
	uint16_t positiveShade;
	uint32_t negativeShadeOffset;
	bool callReadNegativeShade;
	uint16_t negativeShade;
	uint32_t polygonIndicesAddress;
	bool callBuildMaterialPolygon;
	bool materialPolygonCarry;
	uint32_t shadeEntriesAddress;
	uint16_t shadeEntryCount;
	size_t visitCount;
	bool callReleaseMaterialPolygon;
	bool ret;
	SlipTrackWorldMaterialHandlerBranch branch;
} SlipTrackWorldMaterialHandler;

bool SlipTrackWorld_BindHostBuffers(uint8_t *cellTable, size_t cellTableBytes, uint8_t *objectList,
                                    size_t objectListBytes, uint8_t *deferredList, size_t deferredListBytes,
                                    uint8_t *deferredScan, size_t deferredScanBytes, SlipTrackWorldHostBuffers *result);

void SlipTrackWorld_ClearSlotRecordLinks(uint16_t trackHandle, uint8_t *trdBase);

void SlipTrackWorld_FreeAllSlotListEntries(uint16_t trackHandle, uint8_t *slotDrawBase, size_t slotDrawBytes,
                                           uint32_t slotDrawBaseAddress, uint32_t slotDrawFreeListAddress,
                                           uint8_t *trdBase, size_t trackDataSize, uint32_t trdBaseAddress,
                                           uint8_t *slotListBase, size_t slotListBytes, uint32_t slotListBaseAddress,
                                           uint32_t slotListActiveAddress, uint32_t slotListFreeAddress);

void SlipTrackWorld_ResetSlots(uint16_t trackHandle, uint8_t *trdBase, size_t trackDataSize, uint32_t trdBaseAddress,
                               uint8_t *slotDrawBase, size_t slotDrawBytes, uint32_t slotDrawBaseAddress,
                               uint32_t slotDrawFreeListAddress, uint16_t slotDrawCount, uint8_t *slotListBase,
                               size_t slotListBytes, uint32_t slotListBaseAddress, uint32_t slotListActiveAddress,
                               uint32_t slotListFreeAddress);

bool SlipTrackWorld_BindAxisRampWorkspace(uint8_t *workspace, size_t workspaceBytes, uint16_t allocationHandle,
                                          SlipTrackWorldAxisRampWorkspace *result);

bool SlipTrackWorld_BindAxisTestWorkspace(uint8_t *workspace, size_t workspaceBytes, uint16_t allocationHandle,
                                          SlipTrackWorldAxisTestWorkspace *result);

bool SlipTrackWorld_ClearRecordClassificationCache(uint8_t *trkBase, size_t trkSize, uint16_t *clearedCount);

bool SlipTrackWorld_TraversalEntry(uint8_t *trkBase, size_t trkSize, SlipTrackWorldTraversalContext *traversalContext,
                                   SlipTrackWorldTraversalEntry *result);

SlipView3DVec32 SlipTrackWorld_SourceChunkPoint(uint16_t sourceX, uint16_t sourceY, uint16_t sourceZ,
                                                SlipView3DVec32 currentChunkOrigin);

bool SlipTrackWorld_NodeTest(const uint8_t *tableBase, size_t tableSize, uint16_t nodeWordOffset,
                             SlipTrackWorldNodeTest *result);

bool SlipTrackWorld_SumThreePoints(const uint8_t *record, size_t recordBytesRemaining, const uint8_t *pointBase,
                                   size_t pointBaseSize, SlipView3DVec32 origin, SlipView3DVec32 *result);

bool SlipTrackWorld_TransformPoint(SlipView3DVec32 input, const SlipView3DMatrix *viewMatrix, SlipView3DVec32 offset,
                                   SlipView3DVec32 *result);

bool SlipTrackWorld_ChunkSetup(const uint8_t *currentChunk, size_t chunkBytesRemaining, const uint8_t *filter,
                               uint32_t chunkCounter, SlipTrackWorldChunkSetup *result);

bool SlipTrackWorld_StoreRecordCacheResult(uint8_t *record, size_t recordBytesRemaining, uint8_t classificationMask);

bool SlipTrackWorld_ClassifyChildRecord(const uint8_t *childRecord, size_t childRecordBytesRemaining,
                                        const uint8_t *pointBase, size_t pointBaseSize, SlipView3DVec32 origin,
                                        int32_t minZ, int32_t maxZ, SlipTrackWorldProjectMask projectMask,
                                        void *userData, SlipTrackWorldClassifyChild *result);

bool SlipTrackWorld_ScanClassificationMask(uint8_t combinedMask, uint8_t classificationMask, uintptr_t scanPosition,
                                           uint32_t remainingRecordCount, SlipTrackWorldDlTest *result);

bool SlipTrackWorld_RecordScan(uint8_t *trkBase, size_t trkSize, uint32_t recordOffset,
                               const uint8_t classificationByVisit[SLIP_TRACK_WORLD_RECORD_SCAN_VISITS],
                               SlipTrackWorldRecordScan *result);

bool SlipTrackWorld_RecordScanClassified(uint8_t *trkBase, size_t trkSize, uint32_t recordOffset,
                                         const uint8_t *pointBase, size_t pointBaseSize, SlipView3DVec32 origin,
                                         int32_t minZ, int32_t maxZ, SlipTrackWorldProjectMask projectMask,
                                         void *userData, SlipTrackWorldRecordScan *result);

bool SlipTrackWorld_NodeBranch(const uint8_t *record, size_t recordBytesRemaining, const uint8_t *tableBase,
                               size_t tableSize, SlipTrackWorldNodeBranch *result);

bool SlipTrackWorld_DispatchPositiveNodeChild(const uint8_t *record, size_t recordBytesRemaining, uint32_t trkBase,
                                              SlipTrackWorldPositiveNodeChildDispatch *result);

bool SlipTrackWorld_DispatchNegativeNodeChild(const uint8_t *record, size_t recordBytesRemaining, uint32_t trkBase,
                                              SlipTrackWorldNegativeNodeChildDispatch *result);

bool SlipTrackWorld_NodeDispatch(uint8_t *trkBase, size_t trkSize, uint32_t recordOffset, uint32_t trkBasePointer,
                                 const uint8_t *tableBase, size_t tableSize, const uint8_t *pointBase,
                                 size_t pointBaseSize, SlipView3DVec32 origin, const uint8_t *chunkBase,
                                 size_t chunkBaseSize, const uint8_t *filter, uint32_t chunkCounter,
                                 SlipTrackWorldChunkProcessExecution *chunkProcessExecution,
                                 uint32_t drawStateIndexBeforeChunk, SlipTrackWorldNodeDispatch *result);

bool SlipTrackWorld_RecordStep(uint8_t *trkBase, size_t trkSize, uint32_t recordOffset, uint32_t trkBasePointer,
                               const uint8_t classificationByVisit[SLIP_TRACK_WORLD_RECORD_SCAN_VISITS],
                               const uint8_t *tableBase, size_t tableSize, const uint8_t *pointBase,
                               size_t pointBaseSize, SlipView3DVec32 origin, const uint8_t *chunkBase,
                               size_t chunkBaseSize, const uint8_t *filter, uint32_t chunkCounter,
                               SlipTrackWorldChunkProcessExecution *chunkProcessExecution,
                               uint32_t drawStateIndexBeforeChunk, SlipTrackWorldRecordStep *result);

bool SlipTrackWorld_RecordStepClassified(uint8_t *trkBase, size_t trkSize, uint32_t recordOffset,
                                         uint32_t trkBasePointer, const uint8_t *tableBase, size_t tableSize,
                                         const uint8_t *pointBase, size_t pointBaseSize, SlipView3DVec32 origin,
                                         int32_t minZ, int32_t maxZ, SlipTrackWorldProjectMask projectMask,
                                         void *userData, const uint8_t *chunkBase, size_t chunkBaseSize,
                                         const uint8_t *filter, uint32_t chunkCounter,
                                         SlipTrackWorldChunkProcessExecution *chunkProcessExecution,
                                         uint32_t drawStateIndexBeforeChunk, SlipTrackWorldRecordStep *result);

bool SlipTrackWorld_Traverse(uint8_t *trkBase, size_t trkSize, uint32_t rootRecordOffset, uint32_t trkBasePointer,
                             const uint8_t classificationByVisit[SLIP_TRACK_WORLD_RECORD_SCAN_VISITS],
                             const uint8_t *tableBase, size_t tableSize, const uint8_t *pointBase, size_t pointBaseSize,
                             SlipView3DVec32 origin, const uint8_t *chunkBase, size_t chunkBaseSize,
                             const uint8_t *filter, uint32_t chunkCounter,
                             SlipTrackWorldChunkProcessExecution *chunkProcessExecution,
                             uint32_t drawStateIndexBeforeChunk, SlipTrackWorldTraversalVisit *visits,
                             uint16_t visitCapacity, uint32_t *stackOffsets, uint16_t *stackDepths,
                             uint16_t stackCapacity, SlipTrackWorldTraversal *result);

bool SlipTrackWorld_TraverseClassified(
    uint8_t *trkBase, size_t trkSize, uint32_t rootRecordOffset, uint32_t trkBasePointer, const uint8_t *tableBase,
    size_t tableSize, const uint8_t *pointBase, size_t pointBaseSize, SlipView3DVec32 origin, int32_t minZ,
    int32_t maxZ, SlipTrackWorldProjectMask projectMask, void *userData, const uint8_t *chunkBase, size_t chunkBaseSize,
    const uint8_t *filter, uint32_t chunkCounter, SlipTrackWorldChunkProcessExecution *chunkProcessExecution,
    uint32_t drawStateIndexBeforeChunk, SlipTrackWorldTraversalVisit *visits, uint16_t visitCapacity,
    uint32_t *stackOffsets, uint16_t *stackDepths, uint16_t stackCapacity, SlipTrackWorldTraversal *result);

bool SlipTrackWorld_RecordChunk(const uint8_t *record, size_t recordBytesRemaining, const uint8_t *trkBase,
                                size_t trkSize, const uint8_t *pointBase, size_t pointBaseSize, SlipView3DVec32 origin,
                                const uint8_t *chunkBase, size_t chunkBaseSize, const uint8_t *filter,
                                uint32_t chunkCounter, SlipTrackWorldChunkProcessExecution *chunkProcessExecution,
                                SlipTrackWorldRecordChunk *result);

bool SlipTrackWorld_ChunkPointList(const uint8_t *chunk, size_t chunkBytesRemaining, const uint8_t *chunkBase,
                                   size_t chunkBaseSize, SlipTrackWorldChunkPointList *result);

bool SlipTrackWorld_ChunkBsp(const uint8_t *currentChunkAfterBsp, size_t chunkBytesRemaining, const uint8_t *chunkBase,
                             size_t chunkBaseSize, SlipTrackWorldChunkBsp *result);

bool SlipTrackWorld_ChunkProcess(const uint8_t *chunk, size_t chunkBytesRemaining, const uint8_t *chunkBase,
                                 size_t chunkBaseSize, const uint8_t *filter, uint32_t chunkCounter,
                                 SlipTrackWorldChunkProcess *result);

bool SlipTrackWorld_ChunkProcessVertexCache(
    const uint8_t *chunk, size_t chunkBytesRemaining, const uint8_t *chunkBase, size_t chunkBaseSize,
    const uint8_t *filter, uint32_t chunkCounter, SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCapacity,
    SlipTrackWorldBuildVertexRecords buildVertexRecords, void *buildVertexRecordsUserData,
    SlipTrackWorldRestoreVertexBuffer restoreVertexBuffer, void *restoreVertexBufferUserData,
    SlipTrackWorldRawBspClassify classify, SlipTrackWorldRawBspCallback callback, void *rawBspUserData,
    uint16_t rawBspDepthCapacity, SlipView3DVec32 *currentChunkOrigin,
    bool (*chunkFallback)(const uint8_t *chunk, size_t chunkBytesRemaining, void *userData),
    void *chunkFallbackUserData, SlipTrackWorldChunkProcessVertexCache *result);

bool SlipTrackWorld_TraverseRawBsp(const uint8_t *bspBase, size_t bspBytesRemaining,
                                   SlipTrackWorldRawBspClassify classify, SlipTrackWorldRawBspCallback callback,
                                   void *userData, uint16_t depthCapacity, SlipTrackWorldRawBsp *result);

bool SlipTrackWorld_ClassifyPlaneFromSource(uint32_t mode, uint16_t vertexIndex, uint16_t inputPlaneX,
                                            uint16_t inputPlaneY, uint16_t inputPlaneZ, const uint8_t *vertexCacheBase,
                                            size_t vertexCacheBytes, SlipView3DVec32 origin,
                                            SlipTrackWorldSourcePoint sourcePoint, void *userData,
                                            const SlipView3DMatrix *matrix, SlipTrackWorldPlaneClassify *result);

bool SlipTrackWorld_ClassifyAxisPlane(uint32_t mode, uint32_t pointX, uint32_t pointY, uint32_t pointZ, uint16_t axisX,
                                      uint16_t axisY, uint16_t axisZ, SlipTrackWorldAxisPlaneClassify *result);

bool SlipTrackWorld_ChunkAlternatePaths(const uint8_t *chunk, size_t chunkBytesRemaining, const uint8_t *chunkBase,
                                        size_t chunkBaseSize, bool trackWorldDeferredMembershipCarry,
                                        SlipTrackWorldChunkAlternatePaths *result);

bool SlipTrackWorld_ChunkDispatch(uint16_t recordKind, uint16_t recordOffset, const uint8_t *chunkBase,
                                  size_t chunkBaseSize, bool trackWorldDeferredMembershipCarry,
                                  SlipTrackWorldChunkDispatch *result);

bool SlipTrackWorld_RecordVisibility(const uint8_t *record, size_t recordBytesRemaining, uint32_t viewPositionX,
                                     uint32_t viewPositionY, uint32_t viewPositionZ, uint16_t mask, uint32_t mode,
                                     uint32_t minDepth, int32_t detailThreshold,
                                     SlipTrackWorldRecordVisibility *result);

bool SlipTrackWorld_RecordTransformSetup(const uint8_t *record, size_t recordBytesRemaining,
                                         uint32_t currentRecordIndex, SlipTrackWorldRecordTransformSetup *result);

bool SlipTrackWorld_RecordMatrixTransform(const uint8_t *record, size_t recordBytesRemaining,
                                          SlipView3DMatrix *worldMatrix, SlipView3DMatrix *objectViewMatrix,
                                          const SlipView3DMatrix *viewMatrix,
                                          SlipTrackWorldRecordMatrixTransform *result);

bool SlipTrackWorld_RecordFacingTransform(const uint8_t *record, size_t recordBytesRemaining, uint32_t cameraWorldX,
                                          uint32_t cameraWorldZ, SlipView3DMatrix *worldMatrix,
                                          SlipView3DMatrix *objectViewMatrix, const SlipView3DMatrix *viewMatrix,
                                          SlipTrackWorldRecordFacingTransform *result);

bool SlipTrackWorld_RecordScaledCenter(const SlipView3DMatrix *objectViewMatrix, uint32_t inputCenterY,
                                       uint32_t viewPositionX, uint32_t viewPositionY, uint32_t viewPositionZ,
                                       uint32_t restoredPositionZ, uint32_t restoredPositionYWithCenter,
                                       uint32_t restoredPositionX, SlipTrackWorldRecordScaledCenter *result);

bool SlipTrackWorld_RecordSphereCull(SlipView3DVec32 center, uint32_t cullingRadius,
                                     SlipTrackWorldSphereCull sphereCull, void *sphereCullUserData,
                                     uint32_t visibleRecordCount, uint32_t viewDepth,
                                     SlipTrackWorldRecordSphereCull *result);

bool SlipTrackWorld_RecordDrawDispatch(uint16_t mask, uint32_t renderFlagsBeforeMask, uint32_t facingModeFlag,
                                       uint32_t renderFlagsForFacing, uint32_t frameRenderFlags,
                                       SlipTrackWorldRecordDrawDispatch *result);

bool SlipTrackWorld_RecordDrawRestore(uint32_t drawStateIndex, SlipTrackWorldRecordDrawRestore *result);

SlipTrackBeamRecord *SlipTrackWorld_AllocateBeam(SlipTrackBeamState *beams);

bool SlipTrackWorld_PreFrameScale(SlipView3DVec32 input, SlipView3DVec32 nodeOrigin,
                                  SlipTrackWorldRangePlane rangePlane, uint16_t radius, uint16_t axisX, uint16_t axisY,
                                  uint16_t axisZ, SlipTrackWorldPreFrameScale *result);

void SlipTrackWorld_TrackSlotPlaneDistance(const uint8_t *trackRecord, const uint8_t *plane,
                                           const uint8_t *componentBase, size_t componentBaseBytes,
                                           SlipView3DVec32 direction, SlipView3DVec32 queryPoint,
                                           SlipTrackWorldTrackSlotPlaneDistance *result);

void SlipTrackWorld_CheckTrackRecordPrimitives(const uint8_t *trackRecord, const uint8_t *componentBase,
                                               size_t componentBaseBytes, SlipView3DVec32 queryPoint,
                                               SlipTrackWorldCollisionQuery *query);

void SlipTrackWorld_CheckSlotSamples(uint8_t *inputSlot, const SlipObject *objectTable, size_t objectTableBytes,
                                     const uint8_t *trdBase, size_t trackDataSize, uint32_t trdBaseAddress,
                                     const uint8_t *componentBase, size_t componentBaseBytes,
                                     uint32_t componentBaseAddress, const uint8_t *table, size_t tableBytes,
                                     SlipTrackWorldCollisionQuery *query);

void SlipTrackWorld_PreCollisionStep(uint16_t frameStep, uint8_t *slotListBase, uint32_t slotListBaseAddress,
                                     uint32_t slotListSentinelAddress, SlipObject *objectTable, size_t objectTableBytes,
                                     const uint8_t *trdBase, size_t trackDataSize, uint32_t trdBaseAddress,
                                     const uint8_t *componentBase, size_t componentBaseBytes,
                                     uint32_t componentBaseAddress, const uint8_t *table, size_t tableBytes);

SlipView3DVec32 SlipTrackWorld_BisectRecordBoundary(const uint8_t *trdBase, size_t trackDataSize,
                                                    uint32_t trdBaseAddress, const uint8_t *componentBase,
                                                    size_t componentBaseBytes, const uint8_t *table, size_t tableBytes,
                                                    SlipView3DVec32 first, SlipView3DVec32 second);

bool SlipTrackWorld_ClipRefuelBeam(const uint8_t *trackData, size_t trackDataSize, uint32_t trackDataAddress,
                                   const uint8_t *components, size_t componentBytes, const uint8_t *searchTable,
                                   size_t searchTableBytes, SlipView3DVec32 start, SlipView3DVec32 end,
                                   SlipView3DVec32 *clippedEnd);

void SlipTrackWorld_CheckSegmentTransition(const uint8_t *trdBase, size_t trackDataSize, uint32_t trdBaseAddress,
                                           const uint8_t *componentBase, size_t componentBaseBytes,
                                           const uint8_t *table, size_t tableBytes, SlipView3DVec32 segmentStart,
                                           SlipView3DVec32 segmentEnd, SlipTrackWorldSegmentCollision *result);

bool SlipTrackWorld_CheckLineOfSight(uint16_t firstObject, uint16_t secondObject, uint8_t *slotListBase,
                                     uint32_t slotListBaseAddress, SlipObject *objectTable, size_t objectTableBytes,
                                     const uint8_t *trdBase, size_t trackDataSize, uint32_t trdBaseAddress,
                                     const uint8_t *componentBase, size_t componentBaseBytes,
                                     uint32_t componentBaseAddress, const uint8_t *table, size_t tableBytes);

void SlipTrackWorld_PostCollisionStep(uint8_t *slotListBase, uint32_t slotListBaseAddress,
                                      uint32_t slotListSentinelAddress, SlipObject *objectTable,
                                      size_t objectTableBytes, const uint8_t *trdBase, size_t trackDataSize,
                                      uint32_t trdBaseAddress, const uint8_t *componentBase, size_t componentBaseBytes,
                                      uint32_t componentBaseAddress, const uint8_t *table, size_t tableBytes);

bool SlipTrackWorld_SideTest(const uint8_t *componentList, const uint8_t *componentBase, size_t componentBaseBytes,
                             const uint8_t *plane, size_t planeBytesRemaining, SlipView3DVec32 candidate,
                             SlipTrackWorldSideTestVisit *visits, size_t visitCapacity, SlipTrackWorldSideTest *result);

bool SlipTrackWorld_ObjectRecordLookup(uint32_t attachmentListOffset, uint32_t objectListCount,
                                       uint32_t slotDrawBaseToken, SlipTrackWorldObjectRecordLookup *result);

bool SlipTrackWorld_ObjectAttachmentDraw(const uint8_t *drawRecord, size_t recordBytesRemaining,
                                         SlipTrackWorldObjectAttachmentDraw *result);

bool SlipTrackWorld_ObjectAttachmentMatch(const uint8_t *record, size_t recordBytesRemaining,
                                          uint32_t attachmentListHeadAddress,
                                          SlipTrackWorldObjectAttachmentMatch *result);

bool SlipTrackWorld_ObjectCallbackDraw(const uint8_t *drawRecord, size_t recordBytesRemaining, uint32_t counter,
                                       SlipTrackWorldObjectCallbackDraw *result);

bool SlipTrackWorld_ObjectCallbackMatch(const uint8_t *record, size_t recordBytesRemaining,
                                        uint32_t attachmentListHeadAddress, SlipTrackWorldObjectCallbackMatch *result);

bool SlipTrackWorld_ObjectListHead(uint32_t objectListCount, const uint8_t *recordBase, size_t recordBytesRemaining,
                                   uint32_t componentRecord, SlipTrackWorldObjectListHead *result);

bool SlipTrackWorld_ObjectRelativePosition(const uint8_t *objectRecord, size_t recordBytesRemaining,
                                           uint32_t cameraWorldX, uint32_t cameraWorldY, uint32_t cameraWorldZ,
                                           SlipTrackWorldObjectRelativePosition *result);

bool SlipTrackWorld_ObjectListAdvance(uintptr_t currentRecordAddress, uint32_t remainingCountBefore,
                                      SlipTrackWorldObjectListAdvance *result);

bool SlipTrackWorld_ObjectListFinalize(uint32_t componentRecord, uint32_t currentComponentToken,
                                       SlipTrackWorldObjectListFinalize *result);

bool SlipObject_Block(uint32_t objectHandleBeforeMask, uint32_t objectTableToken,
                      SlipObjectMatrixBindingResult *result);

const SlipView3DMatrix *SlipObject_DrawMatrix(const SlipObject *objects, uint16_t object);

bool SlipObject_MatrixCopy(const SlipObject *objectTableBase, size_t objectTableSize, uint32_t objectHandleBeforeMask,
                           SlipView3DMatrix *objectTransformMatrix, SlipObjectMatrixCopy *result);

bool SlipObject_Rotate(SlipObject *objectTableBase, size_t objectTableSize, uint32_t objectHandle, int16_t rotationX,
                       int16_t rotationY, int16_t rotationZ, int16_t rotationMode, const SlipView3DMaths *maths,
                       SlipObjectRotate *result);

SlipObjectDirection SlipObject_Direction(const SlipObject *objectTableBase, uint16_t objectOffset);

void SlipObject_SetDirectionQ14(SlipObject *objectTableBase, uint16_t objectOffset, uint16_t x, uint16_t y, uint16_t z);

void SlipObject_CopyMatrixForwardToDirection(SlipObject *objectTableBase, uint16_t objectOffset);

uint16_t SlipObject_Next(uint16_t previousObjectOffset);
bool SlipObject_IsLive(uint16_t object);

SlipView3DVec32 SlipObject_ExtrapolatedPosition(uint16_t objectHandle);

uint16_t SlipObject_PhysicsOffset(const SlipObject *objectTableBase, uint16_t objectOffset);

void SlipObject_SetPhysicsOffset(SlipObject *objectTableBase, uint16_t objectOffset, uint16_t physicsRecordOffset);

int32_t SlipObject_Speed(const SlipObject *objectTableBase, uint16_t objectOffset);

void SlipObject_SetSpeed(SlipObject *objectTableBase, uint16_t objectOffset, uint32_t speed);

SlipView3DVec32 SlipObject_Velocity(const SlipObject *objectTableBase, uint16_t objectOffset);

bool SlipObject_SetDirection(SlipObject *objectTableBase, size_t objectTableSize, uint32_t objectHandle,
                             int32_t directionX, int32_t directionY, int32_t directionZ,
                             SlipObjectSetDirection *result);

bool SlipObject_MatrixInstall(SlipObject *objectTableBase, size_t objectTableSize, uint32_t objectHandleBeforeMask,
                              const SlipView3DMatrix *savedSourceMatrix, SlipObjectMatrixInstall *result);

bool SlipObject_Position(const SlipObject *objectTableBase, size_t objectTableSize, uint32_t objectHandle,
                         SlipObjectPosition *result);
bool SlipObject_ViewPosition(SlipObject *objectTableBase, size_t objectTableSize, uint16_t objectOffset,
                             SlipView3DVec32 *viewPosition);

bool SlipObject_SetPosition(SlipObject *objectTableBase, size_t objectTableSize, uint32_t objectHandleBeforeMask,
                            uint32_t x, uint32_t y, uint32_t z, SlipObjectSetPosition *result);

enum { SLIP_OBJECT_RENDER_HIDDEN = 0x0002u, SLIP_OBJECT_RENDER_VISIBLE_MASK = UINT16_MAX ^ SLIP_OBJECT_RENDER_HIDDEN };

void SlipObject_Hide(SlipObject *objectTableBase, uint16_t objectOffset);

void SlipObject_Show(SlipObject *objectTableBase, uint16_t objectOffset);

void SlipObject_ResetActiveList(uint32_t eventPayload, uint32_t eventFlags, uintptr_t dispatchData,
                                uint32_t dispatchFrame);
void SlipObject_Shutdown(void);

bool SlipObject_ResetActiveListFresh(SlipObject *objectTableBase, size_t objectTableSize, uint16_t objectCount,
                                     SlipObjectResetActiveList *result);

bool SlipObject_InitTableFresh(SlipObject *objectTableBase, size_t objectTableSize, uint16_t objectCount,
                               SlipObjectInitTable *result);
void SlipObject_BindHostTable(SlipObject *objectTableBase, size_t objectTableSize, uint16_t objectCount);

bool SlipTrack_StartRecord(const uint8_t *trkBase, size_t trkSize, uint16_t startRecordIndex,
                           SlipTrackStartRecord *result);

bool SlipTrack_StartRecordAlternate(const uint8_t *trkBase, size_t trkSize, uint16_t startRecordIndex,
                                    SlipTrackStartRecord *result);

bool SlipTrack_StartHeading(const uint8_t *record, size_t recordBytes, SlipTrackStartHeading *result);

bool SlipObject_SlotAllocate(SlipObjectSlotAllocate *result);

bool SlipObject_SetDrawCallback(uint16_t objectOffset, SlipObjectDrawCallback drawCallback, uint32_t drawCallbackData,
                                SlipObjectSetCallback *result);

bool SlipObject_SetTrackSlot(SlipObject *objectTableBase, size_t objectTableSize, uint16_t objectOffset,
                             uint16_t trackSlotOffset);

uint8_t *SlipObject_PrivateState(uint16_t objectOffset);
SlipTimedEffectObjectState *SlipObject_TimedEffectState(uint16_t objectOffset);
SlipCrossEffectState *SlipObject_CrossEffectState(uint16_t objectOffset);
SlipAnimatedState *SlipObject_AnimatedState(uint16_t objectOffset);
uint32_t SlipObject_GetActorHandle(uint16_t objectOffset);

uint32_t SlipObject_DispatchEvent(uint16_t objectOffset, uint32_t eventValue, uint32_t primaryPayload,
                                  uint32_t secondaryPayload, uint32_t auxiliaryPayload, uintptr_t contextToken,
                                  uint32_t contextValue);

void SlipObject_DispatchUpdate(uintptr_t contextToken, uint32_t contextValue);

void SlipObject_DispatchPostUpdate(uint32_t eventValue, uint32_t primaryPayload, uint32_t secondaryPayload,
                                   uint32_t auxiliaryPayload, uintptr_t contextToken, uint32_t contextValue);

SlipObjectEventCallback SlipObject_Callback(uint16_t objectOffset);

void SlipObject_SetServer(uint16_t serverId, SlipObjectEventCallback callback);
void SlipObject_ExhaustedMatrix(SlipView3DMatrix *matrix);

uint32_t SlipObject_Stop(uint16_t objectOffset, uint32_t eventValue, uint32_t primaryPayload, uint32_t secondaryPayload,
                         uint32_t auxiliaryPayload, uintptr_t contextToken, uint32_t contextValue);

bool SlipObject_SetActorHandle(uint16_t objectOffset, uint32_t actorHandle, SlipObjectActorHandleWriteResult *result);

bool SlipObject_SlotFill(const SlipView3DMatrix *savedTemplate, uint32_t x, uint32_t y, uint32_t z,
                         SlipObjectDrawCallback slotDrawCallback, uint32_t drawData,
                         SlipObjectEventCallback eventCallback, SlipObjectSlotFill *result);

void SlipObject_BeginDeferredSection(void);

void SlipObject_EndDeferredSection(uint32_t eventValue, uint32_t primaryPayload, uint32_t secondaryPayload,
                                   uint32_t auxiliaryPayload, uintptr_t contextToken, uint32_t contextValue);

void SlipObject_FreeImmediate(uint16_t objectOffset, uint32_t eventValue, uint32_t primaryPayload,
                              uint32_t secondaryPayload, uint32_t auxiliaryPayload, uintptr_t contextToken,
                              uint32_t contextValue);

void SlipObject_Free(uint16_t objectOffset, uint32_t eventValue, uint32_t primaryPayload, uint32_t secondaryPayload,
                     uint32_t auxiliaryPayload, uintptr_t contextToken, uint32_t contextValue);

bool SlipObject_SetSlotDrawCallback(SlipObject *objectTableBase, size_t objectTableSize,
                                    uint32_t objectHandleBeforeMask, SlipObjectDrawCallback slotDrawCallback,
                                    SlipObjectDrawCallbackWriteResult *result);

bool SlipObject_GetSlotDrawCallback(const SlipObject *objectTableBase, size_t objectTableSize, uint32_t objectHandle,
                                    SlipObjectDrawCallbackReadResult *result);

bool SlipObject_SetDrawData(SlipObject *objectTableBase, size_t objectTableSize, uint32_t objectHandleBeforeMask,
                            uint32_t drawData, SlipObjectSlotDataWriteResult *result);

bool SlipObject_SetDrawExtent(SlipObject *objectTableBase, size_t objectTableSize, uint32_t objectHandleBeforeMask,
                              uint32_t drawExtent, SlipObjectExtentWriteResult *result);

bool SlipObject_SetEventCallback(SlipObject *objectTableBase, size_t objectTableSize, uint32_t objectHandleBeforeMask,
                                 SlipObjectEventCallback eventCallback, SlipObjectEventCallbackWriteResult *result);

bool SlipObject_GetDrawData(const SlipObject *objectTableBase, size_t objectTableSize, uint32_t objectHandle,
                            SlipObjectSlotDataReadResult *result);

bool SlipObject_GetDrawExtent(const SlipObject *objectTableBase, size_t objectTableSize, uint32_t objectHandle,
                              SlipObjectExtentReadResult *result);

bool SlipTrackWorld_ScheduleDrawCallback(SlipDraw3DListState *drawList, SlipDraw3DListNode *nodePool,
                                         size_t nodePoolBytes, uint32_t drawDepth, uint32_t drawRecordAddress,
                                         uint32_t slotDrawBaseToken, SlipTrackWorldDrawSchedule *result);

bool SlipTrackWorld_DrawCallbackHeader(const uint8_t *objectBase, size_t objectBaseBytes, uint16_t drawRecordOffset,
                                       SlipObjectDrawCallback slotDrawCallback, uint32_t componentBaseToken,
                                       SlipTrackWorldDrawCallbackHeader *result);

bool SlipTrackWorld_BuildAttachmentTransform(const uint8_t *record, size_t recordBytesRemaining, uint8_t *inputChild,
                                             size_t childBytesRemaining, const uint8_t *attachment,
                                             size_t attachmentBytesRemaining, const SlipView3DMatrix *viewMatrix,
                                             SlipView3DVec32 after, SlipTrackWorldBuildAttachment *result);

bool SlipTrackWorld_UseAttachmentTransform(uint8_t *record, size_t recordBytesRemaining,
                                           SlipTrackWorldUseAttachment *result);

bool SlipTrackWorld_InvokeDrawCallback(const uint8_t *record, size_t recordBytesRemaining, uint32_t drawStateIndex,
                                       uint32_t renderMode, uint32_t frameRenderFlags, bool clear,
                                       SlipTrackWorldInvokeDrawCallback *result);

bool SlipTrackWorld_InvokeDrawCallbackExecute(const uint8_t *record, size_t recordBytesRemaining,
                                              uint32_t recordAddress, uint32_t callerValue, uint32_t drawStateIndex,
                                              uint32_t renderMode, uint32_t frameRenderFlags, bool clear,
                                              const uint8_t *slotListBase, size_t slotListBytes,
                                              uint32_t slotListBaseAddress, const SlipObject *objectTableBase,
                                              size_t objectTableBytes, SlipTrackWorldInvokeDrawCallbackExecute *result);

bool SlipTrackWorld_ComponentGate(const uint8_t *record, size_t recordBytesRemaining, const uint8_t *componentBase,
                                  size_t componentBaseBytes, uint32_t componentBaseToken, uint32_t valueBeforeGate,
                                  uint16_t mask, SlipTrackWorldComponentGate *result);

bool SlipTrackWorld_ComponentProject(const uint8_t *record, size_t recordBytesRemaining, SlipView3DVec32 after,
                                     bool trackWorldCullBoundsCarry, SlipTrackWorldComponentProject *result);

uint32_t SlipTrackWorld_ClassifyPoint(SlipView3DVec32 point, uint32_t projectionMask, int32_t minZ, int32_t maxZ);

bool SlipTrackWorld_CullBounds(const uint8_t *component, size_t componentBytesRemaining,
                               const SlipView3DMatrix *viewMatrix, SlipView3DVec32 center, int32_t minZ, int32_t maxZ,
                               SlipTrackWorldSphereCull sphereCull, SlipTrackWorldProjectMask projectMask,
                               void *userData, SlipTrackWorldCullBounds *result);

bool SlipTrackWorld_IndirectCull(SlipView3DVec32 center, int32_t radius, SlipTrackWorldSphereCull callback,
                                 void *userData, SlipTrackWorldIndirectCull *result);

uint16_t SlipTrackWorld_RandomStep(uint16_t state);

bool SlipTrackWorld_ProjectMask(SlipView3DVec32 point, const SlipTrackWorldProjectFrustum *frustum,
                                uint32_t *clipMaskOut);

bool SlipTrackWorld_SphereCull(SlipView3DVec32 center, int32_t radius, const SlipTrackWorldProjectFrustum *frustum,
                               bool *carry);

bool SlipTrackWorld_UpdateDrawFlags(uint32_t renderFlagsValue, int32_t componentDepth, uint32_t textureMode,
                                    uint32_t shading, int32_t componentDistance, uint32_t shadingSecondary,
                                    int32_t componentRadius, SlipTrackWorldDrawFlags *result);

bool SlipTrackWorld_FrameDrawState(uint32_t drawFlagsFrom, uint32_t shading, uint32_t shadingSecondary,
                                   SlipTrackWorldFrameDrawState *result);

bool SlipTrackWorld_FrameCaller(SlipRaceTrackFrameCallback frameCallback, uint16_t overlayEnable,
                                uint16_t actorReplayMode, uint16_t secondaryActorDrawParameter,
                                uint16_t auxiliaryActorDrawParameter, uint32_t savedClipLeft, uint32_t savedClipTop,
                                uint32_t savedClipRight, uint32_t savedClipBottom, int32_t trkHeaderGate,
                                SlipTrackWorldFrameCaller *result);

bool SlipTrackWorld_PreFrameCameraPrefix(uint32_t globalGate, uint32_t savedFrameCallback, uint32_t savedViewportLeft,
                                         uint32_t savedViewportTop, uint32_t savedViewportRight,
                                         uint32_t savedViewportBottom, uint32_t savedViewportCenterX,
                                         uint32_t savedViewportCenterY, uint16_t materialFrameOffset,
                                         uint32_t materialWidth, uint32_t materialHeight,
                                         uint16_t materialViewportWidth, uint32_t savedDepth, uint32_t rangeOriginY,
                                         SlipView3DVec32 objectPositionFrom,
                                         SlipTrackWorldPreFrameCameraPrefix *result);

bool SlipTrackWorld_PreFrameCameraSuffix(uint32_t frameCallbackTo, SlipView3DVec32 savedObjectPosition,
                                         uint32_t savedDepth, uint32_t restoredViewportLeft,
                                         uint32_t restoredViewportTop, uint32_t restoredViewportRight,
                                         uint32_t restoredViewportBottom, uint32_t restoredViewportCenterX,
                                         uint32_t restoredViewportCenterY, SlipTrackWorldPreFrameCameraSuffix *result);

bool SlipTrackWorld_CameraFrame(uint16_t cameraObjectOffset, uint32_t viewportLeftBeforeInset,
                                uint32_t viewportTopBeforeInset, uint32_t viewportRightBeforeInset,
                                uint32_t viewportBottomBeforeInset, uint32_t pushedProjection,
                                SlipRaceTrackFrameCallback frameCallbackFrom, uint32_t viewportLeftAfterInset,
                                uint32_t viewportTopAfterInset, uint32_t viewportRightAfterInset,
                                uint32_t viewportBottomAfterInset, int32_t trkHeaderGate, uint16_t weaponLabelResource,
                                uint32_t labelTopBeforeInset, uint32_t labelHeight, uint32_t labelWidth,
                                SlipTrackWorldCameraFrame *result);

bool SlipTrackWorld_FrameEntry(
    SlipRaceTrackFrameCallback frameCallback, uint32_t recordIndex, uint32_t drawFlagsFrom, uint32_t textureMode,
    uint32_t shading, int32_t componentDistance, uint32_t shadingSecondary, int32_t componentRadius,
    uint32_t savedClipLeft, uint32_t savedClipTop, uint32_t savedClipRight, uint32_t savedClipBottom,
    uint32_t depthFrom, uint32_t savedFadeStart, uint32_t savedFadeEnd, uint32_t savedFadeColour, uint16_t timerValue,
    uint32_t stateTokenFrom, uint32_t renderContext, uint32_t primaryLeft, uint32_t primaryTop, uint32_t primaryRight,
    uint32_t primaryBottom, uint16_t renderMode, uint32_t underSeaColor,
    SlipTrackWorldTraversalCallback traversalCallback, SlipTrackWorldRecordCallback recordCallback,
    uint32_t defaultTraversalGate, uint32_t clipMinX, uint32_t clipMinY, uint32_t clipMaxX, uint32_t clipMaxY,
    uint8_t *trkBase, size_t trkSize, const uint8_t *trdBase, size_t trackDataSize, const uint8_t *table,
    size_t tableBytes, uint32_t trdBaseAddress, uint32_t recordAddress,
    SlipTrackWorldTraversalContext *traversalContext, const SlipRaceTrackFrameCallbackExecuteArgs *callbackArgs,
    const SlipObject *objectTableBase, size_t objectTableSize, SlipView3DMatrix *objectTransformMatrix,
    SlipView3DMatrix *viewMatrix, uint32_t mode, int32_t detailThreshold, uint8_t *rampX, size_t rampXBytes,
    uint8_t *rampY, size_t rampYBytes, uint8_t *rampZ, size_t rampZBytes, uint8_t *tableFirst, size_t tableFirstBytes,
    uint8_t *tableSecond, size_t tableSecondBytes, uint8_t *tableThird, size_t tableThirdBytes,
    SlipTrackWorldAxisTestVisit *axisTestVisits, size_t axisTestVisitCapacity, uint8_t *objectList,
    size_t objectListBytes, uint32_t objectListBaseAddress, uint8_t *deferredList, size_t deferredListBytes,
    const SlipTrackWorldDosAddressMap *deferredEntryMap, size_t deferredEntryMapCount,
    const SlipTrackWorldDosAddressMap *deferredRecordMap, size_t deferredRecordMapCount, uint32_t componentBaseAddress,
    const uint8_t *componentBase, size_t componentBaseBytes, SlipDraw3DVertexRecord *vertexRecords,
    size_t vertexRecordCapacity, SlipTrackWorldDrawStateLoad drawStateLoad, void *drawStateLoadUserData,
    SlipTrackWorldBuildVertexRecords buildVertexRecords, void *buildVertexRecordsUserData,
    SlipTrackWorldRestoreVertexBuffer restoreVertexBuffer, void *restoreVertexBufferUserData,
    SlipTrackWorldDeferredListDirectExecuteVisit *deferredDirectVisits, size_t deferredDirectVisitCapacity,
    const uint8_t *specialRecord, uint32_t globalAfter, uint16_t randomState, uint32_t shadows,
    uint32_t processedComponentCount, uint16_t actorReplayMode, uint32_t ambientLightScaleQ14, uint32_t scaledLightX,
    uint32_t scaledLightY, uint32_t scaledLightZ, uint32_t directLightScaleQ14, uint32_t renderContextCount,
    SlipTrackWorldPrimitiveCallback primitiveCallback, SlipTrackWorldStoreClipBoundsFunction storeClipBoundsFunction,
    void *storeClipBoundsUserData, SlipTrackWorldDirectCallbackFunction callbackFunction, void *callbackUserData,
    SlipTrackWorldComponentActorDraw componentActorDraw, void *componentActorDrawUserData, uint32_t frameRenderFlags,
    uint8_t *slotDrawBase, size_t slotDrawBytes, uint16_t slotDrawCount,
    SlipTrackWorldSlotDrawClearVisit *slotDrawClearVisits, uint16_t slotDrawClearVisitCapacity, uint16_t overlayEnable,
    uint32_t secondaryLeft, uint32_t secondaryRight, uint32_t secondaryTop, uint32_t secondaryBottom,

    SlipTrackWorldSphereCull sphereCull, void *sphereCullUserData,

    SlipTrackWorldShapeDraw shapeDraw, void *shapeDrawUserData, SlipTrackWorldComponentLight scaledLight,
    SlipTrackWorldComponentLight restoreLight, void *lightUserData, SlipTrackWorldFrameEntry *result);

bool SlipRaceTrack_DrawEgyptBackground(const SlipRaceTrackFrameCallbackExecuteArgs *args,
                                       SlipRaceTrackFrameCallbackExecute *result);
bool SlipRaceTrack_DrawArizonaNorwayBackground(const SlipRaceTrackFrameCallbackExecuteArgs *args,
                                               SlipRaceTrackFrameCallbackExecute *result);
bool SlipRaceTrack_DrawChicagoBackground(const SlipRaceTrackFrameCallbackExecuteArgs *args,
                                         SlipRaceTrackFrameCallbackExecute *result);
bool SlipRaceTrack_DrawHawaiiBackground(const SlipRaceTrackFrameCallbackExecuteArgs *args,
                                        SlipRaceTrackFrameCallbackExecute *result);
bool SlipRaceTrack_DrawAmazonBackground(const SlipRaceTrackFrameCallbackExecuteArgs *args,
                                        SlipRaceTrackFrameCallbackExecute *result);
bool SlipRaceTrack_DrawTokyoBackground(const SlipRaceTrackFrameCallbackExecuteArgs *args,
                                       SlipRaceTrackFrameCallbackExecute *result);
bool SlipRaceTrack_DrawLondonBackground(const SlipRaceTrackFrameCallbackExecuteArgs *args,
                                        SlipRaceTrackFrameCallbackExecute *result);
bool SlipRaceTrack_DrawFranceBackground(const SlipRaceTrackFrameCallbackExecuteArgs *args,
                                        SlipRaceTrackFrameCallbackExecute *result);
bool SlipRaceTrack_DrawNewYorkBackground(const SlipRaceTrackFrameCallbackExecuteArgs *args,
                                         SlipRaceTrackFrameCallbackExecute *result);

bool SlipRaceTrack_MaterialGlobals(const uint8_t *materialTable, size_t materialTableBytes, uint16_t materialGlobal,
                                   SlipRaceTrackMaterialGlobals *result);

bool SlipTrackWorld_CameraSetup(uint32_t cameraPositionX, uint32_t cameraPositionY, uint32_t cameraPositionZ,
                                uint32_t viewOriginX, uint32_t viewOriginY, uint32_t viewOriginZ,
                                SlipTrackWorldCameraSetup *result);

bool SlipTrackWorld_CameraSetupExecute(const SlipObject *objectTableBase, size_t objectTableSize,
                                       SlipView3DMatrix *objectTransformMatrix, SlipView3DMatrix *viewMatrix,
                                       SlipTrackWorldCameraSetupExecute *result);

bool SlipTrackWorld_MatrixInstall(SlipView3DMatrix *viewMatrix, const SlipView3DMatrix *sourceMatrix,
                                  SlipTrackWorldMatrixInstall *result);

bool SlipTrackWorld_ClearGlobals(SlipTrackWorldClearGlobals *result);
bool SlipTrackWorld_ClearSlotDrawLinks(uint8_t *slotDrawBase, size_t slotDrawBytes, uint16_t slotDrawCount,
                                       SlipTrackWorldSlotDrawClearVisit *visits, uint16_t visitCapacity,
                                       SlipTrackWorldSlotDrawClear *result);
bool SlipTrackWorld_InitSlotDrawRing(uint8_t *slotDrawBase, size_t slotDrawBytes, uint32_t slotDrawBaseAddress,
                                     uint16_t slotDrawCount, SlipTrackWorldSlotDrawRingVisit *visits,
                                     uint16_t visitCapacity, SlipTrackWorldSlotDrawRing *result);
bool SlipTrackWorld_AllocSlotDrawRecord(uint8_t *slotDrawBase, size_t slotDrawBytes, uint32_t slotDrawBaseAddress,
                                        uint32_t freeListAddress, uint8_t *slotListEntry, size_t slotListEntryBytes,
                                        uint32_t slotListEntryAddress, SlipTrackWorldSlotDrawAlloc *result);
bool SlipTrackWorld_FreeSlotDrawRecord(uint8_t *slotDrawBase, size_t slotDrawBytes, uint32_t slotDrawBaseAddress,
                                       uint32_t freeListAddress, uint8_t *trdBase, size_t trackDataSize,
                                       uint32_t trdBaseAddress, uint32_t drawRecordAddress,
                                       SlipTrackWorldSlotDrawFree *result);
bool SlipTrackWorld_ClearOwnerDrawLinks(uint16_t trackHandle, uint8_t *slotDrawBase, size_t slotDrawBytes,
                                        uint32_t slotDrawBaseAddress, uint32_t freeListAddress, uint8_t *trdBase,
                                        size_t trackDataSize, uint32_t trdBaseAddress, uint8_t *slotListEntry,
                                        size_t slotListEntryBytes, SlipTrackWorldOwnerDrawLinksClear *result);
bool SlipTrackWorld_SelectSlotListEntry(uint32_t callerValue, uint32_t slotListBaseAddress,
                                        const SlipObject *objectTableBase, size_t objectTableBytes,
                                        uint16_t objectOffset, SlipTrackWorldSlotListSelect *result);

const uint8_t *SlipTrackWorld_GetCurrentName(uint32_t currentToken, uint16_t objectOffset, uint8_t *slotListBase,
                                             size_t slotListBytes, uint32_t slotListBaseAddress,
                                             const SlipObject *objectTable, size_t objectTableBytes,
                                             const uint8_t *trdBase, size_t trackDataSize, uint32_t trdBaseAddress,
                                             const uint8_t *componentBase, size_t componentBaseBytes,
                                             uint32_t componentBaseAddress, const uint8_t *table, size_t tableBytes);

void SlipTrackWorld_CurrentSlot(uint32_t currentToken, uint16_t objectOffset, uint8_t *slotListBase,
                                size_t slotListBytes, uint32_t slotListBaseAddress, const SlipObject *objectTable,
                                size_t objectTableBytes, const uint8_t *trdBase, size_t trackDataSize,
                                uint32_t trdBaseAddress, const uint8_t *componentBase, size_t componentBaseBytes,
                                uint32_t componentBaseAddress, const uint8_t *table, size_t tableBytes,
                                uint32_t currentComponentToken, SlipTrackWorldCurrentSlot *result);

uint32_t SlipTrackWorld_CurrentComponent(uint32_t currentToken, uint16_t objectOffset, uint8_t *slotListBase,
                                         size_t slotListBytes, uint32_t slotListBaseAddress,
                                         const SlipObject *objectTable, size_t objectTableBytes, const uint8_t *trdBase,
                                         size_t trackDataSize, uint32_t trdBaseAddress, const uint8_t *componentBase,
                                         size_t componentBaseBytes, uint32_t componentBaseAddress, const uint8_t *table,
                                         size_t tableBytes);

uint16_t SlipTrackWorld_StartComponent(const uint8_t *trdBase);
uint16_t SlipTrackWorld_PreviousComponent(const uint8_t *trdBase);

uint32_t SlipTrackWorld_RaceProgress(uint16_t objectOffset, uint8_t *slotListBase, size_t slotListBytes,
                                     uint32_t slotListBaseAddress, const SlipObject *objectTable,
                                     size_t objectTableBytes, const uint8_t *trdBase, size_t trackDataSize,
                                     uint32_t trdBaseAddress, const uint8_t *componentBase, size_t componentBaseBytes,
                                     uint32_t componentBaseAddress, const uint8_t *table, size_t tableBytes);

void SlipTrackWorld_SetLapDistance(uint8_t *trdBase);

uint32_t SlipTrackWorld_TotalLength(const uint8_t *trdBase);

uint32_t SlipTrackWorld_TrackFloor(const uint8_t *trkBase);
bool SlipTrackWorld_ExecuteObjectAttachmentDraw(const uint8_t *drawRecord, size_t recordBytesRemaining,
                                                uint32_t callerValue, const uint8_t *slotListBase, size_t slotListBytes,
                                                uint32_t slotListBaseAddress, const SlipObject *objectTableBase,
                                                size_t objectTableBytes,
                                                SlipTrackWorldObjectAttachmentDrawExecution *result);
bool SlipTrackWorld_ExecuteObjectCallbackDraw(const uint8_t *drawRecord, size_t recordBytesRemaining,
                                              uint32_t callerValue, uint32_t counter, const uint8_t *slotListBase,
                                              size_t slotListBytes, uint32_t slotListBaseAddress,
                                              const SlipObject *objectTableBase, size_t objectTableBytes,
                                              SlipTrackWorldObjectCallbackDrawExecution *result);
bool SlipTrackWorld_BindSlotDraw(uint16_t requestedDrawCount, uint8_t *slotDrawBase, size_t slotDrawBytes,
                                 uint16_t allocationHandle, uint32_t slotDrawBaseAddress,
                                 SlipTrackWorldSlotDrawRingVisit *visits, uint16_t visitCapacity,
                                 SlipTrackWorldSlotDrawInstall *result);
bool SlipTrackWorld_BindSlotList(uint16_t requestedSlotCount, uint8_t *slotListBase, size_t slotListBytes,
                                 uint16_t allocationHandle, uint32_t slotListBaseAddress,
                                 SlipTrackWorldSlotListVisit *visits, uint16_t visitCapacity,
                                 SlipTrackWorldSlotListInstall *result);
bool SlipTrackWorld_AllocSlotListEntry(uint8_t *slotListBase, size_t slotListBytes, uint32_t slotListBaseAddress,
                                       uint32_t activeListAddress, uint32_t freeListAddress,
                                       SlipTrackWorldSlotListAlloc *result);

bool SlipTrackWorld_FreeSlotListEntry(uint16_t trackHandle, uint8_t *slotDrawBase, size_t slotDrawBytes,
                                      uint32_t slotDrawBaseAddress, uint32_t slotDrawFreeListAddress, uint8_t *trdBase,
                                      size_t trackDataSize, uint32_t trdBaseAddress, uint8_t *slotListBase,
                                      size_t slotListBytes, uint32_t slotListBaseAddress,
                                      uint32_t slotListFreeListAddress, uint8_t *inputSlot);

bool SlipTrackWorld_RemoveObjectSlot(uint32_t objectToken, uint16_t objectOffset, uint16_t trackHandle,
                                     uint8_t *slotDrawBase, size_t slotDrawBytes, uint32_t slotDrawBaseAddress,
                                     uint32_t slotDrawFreeListAddress, uint8_t *trdBase, size_t trackDataSize,
                                     uint32_t trdBaseAddress, uint8_t *slotListBase, size_t slotListBytes,
                                     uint32_t slotListBaseAddress, uint32_t slotListFreeListAddress,
                                     const SlipObject *objectTable, size_t objectTableBytes);

typedef struct SlipTrackWorldSlotCollisionState {
	uint32_t currentRecordOrFlags, preservedObjectFreeValue, remainingCornerCount, secondRecordOrOffset,
	    cornerCursorAddress, slotAddress, firstRecordAddress;
} SlipTrackWorldSlotCollisionState;

bool SlipTrackWorld_RegisterSlot(uint16_t trackHandle, uint8_t *slotDrawBase, size_t slotDrawBytes,
                                 uint32_t slotDrawBaseAddress, uint32_t slotDrawFreeListAddress, uint8_t *slotListBase,
                                 size_t slotListBytes, uint32_t slotListBaseAddress, uint8_t *trdBase,
                                 size_t trackDataSize, uint32_t trdBaseAddress, const SlipObject *objectTable,
                                 size_t objectTableBytes, SlipObjectDrawCallback *slotDrawCallbacks,
                                 size_t slotDrawCallbackCount, uint8_t *inputSlot,
                                 const SlipTrackWorldSlotCollisionState *entryRegisters);

bool SlipTrackWorld_AddSlot(uint16_t objectOffset, uint32_t flags, uint16_t trackHandle, uint8_t *slotDrawBase,
                            size_t slotDrawBytes, SlipObjectDrawCallback *slotDrawCallbacks,
                            size_t slotDrawCallbackCount, uint32_t slotDrawBaseAddress,
                            uint32_t slotDrawFreeListAddress, uint8_t *slotListBase, size_t slotListBytes,
                            uint32_t slotListBaseAddress, uint32_t slotListActiveAddress, uint32_t slotListFreeAddress,
                            SlipObject *objectTable, size_t objectTableBytes, uint8_t *articSlotPool,
                            size_t articSlotPoolBytes, uint32_t articSlotPoolAddress, uint8_t *trdBase,
                            size_t trackDataSize, uint32_t trdBaseAddress, const uint8_t *componentBase,
                            size_t componentBaseBytes, uint32_t componentBaseAddress, const uint8_t *table,
                            size_t tableBytes, SlipTrackWorldAddSlot *result);

bool SlipTrackWorld_StateReset(uint32_t recordIndex, SlipTrackWorldStateReset *result);

bool SlipTrackWorld_FillAxisRamp(uint8_t *ramp, size_t rampBytes, uint16_t directionX, uint16_t directionY,
                                 uint16_t directionZ, uint16_t lastTripletIndex, SlipTrackWorldAxisRamp *result);

bool SlipTrackWorld_BuildAxisRamps(const uint8_t *viewMatrix, size_t matrixBytes, uint8_t *rampX, size_t rampXBytes,
                                   uint8_t *rampY, size_t rampYBytes, uint8_t *rampZ, size_t rampZBytes,
                                   SlipTrackWorldBuildAxisRamps *result);

bool SlipTrackWorld_AxisTestWord(uint32_t mode, uint32_t positionX, uint32_t positionY, uint32_t positionZ,
                                 uint16_t normalX, uint16_t normalY, uint16_t normalZ,
                                 SlipTrackWorldAxisTestWord *result);

bool SlipTrackWorld_BuildAxisTests(uint32_t mode, const uint8_t *viewMatrix, size_t matrixBytes, const uint8_t *rampX,
                                   size_t rampXBytes, const uint8_t *rampY, size_t rampYBytes, const uint8_t *rampZ,
                                   size_t rampZBytes, uint8_t *tableFirst, size_t tableFirstBytes, uint8_t *tableSecond,
                                   size_t tableSecondBytes, uint8_t *tableThird, size_t tableThirdBytes, uint32_t addX,
                                   uint32_t addY, uint32_t addZ, SlipTrackWorldAxisTestVisit *visits,
                                   size_t visitCapacity, SlipTrackWorldBuildAxisTests *result);

bool SlipTrackWorld_PostFrameOverlay(uint16_t overlayEnable, uint32_t renderContextCount, uint32_t primaryLeft,
                                     uint32_t primaryTop, uint32_t primaryRight, uint32_t primaryBottom,
                                     uint32_t secondaryLeft, uint32_t secondaryRight, uint32_t secondaryTop,
                                     uint32_t secondaryBottom, SlipTrackWorldPostFrameOverlay *result);

bool SlipTrackWorld_ScaledCallSetup(uint16_t lightMultiplierQ14, uint32_t ambientLightScaleQ14,
                                    uint32_t directLightScaleQ14, uint32_t scaledLightX, uint32_t scaledLightY,
                                    uint32_t scaledLightZ, SlipTrackWorldScaledCallSetup *result);

bool SlipTrackWorld_RecordAdvance(const uint8_t *record, size_t recordBytesRemaining, size_t recordOffsetBefore,
                                  uint32_t savedCallbackValue, SlipTrackWorldRecordAdvance *result);

bool SlipTrackWorld_DirectCallbackLoop(const uint8_t *component, size_t componentBytesRemaining,
                                       const uint8_t *componentBase, size_t componentBaseBytes,
                                       SlipTrackWorldPrimitiveCallback primitiveCallback, uint32_t callbackValueEntry,
                                       SlipTrackWorldDirectCallbackFunction callbackFunction, void *callbackUserData,
                                       const SlipTrackWorldDirectCallbackEnvironment *callbackEnvironment,
                                       const SlipTrackWorldDirectCallbackInput *callbackInputs,
                                       size_t callbackInputCount, SlipTrackWorldDirectCallbackVisit *visits,
                                       size_t visitCapacity, SlipTrackWorldDirectCallbackLoop *result);

bool SlipTrackWorld_GlobalCarryGate(uint32_t reflectionEnabled, SlipTrackWorldGlobalCarryGate *result);

bool SlipTrackWorld_ReplayList(uint16_t count, const uint8_t *list, size_t listBytes,
                               uint32_t drawStateIndexBeforeChunk, uint32_t drawStateIndexAfterChunk,
                               SlipTrackWorldReplayListVisit *visits, size_t visitCapacity,
                               SlipTrackWorldReplayList *result);

bool SlipTrackWorld_OptionalRecord(const uint8_t *record, size_t recordBytesRemaining, uint16_t planeNormalY,
                                   uint16_t planeNormalZ, uint32_t materialFrameAddress, uint32_t transformedNormalX,
                                   uint32_t transformedNormalY, uint32_t transformedNormalZ,
                                   bool draw3DCapturePostPlaneRingCarry, uint16_t replayCount,
                                   const uint8_t *replayList, size_t replayListBytes, uint32_t replayDrawStateBefore,
                                   uint32_t replayDrawStateAfter, SlipTrackWorldReplayListVisit *replayVisits,
                                   size_t replayVisitCapacity, SlipTrackWorldOptionalRecord *result);

bool SlipTrackWorld_ReplaySourceDispatch(const uint8_t *initialRecord, size_t recordBytesRemaining,
                                         const uint8_t *chunkBase, size_t chunkBaseBytes,
                                         SlipTrackWorldReplaySourceDispatch *result);

bool SlipTrackWorld_ReplaySourceScan(const uint8_t *record, size_t recordBytesRemaining, uint32_t objectBaseAddress,
                                     const uint8_t *objectBase, size_t objectBaseBytes, uint16_t initialCount,
                                     uint16_t replayList[SLIP_TRACK_REPLAY_OBJECT_CAPACITY],
                                     uint32_t *objectDrawCallbacks, size_t objectDrawCallbackCount,
                                     SlipTrackWorldReplaySourceScanVisit *visits, size_t visitCapacity,
                                     SlipTrackWorldReplaySourceScan *result);

bool SlipTrackWorld_MaterialStateStore(uint32_t planeOriginX, uint32_t planeOriginY, uint32_t planeOriginZ,
                                       uint32_t planeNormalX, uint32_t planeNormalY, uint32_t planeNormalZ,
                                       SlipTrackWorldMaterialStateStore *result);

bool SlipTrackWorld_MaterialHandler(uint32_t perspectiveDepth, uint16_t positiveShade, uint16_t negativeShade,
                                    bool carryFrom, const uint8_t *list, size_t listBytes,
                                    SlipTrackWorldMaterialHandlerVisit *visits, size_t visitCapacity,
                                    SlipTrackWorldMaterialHandler *result);

void SlipTrackWorld_BindActorRingHostMappings(const uint8_t *trdBase, size_t trackDataSize, const uint8_t *slotDrawBase,
                                              size_t slotDrawBytes, uint32_t slotDrawBaseAddress,
                                              const SlipObject *objectTable, size_t objectTableBytes);

bool SlipTrackWorld_ComponentSetup(const uint8_t *currentRecord, size_t recordBytesRemaining, uint32_t incomingValue,
                                   uint32_t componentViewZ, uint32_t inputDrawFlags, uint32_t textureMode,
                                   uint32_t shading, int32_t componentDistance, uint32_t shadingSecondary,
                                   int32_t componentRadius, const uint8_t *specialRecord, uint32_t globalAfter,
                                   uint16_t randomState, uint32_t shadows, uint32_t processedComponentCount,
                                   uint32_t drawStateIndexBefore, const SlipTrackWorldComponentRefuelCalls *refuel,
                                   SlipTrackWorldComponentSetup *result);

bool SlipTrackWorld_ComponentTail(const uint8_t *component, size_t componentBytesRemaining,
                                  const uint8_t *componentBase, size_t componentBaseBytes, uint16_t shade,
                                  uint32_t renderFlagsValue, uint16_t actorReplayMode, const uint8_t *storedComponent,
                                  uint32_t ambientLightScaleQ14, uint32_t scaledLightX, uint32_t scaledLightY,
                                  uint32_t scaledLightZ, uint32_t directLightScaleQ14, uint32_t renderContextCount,
                                  SlipTrackWorldPrimitiveCallback primitiveCallback, uint32_t drawStateIndexBefore,
                                  SlipTrackWorldComponentTailVisit *visits, size_t visitCapacity, size_t *visitCount,
                                  SlipTrackWorldComponentTail *result);

bool SlipTrackWorld_PrimitiveWalker(const uint8_t *component, size_t componentBytesRemaining,
                                    const uint8_t *componentBase, size_t componentBaseBytes, uint16_t count,
                                    SlipTrackWorldPrimitiveWalkerVisit *visits, size_t visitCapacity,
                                    SlipTrackWorldPrimitiveWalker *result);

bool SlipTrackWorld_PrimitiveCallbackDispatch(
    const uint8_t *record, size_t recordBytesRemaining, size_t recordOffset, uint32_t callbackValueEntry,
    bool planeVisibleCarry, bool trackWorldGlobalCarryGateCarry, bool solidEmitCarry, bool signFlagFrom,
    uint32_t polygonStatusValue, uint16_t materialFrameOffset, uint32_t perspectiveDepth, uint32_t farTextureDepth,
    uint32_t affineDepthThreshold, uint32_t renderFlagsValue, SlipTrackWorldPrimitiveCallbackDispatch *result);

bool SlipTrackWorld_ListSetup(uint8_t *objectList, size_t objectListBytes, uint32_t listBaseAddress,
                              uint8_t *deferredList, size_t deferredListBytes, uint32_t frameRenderFlags,
                              uint32_t defaultTraversalGate, uint32_t drawStateIndexBefore, uint32_t viewportMinX,
                              uint32_t viewportMinY, uint32_t viewportMaxX, uint32_t viewportMaxY, uint32_t clipMinX,
                              uint32_t clipMinY, uint32_t clipMaxX, uint32_t clipMaxY,
                              SlipTrackWorldTraversalCallback previousRecordCallback,
                              SlipTrackWorldRecordCallback previousTraversalCallback, uint32_t savedMaximumDepth,
                              uint32_t cameraWorldX, uint32_t cameraWorldY, uint32_t cameraWorldZ,
                              const uint8_t *viewMatrix, size_t matrixBytes, const SlipObject *objectTableBase,
                              size_t objectTableSize, uint8_t *trkBase, size_t trkSize, const uint8_t *trdBase,
                              size_t trackDataSize, const uint8_t *componentBase, size_t componentBaseBytes,
                              const uint8_t *table, size_t tableBytes, uint32_t trdBaseAddress, uint32_t recordAddress,
                              SlipTrackWorldTraversalContext *traversalContext, SlipTrackWorldListSetup *result);

bool SlipTrackWorld_ClearListHeads(uint8_t *objectList, size_t objectListBytes, uint8_t *deferredList,
                                   size_t deferredListBytes, SlipTrackWorldListHeadClear *result);

bool SlipTrackWorld_ObjectSelect(const uint8_t *searchedRecord, size_t recordBytesRemaining,
                                 const uint8_t *componentBase, size_t componentBaseBytes,
                                 SlipTrackWorldObjectSelect *result);

bool SlipTrackWorld_ComponentBoundsGate(const uint8_t *componentBase, size_t componentBaseBytes,
                                        uint16_t componentOffset, int32_t localX, int32_t localY, int32_t localZ,
                                        SlipTrackWorldComponentBounds *result);

bool SlipTrackWorld_ComponentChildRangeScan(const uint8_t *componentBase, size_t componentBaseBytes,
                                            uint16_t componentOffset, size_t firstChildRecordOffset,
                                            uint16_t childRecordCount, int32_t storedLocalX, int32_t storedLocalY,
                                            int32_t storedLocalZ, SlipTrackWorldComponentChildRangeVisit *visits,
                                            size_t visitCapacity, SlipTrackWorldComponentChildRangeScan *result);

bool SlipTrackWorld_RecordComponentTest(const uint8_t *trdBase, size_t trdBytes, const uint8_t *componentBase,
                                        size_t componentBaseBytes, uint32_t trdBaseAddress, uint32_t recordAddress,
                                        int32_t objectX, int32_t objectY, int32_t objectZ,
                                        SlipTrackWorldRecordComponentTest *result);

bool SlipTrackWorld_CellRecordBoundsScan(const uint8_t *trdBase, size_t trdBytes, const uint8_t *componentBase,
                                         size_t componentBaseBytes, uint32_t trdBaseAddress,
                                         uint32_t cellDescriptorAddress, int32_t objectX, int32_t objectY,
                                         int32_t objectZ, SlipTrackWorldCellRecordBoundsVisit *visits,
                                         size_t visitCapacity, SlipTrackWorldCellRecordBoundsScan *result);

bool SlipTrackWorld_ObjectDraw(const uint8_t *listCursor, uint8_t *objectList, size_t objectListBytes,
                               uint32_t objectListBaseAddress, uint32_t objectListCursorAddress, const uint8_t *object,
                               size_t objectBytesRemaining, uint32_t objectAddress, uint32_t savedClipMinX,
                               uint32_t savedClipMinY, uint32_t savedClipMaxX, uint32_t savedClipMaxY,
                               uint32_t viewportMinX, uint32_t viewportMinY, uint32_t viewportMaxX,
                               uint32_t viewportMaxY, SlipTrackWorldTraversalCallback previousRecordCallback,
                               SlipTrackWorldRecordCallback previousTraversalCallback, uint16_t mask,
                               uint32_t savedMaximumDepth, uint32_t cameraWorldX, uint32_t cameraWorldY,
                               uint32_t cameraWorldZ, const uint8_t *viewMatrix, size_t matrixBytes,
                               uint32_t drawStateIndex, uint8_t *trkBase, size_t trkSize,
                               SlipTrackWorldTraversalContext *traversalContext, SlipTrackWorldObjectDraw *result);

bool SlipTrackWorld_StateSave(uint32_t clipMinX, uint32_t clipMinY, uint32_t clipMaxX, uint32_t clipMaxY,
                              SlipTrackWorldStateSave *result);

bool SlipTrackWorld_ObjectListEntry(uint8_t *objectList, size_t objectListBytes, uint32_t objectListBaseAddress,
                                    uint32_t *objectListCursorAddress, const uint8_t *currentObject,
                                    size_t objectBytesRemaining, uint32_t currentObjectAddress, uint32_t counter,
                                    uint32_t defaultTraversalGate, uint32_t rangeFlag, uint32_t useFullObjectViewport,
                                    uint32_t viewX, uint32_t viewY, uint32_t viewZ, uint32_t objectToken,
                                    uint32_t viewportMinX, uint32_t viewportMinY, uint32_t viewportMaxX,
                                    uint32_t viewportMaxY, SlipTrackWorldObjectListEntryResult *result);

bool SlipTrackWorld_ObjectTransform(const uint8_t *object, size_t objectBytesRemaining, uint8_t *inputEntry,
                                    size_t entryBytesRemaining, uint32_t cameraWorldX, uint32_t cameraWorldY,
                                    uint32_t cameraWorldZ, uint32_t x, uint32_t y, uint32_t z,
                                    SlipTrackWorldObjectTransform *result);

bool SlipTrackWorld_ComponentList(const uint8_t *currentObject, size_t objectBytesRemaining,
                                  const uint8_t *componentBase, size_t componentBaseBytes,
                                  SlipTrackWorldComponentList *result);

bool SlipTrackWorld_PrimitiveBounds(const uint8_t *list, size_t listBytesRemaining, uint32_t renderContextCount,
                                    uint32_t primaryLeft, uint32_t primaryTop, uint32_t primaryRight,
                                    uint32_t primaryBottom, const SlipTrackWorldPrimitiveBoundsCall *calls,
                                    size_t callCount, SlipTrackWorldPrimitiveBoundsVisit *visits, size_t visitCapacity,
                                    size_t *visitCount, SlipTrackWorldPrimitiveBounds *result);

bool SlipTrackWorld_PrimitiveBoundsEvaluated(
    const uint8_t *list, size_t listBytesRemaining, uint32_t renderContextCount, uint32_t primaryLeft,
    uint32_t primaryTop, uint32_t primaryRight, uint32_t primaryBottom, uint32_t mode, const uint8_t *vertexCacheBase,
    size_t vertexCacheBytes, SlipView3DVec32 origin, SlipTrackWorldSourcePoint sourcePoint, SlipDraw3DRecordPool *pool,
    SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCount, const SlipDraw3DProjectState *state,
    SlipDraw3DTransformFn transform, SlipDraw3DProjectFn projectDepth, void *userData, int depthClipCarry,
    int screenClipCarry, SlipTrackWorldPrimitiveBoundsCall *calls, size_t callCapacity,
    SlipTrackWorldPrimitiveBoundsVisit *visits, size_t visitCapacity, size_t *visitCount,
    SlipTrackWorldPrimitiveBoundsEvaluatedVisit *evaluatedVisits, size_t evaluatedVisitCapacity,
    SlipDraw3DReturnActiveVisit *returnVisits, size_t returnVisitCapacity, SlipDraw3DActiveRingVisit *activeVisits,
    size_t activeVisitCapacity, SlipDraw3DActiveBoundsVisit *boundsVisits, size_t boundsVisitCapacity,
    const SlipView3DMatrix *matrix, SlipTrackWorldPrimitiveBoundsEvaluated *result);

bool SlipTrackWorld_PrimitiveBoundsEvaluatedExecute(
    const uint8_t *list, size_t listBytesRemaining, uint32_t renderContextCount, uint32_t primaryLeft,
    uint32_t primaryTop, uint32_t primaryRight, uint32_t primaryBottom, uint32_t mode, const uint8_t *vertexCacheBase,
    size_t vertexCacheBytes, SlipView3DVec32 origin, SlipTrackWorldSourcePoint sourcePoint, SlipDraw3DRecordPool *pool,
    SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCount, const SlipDraw3DProjectState *state,
    SlipDraw3DTransformFn transform, SlipDraw3DProjectFn projectDepth, SlipDraw3DProjectFn projectScreen,
    void *userData, int hasPostPlanes, const uint8_t *planeBase, size_t planeBytes, uint32_t planeHeadOffset,
    int32_t postLimitXMin, int32_t postLimitXMax, int32_t postLimitYMin, int32_t postLimitYMax,
    size_t maxClipEdgeVisits, SlipTrackWorldPrimitiveBoundsCall *calls, size_t callCapacity,
    SlipTrackWorldPrimitiveBoundsVisit *visits, size_t visitCapacity, size_t *visitCount,
    SlipTrackWorldPrimitiveBoundsEvaluatedExecuteVisit *evaluatedVisits, size_t evaluatedVisitCapacity,
    SlipDraw3DReturnActiveVisit *returnVisits, size_t returnVisitCapacity, SlipDraw3DClipFlagVisit *clipFlagVisits,
    size_t clipFlagVisitCapacity, SlipDraw3DPostPlaneBoundsVisit *postBoundsVisits, size_t postBoundsVisitCapacity,
    SlipDraw3DPostPlaneClipRecordVisit *postClipRecordVisits, size_t postClipRecordVisitCapacity,
    SlipDraw3DPostPlaneClipPlaneVisit *postClipPlaneVisits, size_t postClipPlaneVisitCapacity,
    SlipDraw3DActiveRingVisit *activeVisits, size_t activeVisitCapacity, SlipDraw3DActiveBoundsVisit *boundsVisits,
    size_t boundsVisitCapacity, const SlipView3DMatrix *matrix, SlipTrackWorldPrimitiveBoundsEvaluatedExecute *result);

bool SlipTrackWorld_StoreRangePlane(uint32_t x, uint32_t y, uint32_t z, uint16_t inputX, uint16_t inputY,
                                    uint16_t inputZ, SlipTrackWorldRangePlane *result);

bool SlipTrackWorld_PointLookup(const uint8_t *componentList, const uint8_t *componentBase, size_t componentBaseBytes,
                                uint16_t pointIndex, uint32_t pointXOrInput, uint32_t pointYOrInput,
                                uint32_t pointZOrCountMergedWithInput, SlipTrackWorldPointLookup *result);

bool SlipTrackWorld_PrimitivePreGate(const uint8_t *record, size_t recordBytesRemaining, uint32_t rangeMode,
                                     const uint8_t *componentList, const uint8_t *componentBase,
                                     size_t componentBaseBytes, uint32_t pointInputX, uint32_t pointInputY,
                                     uint32_t pointInputZ, SlipView3DVec32 objectOffset,
                                     SlipView3DVec32 transformedPoint, SlipTrackWorldPrimitivePreGate *result);

bool SlipTrackWorld_PrimitiveDrawGate(const uint8_t *record, size_t recordBytesRemaining, uint32_t countAt,
                                      SlipTrackWorldPrimitiveDrawGateCall rejectionState,
                                      SlipTrackWorldPrimitiveDrawGate *result);

bool SlipTrackWorld_PrimitiveDrawGateEvaluated(
    const uint8_t *record, size_t recordBytesRemaining, uint32_t countAt, uint32_t mode,
    SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCount, SlipView3DVec32 origin,
    SlipTrackWorldSourcePoint sourcePoint, const SlipDraw3DProjectState *state, SlipDraw3DTransformFn transform,
    SlipDraw3DProjectMaskFn projectMask, void *userData, SlipDraw3DPolygonStatusVisit *visits, size_t visitCapacity,
    const SlipView3DMatrix *matrix, SlipTrackWorldPrimitiveDrawGateEvaluated *result);

bool SlipTrackWorld_LoadClipRegisters(uint32_t clipMinX, uint32_t clipMinY, uint32_t clipMaxX, uint32_t clipMaxY,
                                      SlipTrackWorldLoadClipRegisters *result);

bool SlipTrackWorld_PrimitiveRelatedScan(
    uint32_t primitiveToken, const uint8_t *componentRecord, size_t componentRecordBytes, uint32_t componentBaseAddress,
    uint32_t objectBaseAddress, uint32_t currentObjectAddress, uint8_t *objectList, size_t objectListBytes,
    uint32_t viewportMinX, uint32_t viewportMinY, uint32_t viewportMaxX, uint32_t viewportMaxY, uint32_t rangeMinX,
    uint32_t rangeMinY, uint32_t rangeMaxX, uint32_t rangeMaxY, const uint8_t *dispatchComponent,
    SlipTrackWorldPrimitiveRelatedScanCall clipBounds, SlipTrackWorldPrimitiveRelatedScanVisit *visits,
    size_t visitCapacity, size_t *visitCount, SlipTrackWorldPrimitiveRelatedScan *result);

bool SlipTrackWorld_PrimitiveRelatedScanExecute(
    uint32_t primitiveToken, const uint8_t *componentRecord, size_t componentRecordBytes, uint32_t componentBaseAddress,
    uint32_t objectBaseAddress, uint32_t currentObjectAddress, uint8_t *objectList, size_t objectListBytes,
    uint32_t viewportMinX, uint32_t viewportMinY, uint32_t viewportMaxX, uint32_t viewportMaxY, uint32_t rangeMinX,
    uint32_t rangeMinY, uint32_t rangeMaxX, uint32_t rangeMaxY, const uint8_t *component, uint32_t clipMinX,
    uint32_t clipMinY, uint32_t clipMaxX, uint32_t clipMaxY, SlipTrackWorldPrimitiveRelatedScanVisit *visits,
    size_t visitCapacity, size_t *visitCount, SlipTrackWorldPrimitiveRelatedScanExecute *result);

bool SlipTrackWorld_PrimitiveRangeState(const uint8_t *recordFrom, size_t recordBytesRemaining,
                                        uint32_t restoredPrimitiveToken, uint32_t rangeMode, uint32_t rangeFlag,
                                        uint32_t viewportMinX, uint32_t viewportMinY, uint32_t viewportMaxX,
                                        uint32_t viewportMaxY, uint32_t rangeMinX, uint32_t rangeMinY,
                                        uint32_t rangeMaxX, uint32_t rangeMaxY,
                                        SlipTrackWorldPrimitiveRangeStateCall callDraw3DPrimitivePath,
                                        SlipTrackWorldPrimitiveRangeState *result);

bool SlipTrackWorld_PrimitiveRangeStateEvaluated(
    const uint8_t *recordFrom, size_t recordBytesRemaining, uint32_t primitiveToken, uint32_t rangeMode,
    uint32_t rangeFlag, uint32_t viewportMinX, uint32_t viewportMinY, uint32_t viewportMaxX, uint32_t viewportMaxY,
    uint32_t rangeMinX, uint32_t rangeMinY, uint32_t rangeMaxX, uint32_t rangeMaxY, SlipDraw3DRecordPool *pool,
    SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCount, const SlipDraw3DProjectState *state,
    SlipDraw3DTransformFn transform, SlipDraw3DProjectFn projectDepth, void *userData, int depthClipCarry,
    int screenClipCarry, SlipDraw3DReturnActiveVisit *returnVisits, size_t returnVisitCapacity,
    SlipDraw3DActiveRingVisit *activeVisits, size_t activeVisitCapacity, SlipDraw3DActiveBoundsVisit *boundsVisits,
    size_t boundsVisitCapacity, SlipTrackWorldPrimitiveRangeStateEvaluated *result);

bool SlipTrackWorld_PrimitiveRangeStateEvaluatedExecute(
    const uint8_t *recordFrom, size_t recordBytesRemaining, uint32_t primitiveToken, uint32_t rangeMode,
    uint32_t rangeFlag, uint32_t viewportMinX, uint32_t viewportMinY, uint32_t viewportMaxX, uint32_t viewportMaxY,
    uint32_t rangeMinX, uint32_t rangeMinY, uint32_t rangeMaxX, uint32_t rangeMaxY, SlipDraw3DRecordPool *pool,
    SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCount, const SlipDraw3DProjectState *state,
    SlipDraw3DTransformFn transform, SlipDraw3DProjectFn projectDepth, SlipDraw3DProjectFn projectScreen,
    void *userData, int hasPostPlanes, const uint8_t *planeBase, size_t planeBytes, uint32_t planeHeadOffset,
    int32_t postLimitXMin, int32_t postLimitXMax, int32_t postLimitYMin, int32_t postLimitYMax,
    size_t maxClipEdgeVisits, SlipDraw3DReturnActiveVisit *returnVisits, size_t returnVisitCapacity,
    SlipDraw3DClipFlagVisit *clipFlagVisits, size_t clipFlagVisitCapacity,
    SlipDraw3DPostPlaneBoundsVisit *postBoundsVisits, size_t postBoundsVisitCapacity,
    SlipDraw3DPostPlaneClipRecordVisit *postClipRecordVisits, size_t postClipRecordVisitCapacity,
    SlipDraw3DPostPlaneClipPlaneVisit *postClipPlaneVisits, size_t postClipPlaneVisitCapacity,
    SlipDraw3DActiveRingVisit *activeVisits, size_t activeVisitCapacity, SlipDraw3DActiveBoundsVisit *boundsVisits,
    size_t boundsVisitCapacity, SlipTrackWorldPrimitiveRangeStateEvaluatedExecute *result);

bool SlipTrackWorld_PrimitiveNestedObjectCall(uint32_t componentViewX, uint32_t componentViewY, uint32_t componentViewZ,
                                              uint32_t savedObjectWorldX, uint32_t savedObjectWorldY,
                                              uint32_t savedObjectWorldZ, uint32_t componentRecord,
                                              uint32_t currentComponentToken, uint32_t savedCurrentObjectAddress,
                                              uint32_t savedRangeFlag, uint32_t renderContextCount,
                                              SlipTrackWorldPrimitiveNestedObjectCall *result);

bool SlipTrackWorld_PrimitiveDrawEpilogue(uint32_t restoredRangeMaxY, uint32_t restoredRangeMaxX,
                                          uint32_t restoredRangeMinY, uint32_t restoredRangeMinX,
                                          uint32_t restoredClipMaxY, uint32_t restoredClipMaxX,
                                          uint32_t restoredClipMinY, uint32_t restoredClipMinX,
                                          uint32_t restoredPrimitiveToken, uint32_t restoredComponentToken,
                                          SlipTrackWorldPrimitiveDrawEpilogue *result);

bool SlipTrackWorld_PrimitiveDrawAdvance(const uint8_t *record, size_t recordBytesRemaining,
                                         size_t recordOffsetAfterAdvance, uint16_t remainingCountBefore,
                                         SlipTrackWorldPrimitiveDrawAdvance *result);

bool SlipTrackWorld_PrimitiveOuterTail(uint32_t restoredPrimitiveToken, uint32_t rangeMode,
                                       SlipTrackWorldPrimitiveOuterTail *result);

bool SlipTrackWorld_PrimitiveOuterTailExecute(uint32_t primitiveToken, uint32_t rangeMode, uint32_t vertexBufferCursor,
                                              const SlipDraw3DStateRecord *drawStateRecord,
                                              SlipTrackWorldPrimitiveOuterTailExecute *result);

bool SlipTrackWorld_ChildListDispatch(const uint8_t *componentBefore, size_t componentBytesRemaining,
                                      const uint8_t *componentBase, size_t componentBaseBytes,
                                      SlipTrackWorldChildListDispatch *result);

bool SlipTrackWorld_ChildListDispatchExecute(const uint8_t *component, size_t componentBytesRemaining,
                                             const uint8_t *componentBase, size_t componentBaseBytes,
                                             SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCapacity,
                                             SlipTrackWorldChildListDispatchExecute *result);

bool SlipTrackWorld_DeferredListSetup(uint32_t frameRenderFlags, uint32_t viewportMinX, uint32_t viewportMinY,
                                      uint32_t viewportMaxX, uint32_t viewportMaxY, const uint8_t *deferredList,
                                      size_t listBytes, SlipTrackWorldDeferredListSetup *result);

bool SlipTrackWorld_DeferredItemPrologue(uint32_t savedRemainingCount, const uint8_t *savedCursor,
                                         size_t cursorBytesRemaining, const uint8_t *inputEntry,
                                         size_t entryBytesRemaining, uint32_t viewportMinX, uint32_t viewportMinY,
                                         uint32_t viewportMaxX, uint32_t viewportMaxY,
                                         SlipTrackWorldDeferredItemPrologue *result);

bool SlipTrackWorld_DeferredItemDirect(
    uint32_t x, uint32_t y, uint32_t z, const uint8_t *record, size_t recordBytesRemaining,
    uint32_t componentBaseAddress, const uint8_t *componentBase, size_t componentBaseBytes, uint16_t renderContextIndex,
    uint32_t drawFlags, uint32_t textureMode, uint32_t shading, int32_t componentDistance, uint32_t shadingSecondary,
    int32_t componentRadius, const uint8_t *specialRecord, uint32_t globalAfter, uint16_t randomState, uint32_t shadows,
    uint32_t processedComponentCount, uint32_t drawStateIndex, uint16_t actorReplayMode, uint32_t ambientLightScaleQ14,
    uint32_t scaledLightX, uint32_t scaledLightY, uint32_t scaledLightZ, uint32_t directLightScaleQ14,
    uint32_t renderContextCount, SlipTrackWorldPrimitiveCallback primitiveCallback,
    SlipTrackWorldDirectCallbackFunction callbackFunction, void *callbackUserData,
    SlipTrackWorldComponentActorDraw componentActorDraw, void *componentActorDrawUserData,
    SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCapacity, SlipTrackWorldDrawStateLoad drawStateLoad,
    void *drawStateLoadUserData, SlipTrackWorldBuildVertexRecords buildVertexRecords, void *buildVertexRecordsUserData,
    SlipTrackWorldRestoreVertexBuffer restoreVertexBuffer, void *restoreVertexBufferUserData,
    uint32_t savedMaximumDepth, uint32_t savedFadeStart, uint32_t savedFadeEnd, uint32_t savedFadeColour,
    const uint8_t *restoredCursor, uint32_t restoredRemainingCount, SlipTrackWorldComponentLight scaledLight,
    SlipTrackWorldComponentLight restoreLight, void *lightUserData, const SlipTrackWorldComponentRefuelCalls *refuel,
    SlipTrackWorldDeferredItemDirect *result);

bool SlipTrackWorld_DeferredListDirectExecute(
    uint32_t inputFrameRenderFlags, uint32_t viewportMinX, uint32_t viewportMinY, uint32_t viewportMaxX,
    uint32_t viewportMaxY, const uint8_t *deferredList, size_t listBytes, const SlipTrackWorldDosAddressMap *entryMap,
    size_t entryMapCount, const SlipTrackWorldDosAddressMap *recordMap, size_t recordMapCount,
    uint32_t componentBaseAddress, const uint8_t *componentBase, size_t componentBaseBytes, uint16_t renderContextIndex,
    uint32_t mode, uint32_t minDepth, int32_t detailThreshold, uint32_t currentRecordIndex,
    SlipView3DMatrix *worldMatrix, SlipView3DMatrix *objectViewMatrix, const SlipView3DMatrix *viewMatrix,
    uint32_t cameraWorldX, uint32_t cameraWorldZ, SlipTrackWorldSphereCull sphereCull, void *sphereCullUserData,
    SlipTrackWorldShapeDraw shapeDraw, void *shapeDrawUserData, uint32_t visibleRecordCount, uint32_t drawFlagsFrom,
    uint32_t textureMode, uint32_t shading, int32_t componentDistance, uint32_t shadingSecondary,
    int32_t componentRadius, uint32_t frameRenderFlags, uint32_t savedMaximumDepth, uint32_t savedFadeStart,
    uint32_t savedFadeEnd, uint32_t savedFadeColour, const uint8_t *specialRecord, uint32_t globalAfter,
    uint16_t randomState, uint32_t shadows, uint32_t processedComponentCount, uint16_t actorReplayMode,
    uint32_t ambientLightScaleQ14, uint32_t scaledLightX, uint32_t scaledLightY, uint32_t scaledLightZ,
    uint32_t directLightScaleQ14, uint32_t renderContextCount, SlipTrackWorldPrimitiveCallback primitiveCallback,
    SlipTrackWorldStoreClipBoundsFunction storeClipBoundsFunction, void *storeClipBoundsUserData,
    SlipTrackWorldDirectCallbackFunction callbackFunction, void *callbackUserData,
    SlipTrackWorldComponentActorDraw componentActorDraw, void *componentActorDrawUserData,
    SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCapacity, SlipTrackWorldDrawStateLoad drawStateLoad,
    void *drawStateLoadUserData, SlipTrackWorldBuildVertexRecords buildVertexRecords, void *buildVertexRecordsUserData,
    SlipTrackWorldRestoreVertexBuffer restoreVertexBuffer, void *restoreVertexBufferUserData,
    SlipTrackWorldDeferredListDirectExecuteVisit *visits, size_t visitCapacity,
    SlipTrackWorldComponentLight scaledLight, SlipTrackWorldComponentLight restoreLight, void *lightUserData,
    const SlipTrackWorldComponentRefuelCalls *refuel, SlipTrackWorldDeferredListDirectExecute *result);

bool SlipTrackWorld_DeferredMembership(const uint8_t *deferredList, size_t listBytes, uint32_t recordPointerToFind,
                                       const uint8_t *scanBase, size_t scanBytes,
                                       SlipTrackWorldDeferredMembership *result);

bool SlipTrackWorld_DeferredCallbackGate(const uint8_t *record, size_t recordBytesRemaining, uint32_t recordAddress,
                                         SlipView3DVec32 after, const uint8_t *objectList, size_t objectListBytes,
                                         uint32_t deferredEntryActive, uint32_t defaultTraversalGate,
                                         uint16_t renderContextIndex, uint32_t componentBaseAddress,
                                         const uint8_t *componentBase, size_t componentBaseBytes,
                                         uint32_t renderContextCount, SlipTrackWorldDeferredCallbackGate *result);

bool SlipTrackWorld_DeferredContinuationGate(uint16_t renderContextIndex, const uint8_t *objectList,
                                             size_t objectListBytes, uint32_t primaryLeft, uint32_t primaryTop,
                                             uint32_t primaryRight, uint32_t primaryBottom,
                                             SlipTrackWorldDeferredContinuationGate *result);

bool SlipTrackWorld_DeferredCullGate(const uint8_t *record, size_t recordBytesRemaining, uint32_t componentBaseAddress,
                                     const uint8_t *componentBase, size_t componentBaseBytes, const uint8_t *savedEntry,
                                     uint32_t savedMaximumDepth, bool trackWorldCullBoundsCarry, uint32_t viewportMinX,
                                     uint32_t viewportMinY, uint32_t viewportMaxX, uint32_t viewportMaxY,
                                     SlipTrackWorldDeferredCullGate *result);

bool SlipTrackWorld_DeferredEntryWrite(uint8_t *entryBytes, size_t entryBytesRemaining, uint8_t *objectList,
                                       size_t objectListBytes, uint32_t componentViewX, uint32_t componentViewY,
                                       uint32_t componentViewZ, uint32_t recordAddress, uint32_t primaryLeft,
                                       uint32_t primaryTop, uint32_t primaryRight, uint32_t primaryBottom,
                                       uint32_t objectListCursor, SlipTrackWorldDeferredEntryWrite *result);

bool SlipTrackWorld_DeferredAppendTail(uint8_t *deferredList, size_t listBytes, uint32_t scanBaseAddress,
                                       uint8_t *scanBase, size_t scanBytes, uint32_t storedEntryAddress,
                                       uint32_t storedRecordAddress, SlipTrackWorldDeferredAppendTail *result);

bool SlipTrackWorld_DeferredExistingEntry(bool enter, uint8_t *savedEntry, size_t entryBytesRemaining,
                                          const uint8_t *record, size_t recordBytesRemaining,
                                          uint32_t componentBaseAddress, const uint8_t *componentBase,
                                          size_t componentBaseBytes, const uint8_t *objectList, size_t objectListBytes,
                                          uint32_t renderContextCount, uint32_t primaryLeft, uint32_t primaryTop,
                                          uint32_t primaryRight, uint32_t primaryBottom, bool trackWorldCullBoundsCarry,
                                          uint32_t viewportMinX, uint32_t viewportMinY, uint32_t viewportMaxX,
                                          uint32_t viewportMaxY, SlipTrackWorldDeferredExistingEntry *result);

bool SlipTrackWorld_DeferredCallbackHeader(uint32_t deferredEntryActive, uint32_t renderContextCount, uint32_t viewX,
                                           uint32_t viewY, uint32_t viewZ, const uint8_t *objectList,
                                           size_t objectListBytes, uint32_t primaryLeft, uint32_t primaryTop,
                                           uint32_t primaryRight, uint32_t primaryBottom,
                                           SlipTrackWorldDeferredCallbackHeader *result);

bool SlipTrackWorld_DeferredCallbackCull(const uint8_t *record, size_t recordBytesRemaining, uint32_t savedMaximumDepth,
                                         uint32_t viewX, uint32_t viewY, uint32_t viewZ,
                                         bool trackWorldIndirectCullCarry, uint32_t viewportMinX, uint32_t viewportMinY,
                                         uint32_t viewportMaxX, uint32_t viewportMaxY,
                                         SlipTrackWorldDeferredCallbackCull *result);

bool SlipTrackWorld_DeferredCallbackWrite(uint8_t *inputEntry, size_t entryBytesRemaining, uint32_t entryAddress,
                                          uint8_t *objectList, size_t objectListBytes, uint8_t *inputDeferredList,
                                          size_t listBytes, uint32_t scanBaseAddress, uint8_t *scanBase,
                                          size_t scanBytes, uint32_t x, uint32_t y, uint32_t z, uint32_t recordAddress,
                                          uint32_t primaryLeft, uint32_t primaryTop, uint32_t primaryRight,
                                          uint32_t primaryBottom, uint32_t objectListCursor,
                                          SlipTrackWorldDeferredCallbackWrite *result);

bool SlipTrackWorld_TableLookup(const uint8_t *table, size_t tableBytes, uint16_t cellX, uint16_t cellY, uint16_t cellZ,
                                SlipTrackWorldTableLookup *result);

bool SlipTrackWorld_OrientedComponentTest(const uint8_t *componentBase, size_t componentBaseBytes,
                                          uint32_t componentBaseAddress, uint16_t componentOffset, int32_t relativeX,
                                          int32_t relativeY, int32_t relativeZ,
                                          SlipTrackWorldOrientedComponentTest *result);

bool SlipTrackWorld_OrientedRecordTest(const uint8_t *trdBase, size_t trackDataSize, uint32_t trdBaseAddress,
                                       const uint8_t *componentBase, size_t componentBaseBytes,
                                       uint32_t componentBaseAddress, uint32_t recordAddress, int32_t positionX,
                                       int32_t positionY, int32_t positionZ,
                                       SlipTrackWorldOrientedComponentTest *result);

bool SlipTrackWorld_OrientedRecordSearch(const uint8_t *trdBase, size_t trackDataSize, uint32_t trdBaseAddress,
                                         const uint8_t *componentBase, size_t componentBaseBytes,
                                         uint32_t componentBaseAddress, const uint8_t *table, size_t tableBytes,
                                         int32_t positionX, int32_t positionY, int32_t positionZ,
                                         uint32_t recordAddress, SlipTrackWorldOrientedRecordSearch *result);

bool SlipTrackWorld_PositiveRecordSearch(const uint8_t *trdBase, size_t trackDataSize, uint32_t trdBaseAddress,
                                         const uint8_t *componentBase, size_t componentBaseBytes,
                                         uint32_t componentBaseAddress, const uint8_t *table, size_t tableBytes,
                                         int32_t positionX, int32_t positionY, int32_t positionZ,
                                         uint32_t recordAddress, SlipTrackWorldPositiveRecordSearch *result);

bool SlipTrackWorld_UpdateSlotRecord(uint8_t *inputSlot, const SlipObject *objectTable, size_t objectTableBytes,
                                     const uint8_t *trdBase, size_t trackDataSize, uint32_t trdBaseAddress,
                                     const uint8_t *componentBase, size_t componentBaseBytes,
                                     uint32_t componentBaseAddress, const uint8_t *table, size_t tableBytes);

void SlipTrackWorld_UpdateSlots(uint32_t trackHandle, uint8_t *slotDrawBase, size_t slotDrawBytes,
                                SlipObjectDrawCallback *slotDrawCallbacks, size_t slotDrawCallbackCount,
                                uint32_t slotDrawBaseAddress, uint32_t slotDrawFreeListAddress, uint8_t *slotListBase,
                                size_t slotListBytes, uint32_t slotListBaseAddress, uint32_t slotListSentinelAddress,
                                uint8_t *trdBase, size_t trackDataSize, uint32_t trdBaseAddress,
                                const uint8_t *componentBase, size_t componentBaseBytes, uint32_t componentBaseAddress,
                                const uint8_t *table, size_t tableBytes, SlipObject *objectTable,
                                size_t objectTableBytes);

bool SlipTrackWorld_QuerySlotCollision(uint8_t *inputSlot, const SlipObject *objectTable, size_t objectTableBytes,
                                       const uint8_t *trdBase, size_t trackDataSize, uint32_t trdBaseAddress,
                                       const uint8_t *componentBase, size_t componentBaseBytes,
                                       uint32_t componentBaseAddress, const uint8_t *table, size_t tableBytes,
                                       SlipTrackWorldSlotCollisionState *registers);

bool SlipTrackWorld_QueryObjectCollision(uint32_t collisionValue, uint16_t objectOffset, uint8_t *slotListBase,
                                         uint32_t slotListBaseAddress, const SlipObject *objectTable,
                                         size_t objectTableBytes, const uint8_t *trdBase, size_t trackDataSize,
                                         uint32_t trdBaseAddress, const uint8_t *componentBase,
                                         size_t componentBaseBytes, uint32_t componentBaseAddress, const uint8_t *table,
                                         size_t tableBytes);

bool SlipTrackWorld_RecordSearch(const uint8_t *trdBase, size_t trdBytes, const uint8_t *componentBase,
                                 size_t componentBaseBytes, const uint8_t *table, size_t tableBytes,
                                 uint32_t trdBaseAddress, uint32_t initialRecordAddress, int32_t objectX,
                                 int32_t objectY, int32_t objectZ, SlipTrackWorldRecordSearch *result);

bool SlipTrackWorld_PreFrameBuild(SlipTrackBeamState *beams, const uint8_t *trackData, size_t trackBytes,
                                  const uint8_t *components, size_t componentBytes, const uint8_t *cellTable,
                                  size_t cellTableBytes, uint32_t trackBase);

bool SlipTrackWorld_TableStore(uint8_t *table, size_t tableBytes, uint32_t initialCellIndex, uint16_t cellY,
                               uint16_t cellZ, uint32_t tableValue, SlipTrackWorldTableStore *result);

bool SlipTrackWorld_RecordVector(const uint8_t *trkBase, size_t trkSize, uint16_t coordinateOffset,
                                 uint32_t savedParameter, SlipTrackWorldRecordVector *result);

bool SlipTrackWorld_CellTableBuild(uint8_t *table, size_t tableBytes, const uint8_t *trkBase, size_t trkSize,
                                   uint32_t savedVectorParameter, uint32_t tableValueBase,
                                   SlipTrackWorldCellTableVisit *visits, size_t visitCapacity,
                                   SlipTrackWorldCellTableBuild *result);

bool SlipTrackWorld_FrameDispatch(uint32_t renderContext, uint32_t primaryLeft, uint32_t primaryTop,
                                  uint32_t primaryRight, uint32_t primaryBottom, uint16_t renderMode,
                                  uint32_t underSeaColor, SlipTrackWorldTraversalCallback traversalCallback,
                                  uint32_t savedMaximumDepth, uint32_t savedFadeStart, uint32_t savedFadeEnd,
                                  uint32_t savedFadeColour, SlipRaceTrackFrameCallback frameCallback,
                                  uint32_t defaultTraversalGate, uint8_t *trkBase, size_t trkSize,
                                  SlipTrackWorldTraversalContext *traversalContext,
                                  const SlipRaceTrackFrameCallbackExecuteArgs *callbackArgs,
                                  SlipTrackWorldFrameDispatch *result);

extern uint32_t SlipTrackWorld_refuelInitialized;

bool SlipTrackWorld_InitRefuel(uint32_t *initialized, uint32_t *refuelOffset, uint16_t trackHandle, uint8_t *trd,
                               size_t trdBytes, uint32_t trdAddress, const uint8_t *trc, size_t trcBytes,
                               const uint8_t *materials, size_t materialBytes);

#endif
