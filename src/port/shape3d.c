#include "shape3d.h"
#include "byte_order.h"
#include "fixed_point.h"
#include "renderer_flags.h"
#include "shape_format.h"

#include <limits.h>
#include <string.h>

SlipShape3DDoorTemplate SlipShape3D_doorTemplate = {
    .header = {.version = SLIP_SHAPE_VERSION,
               .scaleShift = SLIP_SHAPE_DOOR_COORDINATE_SHIFT,
               .fileSize = sizeof(SlipShape3DDoorTemplate),
               .vertexOffset = offsetof(SlipShape3DDoorTemplate, vertexCount),
               .primitiveOffset = offsetof(SlipShape3DDoorTemplate, primitiveCount),
               .sortList = UINT16_MAX},
    .vertexCount = 4,
    .primitiveCount = 1,
    .countAndFlags = SLIP_PRIMITIVE_TEXTURE_COORDINATES | 4,
    .normalZ = -SLIP_Q14_ONE,
    .indices = {0, 1, 2, 3},
    .textureCoordinates = {{0, 0}, {SLIP_Q14_ONE, 0}, {SLIP_Q14_ONE, SLIP_Q14_ONE}, {0, SLIP_Q14_ONE}}};
uint32_t SlipShape3D_radius;
int32_t SlipShape3D_minimumX, SlipShape3D_maximumX;
int32_t SlipShape3D_minimumY, SlipShape3D_maximumY;
int32_t SlipShape3D_minimumZ, SlipShape3D_maximumZ;

int SlipShape3D_RecalculateBounds(SlipShape3DHeader *header, const SlipShape3DVertex *vertices, uint16_t count) {
	if (header->vertexOffset == 0)
		return 1;
	if (vertices == NULL || count == 0)
		return 0;

	SlipShape3D_radius = 0;
	SlipShape3D_minimumX = INT16_MAX;
	SlipShape3D_maximumX = -INT16_MAX;
	SlipShape3D_minimumY = INT16_MAX;
	SlipShape3D_maximumY = -INT16_MAX;
	SlipShape3D_minimumZ = INT16_MAX;
	SlipShape3D_maximumZ = -INT16_MAX;
	for (uint16_t i = 0; i < count; ++i) {
		const uint32_t shift = header->scaleShift & SLIP_DWORD_SHIFT_COUNT_MASK;
		const int32_t x = (int32_t)((uint32_t)(int32_t)vertices[i].x << shift);
		const int32_t y = (int32_t)((uint32_t)(int32_t)vertices[i].y << shift);
		const int32_t z = (int32_t)((uint32_t)(int32_t)vertices[i].z << shift);
		if (x < SlipShape3D_minimumX)
			SlipShape3D_minimumX = x;
		if (x > SlipShape3D_maximumX)
			SlipShape3D_maximumX = x;
		if (y < SlipShape3D_minimumY)
			SlipShape3D_minimumY = y;
		if (y > SlipShape3D_maximumY)
			SlipShape3D_maximumY = y;
		if (z < SlipShape3D_minimumZ)
			SlipShape3D_minimumZ = z;
		if (z > SlipShape3D_maximumZ)
			SlipShape3D_maximumZ = z;
		const uint32_t radius = SlipView3D_VectorLength(x, y, z);
		if (radius >= SlipShape3D_radius)
			SlipShape3D_radius = radius;
	}

	header->radius = SlipShape3D_radius;
	header->minimumX = SlipShape3D_minimumX;
	header->maximumX = SlipShape3D_maximumX;
	header->minimumY = SlipShape3D_minimumY;
	header->maximumY = SlipShape3D_maximumY;
	header->minimumZ = SlipShape3D_minimumZ;
	header->maximumZ = SlipShape3D_maximumZ;
	return 1;
}

enum {
	SLIP_SHAPE_HEADER_SIZE = 0x1c,
	SLIP_SHAPE_BSP_NODE_SIZE = 0x18,
	SLIP_SHAPE_PRIMITIVE_HEADER_SIZE = 0x0c,
	SLIP_SHAPE_BSP_MAX_DEPTH = 256,
	SLIP_SHAPE3D_PROJECTION_MATRIX_TOKEN = 0x25c54,
	SLIP_SHAPE3D_INSTALL_ERROR_TOKEN = 0x25eca,
	SLIP_SHAPE3D_MISSING_SORT_ERROR_TOKEN = 0x25e50
};

static void SlipShape3D_WriteLE16(uint8_t *destination, uint16_t value) {
	destination[0] = (uint8_t)(value & UINT8_MAX);
	destination[1] = (uint8_t)(value >> 8);
}

static int SlipShape3D_RangeInside(size_t size, uint32_t offset, uint32_t byteCount) {
	return offset <= size && byteCount <= size - offset;
}

static uint32_t SlipShape3D_SignExtendLow16(uint32_t value) { return (uint32_t)(int32_t)(int16_t)(uint16_t)value; }

static uint32_t SlipShape3D_ArithmeticShiftRight32(uint32_t value, uint32_t count) {
	const uint32_t shift = count & SLIP_DWORD_SHIFT_COUNT_MASK;

	if (shift == 0) {
		return value;
	}
	if ((value & (UINT32_MAX ^ INT32_MAX)) != 0) {
		return (value >> shift) | ~(UINT32_MAX >> shift);
	}
	return value >> shift;
}

static uint32_t SlipShape3D_MultiplySignedLowWords(uint32_t lhs, uint16_t rhs) {
	return (uint32_t)((int32_t)(int16_t)(uint16_t)lhs * (int32_t)(int16_t)rhs);
}

int SlipShape3D_DrawSetup(uint32_t translationX, uint32_t translationY, uint32_t translationZ, uint32_t rotationX,
                          uint32_t rotationY, uint32_t rotationZ, SlipShape3DDrawSetup *result) {
	if (result == NULL) {
		return 0;
	}

	*result = (SlipShape3DDrawSetup){translationX, translationY, translationZ, rotationX, rotationY, rotationZ, 1};
	return 1;
}

int SlipShape3D_MatrixSetup(uint32_t sourceMatrixAddress, uint32_t rotationX, uint32_t rotationY, uint32_t rotationZ,
                            SlipShape3DMatrixSetup *result) {
	if (result == NULL) {
		return 0;
	}

	*result = (SlipShape3DMatrixSetup){1,
	                                   1,
	                                   sourceMatrixAddress,
	                                   SLIP_SHAPE3D_PROJECTION_MATRIX_TOKEN,
	                                   1,
	                                   1,
	                                   SLIP_SHAPE3D_PROJECTION_MATRIX_TOKEN,
	                                   rotationX,
	                                   rotationY,
	                                   rotationZ,
	                                   1,
	                                   1,
	                                   1};
	return 1;
}

int SlipShape3D_VertexInit(uint32_t shapePayloadAddress, const uint8_t *shapePayload, size_t shapePayloadBytes,
                           SlipShape3DVertexInit *result) {
	uint32_t vertexBlockOffset;
	uint32_t vertexBlockPointer;
	const uint8_t *vertexBlock;
	uint32_t scaleWord;

	if (result == NULL || shapePayload == NULL ||
	    !SlipShape3D_RangeInside(shapePayloadBytes, SLIP_SHAPE_VERTEX_TABLE_OFFSET + sizeof(uint16_t),
	                             sizeof(uint16_t))) {
		return 0;
	}

	memset(result, 0, sizeof(*result));
	result->shapePointerSaved = 1;
	result->shapeBaseAddress = shapePayloadAddress;
	scaleWord = SlipBytes_ReadLE16(shapePayload + SLIP_SHAPE_SCALE_SHIFT_OFFSET);
	result->scaleShift = scaleWord;
	if (scaleWord != 0) {
		result->computedTransformRightShift = SLIP_Q14_FRACTION_BITS - scaleWord;
		result->transformRightShift = result->computedTransformRightShift;
		result->sourceLeftShift = scaleWord;
		result->branch = SLIP_SHAPE3D_VERTEX_INIT_BRANCH_SHIFTED;
	} else {
		result->branch = SLIP_SHAPE3D_VERTEX_INIT_BRANCH_UNSHIFTED;
	}

	vertexBlockOffset = SlipBytes_ReadLE32(shapePayload + SLIP_SHAPE_VERTEX_TABLE_OFFSET);
	result->vertexBlockOffset = vertexBlockOffset;
	if (shapePayloadAddress > UINT32_MAX - vertexBlockOffset ||
	    !SlipShape3D_RangeInside(shapePayloadBytes, vertexBlockOffset, SLIP_SHAPE_TABLE_COUNT_BYTES)) {
		return 0;
	}
	vertexBlockPointer = shapePayloadAddress + vertexBlockOffset;
	vertexBlock = shapePayload + vertexBlockOffset;
	result->vertexBlockPointer = vertexBlockPointer;
	result->vertexCount = SlipBytes_ReadLE16(vertexBlock);
	if (!SlipShape3D_RangeInside(shapePayloadBytes, vertexBlockOffset + SLIP_SHAPE_TABLE_COUNT_BYTES, 0u)) {
		return 0;
	}
	result->vertexDataPointer = vertexBlockPointer + SLIP_SHAPE_TABLE_COUNT_BYTES;
	result->vertexStride = SLIP_SHAPE_VERTEX_BYTES;
	result->vertexCacheInitCalled = 1;
	result->shapePointerRestored = 1;
	result->returned = 1;
	return 1;
}

int SlipShape3D_TransformUnshifted(uint32_t rotatedX, uint32_t rotatedY, uint32_t rotatedZ, uint32_t translationX,
                                   uint32_t translationY, uint32_t translationZ,
                                   SlipShape3DTransformUnshifted *result) {
	if (result == NULL) {
		return 0;
	}

	*result = (SlipShape3DTransformUnshifted){1,
	                                          1,
	                                          1,
	                                          SLIP_SHAPE3D_PROJECTION_MATRIX_TOKEN,
	                                          1,
	                                          rotatedX,
	                                          rotatedY,
	                                          rotatedZ,
	                                          rotatedX + translationX,
	                                          rotatedY + translationY,
	                                          rotatedZ + translationZ,
	                                          1,
	                                          1,
	                                          1,
	                                          1};
	return 1;
}

int SlipShape3D_SourceUnshifted(uint32_t sourceX, uint32_t sourceY, uint32_t sourceZ,
                                SlipShape3DSourceUnshifted *result) {
	if (result == NULL) {
		return 0;
	}

	*result = (SlipShape3DSourceUnshifted){SlipShape3D_SignExtendLow16(sourceX), SlipShape3D_SignExtendLow16(sourceY),
	                                       SlipShape3D_SignExtendLow16(sourceZ), 1};
	return 1;
}

int SlipShape3D_SourceShifted(uint32_t sourceX, uint32_t sourceY, uint32_t sourceZ, uint32_t shapeScaleShift,
                              SlipShape3DSourceShifted *result) {
	uint32_t shift;
	uint32_t scaledX;
	uint32_t scaledY;
	uint32_t scaledZ;

	if (result == NULL) {
		return 0;
	}

	shift = shapeScaleShift & SLIP_DWORD_SHIFT_COUNT_MASK;
	scaledX = SlipShape3D_SignExtendLow16(sourceX) << shift;
	scaledY = SlipShape3D_SignExtendLow16(sourceY) << shift;
	scaledZ = SlipShape3D_SignExtendLow16(sourceZ) << shift;
	*result = (SlipShape3DSourceShifted){1,
	                                     SlipShape3D_SignExtendLow16(sourceX),
	                                     SlipShape3D_SignExtendLow16(sourceY),
	                                     SlipShape3D_SignExtendLow16(sourceZ),
	                                     shapeScaleShift,
	                                     scaledX,
	                                     scaledY,
	                                     scaledZ,
	                                     scaledZ,
	                                     1,
	                                     1};
	return 1;
}

int SlipShape3D_TransformShifted(uint32_t sourceX, uint32_t sourceY, uint32_t sourceZ, const uint8_t *matrix,
                                 size_t matrixBytes, uint32_t shapeScaleRightShift, uint32_t translationX,
                                 uint32_t translationY, uint32_t translationZ, SlipShape3DTransformShifted *result) {
	uint32_t sourceXForOutputX;
	uint32_t sourceYForOutputX;
	uint32_t sourceZForOutputX;
	uint32_t sourceXForOutputY;
	uint32_t sourceYForOutputY;
	uint32_t sourceZForOutputY;
	uint32_t sourceXForOutputZ;
	uint32_t sourceYForOutputZ;
	uint32_t sourceZForOutputZ;
	uint32_t sumX;
	uint32_t sumY;
	uint32_t sumZ;
	uint32_t shiftedX;
	uint32_t shiftedY;
	uint32_t shiftedZ;

	if (result == NULL || matrix == NULL || !SlipShape3D_RangeInside(matrixBytes, 0, sizeof(SlipView3DMatrix))) {
		return 0;
	}

	sourceXForOutputX =
	    SlipShape3D_MultiplySignedLowWords(sourceX, SlipBytes_ReadLE16(matrix + offsetof(SlipView3DMatrix, m[0])));
	sourceYForOutputX =
	    SlipShape3D_MultiplySignedLowWords(sourceY, SlipBytes_ReadLE16(matrix + offsetof(SlipView3DMatrix, m[3])));
	sourceZForOutputX =
	    SlipShape3D_MultiplySignedLowWords(sourceZ, SlipBytes_ReadLE16(matrix + offsetof(SlipView3DMatrix, m[6])));
	sourceXForOutputY =
	    SlipShape3D_MultiplySignedLowWords(sourceX, SlipBytes_ReadLE16(matrix + offsetof(SlipView3DMatrix, m[1])));
	sourceYForOutputY =
	    SlipShape3D_MultiplySignedLowWords(sourceY, SlipBytes_ReadLE16(matrix + offsetof(SlipView3DMatrix, m[4])));
	sourceZForOutputY =
	    SlipShape3D_MultiplySignedLowWords(sourceZ, SlipBytes_ReadLE16(matrix + offsetof(SlipView3DMatrix, m[7])));
	sourceXForOutputZ =
	    SlipShape3D_MultiplySignedLowWords(sourceX, SlipBytes_ReadLE16(matrix + offsetof(SlipView3DMatrix, m[2])));
	sourceYForOutputZ =
	    SlipShape3D_MultiplySignedLowWords(sourceY, SlipBytes_ReadLE16(matrix + offsetof(SlipView3DMatrix, m[5])));
	sourceZForOutputZ =
	    SlipShape3D_MultiplySignedLowWords(sourceZ, SlipBytes_ReadLE16(matrix + offsetof(SlipView3DMatrix, m[8])));
	sumZ = sourceXForOutputZ + sourceYForOutputZ + sourceZForOutputZ;
	sumY = sourceXForOutputY + sourceYForOutputY + sourceZForOutputY;
	sumX = sourceXForOutputX + sourceYForOutputX + sourceZForOutputX;
	shiftedX = SlipShape3D_ArithmeticShiftRight32(sumX, shapeScaleRightShift);
	shiftedY = SlipShape3D_ArithmeticShiftRight32(sumY, shapeScaleRightShift);
	shiftedZ = SlipShape3D_ArithmeticShiftRight32(sumZ, shapeScaleRightShift);

	*result = (SlipShape3DTransformShifted){1,
	                                        1,
	                                        1,
	                                        SLIP_SHAPE3D_PROJECTION_MATRIX_TOKEN,
	                                        sourceX,
	                                        sourceXForOutputX,
	                                        sourceYForOutputX,
	                                        sourceZForOutputX,
	                                        sourceXForOutputY,
	                                        sourceYForOutputY,
	                                        sourceZForOutputY,
	                                        sourceXForOutputZ,
	                                        sourceYForOutputZ,
	                                        sourceZForOutputZ,
	                                        sumZ,
	                                        sumY,
	                                        sumX,
	                                        shapeScaleRightShift,
	                                        shiftedX,
	                                        shiftedY,
	                                        shiftedZ,
	                                        shiftedX + translationX,
	                                        shiftedY + translationY,
	                                        shiftedZ + translationZ,
	                                        1,
	                                        1,
	                                        1,
	                                        1};
	return 1;
}

int SlipShape3D_Draw(uint32_t installed, uint32_t sourceMatrixAddress, uint32_t shapePayloadAddress,
                     uint8_t *shapePayload, size_t shapePayloadBytes, const uint8_t *materialTable,
                     size_t materialTableBytes, uint16_t materialGlobal, uint32_t shapeBaseGlobal,
                     SlipShape3DPrimitiveClassifier classify, void *classifyUserData,
                     SlipShape3DPrimitiveCallback callback, void *callbackUserData, uint32_t translationX,
                     uint32_t translationY, uint32_t translationZ, uint32_t rotationX, uint32_t rotationY,
                     uint32_t rotationZ, uint32_t savedShapeFlags, SlipShape3DDrawProjectBranch projectBranch,
                     uint32_t partialShapeFlags, SlipShape3DDraw *result) {
	uint16_t shapeFlags;
	uint16_t sortListWord;
	uint32_t bspOffset;

	if (result == NULL) {
		return 0;
	}

	memset(result, 0, sizeof(*result));
	result->initializedValue = installed;
	if (installed == 0) {
		result->uninitializedErrorAddress = SLIP_SHAPE3D_INSTALL_ERROR_TOKEN;
		result->errorHandlerJumped = 1;
		result->branch = SLIP_SHAPE3D_DRAW_BRANCH_INSTALL_ERROR;
		return 1;
	}
	if (shapePayload == NULL ||
	    !SlipShape3D_RangeInside(shapePayloadBytes, SLIP_SHAPE_SORT_LIST_OFFSET, sizeof(uint16_t))) {
		return 0;
	}

	result->drawArgumentsSaved = 1;
	result->matrixSetupCalled = 1;
	if (!SlipShape3D_MatrixSetup(sourceMatrixAddress, rotationX, rotationY, rotationZ, &result->matrixSetup)) {
		return 0;
	}
	result->resourceHandleSaved = 1;
	result->resourceLockCalled = 1;
	shapeFlags = SlipBytes_ReadLE16(shapePayload + SLIP_SHAPE_FLAGS_OFFSET);
	result->shapeFlags = shapeFlags;
	if ((shapeFlags & SLIP_SHAPE_MATERIALS_PREPARED) == 0) {
		result->prepareCalled = 1;
		if (!SlipShape3D_Prepare(shapePayloadAddress, shapePayload, shapePayloadBytes, materialTable,
		                         materialTableBytes, materialGlobal, NULL, 0, NULL, 0, &result->prepare)) {
			return 0;
		}
	}
	result->shapeFlagsRead = 1;
	result->savedShapeFlags = savedShapeFlags;
	result->shapeFlagsSaved = 1;
	result->translationX = translationX;
	result->translationY = translationY;
	result->translationZ = translationZ;
	result->boundingRadius = SlipBytes_ReadLE32(shapePayload + SLIP_SHAPE_RADIUS_OFFSET);
	result->shapeBoundsClassifyCalled = 1;
	result->shapeFlagsSetBeforeProjection = 1;
	result->minimumX = SlipBytes_ReadLE32(shapePayload + SLIP_SHAPE_MINIMUM_X_OFFSET);
	result->minimumY = SlipBytes_ReadLE32(shapePayload + SLIP_SHAPE_MINIMUM_Y_OFFSET);
	result->minimumZ = SlipBytes_ReadLE32(shapePayload + SLIP_SHAPE_MINIMUM_Z_OFFSET);
	result->maximumX = SlipBytes_ReadLE32(shapePayload + SLIP_SHAPE_MAXIMUM_X_OFFSET);
	result->maximumY = SlipBytes_ReadLE32(shapePayload + SLIP_SHAPE_MAXIMUM_Y_OFFSET);
	result->maximumZ = SlipBytes_ReadLE32(shapePayload + SLIP_SHAPE_MAXIMUM_Z_OFFSET);
	result->boundsSet = 1;
	result->projectionTranslationX = translationX;
	result->projectionTranslationY = translationY;
	result->projectionTranslationZ = translationZ;
	result->projectionMatrixAddress = SLIP_SHAPE3D_PROJECTION_MATRIX_TOKEN;
	result->boundsProjectCalled = 1;
	if (projectBranch == SLIP_SHAPE3D_DRAW_PROJECT_REJECTED) {
		result->shapeFlagsRestored = 1;
		result->restoredShapeFlagsSet = 1;
		result->resourceHandleRestored = 1;
		result->resourceUnlockCalled = 1;
		result->drawArgumentsRestored = 1;
		result->returned = 1;
		result->branch = SLIP_SHAPE3D_DRAW_BRANCH_PROJECT_NEGATIVE;
		return 1;
	}
	if (projectBranch == SLIP_SHAPE3D_DRAW_PROJECT_PARTIAL) {
		result->partialShapeFlagsRead = 1;
		result->partialShapeFlags = partialShapeFlags;
		result->clippedShapeFlags = partialShapeFlags | SLIP_SHAPE_INSIDE_VIEW;
		result->clippedShapeFlagsSet = 1;
	}
	result->vertexInitCalled = 1;
	if (!SlipShape3D_VertexInit(shapePayloadAddress, shapePayload, shapePayloadBytes, &result->vertexInit)) {
		return 0;
	}
	sortListWord = SlipBytes_ReadLE16(shapePayload + SLIP_SHAPE_SORT_LIST_OFFSET);
	result->sortListMode = sortListWord;
	if (sortListWord != 0) {
		result->primitiveListTraverseCalled = 1;
		if (!SlipShape3D_TraversePrimitiveList(shapeBaseGlobal, shapePayload, shapePayloadBytes, classify,
		                                       classifyUserData, callback, callbackUserData, NULL, 0,
		                                       &result->primitiveList)) {
			return 0;
		}
		result->shapeFlagsRestored = 1;
		result->restoredShapeFlagsSet = 1;
		result->resourceHandleRestored = 1;
		result->resourceUnlockCalled = 1;
		result->vertexCacheClearCalled = 1;
		result->drawArgumentsRestored = 1;
		result->returned = 1;
		result->branch = SLIP_SHAPE3D_DRAW_BRANCH_LIST;
		return 1;
	}
	bspOffset = SlipBytes_ReadLE32(shapePayload + SLIP_SHAPE_BSP_OFFSET);
	result->bspOffset = bspOffset;
	if (bspOffset == 0) {
		result->missingSortErrorAddress = SLIP_SHAPE3D_MISSING_SORT_ERROR_TOKEN;
		result->errorHandlerJumped = 1;
		result->branch = SLIP_SHAPE3D_DRAW_BRANCH_SORT_ERROR;
		return 1;
	}
	if (!SlipShape3D_RangeInside(shapePayloadBytes, bspOffset, 0u)) {
		return 0;
	}
	if (shapePayloadAddress > UINT32_MAX - bspOffset) {
		return 0;
	}
	result->shapeBaseAddress = shapePayloadAddress;
	result->bspPointer = shapePayloadAddress + bspOffset;
	result->bspTraverseCalled = 1;
	result->shapeFlagsRestored = 1;
	result->restoredShapeFlagsSet = 1;
	result->resourceHandleRestored = 1;
	result->resourceUnlockCalled = 1;
	result->vertexCacheClearCalled = 1;
	result->drawArgumentsRestored = 1;
	result->returned = 1;
	result->branch = SLIP_SHAPE3D_DRAW_BRANCH_BSP;
	return 1;
}

int SlipShape3D_Callback(uint32_t primitiveRelativeOffset, uint32_t nodeKind, uint32_t planeSide,
                         uint32_t shapeBaseAddress, const uint8_t *shapePayload, size_t shapeBytes,
                         SlipShape3DPrimitiveCallback callback, void *callbackUserData,
                         SlipShape3DCallbackTraversal *result) {
	uint32_t primitiveBaseOffset;
	uint32_t primitiveRecordOffset;

	if (result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->primitiveRelativeOffset = primitiveRelativeOffset;
	result->nodeKind = nodeKind;
	result->planeSide = planeSide;
	result->backFacing = (int32_t)planeSide < 0;
	result->nonPrimitiveNode = (uint16_t)nodeKind != SLIP_SHAPE_SORT_NODE_PRIMITIVE;
	result->returned = 1;
	if (result->backFacing || result->nonPrimitiveNode) {
		return 1;
	}
	if (shapePayload == NULL || callback == NULL ||
	    !SlipShape3D_RangeInside(shapeBytes, SLIP_SHAPE_PRIMITIVE_TABLE_OFFSET, sizeof(uint32_t))) {
		return 0;
	}
	primitiveBaseOffset = SlipBytes_ReadLE32(shapePayload + SLIP_SHAPE_PRIMITIVE_TABLE_OFFSET);
	if (primitiveBaseOffset > UINT32_MAX - primitiveRelativeOffset) {
		return 0;
	}
	primitiveRecordOffset = primitiveBaseOffset + primitiveRelativeOffset;
	if (shapeBaseAddress > UINT32_MAX - primitiveRecordOffset ||
	    !SlipShape3D_RangeInside(shapeBytes, primitiveRecordOffset, 0u)) {
		return 0;
	}

	result->shapePointerSaved = 1;
	result->shapeBase = shapeBaseAddress;
	result->primitiveBaseOffset = primitiveBaseOffset;
	result->primitiveListAddress = shapeBaseAddress + primitiveBaseOffset;
	result->primitiveRecordAddress = shapeBaseAddress + primitiveRecordOffset;
	result->primitiveRecord = shapePayload + primitiveRecordOffset;
	result->primitiveCallbackCalled = 1;
	callback(result->primitiveRecordAddress, result->primitiveRecord, shapeBytes - primitiveRecordOffset,
	         callbackUserData);
	result->shapePointerRestored = 1;
	return 1;
}

int SlipShape3D_PrimitiveDispatch(const uint8_t *primitiveRecord, size_t primitiveRecordBytes,
                                  int32_t transformedNormalZ, int32_t textureDispatchThreshold,
                                  uint32_t renderFlagsValue, SlipShape3DPrimitiveDispatch *result) {
	if (primitiveRecord == NULL || result == NULL || primitiveRecordBytes < SLIP_PRIMITIVE_HEADER_BYTES) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->primitiveFlags = SlipBytes_ReadLE16(primitiveRecord + SLIP_PRIMITIVE_RENDER_FLAGS_OFFSET);
	result->renderSkipped = (result->primitiveFlags & SLIP_PRIMITIVE_SKIP_RENDER) != 0;
	result->returned = 1;
	if (result->renderSkipped) {
		return 1;
	}

	result->countAndFlags = SlipBytes_ReadLE16(primitiveRecord);
	result->normalX = SlipBytes_ReadLE16(primitiveRecord + SLIP_PRIMITIVE_NORMAL_X_OFFSET);
	result->normalY = SlipBytes_ReadLE16(primitiveRecord + SLIP_PRIMITIVE_NORMAL_Y_OFFSET);
	result->normalZ = SlipBytes_ReadLE16(primitiveRecord + SLIP_PRIMITIVE_NORMAL_Z_OFFSET);
	result->materialIndex = SlipBytes_ReadLE16(primitiveRecord + SLIP_PRIMITIVE_MATERIAL_OFFSET);
	result->vertexStream = primitiveRecord + SLIP_PRIMITIVE_HEADER_BYTES;
	result->textured = (result->countAndFlags & SLIP_PRIMITIVE_TEXTURE_COORDINATES) != 0;
	if (!result->textured) {
		result->solidPolygonCalled = 1;
		return 1;
	}

	result->normalXSaved = 1;
	result->normalTransformCalled = 1;
	result->transformedNormalZ = transformedNormalZ;
	result->textureDispatchThreshold = textureDispatchThreshold;
	result->alternateTextureRasterSelected = transformedNormalZ > textureDispatchThreshold;
	result->renderFlagsRead = 1;
	result->renderFlagsValue = renderFlagsValue;
	if (result->alternateTextureRasterSelected) {
		result->updatedRenderFlags = renderFlagsValue | SLIP_RENDER_ALTERNATE_TEXTURE_RASTER;
	} else {
		result->updatedRenderFlags = renderFlagsValue & ~SLIP_RENDER_ALTERNATE_TEXTURE_RASTER;
	}
	result->renderFlagsSet = 1;
	result->normalXRestored = 1;
	result->texturedPolygonCalled = 1;
	return 1;
}

int SlipShape3D_UppercaseAscii(uint8_t inputCharacter, SlipShape3DUppercaseAscii *result) {
	uint8_t character = inputCharacter;

	if (result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->inputCharacter = inputCharacter;
	result->belowLowerA = character < 'a';
	if (!result->belowLowerA) {
		result->aboveLowerZ = character > 'z';
		if (!result->aboveLowerZ) {
			character = (uint8_t)(character - ('a' - 'A'));
		}
	}
	result->outputCharacter = character;
	result->returned = 1;
	return 1;
}

int SlipShape3D_MaterialNameScan(uint8_t *shapePayload, size_t shapeBytes, uint16_t requestedMaterialIndex,
                                 SlipShape3DMaterialNameScanVisit *visits, size_t visitCapacity,
                                 SlipShape3DMaterialNameScan *result) {
	uint32_t materialListOffset;
	size_t recordOffset;
	uint16_t count;
	uint16_t i;

	if (shapePayload == NULL || result == NULL || shapeBytes < SLIP_SHAPE_HEADER_SIZE) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->characterValueSaved = 1;
	result->remainingCountSaved = 1;
	result->returned = 1;
	materialListOffset = SlipBytes_ReadLE32(shapePayload + SLIP_SHAPE_MATERIAL_TABLE_OFFSET);
	result->materialListOffset = materialListOffset;
	result->materialNameOffset = 0;
	if (materialListOffset == 0) {
		result->zeroOffset = 1;
		result->missingCarrySet = 1;
		result->remainingCountRestored = 1;
		result->characterValueRestored = 1;
		result->carryOut = 1;
		return 1;
	}
	if (!SlipShape3D_RangeInside(shapeBytes, materialListOffset, SLIP_SHAPE_TABLE_COUNT_BYTES)) {
		return 0;
	}
	count = SlipBytes_ReadLE16(shapePayload + materialListOffset);
	result->count = count;
	result->materialNameOffset = materialListOffset;
	if (count == 0) {
		result->zeroCount = 1;
		result->missingCarrySet = 1;
		result->remainingCountRestored = 1;
		result->characterValueRestored = 1;
		result->carryOut = 1;
		return 1;
	}
	recordOffset = (size_t)materialListOffset + SLIP_SHAPE_TABLE_COUNT_BYTES;
	result->materialListRecordOffset = recordOffset;
	for (i = 0; i < count; ++i) {
		SlipShape3DMaterialNameScanVisit visit;
		uint16_t materialIndex;

		if (!SlipShape3D_RangeInside(shapeBytes, (uint32_t)recordOffset, SLIP_SHAPE_MATERIAL_ENTRY_BYTES)) {
			return 0;
		}
		memset(&visit, 0, sizeof(visit));
		visit.index = i;
		visit.recordOffset = recordOffset;
		materialIndex = SlipBytes_ReadLE16(shapePayload + recordOffset + SLIP_SHAPE_MATERIAL_ID_OFFSET);
		visit.materialIndex = materialIndex;
		visit.match = materialIndex == requestedMaterialIndex;
		if (visit.match) {
			size_t j;

			memcpy(visit.bytesBefore, shapePayload + recordOffset, SLIP_SHAPE_MATERIAL_NAME_BYTES);
			for (j = 0; j < SLIP_SHAPE_MATERIAL_NAME_BYTES; ++j) {
				SlipShape3DUppercaseAscii upper;

				if (!SlipShape3D_UppercaseAscii(shapePayload[recordOffset + j], &upper)) {
					return 0;
				}
				shapePayload[recordOffset + j] = upper.outputCharacter;
			}
			memcpy(visit.bytesAfter, shapePayload + recordOffset, SLIP_SHAPE_MATERIAL_NAME_BYTES);
		}
		if (visits != NULL && result->visitsStored < visitCapacity) {
			visits[result->visitsStored] = visit;
		} else if (visits != NULL) {
			result->hitVisitCapacity = 1;
		}
		++result->visitsStored;
		if (visit.match) {
			result->matchCarryCleared = 1;
			result->remainingCountRestored = 1;
			result->characterValueRestored = 1;
			result->materialNameOffset = recordOffset;
			result->carryOut = 0;
			return 1;
		}
		recordOffset += SLIP_SHAPE_MATERIAL_ENTRY_BYTES;
	}
	result->materialNameOffset = recordOffset;
	result->missingCarrySet = 1;
	result->remainingCountRestored = 1;
	result->characterValueRestored = 1;
	result->carryOut = 1;
	return 1;
}

int SlipShape3D_PrimitiveAdvance(const uint8_t *primitiveRecord, size_t primitiveRecordBytes, uint32_t recordOffset,
                                 SlipShape3DPrimitiveAdvance *result) {
	uint32_t countAndFlags;
	uint32_t stride;
	uint32_t streamBytes;
	uint32_t recordSize;

	if (primitiveRecord == NULL || result == NULL || primitiveRecordBytes < sizeof(uint16_t)) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->countValueSaved = 1;
	result->recordOffset = recordOffset;
	countAndFlags = SlipBytes_ReadLE16(primitiveRecord);
	result->countAndFlags = (uint16_t)countAndFlags;
	result->extendedPayload = (countAndFlags & SLIP_PRIMITIVE_EXTENDED) != 0;
	if (result->extendedPayload) {
		result->strideValueSaved = 1;
		stride = SLIP_SERIALIZED_INDEX_BYTES;
		result->indexStride = stride;
		result->hasVertexNormals = (countAndFlags & SLIP_PRIMITIVE_VERTEX_NORMALS) != 0;
		if (result->hasVertexNormals) {
			stride += SLIP_SERIALIZED_NORMAL_BYTES;
		}
		result->strideWithNormals = stride;
		result->hasTextureCoordinates = (countAndFlags & SLIP_PRIMITIVE_TEXTURE_COORDINATES) != 0;
		if (result->hasTextureCoordinates) {
			stride += SLIP_SERIALIZED_TEXTURE_COORDINATE_BYTES;
		}
		result->vertexStride = stride;
		countAndFlags &= SLIP_PRIMITIVE_VERTEX_COUNT_MASK;
		result->vertexCount = (uint16_t)countAndFlags;
		streamBytes = (countAndFlags * stride) & UINT16_MAX;
		result->streamBytes = streamBytes;
		recordSize = streamBytes + SLIP_PRIMITIVE_HEADER_BYTES;
		result->strideValueRestored = 1;
	} else {
		countAndFlags &= SLIP_PRIMITIVE_VERTEX_COUNT_MASK;
		result->vertexCount = (uint16_t)countAndFlags;
		streamBytes = (countAndFlags * SLIP_SERIALIZED_INDEX_BYTES) & UINT16_MAX;
		result->streamBytes = streamBytes;
		recordSize = streamBytes + SLIP_PRIMITIVE_HEADER_BYTES;
	}
	if (recordSize > UINT32_MAX - recordOffset || primitiveRecordBytes < recordSize) {
		return 0;
	}
	result->recordBytes = recordSize;
	result->nextRecordOffset = recordOffset + recordSize;
	result->countValueRestored = 1;
	result->returned = 1;
	return 1;
}

int SlipShape3D_PrimitiveAdvanceWrapper(const uint8_t *primitiveRecord, size_t primitiveRecordBytes,
                                        uint32_t recordOffset, SlipShape3DPrimitiveAdvanceWrapper *result) {
	if (result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->advanceCalled = 1;
	if (!SlipShape3D_PrimitiveAdvance(primitiveRecord, primitiveRecordBytes, recordOffset, &result->advance)) {
		return 0;
	}
	result->returned = 1;
	return 1;
}

int SlipShape3D_Prepare(uint32_t shapeBaseAddress, uint8_t *shapePayload, size_t shapeBytes,
                        const uint8_t *materialTable, size_t materialTableBytes, uint16_t materialGlobal,
                        SlipShape3DPreparePrimitiveVisit *primitiveVisits, size_t primitiveVisitCapacity,
                        SlipShape3DPrepareMaterialVisit *materialVisits, size_t materialVisitCapacity,
                        SlipShape3DPrepare *result) {
	uint32_t materialListOffset;
	uint32_t primitiveListOffset;
	uint32_t recordOffset;
	uint16_t primitiveCount;
	uint16_t i;
	uint16_t flags;
	uint16_t materialCount;

	if (shapePayload == NULL || result == NULL || shapeBytes < SLIP_SHAPE_HEADER_SIZE) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->shapeBaseAddress = shapeBaseAddress;
	materialListOffset = SlipBytes_ReadLE32(shapePayload + SLIP_SHAPE_MATERIAL_TABLE_OFFSET);
	result->materialListOffset = materialListOffset;
	if (materialListOffset == 0) {
		result->zeroMaterialList = 1;
		flags = SlipBytes_ReadLE16(shapePayload + SLIP_SHAPE_FLAGS_OFFSET);
		SlipShape3D_WriteLE16(shapePayload + SLIP_SHAPE_FLAGS_OFFSET,
		                      (uint16_t)(flags | SLIP_SHAPE_MATERIALS_PREPARED));
		result->preparedFlagSetAtCompletion = 1;
		result->returned = 1;
		return 1;
	}
	primitiveListOffset = SlipBytes_ReadLE32(shapePayload + SLIP_SHAPE_PRIMITIVE_TABLE_OFFSET);
	result->primitiveListOffset = primitiveListOffset;
	if (primitiveListOffset == 0) {
		result->zeroPrimitiveList = 1;
		flags = SlipBytes_ReadLE16(shapePayload + SLIP_SHAPE_FLAGS_OFFSET);
		SlipShape3D_WriteLE16(shapePayload + SLIP_SHAPE_FLAGS_OFFSET,
		                      (uint16_t)(flags | SLIP_SHAPE_MATERIALS_PREPARED));
		result->preparedFlagSetAtCompletion = 1;
		result->returned = 1;
		return 1;
	}
	if (!SlipShape3D_RangeInside(shapeBytes, primitiveListOffset, SLIP_SHAPE_TABLE_COUNT_BYTES)) {
		return 0;
	}
	recordOffset = primitiveListOffset;
	result->primitiveListCursor = recordOffset;
	primitiveCount = SlipBytes_ReadLE16(shapePayload + recordOffset);
	result->primitiveCount = primitiveCount;
	if (primitiveCount == 0) {
		return 0;
	}
	flags = SlipBytes_ReadLE16(shapePayload + SLIP_SHAPE_FLAGS_OFFSET);
	SlipShape3D_WriteLE16(shapePayload + SLIP_SHAPE_FLAGS_OFFSET, (uint16_t)(flags | SLIP_SHAPE_MATERIALS_PREPARED));
	result->preparedFlagSetBeforePrimitives = 1;
	recordOffset += SLIP_SHAPE_TABLE_COUNT_BYTES;
	result->firstPrimitiveRecord = recordOffset;
	for (i = 0; i < primitiveCount; ++i) {
		SlipShape3DPreparePrimitiveVisit visit;
		uint16_t sourceMaterialIndex;
		uint16_t preparedMaterialIndex;

		if (!SlipShape3D_RangeInside(shapeBytes, recordOffset,
		                             SLIP_PRIMITIVE_MATERIAL_OFFSET + SLIP_SERIALIZED_INDEX_BYTES)) {
			return 0;
		}
		memset(&visit, 0, sizeof(visit));
		visit.index = i;
		visit.primitiveRecordOffset = recordOffset;
		visit.remainingCountSaved = 1;
		visit.shapePointerSaved = 1;
		sourceMaterialIndex = SlipBytes_ReadLE16(shapePayload + recordOffset + SLIP_PRIMITIVE_MATERIAL_OFFSET);
		visit.sourceMaterialIndex = sourceMaterialIndex;
		visit.materialNameScanCalled = 1;
		if (!SlipShape3D_MaterialNameScan(shapePayload, shapeBytes, sourceMaterialIndex, NULL, 0, &visit.nameScan)) {
			return 0;
		}
		preparedMaterialIndex = 0;
		if (!visit.nameScan.carryOut) {
			if (!SlipShape3D_RangeInside(shapeBytes, (uint32_t)visit.nameScan.materialNameOffset,
			                             SLIP_DRAW3D_MATERIAL_KEY_BYTES)) {
				return 0;
			}
			visit.materialLookupCalled = 1;
			if (!SlipDraw3D_GetMaterialNumber(materialTable, materialTableBytes, materialGlobal,
			                                  shapePayload + visit.nameScan.materialNameOffset,
			                                  shapeBytes - visit.nameScan.materialNameOffset, &visit.materialLookup)) {
				return 0;
			}
			if (!visit.materialLookup.carryOut) {
				preparedMaterialIndex = visit.materialLookup.materialIndex;
			}
		}
		if (visit.nameScan.carryOut || (visit.materialLookupCalled && visit.materialLookup.carryOut)) {
			visit.missingMaterialCleared = 1;
			preparedMaterialIndex = 0;
		}
		SlipShape3D_WriteLE16(shapePayload + recordOffset + SLIP_PRIMITIVE_MATERIAL_OFFSET, preparedMaterialIndex);
		visit.preparedMaterialIndex = preparedMaterialIndex;
		visit.advanceCalled = 1;
		if (!SlipShape3D_PrimitiveAdvanceWrapper(shapePayload + recordOffset, shapeBytes - recordOffset, recordOffset,
		                                         &visit.advance)) {
			return 0;
		}
		recordOffset = visit.advance.advance.nextRecordOffset;
		visit.shapePointerRestored = 1;
		visit.remainingCountRestored = 1;
		visit.remainingPrimitiveCount = (uint16_t)(primitiveCount - i - 1u);
		visit.continuePrimitives = visit.remainingPrimitiveCount != 0;
		if (primitiveVisits != NULL && result->primitiveVisitsStored < primitiveVisitCapacity) {
			primitiveVisits[result->primitiveVisitsStored] = visit;
		} else if (primitiveVisits != NULL) {
			result->primitiveHitVisitCapacity = 1;
		}
		++result->primitiveVisitsStored;
	}
	if (!SlipShape3D_RangeInside(shapeBytes, materialListOffset, SLIP_SHAPE_TABLE_COUNT_BYTES)) {
		return 0;
	}
	recordOffset = materialListOffset;
	result->materialListCursor = recordOffset;
	materialCount = SlipBytes_ReadLE16(shapePayload + recordOffset);
	result->materialCount = materialCount;
	if (materialCount == 0) {
		return 0;
	}
	recordOffset += SLIP_SHAPE_TABLE_COUNT_BYTES;
	result->firstMaterialRecord = recordOffset;
	for (i = 0; i < materialCount; ++i) {
		SlipShape3DPrepareMaterialVisit visit;

		if (!SlipShape3D_RangeInside(shapeBytes, recordOffset, SLIP_SHAPE_MATERIAL_ENTRY_BYTES)) {
			return 0;
		}
		memset(&visit, 0, sizeof(visit));
		visit.index = i;
		visit.materialRecordOffset = recordOffset;
		visit.shapePointerSaved = 1;
		visit.materialNameOffset = recordOffset;
		visit.materialLookupCalled = 1;
		if (!SlipDraw3D_GetMaterialNumber(materialTable, materialTableBytes, materialGlobal,
		                                  shapePayload + recordOffset, shapeBytes - recordOffset,
		                                  &visit.materialLookup)) {
			return 0;
		}
		visit.shapePointerRestored = 1;
		if (!visit.materialLookup.carryOut) {
			visit.materialIndexStored = 1;
			visit.preparedMaterialIndex = visit.materialLookup.materialIndex;
			SlipShape3D_WriteLE16(shapePayload + recordOffset + SLIP_SHAPE_MATERIAL_ID_OFFSET,
			                      visit.preparedMaterialIndex);
		}
		recordOffset += SLIP_SHAPE_MATERIAL_ENTRY_BYTES;
		visit.nextMaterialOffset = recordOffset;
		visit.remainingMaterialCount = (uint16_t)(materialCount - i - 1u);
		visit.continueMaterials = visit.remainingMaterialCount != 0;
		if (materialVisits != NULL && result->materialVisitsStored < materialVisitCapacity) {
			materialVisits[result->materialVisitsStored] = visit;
		} else if (materialVisits != NULL) {
			result->materialHitVisitCapacity = 1;
		}
		++result->materialVisitsStored;
	}

	flags = SlipBytes_ReadLE16(shapePayload + SLIP_SHAPE_FLAGS_OFFSET);
	SlipShape3D_WriteLE16(shapePayload + SLIP_SHAPE_FLAGS_OFFSET, (uint16_t)(flags | SLIP_SHAPE_MATERIALS_PREPARED));
	result->preparedFlagSetAtCompletion = 1;
	result->returned = 1;
	return 1;
}

int SlipShape3D_FromPayload(const uint8_t *data, size_t size, SlipShape3D *shape) {
	uint32_t fileSize;
	uint32_t bspOffset;
	uint32_t vertexOffset;
	uint32_t primitiveOffset;
	uint32_t materialListOffset;

	if (data == NULL || shape == NULL || size < SLIP_SHAPE_HEADER_SIZE) {
		return 0;
	}

	fileSize = SlipBytes_ReadLE32(data + SLIP_SHAPE_FILE_SIZE_OFFSET);
	bspOffset = SlipBytes_ReadLE32(data + SLIP_SHAPE_BSP_OFFSET);
	vertexOffset = SlipBytes_ReadLE32(data + SLIP_SHAPE_VERTEX_TABLE_OFFSET);
	primitiveOffset = SlipBytes_ReadLE32(data + SLIP_SHAPE_PRIMITIVE_TABLE_OFFSET);
	materialListOffset = SlipBytes_ReadLE32(data + SLIP_SHAPE_MATERIAL_TABLE_OFFSET);

	if (fileSize != 0 && fileSize > size) {
		return 0;
	}
	if (!SlipShape3D_RangeInside(size, vertexOffset, SLIP_SHAPE_TABLE_COUNT_BYTES)) {
		return 0;
	}
	if (bspOffset != 0 && !SlipShape3D_RangeInside(size, bspOffset, SLIP_SHAPE_SORT_HEADER_BYTES)) {
		return 0;
	}
	if (!SlipShape3D_RangeInside(size, primitiveOffset, 0)) {
		return 0;
	}
	if (materialListOffset != 0 && !SlipShape3D_RangeInside(size, materialListOffset, 0)) {
		return 0;
	}

	memset(shape, 0, sizeof(*shape));
	shape->data = data;
	shape->size = size;
	shape->version = SlipBytes_ReadLE16(data + SLIP_SHAPE_VERSION_OFFSET);
	shape->scaleShift = SlipBytes_ReadLE16(data + SLIP_SHAPE_SCALE_SHIFT_OFFSET);
	shape->flags = SlipBytes_ReadLE16(data + SLIP_SHAPE_FLAGS_OFFSET);
	shape->fileSize = fileSize;
	shape->bspOffset = bspOffset;
	shape->vertexOffset = vertexOffset;
	shape->primitiveOffset = primitiveOffset;
	shape->materialListOffset = materialListOffset;
	shape->boundingRadius = SlipShape3D_RangeInside(size, SLIP_SHAPE_RADIUS_OFFSET, sizeof(int32_t))
	                            ? (int32_t)SlipBytes_ReadLE32(data + SLIP_SHAPE_RADIUS_OFFSET)
	                            : 0;
	shape->sortListMode = SlipShape3D_RangeInside(size, SLIP_SHAPE_SORT_LIST_OFFSET, sizeof(uint16_t))
	                          ? SlipBytes_ReadLE16(data + SLIP_SHAPE_SORT_LIST_OFFSET)
	                          : 0;
	return 1;
}

int SlipShape3D_GetVertexCount(const SlipShape3D *shape, uint16_t *count) {
	if (shape == NULL || count == NULL ||
	    !SlipShape3D_RangeInside(shape->size, shape->vertexOffset, SLIP_SHAPE_TABLE_COUNT_BYTES)) {
		return 0;
	}

	*count = SlipBytes_ReadLE16(shape->data + shape->vertexOffset);
	return 1;
}

int SlipShape3D_ReadVertex(const SlipShape3D *shape, uint16_t index, SlipShape3DVertex *vertex) {
	uint16_t count;
	uint32_t offset;

	if (shape == NULL || vertex == NULL || !SlipShape3D_GetVertexCount(shape, &count)) {
		return 0;
	}
	if (index >= count) {
		return 0;
	}
	offset = shape->vertexOffset + SLIP_SHAPE_TABLE_COUNT_BYTES + (uint32_t)index * SLIP_SHAPE_VERTEX_BYTES;
	if (!SlipShape3D_RangeInside(shape->size, offset, SLIP_SHAPE_VERTEX_BYTES)) {
		return 0;
	}

	vertex->x = (int16_t)SlipBytes_ReadLE16(shape->data + offset + SLIP_SHAPE_VERTEX_X_OFFSET);
	vertex->y = (int16_t)SlipBytes_ReadLE16(shape->data + offset + SLIP_SHAPE_VERTEX_Y_OFFSET);
	vertex->z = (int16_t)SlipBytes_ReadLE16(shape->data + offset + SLIP_SHAPE_VERTEX_Z_OFFSET);
	return 1;
}

int SlipShape3D_ParsePrimitive(const SlipShape3D *shape, uint32_t primitiveOffset, int side, uint16_t nodeKind,
                               SlipShape3DPrimitive *primitive) {
	uint32_t absoluteOffset;
	const uint8_t *record;

	if (shape == NULL || primitive == NULL) {
		return 0;
	}
	if (primitiveOffset > UINT32_MAX - shape->primitiveOffset) {
		return 0;
	}
	absoluteOffset = shape->primitiveOffset + primitiveOffset;
	if (!SlipShape3D_RangeInside(shape->size, absoluteOffset, SLIP_SHAPE_PRIMITIVE_HEADER_SIZE)) {
		return 0;
	}

	record = shape->data + absoluteOffset;
	memset(primitive, 0, sizeof(*primitive));
	primitive->primitiveOffset = primitiveOffset;
	primitive->streamOffset = absoluteOffset + SLIP_SHAPE_PRIMITIVE_HEADER_SIZE;
	primitive->side = side;
	primitive->nodeKind = nodeKind;
	primitive->countAndFlags = SlipBytes_ReadLE16(record + SLIP_PRIMITIVE_COUNT_FLAGS_OFFSET);
	primitive->normalX = SlipBytes_ReadLE16(record + SLIP_PRIMITIVE_NORMAL_X_OFFSET);
	primitive->normalY = SlipBytes_ReadLE16(record + SLIP_PRIMITIVE_NORMAL_Y_OFFSET);
	primitive->normalZ = SlipBytes_ReadLE16(record + SLIP_PRIMITIVE_NORMAL_Z_OFFSET);
	primitive->materialIndex = SlipBytes_ReadLE16(record + SLIP_PRIMITIVE_MATERIAL_OFFSET);
	primitive->flags = SlipBytes_ReadLE16(record + SLIP_PRIMITIVE_RENDER_FLAGS_OFFSET);
	return SlipShape3D_RangeInside(shape->size, primitive->streamOffset, 0);
}

int SlipShape3D_ParsePrimitiveStream(const SlipShape3D *shape, const SlipShape3DPrimitive *primitive,
                                     SlipShape3DPrimitiveStream *stream) {
	uint16_t vertexCount;
	uint32_t indexBytes;
	uint32_t extraPayloadOffset;

	if (shape == NULL || primitive == NULL || stream == NULL) {
		return 0;
	}

	vertexCount = (uint16_t)(primitive->countAndFlags & SLIP_PRIMITIVE_VERTEX_COUNT_MASK);
	indexBytes = (uint32_t)vertexCount * SLIP_SERIALIZED_INDEX_BYTES;
	if (!SlipShape3D_RangeInside(shape->size, primitive->streamOffset, indexBytes)) {
		return 0;
	}

	memset(stream, 0, sizeof(*stream));
	stream->vertexCount = vertexCount;
	stream->indexOffset = primitive->streamOffset;
	if ((primitive->countAndFlags & SLIP_PRIMITIVE_VERTEX_NORMALS) != 0) {
		extraPayloadOffset = primitive->streamOffset + indexBytes;
		if (!SlipShape3D_RangeInside(shape->size, extraPayloadOffset,
		                             (uint32_t)vertexCount * SLIP_SERIALIZED_NORMAL_BYTES)) {
			return 0;
		}
		stream->hasExtendedPayload = 1;
		stream->extraPayloadOffset = extraPayloadOffset;
		stream->extraPayloadStride = SLIP_SERIALIZED_NORMAL_BYTES;
	}
	return 1;
}

static int SlipShape3D_EmitPrimitive(const SlipShape3D *shape, uint32_t primitiveOffset, uint32_t bspChild0,
                                     uint32_t bspChild1, int side, uint16_t bspWord0c, SlipShape3DPrimitiveFn callback,
                                     void *userData) {
	SlipShape3DPrimitive primitive;

	if (bspWord0c == 1u) {
		memset(&primitive, 0, sizeof(primitive));
		primitive.primitiveOffset = primitiveOffset;
		primitive.firstChildOffset = bspChild0;
		primitive.secondChildOffset = bspChild1;
		primitive.side = side;
		primitive.nodeKind = bspWord0c;
		return callback(shape, &primitive, userData);
	}
	if (side < 0 || bspWord0c != 0) {
		return 1;
	}
	if (!SlipShape3D_ParsePrimitive(shape, primitiveOffset, side, bspWord0c, &primitive)) {
		return 0;
	}
	if ((primitive.flags & SLIP_PRIMITIVE_SKIP_RENDER) != 0) {
		return 1;
	}
	return callback(shape, &primitive, userData);
}

int SlipShape3D_TraversePrimitiveList(uint32_t shapeBaseAddress, const uint8_t *shapePayload, size_t shapeBytes,
                                      SlipShape3DPrimitiveClassifier classify, void *classifyUserData,
                                      SlipShape3DPrimitiveCallback callback, void *callbackUserData,
                                      SlipShape3DPrimitiveListVisit *visits, size_t visitCapacity,
                                      SlipShape3DPrimitiveList *result) {
	uint32_t offset;
	uint16_t count;
	uint16_t i;

	if (shapePayload == NULL || classify == NULL || result == NULL ||
	    shapeBytes < SLIP_SHAPE_PRIMITIVE_TABLE_OFFSET + sizeof(uint32_t)) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->shapePointerSaved = 1;
	offset = SlipBytes_ReadLE32(shapePayload + SLIP_SHAPE_PRIMITIVE_TABLE_OFFSET);
	result->primitiveListOffset = offset;
	result->primitiveListCursor = offset;
	if (!SlipShape3D_RangeInside(shapeBytes, offset, SLIP_SHAPE_TABLE_COUNT_BYTES)) {
		return 0;
	}
	count = SlipBytes_ReadLE16(shapePayload + offset);
	result->primitiveCount = count;
	if (count == 0) {
		return 0;
	}
	offset += SLIP_SHAPE_TABLE_COUNT_BYTES;
	result->firstPrimitiveRecord = offset;
	for (i = 0; i < count; ++i) {
		SlipShape3DPrimitiveListVisit visit;
		SlipShape3DPrimitiveAdvanceWrapper advance;
		int carry;

		if (!SlipShape3D_RangeInside(shapeBytes, offset, SLIP_PRIMITIVE_HEADER_BYTES + SLIP_SERIALIZED_INDEX_BYTES)) {
			return 0;
		}
		memset(&visit, 0, sizeof(visit));
		visit.index = i;
		visit.primitiveRecordOffset = offset;
		visit.remainingCountSaved = 1;
		visit.normalX = SlipBytes_ReadLE16(shapePayload + offset + SLIP_PRIMITIVE_NORMAL_X_OFFSET);
		visit.normalY = SlipBytes_ReadLE16(shapePayload + offset + SLIP_PRIMITIVE_NORMAL_Y_OFFSET);
		visit.normalZ = SlipBytes_ReadLE16(shapePayload + offset + SLIP_PRIMITIVE_NORMAL_Z_OFFSET);
		visit.planeDistance = SlipBytes_ReadLE16(shapePayload + offset + SLIP_PRIMITIVE_HEADER_BYTES);
		visit.planeClassifyCalled = 1;
		if (!classify(visit.normalX, visit.normalY, visit.normalZ, visit.planeDistance, classifyUserData, &carry)) {
			return 0;
		}
		visit.backFacing = carry != 0;
		if (!visit.backFacing) {
			if (callback == NULL || shapeBaseAddress > UINT32_MAX - offset) {
				return 0;
			}
			visit.primitiveCallbackCalled = 1;
			visit.primitiveRecordAddress = shapeBaseAddress + offset;
			callback(visit.primitiveRecordAddress, shapePayload + offset, shapeBytes - offset, callbackUserData);
		}
		visit.remainingCountRestored = 1;
		visit.advanceCalled = 1;
		if (!SlipShape3D_PrimitiveAdvanceWrapper(shapePayload + offset, shapeBytes - offset, offset, &advance)) {
			return 0;
		}
		visit.advance = advance;
		offset = advance.advance.nextRecordOffset;
		if (!SlipShape3D_RangeInside(shapeBytes, offset, 0u)) {
			return 0;
		}
		visit.remainingPrimitiveCount = (uint16_t)(count - i - 1u);
		visit.continuePrimitives = visit.remainingPrimitiveCount != 0;
		if (visits != NULL && result->visitsStored < visitCapacity) {
			visits[result->visitsStored] = visit;
		} else if (visits != NULL) {
			result->hitVisitCapacity = 1;
		}
		++result->visitsStored;
	}
	result->shapePointerRestored = 1;
	result->returned = 1;
	return 1;
}

int SlipShape3D_TraversePrimitiveListHost(const SlipShape3D *shape, SlipShape3DPrimitiveClassifyFn classify,
                                          SlipShape3DPrimitiveFn callback, void *userData) {
	uint32_t offset;
	uint16_t count;
	uint16_t i;

	if (shape == NULL || callback == NULL ||
	    !SlipShape3D_RangeInside(shape->size, shape->primitiveOffset, SLIP_SHAPE_TABLE_COUNT_BYTES)) {
		return 0;
	}

	count = SlipBytes_ReadLE16(shape->data + shape->primitiveOffset);
	offset = 2;
	for (i = 0; i < count; ++i) {
		SlipShape3DPrimitive primitive;
		SlipShape3DPrimitiveAdvanceWrapper advance;

		if (!SlipShape3D_ParsePrimitive(shape, offset, 0, 0, &primitive)) {
			return 0;
		}
		if (classify == NULL || !classify(shape, &primitive, userData)) {
			if (!callback(shape, &primitive, userData)) {
				return 0;
			}
		}
		if (!SlipShape3D_PrimitiveAdvanceWrapper(shape->data + shape->primitiveOffset + offset,
		                                         shape->size - shape->primitiveOffset - offset, offset, &advance)) {
			return 0;
		}
		offset = advance.advance.nextRecordOffset;
		if (shape->primitiveOffset > UINT32_MAX - offset ||
		    !SlipShape3D_RangeInside(shape->size, shape->primitiveOffset + offset, 0)) {
			return 0;
		}
	}
	return 1;
}

static int SlipShape3D_TraverseNode(const SlipShape3D *shape, uint32_t treeBase, uint32_t relativeOffset,
                                    SlipShape3DClassifyFn classify, SlipShape3DPrimitiveFn callback, void *userData,
                                    unsigned depth) {
	uint32_t nodeOffset;
	uint32_t child0;
	uint32_t child1;
	uint32_t primitiveOffset;
	uint16_t bspWord0c;
	uint16_t planeIndex;
	const uint8_t *node;
	int carrySet;

	if (relativeOffset == 0) {
		return 1;
	}
	if (treeBase > UINT32_MAX - relativeOffset) {
		return 0;
	}
	nodeOffset = treeBase + relativeOffset;
	if (depth > SLIP_SHAPE_BSP_MAX_DEPTH ||
	    !SlipShape3D_RangeInside(shape->size, nodeOffset, SLIP_SHAPE_BSP_NODE_SIZE)) {
		return 0;
	}

	node = shape->data + nodeOffset;
	child0 = SlipBytes_ReadLE32(node + SLIP_SHAPE_SORT_CHILD_0_OFFSET);
	child1 = SlipBytes_ReadLE32(node + SLIP_SHAPE_SORT_CHILD_1_OFFSET);
	primitiveOffset = SlipBytes_ReadLE32(node + SLIP_SHAPE_SORT_INDEX_OFFSET);
	bspWord0c = SlipBytes_ReadLE16(node + SLIP_SHAPE_SORT_TYPE_OFFSET);
	planeIndex = SlipBytes_ReadLE16(node + SLIP_SHAPE_SORT_PLANE_VERTEX_OFFSET);

	if (planeIndex == UINT16_MAX) {
		return SlipShape3D_EmitPrimitive(shape, primitiveOffset, child0, child1, 0, bspWord0c, callback, userData);
	}

	carrySet = classify != NULL && classify(shape, nodeOffset, userData);
	if (carrySet) {
		if (!SlipShape3D_TraverseNode(shape, treeBase, child0, classify, callback, userData, depth + 1)) {
			return 0;
		}
		if (!SlipShape3D_EmitPrimitive(shape, primitiveOffset, child0, child1, -1, bspWord0c, callback, userData)) {
			return 0;
		}
		return SlipShape3D_TraverseNode(shape, treeBase, child1, classify, callback, userData, depth + 1);
	}

	if (!SlipShape3D_TraverseNode(shape, treeBase, child1, classify, callback, userData, depth + 1)) {
		return 0;
	}
	if (!SlipShape3D_EmitPrimitive(shape, primitiveOffset, child0, child1, 1, bspWord0c, callback, userData)) {
		return 0;
	}
	return SlipShape3D_TraverseNode(shape, treeBase, child0, classify, callback, userData, depth + 1);
}

int SlipShape3D_TraverseBsp(const SlipShape3D *shape, SlipShape3DClassifyFn classify, SlipShape3DPrimitiveFn callback,
                            void *userData) {
	if (shape == NULL || callback == NULL || shape->bspOffset == 0) {
		return 0;
	}
	if (!SlipShape3D_RangeInside(shape->size, shape->bspOffset, SLIP_SHAPE_SORT_HEADER_BYTES)) {
		return 0;
	}

	return SlipShape3D_TraverseNode(shape, shape->bspOffset, SLIP_SHAPE_SORT_HEADER_BYTES, classify, callback, userData,
	                                0);
}

uint32_t SlipShape3D_initialized;

void SlipShape3D_Shutdown(void) {
	if (SlipShape3D_initialized != 0) {
		SlipShape3D_initialized = 0;
	}
}
