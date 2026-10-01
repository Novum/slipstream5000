#include "shape3d.h"
#include "byte_order.h"
#include "shape_format.h"

#include <limits.h>
#include <string.h>

SlipShape3DDoorTemplate SlipShape3D_doorTemplate = {
    .header = {.version = 0x0c,
               .scaleShift = 6,
               .fileSize = 0x7a,
               .vertexOffset = 0x3a,
               .primitiveOffset = 0x54,
               .sortList = 0xffff},
    .vertexCount = 4,
    .primitiveCount = 1,
    .countAndFlags = 0x8004,
    .normalZ = -0x4000,
    .indices = {0, 1, 2, 3},
    .textureCoordinates = {{0, 0}, {0x4000, 0}, {0x4000, 0x4000}, {0, 0x4000}}};
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
	SlipShape3D_minimumX = 0x7fff;
	SlipShape3D_maximumX = -0x7fff;
	SlipShape3D_minimumY = 0x7fff;
	SlipShape3D_maximumY = -0x7fff;
	SlipShape3D_minimumZ = 0x7fff;
	SlipShape3D_maximumZ = -0x7fff;
	for (uint16_t i = 0; i < count; ++i) {

		const uint32_t shift = header->scaleShift & 31u;
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
	SLIP_SHAPE_BSP_MAX_DEPTH = 256
};

static void SlipShape3D_WriteLE16(uint8_t *destination, uint16_t value) {
	destination[0] = (uint8_t)(value & 0xffu);
	destination[1] = (uint8_t)(value >> 8);
}

static int SlipShape3D_RangeInside(size_t size, uint32_t offset, uint32_t byteCount) {
	return offset <= size && byteCount <= size - offset;
}

static uint32_t SlipShape3D_SignExtendLow16(uint32_t value) { return (uint32_t)(int32_t)(int16_t)(uint16_t)value; }

static uint32_t SlipShape3D_ArithmeticShiftRight32(uint32_t value, uint32_t count) {
	const uint32_t shift = count & 0x1fu;

	if (shift == 0) {
		return value;
	}
	if ((value & 0x80000000u) != 0) {
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

	*result = (SlipShape3DMatrixSetup){1,         1,         sourceMatrixAddress, 0x00025c54u, 1, 1, 0x00025c54u,
	                                   rotationX, rotationY, rotationZ,           1,           1, 1};
	return 1;
}

int SlipShape3D_VertexInit(uint32_t shapePayloadAddress, const uint8_t *shapePayload, size_t shapePayloadBytes,
                           SlipShape3DVertexInit *result) {
	uint32_t vertexBlockOffset;
	uint32_t vertexBlockPointer;
	const uint8_t *vertexBlock;
	uint32_t scaleWord;

	if (result == NULL || shapePayload == NULL || !SlipShape3D_RangeInside(shapePayloadBytes, 0x12u, 2u)) {
		return 0;
	}

	memset(result, 0, sizeof(*result));
	result->shapePointerSaved = 1;
	result->shapeBaseAddress = shapePayloadAddress;
	scaleWord = SlipBytes_ReadLE16(shapePayload + 0x02u);
	result->scaleShift = scaleWord;
	if (scaleWord != 0) {
		result->computedTransformRightShift = 0x0000000eu - scaleWord;
		result->transformRightShift = result->computedTransformRightShift;
		result->sourceLeftShift = scaleWord;
		result->branch = SLIP_SHAPE3D_VERTEX_INIT_BRANCH_SHIFTED;
	} else {
		result->branch = SLIP_SHAPE3D_VERTEX_INIT_BRANCH_UNSHIFTED;
	}

	vertexBlockOffset = SlipBytes_ReadLE32(shapePayload + 0x10u);
	result->vertexBlockOffset = vertexBlockOffset;
	if (shapePayloadAddress > UINT32_MAX - vertexBlockOffset ||
	    !SlipShape3D_RangeInside(shapePayloadBytes, vertexBlockOffset, 2u)) {
		return 0;
	}
	vertexBlockPointer = shapePayloadAddress + vertexBlockOffset;
	vertexBlock = shapePayload + vertexBlockOffset;
	result->vertexBlockPointer = vertexBlockPointer;
	result->vertexCount = SlipBytes_ReadLE16(vertexBlock);
	if (!SlipShape3D_RangeInside(shapePayloadBytes, vertexBlockOffset + 2u, 0u)) {
		return 0;
	}
	result->vertexDataPointer = vertexBlockPointer + 2u;
	result->vertexStride = 0x00000006u;
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
	                                          0x00025c54u,
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

	shift = shapeScaleShift & 0x1fu;
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

	if (result == NULL || matrix == NULL || !SlipShape3D_RangeInside(matrixBytes, 0, 0x12u)) {
		return 0;
	}

	sourceXForOutputX = SlipShape3D_MultiplySignedLowWords(sourceX, SlipBytes_ReadLE16(matrix + 0x00u));
	sourceYForOutputX = SlipShape3D_MultiplySignedLowWords(sourceY, SlipBytes_ReadLE16(matrix + 0x06u));
	sourceZForOutputX = SlipShape3D_MultiplySignedLowWords(sourceZ, SlipBytes_ReadLE16(matrix + 0x0cu));
	sourceXForOutputY = SlipShape3D_MultiplySignedLowWords(sourceX, SlipBytes_ReadLE16(matrix + 0x02u));
	sourceYForOutputY = SlipShape3D_MultiplySignedLowWords(sourceY, SlipBytes_ReadLE16(matrix + 0x08u));
	sourceZForOutputY = SlipShape3D_MultiplySignedLowWords(sourceZ, SlipBytes_ReadLE16(matrix + 0x0eu));
	sourceXForOutputZ = SlipShape3D_MultiplySignedLowWords(sourceX, SlipBytes_ReadLE16(matrix + 0x04u));
	sourceYForOutputZ = SlipShape3D_MultiplySignedLowWords(sourceY, SlipBytes_ReadLE16(matrix + 0x0au));
	sourceZForOutputZ = SlipShape3D_MultiplySignedLowWords(sourceZ, SlipBytes_ReadLE16(matrix + 0x10u));
	sumZ = sourceXForOutputZ + sourceYForOutputZ + sourceZForOutputZ;
	sumY = sourceXForOutputY + sourceYForOutputY + sourceZForOutputY;
	sumX = sourceXForOutputX + sourceYForOutputX + sourceZForOutputX;
	shiftedX = SlipShape3D_ArithmeticShiftRight32(sumX, shapeScaleRightShift);
	shiftedY = SlipShape3D_ArithmeticShiftRight32(sumY, shapeScaleRightShift);
	shiftedZ = SlipShape3D_ArithmeticShiftRight32(sumZ, shapeScaleRightShift);

	*result = (SlipShape3DTransformShifted){1,
	                                        1,
	                                        1,
	                                        0x00025c54u,
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
		result->uninitializedErrorAddress = 0x00025ecau;
		result->errorHandlerJumped = 1;
		result->branch = SLIP_SHAPE3D_DRAW_BRANCH_INSTALL_ERROR;
		return 1;
	}
	if (shapePayload == NULL || !SlipShape3D_RangeInside(shapePayloadBytes, 0x38u, 2u)) {
		return 0;
	}

	result->drawArgumentsSaved = 1;
	result->matrixSetupCalled = 1;
	if (!SlipShape3D_MatrixSetup(sourceMatrixAddress, rotationX, rotationY, rotationZ, &result->matrixSetup)) {
		return 0;
	}
	result->resourceHandleSaved = 1;
	result->resourceLockCalled = 1;
	shapeFlags = SlipBytes_ReadLE16(shapePayload + 0x04u);
	result->shapeFlags = shapeFlags;
	if ((shapeFlags & 0x0001u) == 0) {
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
	result->boundingRadius = SlipBytes_ReadLE32(shapePayload + 0x1cu);
	result->shapeBoundsClassifyCalled = 1;
	result->shapeFlagsSetBeforeProjection = 1;
	result->minimumX = SlipBytes_ReadLE32(shapePayload + 0x20u);
	result->minimumY = SlipBytes_ReadLE32(shapePayload + 0x28u);
	result->minimumZ = SlipBytes_ReadLE32(shapePayload + 0x30u);
	result->maximumX = SlipBytes_ReadLE32(shapePayload + 0x24u);
	result->maximumY = SlipBytes_ReadLE32(shapePayload + 0x2cu);
	result->maximumZ = SlipBytes_ReadLE32(shapePayload + 0x34u);
	result->boundsSet = 1;
	result->projectionTranslationX = translationX;
	result->projectionTranslationY = translationY;
	result->projectionTranslationZ = translationZ;
	result->projectionMatrixAddress = 0x00025c54u;
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
		result->clippedShapeFlags = partialShapeFlags | 0x00000002u;
		result->clippedShapeFlagsSet = 1;
	}
	result->vertexInitCalled = 1;
	if (!SlipShape3D_VertexInit(shapePayloadAddress, shapePayload, shapePayloadBytes, &result->vertexInit)) {
		return 0;
	}
	sortListWord = SlipBytes_ReadLE16(shapePayload + 0x38u);
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
	bspOffset = SlipBytes_ReadLE32(shapePayload + 0x0cu);
	result->bspOffset = bspOffset;
	if (bspOffset == 0) {
		result->missingSortErrorAddress = 0x00025e50u;
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
	result->nonPrimitiveNode = (uint16_t)nodeKind != 0;
	result->returned = 1;
	if (result->backFacing || result->nonPrimitiveNode) {
		return 1;
	}
	if (shapePayload == NULL || callback == NULL || !SlipShape3D_RangeInside(shapeBytes, 0x14u, 4u)) {
		return 0;
	}
	primitiveBaseOffset = SlipBytes_ReadLE32(shapePayload + 0x14u);
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
	if (primitiveRecord == NULL || result == NULL || primitiveRecordBytes < 0x0cu) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->primitiveFlags = SlipBytes_ReadLE16(primitiveRecord + 0x0au);
	result->renderSkipped = (result->primitiveFlags & 0x0004u) != 0;
	result->returned = 1;
	if (result->renderSkipped) {
		return 1;
	}

	result->countAndFlags = SlipBytes_ReadLE16(primitiveRecord);
	result->normalX = SlipBytes_ReadLE16(primitiveRecord + 0x02u);
	result->normalY = SlipBytes_ReadLE16(primitiveRecord + 0x04u);
	result->normalZ = SlipBytes_ReadLE16(primitiveRecord + 0x06u);
	result->materialIndex = SlipBytes_ReadLE16(primitiveRecord + 0x08u);
	result->vertexStream = primitiveRecord + 0x0cu;
	result->textured = (result->countAndFlags & 0x8000u) != 0;
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
		result->updatedRenderFlags = renderFlagsValue | 0x00000020u;
	} else {
		result->updatedRenderFlags = renderFlagsValue & 0xffffffdfu;
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
	result->belowLowerA = character < 0x61u;
	if (!result->belowLowerA) {
		result->aboveLowerZ = character > 0x7au;
		if (!result->aboveLowerZ) {
			character = (uint8_t)(character - 0x20u);
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

	if (shapePayload == NULL || result == NULL || shapeBytes < 0x1cu) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->characterValueSaved = 1;
	result->remainingCountSaved = 1;
	result->returned = 1;
	materialListOffset = SlipBytes_ReadLE32(shapePayload + 0x18u);
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
	if (!SlipShape3D_RangeInside(shapeBytes, materialListOffset, 2u)) {
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
	recordOffset = (size_t)materialListOffset + 2u;
	result->materialListRecordOffset = recordOffset;
	for (i = 0; i < count; ++i) {
		SlipShape3DMaterialNameScanVisit visit;
		uint16_t materialIndex;

		if (!SlipShape3D_RangeInside(shapeBytes, (uint32_t)recordOffset, 0x12u)) {
			return 0;
		}
		memset(&visit, 0, sizeof(visit));
		visit.index = i;
		visit.recordOffset = recordOffset;
		materialIndex = SlipBytes_ReadLE16(shapePayload + recordOffset + 0x10u);
		visit.materialIndex = materialIndex;
		visit.match = materialIndex == requestedMaterialIndex;
		if (visit.match) {
			size_t j;

			memcpy(visit.bytesBefore, shapePayload + recordOffset, 16u);
			for (j = 0; j < 16u; ++j) {
				SlipShape3DUppercaseAscii upper;

				if (!SlipShape3D_UppercaseAscii(shapePayload[recordOffset + j], &upper)) {
					return 0;
				}
				shapePayload[recordOffset + j] = upper.outputCharacter;
			}
			memcpy(visit.bytesAfter, shapePayload + recordOffset, 16u);
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
		recordOffset += 0x12u;
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

	if (primitiveRecord == NULL || result == NULL || primitiveRecordBytes < 2u) {
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
		stride = 2u;
		result->indexStride = stride;
		result->hasVertexNormals = (countAndFlags & SLIP_PRIMITIVE_VERTEX_NORMALS) != 0;
		if (result->hasVertexNormals) {
			stride += 6u;
		}
		result->strideWithNormals = stride;
		result->hasTextureCoordinates = (countAndFlags & SLIP_PRIMITIVE_TEXTURE_COORDINATES) != 0;
		if (result->hasTextureCoordinates) {
			stride += 4u;
		}
		result->vertexStride = stride;
		countAndFlags &= SLIP_PRIMITIVE_VERTEX_COUNT_MASK;
		result->vertexCount = (uint16_t)countAndFlags;
		streamBytes = (countAndFlags * stride) & 0xffffu;
		result->streamBytes = streamBytes;
		recordSize = streamBytes + 0x0cu;
		result->strideValueRestored = 1;
	} else {
		countAndFlags &= SLIP_PRIMITIVE_VERTEX_COUNT_MASK;
		result->vertexCount = (uint16_t)countAndFlags;
		streamBytes = (countAndFlags << 1u) & 0xffffu;
		result->streamBytes = streamBytes;
		recordSize = streamBytes + 0x0cu;
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

	if (shapePayload == NULL || result == NULL || shapeBytes < 0x1cu) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->shapeBaseAddress = shapeBaseAddress;
	materialListOffset = SlipBytes_ReadLE32(shapePayload + 0x18u);
	result->materialListOffset = materialListOffset;
	if (materialListOffset == 0) {
		result->zeroMaterialList = 1;
		flags = SlipBytes_ReadLE16(shapePayload + 0x04u);
		SlipShape3D_WriteLE16(shapePayload + 0x04u, (uint16_t)(flags | 0x0001u));
		result->preparedFlagSetAtCompletion = 1;
		result->returned = 1;
		return 1;
	}
	primitiveListOffset = SlipBytes_ReadLE32(shapePayload + 0x14u);
	result->primitiveListOffset = primitiveListOffset;
	if (primitiveListOffset == 0) {
		result->zeroPrimitiveList = 1;
		flags = SlipBytes_ReadLE16(shapePayload + 0x04u);
		SlipShape3D_WriteLE16(shapePayload + 0x04u, (uint16_t)(flags | 0x0001u));
		result->preparedFlagSetAtCompletion = 1;
		result->returned = 1;
		return 1;
	}
	if (!SlipShape3D_RangeInside(shapeBytes, primitiveListOffset, 2u)) {
		return 0;
	}
	recordOffset = primitiveListOffset;
	result->primitiveListCursor = recordOffset;
	primitiveCount = SlipBytes_ReadLE16(shapePayload + recordOffset);
	result->primitiveCount = primitiveCount;
	if (primitiveCount == 0) {
		return 0;
	}
	flags = SlipBytes_ReadLE16(shapePayload + 0x04u);
	SlipShape3D_WriteLE16(shapePayload + 0x04u, (uint16_t)(flags | 0x0001u));
	result->preparedFlagSetBeforePrimitives = 1;
	recordOffset += 2u;
	result->firstPrimitiveRecord = recordOffset;
	for (i = 0; i < primitiveCount; ++i) {
		SlipShape3DPreparePrimitiveVisit visit;
		uint16_t sourceMaterialIndex;
		uint16_t preparedMaterialIndex;

		if (!SlipShape3D_RangeInside(shapeBytes, recordOffset, 0x0au)) {
			return 0;
		}
		memset(&visit, 0, sizeof(visit));
		visit.index = i;
		visit.primitiveRecordOffset = recordOffset;
		visit.remainingCountSaved = 1;
		visit.shapePointerSaved = 1;
		sourceMaterialIndex = SlipBytes_ReadLE16(shapePayload + recordOffset + 0x08u);
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
		SlipShape3D_WriteLE16(shapePayload + recordOffset + 0x08u, preparedMaterialIndex);
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
	if (!SlipShape3D_RangeInside(shapeBytes, materialListOffset, 2u)) {
		return 0;
	}
	recordOffset = materialListOffset;
	result->materialListCursor = recordOffset;
	materialCount = SlipBytes_ReadLE16(shapePayload + recordOffset);
	result->materialCount = materialCount;
	if (materialCount == 0) {
		return 0;
	}
	recordOffset += 2u;
	result->firstMaterialRecord = recordOffset;
	for (i = 0; i < materialCount; ++i) {
		SlipShape3DPrepareMaterialVisit visit;

		if (!SlipShape3D_RangeInside(shapeBytes, recordOffset, 0x12u)) {
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
			SlipShape3D_WriteLE16(shapePayload + recordOffset + 0x10u, visit.preparedMaterialIndex);
		}
		recordOffset += 0x12u;
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

	flags = SlipBytes_ReadLE16(shapePayload + 0x04u);
	SlipShape3D_WriteLE16(shapePayload + 0x04u, (uint16_t)(flags | 0x0001u));
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

	fileSize = SlipBytes_ReadLE32(data + 0x08);
	bspOffset = SlipBytes_ReadLE32(data + 0x0c);
	vertexOffset = SlipBytes_ReadLE32(data + 0x10);
	primitiveOffset = SlipBytes_ReadLE32(data + 0x14);
	materialListOffset = SlipBytes_ReadLE32(data + 0x18);

	if (fileSize != 0 && fileSize > size) {
		return 0;
	}
	if (!SlipShape3D_RangeInside(size, vertexOffset, 2)) {
		return 0;
	}
	if (bspOffset != 0 && !SlipShape3D_RangeInside(size, bspOffset, 2)) {
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
	shape->version = SlipBytes_ReadLE16(data + 0x00);
	shape->scaleShift = SlipBytes_ReadLE16(data + 0x02);
	shape->flags = SlipBytes_ReadLE16(data + 0x04);
	shape->fileSize = fileSize;
	shape->bspOffset = bspOffset;
	shape->vertexOffset = vertexOffset;
	shape->primitiveOffset = primitiveOffset;
	shape->materialListOffset = materialListOffset;
	shape->boundingRadius = SlipShape3D_RangeInside(size, 0x1cu, 4) ? (int32_t)SlipBytes_ReadLE32(data + 0x1cu) : 0;
	shape->sortListMode = SlipShape3D_RangeInside(size, 0x38u, 2) ? SlipBytes_ReadLE16(data + 0x38u) : 0;
	return 1;
}

int SlipShape3D_GetVertexCount(const SlipShape3D *shape, uint16_t *count) {
	if (shape == NULL || count == NULL || !SlipShape3D_RangeInside(shape->size, shape->vertexOffset, 2)) {
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
	offset = shape->vertexOffset + 2u + (uint32_t)index * 6u;
	if (!SlipShape3D_RangeInside(shape->size, offset, 6)) {
		return 0;
	}

	vertex->x = (int16_t)SlipBytes_ReadLE16(shape->data + offset);
	vertex->y = (int16_t)SlipBytes_ReadLE16(shape->data + offset + 2u);
	vertex->z = (int16_t)SlipBytes_ReadLE16(shape->data + offset + 4u);
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
	primitive->countAndFlags = SlipBytes_ReadLE16(record + 0x00);
	primitive->normalX = SlipBytes_ReadLE16(record + 0x02);
	primitive->normalY = SlipBytes_ReadLE16(record + 0x04);
	primitive->normalZ = SlipBytes_ReadLE16(record + 0x06);
	primitive->materialIndex = SlipBytes_ReadLE16(record + 0x08);
	primitive->flags = SlipBytes_ReadLE16(record + 0x0a);
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

	vertexCount = (uint16_t)(primitive->countAndFlags & 0x3fffu);
	indexBytes = (uint32_t)vertexCount * 2u;
	if (!SlipShape3D_RangeInside(shape->size, primitive->streamOffset, indexBytes)) {
		return 0;
	}

	memset(stream, 0, sizeof(*stream));
	stream->vertexCount = vertexCount;
	stream->indexOffset = primitive->streamOffset;
	if ((primitive->countAndFlags & 0x4000u) != 0) {
		extraPayloadOffset = primitive->streamOffset + indexBytes;
		if (!SlipShape3D_RangeInside(shape->size, extraPayloadOffset, (uint32_t)vertexCount * 6u)) {
			return 0;
		}
		stream->hasExtendedPayload = 1;
		stream->extraPayloadOffset = extraPayloadOffset;
		stream->extraPayloadStride = 6;
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

	if (shapePayload == NULL || classify == NULL || result == NULL || shapeBytes < 0x18u) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->shapePointerSaved = 1;
	offset = SlipBytes_ReadLE32(shapePayload + 0x14u);
	result->primitiveListOffset = offset;
	result->primitiveListCursor = offset;
	if (!SlipShape3D_RangeInside(shapeBytes, offset, 2u)) {
		return 0;
	}
	count = SlipBytes_ReadLE16(shapePayload + offset);
	result->primitiveCount = count;
	if (count == 0) {
		return 0;
	}
	offset += 2u;
	result->firstPrimitiveRecord = offset;
	for (i = 0; i < count; ++i) {
		SlipShape3DPrimitiveListVisit visit;
		SlipShape3DPrimitiveAdvanceWrapper advance;
		int carry;

		if (!SlipShape3D_RangeInside(shapeBytes, offset, 0x0eu)) {
			return 0;
		}
		memset(&visit, 0, sizeof(visit));
		visit.index = i;
		visit.primitiveRecordOffset = offset;
		visit.remainingCountSaved = 1;
		visit.normalX = SlipBytes_ReadLE16(shapePayload + offset + 0x02u);
		visit.normalY = SlipBytes_ReadLE16(shapePayload + offset + 0x04u);
		visit.normalZ = SlipBytes_ReadLE16(shapePayload + offset + 0x06u);
		visit.planeDistance = SlipBytes_ReadLE16(shapePayload + offset + 0x0cu);
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

	if (shape == NULL || callback == NULL || !SlipShape3D_RangeInside(shape->size, shape->primitiveOffset, 2)) {
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
	child0 = SlipBytes_ReadLE32(node + 0x00);
	child1 = SlipBytes_ReadLE32(node + 0x04);
	primitiveOffset = SlipBytes_ReadLE32(node + 0x08);
	bspWord0c = SlipBytes_ReadLE16(node + 0x0c);
	planeIndex = SlipBytes_ReadLE16(node + 0x10);

	if (planeIndex == 0xffffu) {
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
	if (!SlipShape3D_RangeInside(shape->size, shape->bspOffset, 2)) {
		return 0;
	}

	return SlipShape3D_TraverseNode(shape, shape->bspOffset, 2, classify, callback, userData, 0);
}

uint32_t SlipShape3D_initialized;

void SlipShape3D_Shutdown(void) {
	if (SlipShape3D_initialized != 0) {
		SlipShape3D_initialized = 0;
	}
}
