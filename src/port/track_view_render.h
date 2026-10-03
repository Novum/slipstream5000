#ifndef SLIPSTREAM5000_TRACK_VIEW_RENDER_H
#define SLIPSTREAM5000_TRACK_VIEW_RENDER_H

#include "draw3d.h"
#include "resource.h"
#include "track_world.h"
#include "view3d.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
	SLIP_TRACK_VIEW_RESOURCE_HANDLE_CAPACITY = 512,
	SLIP_TRACK_VIEW_BSP_GATE_TRACE_CAPACITY = 32,
	SLIP_TRACK_VIEW_PRIMITIVE_RANGE_TRACE_CAPACITY = 32,
	SLIP_TRACK_VIEW_DRAW_GATE_TRACE_CAPACITY = 64,
	SLIP_TRACK_DIAGNOSTIC_ACTIVE_VERTEX_LIMIT = 8
};

struct TrackViewRawBspContext;

typedef struct TrackViewImpactSprites {
	uint16_t count;
	const uint16_t *handles;
} TrackViewImpactSprites;

void TrackView_SetImpactSprites(const TrackViewImpactSprites *sprites);
bool TrackView_DrawBeam(struct TrackViewRawBspContext *context, uint32_t beamIndex);

typedef bool (*TrackViewReplayCallback)(struct TrackViewRawBspContext *context);

extern const SlipRaceTrackFrameCallback g_trackViewFrameCallbacks[];

typedef struct TrackViewResourceHandleEntry {
	char name[SLIP_RESOURCE_NAME_BUFFER_BYTES];
	uint32_t resourceHandle;
	SlipResourcePayload payload; /* Host binding of this named resource reference. */
} TrackViewResourceHandleEntry;

/* Host representation conversion of the actual typed material resource. */
void TrackView_MaterialBytes(uint8_t *bytes, const SlipDraw3DMaterialTable *table);

typedef struct TrackViewResourceHandleRegistry {
	const char *const *archives;
	size_t archiveCount;
	TrackViewResourceHandleEntry entries[SLIP_TRACK_VIEW_RESOURCE_HANDLE_CAPACITY];
	size_t entryCount;
	bool hostResources;
} TrackViewResourceHandleRegistry;

void TrackView_ReleaseResource(void *registry, uint32_t handle);
void TrackView_ReleaseSequence(TrackViewResourceHandleRegistry *registry, const uint16_t *handles, uint16_t count);

bool TrackView_LoadResourceHandlePayload(const TrackViewResourceHandleRegistry *registry, uint32_t handle,
                                         SlipResourcePayload *payload);

typedef struct TrackViewMaterialInit {
	uint16_t cage;
	uint16_t roadLine;
	uint32_t roadLineValue;
	uint16_t orangeLight;
	uint32_t orangeLightLow;
	uint32_t orangeLightHigh;
	uint16_t floorLight;
	uint32_t floorLightLow;
	uint32_t floorLightHigh;
	uint16_t blueLight;
	uint32_t blueLightLow;
	uint32_t blueLightHigh;
	uint16_t whiteLight;
	uint16_t yellow;
	uint32_t yellowValue;
} TrackViewMaterialInit;

void TrackView_MaterialInit(const uint8_t *materialTable, size_t materialTableBytes, uint16_t materialGlobal,
                            TrackViewMaterialInit *result);

int TrackView_ResolveComponentMaterials(uint8_t *componentBase, size_t componentBaseBytes, const uint8_t *materialTable,
                                        size_t materialTableBytes, uint16_t materialGlobal,
                                        TrackViewMaterialInit *result);

void TrackView_MaterialAnimationTick(void);

int TrackView_FindNameRecord(void *user, const char name[SLIP_RESOURCE_NAME_BUFFER_BYTES], uint32_t *handle);
int TrackView_LoadNamedResource(void *user, const char name[SLIP_RESOURCE_NAME_BUFFER_BYTES], uint32_t *resourceHandle);
int TrackView_FindNamedResource(void *user, const char name[SLIP_RESOURCE_NAME_BUFFER_BYTES], uint32_t *resourceHandle);

typedef struct TrackViewChunkCallbackContext {
	const SlipView3DMatrix *viewMatrix;
	SlipView3DVec32 offset;
	SlipView3DVec32 currentChunkOrigin;
} TrackViewChunkCallbackContext;

typedef struct TrackViewRawBspGateTrace {
	uint32_t callbackIndex;
	uint32_t recordToken;
	uint16_t recordOffset;
	uint16_t recordKind;
	int32_t side;
	uint32_t objectCountBefore;
	uint32_t deferredCountBefore;
	uint32_t deferredEntryActiveBefore;
	uint32_t deferredEntryActiveAfter;
	uint32_t matchedRecordToken;
	uint32_t deferredCountAfter;
	SlipTrackWorldDeferredCallbackGateBranch branch;
	bool match;
	bool appended;
} TrackViewRawBspGateTrace;

typedef struct TrackViewPrimitiveRangeTrace {
	uint32_t objectAddress;
	uint32_t componentAddress;
	uint32_t primitiveAddress;
	uint32_t relatedObjectAddress;
	uint32_t counter;
	uint32_t rangeFlag;
	uint32_t objectListCountBefore;
	uint32_t objectListCountAfter;
	uint32_t rangeMinXBefore;
	uint32_t rangeMinYBefore;
	uint32_t rangeMaxXBefore;
	uint32_t rangeMaxYBefore;
	uint32_t rangeMinXAfter;
	uint32_t rangeMinYAfter;
	uint32_t rangeMaxXAfter;
	uint32_t rangeMaxYAfter;
	uint32_t primitivePathCarry;
	uint32_t primitivePathBoundsMinX;
	uint32_t primitivePathBoundsMinY;
	uint32_t primitivePathBoundsMaxX;
	uint32_t primitivePathBoundsMaxY;
	uint32_t activeRingVisitCount;
	uint16_t activeRingIndex[SLIP_TRACK_DIAGNOSTIC_ACTIVE_VERTEX_LIMIT];
	uint32_t activeRingFlags[SLIP_TRACK_DIAGNOSTIC_ACTIVE_VERTEX_LIMIT];
	int32_t activeRingSourceX[SLIP_TRACK_DIAGNOSTIC_ACTIVE_VERTEX_LIMIT];
	int32_t activeRingSourceY[SLIP_TRACK_DIAGNOSTIC_ACTIVE_VERTEX_LIMIT];
	int32_t activeRingSourceZ[SLIP_TRACK_DIAGNOSTIC_ACTIVE_VERTEX_LIMIT];
	int32_t activeRingWorldX[SLIP_TRACK_DIAGNOSTIC_ACTIVE_VERTEX_LIMIT];
	int32_t activeRingWorldY[SLIP_TRACK_DIAGNOSTIC_ACTIVE_VERTEX_LIMIT];
	int32_t activeRingWorldZ[SLIP_TRACK_DIAGNOSTIC_ACTIVE_VERTEX_LIMIT];
	int32_t activeRingScreenX[SLIP_TRACK_DIAGNOSTIC_ACTIVE_VERTEX_LIMIT];
	int32_t activeRingScreenY[SLIP_TRACK_DIAGNOSTIC_ACTIVE_VERTEX_LIMIT];
	uint32_t nestedExcludedObjectToken;
	uint32_t nestedClipBoundsSuppressed;
	uint32_t relatedBranch;
	uint32_t rangeBranch;
	uint32_t nestedCall;
} TrackViewPrimitiveRangeTrace;

typedef struct TrackViewPrimitiveDrawGateTrace {
	uint32_t objectAddress;
	uint32_t primitiveAddress;
	uint16_t planeVertexIndex;
	uint16_t planeNormalX;
	uint16_t planeNormalY;
	uint16_t planeNormalZ;
	int32_t originX;
	int32_t originY;
	int32_t originZ;
	int32_t sourceX;
	int32_t sourceY;
	int32_t sourceZ;
	int32_t pointX;
	int32_t pointY;
	int32_t pointZ;
	int32_t relativeX;
	int32_t relativeY;
	int32_t relativeZ;
	int32_t planeX;
	int32_t planeY;
	int32_t planeZ;
	int64_t dotProduct;
	uint32_t allClipFlags;
	uint32_t anyClipFlags;
	uint32_t finalAllMask;
	uint32_t finalAnyMask;
	uint32_t planeRejected;
	uint32_t sign;
	uint32_t gateBranch;
} TrackViewPrimitiveDrawGateTrace;

typedef struct TrackViewRawBspContext {
	const uint8_t *chunkBase;
	size_t chunkBaseBytes;
	uint32_t chunkBaseToken;
	SlipView3DVec32 componentViewOrigin;
	const uint8_t *componentBase;
	size_t componentBaseBytes;
	uint32_t componentBaseToken;
	const uint8_t *materialTable;
	size_t materialTableBytes;
	const uint8_t *exactMaterialTable;
	size_t exactMaterialTableBytes;
	uint16_t materialGlobal;
	uint32_t materialFrameIndex;
	SlipDraw3DRecordPool *drawRecordPool;
	SlipDraw3DProjectState *projectState;
	SlipDraw3DStateRecord *drawStateRecord;
	const TrackViewResourceHandleRegistry *resourceRegistry;
	uint32_t postPlaneHead;
	uint32_t postPlaneColor;
	uint32_t cameraLightX;
	uint32_t cameraLightY;
	uint32_t cameraLightZ;
	uint32_t postPlaneScale;
	int32_t postPlanePointX;
	int32_t postPlanePointY;
	int32_t postPlanePointZ;
	int32_t postPlaneNormalX;
	int32_t postPlaneNormalY;
	int32_t postPlaneNormalZ;
	int32_t postLimitXMin;
	int32_t postLimitYMin;
	int32_t postLimitXMax;
	int32_t postLimitYMax;
	uint8_t *deferredList;
	size_t deferredListBytes;
	uint8_t *deferredScan;
	size_t deferredScanBytes;
	uint32_t deferredScanBaseToken;
	uint8_t *objectList;
	size_t objectListBytes;
	uint32_t objectListBaseToken;
	SlipDraw3DVertexRecord *vertexRecords;

	struct SlipRendererState *hostRenderer;
	uint16_t hostShapeResource;

	size_t vertexRecordCount;

	SlipDraw3DVertexRecord *vertexBufferBase;
	size_t vertexBufferRecordCapacity;
	uint32_t vertexBufferCursor;
	uint32_t vertexBufferLimit;
	SlipDraw3DStateRecord *drawStateRecords;
	size_t drawStateRecordCount;
	TrackViewChunkCallbackContext chunkCallbacks;
	SlipView3DVec32 origin;
	SlipTrackWorldProjectFrustum frustum;

	SlipDraw3DTransformFn transform;
	SlipDraw3DSourcePointFn sourcePoint;
	uint16_t mask;
	uint32_t deferredEntryActive;
	uint32_t defaultTraversalGate;
	uint32_t useFullObjectViewport;
	uint32_t excludedRelatedObjectToken;
	uint32_t nestedClipBoundsSuppressed;
	uint32_t renderContextCount;
	uint32_t primaryLeft;
	uint32_t primaryTop;
	uint32_t primaryRight;
	uint32_t primaryBottom;
	uint32_t viewportMinX;
	uint32_t viewportMinY;
	uint32_t viewportMaxX;
	uint32_t viewportMaxY;
	uint32_t rangeMinX;
	uint32_t rangeMinY;
	uint32_t rangeMaxX;
	uint32_t rangeMaxY;
	uint32_t savedMaximumDepth;
	uint32_t flaggedPrimitiveReject;
	uint32_t textureMode;
	uint32_t textureScrollPhase;
	uint32_t affineDepthThreshold;
	uint32_t farTextureDepth;
	uint32_t shading;
	int32_t componentDistance;
	uint32_t shadingSecondary;
	int32_t componentRadius;
	uint32_t shadows;
	uint32_t processedComponentCount;
	uint32_t recordIndex;
	uint32_t shapeProjectionFlags;
	uint32_t rendererFlags;
	uint32_t reverseTraversal;
	SlipView3DVec32 lightInput;
	uint32_t directLight;
	uint32_t ambientLight;
	uint32_t limitEnabled;
	uint32_t limitStart;
	uint32_t limitEnd;
	const uint8_t *specialRecord;
	uint32_t beamSection;
	uint16_t linkedComponentDrawArgument;
	uint32_t ambientLightScaleQ14;
	uint32_t scaledLightX;
	uint32_t scaledLightY;
	uint32_t scaledLightZ;
	uint32_t directLightScaleQ14;
	uint32_t cameraWorldX;
	uint32_t cameraWorldY;
	uint32_t cameraWorldZ;
	uint32_t materialDepthBase;
	uint32_t materialDepthIndex;
	uint32_t materialSetupSeedY;
	uint32_t materialPlanePointX;
	uint32_t materialPlanePointY;
	uint32_t materialPlanePointZ;
	uint32_t materialPlaneNormalX;
	uint32_t materialPlaneNormalY;
	uint32_t materialPlaneNormalZ;
	uint32_t materialDispatchState;
	uint32_t materialDeferredFlag;
	uint16_t materialDispatchMaterialIndex;
	uint32_t materialDispatchDepth;
	TrackViewReplayCallback replayCallback;
	uint16_t replayCount;
	const uint8_t *replayList;
	size_t replayListBytes;
	SlipObject *objectTable;
	size_t objectTableBytes;
	uint32_t objectTableBaseToken;
	uint8_t *slotDrawBase;
	size_t slotDrawBytes;
	uint32_t slotDrawBaseAddress;
	SlipObjectDrawCallback *slotDrawCallbacks;
	size_t slotDrawCallbackCount;
	uint8_t *slotListBase;
	size_t slotListBytes;
	uint32_t slotListBaseAddress;

	uint8_t *articSlotPool;
	size_t articSlotPoolBytes;
	uint32_t articSlotPoolAddress;
	const SlipView3DMaths *maths;
	const uint8_t *trackCellTable;
	size_t trackCellTableBytes;
	const SlipView3DMatrix *cameraMatrix;

	uint8_t replayListBuffer[SLIP_TRACK_REPLAY_OBJECT_CAPACITY * sizeof(uint16_t)];

	const uint8_t *materialRecord;
	size_t materialRecordBytes;
	uint32_t materialColor;

	uint32_t detailLevel;
	SlipTrackWorldTraversalCallback traversalCallback;
	SlipTrackWorldPrimitiveCallback primitiveCallback;
	SlipTrackWorldRecordCallback recordCallback;
	uint32_t mode;
	uint32_t minDepth;
	uint32_t frameRenderFlags;
	uint32_t articMinimumLod;
	uint32_t articDrawChildren;
	int32_t articViewDepth;
	SlipView3DVec32 articActorPosition;
	uint32_t articSelectedLod;
	uint8_t *articActor;
	uint32_t articReplayLod;
	bool articSortCallback;
	uint32_t collisionBodyDraw;
	int32_t detailThreshold;
	uint32_t callbackCount;
	uint32_t traversalCallbackCount;
	uint32_t recordCallbackCount;
	uint32_t projectedIndexCount;
	uint32_t deferredGateCount;
	uint32_t deferredGateZeroCount;
	uint32_t deferredGateContinue;
	uint32_t deferredContinuationCount;
	uint32_t deferredCullCount;
	uint32_t deferredCullRejectCount;
	uint32_t deferredEntryWriteCount;
	uint32_t deferredAppendCount;
	uint32_t deferredExistingDirectEntryCount;
	uint32_t deferredExistingContinuationEntryCount;
	uint32_t deferredExistingClipBoundsCount;
	uint32_t componentGateCount;
	uint32_t componentProjectCount;
	uint32_t componentCullCount;
	uint32_t componentCullRejectCount;
	uint32_t componentSetupCount;
	uint32_t componentTailCount;
	uint32_t componentTailChildListCount;
	uint32_t componentTailNestedVisitCount;
	uint32_t componentTailVertexBuildCount;
	uint32_t primitiveWalkerCount;
	uint32_t primitiveWalkerDirectBranchCount;
	uint32_t directCallbackCount;
	uint32_t directCallbackDispatchCount;
	uint32_t directCallbackSkipCount;
	uint32_t directCallbackPlaneRejectedCount;
	uint32_t directCallbackGlobalGateRejectedCount;
	uint32_t directCallbackHighTexturedPathCount;
	uint32_t directCallbackLowMaterialCount;
	uint32_t directCallbackOtherMaterialCount;
	uint32_t directCallbackHighFrameCount;
	uint32_t directCallbackHighFarFallbackCount;
	uint32_t directCallbackHighTexturedEmitCount;
	uint32_t directCallbackHighTexturedRingCount;
	uint32_t directCallbackHighTexturedRingFailureStage;
	uint32_t directCallbackHighTexturedRingCarryCount;
	uint32_t directCallbackHighTexturedRingSignRejectCount;
	uint32_t directCallbackHighTexturedRingAllMaskedCount;
	uint32_t directCallbackHighTexturedRingClipDepthCarryCount;
	uint32_t directCallbackHighTexturedRingClipScreenCarryCount;
	uint32_t directCallbackHighTexturedRingLastStatusAny;
	uint32_t directCallbackHighTexturedRingLastStatusAll;
	uint32_t directCallbackHighTexturedRingLastAnyFlags;
	uint32_t directCallbackHighTexturedRingLastAllFlags;
	int32_t directCallbackHighTexturedRingRejectMinZ;
	int32_t directCallbackHighTexturedRingRejectMaxZ;
	uint32_t directCallbackHighTexturedRingRejectVisitCount;
	uint16_t directCallbackHighTexturedRingRejectFirstVertexIndex;
	uint32_t directCallbackHighFlatDispatchCount;
	uint32_t directCallbackHighShadedDispatchCount;
	uint32_t directCallbackHighLineDispatchCount;
	uint32_t directCallbackHighDitheredDispatchCount;
	uint32_t directCallbackHighDispatchDefaultCount;
	uint32_t directCallbackHighDispatchLastMode;
	uint32_t directCallbackHighTexturedDispatchCount;
	uint32_t directCallbackHighTextureLoadCount;
	uint32_t directCallbackHighRasterEntryCount;
	uint32_t directCallbackHighPixelsWrittenCount;
	uint32_t directCallbackRecordCount;
	uint32_t directCallbackActiveIndex;
	uint32_t directCallbackActiveRecordOffset;
	uint16_t directCallbackActivePolygonCountAndFlags;
	uint16_t directCallbackActiveMaterialIndex;
	uint16_t directCallbackActivePlaneIndex;
	uint8_t directCallbackActiveFlags;
	uint8_t directCallbackActiveMaterialFlags;
	uint32_t directCallbackActiveBranchMask;
	uint32_t emitPathCount;
	uint32_t emitPathMaterialGateCount;
	uint32_t emitPathSolidRingCount;
	uint32_t emitPathFlatDispatchCount;
	uint32_t emitPathRasterizedCount;
	uint32_t unsupportedTraversalCallbackCount;
	uint32_t callbackHeaderCount;
	uint32_t callbackHeaderContinueCount;
	uint32_t callbackCullCount;
	uint32_t callbackCullRejectCount;
	uint32_t callbackWriteCount;
	uint32_t objectContinuationCount;
	uint32_t objectChildListDispatchCount;
	uint32_t objectPrimitiveBoundsCount;
	uint32_t objectPrimitiveDrawLoopCount;
	uint32_t objectPrimitiveDrawPreSkipCount;
	uint32_t objectPrimitiveDrawGateCount;
	uint32_t objectPrimitiveDrawGateSkipCount;
	uint32_t objectPrimitiveRelatedScanCount;
	uint32_t objectPrimitiveRelatedScanToEpilogueCount;
	uint32_t objectPrimitiveRangeStateCount;
	uint32_t objectPrimitiveRangeStateToNestedCount;
	uint32_t objectPrimitiveRangeStateToEpilogueCount;
	uint32_t objectPrimitiveNestedPendingCount;
	uint32_t objectPrimitiveDrawAdvanceCount;
	uint32_t objectPrimitiveOuterTailCount;
	uint32_t continuedRecordTransformCount;
	uint32_t truncatedRecordTransformCount;
	uint32_t firstCallbackRecordOffset;
	uint16_t firstCallbackRecordKind;
	int32_t firstCallbackPlaneSide;
	uint16_t firstProjectedVertexIndex;
	SlipDraw3DVec32 firstProjectIndexWorld;
	SlipTrackWorldChunkDispatch firstChunkDispatch;
	SlipTrackWorldDeferredCallbackGate firstGate;
	SlipTrackWorldComponentGate firstComponentGate;
	SlipTrackWorldComponentProject firstComponentProject;
	SlipTrackWorldComponentSetup firstComponentSetup;
	SlipTrackWorldComponentTail firstComponentTail;
	SlipTrackWorldPrimitiveWalker firstPrimitiveWalker;
	SlipTrackWorldRecordVisibility firstRecordVisibility;
	TrackViewRawBspGateTrace gateTrace[SLIP_TRACK_VIEW_BSP_GATE_TRACE_CAPACITY];
	uint32_t gateTraceCount;
	TrackViewPrimitiveRangeTrace rangeTrace[SLIP_TRACK_VIEW_PRIMITIVE_RANGE_TRACE_CAPACITY];
	size_t drawGateTraceCount;
	TrackViewPrimitiveDrawGateTrace drawGateTrace[SLIP_TRACK_VIEW_DRAW_GATE_TRACE_CAPACITY];
	uint32_t rangeTraceCount;
	uint32_t failureAddress;
	bool haveFirstCallback;
	bool haveFirstProjectIndex;
	bool haveFirstGate;
	bool haveFirstComponentGate;
	bool haveFirstComponentProject;
	bool haveFirstComponentSetup;
	bool haveFirstComponentTail;
	bool haveFirstPrimitiveWalker;
	bool haveFirstRecordVisibility;
	bool failed;
} TrackViewRawBspContext;

bool TrackView_ExecuteSlotDrawCallback(TrackViewRawBspContext *context, uint32_t objectRecord);
bool TrackView_ExecuteDroneDrawCallback(TrackViewRawBspContext *context, uint32_t objectRecord);
bool TrackView_ExecuteArticDrawCallback(TrackViewRawBspContext *context, uint32_t objectRecord);
bool TrackView_ExecuteBonusDrawCallback(TrackViewRawBspContext *context, uint32_t objectRecord);
bool TrackView_DrawSprite(TrackViewRawBspContext *context, SlipView3DVec32 view, int32_t radius, uint16_t spriteHandle);
bool TrackView_QueueAnimatedEffect(TrackViewRawBspContext *context, uint32_t objectRecord);
bool TrackView_DrawAnimatedEffect(TrackViewRawBspContext *context, uint32_t objectRecord);
bool TrackView_DrawWeaponProjectile(TrackViewRawBspContext *context, uint32_t objectHandle);
bool TrackView_DrawShapeEffect(TrackViewRawBspContext *context, uint32_t objectRecord);
bool TrackView_QueueShapeEffect(TrackViewRawBspContext *context, uint32_t objectRecord);
bool TrackView_QueueTimedEffect(TrackViewRawBspContext *context, uint32_t objectRecord);
bool TrackView_QueueCrossEffect(TrackViewRawBspContext *context, uint32_t objectRecord);
bool TrackView_DrawCrossEffect(TrackViewRawBspContext *context, uint32_t objectRecord);
bool TrackView_DrawTimedEffect(TrackViewRawBspContext *context, uint32_t objectRecord);
extern TrackViewReplayCallback TrackView_replayCallback;
void TrackView_SetReplayCallback(TrackViewReplayCallback callback);
bool TrackView_ExecuteReplay(TrackViewRawBspContext *context);

bool TrackView_RawBspClassify(const uint8_t *bspNode, uint16_t planeVertexIndex, uint16_t normalX, uint16_t normalY,
                              uint16_t normalZ, void *userData, bool *planeRejected);

bool TrackView_RawBspCallback(const uint8_t *bspNode, uint32_t recordPayload, uint16_t recordKind, int32_t planeSide,
                              void *userData);
bool TrackView_BuildChunkVertexRecords(const uint8_t *vertexSource, size_t sourceBytes, uint16_t vertexCount,
                                       int16_t sourceStride, void *userData);
SlipDraw3DVec32 TrackView_ChunkTransformPoint(uint32_t sourceX, uint32_t sourceY, uint32_t sourceZ,
                                              SlipDraw3DVertexRecord *record, void *userData);
SlipView3DVec32 TrackView_ChunkSourcePoint(int16_t sourceX, int16_t sourceY, int16_t sourceZ, void *userData);
bool TrackView_RestoreChunkVertexBuffer(void *userData);
bool TrackView_BuildComponentVertexRecords(const uint8_t *vertexSource, size_t sourceBytes, uint16_t vertexCount,
                                           int16_t sourceStride, void *userData);
bool TrackView_RestoreComponentVertexBuffer(void *userData);
bool TrackView_DrawSlotRecord(TrackViewRawBspContext *context, uint32_t drawPayload);
bool TrackView_DrawComponentActors(uint32_t objectListOffset, const uint8_t *sectionRecord, uint32_t sectionRenderFlags,
                                   SlipView3DVec32 componentViewOrigin, void *userData);
bool TrackView_ChunkFallback(const uint8_t *chunkRecord, size_t chunkBytesRemaining, void *userData);

bool TrackView_DirectCallback(const uint8_t *primitiveRecord, size_t recordBytesRemaining, size_t recordOffset,
                              const SlipTrackWorldDirectCallbackEnvironment *environment, uint32_t callbackInput,
                              void *userData, uint32_t *callbackResult, bool *carryFromCallback);

bool TrackView_StoreClipBoundsCallback(uint32_t minX, uint32_t minY, uint32_t maxX, uint32_t maxY, void *userData);

bool TrackView_ObjectContinuation(const uint8_t *objectRecord, size_t objectBytesRemaining, uint32_t objectAddress,
                                  uint32_t counter, uint8_t *objectListEntry, size_t objectListEntryBytes,
                                  SlipView3DVec32 objectPosition, SlipView3DVec32 transformedObjectOffset,
                                  void *userData);

bool TrackView_BuildTrackMaterialTable(const char *const *archives, size_t archiveCount, uint8_t resourceIndexMinus,
                                       uint8_t **materialTable, size_t *materialTableBytes, uint16_t *materialGlobal,
                                       TrackViewResourceHandleRegistry *resourceRegistryOut);

void TrackView_VehicleViewResetActorState(void);
void TrackView_TrackGlobeResetActorState(void);

void TrackViewNormalizePrimitiveFlags(uint8_t *componentBase, size_t componentBaseBytes, const uint8_t *materialTable,
                                      size_t materialTableBytes, uint16_t materialGlobal, uint32_t materialFrameIndex,
                                      const TrackViewResourceHandleRegistry *resourceRegistry,
                                      const SlipView3DMaths *maths, SlipView3DVec32 light);

bool TrackViewDrawSceneryShape(TrackViewRawBspContext *context, const uint8_t *record, size_t recordBytes,
                               const SlipView3DMatrix *objectMatrix, const SlipView3DMatrix *viewMatrix,
                               SlipView3DVec32 viewPosition, SlipView3DVec32 worldPosition);

uint32_t TrackView_ProjectMask(SlipView3DVec32 point, void *userData);
bool TrackView_LoadDrawState(uint32_t recordIndex, void *userData);
bool TrackView_SphereCull(SlipView3DVec32 center, int32_t radius, void *userData);
bool TrackView_SceneryCallback(uint32_t recordAddress, uint16_t resourceHandleIndex, const uint8_t *record,
                               size_t recordBytes, const SlipView3DMatrix *objectMatrix,
                               const SlipView3DMatrix *viewMatrix, SlipView3DVec32 viewPosition,
                               SlipView3DVec32 worldPosition, uint32_t renderFlags, void *userData);

SlipView3DMatrix TrackView_VehicleViewIdentityMatrix(void);

bool TrackView_DrawDoor(TrackViewRawBspContext *context, uint32_t object);
bool TrackView_DrawDoorReverse(TrackViewRawBspContext *context, uint32_t object);

#define TRACK_VIEW_CLOUD_SPRITE_MAX 32u

typedef struct TrackViewCloudEntry {
	uint16_t spriteHandle;
	uint16_t reserved[2];
	SlipView3DVec16 position;
	uint16_t width;
	uint16_t height;
} TrackViewCloudEntry;

typedef struct TrackViewCloudState {
	SlipResourcePayload spritePayloads[TRACK_VIEW_CLOUD_SPRITE_MAX];
	size_t spriteCount;
	uint16_t spriteResources[TRACK_VIEW_CLOUD_SPRITE_MAX];
	uint16_t nearResource, farResource, silhouetteResource;
	TrackViewCloudEntry *nearEntries;
	uint16_t nearCount;
	TrackViewCloudEntry *farEntries;
	uint16_t farCount;
	TrackViewCloudEntry *silhouetteEntries;
	uint16_t silhouetteCount;
} TrackViewCloudState;

void TrackView_InitializeClouds(TrackViewCloudState *state);
void TrackView_ShutdownClouds(TrackViewCloudState *state);

typedef struct TrackViewTrackLifecycleArgs {
	TrackViewCloudState *cloudState;
	const char *const *archives;
	size_t archiveCount;
	const SlipView3DMaths *maths;
} TrackViewTrackLifecycleArgs;

typedef bool (*TrackViewTrackLifecycleCallback)(const TrackViewTrackLifecycleArgs *args);

extern const TrackViewTrackLifecycleCallback g_trackViewInitCallbacks[];
extern const TrackViewTrackLifecycleCallback g_trackViewCleanupCallbacks[];

typedef struct TrackViewCloudDrawContext {
	TrackViewRawBspContext *context;
	const SlipView3DMaths *maths;
	const SlipView3DMatrix *viewMatrix;
	uint32_t detailLevel;
	uint16_t nearYaw;
	uint16_t farYaw;
	TrackViewCloudState *cloudState;
	unsigned *drawnCount;
	unsigned *skippedCount;
} TrackViewCloudDrawContext;

void TrackView_DrawClouds(void *userData);
uint16_t TrackView_VehicleViewFrameStep(uint32_t deltaMs);
uint16_t TrackView_TrackGlobeFrameTimerUpdate(void);
void TrackView_RenderSetDiagnostics(bool enabled);
bool TrackView_RenderDiagnosticsEnabled(void);

bool SlipTrackGlobe_UpdateGivenMatrix(const char *resPath, uint16_t track, SlipView3DMatrix *matrix,
                                      uint32_t rotationArgument);
bool SlipTrackGlobe_DrawGivenResources(const char *resPath, uint16_t track, uint16_t grow,
                                       const SlipView3DMatrix *matrix, uint16_t globe, uint16_t flag);
bool SlipTrackGlobe_DrawGivenMatrix(const char *resPath, uint16_t track, uint16_t grow, const SlipView3DMatrix *matrix);
bool SlipTrackGlobe_UpdateMatrix(const char *resPath, uint16_t trackResourceHandle);
bool SlipTrackGlobe_Draw(const char *resPath, uint16_t trackResourceHandle, uint16_t growAmount);
bool SlipTrackGlobe_DrawRetained(const char *resPath, uint16_t trackResourceHandle, uint16_t growAmount,
                                 uint16_t globeResource, uint16_t flagResource);
bool TrackView_DrawVehicleViewModel(const char *resPath, int driver, SlipView3DMatrix *actorObjectMatrix,
                                    uint16_t frameStep);

bool TrackView_ApplyComponentLight(void *context, const SlipTrackWorldComponentTail *tail);
bool TrackView_RestoreComponentLight(void *context, const SlipTrackWorldComponentTail *tail);

void TrackView_BuildRefuelBeams(void *context, const uint8_t *section, uint32_t incomingBeamX,
                                const SlipTrackWorldDrawFlags *flags, uint32_t *active);
uint16_t TrackView_StepEffectRandom(void *context);

#endif
