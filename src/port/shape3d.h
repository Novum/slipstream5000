#ifndef SLIPSTREAM5000_SHAPE3D_H
#define SLIPSTREAM5000_SHAPE3D_H

#include <stddef.h>
#include <stdint.h>

#include "draw3d.h"
#include "shape_format.h"

typedef struct SlipShape3D {
	const uint8_t *data;
	size_t size;
	uint16_t version;
	uint16_t scaleShift;
	uint16_t flags;
	uint32_t fileSize;
	uint32_t bspOffset;
	uint32_t vertexOffset;
	uint32_t primitiveOffset;
	uint32_t materialListOffset;
	int32_t boundingRadius;
	uint16_t sortListMode;
} SlipShape3D;

typedef struct SlipShape3DPrimitive {
	uint32_t primitiveOffset;
	uint32_t firstChildOffset;
	uint32_t secondChildOffset;
	uint32_t streamOffset;
	int side;
	uint16_t nodeKind;
	uint16_t countAndFlags;
	uint16_t normalX;
	uint16_t normalY;
	uint16_t normalZ;
	uint16_t materialIndex;
	uint16_t flags;
} SlipShape3DPrimitive;

typedef struct SlipShape3DVertex {
	int16_t x;
	int16_t y;
	int16_t z;
} SlipShape3DVertex;

#pragma pack(push, 2)

typedef struct SlipShape3DHeader {
	uint16_t version;
	uint16_t scaleShift;
	uint16_t flags;
	uint16_t reserved;
	uint32_t fileSize;
	uint32_t bspOffset;
	uint32_t vertexOffset;
	uint32_t primitiveOffset;
	uint32_t materialOffset;
	uint32_t radius;
	int32_t minimumX;
	int32_t maximumX;
	int32_t minimumY;
	int32_t maximumY;
	int32_t minimumZ;
	int32_t maximumZ;
	uint16_t sortList;
} SlipShape3DHeader;

enum { SLIP_SHAPE_DOOR_COORDINATE_SHIFT = 6 };

typedef struct SlipShape3DDoorTemplate {
	SlipShape3DHeader header;
	uint16_t vertexCount;
	SlipShape3DVertex vertices[4];
	uint16_t primitiveCount;
	uint16_t countAndFlags;
	int16_t normalX;
	int16_t normalY;
	int16_t normalZ;
	uint16_t material;
	uint16_t flags;
	uint16_t indices[4];
	int16_t textureCoordinates[4][2];
} SlipShape3DDoorTemplate;

#pragma pack(pop)
typedef char SlipShape3DHeaderSize[sizeof(SlipShape3DHeader) == 0x3a ? 1 : -1];
typedef char SlipShape3DDoorSize[sizeof(SlipShape3DDoorTemplate) == 0x7a ? 1 : -1];
typedef char SlipShape3DDoorVertexOffset[offsetof(SlipShape3DDoorTemplate, vertices) == 0x3c ? 1 : -1];
typedef char SlipShape3DDoorMaterialOffset[offsetof(SlipShape3DDoorTemplate, material) == 0x5e ? 1 : -1];
extern SlipShape3DDoorTemplate SlipShape3D_doorTemplate;
extern uint32_t SlipShape3D_radius;
extern int32_t SlipShape3D_minimumX, SlipShape3D_maximumX;
extern int32_t SlipShape3D_minimumY, SlipShape3D_maximumY;
extern int32_t SlipShape3D_minimumZ, SlipShape3D_maximumZ;
int SlipShape3D_RecalculateBounds(SlipShape3DHeader *header, const SlipShape3DVertex *vertices, uint16_t count);

typedef struct SlipShape3DPrimitiveStream {
	uint16_t vertexCount;
	uint32_t indexOffset;
	uint32_t extraPayloadOffset;
	uint32_t extraPayloadStride;
	int hasExtendedPayload;
} SlipShape3DPrimitiveStream;

typedef void (*SlipShape3DPrimitiveCallback)(uint32_t primitiveRecordAddress, const uint8_t *primitiveRecord,
                                             size_t primitiveRecordBytes, void *userData);

typedef struct SlipShape3DCallbackTraversal {
	uint32_t primitiveRelativeOffset;
	uint32_t nodeKind;
	uint32_t planeSide;
	int backFacing;
	int nonPrimitiveNode;
	int shapePointerSaved;
	uint32_t shapeBase;
	uint32_t primitiveBaseOffset;
	uint32_t primitiveListAddress;
	uint32_t primitiveRecordAddress;
	const uint8_t *primitiveRecord;
	int primitiveCallbackCalled;
	int shapePointerRestored;
	int returned;
} SlipShape3DCallbackTraversal;

typedef struct SlipShape3DPrimitiveDispatch {
	uint16_t primitiveFlags;
	int renderSkipped;
	uint16_t countAndFlags;
	uint16_t normalX;
	uint16_t normalY;
	uint16_t normalZ;
	uint16_t materialIndex;
	const uint8_t *vertexStream;
	int textured;
	int solidPolygonCalled;
	int returned;
	int normalXSaved;
	int normalTransformCalled;
	int32_t transformedNormalZ;
	int32_t textureDispatchThreshold;
	int alternateTextureRasterSelected;
	int renderFlagsRead;
	uint32_t renderFlagsValue;
	uint32_t updatedRenderFlags;
	int renderFlagsSet;
	int normalXRestored;
	int texturedPolygonCalled;
} SlipShape3DPrimitiveDispatch;

typedef struct SlipShape3DUppercaseAscii {
	uint8_t inputCharacter;
	int belowLowerA;
	int aboveLowerZ;
	uint8_t outputCharacter;
	int returned;
} SlipShape3DUppercaseAscii;

typedef struct SlipShape3DMaterialNameScanVisit {
	uint16_t index;
	size_t recordOffset;
	uint16_t materialIndex;
	int match;
	uint8_t bytesBefore[SLIP_SHAPE_MATERIAL_NAME_BYTES];
	uint8_t bytesAfter[SLIP_SHAPE_MATERIAL_NAME_BYTES];
} SlipShape3DMaterialNameScanVisit;

typedef struct SlipShape3DMaterialNameScan {
	int characterValueSaved;
	int remainingCountSaved;
	uint32_t materialListOffset;
	size_t materialListRecordOffset;
	uint16_t count;
	int zeroOffset;
	int zeroCount;
	size_t visitsStored;
	int hitVisitCapacity;
	int matchCarryCleared;
	int missingCarrySet;
	int remainingCountRestored;
	int characterValueRestored;
	int returned;
	size_t materialNameOffset;
	int carryOut;
} SlipShape3DMaterialNameScan;

typedef struct SlipShape3DPrimitiveAdvance {
	int countValueSaved;
	uint32_t recordOffset;
	uint16_t countAndFlags;
	int extendedPayload;
	int strideValueSaved;
	uint32_t indexStride;
	int hasVertexNormals;
	uint32_t strideWithNormals;
	int hasTextureCoordinates;
	uint32_t vertexStride;
	uint16_t vertexCount;
	uint32_t streamBytes;
	uint32_t recordBytes;
	uint32_t nextRecordOffset;
	int strideValueRestored;
	int countValueRestored;
	int returned;
} SlipShape3DPrimitiveAdvance;

typedef struct SlipShape3DPrimitiveAdvanceWrapper {
	int advanceCalled;
	SlipShape3DPrimitiveAdvance advance;
	int returned;
} SlipShape3DPrimitiveAdvanceWrapper;

typedef struct SlipShape3DPreparePrimitiveVisit {
	uint16_t index;
	uint32_t primitiveRecordOffset;
	int remainingCountSaved;
	int shapePointerSaved;
	uint16_t sourceMaterialIndex;
	int materialNameScanCalled;
	SlipShape3DMaterialNameScan nameScan;
	int materialLookupCalled;
	SlipDraw3DMaterialNumber materialLookup;
	int missingMaterialCleared;
	uint16_t preparedMaterialIndex;
	int advanceCalled;
	SlipShape3DPrimitiveAdvanceWrapper advance;
	int shapePointerRestored;
	int remainingCountRestored;
	uint16_t remainingPrimitiveCount;
	int continuePrimitives;
} SlipShape3DPreparePrimitiveVisit;

typedef struct SlipShape3DPrepareMaterialVisit {
	uint16_t index;
	uint32_t materialRecordOffset;
	int shapePointerSaved;
	uint32_t materialNameOffset;
	int materialLookupCalled;
	SlipDraw3DMaterialNumber materialLookup;
	int shapePointerRestored;
	int materialIndexStored;
	uint16_t preparedMaterialIndex;
	uint32_t nextMaterialOffset;
	uint16_t remainingMaterialCount;
	int continueMaterials;
} SlipShape3DPrepareMaterialVisit;

typedef struct SlipShape3DPrepare {
	uint32_t shapeBaseAddress;
	uint32_t materialListOffset;
	int zeroMaterialList;
	uint32_t primitiveListOffset;
	int zeroPrimitiveList;
	uint32_t primitiveListCursor;
	uint16_t primitiveCount;
	int preparedFlagSetBeforePrimitives;
	uint32_t firstPrimitiveRecord;
	size_t primitiveVisitsStored;
	int primitiveHitVisitCapacity;
	uint32_t materialListCursor;
	uint16_t materialCount;
	uint32_t firstMaterialRecord;
	size_t materialVisitsStored;
	int materialHitVisitCapacity;
	int preparedFlagSetAtCompletion;
	int returned;
} SlipShape3DPrepare;

typedef struct SlipShape3DDrawSetup {
	uint32_t translationX;
	uint32_t translationY;
	uint32_t translationZ;
	uint32_t rotationX;
	uint32_t rotationY;
	uint32_t rotationZ;
	int returned;
} SlipShape3DDrawSetup;

typedef struct SlipShape3DMatrixSetup {
	int sourcePointerSaved;
	int rotationArgumentSaved;
	uint32_t sourceMatrixAddress;
	uint32_t matrixCopyDestination;
	int matrixCopyCalled;
	int rotationArgumentRestored;
	uint32_t rotationMatrixAddress;
	uint32_t rotationX;
	uint32_t rotationY;
	uint32_t rotationZ;
	int matrixRotationCalled;
	int sourcePointerRestored;
	int returned;
} SlipShape3DMatrixSetup;

typedef enum SlipShape3DVertexInitBranch {
	SLIP_SHAPE3D_VERTEX_INIT_BRANCH_SHIFTED,
	SLIP_SHAPE3D_VERTEX_INIT_BRANCH_UNSHIFTED
} SlipShape3DVertexInitBranch;

typedef struct SlipShape3DVertexInit {
	int shapePointerSaved;
	uint32_t shapeBaseAddress;
	uint32_t scaleShift;
	uint32_t computedTransformRightShift;
	uint32_t transformRightShift;
	uint32_t sourceLeftShift;
	uint32_t vertexBlockOffset;
	uint32_t vertexBlockPointer;
	uint32_t vertexCount;
	uint32_t vertexDataPointer;
	uint32_t vertexStride;
	int vertexCacheInitCalled;
	int shapePointerRestored;
	int returned;
	SlipShape3DVertexInitBranch branch;
} SlipShape3DVertexInit;

typedef struct SlipShape3DTransformUnshifted {
	int matrixPointerSaved;
	int auxiliaryArgumentSaved;
	int frameArgumentSaved;
	uint32_t matrixAddress;
	int matrixTransformCalled;
	uint32_t rotatedX;
	uint32_t rotatedY;
	uint32_t rotatedZ;
	uint32_t translatedX;
	uint32_t translatedY;
	uint32_t translatedZ;
	int frameArgumentRestored;
	int auxiliaryArgumentRestored;
	int matrixPointerRestored;
	int returned;
} SlipShape3DTransformUnshifted;

typedef struct SlipShape3DSourceUnshifted {
	uint32_t sourceX;
	uint32_t sourceY;
	uint32_t sourceZ;
	int returned;
} SlipShape3DSourceUnshifted;

typedef struct SlipShape3DSourceShifted {
	int auxiliaryArgumentSaved;
	uint32_t sourceX;
	uint32_t sourceY;
	uint32_t sourceZ;
	uint32_t sourceLeftShift;
	uint32_t scaledX;
	uint32_t scaledY;
	uint32_t scaledZAuxiliary;
	uint32_t scaledZ;
	int auxiliaryArgumentRestored;
	int returned;
} SlipShape3DSourceShifted;

typedef struct SlipShape3DTransformShifted {
	int matrixPointerSaved;
	int auxiliaryArgumentSaved;
	int frameArgumentSaved;
	uint32_t matrixAddress;
	uint32_t sourceX;
	uint32_t xToXProduct;
	uint32_t yToXProduct;
	uint32_t zToXProduct;
	uint32_t xToYProduct;
	uint32_t yToYProduct;
	uint32_t zToYProduct;
	uint32_t xToZProduct;
	uint32_t yToZProduct;
	uint32_t zToZProduct;
	uint32_t sumZ;
	uint32_t sumY;
	uint32_t sumX;
	uint32_t transformRightShift;
	uint32_t rotatedX;
	uint32_t rotatedY;
	uint32_t rotatedZ;
	uint32_t translatedX;
	uint32_t translatedY;
	uint32_t translatedZ;
	int frameArgumentRestored;
	int auxiliaryArgumentRestored;
	int matrixPointerRestored;
	int returned;
} SlipShape3DTransformShifted;

typedef int (*SlipShape3DPrimitiveClassifier)(uint16_t normalX, uint16_t normalY, uint16_t normalZ,
                                              uint16_t planeDistance, void *userData, int *backFacing);

typedef struct SlipShape3DPrimitiveListVisit {
	uint16_t index;
	uint32_t primitiveRecordOffset;
	int remainingCountSaved;
	uint16_t normalX;
	uint16_t normalY;
	uint16_t normalZ;
	uint16_t planeDistance;
	int planeClassifyCalled;
	int backFacing;
	int primitiveCallbackCalled;
	uint32_t primitiveRecordAddress;
	int remainingCountRestored;
	int advanceCalled;
	SlipShape3DPrimitiveAdvanceWrapper advance;
	uint16_t remainingPrimitiveCount;
	int continuePrimitives;
} SlipShape3DPrimitiveListVisit;

typedef struct SlipShape3DPrimitiveList {
	int shapePointerSaved;
	uint32_t primitiveListOffset;
	uint32_t primitiveListCursor;
	uint16_t primitiveCount;
	uint32_t firstPrimitiveRecord;
	size_t visitsStored;
	int hitVisitCapacity;
	int shapePointerRestored;
	int returned;
} SlipShape3DPrimitiveList;

typedef enum SlipShape3DDrawProjectBranch {
	SLIP_SHAPE3D_DRAW_PROJECT_REJECTED,
	SLIP_SHAPE3D_DRAW_PROJECT_PARTIAL,
	SLIP_SHAPE3D_DRAW_PROJECT_INSIDE
} SlipShape3DDrawProjectBranch;

typedef enum SlipShape3DDrawBranch {
	SLIP_SHAPE3D_DRAW_BRANCH_INSTALL_ERROR,
	SLIP_SHAPE3D_DRAW_BRANCH_PROJECT_NEGATIVE,
	SLIP_SHAPE3D_DRAW_BRANCH_SORT_ERROR,
	SLIP_SHAPE3D_DRAW_BRANCH_BSP,
	SLIP_SHAPE3D_DRAW_BRANCH_LIST
} SlipShape3DDrawBranch;

typedef struct SlipShape3DDraw {
	uint32_t initializedValue;
	int drawArgumentsSaved;
	int matrixSetupCalled;
	SlipShape3DMatrixSetup matrixSetup;
	int resourceHandleSaved;
	int resourceLockCalled;
	uint16_t shapeFlags;
	int prepareCalled;
	SlipShape3DPrepare prepare;
	int shapeFlagsRead;
	uint32_t savedShapeFlags;
	int shapeFlagsSaved;
	uint32_t translationX;
	uint32_t translationY;
	uint32_t translationZ;
	uint32_t boundingRadius;
	int shapeBoundsClassifyCalled;
	int shapeFlagsSetBeforeProjection;
	uint32_t minimumX;
	uint32_t minimumY;
	uint32_t minimumZ;
	uint32_t maximumX;
	uint32_t maximumY;
	uint32_t maximumZ;
	int boundsSet;
	uint32_t projectionTranslationX;
	uint32_t projectionTranslationY;
	uint32_t projectionTranslationZ;
	uint32_t projectionMatrixAddress;
	int boundsProjectCalled;
	int partialShapeFlagsRead;
	uint32_t partialShapeFlags;
	uint32_t clippedShapeFlags;
	int clippedShapeFlagsSet;
	int vertexInitCalled;
	SlipShape3DVertexInit vertexInit;
	uint16_t sortListMode;
	uint32_t bspOffset;
	uint32_t shapeBaseAddress;
	uint32_t bspPointer;
	int bspTraverseCalled;
	int primitiveListTraverseCalled;
	SlipShape3DPrimitiveList primitiveList;
	int shapeFlagsRestored;
	int restoredShapeFlagsSet;
	int resourceHandleRestored;
	int resourceUnlockCalled;
	int vertexCacheClearCalled;
	int drawArgumentsRestored;
	int returned;
	uint32_t missingSortErrorAddress;
	uint32_t uninitializedErrorAddress;
	int errorHandlerJumped;
	SlipShape3DDrawBranch branch;
} SlipShape3DDraw;

typedef int (*SlipShape3DClassifyFn)(const SlipShape3D *shape, uint32_t nodeOffset, void *userData);

typedef int (*SlipShape3DPrimitiveClassifyFn)(const SlipShape3D *shape, const SlipShape3DPrimitive *primitive,
                                              void *userData);

typedef int (*SlipShape3DPrimitiveFn)(const SlipShape3D *shape, const SlipShape3DPrimitive *primitive, void *userData);

int SlipShape3D_FromPayload(const uint8_t *data, size_t size, SlipShape3D *shape);
int SlipShape3D_GetVertexCount(const SlipShape3D *shape, uint16_t *count);
int SlipShape3D_ReadVertex(const SlipShape3D *shape, uint16_t index, SlipShape3DVertex *vertex);
int SlipShape3D_ParsePrimitive(const SlipShape3D *shape, uint32_t primitiveOffset, int side, uint16_t nodeKind,
                               SlipShape3DPrimitive *primitive);
int SlipShape3D_ParsePrimitiveStream(const SlipShape3D *shape, const SlipShape3DPrimitive *primitive,
                                     SlipShape3DPrimitiveStream *stream);
int SlipShape3D_TraverseBsp(const SlipShape3D *shape, SlipShape3DClassifyFn classify, SlipShape3DPrimitiveFn callback,
                            void *userData);
int SlipShape3D_TraversePrimitiveList(uint32_t shapeBaseAddress, const uint8_t *shapePayload, size_t shapeBytes,
                                      SlipShape3DPrimitiveClassifier classify, void *classifyUserData,
                                      SlipShape3DPrimitiveCallback callback, void *callbackUserData,
                                      SlipShape3DPrimitiveListVisit *visits, size_t visitCapacity,
                                      SlipShape3DPrimitiveList *result);
int SlipShape3D_TraversePrimitiveListHost(const SlipShape3D *shape, SlipShape3DPrimitiveClassifyFn classify,
                                          SlipShape3DPrimitiveFn callback, void *userData);
int SlipShape3D_DrawSetup(uint32_t translationX, uint32_t translationY, uint32_t translationZ, uint32_t rotationX,
                          uint32_t rotationY, uint32_t rotationZ, SlipShape3DDrawSetup *result);
int SlipShape3D_MatrixSetup(uint32_t sourceMatrixAddress, uint32_t rotationX, uint32_t rotationY, uint32_t rotationZ,
                            SlipShape3DMatrixSetup *result);
int SlipShape3D_VertexInit(uint32_t shapePayloadAddress, const uint8_t *shapePayload, size_t shapePayloadBytes,
                           SlipShape3DVertexInit *result);
int SlipShape3D_TransformUnshifted(uint32_t rotatedX, uint32_t rotatedY, uint32_t rotatedZ, uint32_t translationX,
                                   uint32_t translationY, uint32_t translationZ, SlipShape3DTransformUnshifted *result);
int SlipShape3D_SourceUnshifted(uint32_t sourceX, uint32_t sourceY, uint32_t sourceZ,
                                SlipShape3DSourceUnshifted *result);
int SlipShape3D_SourceShifted(uint32_t sourceX, uint32_t sourceY, uint32_t sourceZ, uint32_t shapeScaleShift,
                              SlipShape3DSourceShifted *result);
int SlipShape3D_TransformShifted(uint32_t sourceX, uint32_t sourceY, uint32_t sourceZ, const uint8_t *matrix,
                                 size_t matrixBytes, uint32_t shapeScaleRightShift, uint32_t translationX,
                                 uint32_t translationY, uint32_t translationZ, SlipShape3DTransformShifted *result);
int SlipShape3D_Draw(uint32_t installed, uint32_t sourceMatrixAddress, uint32_t shapePayloadAddress,
                     uint8_t *shapePayload, size_t shapePayloadBytes, const uint8_t *materialTable,
                     size_t materialTableBytes, uint16_t materialGlobal, uint32_t shapeBaseGlobal,
                     SlipShape3DPrimitiveClassifier classify, void *classifyUserData,
                     SlipShape3DPrimitiveCallback callback, void *callbackUserData, uint32_t translationX,
                     uint32_t translationY, uint32_t translationZ, uint32_t rotationX, uint32_t rotationY,
                     uint32_t rotationZ, uint32_t savedShapeFlags, SlipShape3DDrawProjectBranch projectBranch,
                     uint32_t partialShapeFlags, SlipShape3DDraw *result);
int SlipShape3D_Callback(uint32_t primitiveRelativeOffset, uint32_t nodeKind, uint32_t planeSide,
                         uint32_t shapeBaseAddress, const uint8_t *shapePayload, size_t shapeBytes,
                         SlipShape3DPrimitiveCallback callback, void *callbackUserData,
                         SlipShape3DCallbackTraversal *result);
int SlipShape3D_PrimitiveDispatch(const uint8_t *primitiveRecord, size_t primitiveRecordBytes,
                                  int32_t transformedNormalZ, int32_t textureDispatchThreshold,
                                  uint32_t renderFlagsValue, SlipShape3DPrimitiveDispatch *result);
extern uint32_t SlipShape3D_initialized;
void SlipShape3D_Initialize(void);
void SlipShape3D_InvalidateResident(void);
void SlipShape3D_Shutdown(void);
void SlipShape3D_InvalidateMaterials(SlipShape3DHeader *header);
void SlipShape3D_Loaded(SlipShape3DHeader *header);

void SlipShape3D_ClearPrepared(SlipShape3DHeader *header);
int SlipShape3D_UppercaseAscii(uint8_t inputCharacter, SlipShape3DUppercaseAscii *result);
int SlipShape3D_MaterialNameScan(uint8_t *shapePayload, size_t shapeBytes, uint16_t requestedMaterialIndex,
                                 SlipShape3DMaterialNameScanVisit *visits, size_t visitCapacity,
                                 SlipShape3DMaterialNameScan *result);
int SlipShape3D_PrimitiveAdvance(const uint8_t *primitiveRecord, size_t primitiveRecordBytes, uint32_t recordOffset,
                                 SlipShape3DPrimitiveAdvance *result);
int SlipShape3D_PrimitiveAdvanceWrapper(const uint8_t *primitiveRecord, size_t primitiveRecordBytes,
                                        uint32_t recordOffset, SlipShape3DPrimitiveAdvanceWrapper *result);
int SlipShape3D_Prepare(uint32_t shapeBaseAddress, uint8_t *shapePayload, size_t shapeBytes,
                        const uint8_t *materialTable, size_t materialTableBytes, uint16_t materialGlobal,
                        SlipShape3DPreparePrimitiveVisit *primitiveVisits, size_t primitiveVisitCapacity,
                        SlipShape3DPrepareMaterialVisit *materialVisits, size_t materialVisitCapacity,
                        SlipShape3DPrepare *result);

#endif
