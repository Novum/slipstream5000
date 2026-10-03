#include "draw3d.h"
#include "byte_order.h"
#include "fixed_point.h"
#include "gpu/clip.h"
#include "gpu/renderer.h"
#include "material_format.h"
#include "raster/raster.h"
#include "renderer_flags.h"
#include "shape_format.h"

#include "runtime.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum {
	SLIP_DRAW3D_WORD_ALIGNED_INDEX_MASK = UINT16_MAX & ~1u,
	SLIP_DRAW3D_ROOT16_TRIAL_BIT = 1u << 14,
	SLIP_DRAW3D_ROOT32_TRIAL_BIT = 1u << 30,
	SLIP_DRAW3D_INTERPOLATION_FRACTION_BITS = 30,
	SLIP_DRAW3D_INTERPOLATION_TO_Q14_SHIFT = SLIP_DRAW3D_INTERPOLATION_FRACTION_BITS - SLIP_Q14_FRACTION_BITS,
	SLIP_DRAW3D_SHADE_HIGH_BYTE_MASK = UINT8_MAX << SLIP_SHADE_COLOUR_SHIFT,
	SLIP_BACKGROUND_FILL_ENABLED = UINT16_MAX,
	SLIP_BACKGROUND_NO_PREVIOUS_STRIP = UINT16_MAX,
	SLIP_BACKGROUND_STRIP_CORNER_COUNT = 4,
	SLIP_BACKGROUND_STRIP_BOUNDARY_PAIR_COUNT = 2,
	SLIP_BACKGROUND_FIXED_STRIP_RESERVED_ENTRIES = 2,
	SLIP_BACKGROUND_STRIP_VALUE_FRACTION_BITS = 16,
	SLIP_BACKGROUND_STRIP_CURVATURE_PRESCALE_BITS = 2,
	SLIP_BACKGROUND_STRIP_CORNER_POINTER_BYTES = sizeof(uint32_t),
	SLIP_DRAW3D_POINT_POINTER_BYTES = sizeof(uint32_t),
	SLIP_DRAW3D_VERTEX_SHADE_CONTROL_BYTES = sizeof(uint16_t),
	SLIP_DRAW3D_MATERIAL_CALLBACK_CAPACITY = 32,
	SLIP_DRAW3D_POST_PLANE_SHORT_EDGE_MAXIMUM_LENGTH = 4
};

static const uint32_t SLIP_DRAW3D_UPPER_WORD_MASK = UINT32_MAX ^ UINT16_MAX;
static const uint32_t SLIP_DRAW3D_DWORD_SIGN_BIT = UINT32_C(1) << 31;
static const uint32_t SLIP_DRAW3D_EVEN_SHIFT_COUNT_MASK = UINT32_MAX & ~1u;

static uint8_t standalonePointBuffer[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT * sizeof(RasterTexturedPoint)];
static uint8_t *boundPointBuffer = standalonePointBuffer;

void SlipDraw3D_BindPointBuffer(uint8_t *points) { boundPointBuffer = points != NULL ? points : standalonePointBuffer; }

uint8_t *SlipDraw3D_PointBuffer(void) { return boundPointBuffer; }

static void SlipDraw3D_WriteLE16(uint8_t *p, uint16_t v) {
	p[0] = (uint8_t)(v & UINT8_MAX);
	p[1] = (uint8_t)(v >> 8);
}

static void SlipDraw3D_WriteLE32(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t)(v & UINT8_MAX);
	p[1] = (uint8_t)((v >> 8) & UINT8_MAX);
	p[2] = (uint8_t)((v >> 16) & UINT8_MAX);
	p[3] = (uint8_t)((v >> 24) & UINT8_MAX);
}

static uint16_t SlipDraw3D_SignedProductShift14LowWord(int32_t product) {
	const uint16_t productLowWord = (uint16_t)product;
	const uint16_t productHighWord = (uint16_t)((uint32_t)product >> 16);

	return (uint16_t)((uint16_t)(productLowWord >> SLIP_Q14_FRACTION_BITS) |
	                  (uint16_t)(productHighWord << SLIP_Q14_WORD_HIGH_SHIFT));
}

static uint16_t SlipDraw3D_UnsignedProductShift14LowWord(uint32_t product) {
	const uint16_t productLowWord = (uint16_t)product;
	const uint16_t productHighWord = (uint16_t)(product >> 16);

	return (uint16_t)((uint16_t)(productLowWord >> SLIP_Q14_FRACTION_BITS) |
	                  (uint16_t)(productHighWord << SLIP_Q14_WORD_HIGH_SHIFT));
}

static uint16_t SlipDraw3D_MultiplySignedWordsShift14WithRoundingBit(uint16_t multiplicand, uint16_t operand,
                                                                     bool *carryOut) {
	const int32_t product = (int32_t)(int16_t)multiplicand * (int32_t)(int16_t)operand;
	const uint16_t productLowWord = (uint16_t)product;
	const uint16_t productHighWord = (uint16_t)((uint32_t)product >> 16);

	if (carryOut != NULL) {
		*carryOut = ((productLowWord >> (SLIP_Q14_FRACTION_BITS - 1)) & 1u) != 0u;
	}
	return (uint16_t)((uint16_t)(productLowWord >> SLIP_Q14_FRACTION_BITS) |
	                  (uint16_t)(productHighWord << SLIP_Q14_WORD_HIGH_SHIFT));
}

uint16_t g_spriteScaleX;
uint16_t g_spriteScaleY;
uint16_t g_spriteHalfWidth;
uint16_t g_spriteHalfHeight;
uint32_t SlipDraw3D_minimumDepth;
uint32_t SlipDraw3D_maximumDepth;
uint32_t SlipDraw3D_fadeStart;
uint32_t SlipDraw3D_fadeEnd;
uint32_t SlipDraw3D_fadeRange;
uint32_t SlipDraw3D_fadeColour;
uint32_t SlipDraw3D_directLight;
uint32_t SlipDraw3D_ambientLight;
int32_t SlipDraw3D_lightX;
int32_t SlipDraw3D_lightY;
int32_t SlipDraw3D_lightZ;

static void SlipDraw3D_RefreshPerspectiveScale(SlipDraw3DProjectState *state);
static void SlipDraw3D_RefreshProjectionState(SlipDraw3DProjectState *state);

void SlipDraw3D_SetMinimumDepth(uint32_t minimumDepth) { SlipDraw3D_minimumDepth = minimumDepth; }

void SlipDraw3D_SetMaximumDepth(uint32_t maximumDepth) { SlipDraw3D_maximumDepth = maximumDepth; }

void SlipDraw3D_SetDepthFade(uint32_t fadeStart, uint32_t fadeEnd, uint16_t fadeColour) {
	SlipDraw3D_fadeStart = fadeStart;
	if (fadeStart != 0) {
		SlipDraw3D_fadeColour = (SlipDraw3D_fadeColour & SLIP_DRAW3D_UPPER_WORD_MASK) | fadeColour;
		SlipDraw3D_fadeEnd = fadeEnd;
		SlipDraw3D_fadeRange = fadeEnd - fadeStart;
	}
}

void SlipDraw3D_ResetLighting(void) {
	SlipDraw3D_directLight = 0;
	SlipDraw3D_fadeStart = 0;
}

void SlipDraw3D_NormalizeLighting(void) {
	const uint32_t totalLight = SlipDraw3D_directLight + SlipDraw3D_ambientLight;

	if ((int32_t)totalLight > SLIP_Q14_ONE) {
		const uint32_t lightNormalizationScale = (uint32_t)(SLIP_Q14_ONE * SLIP_Q14_ONE) / totalLight;
		uint64_t product = (uint64_t)SlipDraw3D_directLight * lightNormalizationScale;

		SlipDraw3D_directLight = (uint32_t)(product >> SLIP_Q14_FRACTION_BITS);
		product = (uint64_t)SlipDraw3D_ambientLight * lightNormalizationScale;
		SlipDraw3D_ambientLight = (uint32_t)(product >> SLIP_Q14_FRACTION_BITS);
	}
}

void SlipDraw3D_SetAmbientLight(uint16_t ambientLight) {
	SlipDraw3D_ambientLight = ambientLight;
	SlipDraw3D_NormalizeLighting();
}

void SlipDraw3D_SetLightVector(int32_t lightX, int32_t lightY, int32_t lightZ, uint16_t directLight) {
	SlipDraw3D_lightX = lightX;
	SlipDraw3D_lightY = lightY;
	SlipDraw3D_lightZ = lightZ;
	SlipDraw3D_directLight = (SlipDraw3D_directLight & SLIP_DRAW3D_UPPER_WORD_MASK) | directLight;
	SlipDraw3D_NormalizeLighting();
}

static uint16_t SlipDraw3D_MultiplySignedWordsShift13WithRoundingBit(uint16_t multiplicand, uint16_t operand,
                                                                     bool *carryOut) {
	const int32_t product = (int32_t)(int16_t)multiplicand * (int32_t)(int16_t)operand;
	const uint16_t productLowWord = (uint16_t)product;
	const uint16_t productHighWord = (uint16_t)((uint32_t)product >> 16);

	if (carryOut != NULL) {
		*carryOut = ((productLowWord >> (SLIP_Q14_FRACTION_BITS - 2)) & 1u) != 0u;
	}
	return (uint16_t)((uint16_t)(productLowWord >> (SLIP_Q14_FRACTION_BITS - 1)) |
	                  (uint16_t)(productHighWord << (SLIP_Q14_WORD_HIGH_SHIFT + 1)));
}

static int SlipDraw3D_LinkedRecordOffsetValid(size_t recordBytes, uint32_t offset) {
	return (size_t)offset + SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE <= recordBytes;
}

static int SlipDraw3D_LinkedRecordOffsetAligned(uint32_t offset) {
	return (offset % SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE) == 0;
}

static SlipDraw3DRecordPool g_drawRecordPool;

static SlipDraw3DLinkedDrawRecord *SlipDraw3D_RecordPoolLinkedRecord(SlipDraw3DRecordPool *pool,
                                                                     uint32_t recordOffset) {
	if (pool == NULL || !SlipDraw3D_LinkedRecordOffsetAligned(recordOffset) ||
	    !SlipDraw3D_LinkedRecordOffsetValid(SlipDraw3D_RecordPoolByteSize(), recordOffset)) {
		return NULL;
	}
	return &pool->records[recordOffset / SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE];
}

static uint32_t SlipDraw3D_RecordNext(const uint8_t *recordBase, uint32_t recordOffset) {
	return SlipBytes_ReadLE32(recordBase + recordOffset + SLIP_DRAW3D_RECORD_NEXT_OFFSET);
}

static uint32_t SlipDraw3D_RecordPrev(const uint8_t *recordBase, uint32_t recordOffset) {
	return SlipBytes_ReadLE32(recordBase + recordOffset + SLIP_DRAW3D_RECORD_PREV_OFFSET);
}

static void SlipDraw3D_SetRecordNext(uint8_t *recordBase, uint32_t recordOffset, uint32_t nextOffset) {
	SlipDraw3D_WriteLE32(recordBase + recordOffset + SLIP_DRAW3D_RECORD_NEXT_OFFSET, nextOffset);
}

static void SlipDraw3D_SetRecordPrev(uint8_t *recordBase, uint32_t recordOffset, uint32_t prevOffset) {
	SlipDraw3D_WriteLE32(recordBase + recordOffset + SLIP_DRAW3D_RECORD_PREV_OFFSET, prevOffset);
}

static uint8_t SlipDraw3D_UppercaseAscii(uint8_t character) {
	if (character >= (uint8_t)'a' && character <= (uint8_t)'z') {
		character = (uint8_t)(character - ('a' - 'A'));
	}
	return character;
}

static void SlipDraw3D_ExpandMaterialRecord(uint8_t *expandedRecord, const uint8_t *rawRecord) {
	uint16_t textureShift;
	uint8_t rampStart;
	uint8_t rampEnd;
	uint32_t rampDelta;
	uint32_t textureMask;
	uint32_t rampEndAdjusted;
	size_t i;
	SlipDraw3DMaterialRecord *const material = (void *)expandedRecord;

	memcpy(material->name, rawRecord, sizeof(material->name));
	material->textureTransparency = (int16_t)(int8_t)rawRecord[SLIP_MAT_TRANSPARENCY_OFFSET];
	material->skipFlatPolygon = (int16_t)(int8_t)rawRecord[SLIP_MAT_SKIP_FLAT_OFFSET];
	material->fixedShade = SlipBytes_ReadLE16(rawRecord + SLIP_MAT_FIXED_SHADE_OFFSET);
	material->ambientCoefficient = SlipBytes_ReadLE16(rawRecord + SLIP_MAT_AMBIENT_OFFSET);
	material->diffuseCoefficient = SlipBytes_ReadLE16(rawRecord + SLIP_MAT_DIFFUSE_OFFSET);
	material->specularCoefficient = SlipBytes_ReadLE16(rawRecord + SLIP_MAT_SPECULAR_OFFSET);
	material->vertexShading = (uint32_t)(int32_t)(int8_t)rawRecord[SLIP_MAT_VERTEX_SHADING_OFFSET];
	textureShift = SlipBytes_ReadLE16(rawRecord + SLIP_MAT_DITHER_BITS_OFFSET);
	material->ditherBits = textureShift;
	textureMask = (1u << (textureShift & SLIP_MAT_DITHER_SHIFT_MASK)) - 1u;
	rampStart = rawRecord[SLIP_MAT_RAMP_START_OFFSET];
	rampEnd = rawRecord[SLIP_MAT_RAMP_END_OFFSET];
	rampDelta = (uint32_t)rampEnd - (uint32_t)rampStart;
	if (rampDelta > SLIP_MAT_MAXIMUM_RAMP_RANGE) {
		rampDelta = SLIP_MAT_MAXIMUM_RAMP_RANGE;
	}
	rampEndAdjusted = (uint32_t)rampStart + rampDelta - textureMask;
	material->rampStart = rampStart;
	material->rampEnd = rampEndAdjusted;
	material->importedMaterialByte = rawRecord[SLIP_MAT_IMPORTED_BYTE_OFFSET];
	for (i = 0; i < sizeof(material->name); ++i) {
		material->name[i] = (char)SlipDraw3D_UppercaseAscii((uint8_t)material->name[i]);
	}
	memcpy(material->textureName, rawRecord + SLIP_MAT_TEXTURE_NAME_OFFSET, sizeof(material->textureName));
}

int SlipDraw3D_SetMaterialsNoExisting(const uint8_t *rawMaterialPayload, size_t rawMaterialPayloadBytes,
                                      uint16_t existingMaterialGlobal, uint16_t allocatedResourceHandle,
                                      uint8_t *expandedMaterialTable, size_t expandedMaterialTableBytes,
                                      SlipDraw3DMaterialInstall *result) {
	uint16_t count;
	uint16_t version;
	uint32_t allocationBytes;
	const uint8_t *rawRecordCursor;
	uint8_t *expandedRecordCursor;
	uint16_t remainingMaterials;

	if (result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->savedGeneralState = true;
	result->clearGlobal = true;
	result->existingMaterialGlobal = existingMaterialGlobal;
	if (rawMaterialPayload == NULL || expandedMaterialTable == NULL ||
	    rawMaterialPayloadBytes < SLIP_MAT_HEADER_BYTES) {
		return 0;
	}

	count = SlipBytes_ReadLE16(rawMaterialPayload);
	version = SlipBytes_ReadLE16(rawMaterialPayload + SLIP_MAT_VERSION_OFFSET);
	result->count = count;
	result->version = version;
	if (version != SLIP_MAT_VERSION) {
		result->jumpWrongVersion = true;
		return 0;
	}
	if (existingMaterialGlobal != 0u) {
		return 0;
	}
	result->noExistingMaterialsBranch = true;
	allocationBytes =
	    (uint32_t)count * SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE + SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES;
	result->allocationBytes = allocationBytes;
	if (rawMaterialPayloadBytes < SLIP_MAT_HEADER_BYTES + (size_t)count * SLIP_DRAW3D_RAW_MATERIAL_RECORD_SIZE ||
	    expandedMaterialTableBytes < (size_t)allocationBytes) {
		return 0;
	}

	result->storedMaterialGlobal = allocatedResourceHandle;
	result->tableCount = count;
	SlipDraw3D_WriteLE32(expandedMaterialTable, count);
	rawRecordCursor = rawMaterialPayload + SLIP_MAT_HEADER_BYTES;
	expandedRecordCursor = expandedMaterialTable + SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES;
	remainingMaterials = count;
	while (remainingMaterials != 0u) {
		SlipDraw3D_ExpandMaterialRecord(expandedRecordCursor, rawRecordCursor);
		expandedRecordCursor += SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE;
		rawRecordCursor += SLIP_DRAW3D_RAW_MATERIAL_RECORD_SIZE;
		--remainingMaterials;
		++result->recordsExpanded;
	}

	SlipDraw3D_NotifyMaterials();
	result->callDraw3DNotifyMaterials = true;
	result->calledLoadMaterialFrameSlots = true;
	result->restoredGeneralState = true;
	result->returned = true;
	return 1;
}

int SlipDraw3D_SetMaterialsAppend(const uint8_t *existingMaterialTable, size_t existingMaterialTableBytes,
                                  uint16_t existingMaterialGlobal, const uint8_t *rawMaterialPayload,
                                  size_t rawMaterialPayloadBytes, uint16_t allocatedResourceHandle,
                                  uint8_t *expandedMaterialTable, size_t expandedMaterialTableBytes,
                                  SlipDraw3DMaterialAppend *result) {
	uint16_t count;
	uint16_t version;
	uint32_t existingTableCount;
	uint32_t appendedTableCount;
	uint32_t existingTableAllocationBytes;
	uint32_t outputAllocationBytes;
	const uint8_t *rawRecordCursor;
	uint8_t *expandedRecordCursor;
	uint16_t remainingMaterials;
	uint32_t copiedExistingRecords;

	if (result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->savedGeneralState = true;
	result->clearGlobal = true;
	result->existingMaterialGlobal = existingMaterialGlobal;
	if (existingMaterialTable == NULL || rawMaterialPayload == NULL || expandedMaterialTable == NULL ||
	    existingMaterialTableBytes < SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES ||
	    rawMaterialPayloadBytes < SLIP_MAT_HEADER_BYTES) {
		return 0;
	}

	count = SlipBytes_ReadLE16(rawMaterialPayload);
	version = SlipBytes_ReadLE16(rawMaterialPayload + SLIP_MAT_VERSION_OFFSET);
	result->count = count;
	result->version = version;
	if (version != SLIP_MAT_VERSION) {
		result->jumpWrongVersion = true;
		return 0;
	}
	if (existingMaterialGlobal == 0u) {
		return 0;
	}
	result->appendBranch = true;
	existingTableCount = SlipBytes_ReadLE32(existingMaterialTable);
	result->existingTableCount = existingTableCount;
	existingTableAllocationBytes =
	    existingTableCount * SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE + SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES;
	result->existingTableAllocationBytes = existingTableAllocationBytes;
	appendedTableCount = existingTableCount + count;
	outputAllocationBytes =
	    appendedTableCount * SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE + SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES;
	result->appendedTableCount = appendedTableCount;
	result->outputAllocationBytes = outputAllocationBytes;
	if (existingMaterialTableBytes < (size_t)existingTableAllocationBytes ||
	    rawMaterialPayloadBytes < SLIP_MAT_HEADER_BYTES + (size_t)count * SLIP_DRAW3D_RAW_MATERIAL_RECORD_SIZE ||
	    expandedMaterialTableBytes < (size_t)outputAllocationBytes) {
		return 0;
	}

	result->storedMaterialGlobal = allocatedResourceHandle;
	SlipDraw3D_WriteLE32(expandedMaterialTable, appendedTableCount);
	copiedExistingRecords = existingTableCount;
	memcpy(expandedMaterialTable + SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES,
	       existingMaterialTable + SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES,
	       (size_t)existingTableCount * SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE);
	result->copiedExistingRecords = copiedExistingRecords;

	rawRecordCursor = rawMaterialPayload + SLIP_MAT_HEADER_BYTES;
	expandedRecordCursor = expandedMaterialTable + SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES +
	                       (size_t)existingTableCount * SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE;
	remainingMaterials = count;
	while (remainingMaterials != 0u) {
		SlipDraw3D_ExpandMaterialRecord(expandedRecordCursor, rawRecordCursor);
		expandedRecordCursor += SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE;
		rawRecordCursor += SLIP_DRAW3D_RAW_MATERIAL_RECORD_SIZE;
		--remainingMaterials;
		++result->recordsExpanded;
	}

	SlipDraw3D_NotifyMaterials();
	result->callDraw3DNotifyMaterials = true;
	result->calledLoadMaterialFrameSlots = true;
	result->restoredGeneralState = true;
	result->returned = true;
	return 1;
}

static void SlipDraw3D_WriteI32(uint8_t *p, int32_t v) { SlipDraw3D_WriteLE32(p, (uint32_t)v); }

static uint32_t SlipDraw3D_SignedWordToFixed14(uint32_t inputWord) {
	uint16_t signedHighWord = (uint16_t)inputWord;
	uint16_t fractionLowWord = 0;
	unsigned i;

	for (i = 0; i < 2u; ++i) {
		const uint16_t carry = (uint16_t)(signedHighWord & 1u);

		signedHighWord = (uint16_t)((int16_t)signedHighWord >> 1);
		fractionLowWord = (uint16_t)((uint16_t)(carry << 15) | (uint16_t)(fractionLowWord >> 1));
	}
	return ((uint32_t)signedHighWord << 16) | fractionLowWord;
}

static uint64_t SlipDraw3D_SignedHighHalfShiftRightTwo(uint32_t highValue) {
	uint32_t highPart = highValue;
	uint32_t lowPart = 0;
	unsigned i;

	for (i = 0; i < 2u; ++i) {
		const uint32_t carry = highPart & 1u;

		highPart = (uint32_t)((int32_t)highPart >> 1);
		lowPart = (carry << 31) | (lowPart >> 1);
	}
	return ((uint64_t)highPart << 32) | lowPart;
}

static uint64_t SlipDraw3D_UnsignedHighHalfShiftRightTwo(uint32_t highValue) {
	uint32_t highPart = highValue;
	uint32_t lowPart = 0;
	unsigned i;

	for (i = 0; i < 2u; ++i) {
		const uint32_t carry = highPart & 1u;

		highPart >>= 1;
		lowPart = (carry << 31) | (lowPart >> 1);
	}
	return ((uint64_t)highPart << 32) | lowPart;
}

static int32_t SlipDraw3D_MultiplySigned32Shift30(int32_t value, uint32_t ratio) {
	const int64_t product = (int64_t)value * (int64_t)(int32_t)ratio;

	return (int32_t)(uint32_t)(((uint64_t)product) >> SLIP_DRAW3D_INTERPOLATION_FRACTION_BITS);
}

static uint16_t SlipDraw3D_MultiplySigned16Shift14LowWord(int16_t value, int16_t ratio) {
	const int32_t product = (int32_t)value * (int32_t)ratio;

	return (uint16_t)(((uint32_t)product) >> SLIP_Q14_FRACTION_BITS);
}

static int32_t SlipDraw3D_MultiplySigned16Shift14PreserveDeltaHigh(int32_t delta, int16_t ratio) {
	const int32_t product = (int32_t)(int16_t)delta * (int32_t)ratio;
	const uint16_t low = (uint16_t)(((uint32_t)product) >> SLIP_Q14_FRACTION_BITS);

	return (int32_t)(((uint32_t)delta & SLIP_DRAW3D_UPPER_WORD_MASK) | low);
}

static uint32_t SlipDraw3D_ShiftSignedWordToFractionByteTwice(uint32_t inputWord) {
	uint16_t signedHighWord = (uint16_t)inputWord;
	uint8_t fractionByte = 0;
	unsigned i;

	for (i = 0; i < 2u; ++i) {
		const uint8_t carry = (uint8_t)(signedHighWord & 1u);

		signedHighWord = (uint16_t)((int16_t)signedHighWord >> 1);
		fractionByte = (uint8_t)((uint8_t)(carry << 7) | (uint8_t)(fractionByte >> 1));
	}
	return ((uint32_t)signedHighWord << 16) | ((uint32_t)fractionByte << 8);
}

static uint32_t SlipDraw3D_SignedWordToFixed15(uint32_t inputWord) {
	uint16_t signedHighWord = (uint16_t)inputWord;
	uint16_t fractionLowWord = 0;
	const uint16_t carry = (uint16_t)(signedHighWord & 1u);

	signedHighWord = (uint16_t)((int16_t)signedHighWord >> 1);
	fractionLowWord = (uint16_t)((uint16_t)(carry << 15) | (uint16_t)(fractionLowWord >> 1));
	return ((uint32_t)signedHighWord << 16) | fractionLowWord;
}

static int32_t SlipDraw3D_MultiplySigned32RoundShift30(int32_t value, uint32_t ratio) {
	const int64_t product = (int64_t)value * (int64_t)(int32_t)ratio;
	const uint64_t productBits = (uint64_t)product;
	const uint32_t low = (uint32_t)productBits;
	const uint32_t high = (uint32_t)(productBits >> 32);
	uint32_t out = (low >> SLIP_CLIP_PRECISE_FRACTION_BITS) | (high << (32 - SLIP_CLIP_PRECISE_FRACTION_BITS));

	if (((low >> SLIP_CLIP_PRECISE_ROUND_BIT) & 1u) != 0) {
		++out;
	}
	return (int32_t)out;
}

static int32_t SlipDraw3D_MultiplySigned32RoundShift14(int32_t value, uint32_t ratio) {
	const int64_t product = (int64_t)value * (int64_t)(int32_t)ratio;
	const uint64_t productBits = (uint64_t)product;
	const uint32_t low = (uint32_t)productBits;
	const uint32_t high = (uint32_t)(productBits >> 32);
	uint32_t out = (low >> SLIP_Q14_FRACTION_BITS) | (high << SLIP_Q14_DWORD_HIGH_SHIFT);

	if (((low >> (SLIP_Q14_FRACTION_BITS - 1)) & 1u) != 0) {
		++out;
	}
	return (int32_t)out;
}

static int SlipDraw3D_DivideSignedChecked32(int64_t dividend, int32_t divisor, int32_t *quotient) {
	int64_t q;

	if (divisor == 0 || quotient == NULL) {
		return 0;
	}
	q = dividend / divisor;
	if (q < INT32_MIN || q > INT32_MAX) {
		return 0;
	}
	*quotient = (int32_t)q;
	return 1;
}

static int32_t SlipDraw3D_MultiplySigned32AddOverflowBitBeforeShift30(int32_t value, uint32_t ratio) {
	const int64_t product = (int64_t)value * (int64_t)(int32_t)ratio;
	const uint64_t productBits = (uint64_t)product;
	uint32_t low = (uint32_t)productBits;
	const uint32_t high = (uint32_t)(productBits >> 32);
	const uint32_t carry = product < INT32_MIN || product > INT32_MAX;

	low += carry;
	return (int32_t)((low >> SLIP_CLIP_PRECISE_FRACTION_BITS) | (high << (32 - SLIP_CLIP_PRECISE_FRACTION_BITS)));
}

static int32_t SlipDraw3D_MultiplySigned16RoundShift14LowWord(int16_t value, uint16_t ratio) {
	const int32_t product = (int32_t)value * (int32_t)(int16_t)ratio;
	const uint32_t productBits = (uint32_t)product;
	const uint16_t low = (uint16_t)productBits;
	const uint16_t high = (uint16_t)(productBits >> 16);
	uint16_t out = (uint16_t)((low >> SLIP_Q14_FRACTION_BITS) | (uint16_t)(high << SLIP_Q14_WORD_HIGH_SHIFT));

	if (((low >> (SLIP_Q14_FRACTION_BITS - 1)) & 1u) != 0) {
		++out;
	}
	return (int32_t)(int16_t)out;
}

static int32_t SlipDraw3D_MultiplySigned16RoundShift14(int16_t value, uint16_t ratio) {
	const int32_t product = (int32_t)value * (int32_t)(int16_t)ratio;
	const uint32_t productBits = (uint32_t)product;
	int32_t out = (int32_t)product >> SLIP_Q14_FRACTION_BITS;

	if (((productBits >> (SLIP_Q14_FRACTION_BITS - 1)) & 1u) != 0) {
		++out;
	}
	return out;
}

static int32_t SlipDraw3D_PostPlaneDepth(int32_t recordX, int32_t planeX, int16_t planeNormalX, int32_t recordY,
                                         int32_t planeY, int16_t planeNormalY) {
	const uint32_t deltaX = (uint32_t)recordX - (uint32_t)planeX;
	const uint32_t deltaY = (uint32_t)recordY - (uint32_t)planeY;
	const int32_t productX = (int32_t)(int16_t)(uint16_t)deltaX * (int32_t)planeNormalX;
	const int32_t productY = (int32_t)(int16_t)(uint16_t)deltaY * (int32_t)planeNormalY;
	const uint32_t sum = (uint32_t)productX + (uint32_t)productY;

	return (int32_t)(sum << SLIP_POST_PLANE_DISTANCE_SHIFT);
}

static int SlipDraw3D_InterpolatePlaneIntersectionQ30(int32_t deltaX, int32_t deltaY, int32_t deltaZ,
                                                      int32_t targetDepth, int32_t otherDepth, int32_t *xStep,
                                                      int32_t *yStep, int32_t *zStep, uint32_t *interpolationRatio) {
	uint32_t negTargetDepth;
	uint32_t denominator;
	uint64_t numerator;
	uint64_t quotient;
	uint32_t ratio;

	if (xStep == NULL || yStep == NULL || zStep == NULL || interpolationRatio == NULL) {
		return 0;
	}
	negTargetDepth = (uint32_t)(0u - (uint32_t)targetDepth);
	denominator = (uint32_t)otherDepth + negTargetDepth;
	if (denominator == 0) {
		return 0;
	}
	numerator = SlipDraw3D_SignedHighHalfShiftRightTwo(negTargetDepth);
	quotient = numerator / denominator;
	if (quotient > UINT32_MAX) {
		return 0;
	}
	ratio = (uint32_t)quotient;
	*xStep = SlipDraw3D_MultiplySigned32AddOverflowBitBeforeShift30(deltaX, ratio);
	*yStep = SlipDraw3D_MultiplySigned32RoundShift30(deltaY, ratio);
	*zStep = SlipDraw3D_MultiplySigned32RoundShift30(deltaZ, ratio);
	*interpolationRatio = ratio;
	return 1;
}

static int SlipDraw3D_InterpolatePlaneIntersectionQ14(int32_t deltaX, int32_t deltaY, int32_t deltaZ,
                                                      int32_t targetDepth, int32_t otherDepth, int32_t *xStep,
                                                      int32_t *yStep, int32_t *zStep, uint32_t *interpolationRatio) {
	uint32_t negTargetDepth;
	uint16_t denominator;
	uint32_t numerator;
	uint32_t quotient;
	uint16_t ratio;

	if (xStep == NULL || yStep == NULL || zStep == NULL || interpolationRatio == NULL) {
		return 0;
	}
	negTargetDepth = (uint32_t)(0u - (uint32_t)targetDepth);
	denominator = (uint16_t)((uint32_t)otherDepth + negTargetDepth);
	if (denominator == 0) {
		return 0;
	}
	numerator = SlipDraw3D_ShiftSignedWordToFractionByteTwice(negTargetDepth);
	quotient = numerator / denominator;
	if (quotient > UINT16_MAX) {
		return 0;
	}
	ratio = (uint16_t)quotient;
	*xStep = SlipDraw3D_MultiplySigned16RoundShift14LowWord((int16_t)deltaX, ratio);
	*yStep = SlipDraw3D_MultiplySigned16RoundShift14LowWord((int16_t)deltaY, ratio);
	*zStep = SlipDraw3D_MultiplySigned16RoundShift14LowWord((int16_t)deltaZ, ratio);
	*interpolationRatio = ratio;
	return 1;
}

static int32_t SlipDraw3D_RoundedShiftRight14(int64_t v) {
	uint32_t out = (uint32_t)((uint64_t)v >> SLIP_Q14_FRACTION_BITS);

	if ((v & (INT64_C(1) << (SLIP_Q14_FRACTION_BITS - 1))) != 0) {
		++out;
	}
	return (int32_t)out;
}

void SlipDraw3D_SetAuxiliaryClipPlane(SlipDraw3DProjectState *state, SlipDraw3DVec32 origin, int16_t normalX,
                                      int16_t normalY, int16_t normalZ) {
	state->auxiliaryClipPlaneEnabled = UINT32_MAX;
	state->auxiliaryClipPlaneOrigin = origin;
	state->auxiliaryClipPlaneNormal = (SlipDraw3DVec32){normalX, normalY, normalZ};
}

void SlipDraw3D_ClearAuxiliaryClipPlane(SlipDraw3DProjectState *state) { state->auxiliaryClipPlaneEnabled = false; }

uint32_t SlipDraw3D_ClassifyShapeBounds(SlipDraw3DVec32 center, int32_t radius, const SlipDraw3DProjectState *state) {
	uint32_t renderFlags = 0;

	if (state == NULL) {
		return 0;
	}
	if (state->auxiliaryClipPlaneEnabled) {
		const int64_t planeDistance =
		    (int64_t)(center.x - state->auxiliaryClipPlaneOrigin.x) * state->auxiliaryClipPlaneNormal.x +
		    (int64_t)(center.y - state->auxiliaryClipPlaneOrigin.y) * state->auxiliaryClipPlaneNormal.y +
		    (int64_t)(center.z - state->auxiliaryClipPlaneOrigin.z) * state->auxiliaryClipPlaneNormal.z;

		if (SlipDraw3D_RoundedShiftRight14(planeDistance) <= radius) {
			renderFlags = SLIP_SHAPE_CLIP_AUXILIARY;
		}
	}
	if (radius < SLIP_DRAW3D_SHORT_COORDINATE_RADIUS_LIMIT) {
		renderFlags |= SLIP_SHAPE_SHORT_COORDINATES;
	}
	return renderFlags;
}

int SlipDraw3D_BuildVertexRecords(SlipDraw3DVertexRecord *records, size_t recordCapacity, const uint8_t *source,
                                  size_t sourceSize, uint16_t vertexCount, int16_t sourceStride,
                                  SlipDraw3DTransformFn transform, SlipDraw3DSourcePointFn sourcePoint,
                                  SlipDraw3DStateRecord *stateRecord, uint32_t vertexBufferCursor,
                                  uint32_t vertexBufferLimit, SlipDraw3DBuildVertexRecords *result) {
	size_t i;
	size_t sourceOffset = 0;
	uint32_t byteCount;
	uint32_t cursorAfter;
	bool carry;
	uint32_t effectiveLimit;

	if (records == NULL || source == NULL || vertexCount == 0 || sourceStride <= 0) {
		return 0;
	}
	if (result != NULL) {
		memset(result, 0, sizeof(*result));
		result->savedGeneralState = true;
		result->vertexCount = vertexCount;
		result->capacity = (uint32_t)recordCapacity;
	}
	if (recordCapacity < vertexCount) {
		if (result != NULL) {
			result->calledInitializeVertexBuffer = true;
			result->initialBufferCapacity = (uint32_t)vertexCount + SLIP_DRAW3D_VERTEX_BUFFER_GROWTH_RESERVE;
		}
		return 0;
	}
	byteCount = (uint32_t)vertexCount * SLIP_DRAW3D_VERTEX_RECORD_SIZE;
	cursorAfter = vertexBufferCursor + byteCount;
	carry = cursorAfter < vertexBufferCursor;
	effectiveLimit =
	    vertexBufferLimit != 0 ? vertexBufferLimit : (uint32_t)recordCapacity * SLIP_DRAW3D_VERTEX_RECORD_SIZE;
	if (result != NULL) {
		result->savedSourcePointCallback = true;
		result->savedTransformCallback = true;
		result->stateRecordPointer = stateRecord;
		result->vertexBufferCursorBefore = vertexBufferCursor;
		result->byteCount = byteCount;
		result->proposedVertexBufferCursor = cursorAfter;
		result->vertexBufferLimit = effectiveLimit;
		result->cursorOverflow = carry;
	}
	if (carry || cursorAfter > effectiveLimit) {
		const uint32_t overflowBytes = cursorAfter - effectiveLimit;
		const uint32_t growCount = overflowBytes / SLIP_DRAW3D_VERTEX_RECORD_SIZE;

		if (result != NULL) {
			result->calledGrowVertexBuffer = true;
			result->grownBufferCapacity =
			    growCount + SLIP_DRAW3D_VERTEX_BUFFER_GROWTH_RESERVE + (uint32_t)recordCapacity;
		}
		return 0;
	}
	if (stateRecord != NULL) {
		stateRecord->vertexBufferCursor = vertexBufferCursor;
		stateRecord->transform = transform;
		stateRecord->sourcePoint = sourcePoint;
	}
	if (result != NULL) {
		result->vertexBufferCursorAfter = cursorAfter;
		result->vertexRecordBase = vertexBufferCursor;
		result->storedVertexBufferCursor = vertexBufferCursor;
		result->stateRecordTransform = transform;
		result->transformCallback = transform;
		result->stateRecordSourcePoint = sourcePoint;
		result->sourcePointCallback = sourcePoint;
		result->sourceTailBytesUnsigned = (uint32_t)(uint16_t)(sourceStride - SLIP_SHAPE_VERTEX_BYTES);
		result->vertexRecordTailBytes =
		    SLIP_DRAW3D_VERTEX_RECORD_SIZE - (SLIP_DRAW3D_VERTEX_RECORD_SOURCE_OFFSET + SLIP_SHAPE_VERTEX_BYTES);
		result->initialLoopCount = vertexCount;
		result->initialVertexFlags = 0;
		result->sourceTailBytesSigned = (int32_t)(int16_t)(uint16_t)(sourceStride - SLIP_SHAPE_VERTEX_BYTES);
		result->loopCount = vertexCount;
	}

	for (i = 0; i < vertexCount; ++i) {
		if (sourceOffset > sourceSize || SLIP_SHAPE_VERTEX_BYTES > sourceSize - sourceOffset) {
			return 0;
		}
		SlipDraw3D_WriteLE32(records[i].bytes + SLIP_DRAW3D_VERTEX_RECORD_FLAGS_OFFSET, 0);
		memcpy(records[i].bytes + SLIP_DRAW3D_VERTEX_RECORD_SOURCE_OFFSET, source + sourceOffset,
		       SLIP_SHAPE_VERTEX_BYTES);
		sourceOffset += (size_t)sourceStride;
	}
	if (result != NULL) {
		result->restoredGeneralState = true;
		result->returned = true;
	}
	return 1;
}

int SlipDraw3D_InitVertexBuffer(uint32_t requestedCapacity, uint16_t allocatedResourceHandle, uint32_t baseFrom,
                                bool resourceAllocateAnonymousCarry, SlipDraw3DInitVertexBuffer *result) {
	uint64_t allocationProduct;
	uint32_t allocationBytes;

	if (result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->savedGeneralState = true;
	result->capacityAfter = requestedCapacity;
	result->vertexRecordBytes = SLIP_DRAW3D_VERTEX_RECORD_SIZE;
	allocationProduct = (uint64_t)requestedCapacity * SLIP_DRAW3D_VERTEX_RECORD_SIZE;
	result->allocationBytesLow = (uint32_t)allocationProduct;
	result->allocationBytesHigh = (uint32_t)(allocationProduct >> 32);
	result->allocationBytes = (uint32_t)allocationProduct;
	result->clearedAllocationFlags = true;
	result->callResourceAllocateAnonymous = true;
	result->resourceAllocateAnonymousCarry = resourceAllocateAnonymousCarry;
	if (resourceAllocateAnonymousCarry) {
		result->allocationErrorMessageOffset = SLIP_DRAW3D_VERTEX_ALLOCATION_ERROR_MESSAGE_DOS_OFFSET;
		result->jumpedToAllocationError = true;
		return 0;
	}
	allocationBytes = (uint32_t)allocationProduct;
	result->allocationHandle = allocatedResourceHandle;
	result->handleAfter = allocatedResourceHandle;
	result->calledLockVertexBuffer = true;
	result->cursorAfter = baseFrom;
	result->baseAfter = baseFrom;
	result->limitAfter = baseFrom + allocationBytes;
	result->restoredGeneralState = true;
	result->returned = true;
	return 1;
}

int SlipDraw3D_GrowVertexBuffer(uint32_t requestedCapacity, uint16_t handle, uint32_t base, uint32_t limit,
                                uint32_t cursor, uint32_t stateRecordBase, uint16_t stateRecordCount,
                                const uint8_t *oldBuffer, size_t oldBufferBytes, uint8_t *newBuffer,
                                size_t newBufferBytes, SlipDraw3DStateRecord *stateRecords,
                                uint16_t allocatedResourceHandle, uint32_t newBaseFrom,
                                bool resourceAllocateAnonymousCarry, SlipDraw3DGrowVertexBuffer *result) {
	uint64_t allocationProduct;
	uint32_t allocationBytes;
	uint32_t copyBytes;
	uint32_t baseDelta;
	uint16_t i;

	if (result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->savedGeneralState = true;
	result->pushedHandle = handle;
	result->pushedLimit = limit;
	result->pushedBase = base;
	result->capacityAfter = requestedCapacity;
	result->vertexRecordBytes = SLIP_DRAW3D_VERTEX_RECORD_SIZE;
	allocationProduct = (uint64_t)requestedCapacity * SLIP_DRAW3D_VERTEX_RECORD_SIZE;
	result->allocationBytesLow = (uint32_t)allocationProduct;
	result->allocationBytesHigh = (uint32_t)(allocationProduct >> 32);
	result->allocationBytes = (uint32_t)allocationProduct;
	result->clearedAllocationFlags = true;
	result->callResourceAllocateAnonymous = true;
	result->resourceAllocateAnonymousCarry = resourceAllocateAnonymousCarry;
	if (resourceAllocateAnonymousCarry) {
		result->allocationErrorMessageOffset = SLIP_DRAW3D_VERTEX_GROW_ERROR_MESSAGE_DOS_OFFSET;
		result->jumpedToAllocationError = true;
		return 0;
	}
	if (limit < base || newBuffer == NULL || oldBuffer == NULL || stateRecords == NULL || stateRecordCount == 0) {
		return 0;
	}
	allocationBytes = (uint32_t)allocationProduct;
	copyBytes = limit - base;
	if (newBufferBytes < allocationBytes || oldBufferBytes < copyBytes) {
		return 0;
	}

	result->allocationHandle = allocatedResourceHandle;
	result->handleAfter = allocatedResourceHandle;
	result->calledLockVertexBuffer = true;
	result->baseAfter = newBaseFrom;
	result->limitAfter = newBaseFrom + allocationBytes;
	baseDelta = newBaseFrom - base;
	result->baseDelta = baseDelta;
	result->stateRecordCount = stateRecordCount;
	result->stateRecordBase = stateRecordBase;
	for (i = 0; i < stateRecordCount; ++i) {
		stateRecords[i].vertexBufferCursor += baseDelta;
		++result->stateRecordUpdates;
	}
	result->cursorAfter = cursor + baseDelta;
	result->copyBytes = copyBytes;
	memcpy(newBuffer, oldBuffer, copyBytes);
	result->poppedHandle = handle;
	result->callResourceUnlock = true;
	result->calledFreeVertexBuffer = true;
	result->restoredGeneralState = true;
	result->returned = true;
	return 1;
}

int SlipDraw3D_ProjectIndex(SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCount, uint16_t vertexIndex,
                            SlipDraw3DTransformFn transform, void *userData, SlipDraw3DProjectIndex *result) {
	uint32_t vertexOffset;
	SlipDraw3DVertexRecord *record;
	uint32_t flags;
	SlipDraw3DVec32 world;
	uint32_t sourceX;
	uint32_t sourceY;
	uint32_t sourceZ;
	bool alreadyTransformed;

	if (vertexRecords == NULL || transform == NULL || result == NULL || (size_t)vertexIndex >= vertexRecordCount) {
		return 0;
	}
	vertexOffset = (uint32_t)vertexIndex * SLIP_DRAW3D_VERTEX_RECORD_SIZE;
	record = vertexRecords + vertexIndex;
	flags = record->flags;
	alreadyTransformed = (flags & SLIP_VERTEX_TRANSFORMED) != 0;
	sourceX = 0;
	sourceY = 0;
	sourceZ = 0;
	if (alreadyTransformed) {
		world = record->world;
	} else {
		record->flags = SLIP_VERTEX_TRANSFORMED;
		sourceX = (uint16_t)record->sourceX | ((uint32_t)(uint16_t)record->sourceY << 16);
		sourceY = (uint16_t)record->sourceY | ((uint32_t)(uint16_t)record->sourceZ << 16);
		sourceZ = (uint16_t)record->sourceZ | ((uint32_t)record->sourceFollowingWord << 16);
		world = transform(sourceX, sourceY, sourceZ, record, userData);
		record->world.x = world.x;
		record->world.y = world.y;
		record->world.z = world.z;
	}
	*result = (SlipDraw3DProjectIndex){vertexIndex,         vertexOffset, record,  flags,   alreadyTransformed,
	                                   !alreadyTransformed, sourceX,      sourceY, sourceZ, world};
	return 1;
}

int SlipDraw3D_PolygonStatus(SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCount,
                             const uint8_t *indexStream, size_t indexStreamBytes, uint16_t countAndFlags,
                             const SlipDraw3DProjectState *state, SlipDraw3DTransformFn transform,
                             SlipDraw3DProjectMaskFn projectMask, void *userData, SlipDraw3DPolygonStatusVisit *visits,
                             size_t visitCapacity, SlipDraw3DPolygonStatus *result) {
	uint32_t vertexCount;
	uint32_t allClipFlags;
	uint32_t anyClipFlags;
	uint32_t i;
	uint32_t allMask;
	uint32_t anyMask;

	if (vertexRecords == NULL || indexStream == NULL || state == NULL || transform == NULL || projectMask == NULL ||
	    visits == NULL || result == NULL) {
		return 0;
	}
	vertexCount = (uint32_t)countAndFlags & SLIP_PRIMITIVE_VERTEX_COUNT_MASK;
	if (vertexCount == 0 || visitCapacity < (size_t)vertexCount ||
	    indexStreamBytes < (size_t)vertexCount * SLIP_SERIALIZED_INDEX_BYTES) {
		return 0;
	}
	memset(visits, 0, sizeof(*visits) * (size_t)vertexCount);
	allClipFlags = SLIP_CLIP_ALL;
	anyClipFlags = 0;
	*result = (SlipDraw3DPolygonStatus){
	    countAndFlags, vertexCount, allClipFlags, anyClipFlags, 0, 0, 0, 0, false, 0, 0, 0, false};
	for (i = 0; i < vertexCount; ++i) {
		SlipDraw3DPolygonStatusVisit *visit;
		SlipDraw3DVertexRecord *record;
		uint16_t vertexIndex;
		uint32_t flags;
		uint32_t status;
		SlipDraw3DVec32 world;

		vertexIndex = SlipBytes_ReadLE16(indexStream + (size_t)i * SLIP_SERIALIZED_INDEX_BYTES);
		if ((size_t)vertexIndex >= vertexRecordCount) {
			return 0;
		}
		visit = visits + i;
		visit->vertexIndex = vertexIndex;
		if (!SlipDraw3D_ProjectIndex(vertexRecords, vertexRecordCount, vertexIndex, transform, userData,
		                             &visit->project)) {
			return 0;
		}
		record = visit->project.vertexRecord;
		flags = record->flags;
		visit->flagsBeforeStatus = flags;
		if ((flags & SLIP_VERTEX_DEPTH_CLASSIFIED) != 0) {
			visit->statusAlreadyComputed = true;
			status = flags;
		} else {
			world = visit->project.world;
			status = SLIP_VERTEX_TRANSFORMED_AND_DEPTH_CLASSIFIED;
			if (world.z < state->minZ) {
				status |= SLIP_CLIP_NEAR;
			}
			if (world.z > state->maxZ) {
				status |= SLIP_CLIP_FAR;
			}
			if ((state->renderFlags & SLIP_SHAPE_CLIP_AUXILIARY) != 0) {

				const int32_t deltaX = (int32_t)((uint32_t)world.x - (uint32_t)state->depthOrigin.x);
				const int32_t deltaY = (int32_t)((uint32_t)world.y - (uint32_t)state->depthOrigin.y);
				const int32_t deltaZ = (int32_t)((uint32_t)world.z - (uint32_t)state->depthOrigin.z);
				const uint64_t depth = (uint64_t)((int64_t)deltaX * state->depthNormal.x) +
				                       (uint64_t)((int64_t)deltaY * state->depthNormal.y) +
				                       (uint64_t)((int64_t)deltaZ * state->depthNormal.z);
				const int32_t shifted = SlipDraw3D_RoundedShiftRight14((int64_t)depth);

				record->depth = shifted;
				if (shifted < 0) {
					status |= SLIP_CLIP_AUXILIARY;
				}
			}
			flags |= status;
			record->flags = flags;
			status = flags;
		}
		allClipFlags &= status;
		anyClipFlags |= status;
		visit->depthClipFlags = status;
		visit->flagsAfterStatus = record->flags;
		result->firstPassVisitCount = (size_t)i + 1u;
	}
	result->allClipFlags = allClipFlags;
	result->anyClipFlags = anyClipFlags;
	if (allClipFlags != 0) {
		result->firstPassReject = true;
		result->clipClassification = -1;
		result->signFlagAfterReturn = true;
		return 1;
	}
	allMask = UINT32_MAX;
	anyMask = 0;
	for (i = vertexCount; i > 0; --i) {
		const uint32_t originalIndex = i - 1u;
		SlipDraw3DPolygonStatusVisit *const visit = visits + originalIndex;
		SlipDraw3DVertexRecord *const record = visit->project.vertexRecord;
		uint32_t flags = record->flags;
		uint32_t mask;
		SlipDraw3DVec32 world;

		if ((flags & SLIP_VERTEX_VIEW_MASK_CLASSIFIED) == 0) {
			flags |= SLIP_VERTEX_VIEW_MASK_CLASSIFIED;
			record->flags = flags;
			world = record->world;
			mask = projectMask(world, userData);
			record->clipMask = mask;
		} else {
			mask = record->clipMask;
		}
		allMask &= mask;
		anyMask |= mask;
		visit->reversePassVisited = true;
		visit->reversePassOrder = (uint32_t)result->reversePassVisitCount;
		visit->projectMask = mask;
		visit->allMaskAfter = allMask;
		visit->anyMaskAfter = anyMask;
		++result->reversePassVisitCount;
	}
	result->finalAllMask = allMask;
	result->finalAnyMask = anyMask;
	if (allMask != 0) {
		result->clipClassification = -1;
		result->signFlagAfterReturn = true;
	} else if (anyMask != 0) {
		result->clipClassification = 1;
	} else {
		result->clipClassification = 0;
	}
	return 1;
}

int SlipDraw3D_PerspectiveDepth(SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCount,
                                const uint8_t *indexStream, size_t indexStreamBytes, uint16_t countAndFlags,
                                uint32_t projectionFactor, SlipDraw3DTransformFn transform, void *userData,
                                SlipDraw3DPerspectiveDepthVisit *visits, size_t visitCapacity,
                                SlipDraw3DPerspectiveDepth *result) {
	uint32_t vertexCount;
	uint32_t minDepth;
	size_t i;

	if (vertexRecords == NULL || indexStream == NULL || transform == NULL || visits == NULL || result == NULL) {
		return 0;
	}
	vertexCount = (uint32_t)countAndFlags & SLIP_PRIMITIVE_VERTEX_COUNT_MASK;
	if (vertexCount == 0 || visitCapacity < (size_t)vertexCount ||
	    indexStreamBytes < (size_t)vertexCount * SLIP_SERIALIZED_INDEX_BYTES) {
		return 0;
	}
	memset(visits, 0, sizeof(*visits) * (size_t)vertexCount);
	minDepth = INT32_MAX;
	*result = (SlipDraw3DPerspectiveDepth){countAndFlags, (uint16_t)vertexCount, minDepth, 0,
	                                       minDepth,      projectionFactor,      0,        false};
	for (i = 0; i < (size_t)vertexCount; ++i) {
		SlipDraw3DPerspectiveDepthVisit *const visit = visits + i;
		SlipDraw3DVertexRecord *record;
		uint16_t vertexIndex;
		uint32_t flags;
		SlipDraw3DVec32 world;

		vertexIndex = SlipBytes_ReadLE16(indexStream + i * SLIP_SERIALIZED_INDEX_BYTES);
		if ((size_t)vertexIndex >= vertexRecordCount) {
			return 0;
		}
		record = vertexRecords + vertexIndex;
		flags = record->flags;
		visit->vertexIndex = vertexIndex;
		visit->vertexOffset = (uint32_t)vertexIndex * SLIP_DRAW3D_VERTEX_RECORD_SIZE;
		visit->alreadyTransformed = (flags & SLIP_VERTEX_TRANSFORMED) != 0;
		visit->minDepthBefore = minDepth;
		if (!visit->alreadyTransformed) {

			const uint32_t sourceX = (uint16_t)record->sourceX | ((uint32_t)(uint16_t)record->sourceY << 16);
			const uint32_t sourceY = (uint16_t)record->sourceY | ((uint32_t)(uint16_t)record->sourceZ << 16);
			const uint32_t sourceZ = (uint16_t)record->sourceZ | ((uint32_t)record->sourceFollowingWord << 16);

			record->flags = SLIP_VERTEX_TRANSFORMED;
			world = transform(sourceX, sourceY, sourceZ, record, userData);
			record->world = world;
			visit->calledTransform = true;
			visit->sourceX = sourceX;
			visit->sourceY = sourceY;
			visit->sourceZ = sourceZ;
		} else {
			world = record->world;
		}
		visit->world = world;
		if (world.z < 0) {
			visit->negativeDepthExit = true;
			result->visitCount = i + 1u;
			result->negativeDepthExit = true;
			result->fadeDepth = (uint32_t)world.z;
			return 1;
		}
		if ((uint32_t)world.z <= minDepth) {
			minDepth = (uint32_t)world.z;
		}
		visit->minDepthAfter = minDepth;
		result->visitCount = i + 1u;
	}
	result->minDepth = minDepth;
	result->fadeDepth = (uint32_t)((uint64_t)((int64_t)(int32_t)projectionFactor * (int32_t)minDepth) >>
	                               SLIP_DRAW3D_SCALE_FRACTION_BITS);
	return 1;
}

uint32_t SlipDraw3D_ProjectVertex(SlipDraw3DVertexRecord *record, const SlipDraw3DProjectState *state,
                                  SlipDraw3DTransformFn transform, SlipDraw3DProjectFn projectPrimary,
                                  SlipDraw3DProjectFn projectSecondary, void *userData) {
	uint32_t flags;
	uint32_t screenFlags;
	SlipDraw3DVec32 world;
	int32_t screenX;
	int32_t screenY;

	flags = record->flags;
	if ((flags & SLIP_VERTEX_PROJECTED) != 0) {
		return flags;
	}

	if ((flags & SLIP_VERTEX_TRANSFORMED) == 0) {
		world =
		    transform(((uint16_t)record->sourceX | ((uint32_t)(uint16_t)record->sourceY << 16)),
		              ((uint16_t)record->sourceY | ((uint32_t)(uint16_t)record->sourceZ << 16)),
		              ((uint16_t)record->sourceZ | ((uint32_t)record->sourceFollowingWord << 16)), record, userData);
		record->world.x = world.x;
		record->world.y = world.y;
		record->world.z = world.z;
		flags = record->flags;
		if ((flags & SLIP_VERTEX_PROJECTED) != 0) {
			return flags;
		}
	} else {
		world = record->world;
	}

	if ((flags & SLIP_VERTEX_DEPTH_CLASSIFIED) == 0) {
		uint32_t newFlags = (SLIP_VERTEX_TRANSFORMED | SLIP_VERTEX_DEPTH_CLASSIFIED);

		if (world.z < state->minZ) {
			newFlags |= SLIP_CLIP_NEAR;
		}
		if (world.z > state->maxZ) {
			newFlags |= SLIP_CLIP_FAR;
		}
		if ((state->renderFlags & SLIP_SHAPE_CLIP_AUXILIARY) != 0) {

			const int32_t deltaX = (int32_t)((uint32_t)world.x - (uint32_t)state->depthOrigin.x);
			const int32_t deltaY = (int32_t)((uint32_t)world.y - (uint32_t)state->depthOrigin.y);
			const int32_t deltaZ = (int32_t)((uint32_t)world.z - (uint32_t)state->depthOrigin.z);
			const uint64_t depth = (uint64_t)((int64_t)deltaX * state->depthNormal.x) +
			                       (uint64_t)((int64_t)deltaY * state->depthNormal.y) +
			                       (uint64_t)((int64_t)deltaZ * state->depthNormal.z);
			const int32_t shifted = SlipDraw3D_RoundedShiftRight14((int64_t)depth);

			record->depth = shifted;
			if (shifted < 0) {
				newFlags |= SLIP_CLIP_AUXILIARY;
			}
		}
		flags |= newFlags;
		record->flags = flags;
	}

	if ((flags & SLIP_CLIP_BEFORE_PROJECTION) != 0) {
		return flags;
	}

	if ((state->renderFlags & SLIP_SHAPE_SECONDARY_PROJECTION) != 0u) {
		projectSecondary(world, &screenX, &screenY, userData);
	} else {
		projectPrimary(world, &screenX, &screenY, userData);
	}
	record->screenX = screenX;
	record->screenY = screenY;

	screenFlags = SLIP_VERTEX_PROJECTED;
	if (screenX < state->minX) {
		screenFlags |= SLIP_CLIP_LEFT;
	}
	if (screenX > state->maxX) {
		screenFlags |= SLIP_CLIP_RIGHT;
	}
	if (screenY < state->minY) {
		screenFlags |= SLIP_CLIP_TOP;
	}
	if (screenY > state->maxY) {
		screenFlags |= SLIP_CLIP_BOTTOM;
	}
	if ((screenFlags & SLIP_CLIP_SCREEN) != 0 && screenX < SLIP_SCREEN_CLIP_COORDINATE_LIMIT &&
	    screenY < SLIP_SCREEN_CLIP_COORDINATE_LIMIT && screenX > -SLIP_SCREEN_CLIP_COORDINATE_LIMIT &&
	    screenY > -SLIP_SCREEN_CLIP_COORDINATE_LIMIT) {
		screenFlags |= SLIP_VERTEX_SCREEN_CLIP_IN_RANGE;
	}
	record->flags |= screenFlags;
	return record->flags;
}

int SlipDraw3D_BuildSolidDrawRecords(SlipDraw3DDrawRecord *drawRecords, size_t drawRecordCapacity,
                                     SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCount,
                                     const uint8_t *indices, size_t indexBytes, uint16_t vertexCount,
                                     uint32_t materialDitherBits, const SlipDraw3DProjectState *state,
                                     SlipDraw3DTransformFn transform, SlipDraw3DProjectFn project, void *userData,
                                     SlipDraw3DBuildResult *result) {
	SlipDraw3DRecordPool *const recordPool = &g_drawRecordPool;
	SlipDraw3DFirstActiveRecord firstActive;
	SlipDraw3DSolidLoopInit loopInit;
	uint8_t *recordPoolBytes;
	size_t recordPoolByteSize;
	uint32_t currentRecordOffset;
	uint32_t firstRecordOffset;
	uint32_t indexStreamOffset;
	uint32_t loopCount;
	uint32_t allClipFlags;
	uint32_t anyClipFlags;
	size_t outputIndex;

	if (drawRecords == NULL || vertexRecords == NULL || indices == NULL || state == NULL || transform == NULL ||
	    project == NULL || result == NULL || vertexCount == 0 || drawRecordCapacity < vertexCount ||
	    indexBytes < (size_t)vertexCount * SLIP_SERIALIZED_INDEX_BYTES ||
	    vertexCount > SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT) {
		return 0;
	}

	if (!SlipDraw3D_EnsureRecordPool() || (recordPoolBytes = SlipDraw3D_RecordPoolBytes(recordPool)) == NULL ||
	    (recordPoolByteSize = SlipDraw3D_RecordPoolByteSize()) == 0 ||
	    !SlipDraw3D_AllocateFirstActiveRecord(recordPoolBytes, recordPoolByteSize, recordPool->freeHeadOffset,
	                                          &firstActive)) {
		return 0;
	}
	recordPool->inputActiveHeadOffset = firstActive.inputActiveHeadOffset;
	firstRecordOffset = recordPool->inputActiveHeadOffset;
	currentRecordOffset = firstRecordOffset;
	if (!SlipDraw3D_InitSolidLoop(firstRecordOffset, &loopInit)) {
		return 0;
	}
	allClipFlags = loopInit.allClipFlagsInitial;
	anyClipFlags = loopInit.anyClipFlagsInitial;
	indexStreamOffset = 0;
	loopCount = vertexCount;
	outputIndex = 0;

	for (;;) {
		SlipDraw3DSolidRecordCopy copy;
		SlipDraw3DDrawRecord *currentRecord;
		const uint16_t index = SlipBytes_ReadLE16(indices + indexStreamOffset);
		uint32_t flags;

		if (index >= vertexRecordCount) {
			return 0;
		}
		flags = SlipDraw3D_ProjectVertex(&vertexRecords[index], state, transform, project, project, userData);
		currentRecord = SlipDraw3D_RecordPoolDrawRecord(recordPool, currentRecordOffset);
		if (currentRecord == NULL) {
			return 0;
		}
		if (!SlipDraw3D_CopySolidRecord(currentRecord, (const uint8_t *)vertexRecords,
		                                vertexRecordCount * sizeof(*vertexRecords), indices, indexBytes,
		                                indexStreamOffset, loopCount, allClipFlags, anyClipFlags, flags, &copy)) {
			return 0;
		}
		allClipFlags = copy.allClipFlagsAfter;
		anyClipFlags = copy.anyClipFlagsAfter;
		memcpy(drawRecords[outputIndex].bytes, currentRecord->bytes, SLIP_DRAW3D_DRAW_RECORD_SIZE);
		++outputIndex;
		if (copy.branchToClose) {
			break;
		}
		{
			SlipDraw3DAppendSolidRecord append;

			if (!SlipDraw3D_AppendSolidRecord(recordPoolBytes, recordPoolByteSize, recordPool->freeHeadOffset,
			                                  currentRecordOffset, indexStreamOffset, &append)) {
				return 0;
			}
			currentRecordOffset = append.appendedRecordOffset;
			indexStreamOffset = append.indexStreamOffsetAfter;
			loopCount = copy.loopCountAfterDec;
		}
	}
	{
		SlipDraw3DCloseRecordRing close;

		if (!SlipDraw3D_CloseRecordRing(recordPoolBytes, recordPoolByteSize, currentRecordOffset, firstRecordOffset,
		                                &close)) {
			return 0;
		}
	}

	result->allClipFlags = allClipFlags;
	result->anyClipFlags = anyClipFlags;
	result->drawMode = materialDitherBits != 0u ? SLIP_POLYGON_DRAW_DITHERED : SLIP_POLYGON_DRAW_FLAT;
	result->materialDitherBits = materialDitherBits;
	result->ditherBits = (uint8_t)materialDitherBits;
	return 1;
}

int SlipDraw3D_BuildSolidRingExecute(
    SlipDraw3DRecordPool *pool, SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCount, const uint8_t *indices,
    size_t indexBytes, uint16_t vertexCount, uint32_t materialDitherBits, const SlipDraw3DProjectState *state,
    SlipDraw3DTransformFn transform, SlipDraw3DProjectFn projectPrimary, SlipDraw3DProjectFn projectSecondary,
    void *userData, int hasPostPlanes, const uint8_t *planeBase, size_t planeBytes, uint32_t planeHeadOffset,
    int32_t postLimitXMin, int32_t postLimitXMax, int32_t postLimitYMin, int32_t postLimitYMax,
    size_t maxClipEdgeVisits, SlipDraw3DClipFlagVisit *clipFlagVisits, size_t clipFlagVisitCapacity,
    SlipDraw3DPostPlaneBoundsVisit *postBoundsVisits, size_t postBoundsVisitCapacity,
    SlipDraw3DPostPlaneClipRecordVisit *postClipRecordVisits, size_t postClipRecordVisitCapacity,
    SlipDraw3DPostPlaneClipPlaneVisit *postClipPlaneVisits, size_t postClipPlaneVisitCapacity,
    SlipDraw3DSolidRingExecute *result) {
	uint8_t *recordPoolBytes;
	size_t recordPoolByteSize;
	SlipDraw3DFirstActiveRecord firstActive;
	uint32_t currentRecordOffset;
	uint32_t firstRecordOffset;
	uint32_t indexStreamOffset;
	uint32_t loopCount;
	uint32_t allClipFlags;
	uint32_t anyClipFlags;

	if (pool == NULL || vertexRecords == NULL || indices == NULL || state == NULL || transform == NULL ||
	    projectPrimary == NULL || projectSecondary == NULL || result == NULL || vertexCount == 0 ||
	    indexBytes < (size_t)vertexCount * SLIP_SERIALIZED_INDEX_BYTES ||
	    vertexCount > SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	recordPoolBytes = SlipDraw3D_RecordPoolBytes(pool);
	recordPoolByteSize = SlipDraw3D_RecordPoolByteSize();
	if (recordPoolBytes == NULL || !SlipDraw3D_AllocateFirstActiveRecord(recordPoolBytes, recordPoolByteSize,
	                                                                     pool->freeHeadOffset, &firstActive)) {
		return 0;
	}
	pool->inputActiveHeadOffset = firstActive.inputActiveHeadOffset;
	firstRecordOffset = pool->inputActiveHeadOffset;
	currentRecordOffset = firstRecordOffset;
	allClipFlags = SLIP_CLIP_ALL;
	anyClipFlags = 0;
	indexStreamOffset = 0;
	loopCount = vertexCount;

	for (;;) {
		SlipDraw3DSolidRecordCopy copy;
		SlipDraw3DDrawRecord *currentRecord;
		const uint16_t index = SlipBytes_ReadLE16(indices + indexStreamOffset);
		uint32_t flags;

		if (index >= vertexRecordCount) {
			return 0;
		}
		flags = SlipDraw3D_ProjectVertex(&vertexRecords[index], state, transform, projectPrimary, projectSecondary,
		                                 userData);
		currentRecord = SlipDraw3D_RecordPoolDrawRecord(pool, currentRecordOffset);
		if (currentRecord == NULL ||
		    !SlipDraw3D_CopySolidRecord(currentRecord, (const uint8_t *)vertexRecords,
		                                vertexRecordCount * sizeof(*vertexRecords), indices, indexBytes,
		                                indexStreamOffset, loopCount, allClipFlags, anyClipFlags, flags, &copy)) {
			return 0;
		}
		allClipFlags = copy.allClipFlagsAfter;
		anyClipFlags = copy.anyClipFlagsAfter;
		if (copy.branchToClose) {
			break;
		}
		{
			SlipDraw3DAppendSolidRecord append;

			if (!SlipDraw3D_AppendSolidRecord(recordPoolBytes, recordPoolByteSize, pool->freeHeadOffset,
			                                  currentRecordOffset, indexStreamOffset, &append)) {
				return 0;
			}
			currentRecordOffset = append.appendedRecordOffset;
			indexStreamOffset = append.indexStreamOffsetAfter;
			loopCount = copy.loopCountAfterDec;
		}
	}
	{
		SlipDraw3DCloseRecordRing close;

		if (!SlipDraw3D_CloseRecordRing(recordPoolBytes, recordPoolByteSize, currentRecordOffset, firstRecordOffset,
		                                &close)) {
			return 0;
		}
	}
	result->build = (SlipDraw3DBuildResult){
	    allClipFlags, anyClipFlags, materialDitherBits != 0u ? SLIP_POLYGON_DRAW_DITHERED : SLIP_POLYGON_DRAW_FLAT,
	    materialDitherBits, (uint8_t)materialDitherBits};
	result->returned = true;
	if ((anyClipFlags & SLIP_DRAW3D_CLIP_MASK) != 0u && (allClipFlags & SLIP_DRAW3D_CLIP_MASK) != 0u) {
		result->carryOut = true;
		result->activeHeadOffsetOut = pool->inputActiveHeadOffset;
		return 1;
	}
	if ((anyClipFlags & SLIP_DRAW3D_CLIP_MASK) != 0u) {
		if (!SlipDraw3D_ClipDispatchExecute(
		        recordPoolBytes, recordPoolByteSize, pool->inputActiveHeadOffset, pool->freeHeadOffset, anyClipFlags,
		        allClipFlags, state->renderFlags,
		        materialDitherBits != 0u ? SLIP_POLYGON_DRAW_DITHERED : SLIP_POLYGON_DRAW_FLAT, state->minZ,
		        state->maxZ, state->minX, state->maxX, state->minY, state->maxY, projectPrimary, projectSecondary,
		        userData, hasPostPlanes, planeBase, planeBytes, planeHeadOffset, postLimitXMin, postLimitXMax,
		        postLimitYMin, postLimitYMax, maxClipEdgeVisits, clipFlagVisits, clipFlagVisitCapacity,
		        postBoundsVisits, postBoundsVisitCapacity, postClipRecordVisits, postClipRecordVisitCapacity,
		        postClipPlaneVisits, postClipPlaneVisitCapacity, &result->dispatch)) {
			return 0;
		}
		pool->inputActiveHeadOffset = result->dispatch.activeHeadOffsetOut;
		result->carryOut = result->dispatch.dispatch.carryOut;
	}
	result->activeHeadOffsetOut = pool->inputActiveHeadOffset;
	return 1;
}

static int SlipDraw3D_SelectLineClipPair(uint8_t *recordBase, size_t recordBytes, uint32_t inputActiveHeadOffset,
                                         uint32_t inputClipMask, SlipDraw3DLineClipSelect *result) {
	uint32_t clipMask;
	uint32_t pairedOffset;
	uint32_t activeFlags;

	if (recordBase == NULL || result == NULL ||
	    !SlipDraw3D_LinkedRecordOffsetValid(recordBytes, inputActiveHeadOffset)) {
		return 0;
	}
	clipMask = (uint16_t)inputClipMask;
	pairedOffset = SlipDraw3D_RecordNext(recordBase, inputActiveHeadOffset);
	if (!SlipDraw3D_LinkedRecordOffsetValid(recordBytes, pairedOffset)) {
		return 0;
	}
	activeFlags = SlipBytes_ReadLE32(recordBase + inputActiveHeadOffset + SLIP_DRAW3D_VERTEX_RECORD_FLAGS_OFFSET);
	*result =
	    (SlipDraw3DLineClipSelect){.clipMask = clipMask,
	                               .inputActiveHeadOffset = inputActiveHeadOffset,
	                               .pairedOffset = pairedOffset,
	                               .activeHeadHasMask = (activeFlags & clipMask) != 0,
	                               .targetOffset = (activeFlags & clipMask) != 0 ? inputActiveHeadOffset : pairedOffset,
	                               .otherOffset = (activeFlags & clipMask) != 0 ? pairedOffset : inputActiveHeadOffset,
	                               .returned = true};
	return 1;
}

int SlipDraw3D_LineClipExecute(uint8_t *recordBase, size_t recordBytes, uint32_t inputActiveHeadOffset,
                               uint32_t inputAnyClipFlags, uint32_t renderFlags, uint32_t projectionMode,
                               int32_t limitZMin, int32_t limitZMax, int32_t limitXMin, int32_t limitXMax,
                               int32_t limitYMin, int32_t limitYMax, SlipDraw3DProjectFn projectPrimary,
                               SlipDraw3DProjectFn projectSecondary, void *userData,
                               SlipDraw3DLineClipExecute *result) {
	SlipDraw3DClipFlagVisit clipFlagVisits[4];
	uint32_t anyClipFlags;
	uint32_t savedAnyClipFlags;

	if (recordBase == NULL || result == NULL || projectPrimary == NULL || projectSecondary == NULL ||
	    !SlipDraw3D_LinkedRecordOffsetValid(recordBytes, inputActiveHeadOffset)) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	memset(clipFlagVisits, 0, sizeof(clipFlagVisits));
	anyClipFlags = inputAnyClipFlags;
	result->inputActiveHeadOffset = inputActiveHeadOffset;
	result->anyFlagsEntry = inputAnyClipFlags;
	result->renderFlags = renderFlags;
	result->returned = true;

	result->testRenderFlagsBit = (renderFlags & SLIP_SHAPE_CLIP_AUXILIARY) != 0;
	if (result->testRenderFlagsBit) {
		result->testDepthPlane = (anyClipFlags & SLIP_CLIP_AUXILIARY) != 0;
		if (result->testDepthPlane) {
			if (!SlipDraw3D_SelectLineClipPair(recordBase, recordBytes, inputActiveHeadOffset, SLIP_CLIP_AUXILIARY,
			                                   &result->selectDepthPlane) ||
			    !SlipDraw3D_SplitDepthMathRecordWithCallback(
			        recordBase, recordBytes, result->selectDepthPlane.targetOffset,
			        result->selectDepthPlane.otherOffset, renderFlags, projectionMode, projectPrimary, projectSecondary,
			        userData, limitZMin, limitZMax, limitXMin, limitXMax, limitYMin, limitYMax,
			        &result->splitDepthPlane) ||
			    !SlipDraw3D_CollectClipFlags(recordBase, recordBytes, inputActiveHeadOffset, clipFlagVisits,
			                                 sizeof(clipFlagVisits) / sizeof(clipFlagVisits[0]),
			                                 &result->clipFlagsAfterDepthPlane)) {
				return 0;
			}
			result->allFlagsAfterDepthPlane = result->clipFlagsAfterDepthPlane.allFlagsOut;
			anyClipFlags = result->clipFlagsAfterDepthPlane.anyFlagsOut;
			result->anyFlagsAfterDepthPlane = anyClipFlags;
			if ((result->allFlagsAfterDepthPlane & SLIP_CLIP_ALL) != 0) {
				result->setRejectCarry = true;
				result->carryOut = true;
				result->branch = SLIP_DRAW3D_LINE_CLIP_BRANCH_REJECT_DEPTH_PLANE;
				return 1;
			}
		}
	}

	result->testNear = (anyClipFlags & SLIP_CLIP_NEAR) != 0;
	if (result->testNear) {
		if (!SlipDraw3D_SelectLineClipPair(recordBase, recordBytes, inputActiveHeadOffset, SLIP_CLIP_NEAR,
		                                   &result->selectNear) ||
		    !SlipDraw3D_SplitDepthRecordWithCallback(recordBase, recordBytes, result->selectNear.targetOffset,
		                                             result->selectNear.otherOffset, renderFlags, projectionMode,
		                                             limitZMin, projectPrimary, projectSecondary, userData, limitXMin,
		                                             limitXMax, limitYMin, limitYMax, &result->splitNear)) {
			return 0;
		}
	}
	result->testFar = (anyClipFlags & SLIP_CLIP_FAR) != 0;
	if (result->testFar) {
		if (!SlipDraw3D_SelectLineClipPair(recordBase, recordBytes, inputActiveHeadOffset, SLIP_CLIP_FAR,
		                                   &result->selectFar) ||
		    !SlipDraw3D_SplitDepthRecordWithCallback(recordBase, recordBytes, result->selectFar.targetOffset,
		                                             result->selectFar.otherOffset, renderFlags, projectionMode,
		                                             limitZMax, projectPrimary, projectSecondary, userData, limitXMin,
		                                             limitXMax, limitYMin, limitYMax, &result->splitFar)) {
			return 0;
		}
	}
	if (!SlipDraw3D_CollectClipFlags(recordBase, recordBytes, inputActiveHeadOffset, clipFlagVisits,
	                                 sizeof(clipFlagVisits) / sizeof(clipFlagVisits[0]),
	                                 &result->clipFlagsAfterDepthRange)) {
		return 0;
	}
	result->allFlagsAfterDepthRange = result->clipFlagsAfterDepthRange.allFlagsOut;
	anyClipFlags = result->clipFlagsAfterDepthRange.anyFlagsOut;
	result->anyFlagsAfterDepthRange = anyClipFlags;
	if ((result->allFlagsAfterDepthRange & SLIP_CLIP_SCREEN) != 0) {
		result->setRejectCarry = true;
		result->carryOut = true;
		result->branch = SLIP_DRAW3D_LINE_CLIP_BRANCH_REJECT_SCREEN_X;
		return 1;
	}

	savedAnyClipFlags = anyClipFlags;
	result->testLeft = (anyClipFlags & SLIP_CLIP_LEFT) != 0;
	if (result->testLeft) {
		if (!SlipDraw3D_SelectLineClipPair(recordBase, recordBytes, inputActiveHeadOffset, SLIP_CLIP_LEFT,
		                                   &result->selectLeft) ||
		    !SlipDraw3D_SplitScreenXRecord(recordBase, recordBytes, result->selectLeft.targetOffset,
		                                   result->selectLeft.otherOffset, projectionMode, limitXMin, limitYMin,
		                                   limitYMax, &result->splitLeft)) {
			return 0;
		}
	}
	result->testRight = (savedAnyClipFlags & SLIP_CLIP_RIGHT) != 0;
	if (result->testRight) {
		if (!SlipDraw3D_SelectLineClipPair(recordBase, recordBytes, inputActiveHeadOffset, SLIP_CLIP_RIGHT,
		                                   &result->selectRight) ||
		    !SlipDraw3D_SplitScreenXRecord(recordBase, recordBytes, result->selectRight.targetOffset,
		                                   result->selectRight.otherOffset, projectionMode, limitXMax, limitYMin,
		                                   limitYMax, &result->splitRight)) {
			return 0;
		}
	}
	if (!SlipDraw3D_CollectClipFlags(recordBase, recordBytes, inputActiveHeadOffset, clipFlagVisits,
	                                 sizeof(clipFlagVisits) / sizeof(clipFlagVisits[0]),
	                                 &result->clipFlagsAfterScreenX)) {
		return 0;
	}
	result->allFlagsAfterScreenX = result->clipFlagsAfterScreenX.allFlagsOut;
	anyClipFlags = result->clipFlagsAfterScreenX.anyFlagsOut;
	result->anyFlagsAfterScreenX = anyClipFlags;
	if ((result->allFlagsAfterScreenX & SLIP_CLIP_VERTICAL) != 0) {
		result->setRejectCarry = true;
		result->carryOut = true;
		result->branch = SLIP_DRAW3D_LINE_CLIP_BRANCH_REJECT_SCREEN_Y;
		return 1;
	}

	savedAnyClipFlags = anyClipFlags;
	result->testTop = (anyClipFlags & SLIP_CLIP_TOP) != 0;
	if (result->testTop) {
		if (!SlipDraw3D_SelectLineClipPair(recordBase, recordBytes, inputActiveHeadOffset, SLIP_CLIP_TOP,
		                                   &result->selectTop) ||
		    !SlipDraw3D_SplitScreenYRecord(recordBase, recordBytes, result->selectTop.targetOffset,
		                                   result->selectTop.otherOffset, projectionMode, limitYMin,
		                                   &result->splitTop)) {
			return 0;
		}
	}
	result->testBottom = (savedAnyClipFlags & SLIP_CLIP_BOTTOM) != 0;
	if (result->testBottom) {
		if (!SlipDraw3D_SelectLineClipPair(recordBase, recordBytes, inputActiveHeadOffset, SLIP_CLIP_BOTTOM,
		                                   &result->selectBottom) ||
		    !SlipDraw3D_SplitScreenYRecord(recordBase, recordBytes, result->selectBottom.targetOffset,
		                                   result->selectBottom.otherOffset, projectionMode, limitYMax,
		                                   &result->splitBottom)) {
			return 0;
		}
	}
	result->clearedRejectCarry = true;
	result->carryOut = false;
	result->branch = SLIP_DRAW3D_LINE_CLIP_BRANCH_ACCEPT;
	return 1;
}

int SlipDraw3D_BuildLinePairExecute(SlipDraw3DRecordPool *pool, SlipDraw3DVertexRecord *vertexRecords,
                                    size_t vertexRecordCount, const uint8_t *indices, size_t indexBytes,
                                    uint32_t materialColor, const SlipDraw3DProjectState *state,
                                    SlipDraw3DTransformFn transform, SlipDraw3DProjectFn projectPrimary,
                                    SlipDraw3DProjectFn projectSecondary, void *userData,
                                    SlipDraw3DLinePairExecute *result) {
	uint8_t *recordPoolBytes;
	size_t recordPoolByteSize;
	SlipDraw3DFreeRecordPop firstPop;
	SlipDraw3DFreeRecordPop secondPop;
	uint32_t allClipFlags = SLIP_CLIP_ALL;
	uint32_t anyClipFlags = 0;
	uint32_t firstRecordOffset;
	uint32_t secondRecordOffset;
	size_t copyIndex;

	if (pool == NULL || vertexRecords == NULL || indices == NULL || state == NULL || transform == NULL ||
	    projectPrimary == NULL || projectSecondary == NULL || result == NULL || vertexRecordCount == 0 ||
	    indexBytes < 2 * SLIP_SERIALIZED_INDEX_BYTES) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	recordPoolBytes = SlipDraw3D_RecordPoolBytes(pool);
	recordPoolByteSize = SlipDraw3D_RecordPoolByteSize();
	if (recordPoolBytes == NULL ||
	    !SlipDraw3D_PopFreeRecord(recordPoolBytes, recordPoolByteSize, pool->freeHeadOffset, &firstPop)) {
		return 0;
	}
	firstRecordOffset = firstPop.poppedRecordOffset;
	pool->inputActiveHeadOffset = firstRecordOffset;

	if (!SlipDraw3D_PopFreeRecord(recordPoolBytes, recordPoolByteSize, pool->freeHeadOffset, &secondPop)) {
		return 0;
	}
	secondRecordOffset = secondPop.poppedRecordOffset;
	SlipDraw3D_SetRecordNext(recordPoolBytes, firstRecordOffset, secondRecordOffset);
	SlipDraw3D_SetRecordPrev(recordPoolBytes, secondRecordOffset, firstRecordOffset);

	for (copyIndex = 0; copyIndex < 2u; ++copyIndex) {
		const uint32_t recordOffset = copyIndex == 0 ? firstRecordOffset : secondRecordOffset;
		SlipDraw3DDrawRecord *const drawRecord = SlipDraw3D_RecordPoolDrawRecord(pool, recordOffset);
		const uint16_t vertexIndex = SlipBytes_ReadLE16(indices + copyIndex * SLIP_SERIALIZED_INDEX_BYTES);
		uint32_t flags;

		if (drawRecord == NULL || (size_t)vertexIndex >= vertexRecordCount) {
			return 0;
		}
		flags = SlipDraw3D_ProjectVertex(&vertexRecords[vertexIndex], state, transform, projectPrimary,
		                                 projectSecondary, userData);
		memcpy(drawRecord->bytes, vertexRecords[vertexIndex].bytes, SLIP_DRAW3D_VERTEX_DRAW_PREFIX_BYTES);
		allClipFlags &= flags;
		anyClipFlags |= flags;
	}
	SlipDraw3D_SetRecordNext(recordPoolBytes, secondRecordOffset, firstRecordOffset);
	SlipDraw3D_SetRecordPrev(recordPoolBytes, firstRecordOffset, secondRecordOffset);

	result->build = (SlipDraw3DBuildResult){allClipFlags, anyClipFlags, 2u, materialColor, (uint8_t)materialColor};
	result->activeHeadOffsetOut = pool->inputActiveHeadOffset;
	result->returned = true;
	if ((anyClipFlags & SLIP_DRAW3D_CLIP_MASK) == 0u) {
		result->clearedRejectCarry = true;
		return 1;
	}
	if ((allClipFlags & SLIP_DRAW3D_CLIP_MASK) != 0u) {
		result->setRejectCarry = true;
		result->carryOut = true;
		return 1;
	}
	result->callRendererClipLine = true;
	if (!SlipDraw3D_LineClipExecute(recordPoolBytes, recordPoolByteSize, pool->inputActiveHeadOffset, anyClipFlags,
	                                state->renderFlags, 0u, state->minZ, state->maxZ, state->minX, state->maxX,
	                                state->minY, state->maxY, projectPrimary, projectSecondary, userData,
	                                &result->lineClip)) {
		return 0;
	}
	if (result->lineClip.carryOut) {
		result->setRejectCarry = true;
		result->carryOut = true;
		return 1;
	}
	result->clearedRejectCarry = true;
	result->carryOut = false;
	return 1;
}

SlipDraw3DClipStep SlipDraw3D_ClassifyClip(uint32_t allClipFlags, uint32_t anyClipFlags) {
	if ((anyClipFlags & SLIP_DRAW3D_CLIP_MASK) == 0) {
		return SLIP_DRAW3D_CLIP_SCREEN;
	}
	if ((allClipFlags & SLIP_DRAW3D_CLIP_MASK) != 0) {
		return SLIP_DRAW3D_CLIP_REJECT;
	}
	return SLIP_DRAW3D_CLIP_DEPTH_THEN_SCREEN;
}

int SlipDraw3D_SetLimitState(uint32_t limitStart, uint32_t limitEnd, SlipDraw3DLimitState *state) {
	if (state == NULL) {
		return 0;
	}
	state->limitStart = limitStart;
	state->limitEnd = limitEnd;
	state->limitEnabled = UINT32_MAX;
	return 1;
}

int SlipDraw3D_ClearLimitState(SlipDraw3DLimitState *state) {
	if (state == NULL) {
		return 0;
	}
	state->limitEnabled = 0;
	return 1;
}

SlipDraw3DListState SlipDraw3D_listState;
SlipDraw3DListNode *SlipDraw3D_listPool;
uint8_t SlipDraw3D_listInitialized;

void SlipDraw3D_FreeList(void) {
	if (SlipDraw3D_listInitialized != 0) {
		SlipDraw3D_listInitialized = 0;
	}
}

bool SlipDraw3D_InitList(SlipDraw3DListNode *pool, uint16_t count) {
	SlipDraw3D_FreeList();
	++SlipDraw3D_listInitialized;
	SlipDraw3D_listState.capacity = count;
	SlipDraw3D_listState.remaining = count;
	if (pool == NULL) {
		return false;
	}
	SlipDraw3D_listPool = pool;
	SlipDraw3D_listState.baseOffset = 0;
	SlipDraw3D_listState.frameRootOffsets[0] = 0;
	SlipDraw3D_listState.currentOffset = 0;
	SlipRuntime_RegisterExit(SlipDraw3D_FreeList);
	SlipDraw3D_listState.frameDepth = UINT32_MAX;
	return true;
}

int SlipDraw3D_ListPushFrame(SlipDraw3DListState *state, SlipDraw3DListNode *nodePool, size_t nodePoolBytes) {
	uint32_t depth;
	uint32_t rootOffset;
	uint32_t usedNodes;
	SlipDraw3DListNode *root;

	(void)nodePoolBytes;
	depth = state->frameDepth + 1u;
	state->frameDepth = depth;
	if (depth != 0) {
		state->frameRootOffsets[depth] = state->currentOffset;
	}
	rootOffset = state->frameRootOffsets[depth];
	root = &nodePool[rootOffset / SLIP_DRAW3D_LIST_NODE_SIZE];
	usedNodes = rootOffset == state->baseOffset ? 0 : (rootOffset - state->baseOffset) / SLIP_DRAW3D_LIST_NODE_SIZE;
	state->currentOffset = rootOffset;
	state->remaining = state->capacity - usedNodes;
	root->callback = NULL;
	root->leftOffset = 0;
	root->rightOffset = 0;
	return 1;
}

int SlipDraw3D_ListPopFrame(SlipDraw3DListState *state) {
	uint32_t depth;
	uint32_t rootOffset;
	uint32_t usedNodes;

	depth = state->frameDepth;
	state->frameDepth = depth - 1u;
	rootOffset = state->frameRootOffsets[depth];
	usedNodes = rootOffset == state->baseOffset ? 0 : (rootOffset - state->baseOffset) / SLIP_DRAW3D_LIST_NODE_SIZE;
	state->currentOffset = rootOffset;
	state->remaining = state->capacity - usedNodes;
	return 1;
}

int SlipDraw3D_ListInsert(SlipDraw3DListState *state, SlipDraw3DListNode *nodePool, size_t nodePoolBytes,
                          uint32_t sortKey, SlipDraw3DListCallback callback, uint32_t payload) {
	uint32_t nodeOffset;
	uint32_t scanOffset;
	SlipDraw3DListNode *node;

	(void)nodePoolBytes;
	if (state->remaining == 0) {
		return 0;
	}
	nodeOffset = state->currentOffset;
	node = &nodePool[nodeOffset / SLIP_DRAW3D_LIST_NODE_SIZE];
	node->sortKey = sortKey;
	node->payload = payload;
	node->callback = callback;
	node->leftOffset = 0;
	node->rightOffset = 0;
	state->currentOffset = nodeOffset + SLIP_DRAW3D_LIST_NODE_SIZE;
	--state->remaining;
	scanOffset = state->frameRootOffsets[state->frameDepth];
	if (nodeOffset == scanOffset) {
		return 1;
	}
	for (;;) {
		SlipDraw3DListNode *scan;
		uint32_t *childOffset;

		scan = &nodePool[scanOffset / SLIP_DRAW3D_LIST_NODE_SIZE];
		childOffset = (int32_t)sortKey < (int32_t)scan->sortKey ? &scan->leftOffset : &scan->rightOffset;
		if (*childOffset == 0) {
			*childOffset = nodeOffset;
			return 1;
		}
		scanOffset = *childOffset;
	}
}

static void SlipDraw3D_ListTraverseNode(const SlipDraw3DListNode *nodePool, uint32_t nodeOffset,
                                        struct TrackViewRawBspContext *context) {
	const SlipDraw3DListNode *const node = &nodePool[nodeOffset / SLIP_DRAW3D_LIST_NODE_SIZE];
	if (node->rightOffset != 0)
		SlipDraw3D_ListTraverseNode(nodePool, node->rightOffset, context);

	if (node->callback != NULL)
		(void)node->callback(context, node->payload);
	if (node->leftOffset != 0)
		SlipDraw3D_ListTraverseNode(nodePool, node->leftOffset, context);
}

int SlipDraw3D_ListTraverse(const SlipDraw3DListState *state, const SlipDraw3DListNode *nodePool, size_t nodePoolBytes,
                            struct TrackViewRawBspContext *context) {
	(void)nodePoolBytes;
	SlipDraw3D_ListTraverseNode(nodePool, state->frameRootOffsets[state->frameDepth], context);
	return 1;
}

void SlipDraw3D_InitDefaultProjectState(SlipDraw3DProjectState *state) {
	if (state == NULL) {
		return;
	}

	memset(state, 0, sizeof(*state));
	state->minZ = SLIP_DRAW3D_DEFAULT_NEAR_DEPTH;
	state->maxZ = INT32_MAX;
	state->minX = 0;
	state->minY = 0;
	state->maxX = SLIPSTREAM_SCREEN_WIDTH - 1;
	state->maxY = SLIPSTREAM_SCREEN_HEIGHT - 1;
	state->centerX = SLIPSTREAM_SCREEN_WIDTH / 2;
	state->centerY = SLIPSTREAM_SCREEN_HEIGHT / 2;
	state->projectionScale = SLIP_DRAW3D_DEFAULT_FOCAL_LENGTH;
	state->projectionMode = SLIP_DRAW3D_PROJECTION_PERSPECTIVE;
	state->perspectiveScale = SLIP_DRAW3D_SCALE_ONE_Q16;
	state->projectionScaleFactor = SLIP_DRAW3D_DEFAULT_FOCAL_LENGTH;
	state->modeOneScale = 1;
	state->inverseProjectionScale = SLIP_DRAW3D_SCALE_ONE_Q16;
	SlipDraw3D_RefreshProjectionState(state);
}

void SlipDraw3D_SetViewport(SlipDraw3DProjectState *state, int32_t minX, int32_t minY, int32_t maxX, int32_t maxY,
                            int32_t centerX, int32_t centerY) {
	state->minX = (int16_t)(uint16_t)minX;
	state->minY = (uint16_t)minY;
	state->maxX = (uint16_t)maxX;
	state->maxY = (uint16_t)maxY;
	state->centerX = (int32_t)(((uint32_t)state->centerX & SLIP_DRAW3D_UPPER_WORD_MASK) | (uint16_t)centerX);
	state->centerY = (int32_t)(((uint32_t)state->centerY & SLIP_DRAW3D_UPPER_WORD_MASK) | (uint16_t)centerY);
	SlipDraw3D_RefreshPerspectiveScale(state);
	SlipDraw3D_RefreshProjectionState(state);
}

bool SlipDraw3D_LoadClipAndCenter(const SlipDraw3DProjectState *state, SlipDraw3DClipAndCenter *result) {
	if (state == NULL || result == NULL) {
		return false;
	}

	*result = (SlipDraw3DClipAndCenter){(uint32_t)state->minX,
	                                    (uint32_t)state->minY,
	                                    (uint32_t)state->maxX,
	                                    (uint32_t)state->maxY,
	                                    (uint32_t)state->centerX,
	                                    (uint32_t)state->centerY,
	                                    true};
	return true;
}

void SlipDraw3D_StoreClipBounds(SlipDraw3DProjectState *state, uint32_t minimumX, uint32_t minimumY, uint32_t maximumX,
                                uint32_t maximumY) {
	if (state == NULL) {
		return;
	}

	state->minX = (int16_t)(uint16_t)minimumX;
	state->minY = (uint16_t)minimumY;
	state->maxX = (uint16_t)maximumX;
	state->maxY = (uint16_t)maximumY;
	SlipDraw3D_RefreshProjectionState(state);
}

static int32_t SlipDraw3D_SubtractSignedLowWords(uint32_t lhs, uint32_t rhs) { return (int16_t)(uint16_t)(lhs - rhs); }

static int32_t SlipDraw3D_MultiplyLow32(int32_t lhs, uint32_t rhs) { return (int32_t)((uint32_t)lhs * rhs); }

static uint16_t SlipDraw3D_Root32Software(uint32_t source) {
	uint16_t sourceLow;
	uint16_t sourceHigh;
	uint16_t remainder = 0;
	uint16_t root = 0;
	uint16_t iterations;
	uint16_t shift;
	uint16_t highestBit;

	if (source == 0) {
		return 0;
	}
	highestBit = 31;
	while ((source >> highestBit) == 0) {
		--highestBit;
	}
	shift = (uint16_t)((highestBit ^ 31u) & SLIP_DRAW3D_WORD_ALIGNED_INDEX_MASK);
	source <<= shift;
	iterations = (uint16_t)(16u - (shift >> 1));
	sourceLow = (uint16_t)source;
	sourceHigh = (uint16_t)(source >> 16);
	for (unsigned iteration = 0; iteration < iterations; ++iteration) {
		const uint16_t candidateHigh = (uint16_t)(sourceHigh - SLIP_DRAW3D_ROOT16_TRIAL_BIT);
		const uint32_t subtrahend = (uint32_t)root + (sourceHigh < SLIP_DRAW3D_ROOT16_TRIAL_BIT ? 1u : 0u);
		const uint16_t candidateRemainder = (uint16_t)(remainder - subtrahend);
		bool borrow = (uint32_t)remainder < subtrahend;
		uint16_t carryLow;
		uint16_t carryHigh;

		if (borrow) {
			const uint32_t restoredHigh = (uint32_t)candidateHigh + SLIP_DRAW3D_ROOT16_TRIAL_BIT;

			sourceHigh = (uint16_t)restoredHigh;
			remainder = (uint16_t)(candidateRemainder + root + (restoredHigh >> 16));
			root <<= 1;
		} else {
			sourceHigh = candidateHigh;
			remainder = candidateRemainder;
			root = (uint16_t)((root << 1) + 1u);
		}
		carryLow = (uint16_t)(sourceLow >> 15);
		sourceLow <<= 1;
		carryHigh = (uint16_t)(sourceHigh >> 15);
		sourceHigh = (uint16_t)((sourceHigh << 1) | carryLow);
		remainder = (uint16_t)((remainder << 1) | carryHigh);
		carryLow = (uint16_t)(sourceLow >> 15);
		sourceLow <<= 1;
		carryHigh = (uint16_t)(sourceHigh >> 15);
		sourceHigh = (uint16_t)((sourceHigh << 1) | carryLow);
		remainder = (uint16_t)((remainder << 1) | carryHigh);
	}
	return root;
}

static uint32_t SlipDraw3D_Root64Software(uint32_t sourceLow, uint32_t sourceHigh) {
	uint32_t remainder = 0;
	uint32_t root = 0;
	uint32_t iterations;
	uint32_t shift;
	uint32_t highestBit;

	if (sourceHigh == 0) {
		return SlipDraw3D_Root32Software(sourceLow);
	}
	highestBit = 31;
	while ((sourceHigh >> highestBit) == 0) {
		--highestBit;
	}
	shift = (highestBit ^ 31u) & SLIP_DRAW3D_EVEN_SHIFT_COUNT_MASK;
	iterations = 32;
	if (shift != 0) {
		const uint32_t mask = UINT32_MAX << shift;
		const uint32_t rotatedLow = (sourceLow << shift) | (sourceLow >> (32u - shift));

		sourceLow = rotatedLow & mask;
		sourceHigh = (sourceHigh << shift) | (rotatedLow & ~mask);
		iterations -= shift >> 1;
	}
	for (unsigned iteration = 0; iteration < iterations; ++iteration) {
		const uint32_t candidateHigh = sourceHigh - SLIP_DRAW3D_ROOT32_TRIAL_BIT;
		const uint64_t subtrahend = (uint64_t)root + (sourceHigh < SLIP_DRAW3D_ROOT32_TRIAL_BIT ? 1u : 0u);
		const uint32_t candidateRemainder = (uint32_t)(remainder - subtrahend);
		bool borrow = (uint64_t)remainder < subtrahend;
		uint32_t carryLow;
		uint32_t carryHigh;

		if (borrow) {
			const uint64_t restoredHigh = (uint64_t)candidateHigh + SLIP_DRAW3D_ROOT32_TRIAL_BIT;

			sourceHigh = (uint32_t)restoredHigh;
			remainder = candidateRemainder + root + (uint32_t)(restoredHigh >> 32);
			root <<= 1;
		} else {
			sourceHigh = candidateHigh;
			remainder = candidateRemainder;
			root = (root << 1) + 1u;
		}
		carryLow = sourceLow >> 31;
		sourceLow <<= 1;
		carryHigh = sourceHigh >> 31;
		sourceHigh = (sourceHigh << 1) | carryLow;
		remainder = (remainder << 1) | carryHigh;
		carryLow = sourceLow >> 31;
		sourceLow <<= 1;
		carryHigh = sourceHigh >> 31;
		sourceHigh = (sourceHigh << 1) | carryLow;
		remainder = (remainder << 1) | carryHigh;
	}
	return root;
}

uint16_t SlipDraw3D_Root32(uint32_t value) { return SlipDraw3D_Root32Software(value); }

uint32_t SlipDraw3D_Root64(uint32_t valueLow, uint32_t valueHigh) {
	const uint32_t integerIndefinite = SLIP_DRAW3D_DWORD_SIGN_BIT;
	if ((int32_t)valueHigh < 0) {
		valueLow &= ~1u;
	}
	const uint32_t root = SlipDraw3D_Root64Software(valueLow, valueHigh);

	return root > INT32_MAX ? integerIndefinite : root;
}

int SlipDraw3D_ApproxAbsVectorLength(uint32_t inputX, uint32_t inputY, uint32_t inputZ,
                                     SlipDraw3DApproxAbsVectorLength *result) {
	uint32_t largestComponent = inputX;
	uint32_t secondComponent = inputY;
	uint32_t thirdComponent = inputZ;
	bool swappedFirstSecondComponent = false;
	bool swappedFirstThirdComponent = false;
	uint32_t otherSum;
	uint32_t otherQuarter;

	if (result == NULL) {
		return 0;
	}
	if ((int32_t)largestComponent < 0) {
		largestComponent = 0u - largestComponent;
	}
	if ((int32_t)secondComponent < 0) {
		secondComponent = 0u - secondComponent;
	}
	if ((int32_t)thirdComponent < 0) {
		thirdComponent = 0u - thirdComponent;
	}
	inputX = largestComponent;
	inputY = secondComponent;
	inputZ = thirdComponent;

	if (largestComponent <= secondComponent) {
		const uint32_t tmp = largestComponent;

		largestComponent = secondComponent;
		secondComponent = tmp;
		swappedFirstSecondComponent = true;
	}
	if (largestComponent <= thirdComponent) {
		const uint32_t tmp = largestComponent;

		largestComponent = thirdComponent;
		thirdComponent = tmp;
		swappedFirstThirdComponent = true;
	}

	otherSum = secondComponent + thirdComponent;
	otherQuarter = (uint32_t)((int32_t)otherSum >> 2);
	*result = (SlipDraw3DApproxAbsVectorLength){
	    inputX,           inputY,   inputZ,       swappedFirstSecondComponent,     swappedFirstThirdComponent,
	    largestComponent, otherSum, otherQuarter, largestComponent + otherQuarter, true};
	return 1;
}

int SlipDraw3D_LightDepthBlend(uint32_t depth, uint32_t fadeStart, uint32_t fadeEnd, uint32_t fadeRange,
                               SlipDraw3DLightDepthBlend *result) {
	SlipDraw3DLightDepthBlend out;

	if (result == NULL) {
		return 0;
	}

	out = (SlipDraw3DLightDepthBlend){depth, fadeStart, fadeEnd, fadeRange, SLIP_DRAW3D_LIGHT_DEPTH_BLEND_BRANCH_ZERO,
	                                  0,     0,         0,       0,         true};
	if (fadeStart == 0u || (int32_t)depth <= (int32_t)fadeStart) {
		*result = out;
		return 1;
	}
	if ((int32_t)depth >= (int32_t)fadeEnd) {
		out.branch = SLIP_DRAW3D_LIGHT_DEPTH_BLEND_BRANCH_LIMIT;
		out.fadeBlendQ14 = SLIP_Q14_ONE;
		*result = out;
		return 1;
	}
	if (fadeRange == 0u) {
		return 0;
	}

	out.branch = SLIP_DRAW3D_LIGHT_DEPTH_BLEND_BRANCH_INTERIOR;
	out.delta = depth - fadeStart;
	out.shiftedDividend = (int64_t)(int32_t)out.delta << SLIP_DRAW3D_INTERPOLATION_FRACTION_BITS;
	out.quotient = (int32_t)(out.shiftedDividend / (int32_t)fadeRange);
	out.fadeBlendQ14 = (uint32_t)out.quotient >> SLIP_DRAW3D_INTERPOLATION_TO_Q14_SHIFT;
	*result = out;
	return 1;
}

static int16_t SlipDraw3D_DivideComponentQ14(int16_t component, int16_t length) {
	return (int16_t)(((int32_t)component << SLIP_Q14_FRACTION_BITS) / (int32_t)length);
}

int SlipDraw3D_NormalizeVector2D(uint32_t inputX, uint32_t inputY, SlipDraw3DNormalizeVector2D *result) {
	int16_t componentX = (int16_t)(uint16_t)inputX;
	int16_t componentY = (int16_t)(uint16_t)inputY;
	const uint32_t squareX = (uint32_t)((int32_t)componentX * (int32_t)componentX);
	const uint32_t squareY = (uint32_t)((int32_t)componentY * (int32_t)componentY);
	const uint32_t squareSumLow = squareX + squareY;
	const uint64_t shiftedSquareSum = (uint64_t)squareSumLow << 2;
	const uint32_t doubledRoot = SlipDraw3D_Root64((uint32_t)shiftedSquareSum, (uint32_t)(shiftedSquareSum >> 32));
	const uint16_t length = (uint16_t)((doubledRoot >> 1) + (doubledRoot & 1u));

	if (result == NULL) {
		return 0;
	}
	if (length != 0) {
		uint16_t divisor = length;

		if ((int16_t)divisor < 0) {
			componentX = (int16_t)(componentX >> 1);
			componentY = (int16_t)(componentY >> 1);
			divisor >>= 1;
		}
		result->unitXQ14 = SlipDraw3D_DivideComponentQ14(componentX, (int16_t)divisor);
		result->unitYQ14 = SlipDraw3D_DivideComponentQ14(componentY, (int16_t)divisor);
		result->length = length;
	} else {
		result->unitXQ14 = 0;
		result->unitYQ14 = componentY;
		result->length = 0;
	}
	result->returned = true;
	return 1;
}

static int32_t SlipDraw3D_DivideSignedShifted16(uint32_t dividendSource, uint32_t divisorSource) {
	const int32_t dividend = (int32_t)(dividendSource << 16);
	const int32_t divisor = (int32_t)divisorSource;

	return dividend / divisor;
}

uint32_t SlipDraw3D_DetailValue(uint32_t mode, uint32_t minDepth, uint32_t detailScale, uint32_t depth) {
	uint32_t dividendLow;
	uint32_t dividendHigh;

	if (mode == SLIP_DRAW3D_PROJECTION_ORTHOGRAPHIC) {
		return detailScale;
	}
	if ((int32_t)depth <= (int32_t)minDepth) {
		return INT32_MAX;
	}
	dividendLow = detailScale << SLIP_DRAW3D_DEFAULT_FOCAL_LENGTH_SHIFT;
	dividendHigh = (uint32_t)((int32_t)detailScale >> (32 - SLIP_DRAW3D_DEFAULT_FOCAL_LENGTH_SHIFT));
	if (dividendHigh >= depth) {
		return INT32_MAX;
	}
	return (uint32_t)((((uint64_t)dividendHigh << 32) | dividendLow) / depth);
}

int SlipDraw3D_RefreshMode0Projection(uint32_t mode, uint32_t projectionScale, uint32_t minX, uint32_t maxX,
                                      uint32_t minY, uint32_t maxY, uint32_t centerX, uint32_t centerY,
                                      SlipDraw3DRefreshMode0Projection *result) {
	SlipDraw3DNormalizeVector2D minXPlane;
	SlipDraw3DNormalizeVector2D maxXPlane;
	SlipDraw3DNormalizeVector2D maxYPlane;
	SlipDraw3DNormalizeVector2D minYPlane;

	if (result == NULL || mode != SLIP_DRAW3D_PROJECTION_PERSPECTIVE || projectionScale == 0) {
		return 0;
	}
	if (!SlipDraw3D_NormalizeVector2D(minX - centerX, projectionScale, &minXPlane) ||
	    !SlipDraw3D_NormalizeVector2D((maxX + 1u) - centerX, projectionScale, &maxXPlane) ||
	    !SlipDraw3D_NormalizeVector2D((maxY + 1u) - centerY, projectionScale, &maxYPlane) ||
	    !SlipDraw3D_NormalizeVector2D(minY - centerY, projectionScale, &minYPlane)) {
		return 0;
	}

	*result =
	    (SlipDraw3DRefreshMode0Projection){mode,
	                                       projectionScale,
	                                       SlipDraw3D_DivideSignedShifted16((maxX + 1u) - centerX, projectionScale),
	                                       SlipDraw3D_DivideSignedShifted16(minX - centerX, projectionScale),
	                                       -SlipDraw3D_DivideSignedShifted16((maxY + 1u) - centerY, projectionScale),
	                                       -SlipDraw3D_DivideSignedShifted16(minY - centerY, projectionScale),
	                                       minXPlane.unitYQ14,
	                                       (int16_t)-minXPlane.unitXQ14,
	                                       (int16_t)-maxXPlane.unitYQ14,
	                                       maxXPlane.unitXQ14,
	                                       maxYPlane.unitYQ14,
	                                       maxYPlane.unitXQ14,
	                                       (int16_t)-minYPlane.unitYQ14,
	                                       (int16_t)-minYPlane.unitXQ14,
	                                       true};
	return 1;
}

int SlipDraw3D_RefreshMode1Projection(uint32_t mode, uint32_t scale, uint32_t minX, uint32_t maxX, uint32_t minY,
                                      uint32_t maxY, uint32_t centerX, uint32_t centerY,
                                      SlipDraw3DRefreshMode1Projection *result) {
	if (result == NULL || mode != SLIP_DRAW3D_PROJECTION_ORTHOGRAPHIC || scale == 0) {
		return 0;
	}

	*result = (SlipDraw3DRefreshMode1Projection){
	    mode,
	    scale,
	    SLIP_DRAW3D_ORTHOGRAPHIC_RECIPROCAL_ONE_Q30 / scale,
	    SlipDraw3D_MultiplyLow32(SlipDraw3D_SubtractSignedLowWords(maxX, centerX), scale),
	    SlipDraw3D_MultiplyLow32(SlipDraw3D_SubtractSignedLowWords(minX, centerX), scale),
	    SlipDraw3D_MultiplyLow32(SlipDraw3D_SubtractSignedLowWords(centerY, minY), scale),
	    SlipDraw3D_MultiplyLow32(SlipDraw3D_SubtractSignedLowWords(centerY, maxY), scale),
	    true};
	return 1;
}

int32_t SlipDraw3D_HorizontalProjectionScale(const SlipDraw3DProjectState *state) {
	return state->squarePixels ? state->projectionScale * SLIP_DRAW3D_SQUARE_PIXEL_SCALE_NUMERATOR /
	                                 SLIP_DRAW3D_SQUARE_PIXEL_SCALE_DENOMINATOR
	                           : state->projectionScale;
}

int SlipDraw3D_RefreshProjectFrustum(const SlipDraw3DProjectState *state, uint32_t mode,
                                     SlipDraw3DRefreshMode0Projection *result) {
	if (!SlipDraw3D_RefreshMode0Projection(mode, state->projectionScale, state->minX, state->maxX, state->minY,
	                                       state->maxY, state->centerX, state->centerY, result))
		return 0;
	if (state->squarePixels) {
		SlipDraw3DRefreshMode0Projection horizontal;
		if (!SlipDraw3D_RefreshMode0Projection(mode, SlipDraw3D_HorizontalProjectionScale(state), state->minX,
		                                       state->maxX, state->minY, state->maxY, state->centerX, state->centerY,
		                                       &horizontal))
			return 0;
		result->maxXStep = horizontal.maxXStep;
		result->minXStep = horizontal.minXStep;
		result->minXPlaneDepthQ = horizontal.minXPlaneDepthQ;
		result->minXPlaneNegXQ = horizontal.minXPlaneNegXQ;
		result->maxXPlaneNegDepthQ = horizontal.maxXPlaneNegDepthQ;
		result->maxXPlaneXQ = horizontal.maxXPlaneXQ;
	}
	return 1;
}

static void SlipDraw3D_RefreshPerspectiveScale(SlipDraw3DProjectState *state) {
	uint32_t projectionScale = (uint32_t)(((uint64_t)state->perspectiveScale * state->projectionScaleFactor) >>
	                                      SLIP_DRAW3D_SCALE_FRACTION_BITS);

	if ((int32_t)projectionScale < SLIP_DRAW3D_FOCAL_LENGTH_MINIMUM) {
		projectionScale = SLIP_DRAW3D_FOCAL_LENGTH_MINIMUM;
	}
	if ((int32_t)projectionScale > SLIP_DRAW3D_FOCAL_LENGTH_MAXIMUM) {
		projectionScale = SLIP_DRAW3D_FOCAL_LENGTH_MAXIMUM;
	}
	state->projectionScale = (int32_t)projectionScale;
}

static void SlipDraw3D_RefreshProjectionState(SlipDraw3DProjectState *state) {
	if (state->projectionMode == SLIP_DRAW3D_PROJECTION_PERSPECTIVE) {
		const int32_t horizontalScale = SlipDraw3D_HorizontalProjectionScale(state);
		if (state->projectionScale == SLIP_DRAW3D_DEFAULT_FOCAL_LENGTH && !state->squarePixels) {
			state->projectPrimary = SlipDraw3D_ProjectPerspective32Callback;
			state->projectSecondary = SlipDraw3D_ProjectPerspective16Callback;
		} else {
			state->projectPrimary = SlipDraw3D_ProjectCheckedPerspectiveCallback;
			state->projectSecondary = SlipDraw3D_ProjectCheckedPerspectiveCallback;
		}
		state->rightSlope =
		    (int32_t)(((uint32_t)state->maxX + 1u - (uint32_t)state->centerX) << SLIP_DRAW3D_SCALE_FRACTION_BITS) /
		    horizontalScale;
		state->leftSlope =
		    (int32_t)(((uint32_t)state->minX - (uint32_t)state->centerX) << SLIP_DRAW3D_SCALE_FRACTION_BITS) /
		    horizontalScale;
		state->bottomSlope = (int32_t)(0u - (uint32_t)((int32_t)(((uint32_t)state->maxY + 1u - (uint32_t)state->centerY)
		                                                         << SLIP_DRAW3D_SCALE_FRACTION_BITS) /
		                                               state->projectionScale));
		state->topSlope = (int32_t)(0u - (uint32_t)((int32_t)(((uint32_t)state->minY - (uint32_t)state->centerY)
		                                                      << SLIP_DRAW3D_SCALE_FRACTION_BITS) /
		                                            state->projectionScale));
		SlipDraw3DNormalizeVector2D normal;
		SlipDraw3D_NormalizeVector2D((uint32_t)state->minX - (uint32_t)state->centerX, (uint32_t)horizontalScale,
		                             &normal);
		state->leftNormalX = normal.unitYQ14;
		state->leftNormalZ = (int16_t)(uint16_t)(0u - (uint16_t)normal.unitXQ14);
		SlipDraw3D_NormalizeVector2D((uint32_t)state->maxX + 1u - (uint32_t)state->centerX, (uint32_t)horizontalScale,
		                             &normal);
		state->rightNormalX = (int16_t)(uint16_t)(0u - (uint16_t)normal.unitYQ14);
		state->rightNormalZ = normal.unitXQ14;
		SlipDraw3D_NormalizeVector2D((uint32_t)state->maxY + 1u - (uint32_t)state->centerY,
		                             (uint32_t)state->projectionScale, &normal);
		state->bottomNormalY = normal.unitYQ14;
		state->bottomNormalZ = normal.unitXQ14;
		SlipDraw3D_NormalizeVector2D((uint32_t)state->minY - (uint32_t)state->centerY, (uint32_t)state->projectionScale,
		                             &normal);
		state->topNormalY = (int16_t)(uint16_t)(0u - (uint16_t)normal.unitYQ14);
		state->topNormalZ = (int16_t)(uint16_t)(0u - (uint16_t)normal.unitXQ14);
		state->projectMask = SlipDraw3D_ProjectMaskPerspective;
		state->sphereOutside = SlipDraw3D_SphereOutsidePerspective;
	} else if (state->projectionMode == SLIP_DRAW3D_PROJECTION_ORTHOGRAPHIC) {
		state->modeOneReciprocal = SLIP_DRAW3D_ORTHOGRAPHIC_RECIPROCAL_ONE_Q30 / state->modeOneScale;
		state->projectPrimary = SlipDraw3D_ProjectOrthographicCallback;
		state->projectSecondary = SlipDraw3D_ProjectOrthographicCallback;
		state->modeOneMaximumXScaled =
		    (int32_t)((uint32_t)(int32_t)(int16_t)(uint16_t)((uint32_t)state->maxX - (uint32_t)state->centerX) *
		              state->modeOneScale);
		state->modeOneMinimumXScaled =
		    (int32_t)((uint32_t)(int32_t)(int16_t)(uint16_t)((uint32_t)state->minX - (uint32_t)state->centerX) *
		              state->modeOneScale);
		state->modeOneMinimumYScaled =
		    (int32_t)((uint32_t)(int32_t)(int16_t)(uint16_t)((uint32_t)state->centerY - (uint32_t)state->minY) *
		              state->modeOneScale);
		state->modeOneMaximumYScaled =
		    (int32_t)((uint32_t)(int32_t)(int16_t)(uint16_t)((uint32_t)state->centerY - (uint32_t)state->maxY) *
		              state->modeOneScale);
		state->projectMask = SlipDraw3D_ProjectMaskOrthographic;
		state->sphereOutside = SlipDraw3D_SphereOutsideOrthographic;
	}
}

void SlipDraw3D_SetProjectionMode(SlipDraw3DProjectState *state, uint16_t mode) {
	state->projectionMode = (state->projectionMode & SLIP_DRAW3D_UPPER_WORD_MASK) | mode;
	SlipDraw3D_RefreshPerspectiveScale(state);
	SlipDraw3D_RefreshProjectionState(state);
}

void SlipDraw3D_SetProjectionScale(SlipDraw3DProjectState *state, uint32_t scale) {
	if (state->projectionMode != SLIP_DRAW3D_PROJECTION_PERSPECTIVE) {
		state->modeOneScale = scale;
		if ((int32_t)scale < 1) {
			scale = 1;
		}
		if ((int32_t)scale > SLIP_DRAW3D_ORTHOGRAPHIC_SCALE_MAXIMUM) {
			scale = SLIP_DRAW3D_ORTHOGRAPHIC_SCALE_MAXIMUM;
		}
		state->modeOneScale = scale;
		SlipDraw3D_RefreshPerspectiveScale(state);
		SlipDraw3D_RefreshProjectionState(state);
		state->inverseProjectionScale = SLIP_DRAW3D_SCALE_ONE_Q16;
		return;
	}

	state->perspectiveScale = scale;
	SlipDraw3D_RefreshPerspectiveScale(state);
	state->inverseProjectionScale =
	    (uint32_t)((UINT64_C(1) << (2 * SLIP_DRAW3D_SCALE_FRACTION_BITS)) / state->perspectiveScale);
	SlipDraw3D_RefreshPerspectiveScale(state);
	SlipDraw3D_RefreshProjectionState(state);
}

void SlipDraw3D_SetProjectionScaleFactor(SlipDraw3DProjectState *state, uint16_t scaleFactor) {
	state->projectionScaleFactor = (state->projectionScaleFactor & SLIP_DRAW3D_UPPER_WORD_MASK) | scaleFactor;
	SlipDraw3D_RefreshPerspectiveScale(state);
	SlipDraw3D_RefreshProjectionState(state);
}

void SlipDraw3D_SetCameraDistance(SlipDraw3DProjectState *state, uint32_t cameraDistance) {
	if (state->projectionMode != SLIP_DRAW3D_PROJECTION_PERSPECTIVE) {
		SlipDraw3D_SetProjectionScale(state, cameraDistance / state->projectionScaleFactor);
	}
}

void SlipDraw3D_ProjectModeOne(const SlipDraw3DProjectState *state, int32_t horizontal, int32_t vertical,
                               int32_t *screenX, int32_t *screenY) {
	const int32_t projectedX = (int32_t)((uint64_t)((int64_t)horizontal * (int32_t)state->modeOneReciprocal) >>
	                                     SLIP_DRAW3D_ORTHOGRAPHIC_RECIPROCAL_FRACTION_BITS);
	const int32_t projectedY = (int32_t)((uint64_t)((int64_t)vertical * (int32_t)state->modeOneReciprocal) >>
	                                     SLIP_DRAW3D_ORTHOGRAPHIC_RECIPROCAL_FRACTION_BITS);

	*screenX = (int32_t)((uint32_t)projectedX + (uint32_t)state->centerX);
	*screenY = (int32_t)((uint32_t)(0u - (uint32_t)projectedY) + (uint32_t)state->centerY);
}

static int32_t SlipDraw3D_MultiplySigned32Shift16(int32_t left, int32_t right) {
	return (int32_t)((uint64_t)((int64_t)left * (int64_t)right) >> SLIP_DRAW3D_SCALE_FRACTION_BITS);
}

uint32_t SlipDraw3D_ProjectMaskPerspective(SlipDraw3DVec32 point, const SlipDraw3DProjectState *state) {
	uint32_t mask = 0;

	if (point.x >= SlipDraw3D_MultiplySigned32Shift16(state->rightSlope, point.z)) {
		mask |= SLIP_BOUNDS_CLIP_RIGHT;
	}
	if (point.x < SlipDraw3D_MultiplySigned32Shift16(state->leftSlope, point.z)) {
		mask |= SLIP_BOUNDS_CLIP_LEFT;
	}
	if (point.y >= SlipDraw3D_MultiplySigned32Shift16(state->topSlope, point.z)) {
		mask |= SLIP_BOUNDS_CLIP_TOP;
	}
	if (point.y < SlipDraw3D_MultiplySigned32Shift16(state->bottomSlope, point.z)) {
		mask |= SLIP_BOUNDS_CLIP_BOTTOM;
	}
	return mask;
}

uint32_t SlipDraw3D_ProjectMaskOrthographic(SlipDraw3DVec32 point, const SlipDraw3DProjectState *state) {
	uint32_t mask = 0;

	if (point.x <= state->modeOneMinimumXScaled) {
		mask |= SLIP_BOUNDS_CLIP_LEFT;
	}
	if (point.x >= state->modeOneMaximumXScaled) {
		mask |= SLIP_BOUNDS_CLIP_RIGHT;
	}
	if (point.y <= state->modeOneMaximumYScaled) {
		mask |= SLIP_BOUNDS_CLIP_BOTTOM;
	}
	if (point.y >= state->modeOneMinimumYScaled) {
		mask |= SLIP_BOUNDS_CLIP_TOP;
	}
	return mask;
}

bool SlipDraw3D_SphereOutsidePerspective(SlipDraw3DVec32 center, int32_t radius, const SlipDraw3DProjectState *state) {
	if ((int32_t)((uint32_t)center.z + (uint32_t)radius) <= state->minZ)
		return true;
	if ((int32_t)((uint32_t)center.z - (uint32_t)radius) >= state->maxZ)
		return true;
	uint32_t leftDistance = (uint32_t)((uint64_t)((int64_t)state->leftNormalX * center.x) >> SLIP_Q14_FRACTION_BITS);
	leftDistance += (uint32_t)((uint64_t)((int64_t)state->leftNormalZ * center.z) >> SLIP_Q14_FRACTION_BITS);
	leftDistance += (uint32_t)radius;
	if ((int32_t)leftDistance < 0)
		return true;
	uint32_t rightDistance = (uint32_t)((uint64_t)((int64_t)state->rightNormalX * center.x) >> SLIP_Q14_FRACTION_BITS);
	rightDistance += (uint32_t)((uint64_t)((int64_t)state->rightNormalZ * center.z) >> SLIP_Q14_FRACTION_BITS);
	rightDistance += (uint32_t)radius;
	if ((int32_t)rightDistance < 0)
		return true;
	uint32_t bottomDistance =
	    (uint32_t)((uint64_t)((int64_t)state->bottomNormalY * center.y) >> SLIP_Q14_FRACTION_BITS);
	bottomDistance += (uint32_t)((uint64_t)((int64_t)state->bottomNormalZ * center.z) >> SLIP_Q14_FRACTION_BITS);
	bottomDistance += (uint32_t)radius;
	if ((int32_t)bottomDistance < 0)
		return true;
	uint32_t topDistance = (uint32_t)((uint64_t)((int64_t)state->topNormalY * center.y) >> SLIP_Q14_FRACTION_BITS);
	topDistance += (uint32_t)((uint64_t)((int64_t)state->topNormalZ * center.z) >> SLIP_Q14_FRACTION_BITS);
	topDistance += (uint32_t)radius;
	if ((int32_t)topDistance < 0)
		return true;
	return false;
}

bool SlipDraw3D_SphereOutsideOrthographic(SlipDraw3DVec32 center, int32_t radius, const SlipDraw3DProjectState *state) {
	if ((int32_t)((uint32_t)center.z + (uint32_t)radius) <= state->minZ)
		return true;
	if ((int32_t)((uint32_t)center.z - (uint32_t)radius) >= state->maxZ)
		return true;
	if ((int32_t)((uint32_t)center.x + (uint32_t)radius) < state->modeOneMinimumXScaled)
		return true;
	if ((int32_t)((uint32_t)center.x - (uint32_t)radius) > state->modeOneMaximumXScaled)
		return true;
	if ((int32_t)((uint32_t)center.y + (uint32_t)radius) < state->modeOneMaximumYScaled)
		return true;
	if ((int32_t)((uint32_t)center.y - (uint32_t)radius) > state->modeOneMinimumYScaled)
		return true;
	return false;
}

static uint32_t SlipDraw3D_ClassifyProjectionBounds(SlipDraw3DVec32 point, const SlipDraw3DProjectState *state) {
	if (state->projectionMode == SLIP_DRAW3D_PROJECTION_PERSPECTIVE) {
		return SlipDraw3D_ProjectMaskPerspective(point, state);
	}
	if (state->projectionMode == SLIP_DRAW3D_PROJECTION_ORTHOGRAPHIC) {
		return SlipDraw3D_ProjectMaskOrthographic(point, state);
	}
	return UINT32_MAX;
}

int32_t SlipDraw3D_ClassifyPoints(const SlipDraw3DVec32 *const *points, uint16_t pointCount,
                                  const SlipDraw3DProjectState *state) {
	uint32_t allMasks = UINT32_MAX;
	uint32_t anyMasks = 0;
	uint16_t pointIndex;

	if (points == NULL || pointCount == 0u || state == NULL) {
		return -1;
	}
	for (pointIndex = 0; pointIndex < pointCount; ++pointIndex) {
		uint32_t depthClipMask = 0;

		if (points[pointIndex] == NULL) {
			return -1;
		}
		if (points[pointIndex]->z < (int32_t)SlipDraw3D_minimumDepth) {
			depthClipMask |= SLIP_BOX_CLIP_NEAR;
		}
		if (points[pointIndex]->z > (int32_t)SlipDraw3D_maximumDepth) {
			depthClipMask |= SLIP_BOX_CLIP_FAR;
		}
		allMasks &= depthClipMask;
		anyMasks |= depthClipMask;
	}
	if (allMasks != 0u) {
		return -1;
	}

	allMasks = UINT32_MAX;
	for (pointIndex = 0; pointIndex < pointCount; ++pointIndex) {
		const uint32_t projectionClipMask = SlipDraw3D_ClassifyProjectionBounds(*points[pointIndex], state);

		allMasks &= projectionClipMask;
		anyMasks |= projectionClipMask;
	}
	if (allMasks != 0u) {
		return -1;
	}
	return anyMasks != 0u ? 1 : 0;
}

static uint16_t SlipDraw3D_pointColor;

void SlipDraw3D_DrawPoint(SlipDraw3DVec32 point, uint16_t color, const SlipDraw3DProjectState *state) {
	if (point.z < (int32_t)SlipDraw3D_minimumDepth || point.z > (int32_t)SlipDraw3D_maximumDepth)
		return;
	SlipDraw3D_pointColor = color;
	if ((uint16_t)state->projectMask(point, state) != 0)
		return;
	int32_t x, y;
	state->projectPrimary(point, &x, &y, (void *)state);
	if (x < state->minX || x > state->maxX || y < state->minY || y > state->maxY)
		return;
	if (SlipRaceGpu_Active())
		Raster_DrawLineClipped((uint8_t)SlipDraw3D_pointColor, (int16_t)x, (int16_t)y, (int16_t)x, (int16_t)y);
	else
		Raster_PutPixelClipped((uint8_t)SlipDraw3D_pointColor, (int16_t)y, (int16_t)x);
}

int SlipDraw3D_ProjectVisiblePoint(SlipDraw3DVec32 point, const SlipDraw3DProjectState *state, int32_t *screenX,
                                   int32_t *screenY) {
	if (state == NULL || screenX == NULL || screenY == NULL || point.z < (int32_t)SlipDraw3D_minimumDepth ||
	    point.z > (int32_t)SlipDraw3D_maximumDepth) {
		return 0;
	}

	if ((uint16_t)state->projectMask(point, state) != 0) {
		return 0;
	}
	state->projectPrimary(point, screenX, screenY, (void *)state);
	return 1;
}

int SlipDraw3D_ProjectScreen(SlipDraw3DVec32 world, const SlipDraw3DProjectState *state, int32_t *screenX,
                             int32_t *screenY) {
	int64_t projectedX;
	int64_t projectedY;

	if (state == NULL || screenX == NULL || screenY == NULL || world.z < state->minZ || world.z > state->maxZ ||
	    world.z == 0) {
		return 0;
	}

	projectedX = ((int64_t)world.x * SlipDraw3D_HorizontalProjectionScale(state)) / world.z;
	projectedY = ((int64_t)world.y * state->projectionScale) / world.z;
	if (projectedX < INT32_MIN || projectedX > INT32_MAX || projectedY < INT32_MIN || projectedY > INT32_MAX) {
		return 0;
	}

	*screenX = state->centerX + (int32_t)projectedX;
	*screenY = state->centerY - (int32_t)projectedY;
	return 1;
}

void SlipDraw3D_ProjectCheckedPerspectiveCallback(SlipDraw3DVec32 world, int32_t *screenX, int32_t *screenY,
                                                  void *userData) {
	if (!SlipDraw3D_ProjectScreen(world, (const SlipDraw3DProjectState *)userData, screenX, screenY)) {
		*screenX = INT32_MAX;
		*screenY = INT32_MAX;
	}
}

void SlipDraw3D_ProjectOrthographicCallback(SlipDraw3DVec32 world, int32_t *screenX, int32_t *screenY, void *userData) {
	SlipDraw3D_ProjectModeOne(userData, world.x, world.y, screenX, screenY);
}

void SlipDraw3D_ProjectPerspective32Callback(SlipDraw3DVec32 world, int32_t *screenX, int32_t *screenY,
                                             void *userData) {
	const SlipDraw3DProjectState *const state = (const SlipDraw3DProjectState *)userData;
	int32_t projectedX;
	int32_t projectedY;

	projectedX = (int32_t)(((int64_t)world.x * SLIP_DRAW3D_DEFAULT_FOCAL_LENGTH) / world.z);
	projectedY = (int32_t)(((int64_t)world.y * SLIP_DRAW3D_DEFAULT_FOCAL_LENGTH) / world.z);
	*screenX = (int32_t)((uint32_t)projectedX + (uint32_t)state->centerX);
	*screenY = (int32_t)((uint32_t)(-projectedY) + (uint32_t)state->centerY);
}

void SlipDraw3D_ProjectPerspective16Callback(SlipDraw3DVec32 world, int32_t *screenX, int32_t *screenY,
                                             void *userData) {
	const SlipDraw3DProjectState *const state = (const SlipDraw3DProjectState *)userData;
	int16_t projectedX;
	int16_t projectedY;
	int16_t divisor;
	int32_t dividend;

	if (world.z > INT16_MAX) {
		SlipDraw3D_ProjectPerspective32Callback(world, screenX, screenY, userData);
		return;
	}

	divisor = (int16_t)world.z;
	dividend = (int32_t)((uint32_t)world.x << SLIP_DRAW3D_DEFAULT_FOCAL_LENGTH_SHIFT);
	projectedX = (int16_t)(dividend / divisor);
	dividend = (int32_t)((uint32_t)world.y << SLIP_DRAW3D_DEFAULT_FOCAL_LENGTH_SHIFT);
	projectedY = (int16_t)(dividend / divisor);
	*screenX = (int32_t)((uint32_t)(int32_t)projectedX + (uint32_t)state->centerX);
	*screenY = (int32_t)((uint32_t)(-(int32_t)projectedY) + (uint32_t)state->centerY);
}

static int SlipDraw3D_IsAxisRect(const SlipDraw3DRasterPoint *p) {
	if (p[0].x == p[1].x) {
		return p[0].y == p[3].y && p[2].y == p[1].y && p[2].x == p[3].x;
	}
	return p[0].y == p[1].y && p[0].x == p[3].x && p[2].x == p[1].x && p[2].y == p[3].y;
}

int SlipDraw3D_PrepareFlatDispatch(const SlipDraw3DDrawRecord *drawRecords, uint16_t drawRecordCount, uint32_t drawMode,
                                   uint32_t renderFlags, SlipDraw3DRasterPoint *points, size_t pointCapacity,
                                   SlipDraw3DDispatchResult *result) {
	uint16_t i;

	if (drawRecords == NULL || points == NULL || result == NULL || drawRecordCount == 0 ||
	    pointCapacity < drawRecordCount) {
		return 0;
	}

	memset(result, 0, sizeof(*result));
	if (drawMode != SLIP_POLYGON_DRAW_FLAT && drawMode != SLIP_POLYGON_DRAW_LINE &&
	    drawMode != SLIP_POLYGON_DRAW_DITHERED) {
		result->kind = SLIP_DRAW3D_DISPATCH_UNSUPPORTED;
		return 1;
	}

	for (i = 0; i < drawRecordCount; ++i) {
		points[i].x = drawRecords[i].screenX;
		points[i].y = drawRecords[i].screenY;
	}

	result->pointCount = drawRecordCount;
	if ((renderFlags & SLIP_RENDER_WIREFRAME) != 0) {
		result->kind = SLIP_DRAW3D_DISPATCH_WIREFRAME;
		return 1;
	}
	if (drawMode == SLIP_POLYGON_DRAW_LINE) {
		result->kind = drawRecordCount == 2 ? SLIP_DRAW3D_DISPATCH_SOLID_LINE : SLIP_DRAW3D_DISPATCH_UNSUPPORTED;
	} else if (drawMode == SLIP_POLYGON_DRAW_DITHERED) {
		result->kind = SLIP_DRAW3D_DISPATCH_DITHERED_FLAT_POLYGON;
	} else if (drawRecordCount == SLIP_POLYGON_RECTANGLE_VERTICES && SlipDraw3D_IsAxisRect(points)) {
		result->kind = SLIP_DRAW3D_DISPATCH_SOLID_RECT;
	} else {
		result->kind = SLIP_DRAW3D_DISPATCH_SOLID_FLAT_POLYGON;
	}
	return 1;
}

int SlipDraw3D_RasterizeFlatDispatch(uint8_t color, uint8_t ditherBits, const SlipDraw3DRasterPoint *points,
                                     const SlipDraw3DDispatchResult *dispatch) {
	RasterPoint *rasterPoints;
	uint16_t i;

	if (points == NULL || dispatch == NULL || dispatch->pointCount == 0) {
		return 0;
	}
	if (dispatch->kind == SLIP_DRAW3D_DISPATCH_SOLID_RECT) {
		int32_t minX = points[0].x;
		int32_t maxX = points[0].x;
		int32_t minY = points[0].y;
		int32_t maxY = points[0].y;

		if (dispatch->pointCount != SLIP_POLYGON_RECTANGLE_VERTICES) {
			return 0;
		}
		for (i = 1; i < dispatch->pointCount; ++i) {
			if (points[i].x < minX) {
				minX = points[i].x;
			}
			if (points[i].x > maxX) {
				maxX = points[i].x;
			}
			if (points[i].y < minY) {
				minY = points[i].y;
			}
			if (points[i].y > maxY) {
				maxY = points[i].y;
			}
		}
		Raster_FillRectUnchecked(color, (int16_t)minX, (int16_t)minY, (int16_t)maxX, (int16_t)maxY);
		return 1;
	}
	if (dispatch->kind == SLIP_DRAW3D_DISPATCH_SOLID_LINE) {
		if (dispatch->pointCount != 2) {
			return 0;
		}
		Raster_DrawLineSolid(color, (int16_t)points[0].x, (int16_t)points[0].y, (int16_t)points[1].x,
		                     (int16_t)points[1].y);
		return 1;
	}

	if (dispatch->kind != SLIP_DRAW3D_DISPATCH_SOLID_FLAT_POLYGON &&
	    dispatch->kind != SLIP_DRAW3D_DISPATCH_DITHERED_FLAT_POLYGON) {
		return 0;
	}
	if (dispatch->pointCount < 3) {
		return 0;
	}

	rasterPoints = (RasterPoint *)malloc((size_t)dispatch->pointCount * sizeof(*rasterPoints));
	if (rasterPoints == NULL) {
		return 0;
	}
	for (i = 0; i < dispatch->pointCount; ++i) {
		rasterPoints[i].x = points[i].x;
		rasterPoints[i].y = points[i].y;
	}
	if (dispatch->kind == SLIP_DRAW3D_DISPATCH_DITHERED_FLAT_POLYGON) {
		Raster_DrawDitheredFlatPolygon(color, ditherBits, rasterPoints, dispatch->pointCount);
	} else {
		Raster_DrawSolidFlatPolygon(color, rasterPoints, dispatch->pointCount);
	}
	free(rasterPoints);
	return 1;
}

int SlipDraw3D_RasterizeFlatRing(const SlipDraw3DRecordPool *pool, uint32_t inputActiveHeadOffset, uint32_t drawMode,
                                 uint32_t renderFlags, uint32_t materialColor, uint32_t materialDitherBits,
                                 uint32_t linkOffset, SlipDraw3DRasterPoint *points, size_t pointCapacity,
                                 SlipDraw3DFlatRingDispatch *result) {
	const uint8_t *recordPoolBytes;
	size_t recordPoolBytesCount;
	uint32_t currentOffset;
	uint16_t pointCount;
	RasterShadedPoint shadedPoints[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	uint8_t firstShade = 0;
	bool shadesDiffer = false;

	if (pool == NULL || points == NULL || result == NULL || pointCapacity == 0u) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->inputActiveHeadOffset = inputActiveHeadOffset;
	result->linkOffset = linkOffset;
	result->drawMode = drawMode;
	result->renderFlags = renderFlags;
	result->materialColor = materialColor;
	result->materialDitherBits = materialDitherBits;

	recordPoolBytes = SlipDraw3D_RecordPoolConstBytes(pool);
	recordPoolBytesCount = SlipDraw3D_RecordPoolByteSize();
	if (recordPoolBytes == NULL ||
	    (linkOffset != SLIP_DRAW3D_RECORD_NEXT_OFFSET && linkOffset != SLIP_DRAW3D_RECORD_PREV_OFFSET) ||
	    !SlipDraw3D_LinkedRecordOffsetValid(recordPoolBytesCount, inputActiveHeadOffset)) {
		return 0;
	}

	currentOffset = inputActiveHeadOffset;
	pointCount = 0;
	do {
		const uint8_t *record;

		if (pointCount >= SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT || (size_t)pointCount >= pointCapacity ||
		    !SlipDraw3D_LinkedRecordOffsetValid(recordPoolBytesCount, currentOffset)) {
			return 0;
		}
		record = recordPoolBytes + currentOffset;
		const uint8_t *rasterPoint = record + SLIP_DRAW3D_VERTEX_RECORD_SCREEN_OFFSET;

		if (drawMode == SLIP_POLYGON_DRAW_SHADED ||
		    ((drawMode == SLIP_POLYGON_DRAW_FLAT || drawMode == SLIP_POLYGON_DRAW_DITHERED) &&
		     (renderFlags & SLIP_RENDER_WIREFRAME) == 0)) {
			uint8_t *const destination = boundPointBuffer + (size_t)pointCount * sizeof(RasterTexturedPoint);
			SlipDraw3D_WriteLE32(destination, SlipBytes_ReadLE32(rasterPoint));
			SlipDraw3D_WriteLE32(destination + offsetof(RasterPoint, y),
			                     SlipBytes_ReadLE32(rasterPoint + offsetof(RasterPoint, y)));
			if (drawMode == SLIP_POLYGON_DRAW_SHADED)
				SlipDraw3D_WriteLE32(destination + offsetof(RasterShadedPoint, shade),
				                     SlipBytes_ReadLE16(record + offsetof(SlipDraw3DDrawRecord, shade)));
			rasterPoint = destination;
		}
		points[pointCount].x = SlipBytes_ReadLEI32(rasterPoint);
		points[pointCount].y = SlipBytes_ReadLEI32(rasterPoint + offsetof(RasterPoint, y));
		shadedPoints[pointCount].x = points[pointCount].x;
		shadedPoints[pointCount].y = points[pointCount].y;
		shadedPoints[pointCount].shade = drawMode == SLIP_POLYGON_DRAW_SHADED
		                                     ? SlipBytes_ReadLE16(rasterPoint + offsetof(RasterShadedPoint, shade))
		                                     : SlipBytes_ReadLE16(record + offsetof(SlipDraw3DDrawRecord, shade));
		if (pointCount == 0u) {
			firstShade = (uint8_t)(shadedPoints[pointCount].shade >> 8);
		} else if ((uint8_t)(shadedPoints[pointCount].shade >> 8) != firstShade) {
			shadesDiffer = true;
		}
		++pointCount;
		currentOffset = SlipBytes_ReadLE32(record + linkOffset);
	} while (currentOffset != inputActiveHeadOffset);

	result->pointCount = pointCount;
	result->dispatch.pointCount = pointCount;
	result->returned = true;

	if (drawMode != SLIP_POLYGON_DRAW_FLAT && drawMode != SLIP_POLYGON_DRAW_SHADED &&
	    drawMode != SLIP_POLYGON_DRAW_LINE && drawMode != SLIP_POLYGON_DRAW_DITHERED) {
		result->dispatch.kind = SLIP_DRAW3D_DISPATCH_UNSUPPORTED;
		return 1;
	}

	if (drawMode == SLIP_POLYGON_DRAW_SHADED) {
		result->dispatch.kind = SLIP_DRAW3D_DISPATCH_SHADED_FLAT_POLYGON;
		if (shadesDiffer) {
			Raster_DrawShadedFlatPolygon(shadedPoints, pointCount);
		} else {
			Raster_DrawSolidFlatPolygon(firstShade, (const RasterPoint *)points, pointCount);
		}
		result->rasterized = true;
		return 1;
	}
	if ((renderFlags & SLIP_RENDER_WIREFRAME) != 0u) {
		result->dispatch.kind = SLIP_DRAW3D_DISPATCH_WIREFRAME;
		return 1;
	}
	if (drawMode == SLIP_POLYGON_DRAW_LINE) {
		result->dispatch.kind = pointCount == 2u ? SLIP_DRAW3D_DISPATCH_SOLID_LINE : SLIP_DRAW3D_DISPATCH_UNSUPPORTED;
	} else if (drawMode == SLIP_POLYGON_DRAW_DITHERED) {
		result->dispatch.kind = SLIP_DRAW3D_DISPATCH_DITHERED_FLAT_POLYGON;
	} else if (pointCount == SLIP_POLYGON_RECTANGLE_VERTICES && SlipDraw3D_IsAxisRect(points)) {
		result->dispatch.kind = SLIP_DRAW3D_DISPATCH_SOLID_RECT;
	} else {
		result->dispatch.kind = SLIP_DRAW3D_DISPATCH_SOLID_FLAT_POLYGON;
	}
	result->rasterized = SlipDraw3D_RasterizeFlatDispatch((uint8_t)materialColor, (uint8_t)materialDitherBits, points,
	                                                      &result->dispatch) != 0;
	return 1;
}

int SlipDraw3D_PrepareTexturedDispatch(const SlipDraw3DRecordPool *pool, uint32_t inputActiveHeadOffset,
                                       uint32_t drawMode, uint32_t renderFlags, uint32_t textureHandle,
                                       uint32_t reverseTraversal, uint32_t pointBufferBase,
                                       SlipDraw3DTexturedDispatchPoint *points, size_t pointCapacity,
                                       SlipDraw3DTexturedDispatchVisit *visits, size_t visitCapacity,
                                       SlipDraw3DTexturedDispatch *result) {
	size_t recordPoolBytesCount;
	uint32_t linkOffset;
	uint32_t currentOffset;
	uint32_t pointBufferOffset;
	size_t pointCount;

	if (result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->savedGeneralState = true;
	result->linkOffsetInitial = SLIP_DRAW3D_RECORD_NEXT_OFFSET;
	result->reverseTraversal = reverseTraversal;
	linkOffset = reverseTraversal == 0 ? SLIP_DRAW3D_RECORD_NEXT_OFFSET : SLIP_DRAW3D_RECORD_PREV_OFFSET;
	result->linkOffset = linkOffset;
	result->drawMode = drawMode;
	result->lineBranch = drawMode == SLIP_POLYGON_DRAW_LINE;
	result->flatBranch = drawMode == SLIP_POLYGON_DRAW_FLAT;
	result->ditheredBranch = drawMode == SLIP_POLYGON_DRAW_DITHERED;
	result->shadedBranch = drawMode == SLIP_POLYGON_DRAW_SHADED;
	result->branchDefaultTextured =
	    !result->lineBranch && !result->flatBranch && !result->ditheredBranch && !result->shadedBranch;
	if (!result->branchDefaultTextured) {
		return 1;
	}
	if (pool == NULL || points == NULL || visits == NULL || pointCapacity == 0u || visitCapacity == 0u) {
		return 0;
	}
	recordPoolBytesCount = SlipDraw3D_RecordPoolByteSize();
	if (!SlipDraw3D_LinkedRecordOffsetValid(recordPoolBytesCount, inputActiveHeadOffset)) {
		return 0;
	}

	result->pointBufferBase = pointBufferBase;
	result->inputActiveHeadOffset = inputActiveHeadOffset;
	currentOffset = inputActiveHeadOffset;
	pointBufferOffset = pointBufferBase;
	pointCount = 0;
	do {
		const SlipDraw3DLinkedDrawRecord *record;
		SlipDraw3DTexturedDispatchPoint point;
		uint32_t nextOffset;

		if (pointCount >= pointCapacity || pointCount >= visitCapacity ||
		    pointCount >= SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT ||
		    !SlipDraw3D_LinkedRecordOffsetValid(recordPoolBytesCount, currentOffset)) {
			return 0;
		}
		record = &pool->records[currentOffset / SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE];
		point = (SlipDraw3DTexturedDispatchPoint){.screenX = record->drawRecord.screenX,
		                                          .screenY = record->drawRecord.screenY,
		                                          .textureU = (uint16_t)record->drawRecord.textureU,
		                                          .textureV = (uint16_t)record->drawRecord.textureV,
		                                          .depth = record->drawRecord.world.z};
		points[pointCount] = point;
		nextOffset = linkOffset == SLIP_DRAW3D_RECORD_NEXT_OFFSET ? record->links.nextOffset : record->links.prevOffset;
		visits[pointCount] = (SlipDraw3DTexturedDispatchVisit){.recordOffset = currentOffset,
		                                                       .pointBufferOffset = pointBufferOffset,
		                                                       .point = point,
		                                                       .nextRecordOffset = nextOffset,
		                                                       .pointCountAfterInc = (uint32_t)pointCount + 1u,
		                                                       .loop = nextOffset != inputActiveHeadOffset};
		++pointCount;
		pointBufferOffset += sizeof(RasterTexturedPoint);
		currentOffset = nextOffset;
	} while (currentOffset != inputActiveHeadOffset);

	result->textureHandle = textureHandle;
	result->pointBufferReset = pointBufferBase;
	result->renderFlags = renderFlags;
	result->pointCount = pointCount;
	if ((renderFlags & SLIP_RENDER_ALTERNATE_TEXTURE_RASTER) != 0u) {
		if ((renderFlags & SLIP_RENDER_MASKED_TEXTURE) != 0u) {
			result->rasterizerCall = SLIP_DRAW3D_TEXTURED_DISPATCH_OPAQUE_AFFINE;
			result->calledOpaqueAffineRasterizer = true;
		} else {
			result->rasterizerCall = SLIP_DRAW3D_TEXTURED_DISPATCH_TRANSPARENT_AFFINE;
			result->calledTransparentAffineRasterizer = true;
		}
	} else if ((renderFlags & SLIP_RENDER_MASKED_TEXTURE) != 0u) {
		result->rasterizerCall = SLIP_DRAW3D_TEXTURED_DISPATCH_OPAQUE_PERSPECTIVE;
		result->calledOpaquePerspectiveRasterizer = true;
	} else {
		result->rasterizerCall = SLIP_DRAW3D_TEXTURED_DISPATCH_TRANSPARENT_PERSPECTIVE;
		result->calledTransparentPerspectiveRasterizer = true;
	}
	result->restoredGeneralState = true;
	result->returned = true;
	return 1;
}

int SlipDraw3D_EmitConditionalPolygon(uint32_t postPlaneHead, uint32_t renderFlags, uint16_t countAndFlags,
                                      int materialGateCarry, SlipDraw3DConditionalPolygonEmission *result) {
	bool carryFromBuilder = materialGateCarry != 0;
	bool usesIndexedPath = postPlaneHead != 0 || (renderFlags & SLIP_SHAPE_INSIDE_VIEW) == 0;

	if (result == NULL) {
		return 0;
	}
	*result = (SlipDraw3DConditionalPolygonEmission){.savedGeneralState = true,
	                                                 .postPlaneHead = postPlaneHead,
	                                                 .renderFlags = renderFlags,
	                                                 .postPlaneBranch = postPlaneHead != 0,
	                                                 .insideViewBranch = postPlaneHead == 0 &&
	                                                                     (renderFlags & SLIP_SHAPE_INSIDE_VIEW) != 0,
	                                                 .callReturnActiveRing = usesIndexedPath,
	                                                 .callMaterialGate = usesIndexedPath,
	                                                 .materialGateCarry = usesIndexedPath && carryFromBuilder,
	                                                 .jumpOnCarry = usesIndexedPath && carryFromBuilder,
	                                                 .callRasterizeFlatDispatch = usesIndexedPath && !carryFromBuilder,
	                                                 .clearedIndexedPathCarry = usesIndexedPath && !carryFromBuilder,
	                                                 .countAndFlags = (uint32_t)countAndFlags,
	                                                 .calledUnclippedPolygon = !usesIndexedPath,
	                                                 .clearedUnclippedPathCarry = !usesIndexedPath,
	                                                 .restoredGeneralState = true,
	                                                 .returned = true,
	                                                 .carryOut = usesIndexedPath && carryFromBuilder};
	return 1;
}

int SlipDraw3D_EmitActiveMaterialPolygon(int carryFrom, SlipDraw3DActiveMaterialPolygonEmission *result) {
	bool carry = carryFrom != 0;

	if (result == NULL) {
		return 0;
	}
	*result = (SlipDraw3DActiveMaterialPolygonEmission){.savedGeneralState = true,
	                                                    .callReturnActiveRing = true,
	                                                    .calledBuildActiveMaterialRing = true,
	                                                    .materialRingRejected = carry,
	                                                    .jumpOnCarry = carry,
	                                                    .callRasterizeFlatDispatch = !carry,
	                                                    .clearedRejectCarry = !carry,
	                                                    .restoredGeneralState = true,
	                                                    .returned = true,
	                                                    .carryOut = carry};
	return 1;
}

int SlipDraw3D_EmitIndexedTexturedPolygon(int indexedRingRejected, int flatDispatchCarry,
                                          SlipDraw3DIndexedTexturedPolygonEmission *result) {
	bool carryFromBuilder = indexedRingRejected != 0;
	bool carryFromDispatch = flatDispatchCarry != 0;

	if (result == NULL) {
		return 0;
	}
	*result = (SlipDraw3DIndexedTexturedPolygonEmission){.savedGeneralState = true,
	                                                     .callReturnActiveRing = true,
	                                                     .calledBuildTexturedRing = true,
	                                                     .texturedRingRejected = carryFromBuilder,
	                                                     .jumpOnCarry = carryFromBuilder,
	                                                     .callRasterizeFlatDispatch = !carryFromBuilder,
	                                                     .flatDispatchCarry = !carryFromBuilder && carryFromDispatch,
	                                                     .restoredGeneralState = true,
	                                                     .returned = true,
	                                                     .carryOut = carryFromBuilder ||
	                                                                 (!carryFromBuilder && carryFromDispatch)};
	return 1;
}

int SlipDraw3D_EmitPointPolygon(int draw3DPointPolygonCarry, SlipDraw3DPointPolygonEmission *result) {
	bool carry = draw3DPointPolygonCarry != 0;

	if (result == NULL) {
		return 0;
	}
	*result = (SlipDraw3DPointPolygonEmission){.savedGeneralState = true,
	                                           .callReturnActiveRing = true,
	                                           .callDraw3DPointPolygon = true,
	                                           .draw3DPointPolygonCarry = carry,
	                                           .jumpOnCarry = carry,
	                                           .callRasterizeFlatDispatch = !carry,
	                                           .clearedRejectCarry = !carry,
	                                           .restoredGeneralState = true,
	                                           .returned = true,
	                                           .carryOut = carry};
	return 1;
}

int SlipDraw3D_EmitLinePair(int carryFrom, SlipDraw3DLinePairEmission *result) {
	bool carry = carryFrom != 0;

	if (result == NULL) {
		return 0;
	}
	*result = (SlipDraw3DLinePairEmission){.savedGeneralState = true,
	                                       .callReturnActiveRing = true,
	                                       .calledBuildLinePair = true,
	                                       .linePairRejected = carry,
	                                       .jumpOnCarry = carry,
	                                       .callRasterizeFlatDispatch = !carry,
	                                       .clearedRejectCarry = !carry,
	                                       .restoredGeneralState = true,
	                                       .returned = true,
	                                       .carryOut = carry};
	return 1;
}

int SlipDraw3D_EmitLine(int rendererBuildLineCarry, SlipDraw3DLineEmission *result) {
	bool carry = rendererBuildLineCarry != 0;

	if (result == NULL) {
		return 0;
	}
	*result = (SlipDraw3DLineEmission){.savedGeneralState = true,
	                                   .callReturnActiveRing = true,
	                                   .callRendererBuildLine = true,
	                                   .rendererBuildLineCarry = carry,
	                                   .jumpOnCarry = carry,
	                                   .callRasterizeFlatDispatch = !carry,
	                                   .clearedRejectCarry = !carry,
	                                   .restoredGeneralState = true,
	                                   .returned = true,
	                                   .carryOut = carry};
	return 1;
}

uint8_t *SlipDraw3D_RecordPoolBytes(SlipDraw3DRecordPool *pool) {
	if (pool == NULL) {
		return NULL;
	}
	return (uint8_t *)pool->records;
}

const uint8_t *SlipDraw3D_RecordPoolConstBytes(const SlipDraw3DRecordPool *pool) {
	if (pool == NULL) {
		return NULL;
	}
	return (const uint8_t *)pool->records;
}

size_t SlipDraw3D_RecordPoolByteSize(void) { return sizeof(((SlipDraw3DRecordPool *)0)->records); }

SlipDraw3DDrawRecord *SlipDraw3D_RecordPoolDrawRecord(SlipDraw3DRecordPool *pool, uint32_t recordOffset) {
	SlipDraw3DLinkedDrawRecord *const record = SlipDraw3D_RecordPoolLinkedRecord(pool, recordOffset);

	if (record == NULL) {
		return NULL;
	}
	return &record->drawRecord;
}

const SlipDraw3DDrawRecord *SlipDraw3D_RecordPoolConstDrawRecord(const SlipDraw3DRecordPool *pool,
                                                                 uint32_t recordOffset) {
	if (pool == NULL || !SlipDraw3D_LinkedRecordOffsetAligned(recordOffset) ||
	    !SlipDraw3D_LinkedRecordOffsetValid(SlipDraw3D_RecordPoolByteSize(), recordOffset)) {
		return NULL;
	}
	return &pool->records[recordOffset / SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE].drawRecord;
}

SlipDraw3DRecordPool *SlipDraw3D_GlobalRecordPool(void) { return &g_drawRecordPool; }

int SlipDraw3D_EnsureRecordPool(void) {
	static int initialised;
	SlipDraw3DRecordPoolInit poolInit;

	if (initialised) {
		return 1;
	}
	if (!SlipDraw3D_InitRecordPool(&g_drawRecordPool, &poolInit)) {
		return 0;
	}
	initialised = 1;
	return 1;
}

int SlipDraw3D_InitRecordPool(SlipDraw3DRecordPool *pool, SlipDraw3DRecordPoolInit *result) {
	uint32_t currentOffset;
	uint32_t nextOffset;
	uint16_t remaining;

	if (pool == NULL || result == NULL) {
		return 0;
	}

	pool->usableRecordCount = SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT;
	pool->freeHeadOffset = 0;
	remaining = pool->usableRecordCount;
	currentOffset = pool->freeHeadOffset;
	do {
		SlipDraw3DLinkedDrawRecord *const currentRecord = SlipDraw3D_RecordPoolLinkedRecord(pool, currentOffset);
		SlipDraw3DLinkedDrawRecord *nextRecord;

		nextOffset = currentOffset + SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE;
		nextRecord = SlipDraw3D_RecordPoolLinkedRecord(pool, nextOffset);
		if (currentRecord == NULL || nextRecord == NULL) {
			return 0;
		}
		currentRecord->links.nextOffset = nextOffset;
		nextRecord->links.prevOffset = currentOffset;
		currentOffset = nextOffset;
		--remaining;
	} while (remaining != 0);
	{
		SlipDraw3DLinkedDrawRecord *const lastRecord = SlipDraw3D_RecordPoolLinkedRecord(pool, currentOffset);
		SlipDraw3DLinkedDrawRecord *const freeHeadRecord =
		    SlipDraw3D_RecordPoolLinkedRecord(pool, pool->freeHeadOffset);

		if (lastRecord == NULL || freeHeadRecord == NULL) {
			return 0;
		}
		lastRecord->links.nextOffset = pool->freeHeadOffset;
		freeHeadRecord->links.prevOffset = currentOffset;
	}
	pool->inputActiveHeadOffset = 0;

	*result = (SlipDraw3DRecordPoolInit){.usableRecordCount = pool->usableRecordCount,
	                                     .allocationBytes = SLIP_DRAW3D_RECORD_POOL_BYTES,
	                                     .freeHeadOffset = pool->freeHeadOffset,
	                                     .firstUsableOffset = SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE,
	                                     .lastRecordOffset =
	                                         SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT * SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE,
	                                     .inputActiveHeadOffset = pool->inputActiveHeadOffset,
	                                     .clearedRejectCarry = true,
	                                     .carryOut = false};
	return 1;
}

int SlipDraw3D_MaterialGate(const uint8_t *materialTable, size_t materialTableBytes, uint16_t materialIndex,
                            uint32_t flags, uint32_t countAndFlags, const uint8_t *indexedRecordBase,
                            size_t indexedRecordBytes, SlipDraw3DMaterialGate *result) {
	const uint8_t *materialRecord;
	uint16_t materialCount;
	size_t materialRecordOffset;
	uint16_t rejectWord;
	uint32_t vertexShading;
	bool flagsBlockedIndexedPath;
	bool hasIndexedVertices;

	if (materialTable == NULL || result == NULL || materialTableBytes < offsetof(SlipDraw3DMaterialTable, records)) {
		return 0;
	}

	memset(result, 0, sizeof(*result));
	materialCount = SlipBytes_ReadLE16(materialTable);
	result->materialCount = materialCount;
	if (materialIndex < materialCount) {
		const uint32_t materialRecordByteOffset = (uint16_t)(SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE * materialIndex);

		materialRecordOffset = (size_t)materialRecordByteOffset + offsetof(SlipDraw3DMaterialTable, records);
		result->selectedIndexedMaterial = true;
		result->materialRecordOffset = materialRecordByteOffset;
	} else {
		materialRecordOffset = offsetof(SlipDraw3DMaterialTable, records);
	}
	if (materialRecordOffset > materialTableBytes ||
	    materialTableBytes - materialRecordOffset <
	        offsetof(SlipDraw3DMaterialRecord, vertexShading) + sizeof(uint32_t)) {
		return 0;
	}
	materialRecord = materialTable + materialRecordOffset;
	result->materialRecord = materialRecord;

	rejectWord = SlipBytes_ReadLE16(materialRecord + offsetof(SlipDraw3DMaterialRecord, skipFlatPolygon));
	result->rejectWord = rejectWord;
	if (rejectWord != 0) {
		result->setRejectCarry = true;
		result->branch = SLIP_DRAW3D_MATERIAL_GATE_BRANCH_REJECT;
		return 1;
	}

	result->storedMaterialRecord = materialRecord;
	vertexShading = SlipBytes_ReadLE32(materialRecord + offsetof(SlipDraw3DMaterialRecord, vertexShading));
	result->vertexShading = vertexShading;
	result->flags = flags;
	flagsBlockedIndexedPath = (flags & SLIP_RENDER_DISABLE_VERTEX_SHADING) != 0;
	result->flagsBlockedIndexedPath = flagsBlockedIndexedPath;
	result->countAndFlags = countAndFlags;
	hasIndexedVertices = (countAndFlags & SLIP_PRIMITIVE_VERTEX_NORMALS) != 0;
	result->hasIndexedShadingFlag = hasIndexedVertices;
	if (vertexShading == 0 || flagsBlockedIndexedPath || !hasIndexedVertices) {
		result->branch = SLIP_DRAW3D_MATERIAL_GATE_BRANCH_REGULAR;
		return 1;
	}

	result->maskedVertexCount = countAndFlags & SLIP_PRIMITIVE_VERTEX_COUNT_MASK;
	materialRecordOffset = (size_t)result->maskedVertexCount * SLIP_SERIALIZED_INDEX_BYTES;
	if (indexedRecordBase == NULL || materialRecordOffset > indexedRecordBytes) {
		return 0;
	}
	result->indexedRecordPointer = indexedRecordBase + materialRecordOffset;
	result->indexedRecordIndex = (uint16_t)result->maskedVertexCount;
	result->mode = SLIP_INTERPOLATE_SHADE;
	result->drawMode = SLIP_POLYGON_DRAW_SHADED;
	result->branch = SLIP_DRAW3D_MATERIAL_GATE_BRANCH_INDEXED;
	return 1;
}

int SlipDraw3D_LoadMaterialFrameSlots(uint8_t *materialTable, size_t materialTableBytes,
                                      SlipDraw3DResourceFindNameRecord findNameRecord, void *findNameRecordUser,
                                      SlipDraw3DMaterialFrameSlots *result) {
	uint32_t materialCount;
	uint32_t materialIndex;

	if (result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->returned = true;
	if (materialTable == NULL) {
		result->nullTable = true;
		return 1;
	}
	if (materialTableBytes < offsetof(SlipDraw3DMaterialTable, records) || findNameRecord == NULL) {
		return 0;
	}
	SlipDraw3DMaterialTable *const materials = (void *)materialTable;
	materialCount = materials->count;
	result->materialTableCount = materialCount;
	if (materialCount >
	    (materialTableBytes - offsetof(SlipDraw3DMaterialTable, records)) / SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE) {
		return 0;
	}

	for (materialIndex = 0; materialIndex < materialCount; ++materialIndex) {
		SlipDraw3DMaterialRecord *const material = &materials->records[materialIndex];
		char textureName[SLIP_MAT_TEXTURE_NAME_BYTES + 1];
		size_t nameCursor;
		size_t starOffset;
		bool foundStar = false;

		for (unsigned frame = 0; frame < SLIP_DRAW3D_MATERIAL_FRAME_COUNT; ++frame)
			material->textureHandles[frame] = 0;
		++result->recordsVisited;
		if (material->textureName[0] == 0) {
			continue;
		}
		++result->recordsWithTextureName;
		memset(textureName, 0, sizeof(textureName));
		memcpy(textureName, material->textureName, sizeof(material->textureName));

		starOffset = 0;
		for (nameCursor = 0; nameCursor < sizeof(textureName); ++nameCursor) {
			if (textureName[nameCursor] == '\0') {
				break;
			}
			if (textureName[nameCursor] == '*') {
				starOffset = nameCursor;
				foundStar = true;
				break;
			}
		}

		if (!foundStar) {
			uint32_t resourceHandle = 0;

			++result->resourceLookups;
			if (findNameRecord(findNameRecordUser, textureName, &resourceHandle)) {
				size_t slot;

				++result->resourceHits;
				for (slot = 0; slot < SLIP_DRAW3D_MATERIAL_FRAME_COUNT; ++slot) {
					material->textureHandles[slot] = resourceHandle;
					++result->slotWrites;
				}
			}
			continue;
		}

		++result->recordsWithWildcard;
		if (textureName[starOffset + 1u] != '\0' && textureName[starOffset + 1u] != '.') {
			continue;
		}

		for (uint32_t digitIndex = 0; digitIndex < SLIP_DRAW3D_MATERIAL_FRAME_COUNT; ++digitIndex) {
			uint32_t resourceHandle = 0;

			textureName[starOffset] = (char)('0' + digitIndex);
			++result->resourceLookups;
			if (!findNameRecord(findNameRecordUser, textureName, &resourceHandle)) {
				continue;
			}
			++result->resourceHits;
			for (uint32_t slot = digitIndex; slot < SLIP_DRAW3D_MATERIAL_FRAME_COUNT; ++slot) {
				material->textureHandles[slot] = resourceHandle;
				++result->slotWrites;
			}
		}

		for (size_t i = 0; i < SLIP_MAT_TEXTURE_SUFFIX_BYTES; ++i) {
			textureName[starOffset + i] = textureName[starOffset + i + 1u];
		}
		textureName[starOffset + SLIP_MAT_TEXTURE_SUFFIX_BYTES] = '\0';
		{
			uint32_t resourceHandle = 0;

			++result->resourceLookups;
			if (findNameRecord(findNameRecordUser, textureName, &resourceHandle)) {
				++result->resourceHits;
				for (size_t slot = 0; slot < SLIP_DRAW3D_MATERIAL_FRAME_COUNT; ++slot) {
					uint32_t *const slotHandle = &material->textureHandles[slot];

					if (*slotHandle != 0) {
						continue;
					}
					*slotHandle = resourceHandle;
					++result->slotWrites;
				}
			}
		}
	}
	return 1;
}

int SlipDraw3D_TexturedEmitGate(const uint8_t *materialTable, size_t materialTableBytes,
                                const SlipDraw3DStateRecord *drawStateRecord, uint32_t renderFlags,
                                uint32_t countAndFlags, uint16_t normalX, uint16_t normalY, uint16_t normalZ,
                                uint16_t materialIndex, uint32_t frameIndex, SlipDraw3DTexturedEmitGate *result) {
	uint16_t materialCount;
	size_t materialRecordOffset;
	size_t frameSlotOffset;

	if (result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->savedRenderFlags = renderFlags;
	result->savedCountAndFlags = countAndFlags;
	result->normalX = normalX;
	result->normalY = normalY;
	result->normalZ = normalZ;
	result->flagsBlockTexturedBranch = (renderFlags & SLIP_RENDER_SOLID_TEXTURE_FALLBACK) != 0;
	result->lacksTextureFlag = (countAndFlags & SLIP_PRIMITIVE_TEXTURE_COORDINATES) == 0;
	if (result->flagsBlockTexturedBranch || result->lacksTextureFlag) {
		result->fallback = true;
		return 1;
	}
	if (materialTable == NULL || materialTableBytes < offsetof(SlipDraw3DMaterialTable, records)) {
		return 0;
	}
	materialCount = SlipBytes_ReadLE16(materialTable);
	result->materialCount = materialCount;
	if (materialIndex < materialCount) {
		const uint32_t materialRecordByteOffset = (uint16_t)(SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE * materialIndex);

		materialRecordOffset = (size_t)materialRecordByteOffset + offsetof(SlipDraw3DMaterialTable, records);
		result->selectedIndexedMaterial = true;
		result->materialRecordOffset = materialRecordByteOffset;
	} else {
		materialRecordOffset = offsetof(SlipDraw3DMaterialTable, records);
	}
	result->frameIndex = frameIndex;
	frameSlotOffset = materialRecordOffset + offsetof(SlipDraw3DMaterialRecord, textureHandles) +
	                  frameIndex * SLIP_DRAW3D_TEXTURE_HANDLE_BYTES;
	result->frameSlotOffset = (uint32_t)frameSlotOffset;
	if (frameSlotOffset > materialTableBytes || materialTableBytes - frameSlotOffset < sizeof(uint32_t) ||
	    materialRecordOffset > materialTableBytes ||
	    materialTableBytes - materialRecordOffset <
	        offsetof(SlipDraw3DMaterialRecord, textureTransparency) + sizeof(int16_t)) {
		return 0;
	}
	result->textureHandle = SlipBytes_ReadLE32(materialTable + frameSlotOffset);
	if (result->textureHandle == 0) {
		result->fallback = true;
		return 1;
	}
	if (drawStateRecord == NULL) {
		return 0;
	}
	result->calledBuildTexturedRing = true;
	result->renderFlagsAfter = renderFlags;
	if ((result->renderFlagsAfter & SLIP_RENDER_ALTERNATE_TEXTURE_RASTER) == 0u) {
		uint16_t roundedNormalDepthX;
		uint16_t roundedNormalDepthXY;

		result->normalDepthX = SlipDraw3D_MultiplySignedWordsShift14WithRoundingBit(
		    normalX, (uint16_t)drawStateRecord->matrix.m[2], &result->normalDepthXRoundingCarry);
		roundedNormalDepthX = (uint16_t)(result->normalDepthX + (result->normalDepthXRoundingCarry ? 1u : 0u));
		result->normalDepthY = SlipDraw3D_MultiplySignedWordsShift14WithRoundingBit(
		    normalY, (uint16_t)drawStateRecord->matrix.m[5], &result->normalDepthYRoundingCarry);
		roundedNormalDepthXY =
		    (uint16_t)(roundedNormalDepthX + result->normalDepthY + (result->normalDepthYRoundingCarry ? 1u : 0u));
		result->normalDepthZ = SlipDraw3D_MultiplySignedWordsShift14WithRoundingBit(
		    normalZ, (uint16_t)drawStateRecord->matrix.m[8], &result->normalDepthZRoundingCarry);
		result->normalDepth =
		    (uint16_t)(result->normalDepthZ + roundedNormalDepthXY + (result->normalDepthZRoundingCarry ? 1u : 0u));
		if ((int16_t)result->normalDepth <= SLIP_TEXTURE_AFFINE_FACING_THRESHOLD) {
			result->renderFlagsAfter |= SLIP_RENDER_ALTERNATE_TEXTURE_RASTER;
		}
	}
	result->transparentWord = SlipBytes_ReadLE16(materialTable + materialRecordOffset +
	                                             offsetof(SlipDraw3DMaterialRecord, textureTransparency));
	result->renderFlagsForDispatch = result->renderFlagsAfter;
	if ((result->renderFlagsForDispatch & SLIP_RENDER_MASKED_TEXTURE) == 0u) {
		result->renderFlagsForDispatch &= ~SLIP_RENDER_MASKED_TEXTURE;
		if (result->transparentWord == 0u) {
			result->renderFlagsForDispatch |= SLIP_RENDER_MASKED_TEXTURE;
		}
	}
	result->storedMaterialRecord = materialTable + materialRecordOffset;
	result->calledTexturedDispatch = true;
	return 1;
}

int SlipDraw3D_GetMaterialNumber(const uint8_t *materialTable, size_t materialTableBytes, uint16_t materialGlobal,
                                 const uint8_t *sourceName, size_t sourceNameBytes, SlipDraw3DMaterialNumber *result) {
	uint16_t remainingKeyBytes;
	size_t sourceOffset;
	uint16_t keyOffset;
	uint32_t materialCount;
	uint16_t loopCount;
	uint16_t materialIndex;

	if (result == NULL || sourceName == NULL) {
		return 0;
	}

	memset(result, 0, sizeof(*result));
	result->materialGlobal = materialGlobal;
	if (materialGlobal == 0) {
		result->jumpNoMaterials = true;
		result->carryOut = true;
		return 1;
	}
	if (materialTable == NULL || materialTableBytes < offsetof(SlipDraw3DMaterialTable, records)) {
		return 0;
	}

	remainingKeyBytes = SLIP_DRAW3D_MATERIAL_KEY_BYTES;
	sourceOffset = 0;
	keyOffset = 0;
	while (remainingKeyBytes != 0) {
		uint8_t normalizedCharacter;

		if (sourceOffset >= sourceNameBytes) {
			return 0;
		}
		normalizedCharacter = SlipDraw3D_UppercaseAscii(sourceName[sourceOffset]);
		++sourceOffset;
		if (normalizedCharacter == 0) {
			result->sourceTerminator = true;
			break;
		}
		result->normalizedKey[keyOffset] = normalizedCharacter;
		++keyOffset;
		--remainingKeyBytes;
	}
	result->keyBytesCopied = keyOffset;
	while (remainingKeyBytes != 0) {
		result->normalizedKey[keyOffset] = (uint8_t)' ';
		++keyOffset;
		--remainingKeyBytes;
		++result->keyBytesFilled;
	}

	materialCount = SlipBytes_ReadLE32(materialTable);
	loopCount = (uint16_t)materialCount;
	result->materialTableCount = materialCount;
	if (loopCount == 0) {
		return 0;
	}

	materialIndex = 0;
	while (loopCount != 0) {
		const size_t materialRecordOffset = offsetof(SlipDraw3DMaterialTable, records) +
		                                    (size_t)materialIndex * SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE;

		if (materialRecordOffset > materialTableBytes ||
		    materialTableBytes - materialRecordOffset < SLIP_DRAW3D_MATERIAL_KEY_BYTES) {
			return 0;
		}
		++result->recordsCompared;
		if (memcmp(result->normalizedKey, materialTable + materialRecordOffset, SLIP_DRAW3D_MATERIAL_KEY_BYTES) == 0) {
			result->materialIndex = materialIndex;
			result->materialRecordOffset = (uint16_t)(materialIndex * SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE);
			result->clearedRejectCarry = true;
			result->carryOut = false;
			return 1;
		}
		++materialIndex;
		--loopCount;
	}

	result->materialIndex = materialIndex;
	result->materialRecordOffset = (uint16_t)(materialIndex * SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE);
	result->setRejectCarry = true;
	result->carryOut = true;
	return 1;
}

const uint8_t *SlipDraw3D_GetMaterialName(const uint8_t *table, size_t tableBytes, uint16_t materialHandle) {
	const uint16_t index = materialHandle & SLIP_DRAW3D_MATERIAL_INDEX_MASK;
	size_t offset;
	if (table == NULL || tableBytes < offsetof(SlipDraw3DMaterialTable, records))
		return NULL;
	offset = offsetof(SlipDraw3DMaterialTable, records) +
	         (index < SlipBytes_ReadLE16(table) ? (uint16_t)(index * SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE) : 0u);
	if (offset > tableBytes || tableBytes - offset < sizeof(uint32_t))
		return NULL;
	return table + offset;
}

bool SlipDraw3D_GetMaterialValues(const uint8_t *materialTable, size_t materialTableBytes, uint16_t materialIndex,
                                  uint32_t *materialColor, uint32_t *materialControl) {
	size_t recordOffset;
	uint16_t materialCount;

	if (materialTable == NULL || materialTableBytes < offsetof(SlipDraw3DMaterialTable, records) ||
	    materialColor == NULL || materialControl == NULL) {
		return false;
	}
	materialIndex &= SLIP_DRAW3D_MATERIAL_INDEX_MASK;
	materialCount = SlipBytes_ReadLE16(materialTable);
	recordOffset = offsetof(SlipDraw3DMaterialTable, records);
	if (materialIndex < materialCount) {
		recordOffset += (uint16_t)(SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE * materialIndex);
	}
	if (recordOffset > materialTableBytes ||
	    materialTableBytes - recordOffset < offsetof(SlipDraw3DMaterialRecord, rampEnd) + sizeof(uint32_t)) {
		return false;
	}
	*materialColor = SlipBytes_ReadLE32(materialTable + recordOffset + offsetof(SlipDraw3DMaterialRecord, rampStart));
	*materialControl = SlipBytes_ReadLE32(materialTable + recordOffset + offsetof(SlipDraw3DMaterialRecord, rampEnd));
	return true;
}

int SlipDraw3D_PopFreeRecord(uint8_t *drawRecordPool, size_t recordBytes, uint32_t freeHeadOffset,
                             SlipDraw3DFreeRecordPop *result) {
	uint32_t poppedRecordOffset;
	uint32_t nextFreeOffset;

	if (drawRecordPool == NULL || result == NULL || !SlipDraw3D_LinkedRecordOffsetValid(recordBytes, freeHeadOffset)) {
		return 0;
	}
	poppedRecordOffset = SlipDraw3D_RecordNext(drawRecordPool, freeHeadOffset);
	if (!SlipDraw3D_LinkedRecordOffsetValid(recordBytes, poppedRecordOffset)) {
		return 0;
	}
	nextFreeOffset = SlipDraw3D_RecordNext(drawRecordPool, poppedRecordOffset);
	if (!SlipDraw3D_LinkedRecordOffsetValid(recordBytes, nextFreeOffset)) {
		return 0;
	}

	SlipDraw3D_SetRecordNext(drawRecordPool, freeHeadOffset, nextFreeOffset);
	SlipDraw3D_SetRecordPrev(drawRecordPool, nextFreeOffset, freeHeadOffset);
	*result = (SlipDraw3DFreeRecordPop){.freeHeadOffset = freeHeadOffset,
	                                    .poppedRecordOffset = poppedRecordOffset,
	                                    .nextFreeOffset = nextFreeOffset,
	                                    .freeHeadNextAfter = nextFreeOffset,
	                                    .nextFreePrevAfter = freeHeadOffset};
	return 1;
}

int SlipDraw3D_CopyIndexedRecord(SlipDraw3DDrawRecord *drawRecord, const uint8_t *vertexRecordBase,
                                 size_t vertexRecordBytes, const uint8_t *indexStream, size_t indexStreamBytes,
                                 uint32_t indexStreamOffset, uint32_t allClipFlagsIn, uint32_t anyClipFlagsIn,
                                 uint32_t projectedVertexFlags, SlipDraw3DIndexedRecordCopy *result) {
	uint16_t indexWord;
	uint32_t vertexRecordOffset;
	const uint8_t *vertexRecord;

	if (drawRecord == NULL || vertexRecordBase == NULL || indexStream == NULL || result == NULL ||
	    (size_t)indexStreamOffset > indexStreamBytes ||
	    indexStreamBytes - (size_t)indexStreamOffset < SLIP_SERIALIZED_INDEX_BYTES) {
		return 0;
	}
	indexWord = SlipBytes_ReadLE16(indexStream + indexStreamOffset);
	vertexRecordOffset = (uint32_t)indexWord * SLIP_DRAW3D_VERTEX_RECORD_SIZE;
	if ((size_t)vertexRecordOffset > vertexRecordBytes ||
	    vertexRecordBytes - (size_t)vertexRecordOffset < SLIP_DRAW3D_VERTEX_DRAW_PREFIX_BYTES) {
		return 0;
	}

	vertexRecord = vertexRecordBase + vertexRecordOffset;

	const SlipDraw3DVertexRecord *const source = (const void *)vertexRecord;
	drawRecord->world = source->world;
	drawRecord->screenX = source->screenX;
	drawRecord->screenY = source->screenY;
	drawRecord->flags = source->flags;
	drawRecord->depth = source->depth;
	*result = (SlipDraw3DIndexedRecordCopy){.indexWord = indexWord,
	                                        .vertexRecordOffset = vertexRecordOffset,
	                                        .vertexRecordPointer = vertexRecord,
	                                        .callProjectVertex = true,
	                                        .flagsFromProjectVertex = projectedVertexFlags,
	                                        .copiedDwords = SLIP_DRAW3D_VERTEX_DRAW_PREFIX_DWORDS,
	                                        .allClipFlagsAfter = allClipFlagsIn & projectedVertexFlags,
	                                        .anyClipFlagsAfter = anyClipFlagsIn | projectedVertexFlags};
	return 1;
}

int SlipDraw3D_InitSolidLoop(uint32_t firstRecordOffset, SlipDraw3DSolidLoopInit *result) {
	if (result == NULL) {
		return 0;
	}
	*result = (SlipDraw3DSolidLoopInit){
	    .firstRecordOffset = firstRecordOffset, .allClipFlagsInitial = SLIP_CLIP_ALL, .anyClipFlagsInitial = 0};
	return 1;
}

int SlipDraw3D_CopySolidRecord(SlipDraw3DDrawRecord *drawRecord, const uint8_t *vertexRecordBase,
                               size_t vertexRecordBytes, const uint8_t *indexStream, size_t indexStreamBytes,
                               uint32_t indexStreamOffset, uint32_t remainingVertices, uint32_t allClipFlagsIn,
                               uint32_t anyClipFlagsIn, uint32_t projectedVertexFlags,
                               SlipDraw3DSolidRecordCopy *result) {
	const uint8_t *vertexRecord;
	uint16_t indexWord;
	uint32_t vertexRecordOffset;
	uint32_t loopCountAfterDec;
	unsigned i;

	if (drawRecord == NULL || vertexRecordBase == NULL || indexStream == NULL || result == NULL ||
	    (size_t)indexStreamOffset > indexStreamBytes ||
	    indexStreamBytes - (size_t)indexStreamOffset < SLIP_SERIALIZED_INDEX_BYTES || remainingVertices == 0) {
		return 0;
	}
	indexWord = SlipBytes_ReadLE16(indexStream + indexStreamOffset);
	vertexRecordOffset = (uint32_t)indexWord * SLIP_DRAW3D_VERTEX_RECORD_SIZE;
	if ((size_t)vertexRecordOffset > vertexRecordBytes ||
	    vertexRecordBytes - (size_t)vertexRecordOffset < SLIP_DRAW3D_VERTEX_DRAW_PREFIX_BYTES) {
		return 0;
	}
	vertexRecord = vertexRecordBase + vertexRecordOffset;
	for (i = 0; i < SLIP_DRAW3D_VERTEX_DRAW_PREFIX_DWORDS; ++i) {
		SlipDraw3D_WriteLE32(drawRecord->bytes + i * sizeof(uint32_t),
		                     SlipBytes_ReadLE32(vertexRecord + i * sizeof(uint32_t)));
	}
	loopCountAfterDec = remainingVertices - 1u;
	*result = (SlipDraw3DSolidRecordCopy){.loopCountEntry = remainingVertices,
	                                      .indexWord = indexWord,
	                                      .vertexRecordOffset = vertexRecordOffset,
	                                      .vertexRecordPointer = vertexRecord,
	                                      .callProjectVertex = true,
	                                      .flagsFromProjectVertex = projectedVertexFlags,
	                                      .copiedDwords = SLIP_DRAW3D_VERTEX_DRAW_PREFIX_DWORDS,
	                                      .allClipFlagsAfter = allClipFlagsIn & projectedVertexFlags,
	                                      .anyClipFlagsAfter = anyClipFlagsIn | projectedVertexFlags,
	                                      .loopCountAfterDec = loopCountAfterDec,
	                                      .branchToClose = loopCountAfterDec == 0};
	return 1;
}

int SlipDraw3D_StoreMaterialBytes(SlipDraw3DDrawRecord *drawRecord, const uint8_t *materialInputStream,
                                  size_t materialInputBytes, uint32_t materialInputOffset,
                                  const uint8_t *materialRecord, uint16_t returnedMaterialColor,
                                  SlipDraw3DMaterialBytes *result) {
	const uint8_t *materialInput;
	uint16_t normalX;
	uint16_t normalY;
	uint16_t normalZ;
	uint8_t shadeHighByte;
	uint8_t shadeLowByte;

	if (drawRecord == NULL || materialInputStream == NULL || result == NULL ||
	    (size_t)materialInputOffset > materialInputBytes ||
	    materialInputBytes - (size_t)materialInputOffset < SLIP_SERIALIZED_NORMAL_BYTES) {
		return 0;
	}
	materialInput = materialInputStream + materialInputOffset;
	normalZ = SlipBytes_ReadLE16(materialInput + SLIP_SERIALIZED_NORMAL_Z_OFFSET);
	normalY = SlipBytes_ReadLE16(materialInput + SLIP_SERIALIZED_NORMAL_Y_OFFSET);
	normalX = SlipBytes_ReadLE16(materialInput + SLIP_SERIALIZED_NORMAL_X_OFFSET);
	shadeHighByte = (uint8_t)(returnedMaterialColor & UINT8_MAX);
	shadeLowByte = (uint8_t)(returnedMaterialColor >> 8);
	drawRecord->shade = (uint16_t)(((uint16_t)shadeHighByte << 8) | shadeLowByte);
	*result = (SlipDraw3DMaterialBytes){.materialInputPointer = materialInput,
	                                    .normalX = normalX,
	                                    .normalY = normalY,
	                                    .normalZ = normalZ,
	                                    .materialRecord = materialRecord,
	                                    .callMaterialColor = true,
	                                    .materialColor = returnedMaterialColor,
	                                    .shadeHighByte = shadeHighByte,
	                                    .shadeLowByte = shadeLowByte};
	return 1;
}

int SlipDraw3D_RegularSetup(const uint8_t *materialRecord, size_t materialRecordBytes, uint32_t countAndFlags,
                            uint32_t materialColor, SlipDraw3DRegularSetup *result) {
	uint32_t drawMode;
	uint32_t materialDitherBits;
	uint32_t maskedIndex;

	if (materialRecord == NULL || result == NULL ||
	    materialRecordBytes < offsetof(SlipDraw3DMaterialRecord, ditherBits) + sizeof(uint32_t)) {
		return 0;
	}
	drawMode = SLIP_POLYGON_DRAW_FLAT;
	materialDitherBits = SlipBytes_ReadLE32(materialRecord + offsetof(SlipDraw3DMaterialRecord, ditherBits));
	if (materialDitherBits != 0u) {
		drawMode = SLIP_POLYGON_DRAW_DITHERED;
	}
	maskedIndex = countAndFlags & SLIP_PRIMITIVE_VERTEX_COUNT_MASK;
	*result = (SlipDraw3DRegularSetup){.materialRecord = materialRecord,
	                                   .drawModeInitial = SLIP_POLYGON_DRAW_FLAT,
	                                   .materialDitherBits = materialDitherBits,
	                                   .flatBranch = materialDitherBits == 0u,
	                                   .drawMode = drawMode,
	                                   .countAndFlags = countAndFlags,
	                                   .maskedIndex = maskedIndex,
	                                   .callMaterialColor = true,
	                                   .materialColor = materialColor,
	                                   .storedCountAndFlags = countAndFlags,
	                                   .storedMaterialColor = materialColor,
	                                   .mode = 0};
	return 1;
}

int SlipDraw3D_AllocateFirstActiveRecord(uint8_t *drawRecordPool, size_t recordBytes, uint32_t freeHeadOffset,
                                         SlipDraw3DFirstActiveRecord *result) {
	SlipDraw3DFreeRecordPop pop;

	if (result == NULL) {
		return 0;
	}
	if (!SlipDraw3D_PopFreeRecord(drawRecordPool, recordBytes, freeHeadOffset, &pop)) {
		return 0;
	}
	*result = (SlipDraw3DFirstActiveRecord){.pop = pop, .inputActiveHeadOffset = pop.poppedRecordOffset};
	return 1;
}

int SlipDraw3D_AppendSolidRecord(uint8_t *drawRecordPool, size_t recordBytes, uint32_t freeHeadOffset,
                                 uint32_t previousRecordOffset, uint32_t indexStreamOffset,
                                 SlipDraw3DAppendSolidRecord *result) {
	SlipDraw3DFreeRecordPop pop;
	uint32_t appendedRecordOffset;

	if (drawRecordPool == NULL || result == NULL ||
	    !SlipDraw3D_LinkedRecordOffsetValid(recordBytes, previousRecordOffset)) {
		return 0;
	}
	if (!SlipDraw3D_PopFreeRecord(drawRecordPool, recordBytes, freeHeadOffset, &pop)) {
		return 0;
	}
	appendedRecordOffset = pop.poppedRecordOffset;
	SlipDraw3D_SetRecordNext(drawRecordPool, previousRecordOffset, appendedRecordOffset);
	SlipDraw3D_SetRecordPrev(drawRecordPool, appendedRecordOffset, previousRecordOffset);
	*result = (SlipDraw3DAppendSolidRecord){.previousRecordOffset = previousRecordOffset,
	                                        .pop = pop,
	                                        .appendedRecordOffset = appendedRecordOffset,
	                                        .previousNextAfter = appendedRecordOffset,
	                                        .appendedPrevAfter = previousRecordOffset,
	                                        .indexStreamOffsetAfter = indexStreamOffset + SLIP_SERIALIZED_INDEX_BYTES,
	                                        .jumpToLoop = true};
	return 1;
}

int SlipDraw3D_AppendRecord(uint8_t *drawRecordPool, size_t recordBytes, uint32_t freeHeadOffset,
                            uint32_t previousRecordOffset, uint32_t indexStreamOffset, uint32_t materialInputOffset,
                            SlipDraw3DAppendRecord *result) {
	SlipDraw3DFreeRecordPop pop;
	uint32_t appendedRecordOffset;

	if (drawRecordPool == NULL || result == NULL ||
	    !SlipDraw3D_LinkedRecordOffsetValid(recordBytes, previousRecordOffset)) {
		return 0;
	}
	if (!SlipDraw3D_PopFreeRecord(drawRecordPool, recordBytes, freeHeadOffset, &pop)) {
		return 0;
	}
	appendedRecordOffset = pop.poppedRecordOffset;
	SlipDraw3D_SetRecordNext(drawRecordPool, previousRecordOffset, appendedRecordOffset);
	SlipDraw3D_SetRecordPrev(drawRecordPool, appendedRecordOffset, previousRecordOffset);
	*result = (SlipDraw3DAppendRecord){.previousRecordOffset = previousRecordOffset,
	                                   .pop = pop,
	                                   .appendedRecordOffset = appendedRecordOffset,
	                                   .previousNextAfter = appendedRecordOffset,
	                                   .appendedPrevAfter = previousRecordOffset,
	                                   .indexStreamOffsetAfter = indexStreamOffset + SLIP_SERIALIZED_INDEX_BYTES,
	                                   .materialInputOffsetAfter = materialInputOffset + SLIP_SERIALIZED_NORMAL_BYTES};
	return 1;
}

int SlipDraw3D_CloseRecordRing(uint8_t *drawRecordPool, size_t recordBytes, uint32_t lastRecordOffset,
                               uint32_t firstRecordOffset, SlipDraw3DCloseRecordRing *result) {
	if (drawRecordPool == NULL || result == NULL ||
	    !SlipDraw3D_LinkedRecordOffsetValid(recordBytes, lastRecordOffset) ||
	    !SlipDraw3D_LinkedRecordOffsetValid(recordBytes, firstRecordOffset)) {
		return 0;
	}
	SlipDraw3D_SetRecordNext(drawRecordPool, lastRecordOffset, firstRecordOffset);
	SlipDraw3D_SetRecordPrev(drawRecordPool, firstRecordOffset, lastRecordOffset);
	*result = (SlipDraw3DCloseRecordRing){.lastRecordOffset = lastRecordOffset,
	                                      .firstRecordOffset = firstRecordOffset,
	                                      .lastNextAfter = firstRecordOffset,
	                                      .firstPrevAfter = lastRecordOffset};
	return 1;
}

int SlipDraw3D_ReturnActiveRing(SlipDraw3DRecordPool *pool, SlipDraw3DReturnActiveVisit *visits, size_t visitCapacity,
                                SlipDraw3DReturnActiveRing *result) {
	uint8_t *recordPoolBytes;
	size_t recordPoolBytesCount;
	uint32_t currentRecordOffset;
	size_t visitCount;

	if (pool == NULL || result == NULL) {
		return 0;
	}
	recordPoolBytes = SlipDraw3D_RecordPoolBytes(pool);
	recordPoolBytesCount = SlipDraw3D_RecordPoolByteSize();
	if (recordPoolBytes == NULL) {
		return 0;
	}
	currentRecordOffset = pool->inputActiveHeadOffset;
	*result = (SlipDraw3DReturnActiveRing){
	    .activeHeadEntry = currentRecordOffset, .activeHeadZero = currentRecordOffset == 0, .returned = true};
	if (currentRecordOffset == 0) {
		result->activeHeadAfter = pool->inputActiveHeadOffset;
		return 1;
	}
	if (visits == NULL || visitCapacity == 0) {
		return 0;
	}
	visitCount = 0;
	for (;;) {
		SlipDraw3DReturnActiveVisit *visit;
		uint32_t savedNextOffset;
		uint32_t nextOffset;
		uint32_t previousOffset;
		uint32_t freeHeadOffset;
		uint32_t freeFirstOffset;
		bool loop;

		if (visitCount >= visitCapacity ||
		    !SlipDraw3D_LinkedRecordOffsetValid(recordPoolBytesCount, currentRecordOffset)) {
			return 0;
		}
		savedNextOffset = SlipDraw3D_RecordNext(recordPoolBytes, currentRecordOffset);
		nextOffset = SlipDraw3D_RecordNext(recordPoolBytes, currentRecordOffset);
		previousOffset = SlipDraw3D_RecordPrev(recordPoolBytes, currentRecordOffset);
		freeHeadOffset = pool->freeHeadOffset;
		if (!SlipDraw3D_LinkedRecordOffsetValid(recordPoolBytesCount, savedNextOffset) ||
		    !SlipDraw3D_LinkedRecordOffsetValid(recordPoolBytesCount, nextOffset) ||
		    !SlipDraw3D_LinkedRecordOffsetValid(recordPoolBytesCount, previousOffset) ||
		    !SlipDraw3D_LinkedRecordOffsetValid(recordPoolBytesCount, freeHeadOffset)) {
			return 0;
		}
		SlipDraw3D_SetRecordNext(recordPoolBytes, previousOffset, nextOffset);
		SlipDraw3D_SetRecordPrev(recordPoolBytes, nextOffset, previousOffset);
		freeFirstOffset = SlipDraw3D_RecordNext(recordPoolBytes, freeHeadOffset);
		if (!SlipDraw3D_LinkedRecordOffsetValid(recordPoolBytesCount, freeFirstOffset)) {
			return 0;
		}
		SlipDraw3D_SetRecordNext(recordPoolBytes, freeHeadOffset, currentRecordOffset);
		SlipDraw3D_SetRecordPrev(recordPoolBytes, freeFirstOffset, currentRecordOffset);
		SlipDraw3D_SetRecordNext(recordPoolBytes, currentRecordOffset, freeFirstOffset);
		SlipDraw3D_SetRecordPrev(recordPoolBytes, currentRecordOffset, freeHeadOffset);
		loop = savedNextOffset != currentRecordOffset;
		visit = visits + visitCount;
		*visit = (SlipDraw3DReturnActiveVisit){.currentRecordOffset = currentRecordOffset,
		                                       .savedNextOffset = savedNextOffset,
		                                       .previousOffset = previousOffset,
		                                       .nextAfterUnlink = nextOffset,
		                                       .prevNextAfterUnlink = nextOffset,
		                                       .nextPrevAfterUnlink = previousOffset,
		                                       .freeHeadOffset = freeHeadOffset,
		                                       .freeFirstOffset = freeFirstOffset,
		                                       .freeHeadNextAfter = currentRecordOffset,
		                                       .freeFirstPrevAfter = currentRecordOffset,
		                                       .currentNextAfter = freeFirstOffset,
		                                       .currentPrevAfter = freeHeadOffset,
		                                       .nextCurrentAfterXchg = savedNextOffset,
		                                       .loop = loop};
		++visitCount;
		if (!loop) {
			break;
		}
		currentRecordOffset = savedNextOffset;
	}
	pool->inputActiveHeadOffset = 0;
	result->visitCount = visitCount;
	result->activeHeadAfter = 0;
	return 1;
}

int SlipDraw3D_PointPointerRing(SlipDraw3DRecordPool *pool, const uint8_t *pointPointerTable,
                                size_t pointPointerTableBytes, uint16_t countAndFlags, uint32_t materialColor,
                                const uint32_t *flagsFromByVisit, size_t flagCount, int depthClipRejected,
                                int screenClipRejected, SlipDraw3DPointPointerRingVisit *visits, size_t visitCapacity,
                                SlipDraw3DPointPointerRing *result) {
	uint8_t *recordPoolBytes;
	size_t recordPoolBytesCount;
	uint32_t pointCount;
	uint32_t loopCount;
	uint32_t pointPointerTableOffset;
	uint32_t allClipFlags;
	uint32_t anyClipFlags;
	uint32_t firstRecordOffset;
	uint32_t currentRecordOffset;
	size_t visitCount;
	SlipDraw3DFreeRecordPop firstPop;

	if (pool == NULL || pointPointerTable == NULL || flagsFromByVisit == NULL || visits == NULL || result == NULL) {
		return 0;
	}
	pointCount = (uint32_t)countAndFlags;
	if (pointCount == 0 || pointCount > flagCount || pointCount > visitCapacity ||
	    pointPointerTableBytes < pointCount * SLIP_DRAW3D_POINT_POINTER_BYTES) {
		return 0;
	}
	recordPoolBytes = SlipDraw3D_RecordPoolBytes(pool);
	recordPoolBytesCount = SlipDraw3D_RecordPoolByteSize();
	if (recordPoolBytes == NULL ||
	    !SlipDraw3D_PopFreeRecord(recordPoolBytes, recordPoolBytesCount, pool->freeHeadOffset, &firstPop)) {
		return 0;
	}
	firstRecordOffset = firstPop.poppedRecordOffset;
	currentRecordOffset = firstRecordOffset;
	pool->inputActiveHeadOffset = firstRecordOffset;
	allClipFlags = SLIP_DRAW3D_CLIP_MASK;
	anyClipFlags = 0;
	loopCount = pointCount;
	pointPointerTableOffset = 0;
	visitCount = 0;
	while (loopCount != 0u) {
		SlipDraw3DPointPointerRingVisit *visit;
		uint32_t pointPointerToken;
		uint32_t flagsFrom;
		uint32_t loopCountAfterDec;
		bool appendNextRecord;
		SlipDraw3DFreeRecordPop appendPop = {0};
		uint32_t appendedRecordOffset = 0;
		uint32_t pointPointerTableOffsetAfter = pointPointerTableOffset;

		pointPointerToken = SlipBytes_ReadLE32(pointPointerTable + pointPointerTableOffset);
		flagsFrom = flagsFromByVisit[visitCount];
		allClipFlags &= flagsFrom;
		anyClipFlags |= flagsFrom;
		loopCountAfterDec = loopCount - 1u;
		appendNextRecord = loopCountAfterDec != 0u;
		if (appendNextRecord) {
			if (!SlipDraw3D_PopFreeRecord(recordPoolBytes, recordPoolBytesCount, pool->freeHeadOffset, &appendPop)) {
				return 0;
			}
			appendedRecordOffset = appendPop.poppedRecordOffset;
			SlipDraw3D_SetRecordNext(recordPoolBytes, currentRecordOffset, appendedRecordOffset);
			SlipDraw3D_SetRecordPrev(recordPoolBytes, appendedRecordOffset, currentRecordOffset);
			pointPointerTableOffsetAfter = pointPointerTableOffset + SLIP_DRAW3D_POINT_POINTER_BYTES;
		}
		visit = visits + visitCount;
		*visit = (SlipDraw3DPointPointerRingVisit){loopCount,
		                                           pointPointerToken,
		                                           true,
		                                           flagsFrom,
		                                           allClipFlags,
		                                           anyClipFlags,
		                                           loopCountAfterDec,
		                                           appendNextRecord,
		                                           appendPop,
		                                           appendNextRecord ? currentRecordOffset : 0,
		                                           pointPointerTableOffsetAfter,
		                                           false,
		                                           0,
		                                           0,
		                                           0};
		if (appendNextRecord) {
			currentRecordOffset = appendedRecordOffset;
		}
		pointPointerTableOffset = pointPointerTableOffsetAfter;
		loopCount = loopCountAfterDec;
		++visitCount;
	}
	SlipDraw3D_SetRecordNext(recordPoolBytes, currentRecordOffset, firstRecordOffset);
	SlipDraw3D_SetRecordPrev(recordPoolBytes, firstRecordOffset, currentRecordOffset);

	*result = (SlipDraw3DPointPointerRing){countAndFlags,
	                                       pointCount,
	                                       0,
	                                       0,
	                                       materialColor,
	                                       firstPop,
	                                       firstRecordOffset,
	                                       firstRecordOffset,
	                                       SLIP_DRAW3D_CLIP_MASK,
	                                       0,
	                                       visitCount,
	                                       currentRecordOffset,
	                                       firstRecordOffset,
	                                       firstRecordOffset,
	                                       currentRecordOffset,
	                                       anyClipFlags,
	                                       (anyClipFlags & SLIP_DRAW3D_CLIP_MASK) == 0,
	                                       allClipFlags,
	                                       (allClipFlags & SLIP_DRAW3D_CLIP_MASK) != 0,
	                                       false,
	                                       false,
	                                       false,
	                                       false,
	                                       false,
	                                       true,
	                                       false};
	if (result->anyMaskedZero) {
		result->calledClipScreen = true;
		result->screenClipRejected = screenClipRejected != 0;
		result->carryOut = result->screenClipRejected;
	} else if (result->allMaskedNonzero) {
		result->setRejectCarry = true;
		result->carryOut = true;
	} else {
		result->calledClipDepth = true;
		result->depthClipRejected = depthClipRejected != 0;
		if (result->depthClipRejected) {
			result->carryOut = true;
		} else {
			result->calledClipScreen = true;
			result->screenClipRejected = screenClipRejected != 0;
			result->carryOut = result->screenClipRejected;
		}
	}
	return 1;
}

int SlipDraw3D_PointPointerRingWithScreenPointFlags(SlipDraw3DRecordPool *pool, const uint8_t *pointPointerTable,
                                                    size_t pointPointerTableBytes, uint32_t pointCoordinateBaseAddress,
                                                    const uint8_t *pointCoordinateMemory,
                                                    size_t pointCoordinateMemoryBytes, uint16_t countAndFlags,
                                                    uint32_t materialColor, int32_t minX, int32_t maxX, int32_t minY,
                                                    int32_t maxY, int depthClipRejected, int screenClipRejected,
                                                    SlipDraw3DPointPointerRingVisit *visits, size_t visitCapacity,
                                                    SlipDraw3DPointPointerRing *result) {
	uint8_t *recordPoolBytes;
	size_t recordPoolBytesCount;
	uint32_t pointCount;
	uint32_t loopCount;
	uint32_t pointPointerTableOffset;
	uint32_t allClipFlags;
	uint32_t anyClipFlags;
	uint32_t firstRecordOffset;
	uint32_t currentRecordOffset;
	size_t visitCount;
	SlipDraw3DFreeRecordPop firstPop;

	if (pool == NULL || pointPointerTable == NULL || pointCoordinateMemory == NULL || visits == NULL ||
	    result == NULL) {
		return 0;
	}
	pointCount = (uint32_t)countAndFlags;
	if (pointCount == 0 || pointCount > visitCapacity ||
	    pointPointerTableBytes < pointCount * SLIP_DRAW3D_POINT_POINTER_BYTES) {
		return 0;
	}
	recordPoolBytes = SlipDraw3D_RecordPoolBytes(pool);
	recordPoolBytesCount = SlipDraw3D_RecordPoolByteSize();
	if (recordPoolBytes == NULL ||
	    !SlipDraw3D_PopFreeRecord(recordPoolBytes, recordPoolBytesCount, pool->freeHeadOffset, &firstPop)) {
		return 0;
	}
	firstRecordOffset = firstPop.poppedRecordOffset;
	currentRecordOffset = firstRecordOffset;
	pool->inputActiveHeadOffset = firstRecordOffset;
	allClipFlags = SLIP_DRAW3D_CLIP_MASK;
	anyClipFlags = 0;
	loopCount = pointCount;
	pointPointerTableOffset = 0;
	visitCount = 0;
	while (loopCount != 0u) {
		SlipDraw3DPointPointerRingVisit *visit;
		uint32_t pointPointerToken;
		uint32_t pointPointerHostOffset;
		int32_t pointScreenX;
		int32_t pointScreenY;
		SlipDraw3DScreenPointFlags screenPointFlags;
		uint32_t flagsFrom;
		uint32_t loopCountAfterDec;
		bool appendNextRecord;
		SlipDraw3DFreeRecordPop appendPop = {0};
		uint32_t appendedRecordOffset = 0;
		uint32_t pointPointerTableOffsetAfter = pointPointerTableOffset;

		pointPointerToken = SlipBytes_ReadLE32(pointPointerTable + pointPointerTableOffset);
		if (pointPointerToken < pointCoordinateBaseAddress) {
			return 0;
		}
		pointPointerHostOffset = pointPointerToken - pointCoordinateBaseAddress;
		if ((size_t)pointPointerHostOffset > pointCoordinateMemoryBytes ||
		    pointCoordinateMemoryBytes - (size_t)pointPointerHostOffset < sizeof(SlipDraw3DScreenPoint16)) {
			return 0;
		}
		pointScreenX = (int16_t)SlipBytes_ReadLE16(pointCoordinateMemory + pointPointerHostOffset);
		pointScreenY = (int16_t)SlipBytes_ReadLE16(pointCoordinateMemory + pointPointerHostOffset +
		                                           offsetof(SlipDraw3DScreenPoint16, y));
		if (!SlipDraw3D_ScreenPointFlags(SlipDraw3D_RecordPoolDrawRecord(pool, currentRecordOffset), pointScreenX,
		                                 pointScreenY, minX, maxX, minY, maxY, &screenPointFlags)) {
			return 0;
		}
		flagsFrom = screenPointFlags.screenClipFlagsOut;
		allClipFlags &= flagsFrom;
		anyClipFlags |= flagsFrom;
		loopCountAfterDec = loopCount - 1u;
		appendNextRecord = loopCountAfterDec != 0u;
		if (appendNextRecord) {
			if (!SlipDraw3D_PopFreeRecord(recordPoolBytes, recordPoolBytesCount, pool->freeHeadOffset, &appendPop)) {
				return 0;
			}
			appendedRecordOffset = appendPop.poppedRecordOffset;
			SlipDraw3D_SetRecordNext(recordPoolBytes, currentRecordOffset, appendedRecordOffset);
			SlipDraw3D_SetRecordPrev(recordPoolBytes, appendedRecordOffset, currentRecordOffset);
			pointPointerTableOffsetAfter = pointPointerTableOffset + SLIP_DRAW3D_POINT_POINTER_BYTES;
		}
		visit = visits + visitCount;
		*visit = (SlipDraw3DPointPointerRingVisit){loopCount,
		                                           pointPointerToken,
		                                           true,
		                                           flagsFrom,
		                                           allClipFlags,
		                                           anyClipFlags,
		                                           loopCountAfterDec,
		                                           appendNextRecord,
		                                           appendPop,
		                                           appendNextRecord ? currentRecordOffset : 0,
		                                           pointPointerTableOffsetAfter,
		                                           true,
		                                           pointPointerHostOffset,
		                                           pointScreenX,
		                                           pointScreenY};
		if (appendNextRecord) {
			currentRecordOffset = appendedRecordOffset;
		}
		pointPointerTableOffset = pointPointerTableOffsetAfter;
		loopCount = loopCountAfterDec;
		++visitCount;
	}
	SlipDraw3D_SetRecordNext(recordPoolBytes, currentRecordOffset, firstRecordOffset);
	SlipDraw3D_SetRecordPrev(recordPoolBytes, firstRecordOffset, currentRecordOffset);

	*result = (SlipDraw3DPointPointerRing){countAndFlags,
	                                       pointCount,
	                                       0,
	                                       0,
	                                       materialColor,
	                                       firstPop,
	                                       firstRecordOffset,
	                                       firstRecordOffset,
	                                       SLIP_DRAW3D_CLIP_MASK,
	                                       0,
	                                       visitCount,
	                                       currentRecordOffset,
	                                       firstRecordOffset,
	                                       firstRecordOffset,
	                                       currentRecordOffset,
	                                       anyClipFlags,
	                                       (anyClipFlags & SLIP_DRAW3D_CLIP_MASK) == 0,
	                                       allClipFlags,
	                                       (allClipFlags & SLIP_DRAW3D_CLIP_MASK) != 0,
	                                       false,
	                                       false,
	                                       false,
	                                       false,
	                                       false,
	                                       true,
	                                       false};
	if (result->anyMaskedZero) {
		result->calledClipScreen = true;
		result->screenClipRejected = screenClipRejected != 0;
		result->carryOut = result->screenClipRejected;
	} else if (result->allMaskedNonzero) {
		result->setRejectCarry = true;
		result->carryOut = true;
	} else {
		result->calledClipDepth = true;
		result->depthClipRejected = depthClipRejected != 0;
		if (result->depthClipRejected) {
			result->carryOut = true;
		} else {
			result->calledClipScreen = true;
			result->screenClipRejected = screenClipRejected != 0;
			result->carryOut = result->screenClipRejected;
		}
	}
	return 1;
}

int SlipDraw3D_PointPolygon(SlipDraw3DRecordPool *pool, const SlipDraw3DVec32 *const *points, const uint16_t *shades,
                            uint16_t count, uint32_t color, SlipDraw3DProjectState *state, int hasPostPlanes,
                            const uint8_t *postPlanes, size_t postPlaneBytes, uint32_t postPlaneHead, int32_t postMinX,
                            int32_t postMaxX, int32_t postMinY, int32_t postMaxY, size_t maxClipEdges,
                            SlipDraw3DClipFlagVisit *flagVisits, size_t flagCapacity,
                            SlipDraw3DPostPlaneBoundsVisit *boundsVisits, size_t boundsCapacity,
                            SlipDraw3DPostPlaneClipRecordVisit *recordVisits, size_t recordCapacity,
                            SlipDraw3DPostPlaneClipPlaneVisit *planeVisits, size_t planeCapacity,
                            SlipDraw3DPointPolygon *result) {
	if (pool == NULL || points == NULL || ((color & SLIP_POLYGON_COLOUR_VERTEX_SHADED) != 0 && shades == NULL) ||
	    state == NULL || result == NULL || count == 0)
		return 0;
	*result =
	    (SlipDraw3DPointPolygon){.mode = (color & SLIP_POLYGON_COLOUR_VERTEX_SHADED) != 0 ? SLIP_INTERPOLATE_SHADE : 0,
	                             .drawMode = (color & SLIP_POLYGON_COLOUR_VERTEX_SHADED) != 0 ? SLIP_POLYGON_DRAW_SHADED
	                                                                                          : SLIP_POLYGON_DRAW_FLAT,
	                             .color = color,
	                             .allFlags = SLIP_CLIP_ALL};
	SlipDraw3DLinkedDrawRecord *const freeHead = SlipDraw3D_RecordPoolLinkedRecord(pool, pool->freeHeadOffset);
	if (freeHead == NULL)
		return 0;
	const uint32_t firstOffset = freeHead->links.nextOffset;
	uint32_t currentOffset = firstOffset;
	SlipDraw3DLinkedDrawRecord *previous = NULL;

	for (uint32_t i = 0; i < count; ++i) {
		SlipDraw3DLinkedDrawRecord *const current = SlipDraw3D_RecordPoolLinkedRecord(pool, currentOffset);
		if (current == NULL || points[i] == NULL)
			return 0;
		const uint32_t nextOffset = current->links.nextOffset;
		SlipDraw3DLinkedDrawRecord *const next = SlipDraw3D_RecordPoolLinkedRecord(pool, nextOffset);
		if (next == NULL)
			return 0;
		freeHead->links.nextOffset = nextOffset;
		next->links.prevOffset = pool->freeHeadOffset;
		if (previous == NULL)
			pool->inputActiveHeadOffset = firstOffset;
		else {
			previous->links.nextOffset = currentOffset;
			current->links.prevOffset = (uint32_t)(previous - pool->records) * SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE;
		}
		const uint32_t flags = SlipDraw3D_ProjectDrawRecordPoint(&current->drawRecord, *points[i], state);
		result->allFlags &= flags;
		result->anyFlags |= flags;
		if ((color & SLIP_POLYGON_COLOUR_VERTEX_SHADED) != 0)
			current->drawRecord.shade = (uint16_t)((shades[i] & UINT8_MAX) << 8);
		previous = current;
		if (i + 1u < count)
			currentOffset = freeHead->links.nextOffset;
	}

	previous->links.nextOffset = firstOffset;
	pool->records[firstOffset / SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE].links.prevOffset = currentOffset;
	uint8_t *const recordBase = SlipDraw3D_RecordPoolBytes(pool);
	const size_t recordBytes = SlipDraw3D_RecordPoolByteSize();
	uint32_t screenFlags = result->anyFlags;
	if ((screenFlags & SLIP_CLIP_ALL) != 0) {
		if ((result->allFlags & SLIP_CLIP_ALL) != 0) {
			result->carryOut = true;
			return 1;
		}
		result->calledClipDepth = true;
		if (!SlipDraw3D_ClippedDepthExecute(recordBase, recordBytes, pool->inputActiveHeadOffset, pool->freeHeadOffset,
		                                    result->allFlags, result->anyFlags, state->renderFlags, result->mode,
		                                    state->minZ, state->maxZ, state->minX, state->maxX, state->minY,
		                                    state->maxY, state->projectPrimary, state->projectSecondary, state,
		                                    maxClipEdges, flagVisits, flagCapacity, &result->depth))
			return 0;
		pool->inputActiveHeadOffset = result->depth.activeHeadOffsetOut;
		screenFlags = result->depth.anyFlagsOut;
		result->allFlags = result->depth.allFlagsOut;
		result->anyFlags = screenFlags;
		if (result->depth.dispatch.carryOut) {
			result->carryOut = true;
			return 1;
		}
	}
	result->calledClipScreen = true;
	if (!SlipDraw3D_ScreenPlaneExecute(
	        recordBase, recordBytes, pool->inputActiveHeadOffset, pool->freeHeadOffset, screenFlags, result->mode,
	        state->minX, state->maxX, state->minY, state->maxY, hasPostPlanes, postPlanes, postPlaneBytes,
	        postPlaneHead, postMinX, postMaxX, postMinY, postMaxY, maxClipEdges, flagVisits, flagCapacity, boundsVisits,
	        boundsCapacity, recordVisits, recordCapacity, planeVisits, planeCapacity, &result->screen))
		return 0;
	pool->inputActiveHeadOffset = result->screen.activeHeadOffsetOut;
	result->carryOut = result->screen.dispatch.carryOut;
	return 1;
}

int SlipDraw3D_SpritePolygon(SlipDraw3DRecordPool *pool, const SlipDraw3DVec32 *const *points,
                             const SlipDraw3DTextureCoordinates *textureCoordinates, uint16_t count,
                             uint32_t textureHandle, SlipDraw3DProjectState *state, int hasPostPlanes,
                             const uint8_t *postPlanes, size_t postPlaneBytes, uint32_t postPlaneHead, int32_t postMinX,
                             int32_t postMaxX, int32_t postMinY, int32_t postMaxY, size_t maxClipEdges,
                             SlipDraw3DClipFlagVisit *flagVisits, size_t flagCapacity,
                             SlipDraw3DPostPlaneBoundsVisit *boundsVisits, size_t boundsCapacity,
                             SlipDraw3DPostPlaneClipRecordVisit *recordVisits, size_t recordCapacity,
                             SlipDraw3DPostPlaneClipPlaneVisit *planeVisits, size_t planeCapacity,
                             SlipDraw3DSpritePolygon *result) {
	if (pool == NULL || points == NULL || textureCoordinates == NULL || state == NULL || result == NULL || count == 0)
		return 0;
	*result = (SlipDraw3DSpritePolygon){.mode = SLIP_INTERPOLATE_TEXTURE,
	                                    .drawMode = SLIP_POLYGON_DRAW_TEXTURED,
	                                    .textureHandle = textureHandle,
	                                    .allFlags = SLIP_CLIP_ALL};
	SlipDraw3DLinkedDrawRecord *const freeHead = SlipDraw3D_RecordPoolLinkedRecord(pool, pool->freeHeadOffset);
	if (freeHead == NULL)
		return 0;
	const uint32_t firstOffset = freeHead->links.nextOffset;
	uint32_t currentOffset = firstOffset;
	SlipDraw3DLinkedDrawRecord *previous = NULL;

	for (uint32_t i = 0; i < count; ++i) {
		SlipDraw3DLinkedDrawRecord *const current = SlipDraw3D_RecordPoolLinkedRecord(pool, currentOffset);
		if (current == NULL || points[i] == NULL)
			return 0;
		const uint32_t nextOffset = current->links.nextOffset;
		SlipDraw3DLinkedDrawRecord *const next = SlipDraw3D_RecordPoolLinkedRecord(pool, nextOffset);
		if (next == NULL)
			return 0;
		freeHead->links.nextOffset = nextOffset;
		next->links.prevOffset = pool->freeHeadOffset;
		if (previous == NULL)
			pool->inputActiveHeadOffset = firstOffset;
		else {
			previous->links.nextOffset = currentOffset;
			current->links.prevOffset = (uint32_t)(previous - pool->records) * SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE;
		}
		const uint32_t flags = SlipDraw3D_ProjectDrawRecordPoint(&current->drawRecord, *points[i], state);
		result->allFlags &= flags;
		result->anyFlags |= flags;
		current->drawRecord.textureU = textureCoordinates[i].u;
		current->drawRecord.textureV = textureCoordinates[i].v;
		previous = current;
		if (i + 1u < count)
			currentOffset = freeHead->links.nextOffset;
	}

	previous->links.nextOffset = firstOffset;
	pool->records[firstOffset / SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE].links.prevOffset = currentOffset;
	uint8_t *const recordBase = SlipDraw3D_RecordPoolBytes(pool);
	const size_t recordBytes = SlipDraw3D_RecordPoolByteSize();
	uint32_t screenFlags = result->anyFlags;
	if ((screenFlags & SLIP_CLIP_ALL) != 0) {
		if ((result->allFlags & SLIP_CLIP_ALL) != 0) {
			result->carryOut = true;
			return 1;
		}
		result->calledClipDepth = true;
		if (!SlipDraw3D_ClippedDepthExecute(recordBase, recordBytes, pool->inputActiveHeadOffset, pool->freeHeadOffset,
		                                    result->allFlags, result->anyFlags, state->renderFlags, 2, state->minZ,
		                                    state->maxZ, state->minX, state->maxX, state->minY, state->maxY,
		                                    state->projectPrimary, state->projectSecondary, state, maxClipEdges,
		                                    flagVisits, flagCapacity, &result->depth))
			return 0;
		pool->inputActiveHeadOffset = result->depth.activeHeadOffsetOut;
		screenFlags = result->depth.anyFlagsOut;
		result->allFlags = result->depth.allFlagsOut;
		result->anyFlags = screenFlags;
		if (result->depth.dispatch.carryOut) {
			result->carryOut = true;
			return 1;
		}
	}
	result->calledClipScreen = true;
	if (!SlipDraw3D_ScreenPlaneExecute(
	        recordBase, recordBytes, pool->inputActiveHeadOffset, pool->freeHeadOffset, screenFlags, 2, state->minX,
	        state->maxX, state->minY, state->maxY, hasPostPlanes, postPlanes, postPlaneBytes, postPlaneHead, postMinX,
	        postMaxX, postMinY, postMaxY, maxClipEdges, flagVisits, flagCapacity, boundsVisits, boundsCapacity,
	        recordVisits, recordCapacity, planeVisits, planeCapacity, &result->screen))
		return 0;
	pool->inputActiveHeadOffset = result->screen.activeHeadOffsetOut;
	result->carryOut = result->screen.dispatch.carryOut;
	return 1;
}

int SlipDraw3D_SpritePointTextureRing(SlipDraw3DRecordPool *pool, const SlipDraw3DScreenPoint16 *const *points,
                                      const SlipDraw3DTextureCoordinates *textureCoordinates, uint16_t countAndFlags,
                                      uint32_t textureHandle, int32_t minX, int32_t maxX, int32_t minY, int32_t maxY,
                                      int screenClipRejected, SlipDraw3DSpritePointTextureRingVisit *visits,
                                      size_t visitCapacity, SlipDraw3DSpritePointTextureRing *result) {
	uint8_t *recordPoolBytes;
	size_t recordPoolBytesCount;
	uint32_t pointCount;
	uint32_t loopCount;
	uint32_t pointPointerTableOffset;
	uint32_t textureCoordOffset;
	uint32_t allClipFlags;
	uint32_t anyClipFlags;
	uint32_t firstRecordOffset;
	uint32_t currentRecordOffset;
	size_t visitCount;
	SlipDraw3DFreeRecordPop firstPop;

	if (pool == NULL || points == NULL || textureCoordinates == NULL || visits == NULL || result == NULL) {
		return 0;
	}
	pointCount = (uint32_t)countAndFlags;
	if (pointCount == 0 || pointCount > visitCapacity) {
		return 0;
	}
	recordPoolBytes = SlipDraw3D_RecordPoolBytes(pool);
	recordPoolBytesCount = SlipDraw3D_RecordPoolByteSize();
	if (recordPoolBytes == NULL ||
	    !SlipDraw3D_PopFreeRecord(recordPoolBytes, recordPoolBytesCount, pool->freeHeadOffset, &firstPop)) {
		return 0;
	}
	firstRecordOffset = firstPop.poppedRecordOffset;
	currentRecordOffset = firstRecordOffset;
	pool->inputActiveHeadOffset = firstRecordOffset;
	allClipFlags = SLIP_DRAW3D_CLIP_MASK;
	anyClipFlags = 0;
	loopCount = pointCount;
	pointPointerTableOffset = 0;
	textureCoordOffset = 0;
	visitCount = 0;
	while (loopCount != 0u) {
		SlipDraw3DSpritePointTextureRingVisit *visit;
		SlipDraw3DDrawRecord *drawRecord;
		const SlipDraw3DScreenPoint16 *point;
		int32_t pointScreenX;
		int32_t pointScreenY;
		SlipDraw3DScreenPointFlags screenPointFlags;
		uint32_t flagsFrom;
		uint16_t textureU;
		uint16_t textureV;
		uint32_t loopCountAfterDec;
		bool appendNextRecord;
		SlipDraw3DFreeRecordPop appendPop = {0};
		uint32_t appendedRecordOffset = 0;
		uint32_t pointPointerTableOffsetAfter = pointPointerTableOffset;
		uint32_t textureCoordOffsetAfter = textureCoordOffset;

		if (visitCount >= visitCapacity) {
			return 0;
		}
		point = points[visitCount];
		if (point == NULL)
			return 0;
		drawRecord = SlipDraw3D_RecordPoolDrawRecord(pool, currentRecordOffset);
		if (drawRecord == NULL) {
			return 0;
		}
		pointScreenX = point->x;
		pointScreenY = point->y;
		if (!SlipDraw3D_ScreenPointFlags(drawRecord, pointScreenX, pointScreenY, minX, maxX, minY, maxY,
		                                 &screenPointFlags)) {
			return 0;
		}
		flagsFrom = screenPointFlags.screenClipFlagsOut;
		allClipFlags &= flagsFrom;
		anyClipFlags |= flagsFrom;
		textureU = textureCoordinates[visitCount].u;
		textureV = textureCoordinates[visitCount].v;
		drawRecord->textureU = textureU;
		drawRecord->textureV = textureV;
		loopCountAfterDec = loopCount - 1u;
		appendNextRecord = loopCountAfterDec != 0u;
		if (appendNextRecord) {
			if (!SlipDraw3D_PopFreeRecord(recordPoolBytes, recordPoolBytesCount, pool->freeHeadOffset, &appendPop)) {
				return 0;
			}
			appendedRecordOffset = appendPop.poppedRecordOffset;
			SlipDraw3D_SetRecordNext(recordPoolBytes, currentRecordOffset, appendedRecordOffset);
			SlipDraw3D_SetRecordPrev(recordPoolBytes, appendedRecordOffset, currentRecordOffset);
			pointPointerTableOffsetAfter = pointPointerTableOffset + SLIP_DRAW3D_POINT_POINTER_BYTES;
			textureCoordOffsetAfter = textureCoordOffset + SLIP_SERIALIZED_TEXTURE_COORDINATE_BYTES;
		}
		visit = visits + visitCount;
		*visit = (SlipDraw3DSpritePointTextureRingVisit){loopCount,
		                                                 point,
		                                                 pointScreenX,
		                                                 pointScreenY,
		                                                 true,
		                                                 flagsFrom,
		                                                 allClipFlags,
		                                                 anyClipFlags,
		                                                 textureU,
		                                                 textureV,
		                                                 loopCountAfterDec,
		                                                 appendNextRecord,
		                                                 appendPop,
		                                                 appendNextRecord ? currentRecordOffset : 0,
		                                                 pointPointerTableOffsetAfter,
		                                                 textureCoordOffsetAfter};
		if (appendNextRecord) {
			currentRecordOffset = appendedRecordOffset;
		}
		pointPointerTableOffset = pointPointerTableOffsetAfter;
		textureCoordOffset = textureCoordOffsetAfter;
		loopCount = loopCountAfterDec;
		++visitCount;
	}
	SlipDraw3D_SetRecordNext(recordPoolBytes, currentRecordOffset, firstRecordOffset);
	SlipDraw3D_SetRecordPrev(recordPoolBytes, firstRecordOffset, currentRecordOffset);

	*result = (SlipDraw3DSpritePointTextureRing){countAndFlags,
	                                             pointCount,
	                                             SLIP_INTERPOLATE_TEXTURE,
	                                             SLIP_POLYGON_DRAW_TEXTURED,
	                                             textureHandle,
	                                             firstPop,
	                                             firstRecordOffset,
	                                             firstRecordOffset,
	                                             SLIP_DRAW3D_CLIP_MASK,
	                                             0,
	                                             visitCount,
	                                             currentRecordOffset,
	                                             firstRecordOffset,
	                                             firstRecordOffset,
	                                             currentRecordOffset,
	                                             anyClipFlags,
	                                             (anyClipFlags & SLIP_DRAW3D_CLIP_MASK) == 0,
	                                             allClipFlags,
	                                             (allClipFlags & SLIP_DRAW3D_CLIP_MASK) != 0,
	                                             false,
	                                             false,
	                                             false,
	                                             true,
	                                             false};
	if (result->anyMaskedZero) {
		result->carryOut = false;
	} else if (result->allMaskedNonzero) {
		result->setRejectCarry = true;
		result->carryOut = true;
	} else {
		result->calledClipScreen = true;
		result->screenClipRejected = screenClipRejected != 0;
		result->carryOut = result->screenClipRejected;
	}
	return 1;
}

int SlipDraw3D_BuildActiveRing(SlipDraw3DRecordPool *pool, SlipDraw3DVertexRecord *vertexRecords,
                               size_t vertexRecordCount, const uint8_t *indexStream, size_t indexStreamBytes,
                               uint16_t countAndFlags, const SlipDraw3DProjectState *state,
                               SlipDraw3DTransformFn transform, SlipDraw3DProjectFn projectPrimary,
                               SlipDraw3DProjectFn projectSecondary, void *userData, int depthClipRejected,
                               int screenClipRejected, SlipDraw3DActiveRingVisit *visits, size_t visitCapacity,
                               SlipDraw3DActiveRingBuild *result) {
	uint8_t *recordPoolBytes;
	size_t recordPoolBytesCount;
	SlipDraw3DFreeRecordPop firstPop;
	uint32_t currentRecordOffset;
	uint32_t firstRecordOffset;
	uint32_t allClipFlags;
	uint32_t anyClipFlags;
	uint32_t indexStreamOffset;
	uint32_t loopCount;
	size_t visitCount;

	if (pool == NULL || vertexRecords == NULL || indexStream == NULL || state == NULL || transform == NULL ||
	    projectPrimary == NULL || projectSecondary == NULL || visits == NULL || result == NULL || countAndFlags == 0 ||
	    visitCapacity < countAndFlags || indexStreamBytes < (size_t)countAndFlags * SLIP_SERIALIZED_INDEX_BYTES) {
		return 0;
	}
	recordPoolBytes = SlipDraw3D_RecordPoolBytes(pool);
	recordPoolBytesCount = SlipDraw3D_RecordPoolByteSize();
	if (recordPoolBytes == NULL ||
	    !SlipDraw3D_PopFreeRecord(recordPoolBytes, recordPoolBytesCount, pool->freeHeadOffset, &firstPop)) {
		return 0;
	}
	pool->inputActiveHeadOffset = firstPop.poppedRecordOffset;
	currentRecordOffset = pool->inputActiveHeadOffset;
	firstRecordOffset = currentRecordOffset;
	allClipFlags = SLIP_CLIP_ALL;
	anyClipFlags = 0;
	indexStreamOffset = 0;
	loopCount = countAndFlags;
	visitCount = 0;

	for (;;) {
		SlipDraw3DActiveRingVisit *visit;
		SlipDraw3DDrawRecord *drawRecord;
		uint16_t indexWord;
		uint32_t vertexRecordOffset;
		uint32_t flags;
		size_t i;

		if (visitCount >= visitCapacity || (size_t)indexStreamOffset > indexStreamBytes ||
		    indexStreamBytes - (size_t)indexStreamOffset < SLIP_SERIALIZED_INDEX_BYTES) {
			return 0;
		}
		indexWord = SlipBytes_ReadLE16(indexStream + indexStreamOffset);
		vertexRecordOffset = (uint32_t)indexWord * SLIP_DRAW3D_VERTEX_RECORD_SIZE;
		if (indexWord >= vertexRecordCount || (size_t)vertexRecordOffset > vertexRecordCount * sizeof(*vertexRecords) ||
		    vertexRecordCount * sizeof(*vertexRecords) - (size_t)vertexRecordOffset <
		        SLIP_DRAW3D_VERTEX_DRAW_PREFIX_BYTES) {
			return 0;
		}
		flags = SlipDraw3D_ProjectVertex(&vertexRecords[indexWord], state, transform, projectPrimary, projectSecondary,
		                                 userData);
		drawRecord = SlipDraw3D_RecordPoolDrawRecord(pool, currentRecordOffset);
		if (drawRecord == NULL) {
			return 0;
		}
		for (i = 0; i < SLIP_DRAW3D_VERTEX_DRAW_PREFIX_DWORDS; ++i) {
			SlipDraw3D_WriteLE32(drawRecord->bytes + i * sizeof(uint32_t),
			                     SlipBytes_ReadLE32(vertexRecords[indexWord].bytes + i * sizeof(uint32_t)));
		}
		allClipFlags &= flags;
		anyClipFlags |= flags;
		visit = visits + visitCount;
		*visit = (SlipDraw3DActiveRingVisit){.savedLoopCount = loopCount,
		                                     .indexWord = indexWord,
		                                     .vertexRecordOffset = vertexRecordOffset,
		                                     .drawRecordOffset = currentRecordOffset,
		                                     .callProjectVertex = true,
		                                     .flagsFromProjectVertex = flags,
		                                     .copiedDwords = SLIP_DRAW3D_VERTEX_DRAW_PREFIX_DWORDS,
		                                     .allClipFlagsAfter = allClipFlags,
		                                     .anyClipFlagsAfter = anyClipFlags};
		--loopCount;
		visit->loopCountAfterDec = loopCount;
		++visitCount;
		if (loopCount == 0) {
			break;
		}
		{
			SlipDraw3DFreeRecordPop appendPop;
			const uint32_t previousRecordOffset = currentRecordOffset;

			if (!SlipDraw3D_PopFreeRecord(recordPoolBytes, recordPoolBytesCount, pool->freeHeadOffset, &appendPop)) {
				return 0;
			}
			currentRecordOffset = appendPop.poppedRecordOffset;
			SlipDraw3D_SetRecordNext(recordPoolBytes, previousRecordOffset, currentRecordOffset);
			SlipDraw3D_SetRecordPrev(recordPoolBytes, currentRecordOffset, previousRecordOffset);
			indexStreamOffset += SLIP_SERIALIZED_INDEX_BYTES;
			visit->appendNextRecord = true;
			visit->appendPop = appendPop;
			visit->appendedPrevAfter = previousRecordOffset;
			visit->indexStreamOffsetAfter = indexStreamOffset;
		}
	}
	SlipDraw3D_SetRecordNext(recordPoolBytes, currentRecordOffset, firstRecordOffset);
	SlipDraw3D_SetRecordPrev(recordPoolBytes, firstRecordOffset, currentRecordOffset);

	*result = (SlipDraw3DActiveRingBuild){.countAndFlags = countAndFlags,
	                                      .vertexCount = countAndFlags,
	                                      .mode = 0,
	                                      .firstPop = firstPop,
	                                      .inputActiveHeadOffset = pool->inputActiveHeadOffset,
	                                      .savedFirstRecordOffset = firstRecordOffset,
	                                      .allClipFlagsInitial = SLIP_CLIP_ALL,
	                                      .anyClipFlagsInitial = 0,
	                                      .visitCount = visitCount,
	                                      .lastRecordOffset = currentRecordOffset,
	                                      .firstRecordOffset = firstRecordOffset,
	                                      .lastNextAfter = firstRecordOffset,
	                                      .firstPrevAfter = currentRecordOffset,
	                                      .anyFlagsBeforeDispatch = anyClipFlags,
	                                      .anyMaskedZero = (anyClipFlags & SLIP_DRAW3D_CLIP_MASK) == 0,
	                                      .returned = true};
	if (!result->anyMaskedZero) {
		result->allFlagsBeforeDispatch = allClipFlags;
		result->allMaskedNonzero = (allClipFlags & SLIP_DRAW3D_CLIP_MASK) != 0;
		if (result->allMaskedNonzero) {
			result->setRejectCarry = true;
			result->carryOut = true;
			return 1;
		}
		result->calledClipDepth = true;
		result->depthClipRejected = depthClipRejected != 0;
		if (result->depthClipRejected) {
			result->carryOut = true;
			return 1;
		}
	}
	result->calledClipScreen = true;
	result->screenClipRejected = screenClipRejected != 0;
	result->carryOut = result->screenClipRejected;
	return 1;
}

int SlipDraw3D_BuildTexturedRing(SlipDraw3DRecordPool *pool, SlipDraw3DVertexRecord *vertexRecords,
                                 size_t vertexRecordCount, const uint8_t *indexStream, size_t indexStreamBytes,
                                 uint16_t countAndFlags, uint32_t textureHandle, const SlipDraw3DProjectState *state,
                                 SlipDraw3DTransformFn transform, SlipDraw3DProjectFn projectPrimary,
                                 SlipDraw3DProjectFn projectSecondary, void *userData, int signFlagFrom,
                                 int depthClipRejected, int screenClipRejected, SlipDraw3DTexturedRingVisit *visits,
                                 size_t visitCapacity, SlipDraw3DTexturedRingBuild *result) {
	uint8_t *recordPoolBytes;
	size_t recordPoolBytesCount;
	SlipDraw3DFreeRecordPop firstPop;
	uint32_t currentRecordOffset;
	uint32_t firstRecordOffset;
	uint32_t allClipFlags;
	uint32_t anyClipFlags;
	uint32_t indexStreamOffset;
	uint32_t textureCoordOffset;
	uint32_t textureCoordBaseOffset;
	uint32_t loopCount;
	size_t textureCoordBytesRequired;
	bool wideTextureCoordStride;
	size_t visitCount;

	if (result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->calledPolygonStatus = true;
	result->signReject = signFlagFrom != 0;
	result->returned = true;
	if (result->signReject) {
		result->setRejectCarry = true;
		result->carryOut = true;
		return 1;
	}
	if (pool == NULL || vertexRecords == NULL || indexStream == NULL || state == NULL || transform == NULL ||
	    projectPrimary == NULL || projectSecondary == NULL || visits == NULL || countAndFlags == 0 ||
	    visitCapacity < (size_t)(countAndFlags & SLIP_PRIMITIVE_VERTEX_COUNT_MASK) ||
	    indexStreamBytes < (size_t)(countAndFlags & SLIP_PRIMITIVE_VERTEX_COUNT_MASK) * SLIP_SERIALIZED_INDEX_BYTES) {
		return 0;
	}
	result->countAndFlags = countAndFlags;
	loopCount = (uint32_t)countAndFlags & SLIP_PRIMITIVE_VERTEX_COUNT_MASK;
	wideTextureCoordStride = ((uint32_t)countAndFlags & SLIP_PRIMITIVE_VERTEX_NORMALS) != 0;
	textureCoordBaseOffset =
	    ((uint32_t)countAndFlags * SLIP_SERIALIZED_INDEX_BYTES) & SLIP_SERIALIZED_TEXTURE_COORDINATE_BASE_MASK;
	if (wideTextureCoordStride) {
		textureCoordBaseOffset =
		    ((uint32_t)countAndFlags * (SLIP_SERIALIZED_INDEX_BYTES + SLIP_SERIALIZED_NORMAL_BYTES)) &
		    SLIP_SERIALIZED_TEXTURE_COORDINATE_BASE_MASK;
	}
	textureCoordBytesRequired = (size_t)loopCount * SLIP_SERIALIZED_TEXTURE_COORDINATE_BYTES;
	if (loopCount == 0 || textureCoordBaseOffset > indexStreamBytes ||
	    indexStreamBytes - textureCoordBaseOffset < textureCoordBytesRequired) {
		return 0;
	}
	recordPoolBytes = SlipDraw3D_RecordPoolBytes(pool);
	recordPoolBytesCount = SlipDraw3D_RecordPoolByteSize();
	if (recordPoolBytes == NULL ||
	    !SlipDraw3D_PopFreeRecord(recordPoolBytes, recordPoolBytesCount, pool->freeHeadOffset, &firstPop)) {
		return 0;
	}
	pool->inputActiveHeadOffset = firstPop.poppedRecordOffset;
	currentRecordOffset = pool->inputActiveHeadOffset;
	firstRecordOffset = currentRecordOffset;
	allClipFlags = SLIP_CLIP_ALL;
	anyClipFlags = 0;
	indexStreamOffset = 0;
	textureCoordOffset = textureCoordBaseOffset;
	visitCount = 0;

	for (;;) {
		SlipDraw3DTexturedRingVisit *visit;
		SlipDraw3DDrawRecord *drawRecord;
		uint16_t indexWord;
		uint32_t vertexRecordOffset;
		uint32_t flags;
		uint16_t textureCoordWord;
		uint16_t textureV;
		size_t i;

		if (visitCount >= visitCapacity || (size_t)indexStreamOffset > indexStreamBytes ||
		    indexStreamBytes - (size_t)indexStreamOffset < SLIP_SERIALIZED_INDEX_BYTES ||
		    (size_t)textureCoordOffset > indexStreamBytes ||
		    indexStreamBytes - (size_t)textureCoordOffset < SLIP_SERIALIZED_TEXTURE_COORDINATE_BYTES) {
			return 0;
		}
		indexWord = SlipBytes_ReadLE16(indexStream + indexStreamOffset);
		vertexRecordOffset = (uint32_t)indexWord * SLIP_DRAW3D_VERTEX_RECORD_SIZE;
		if (indexWord >= vertexRecordCount || (size_t)vertexRecordOffset > vertexRecordCount * sizeof(*vertexRecords) ||
		    vertexRecordCount * sizeof(*vertexRecords) - (size_t)vertexRecordOffset <
		        SLIP_DRAW3D_VERTEX_DRAW_PREFIX_BYTES) {
			return 0;
		}
		flags = SlipDraw3D_ProjectVertex(&vertexRecords[indexWord], state, transform, projectPrimary, projectSecondary,
		                                 userData);
		drawRecord = SlipDraw3D_RecordPoolDrawRecord(pool, currentRecordOffset);
		if (drawRecord == NULL) {
			return 0;
		}
		for (i = 0; i < SLIP_DRAW3D_VERTEX_DRAW_PREFIX_DWORDS; ++i) {
			SlipDraw3D_WriteLE32(drawRecord->bytes + i * sizeof(uint32_t),
			                     SlipBytes_ReadLE32(vertexRecords[indexWord].bytes + i * sizeof(uint32_t)));
		}
		allClipFlags &= flags;
		anyClipFlags |= flags;
		textureCoordWord = SlipBytes_ReadLE16(indexStream + textureCoordOffset + SLIP_SERIALIZED_TEXTURE_U_OFFSET);

		textureV = SlipBytes_ReadLE16(indexStream + textureCoordOffset + SLIP_SERIALIZED_TEXTURE_V_OFFSET);
		drawRecord->textureU = textureCoordWord;
		drawRecord->textureV = textureV;
		visit = visits + visitCount;
		*visit = (SlipDraw3DTexturedRingVisit){.savedLoopCount = loopCount,
		                                       .indexWord = indexWord,
		                                       .vertexRecordOffset = vertexRecordOffset,
		                                       .drawRecordOffset = currentRecordOffset,
		                                       .callProjectVertex = true,
		                                       .flagsFromProjectVertex = flags,
		                                       .copiedDwords = SLIP_DRAW3D_VERTEX_DRAW_PREFIX_DWORDS,
		                                       .allClipFlagsAfter = allClipFlags,
		                                       .anyClipFlagsAfter = anyClipFlags,
		                                       .textureCoordOffset = textureCoordOffset,
		                                       .textureCoordWord = textureCoordWord,
		                                       .textureV = textureV};
		--loopCount;
		visit->loopCountAfterDec = loopCount;
		++visitCount;
		if (loopCount == 0) {
			break;
		}
		{
			SlipDraw3DFreeRecordPop appendPop;
			const uint32_t previousRecordOffset = currentRecordOffset;

			if (!SlipDraw3D_PopFreeRecord(recordPoolBytes, recordPoolBytesCount, pool->freeHeadOffset, &appendPop)) {
				return 0;
			}
			currentRecordOffset = appendPop.poppedRecordOffset;
			SlipDraw3D_SetRecordNext(recordPoolBytes, previousRecordOffset, currentRecordOffset);
			SlipDraw3D_SetRecordPrev(recordPoolBytes, currentRecordOffset, previousRecordOffset);
			indexStreamOffset += SLIP_SERIALIZED_INDEX_BYTES;
			textureCoordOffset += SLIP_SERIALIZED_TEXTURE_COORDINATE_BYTES;
			visit->appendNextRecord = true;
			visit->appendPop = appendPop;
			visit->appendedPrevAfter = previousRecordOffset;
			visit->indexStreamOffsetAfter = indexStreamOffset;
			visit->textureCoordOffsetAfter = textureCoordOffset;
		}
	}
	SlipDraw3D_SetRecordNext(recordPoolBytes, currentRecordOffset, firstRecordOffset);
	SlipDraw3D_SetRecordPrev(recordPoolBytes, firstRecordOffset, currentRecordOffset);

	*result = (SlipDraw3DTexturedRingBuild){.calledPolygonStatus = true,
	                                        .countAndFlags = countAndFlags,
	                                        .vertexCount = (uint32_t)countAndFlags & SLIP_PRIMITIVE_VERTEX_COUNT_MASK,
	                                        .wideTextureCoordStride = wideTextureCoordStride,
	                                        .textureCoordBaseOffset = textureCoordBaseOffset,
	                                        .textureCoordStreamOffset = textureCoordBaseOffset,
	                                        .textureHandle = textureHandle,
	                                        .mode = SLIP_INTERPOLATE_TEXTURE_PERSPECTIVE,
	                                        .drawMode = SLIP_POLYGON_DRAW_TEXTURED,
	                                        .firstPop = firstPop,
	                                        .inputActiveHeadOffset = pool->inputActiveHeadOffset,
	                                        .savedFirstRecordOffset = firstRecordOffset,
	                                        .allClipFlagsInitial = SLIP_CLIP_ALL,
	                                        .anyClipFlagsInitial = 0,
	                                        .visitCount = visitCount,
	                                        .lastRecordOffset = currentRecordOffset,
	                                        .firstRecordOffset = firstRecordOffset,
	                                        .lastNextAfter = firstRecordOffset,
	                                        .firstPrevAfter = currentRecordOffset,
	                                        .anyFlagsBeforeDispatch = anyClipFlags,
	                                        .anyMaskedZero = (anyClipFlags & SLIP_DRAW3D_CLIP_MASK) == 0,
	                                        .returned = true};
	if (!result->anyMaskedZero) {
		result->allFlagsBeforeDispatch = allClipFlags;
		result->allMaskedNonzero = (allClipFlags & SLIP_DRAW3D_CLIP_MASK) != 0;
		if (result->allMaskedNonzero) {
			result->setRejectCarry = true;
			result->carryOut = true;
			return 1;
		}
		result->calledClipDepth = true;
		result->depthClipRejected = depthClipRejected != 0;
		if (result->depthClipRejected) {
			result->carryOut = true;
			return 1;
		}
	}
	result->calledClipScreen = true;
	result->screenClipRejected = screenClipRejected != 0;
	result->carryOut = result->screenClipRejected;
	return 1;
}

int SlipDraw3D_BuildTexturedRingExecute(
    SlipDraw3DRecordPool *pool, SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCount,
    const uint8_t *indexStream, size_t indexStreamBytes, uint16_t countAndFlags, uint32_t textureHandle,
    const SlipDraw3DProjectState *state, SlipDraw3DTransformFn transform, SlipDraw3DProjectMaskFn projectMask,
    SlipDraw3DProjectFn projectPrimary, SlipDraw3DProjectFn projectSecondary, void *userData, int hasPostPlanes,
    const uint8_t *planeBase, size_t planeBytes, uint32_t planeHeadOffset, int32_t postLimitXMin, int32_t postLimitXMax,
    int32_t postLimitYMin, int32_t postLimitYMax, size_t maxClipEdgeVisits, SlipDraw3DClipFlagVisit *clipFlagVisits,
    size_t clipFlagVisitCapacity, SlipDraw3DPostPlaneBoundsVisit *postBoundsVisits, size_t postBoundsVisitCapacity,
    SlipDraw3DPostPlaneClipRecordVisit *postClipRecordVisits, size_t postClipRecordVisitCapacity,
    SlipDraw3DPostPlaneClipPlaneVisit *postClipPlaneVisits, size_t postClipPlaneVisitCapacity,
    SlipDraw3DPolygonStatusVisit *statusVisits, size_t statusVisitCapacity, SlipDraw3DTexturedRingVisit *visits,
    size_t visitCapacity, SlipDraw3DTexturedRingExecute *result) {
	uint8_t *recordPoolBytes;
	size_t recordPoolBytesCount;
	SlipDraw3DTexturedRingBuild build;
	SlipDraw3DPolygonStatus status;

	if (pool == NULL || state == NULL || result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	if (!SlipDraw3D_PolygonStatus(vertexRecords, vertexRecordCount, indexStream, indexStreamBytes, countAndFlags, state,
	                              transform, projectMask, userData, statusVisits, statusVisitCapacity, &status)) {
		result->failureStage = SLIP_DRAW3D_TEXTURED_RING_STATUS_FAILURE_DOS_STAGE;
		return 0;
	}
	result->calledPolygonStatus = true;
	result->status = status;
	if (!SlipDraw3D_BuildTexturedRing(pool, vertexRecords, vertexRecordCount, indexStream, indexStreamBytes,
	                                  countAndFlags, textureHandle, state, transform, projectPrimary, projectSecondary,
	                                  userData, status.clipClassification == -1, 0, 0, visits, visitCapacity, &build)) {
		result->failureStage = SLIP_DRAW3D_TEXTURED_RING_BUILD_FAILURE_DOS_STAGE;
		return 0;
	}
	result->build = build;
	result->returned = true;
	if (build.signReject) {
		result->carryOut = true;
		result->activeHeadOffsetOut = pool->inputActiveHeadOffset;
		return 1;
	}
	if (build.allMaskedNonzero) {
		result->carryOut = true;
		result->activeHeadOffsetOut = pool->inputActiveHeadOffset;
		return 1;
	}
	recordPoolBytes = SlipDraw3D_RecordPoolBytes(pool);
	recordPoolBytesCount = SlipDraw3D_RecordPoolByteSize();
	if (recordPoolBytes == NULL) {
		return 0;
	}
	if (!SlipDraw3D_ClipDispatchExecute(
	        recordPoolBytes, recordPoolBytesCount, pool->inputActiveHeadOffset, pool->freeHeadOffset,
	        build.anyFlagsBeforeDispatch, build.allFlagsBeforeDispatch, state->renderFlags, build.mode, state->minZ,
	        state->maxZ, state->minX, state->maxX, state->minY, state->maxY, projectPrimary, projectSecondary, userData,
	        hasPostPlanes, planeBase, planeBytes, planeHeadOffset, postLimitXMin, postLimitXMax, postLimitYMin,
	        postLimitYMax, maxClipEdgeVisits, clipFlagVisits, clipFlagVisitCapacity, postBoundsVisits,
	        postBoundsVisitCapacity, postClipRecordVisits, postClipRecordVisitCapacity, postClipPlaneVisits,
	        postClipPlaneVisitCapacity, &result->dispatch)) {
		result->failureStage = SLIP_DRAW3D_TEXTURED_RING_CLIP_FAILURE_DOS_STAGE;
		return 0;
	}
	pool->inputActiveHeadOffset = result->dispatch.activeHeadOffsetOut;
	result->activeHeadOffsetOut = pool->inputActiveHeadOffset;
	result->build.inputActiveHeadOffset = result->activeHeadOffsetOut;
	result->build.calledClipDepth = result->dispatch.dispatch.calledClipDepth;
	result->build.depthClipRejected = result->dispatch.dispatch.depthClipRejected;
	result->build.calledClipScreen = result->dispatch.dispatch.calledClipScreen;
	result->build.screenClipRejected = result->dispatch.dispatch.screenClipRejected;
	result->build.carryOut = result->dispatch.dispatch.carryOut;
	result->carryOut = result->build.carryOut;
	return 1;
}

int SlipDraw3D_BuildActiveRingExecute(
    SlipDraw3DRecordPool *pool, SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCount,
    const uint8_t *indexStream, size_t indexStreamBytes, uint16_t countAndFlags, const SlipDraw3DProjectState *state,
    SlipDraw3DTransformFn transform, SlipDraw3DProjectFn projectPrimary, SlipDraw3DProjectFn projectSecondary,
    void *userData, int hasPostPlanes, const uint8_t *planeBase, size_t planeBytes, uint32_t planeHeadOffset,
    int32_t postLimitXMin, int32_t postLimitXMax, int32_t postLimitYMin, int32_t postLimitYMax,
    size_t maxClipEdgeVisits, SlipDraw3DClipFlagVisit *clipFlagVisits, size_t clipFlagVisitCapacity,
    SlipDraw3DPostPlaneBoundsVisit *postBoundsVisits, size_t postBoundsVisitCapacity,
    SlipDraw3DPostPlaneClipRecordVisit *postClipRecordVisits, size_t postClipRecordVisitCapacity,
    SlipDraw3DPostPlaneClipPlaneVisit *postClipPlaneVisits, size_t postClipPlaneVisitCapacity,
    SlipDraw3DActiveRingVisit *visits, size_t visitCapacity, SlipDraw3DActiveRingExecute *result) {
	uint8_t *recordPoolBytes;
	size_t recordPoolBytesCount;
	SlipDraw3DActiveRingBuild build;

	if (pool == NULL || state == NULL || result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	if (!SlipDraw3D_BuildActiveRing(pool, vertexRecords, vertexRecordCount, indexStream, indexStreamBytes,
	                                countAndFlags, state, transform, projectPrimary, projectSecondary, userData, 0, 0,
	                                visits, visitCapacity, &build)) {
		return 0;
	}
	result->build = build;
	result->returned = true;
	if (build.allMaskedNonzero) {
		result->carryOut = true;
		result->activeHeadOffsetOut = pool->inputActiveHeadOffset;
		return 1;
	}
	recordPoolBytes = SlipDraw3D_RecordPoolBytes(pool);
	recordPoolBytesCount = SlipDraw3D_RecordPoolByteSize();
	if (recordPoolBytes == NULL) {
		return 0;
	}
	if (!SlipDraw3D_ClipDispatchExecute(
	        recordPoolBytes, recordPoolBytesCount, pool->inputActiveHeadOffset, pool->freeHeadOffset,
	        build.anyFlagsBeforeDispatch, build.allFlagsBeforeDispatch, state->renderFlags, 0, state->minZ, state->maxZ,
	        state->minX, state->maxX, state->minY, state->maxY, projectPrimary, projectSecondary, userData,
	        hasPostPlanes, planeBase, planeBytes, planeHeadOffset, postLimitXMin, postLimitXMax, postLimitYMin,
	        postLimitYMax, maxClipEdgeVisits, clipFlagVisits, clipFlagVisitCapacity, postBoundsVisits,
	        postBoundsVisitCapacity, postClipRecordVisits, postClipRecordVisitCapacity, postClipPlaneVisits,
	        postClipPlaneVisitCapacity, &result->dispatch)) {
		return 0;
	}
	pool->inputActiveHeadOffset = result->dispatch.activeHeadOffsetOut;
	result->activeHeadOffsetOut = pool->inputActiveHeadOffset;
	result->build.inputActiveHeadOffset = result->activeHeadOffsetOut;
	result->build.calledClipDepth = result->dispatch.dispatch.calledClipDepth;
	result->build.depthClipRejected = result->dispatch.dispatch.depthClipRejected;
	result->build.calledClipScreen = result->dispatch.dispatch.calledClipScreen;
	result->build.screenClipRejected = result->dispatch.dispatch.screenClipRejected;
	result->build.carryOut = result->dispatch.dispatch.carryOut;
	result->carryOut = result->build.carryOut;
	return 1;
}

int SlipDraw3D_BuildActiveMaterialRingExecute(
    SlipDraw3DRecordPool *pool, SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCount,
    const uint8_t *indexStream, size_t indexStreamBytes, const uint8_t *materialControl, size_t materialControlBytes,
    uint16_t countAndFlags, uint32_t materialColor, uint32_t renderFlags, const SlipDraw3DProjectState *state,
    SlipDraw3DTransformFn transform, SlipDraw3DProjectFn projectPrimary, SlipDraw3DProjectFn projectSecondary,
    void *userData, int hasPostPlanes, const uint8_t *planeBase, size_t planeBytes, uint32_t planeHeadOffset,
    int32_t postLimitXMin, int32_t postLimitXMax, int32_t postLimitYMin, int32_t postLimitYMax,
    size_t maxClipEdgeVisits, SlipDraw3DClipFlagVisit *clipFlagVisits, size_t clipFlagVisitCapacity,
    SlipDraw3DPostPlaneBoundsVisit *postBoundsVisits, size_t postBoundsVisitCapacity,
    SlipDraw3DPostPlaneClipRecordVisit *postClipRecordVisits, size_t postClipRecordVisitCapacity,
    SlipDraw3DPostPlaneClipPlaneVisit *postClipPlaneVisits, size_t postClipPlaneVisitCapacity,
    SlipDraw3DActiveRingVisit *visits, size_t visitCapacity, SlipDraw3DActiveMaterialRingExecute *result) {
	uint8_t *recordPoolBytes;
	size_t recordPoolBytesCount;
	SlipDraw3DActiveRingBuild build;
	bool specialBranch;

	if (pool == NULL || state == NULL || result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	specialBranch = (renderFlags & SLIP_RENDER_DISABLE_VERTEX_SHADING) == 0u &&
	                (materialColor & SLIP_POLYGON_COLOUR_VERTEX_SHADED) != 0u;
	result->specialBranch = (renderFlags & SLIP_RENDER_DISABLE_VERTEX_SHADING) == 0u;
	result->hasVertexShadingFlag = (materialColor & SLIP_POLYGON_COLOUR_VERTEX_SHADED) != 0u;
	result->mode = specialBranch ? SLIP_INTERPOLATE_SHADE : 0;
	result->drawMode = specialBranch ? SLIP_POLYGON_DRAW_SHADED : SLIP_POLYGON_DRAW_FLAT;
	result->materialColor = specialBranch ? 0u : materialColor;
	if (!SlipDraw3D_BuildActiveRing(pool, vertexRecords, vertexRecordCount, indexStream, indexStreamBytes,
	                                countAndFlags, state, transform, projectPrimary, projectSecondary, userData, 0, 0,
	                                visits, visitCapacity, &build)) {
		return 0;
	}
	result->build = build;
	result->build.mode = result->mode;
	if (specialBranch) {
		size_t i;

		if (materialControl == NULL ||
		    materialControlBytes < (size_t)countAndFlags * SLIP_DRAW3D_VERTEX_SHADE_CONTROL_BYTES || visits == NULL ||
		    visitCapacity < build.visitCount) {
			return 0;
		}
		recordPoolBytes = SlipDraw3D_RecordPoolBytes(pool);
		recordPoolBytesCount = SlipDraw3D_RecordPoolByteSize();
		if (recordPoolBytes == NULL) {
			return 0;
		}
		for (i = 0; i < build.visitCount; ++i) {
			const uint32_t recordOffset = visits[i].drawRecordOffset;

			if (!SlipDraw3D_LinkedRecordOffsetValid(recordPoolBytesCount, recordOffset)) {
				return 0;
			}
			recordPoolBytes[recordOffset + offsetof(SlipDraw3DDrawRecord, shade) + 1] =
			    materialControl[i * SLIP_DRAW3D_VERTEX_SHADE_CONTROL_BYTES];
			recordPoolBytes[recordOffset + offsetof(SlipDraw3DDrawRecord, shade)] = 0;
		}
	}
	result->returned = true;
	if (build.allMaskedNonzero) {
		result->carryOut = true;
		result->activeHeadOffsetOut = pool->inputActiveHeadOffset;
		return 1;
	}
	recordPoolBytes = SlipDraw3D_RecordPoolBytes(pool);
	recordPoolBytesCount = SlipDraw3D_RecordPoolByteSize();
	if (recordPoolBytes == NULL) {
		return 0;
	}
	if (!SlipDraw3D_ClipDispatchExecute(
	        recordPoolBytes, recordPoolBytesCount, pool->inputActiveHeadOffset, pool->freeHeadOffset,
	        build.anyFlagsBeforeDispatch, build.allFlagsBeforeDispatch, state->renderFlags, 0, state->minZ, state->maxZ,
	        state->minX, state->maxX, state->minY, state->maxY, projectPrimary, projectSecondary, userData,
	        hasPostPlanes, planeBase, planeBytes, planeHeadOffset, postLimitXMin, postLimitXMax, postLimitYMin,
	        postLimitYMax, maxClipEdgeVisits, clipFlagVisits, clipFlagVisitCapacity, postBoundsVisits,
	        postBoundsVisitCapacity, postClipRecordVisits, postClipRecordVisitCapacity, postClipPlaneVisits,
	        postClipPlaneVisitCapacity, &result->dispatch)) {
		return 0;
	}
	pool->inputActiveHeadOffset = result->dispatch.activeHeadOffsetOut;
	result->activeHeadOffsetOut = pool->inputActiveHeadOffset;
	result->build.inputActiveHeadOffset = result->activeHeadOffsetOut;
	result->build.calledClipDepth = result->dispatch.dispatch.calledClipDepth;
	result->build.depthClipRejected = result->dispatch.dispatch.depthClipRejected;
	result->build.calledClipScreen = result->dispatch.dispatch.calledClipScreen;
	result->build.screenClipRejected = result->dispatch.dispatch.screenClipRejected;
	result->build.carryOut = result->dispatch.dispatch.carryOut;
	result->carryOut = result->build.carryOut;
	return 1;
}

int SlipDraw3D_ActiveBounds(const SlipDraw3DRecordPool *pool, SlipDraw3DActiveBoundsVisit *visits, size_t visitCapacity,
                            SlipDraw3DActiveBounds *result) {
	const uint8_t *recordPoolBytes;
	size_t recordPoolBytesCount;
	uint32_t headOffset;
	uint32_t recordOffset;
	int32_t minX;
	int32_t minY;
	int32_t maxX;
	int32_t maxY;
	size_t visitCount;

	if (pool == NULL || visits == NULL || result == NULL) {
		return 0;
	}
	recordPoolBytes = SlipDraw3D_RecordPoolConstBytes(pool);
	recordPoolBytesCount = SlipDraw3D_RecordPoolByteSize();
	headOffset = pool->inputActiveHeadOffset;
	if (recordPoolBytes == NULL || !SlipDraw3D_LinkedRecordOffsetValid(recordPoolBytesCount, headOffset) ||
	    visitCapacity == 0) {
		return 0;
	}
	minX = INT16_MAX;
	minY = INT16_MAX;
	maxX = SLIP_DRAW3D_ACTIVE_BOUNDS_MAXIMUM_INITIAL;
	maxY = SLIP_DRAW3D_ACTIVE_BOUNDS_MAXIMUM_INITIAL;
	recordOffset = headOffset;
	visitCount = 0;
	do {
		SlipDraw3DActiveBoundsVisit *visit;
		const uint8_t *record;
		int32_t screenX;
		int32_t screenY;
		uint32_t nextOffset;

		if (visitCount >= visitCapacity || !SlipDraw3D_LinkedRecordOffsetValid(recordPoolBytesCount, recordOffset)) {
			return 0;
		}
		record = recordPoolBytes + recordOffset;
		screenX = SlipBytes_ReadLEI32(record + offsetof(SlipDraw3DDrawRecord, screenX));
		if (screenX < minX) {
			minX = screenX;
		}
		if (screenX > maxX) {
			maxX = screenX;
		}
		screenY = SlipBytes_ReadLEI32(record + offsetof(SlipDraw3DDrawRecord, screenY));
		if (screenY < minY) {
			minY = screenY;
		}
		if (screenY > maxY) {
			maxY = screenY;
		}
		nextOffset = SlipDraw3D_RecordNext(recordPoolBytes, recordOffset);
		visit = visits + visitCount;
		*visit = (SlipDraw3DActiveBoundsVisit){.recordOffset = recordOffset,
		                                       .screenX = screenX,
		                                       .minXAfter = minX,
		                                       .maxXAfter = maxX,
		                                       .screenY = screenY,
		                                       .minYAfter = minY,
		                                       .maxYAfter = maxY,
		                                       .nextRecordOffset = nextOffset,
		                                       .loop = nextOffset != headOffset};
		recordOffset = nextOffset;
		++visitCount;
	} while (recordOffset != headOffset);
	*result = (SlipDraw3DActiveBounds){.activeHeadOffset = headOffset,
	                                   .minXInitial = INT16_MAX,
	                                   .minYInitial = INT16_MAX,
	                                   .maxXInitial = SLIP_DRAW3D_ACTIVE_BOUNDS_MAXIMUM_INITIAL,
	                                   .maxYInitial = SLIP_DRAW3D_ACTIVE_BOUNDS_MAXIMUM_INITIAL,
	                                   .visitCount = visitCount,
	                                   .minX = minX,
	                                   .minY = minY,
	                                   .maxX = maxX,
	                                   .maxY = maxY,
	                                   .returned = true};
	return 1;
}

static int SlipDraw3D_CountRecordRing(const uint8_t *recordPoolBytes, size_t recordPoolBytesCount, uint32_t headOffset,
                                      uint32_t *countOut) {
	uint32_t currentOffset;
	uint32_t count;

	if (recordPoolBytes == NULL || countOut == NULL ||
	    !SlipDraw3D_LinkedRecordOffsetValid(recordPoolBytesCount, headOffset)) {
		return 0;
	}
	currentOffset = headOffset;
	count = 0;
	do {
		if (!SlipDraw3D_LinkedRecordOffsetValid(recordPoolBytesCount, currentOffset)) {
			return 0;
		}
		currentOffset = SlipDraw3D_RecordNext(recordPoolBytes, currentOffset);
		++count;
	} while (currentOffset != headOffset);
	*countOut = count;
	return 1;
}

static int SlipDraw3D_MoveRecordAfterFreeHead(uint8_t *recordPoolBytes, size_t recordPoolBytesCount,
                                              uint32_t freeHeadOffset, uint32_t movingOffset, uint32_t *savedNextOut,
                                              uint32_t *previousOut, uint32_t *freeFirstOut) {
	uint32_t nextOffset;
	uint32_t previousOffset;
	uint32_t freeFirstOffset;

	if (recordPoolBytes == NULL || !SlipDraw3D_LinkedRecordOffsetValid(recordPoolBytesCount, freeHeadOffset) ||
	    !SlipDraw3D_LinkedRecordOffsetValid(recordPoolBytesCount, movingOffset)) {
		return 0;
	}
	nextOffset = SlipDraw3D_RecordNext(recordPoolBytes, movingOffset);
	previousOffset = SlipDraw3D_RecordPrev(recordPoolBytes, movingOffset);
	if (!SlipDraw3D_LinkedRecordOffsetValid(recordPoolBytesCount, nextOffset) ||
	    !SlipDraw3D_LinkedRecordOffsetValid(recordPoolBytesCount, previousOffset)) {
		return 0;
	}
	SlipDraw3D_SetRecordNext(recordPoolBytes, previousOffset, nextOffset);
	SlipDraw3D_SetRecordPrev(recordPoolBytes, nextOffset, previousOffset);
	freeFirstOffset = SlipDraw3D_RecordNext(recordPoolBytes, freeHeadOffset);
	if (!SlipDraw3D_LinkedRecordOffsetValid(recordPoolBytesCount, freeFirstOffset)) {
		return 0;
	}
	SlipDraw3D_SetRecordNext(recordPoolBytes, freeHeadOffset, movingOffset);
	SlipDraw3D_SetRecordPrev(recordPoolBytes, freeFirstOffset, movingOffset);
	SlipDraw3D_SetRecordNext(recordPoolBytes, movingOffset, freeFirstOffset);
	SlipDraw3D_SetRecordPrev(recordPoolBytes, movingOffset, freeHeadOffset);
	if (savedNextOut != NULL) {
		*savedNextOut = nextOffset;
	}
	if (previousOut != NULL) {
		*previousOut = previousOffset;
	}
	if (freeFirstOut != NULL) {
		*freeFirstOut = freeFirstOffset;
	}
	return 1;
}

int SlipDraw3D_CapturePostPlaneRing(SlipDraw3DRecordPool *pool, uint32_t postPlaneHead, uint32_t sourceOffset,
                                    uint32_t planePointX, uint32_t planePointY, uint32_t planePointZ,
                                    uint16_t planeNormalX, uint16_t planeNormalY, uint16_t planeNormalZ,
                                    uint32_t cameraLightX, uint32_t cameraLightY, uint32_t cameraLightZ,
                                    SlipDraw3DPostPlaneCaptureVisit *visits, size_t visitCapacity,
                                    SlipDraw3DPostPlaneCapture *result) {
	uint8_t *recordPoolBytes;
	size_t recordPoolBytesCount;
	SlipDraw3DActiveBoundsVisit boundsVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	SlipDraw3DActiveBounds bounds;
	SlipView3DDotProductQ14 dot;
	uint32_t dotOut;
	uint16_t planeLightDotWord;
	uint32_t planeScale;
	uint32_t currentOffset;
	uint32_t anyEdgeDelta;
	size_t visitCount;

	if (pool == NULL || result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->postPlaneHeadIn = postPlaneHead;
	result->sourceOffset = sourceOffset;
	result->planePointX = (int32_t)planePointX;
	result->planePointY = (int32_t)planePointY;
	result->planePointZ = (int32_t)planePointZ;
	result->planeNormalX = (int32_t)(int16_t)planeNormalX;
	result->planeNormalY = (int32_t)(int16_t)planeNormalY;
	result->planeNormalZ = (int32_t)(int16_t)planeNormalZ;
	result->lightX = cameraLightX;
	result->lightY = cameraLightY;
	result->lightZ = cameraLightZ;
	result->activeHeadIn = pool->inputActiveHeadOffset;
	result->activeHeadOut = pool->inputActiveHeadOffset;
	result->freeHead = pool->freeHeadOffset;
	result->postPlaneHeadOut = postPlaneHead;
	result->returned = true;
	if (postPlaneHead != 0) {
		result->existingPostPlane = true;
		result->clearedRejectCarry = true;
		result->carryOut = false;
		return 1;
	}
	dotOut = SlipView3D_DotProductQ14((uint16_t)(0u - cameraLightX), (uint16_t)(0u - cameraLightY),
	                                  (uint16_t)(0u - cameraLightZ), planeNormalX, planeNormalY, planeNormalZ, &dot);
	planeLightDotWord = (uint16_t)dotOut;
	result->dot = dot;
	result->planeLightDot = (int16_t)planeLightDotWord;
	if ((int16_t)planeLightDotWord < SLIP_DRAW3D_POST_PLANE_MINIMUM_LIGHT_DOT_Q14) {
		result->setRejectCarry = true;
		result->carryOut = true;
		return 1;
	}
	planeScale = (SLIP_Q14_ONE * SLIP_DRAW3D_SCALE_ONE_Q16) / (uint32_t)planeLightDotWord;
	result->planeScale = planeScale;
	result->calledActiveBounds = true;
	if (!SlipDraw3D_ActiveBounds(pool, boundsVisits, sizeof(boundsVisits) / sizeof(boundsVisits[0]), &bounds)) {
		return 0;
	}
	result->bounds = bounds;
	result->limitXMin = bounds.minX;
	result->limitYMin = bounds.minY;
	result->limitXMax = bounds.maxX;
	result->limitYMax = bounds.maxY;
	recordPoolBytes = SlipDraw3D_RecordPoolBytes(pool);
	recordPoolBytesCount = SlipDraw3D_RecordPoolByteSize();
	if (recordPoolBytes == NULL ||
	    !SlipDraw3D_LinkedRecordOffsetValid(recordPoolBytesCount, pool->inputActiveHeadOffset) ||
	    !SlipDraw3D_LinkedRecordOffsetValid(recordPoolBytesCount, pool->freeHeadOffset)) {
		return 0;
	}
	currentOffset = pool->inputActiveHeadOffset;
	anyEdgeDelta = 0;
	visitCount = 0;
	for (;;) {
		uint8_t *currentRecord;
		uint8_t *nextRecord;
		uint32_t nextOffset;
		uint32_t remainingCount;
		int32_t edgeX;
		int32_t edgeY;
		SlipDraw3DNormalizeVector2D normal;
		SlipDraw3DPostPlaneCaptureVisit *visit;

		if (visitCount >= visitCapacity || !SlipDraw3D_LinkedRecordOffsetValid(recordPoolBytesCount, currentOffset)) {
			return 0;
		}
		currentRecord = recordPoolBytes + currentOffset;
		nextOffset = SlipDraw3D_RecordNext(recordPoolBytes, currentOffset);
		if (!SlipDraw3D_LinkedRecordOffsetValid(recordPoolBytesCount, nextOffset)) {
			return 0;
		}
		nextRecord = recordPoolBytes + nextOffset;
		edgeY = (int32_t)((uint32_t)SlipBytes_ReadLEI32(nextRecord + offsetof(SlipDraw3DDrawRecord, screenY)) -
		                  (uint32_t)SlipBytes_ReadLEI32(currentRecord + offsetof(SlipDraw3DDrawRecord, screenY)));
		edgeX = (int32_t)((uint32_t)SlipBytes_ReadLEI32(nextRecord + offsetof(SlipDraw3DDrawRecord, screenX)) -
		                  (uint32_t)SlipBytes_ReadLEI32(currentRecord + offsetof(SlipDraw3DDrawRecord, screenX)));
		anyEdgeDelta |= (uint32_t)edgeX;
		anyEdgeDelta |= (uint32_t)edgeY;
		if (!SlipDraw3D_NormalizeVector2D((uint32_t)edgeX, (uint32_t)edgeY, &normal)) {
			return 0;
		}
		visit = visits + visitCount;
		*visit = (SlipDraw3DPostPlaneCaptureVisit){
		    .currentOffset = currentOffset, .nextOffset = nextOffset, .edgeX = edgeX, .edgeY = edgeY, .normal = normal};
		if ((int16_t)normal.length <= SLIP_DRAW3D_POST_PLANE_SHORT_EDGE_MAXIMUM_LENGTH) {
			uint32_t savedNextOffset;
			uint32_t previousOffset;
			uint32_t freeFirstOffset;

			if (!SlipDraw3D_MoveRecordAfterFreeHead(recordPoolBytes, recordPoolBytesCount, pool->freeHeadOffset,
			                                        nextOffset, &savedNextOffset, &previousOffset, &freeFirstOffset)) {
				return 0;
			}
			(void)savedNextOffset;
			(void)previousOffset;
			(void)freeFirstOffset;
			pool->inputActiveHeadOffset = currentOffset;
			if (!SlipDraw3D_CountRecordRing(recordPoolBytes, recordPoolBytesCount, pool->inputActiveHeadOffset,
			                                &remainingCount)) {
				return 0;
			}
			visit->removedShortEdge = true;
			visit->activeHeadAfterRemove = pool->inputActiveHeadOffset;
			visit->remainingCount = remainingCount;
			++visitCount;
			if (remainingCount < 3u) {
				result->visitCount = visitCount;
				result->anyEdgeDelta = anyEdgeDelta;
				result->activeHeadOut = pool->inputActiveHeadOffset;
				result->postPlaneHeadOut = 0;
				result->setRejectCarry = true;
				result->carryOut = true;
				return 1;
			}
			currentOffset = pool->inputActiveHeadOffset;
			continue;
		}
		SlipDraw3D_WriteI32(currentRecord + SLIP_DRAW3D_EDGE_NORMAL_X_OFFSET, -(int32_t)normal.unitYQ14);
		SlipDraw3D_WriteI32(currentRecord + SLIP_DRAW3D_EDGE_NORMAL_Y_OFFSET, (int32_t)normal.unitXQ14);
		visit->keptEdge = true;
		visit->storedNormalX = (int32_t)normal.unitXQ14;
		visit->storedNormalY = -(int32_t)normal.unitYQ14;
		currentOffset = nextOffset;
		visit->loop = currentOffset != pool->inputActiveHeadOffset;
		++visitCount;
		if (currentOffset == pool->inputActiveHeadOffset) {
			break;
		}
	}
	result->visitCount = visitCount;
	result->anyEdgeDelta = anyEdgeDelta;
	if (anyEdgeDelta == 0) {
		result->activeHeadOut = pool->inputActiveHeadOffset;
		result->postPlaneHeadOut = 0;
		result->setRejectCarry = true;
		result->carryOut = true;
		return 1;
	}
	result->postPlaneHeadOut = pool->inputActiveHeadOffset;
	pool->inputActiveHeadOffset = 0;
	result->activeHeadOut = pool->inputActiveHeadOffset;
	result->clearedRejectCarry = true;
	result->carryOut = false;
	return 1;
}

int SlipDraw3D_ReleasePostPlaneRing(SlipDraw3DRecordPool *pool, uint32_t postPlaneHead,
                                    SlipDraw3DPostPlaneReleaseVisit *visits, size_t visitCapacity,
                                    SlipDraw3DPostPlaneRelease *result) {
	uint8_t *recordPoolBytes;
	size_t recordPoolBytesCount;
	uint32_t currentOffset;
	size_t visitCount;

	if (pool == NULL || result == NULL) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->postPlaneHeadIn = postPlaneHead;
	result->freeHead = pool->freeHeadOffset;
	result->postPlaneHeadOut = 0;
	result->returned = true;
	if (postPlaneHead == 0) {
		return 1;
	}
	recordPoolBytes = SlipDraw3D_RecordPoolBytes(pool);
	recordPoolBytesCount = SlipDraw3D_RecordPoolByteSize();
	if (recordPoolBytes == NULL || !SlipDraw3D_LinkedRecordOffsetValid(recordPoolBytesCount, postPlaneHead) ||
	    !SlipDraw3D_LinkedRecordOffsetValid(recordPoolBytesCount, pool->freeHeadOffset)) {
		return 0;
	}
	currentOffset = postPlaneHead;
	visitCount = 0;
	for (;;) {
		uint32_t savedNextOffset;
		uint32_t previousOffset;
		uint32_t freeFirstOffset;
		SlipDraw3DPostPlaneReleaseVisit *visit;
		bool loop;

		if (visitCount >= visitCapacity || !SlipDraw3D_LinkedRecordOffsetValid(recordPoolBytesCount, currentOffset)) {
			return 0;
		}
		savedNextOffset = SlipDraw3D_RecordNext(recordPoolBytes, currentOffset);
		if (!SlipDraw3D_MoveRecordAfterFreeHead(recordPoolBytes, recordPoolBytesCount, pool->freeHeadOffset,
		                                        currentOffset, NULL, &previousOffset, &freeFirstOffset)) {
			return 0;
		}
		loop = savedNextOffset != postPlaneHead;
		visit = visits + visitCount;
		*visit = (SlipDraw3DPostPlaneReleaseVisit){.movedOffset = currentOffset,
		                                           .savedNextOffset = savedNextOffset,
		                                           .previousOffset = previousOffset,
		                                           .nextOffset = savedNextOffset,
		                                           .freeFirstOffset = freeFirstOffset,
		                                           .loop = loop};
		++visitCount;
		currentOffset = savedNextOffset;
		if (!loop) {
			break;
		}
	}
	result->visitCount = visitCount;
	return 1;
}

int SlipDraw3D_PrimitivePath(SlipDraw3DRecordPool *pool, SlipDraw3DVertexRecord *vertexRecords,
                             size_t vertexRecordCount, const uint8_t *indexStream, size_t indexStreamBytes,
                             uint16_t countAndFlags, const SlipDraw3DProjectState *state,
                             SlipDraw3DTransformFn transform, SlipDraw3DProjectFn project, void *userData,
                             int depthClipRejected, int screenClipRejected, SlipDraw3DReturnActiveVisit *returnVisits,
                             size_t returnVisitCapacity, SlipDraw3DActiveRingVisit *activeVisits,
                             size_t activeVisitCapacity, SlipDraw3DActiveBoundsVisit *boundsVisits,
                             size_t boundsVisitCapacity, SlipDraw3DPrimitivePath *result) {
	SlipDraw3DReturnActiveRing returnActive;
	SlipDraw3DActiveRingBuild activeRing;
	SlipDraw3DActiveBounds bounds;

	if (result == NULL) {
		return 0;
	}
	*result = (SlipDraw3DPrimitivePath){
	    .savedIndexStream = true, .savedMaterialStream = true, .savedCountAndFlags = true, .returned = true};
	result->callReturnActiveRing = true;
	if (!SlipDraw3D_ReturnActiveRing(pool, returnVisits, returnVisitCapacity, &returnActive)) {
		return 0;
	}
	result->returnActive = returnActive;
	result->callDraw3DBuildActiveRing = true;
	if (!SlipDraw3D_BuildActiveRing(pool, vertexRecords, vertexRecordCount, indexStream, indexStreamBytes,
	                                countAndFlags, state, transform, project, project, userData, depthClipRejected,
	                                screenClipRejected, activeVisits, activeVisitCapacity, &activeRing)) {
		return 0;
	}
	result->activeRing = activeRing;
	result->draw3DBuildActiveRingCarry = activeRing.carryOut;
	if (result->draw3DBuildActiveRingCarry) {
		result->setRejectCarry = true;
		result->carryOut = true;
		return 1;
	}
	result->callDraw3DActiveBounds = true;
	if (!SlipDraw3D_ActiveBounds(pool, boundsVisits, boundsVisitCapacity, &bounds)) {
		return 0;
	}
	result->bounds = bounds;
	result->minX = bounds.minX;
	result->minY = bounds.minY;
	result->maxX = bounds.maxX;
	result->maxY = bounds.maxY;
	result->clearedRejectCarry = true;
	result->carryOut = false;
	return 1;
}

int SlipDraw3D_PrimitivePathExecute(
    SlipDraw3DRecordPool *pool, SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCount,
    const uint8_t *indexStream, size_t indexStreamBytes, uint16_t countAndFlags, const SlipDraw3DProjectState *state,
    SlipDraw3DTransformFn transform, SlipDraw3DProjectFn projectPrimary, SlipDraw3DProjectFn projectSecondary,
    void *userData, int hasPostPlanes, const uint8_t *planeBase, size_t planeBytes, uint32_t planeHeadOffset,
    int32_t postLimitXMin, int32_t postLimitXMax, int32_t postLimitYMin, int32_t postLimitYMax,
    size_t maxClipEdgeVisits, SlipDraw3DReturnActiveVisit *returnVisits, size_t returnVisitCapacity,
    SlipDraw3DClipFlagVisit *clipFlagVisits, size_t clipFlagVisitCapacity,
    SlipDraw3DPostPlaneBoundsVisit *postBoundsVisits, size_t postBoundsVisitCapacity,
    SlipDraw3DPostPlaneClipRecordVisit *postClipRecordVisits, size_t postClipRecordVisitCapacity,
    SlipDraw3DPostPlaneClipPlaneVisit *postClipPlaneVisits, size_t postClipPlaneVisitCapacity,
    SlipDraw3DActiveRingVisit *activeVisits, size_t activeVisitCapacity, SlipDraw3DActiveBoundsVisit *boundsVisits,
    size_t boundsVisitCapacity, SlipDraw3DPrimitivePathExecute *result) {
	SlipDraw3DReturnActiveRing returnActive;
	SlipDraw3DActiveRingExecute activeRing;
	SlipDraw3DActiveBounds bounds;

	if (result == NULL) {
		return 0;
	}
	*result = (SlipDraw3DPrimitivePathExecute){
	    .savedIndexStream = true, .savedMaterialStream = true, .savedCountAndFlags = true, .returned = true};
	result->callReturnActiveRing = true;
	if (!SlipDraw3D_ReturnActiveRing(pool, returnVisits, returnVisitCapacity, &returnActive)) {
		return 0;
	}
	result->returnActive = returnActive;
	result->callDraw3DBuildActiveRing = true;
	if (!SlipDraw3D_BuildActiveRingExecute(
	        pool, vertexRecords, vertexRecordCount, indexStream, indexStreamBytes, countAndFlags, state, transform,
	        projectPrimary, projectSecondary, userData, hasPostPlanes, planeBase, planeBytes, planeHeadOffset,
	        postLimitXMin, postLimitXMax, postLimitYMin, postLimitYMax, maxClipEdgeVisits, clipFlagVisits,
	        clipFlagVisitCapacity, postBoundsVisits, postBoundsVisitCapacity, postClipRecordVisits,
	        postClipRecordVisitCapacity, postClipPlaneVisits, postClipPlaneVisitCapacity, activeVisits,
	        activeVisitCapacity, &activeRing)) {
		return 0;
	}
	result->activeRing = activeRing;
	result->draw3DBuildActiveRingCarry = activeRing.carryOut;
	if (result->draw3DBuildActiveRingCarry) {
		result->setRejectCarry = true;
		result->carryOut = true;
		return 1;
	}
	result->callDraw3DActiveBounds = true;
	if (!SlipDraw3D_ActiveBounds(pool, boundsVisits, boundsVisitCapacity, &bounds)) {
		return 0;
	}
	result->bounds = bounds;
	result->minX = bounds.minX;
	result->minY = bounds.minY;
	result->maxX = bounds.maxX;
	result->maxY = bounds.maxY;
	result->clearedRejectCarry = true;
	result->carryOut = false;
	return 1;
}

int SlipDraw3D_CollectClipFlags(const uint8_t *drawRecordBase, size_t drawRecordBytes, uint32_t headOffset,
                                SlipDraw3DClipFlagVisit *visits, size_t visitCapacity, SlipDraw3DClipFlags *result) {
	uint32_t recordOffset;
	uint32_t loopHeadOffset;
	uint32_t allClipFlags;
	uint32_t anyClipFlags;
	size_t visitCount;

	if (drawRecordBase == NULL || visits == NULL || result == NULL || (size_t)headOffset > drawRecordBytes ||
	    drawRecordBytes - (size_t)headOffset < SLIP_DRAW3D_DRAW_RECORD_SIZE) {
		return 0;
	}
	recordOffset = headOffset;
	loopHeadOffset = recordOffset;
	allClipFlags = SLIP_CLIP_ALL;
	anyClipFlags = 0;
	visitCount = 0;
	do {
		const uint8_t *record;
		uint32_t recordFlags;
		uint32_t next;
		SlipDraw3DClipFlagVisit *visit;

		if (visitCount >= visitCapacity || (size_t)recordOffset > drawRecordBytes ||
		    drawRecordBytes - (size_t)recordOffset < SLIP_DRAW3D_DRAW_RECORD_SIZE) {
			return 0;
		}
		record = drawRecordBase + recordOffset;
		recordFlags = SlipBytes_ReadLE32(record + SLIP_DRAW3D_VERTEX_RECORD_FLAGS_OFFSET);
		allClipFlags &= recordFlags;
		anyClipFlags |= recordFlags;
		next = SlipBytes_ReadLE32(record + SLIP_DRAW3D_RECORD_NEXT_OFFSET);
		visit = visits + visitCount;
		*visit = (SlipDraw3DClipFlagVisit){.recordOffset = recordOffset,
		                                   .flags = recordFlags,
		                                   .allFlagsAfterAnd = allClipFlags,
		                                   .anyFlagsAfter = anyClipFlags,
		                                   .nextRecordOffset = next,
		                                   .loop = next != loopHeadOffset};
		recordOffset = next;
		++visitCount;
	} while (recordOffset != loopHeadOffset);
	*result = (SlipDraw3DClipFlags){.headOffset = headOffset,
	                                .loopHeadOffset = loopHeadOffset,
	                                .allFlagsInitial = SLIP_CLIP_ALL,
	                                .anyFlagsInitial = 0,
	                                .visitCount = (uint32_t)visitCount,
	                                .allFlagsOut = allClipFlags,
	                                .anyFlagsOut = anyClipFlags,
	                                .returned = true};
	return 1;
}

int SlipDraw3D_ClipDispatch(uint32_t anyFlags, uint32_t allFlags, int depthClipRejected, int screenClipRejected,
                            SlipDraw3DClipDispatch *result) {
	bool anyMaskedZero;
	bool allMaskedNonzero;

	if (result == NULL) {
		return 0;
	}
	anyMaskedZero = (anyFlags & SLIP_DRAW3D_CLIP_MASK) == 0;
	allMaskedNonzero = (allFlags & SLIP_DRAW3D_CLIP_MASK) != 0;
	*result = (SlipDraw3DClipDispatch){.anyFlags = anyFlags,
	                                   .anyMaskedZero = anyMaskedZero,
	                                   .allFlags = allFlags,
	                                   .allMaskedNonzero = false,
	                                   .returned = true};
	if (!anyMaskedZero) {
		result->allMaskedNonzero = allMaskedNonzero;
		if (allMaskedNonzero) {
			result->setRejectCarry = true;
			result->carryOut = true;
			result->branch = SLIP_DRAW3D_CLIP_DISPATCH_BRANCH_REJECT;
			return 1;
		}
		result->calledClipDepth = true;
		result->depthClipRejected = depthClipRejected != 0;
		if (result->depthClipRejected) {
			result->carryOut = true;
			result->branch = SLIP_DRAW3D_CLIP_DISPATCH_BRANCH_CLIPPED_RETURN;
			return 1;
		}
	}
	result->calledClipScreen = true;
	result->screenClipRejected = screenClipRejected != 0;
	result->carryOut = result->screenClipRejected;
	result->branch = anyMaskedZero ? SLIP_DRAW3D_CLIP_DISPATCH_BRANCH_UNCLIPPED
	                               : SLIP_DRAW3D_CLIP_DISPATCH_BRANCH_CLIPPED_THEN_UNCLIPPED;
	return 1;
}

int SlipDraw3D_ClippedDepthDispatch(const SlipDraw3DClippedDepthInputs *inputs,
                                    SlipDraw3DClippedDepthDispatch *result) {
	uint32_t allClipFlags;
	uint32_t anyClipFlags;

	if (inputs == NULL || result == NULL) {
		return 0;
	}
	*result = (SlipDraw3DClippedDepthDispatch){.returned = true};
	allClipFlags = inputs->allFlagsEntry;
	anyClipFlags = inputs->anyFlagsEntry;
	result->auxiliaryClipEnabled = (inputs->renderFlags & SLIP_SHAPE_CLIP_AUXILIARY) != 0;
	if (result->auxiliaryClipEnabled) {
		result->anyFlagsCurrent = anyClipFlags;
		result->needsAuxiliaryClip = (anyClipFlags & SLIP_CLIP_AUXILIARY) != 0;
		if (result->needsAuxiliaryClip) {
			result->calledClipAuxiliaryEdge = true;
			result->rejectedAuxiliaryClip = inputs->rejectedAuxiliaryClip != 0;
			if (result->rejectedAuxiliaryClip) {
				result->setRejectCarry = true;
				result->carryOut = true;
				result->branch = SLIP_DRAW3D_CLIPPED_DEPTH_BRANCH_REJECT_2000;
				return 1;
			}
			result->targetOffsetAfterAuxiliaryClip = inputs->targetOffsetAfterAuxiliaryClip;
			result->otherOffsetAfterAuxiliaryClip = inputs->otherOffsetAfterAuxiliaryClip;
			result->calledSplitFirstAuxiliaryIntersection = true;
			result->calledSplitSecondAuxiliaryIntersection = true;
			result->calledScanAuxiliaryClipFlags = true;
			result->allFlagsAfterAuxiliaryScan = inputs->allFlagsFromAfter;
			allClipFlags = inputs->allFlagsFromAfter;
			anyClipFlags = inputs->anyFlagsFromAfter;
			if ((result->allFlagsAfterAuxiliaryScan & SLIP_CLIP_ALL) != 0) {
				result->rejectAfterAuxiliaryScan = true;
				result->setRejectCarry = true;
				result->carryOut = true;
				result->branch = SLIP_DRAW3D_CLIPPED_DEPTH_BRANCH_REJECT_AFTER_2000_SCAN;
				return 1;
			}
		}
	}
	result->anyFlagsAfterAuxiliaryScan = anyClipFlags;
	result->needsDepthClip = (anyClipFlags & SLIP_CLIP_DEPTH) != 0;
	if (result->needsDepthClip) {
		result->needsNearClip = (anyClipFlags & SLIP_CLIP_NEAR) != 0;
		if (result->needsNearClip) {
			result->savedAnyClipFlags = true;
			result->calledClipNearEdge = true;
			result->rejectedNearClip = inputs->rejectedNearClip != 0;
			if (result->rejectedNearClip) {
				result->setRejectCarry = true;
				result->carryOut = true;
				result->branch = SLIP_DRAW3D_CLIPPED_DEPTH_BRANCH_REJECT_0080;
				return 1;
			}
			result->targetOffsetAfterNearClip = inputs->targetOffsetAfterNearClip;
			result->otherOffsetAfterNearClip = inputs->otherOffsetAfterNearClip;
			result->firstNearDepthLimit = inputs->minimumDepth;
			result->secondNearDepthLimit = inputs->minimumDepth;
			result->calledSplitFirstNearIntersection = true;
			result->calledSplitSecondNearIntersection = true;
		}
		result->needsFarClip = (anyClipFlags & SLIP_CLIP_FAR) != 0;
		if (result->needsFarClip) {
			result->calledClipFarEdge = true;
			result->rejectedFarClip = inputs->rejectedFarClip != 0;
			if (result->rejectedFarClip) {
				result->setRejectCarry = true;
				result->carryOut = true;
				result->branch = SLIP_DRAW3D_CLIPPED_DEPTH_BRANCH_REJECT_0100;
				return 1;
			}
			result->targetOffsetAfterFarClip = inputs->targetOffsetAfterFarClip;
			result->otherOffsetAfterFarClip = inputs->otherOffsetAfterFarClip;
			result->firstFarDepthLimit = inputs->maximumDepth;
			result->secondFarDepthLimit = inputs->maximumDepth;
			result->calledSplitFirstFarIntersection = true;
			result->calledSplitSecondFarIntersection = true;
		}
		result->calledScanDepthClipFlags = true;
		allClipFlags = inputs->allFlagsFromFinal;
		result->anyFlagsAfterFinalScan = inputs->anyFlagsFromFinal;
	}
	result->allFlagsFinalCheck = allClipFlags;
	if ((result->allFlagsFinalCheck & SLIP_CLIP_SCREEN) != 0) {
		result->rejectFinalScan = true;
		result->setRejectCarry = true;
		result->carryOut = true;
		result->branch = SLIP_DRAW3D_CLIPPED_DEPTH_BRANCH_REJECT_FINAL_SCAN;
		return 1;
	}
	result->clearedRejectCarry = true;
	result->carryOut = false;
	result->branch = SLIP_DRAW3D_CLIPPED_DEPTH_BRANCH_ACCEPT;
	return 1;
}

int SlipDraw3D_ClippedDepthExecute(uint8_t *recordBase, size_t recordBytes, uint32_t inputActiveHeadOffset,
                                   uint32_t freeHeadOffset, uint32_t allFlagsEntry, uint32_t anyFlagsEntry,
                                   uint32_t renderFlags, uint32_t projectionMode, int32_t limitZMin, int32_t limitZMax,
                                   int32_t limitXMin, int32_t limitXMax, int32_t limitYMin, int32_t limitYMax,
                                   SlipDraw3DProjectFn projectPrimary, SlipDraw3DProjectFn projectSecondary,
                                   void *userData, size_t maxClipEdgeVisits, SlipDraw3DClipFlagVisit *clipFlagVisits,
                                   size_t clipFlagVisitCapacity, SlipDraw3DClippedDepthExecute *result) {
	uint32_t activeHeadOffset;
	uint32_t allClipFlags;
	uint32_t anyClipFlags;
	SlipDraw3DClippedDepthDispatch *dispatch;

	if (recordBase == NULL || result == NULL || maxClipEdgeVisits == 0 ||
	    !SlipDraw3D_LinkedRecordOffsetValid(recordBytes, inputActiveHeadOffset) ||
	    !SlipDraw3D_LinkedRecordOffsetValid(recordBytes, freeHeadOffset)) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	activeHeadOffset = inputActiveHeadOffset;
	allClipFlags = allFlagsEntry;
	anyClipFlags = anyFlagsEntry;
	result->activeHeadOffsetIn = inputActiveHeadOffset;
	result->activeHeadOffsetOut = inputActiveHeadOffset;
	result->freeHeadOffset = freeHeadOffset;
	result->allFlagsOut = allClipFlags;
	result->anyFlagsOut = anyClipFlags;
	dispatch = &result->dispatch;
	dispatch->returned = true;
	dispatch->auxiliaryClipEnabled = (renderFlags & SLIP_SHAPE_CLIP_AUXILIARY) != 0;
	if (dispatch->auxiliaryClipEnabled) {
		dispatch->anyFlagsCurrent = anyClipFlags;
		dispatch->needsAuxiliaryClip = (anyClipFlags & SLIP_CLIP_AUXILIARY) != 0;
		if (dispatch->needsAuxiliaryClip) {
			dispatch->calledClipAuxiliaryEdge = true;
			if (!SlipDraw3D_ClipEdgeList(recordBase, recordBytes, activeHeadOffset, freeHeadOffset, SLIP_CLIP_AUXILIARY,
			                             maxClipEdgeVisits, &result->auxiliaryClip)) {
				return 0;
			}
			dispatch->rejectedAuxiliaryClip = result->auxiliaryClip.carryOut;
			if (dispatch->rejectedAuxiliaryClip) {
				dispatch->setRejectCarry = true;
				dispatch->carryOut = true;
				dispatch->branch = SLIP_DRAW3D_CLIPPED_DEPTH_BRANCH_REJECT_2000;
				result->activeHeadOffsetOut = activeHeadOffset;
				result->allFlagsOut = allClipFlags;
				result->anyFlagsOut = anyClipFlags;
				return 1;
			}
			activeHeadOffset = result->auxiliaryClip.headOffsetOut;
			dispatch->targetOffsetAfterAuxiliaryClip = result->auxiliaryClip.targetOffsetOut;
			dispatch->otherOffsetAfterAuxiliaryClip = result->auxiliaryClip.otherOffsetOut;
			dispatch->calledSplitFirstAuxiliaryIntersection = true;
			if (!SlipDraw3D_SplitDepthMathRecordWithCallback(
			        recordBase, recordBytes, result->auxiliaryClip.firstInsideOffset,
			        result->auxiliaryClip.firstOutsideOffset, renderFlags, projectionMode, projectPrimary,
			        projectSecondary, userData, limitZMin, limitZMax, limitXMin, limitXMax, limitYMin, limitYMax,
			        &result->firstAuxiliarySplit)) {
				return 0;
			}
			dispatch->calledSplitSecondAuxiliaryIntersection = true;
			if (!SlipDraw3D_SplitDepthMathRecordWithCallback(
			        recordBase, recordBytes, result->auxiliaryClip.targetOffsetOut,
			        result->auxiliaryClip.otherOffsetOut, renderFlags, projectionMode, projectPrimary, projectSecondary,
			        userData, limitZMin, limitZMax, limitXMin, limitXMax, limitYMin, limitYMax,
			        &result->secondAuxiliarySplit)) {
				return 0;
			}
			dispatch->calledScanAuxiliaryClipFlags = true;
			if (!SlipDraw3D_CollectClipFlags(recordBase, recordBytes, activeHeadOffset, clipFlagVisits,
			                                 clipFlagVisitCapacity, &result->clipFlagsAfter)) {
				return 0;
			}
			allClipFlags = result->clipFlagsAfter.allFlagsOut;
			anyClipFlags = result->clipFlagsAfter.anyFlagsOut;
			dispatch->allFlagsAfterAuxiliaryScan = allClipFlags;
			if ((dispatch->allFlagsAfterAuxiliaryScan & SLIP_CLIP_ALL) != 0) {
				dispatch->rejectAfterAuxiliaryScan = true;
				dispatch->setRejectCarry = true;
				dispatch->carryOut = true;
				dispatch->branch = SLIP_DRAW3D_CLIPPED_DEPTH_BRANCH_REJECT_AFTER_2000_SCAN;
				result->activeHeadOffsetOut = activeHeadOffset;
				result->allFlagsOut = allClipFlags;
				result->anyFlagsOut = anyClipFlags;
				return 1;
			}
		}
	}
	dispatch->anyFlagsAfterAuxiliaryScan = anyClipFlags;
	dispatch->needsDepthClip = (anyClipFlags & SLIP_CLIP_DEPTH) != 0;
	if (dispatch->needsDepthClip) {
		dispatch->needsNearClip = (anyClipFlags & SLIP_CLIP_NEAR) != 0;
		if (dispatch->needsNearClip) {
			dispatch->savedAnyClipFlags = true;
			dispatch->calledClipNearEdge = true;
			if (!SlipDraw3D_ClipEdgeList(recordBase, recordBytes, activeHeadOffset, freeHeadOffset, SLIP_CLIP_NEAR,
			                             maxClipEdgeVisits, &result->nearClip)) {
				return 0;
			}
			dispatch->rejectedNearClip = result->nearClip.carryOut;
			if (dispatch->rejectedNearClip) {
				dispatch->setRejectCarry = true;
				dispatch->carryOut = true;
				dispatch->branch = SLIP_DRAW3D_CLIPPED_DEPTH_BRANCH_REJECT_0080;
				result->activeHeadOffsetOut = activeHeadOffset;
				result->allFlagsOut = allClipFlags;
				result->anyFlagsOut = anyClipFlags;
				return 1;
			}
			activeHeadOffset = result->nearClip.headOffsetOut;
			dispatch->targetOffsetAfterNearClip = result->nearClip.targetOffsetOut;
			dispatch->otherOffsetAfterNearClip = result->nearClip.otherOffsetOut;
			dispatch->firstNearDepthLimit = (uint32_t)limitZMin;
			dispatch->secondNearDepthLimit = (uint32_t)limitZMin;
			dispatch->calledSplitFirstNearIntersection = true;
			if (!SlipDraw3D_SplitDepthRecordWithCallback(
			        recordBase, recordBytes, result->nearClip.firstInsideOffset, result->nearClip.firstOutsideOffset,
			        renderFlags, projectionMode, limitZMin, projectPrimary, projectSecondary, userData, limitXMin,
			        limitXMax, limitYMin, limitYMax, &result->firstNearSplit)) {
				return 0;
			}
			dispatch->calledSplitSecondNearIntersection = true;
			if (!SlipDraw3D_SplitDepthRecordWithCallback(
			        recordBase, recordBytes, result->nearClip.targetOffsetOut, result->nearClip.otherOffsetOut,
			        renderFlags, projectionMode, limitZMin, projectPrimary, projectSecondary, userData, limitXMin,
			        limitXMax, limitYMin, limitYMax, &result->secondNearSplit)) {
				return 0;
			}
		}
		dispatch->needsFarClip = (anyClipFlags & SLIP_CLIP_FAR) != 0;
		if (dispatch->needsFarClip) {
			dispatch->calledClipFarEdge = true;
			if (!SlipDraw3D_ClipEdgeList(recordBase, recordBytes, activeHeadOffset, freeHeadOffset, SLIP_CLIP_FAR,
			                             maxClipEdgeVisits, &result->farClip)) {
				return 0;
			}
			dispatch->rejectedFarClip = result->farClip.carryOut;
			if (dispatch->rejectedFarClip) {
				dispatch->setRejectCarry = true;
				dispatch->carryOut = true;
				dispatch->branch = SLIP_DRAW3D_CLIPPED_DEPTH_BRANCH_REJECT_0100;
				result->activeHeadOffsetOut = activeHeadOffset;
				result->allFlagsOut = allClipFlags;
				result->anyFlagsOut = anyClipFlags;
				return 1;
			}
			activeHeadOffset = result->farClip.headOffsetOut;
			dispatch->targetOffsetAfterFarClip = result->farClip.targetOffsetOut;
			dispatch->otherOffsetAfterFarClip = result->farClip.otherOffsetOut;
			dispatch->firstFarDepthLimit = (uint32_t)limitZMax;
			dispatch->secondFarDepthLimit = (uint32_t)limitZMax;
			dispatch->calledSplitFirstFarIntersection = true;
			if (!SlipDraw3D_SplitDepthRecordWithCallback(
			        recordBase, recordBytes, result->farClip.firstInsideOffset, result->farClip.firstOutsideOffset,
			        renderFlags, projectionMode, limitZMax, projectPrimary, projectSecondary, userData, limitXMin,
			        limitXMax, limitYMin, limitYMax, &result->firstFarSplit)) {
				return 0;
			}
			dispatch->calledSplitSecondFarIntersection = true;
			if (!SlipDraw3D_SplitDepthRecordWithCallback(
			        recordBase, recordBytes, result->farClip.targetOffsetOut, result->farClip.otherOffsetOut,
			        renderFlags, projectionMode, limitZMax, projectPrimary, projectSecondary, userData, limitXMin,
			        limitXMax, limitYMin, limitYMax, &result->secondFarSplit)) {
				return 0;
			}
		}
		dispatch->calledScanDepthClipFlags = true;
		if (!SlipDraw3D_CollectClipFlags(recordBase, recordBytes, activeHeadOffset, clipFlagVisits,
		                                 clipFlagVisitCapacity, &result->clipFlagsFinal)) {
			return 0;
		}
		allClipFlags = result->clipFlagsFinal.allFlagsOut;
		anyClipFlags = result->clipFlagsFinal.anyFlagsOut;
		dispatch->anyFlagsAfterFinalScan = anyClipFlags;
	}
	dispatch->allFlagsFinalCheck = allClipFlags;
	if ((dispatch->allFlagsFinalCheck & SLIP_CLIP_SCREEN) != 0) {
		dispatch->rejectFinalScan = true;
		dispatch->setRejectCarry = true;
		dispatch->carryOut = true;
		dispatch->branch = SLIP_DRAW3D_CLIPPED_DEPTH_BRANCH_REJECT_FINAL_SCAN;
		result->activeHeadOffsetOut = activeHeadOffset;
		result->allFlagsOut = allClipFlags;
		result->anyFlagsOut = anyClipFlags;
		return 1;
	}
	dispatch->clearedRejectCarry = true;
	dispatch->carryOut = false;
	dispatch->branch = SLIP_DRAW3D_CLIPPED_DEPTH_BRANCH_ACCEPT;
	result->activeHeadOffsetOut = activeHeadOffset;
	result->allFlagsOut = allClipFlags;
	result->anyFlagsOut = anyClipFlags;
	return 1;
}

int SlipDraw3D_ScreenPlaneDispatch(const SlipDraw3DScreenPlaneInputs *inputs, SlipDraw3DScreenPlaneDispatch *result) {
	uint32_t anyClipFlags;

	if (inputs == NULL || result == NULL) {
		return 0;
	}
	*result = (SlipDraw3DScreenPlaneDispatch){.returned = true};
	result->hasScreenClipFlags = (inputs->anyFlagsEntry & SLIP_CLIP_SCREEN) != 0;
	if (result->hasScreenClipFlags) {
		anyClipFlags = inputs->anyFlagsEntry;
		result->anyFlagsInitial = anyClipFlags;
		result->needsLeftClip = (anyClipFlags & SLIP_CLIP_LEFT) != 0;
		if (result->needsLeftClip) {
			result->calledClipLeftEdge = true;
			result->rejectedLeftClip = inputs->rejectedLeftClip != 0;
			if (result->rejectedLeftClip) {
				result->setRejectCarry = true;
				result->carryOut = true;
				result->branch = SLIP_DRAW3D_SCREEN_PLANE_BRANCH_REJECT_0008;
				return 1;
			}
			result->targetOffsetAfterLeftClip = inputs->targetOffsetAfterLeftClip;
			result->otherOffsetAfterLeftClip = inputs->otherOffsetAfterLeftClip;
			result->firstLeftScreenLimit = inputs->clipMinX;
			result->secondLeftScreenLimit = inputs->clipMinX;
			result->calledSplitFirstLeftIntersection = true;
			result->calledSplitSecondLeftIntersection = true;
		}
		result->needsRightClip = (anyClipFlags & SLIP_CLIP_RIGHT) != 0;
		if (result->needsRightClip) {
			result->calledClipRightEdge = true;
			result->rejectedRightClip = inputs->rejectedRightClip != 0;
			if (result->rejectedRightClip) {
				result->setRejectCarry = true;
				result->carryOut = true;
				result->branch = SLIP_DRAW3D_SCREEN_PLANE_BRANCH_REJECT_0010;
				return 1;
			}
			result->targetOffsetAfterRightClip = inputs->targetOffsetAfterRightClip;
			result->otherOffsetAfterRightClip = inputs->otherOffsetAfterRightClip;
			result->firstRightScreenLimit = inputs->clipMaxX;
			result->secondRightScreenLimit = inputs->clipMaxX;
			result->calledSplitFirstRightIntersection = true;
			result->calledSplitSecondRightIntersection = true;
		}
		result->calledScanHorizontalClipFlags = true;
		result->allFlagsAfterHorizontalScan = inputs->allFlagsFromAfter;
		result->anyFlagsAfterHorizontalScan = inputs->anyFlagsFromAfter;
		if ((result->allFlagsAfterHorizontalScan & SLIP_CLIP_VERTICAL) != 0) {
			result->rejectAfterHorizontalScan = true;
			result->setRejectCarry = true;
			result->carryOut = true;
			result->branch = SLIP_DRAW3D_SCREEN_PLANE_BRANCH_REJECT_AFTER_0018_SCAN;
			return 1;
		}
		anyClipFlags = inputs->anyFlagsFromAfter;
		result->anyFlagsAfterHorizontalScan = anyClipFlags;
		result->needsTopClip = (anyClipFlags & SLIP_CLIP_TOP) != 0;
		if (result->needsTopClip) {
			result->calledClipTopEdge = true;
			result->rejectedTopClip = inputs->rejectedTopClip != 0;
			if (result->rejectedTopClip) {
				result->setRejectCarry = true;
				result->carryOut = true;
				result->branch = SLIP_DRAW3D_SCREEN_PLANE_BRANCH_REJECT_0020;
				return 1;
			}
			result->targetOffsetAfterTopClip = inputs->targetOffsetAfterTopClip;
			result->otherOffsetAfterTopClip = inputs->otherOffsetAfterTopClip;
			result->firstTopScreenLimit = inputs->clipMinY;
			result->secondTopScreenLimit = inputs->clipMinY;
			result->calledSplitFirstTopIntersection = true;
			result->calledSplitSecondTopIntersection = true;
		}
		result->needsBottomClip = (anyClipFlags & SLIP_CLIP_BOTTOM) != 0;
		if (result->needsBottomClip) {
			result->calledClipBottomEdge = true;
			result->rejectedBottomClip = inputs->rejectedBottomClip != 0;
			if (result->rejectedBottomClip) {
				result->setRejectCarry = true;
				result->carryOut = true;
				result->branch = SLIP_DRAW3D_SCREEN_PLANE_BRANCH_REJECT_0040;
				return 1;
			}
			result->targetOffsetAfterBottomClip = inputs->targetOffsetAfterBottomClip;
			result->otherOffsetAfterBottomClip = inputs->otherOffsetAfterBottomClip;
			result->firstBottomScreenLimit = inputs->clipMaxY;
			result->secondBottomScreenLimit = inputs->clipMaxY;
			result->calledSplitFirstBottomIntersection = true;
			result->calledSplitSecondBottomIntersection = true;
		}
	}
	result->hasPostPlanes = inputs->postPlaneHead != 0;
	if (result->hasPostPlanes) {
		result->calledPostPlaneBounds = true;
		result->postPlaneBoundsRejected = inputs->postPlaneBoundsRejected != 0;
		if (result->postPlaneBoundsRejected) {
			result->setRejectCarry = true;
			result->carryOut = true;
			result->branch = SLIP_DRAW3D_SCREEN_PLANE_BRANCH_REJECT_1DE5C;
			return 1;
		}
		result->calledPostPlaneClip = true;
		result->postPlaneClipRejected = inputs->postPlaneClipRejected != 0;
		if (result->postPlaneClipRejected) {
			result->setRejectCarry = true;
			result->carryOut = true;
			result->branch = SLIP_DRAW3D_SCREEN_PLANE_BRANCH_REJECT_1DEB6;
			return 1;
		}
	}
	result->clearedRejectCarry = true;
	result->carryOut = false;
	result->branch = SLIP_DRAW3D_SCREEN_PLANE_BRANCH_ACCEPT;
	return 1;
}

int SlipDraw3D_ClipEdgeList(uint8_t *recordBase, size_t recordBytes, uint32_t headOffset, uint32_t freeHeadOffset,
                            uint32_t clipMask, size_t maxVisits, SlipDraw3DClipEdge *result) {
	uint32_t firstInsideOffset;
	uint32_t firstOutsideOffset;
	uint32_t scanOffset;
	uint32_t nextOffset;
	uint32_t lastInsideOffset;
	uint32_t previousInsideOffset;
	uint32_t movingOffset;
	size_t visits;

	if (recordBase == NULL || result == NULL || clipMask == 0 || maxVisits == 0 ||
	    !SlipDraw3D_LinkedRecordOffsetValid(recordBytes, headOffset) ||
	    !SlipDraw3D_LinkedRecordOffsetValid(recordBytes, freeHeadOffset)) {
		return 0;
	}
	*result = (SlipDraw3DClipEdge){.savedRingHead = true,
	                               .clipMask = clipMask,
	                               .headOffsetIn = headOffset,
	                               .freeHeadOffset = freeHeadOffset,
	                               .returned = true};

	scanOffset = headOffset;
	visits = 0;
	for (;;) {
		uint32_t scanFlags;
		uint32_t nextFlags;

		if (!SlipDraw3D_LinkedRecordOffsetValid(recordBytes, scanOffset)) {
			return 0;
		}
		nextOffset = SlipDraw3D_RecordNext(recordBase, scanOffset);
		if (!SlipDraw3D_LinkedRecordOffsetValid(recordBytes, nextOffset)) {
			return 0;
		}
		scanFlags = SlipBytes_ReadLE32(recordBase + scanOffset + SLIP_DRAW3D_VERTEX_RECORD_FLAGS_OFFSET);
		nextFlags = SlipBytes_ReadLE32(recordBase + nextOffset + SLIP_DRAW3D_VERTEX_RECORD_FLAGS_OFFSET);
		if ((scanFlags & clipMask) != 0 && (nextFlags & clipMask) == 0) {
			break;
		}
		scanOffset = nextOffset;
		++visits;
		if (visits >= maxVisits) {
			return 0;
		}
	}
	firstInsideOffset = scanOffset;
	firstOutsideOffset = nextOffset;
	result->firstInsideOffset = firstInsideOffset;
	result->firstOutsideOffset = firstOutsideOffset;

	scanOffset = firstOutsideOffset;
	for (;;) {
		uint32_t scanFlags;

		if (!SlipDraw3D_LinkedRecordOffsetValid(recordBytes, scanOffset)) {
			return 0;
		}
		nextOffset = SlipDraw3D_RecordNext(recordBase, scanOffset);
		if (!SlipDraw3D_LinkedRecordOffsetValid(recordBytes, nextOffset)) {
			return 0;
		}
		lastInsideOffset = scanOffset;
		scanFlags = SlipBytes_ReadLE32(recordBase + scanOffset + SLIP_DRAW3D_VERTEX_RECORD_FLAGS_OFFSET);
		if ((scanFlags & clipMask) != 0) {
			break;
		}
		scanOffset = nextOffset;
		++visits;
		if (visits >= maxVisits) {
			return 0;
		}
	}
	result->nextInsideOffset = scanOffset;

	for (;;) {
		uint32_t scanFlags;

		if (!SlipDraw3D_LinkedRecordOffsetValid(recordBytes, scanOffset)) {
			return 0;
		}
		nextOffset = SlipDraw3D_RecordNext(recordBase, scanOffset);
		if (!SlipDraw3D_LinkedRecordOffsetValid(recordBytes, nextOffset)) {
			return 0;
		}
		scanFlags = SlipBytes_ReadLE32(recordBase + scanOffset + SLIP_DRAW3D_VERTEX_RECORD_FLAGS_OFFSET);
		if ((scanFlags & clipMask) == 0) {
			break;
		}
		scanOffset = nextOffset;
		++visits;
		if (visits >= maxVisits) {
			return 0;
		}
	}
	result->compareOffset = scanOffset;
	if (scanOffset != firstOutsideOffset) {
		result->targetOffsetOut = lastInsideOffset;
		result->headCandidateOffsetOut = firstOutsideOffset;
		result->setRejectCarry = true;
		result->carryOut = true;
		result->branch = SLIP_DRAW3D_CLIP_EDGE_BRANCH_REJECT_MULTIPLE_SPANS;
		return 1;
	}

	result->savedOtherRecord = true;
	result->savedTargetRecord = true;
	previousInsideOffset = SlipDraw3D_RecordPrev(recordBase, firstInsideOffset);
	if (!SlipDraw3D_LinkedRecordOffsetValid(recordBytes, previousInsideOffset)) {
		return 0;
	}
	result->previousOfFirstInside = previousInsideOffset;
	result->previousOfFirstInsideMasked =
	    (SlipBytes_ReadLE32(recordBase + previousInsideOffset + SLIP_DRAW3D_VERTEX_RECORD_FLAGS_OFFSET) & clipMask) !=
	    0;
	if (!result->previousOfFirstInsideMasked) {
		uint32_t borrowed;
		uint32_t freeNext;
		uint32_t originalInside;
		uint32_t originalOutside;
		uint32_t previous;
		size_t i;

		originalInside = firstInsideOffset;
		originalOutside = firstOutsideOffset;
		borrowed = SlipDraw3D_RecordNext(recordBase, freeHeadOffset);
		if (!SlipDraw3D_LinkedRecordOffsetValid(recordBytes, borrowed)) {
			return 0;
		}
		freeNext = SlipDraw3D_RecordNext(recordBase, borrowed);
		if (!SlipDraw3D_LinkedRecordOffsetValid(recordBytes, freeNext)) {
			return 0;
		}
		SlipDraw3D_SetRecordNext(recordBase, freeHeadOffset, freeNext);
		SlipDraw3D_SetRecordPrev(recordBase, freeNext, freeHeadOffset);
		result->borrowedFromFreeList = true;
		result->borrowedOffset = borrowed;
		result->freeNextAfterBorrow = freeNext;
		result->borrowedDrawRecordOffset = borrowed;

		SlipDraw3D_SetRecordNext(recordBase, borrowed, originalInside);
		previous = SlipDraw3D_RecordPrev(recordBase, originalInside);
		if (!SlipDraw3D_LinkedRecordOffsetValid(recordBytes, previous)) {
			return 0;
		}
		SlipDraw3D_SetRecordPrev(recordBase, originalInside, borrowed);
		SlipDraw3D_SetRecordNext(recordBase, previous, borrowed);
		SlipDraw3D_SetRecordPrev(recordBase, borrowed, previous);
		for (i = 0; i < offsetof(SlipDraw3DLinkedDrawRecordLinks, nextOffset) / sizeof(uint32_t); ++i) {
			uint32_t dwordValue;

			dwordValue = SlipBytes_ReadLE32(recordBase + originalInside + i * sizeof(uint32_t));
			SlipDraw3D_WriteLE32(recordBase + borrowed + i * sizeof(uint32_t), dwordValue);
		}
		result->copiedDrawPayload = true;
		result->targetOffsetOut = borrowed;
		result->otherOffsetOut = previous;
		result->headCandidateOffsetOut = originalOutside;
		result->headOffsetOut = originalOutside;
		result->clearedRejectCarry = true;
		result->carryOut = false;
		result->branch = SLIP_DRAW3D_CLIP_EDGE_BRANCH_BORROW_AND_COPY_PREVIOUS_OUTSIDE;
		return 1;
	}

	movingOffset = previousInsideOffset;
	for (;;) {
		uint32_t previous;
		uint32_t previousFlags;

		if (!SlipDraw3D_LinkedRecordOffsetValid(recordBytes, movingOffset)) {
			return 0;
		}
		previous = SlipDraw3D_RecordPrev(recordBase, movingOffset);
		if (!SlipDraw3D_LinkedRecordOffsetValid(recordBytes, previous)) {
			return 0;
		}
		previousFlags = SlipBytes_ReadLE32(recordBase + previous + SLIP_DRAW3D_VERTEX_RECORD_FLAGS_OFFSET);
		if ((previousFlags & clipMask) == 0) {
			result->targetOffsetOut = movingOffset;
			result->otherOffsetOut = previous;
			result->headCandidateOffsetOut = firstOutsideOffset;
			result->headOffsetOut = firstOutsideOffset;
			result->clearedRejectCarry = true;
			result->carryOut = false;
			result->branch = SLIP_DRAW3D_CLIP_EDGE_BRANCH_MOVE_INTERIOR_PREVIOUS_INSIDE;
			return 1;
		}
		{
			uint32_t next;
			uint32_t freeFirst;

			next = SlipDraw3D_RecordNext(recordBase, movingOffset);
			if (!SlipDraw3D_LinkedRecordOffsetValid(recordBytes, next)) {
				return 0;
			}
			SlipDraw3D_SetRecordNext(recordBase, previous, next);
			SlipDraw3D_SetRecordPrev(recordBase, next, previous);
			freeFirst = SlipDraw3D_RecordNext(recordBase, freeHeadOffset);
			if (!SlipDraw3D_LinkedRecordOffsetValid(recordBytes, freeFirst)) {
				return 0;
			}
			SlipDraw3D_SetRecordNext(recordBase, freeHeadOffset, movingOffset);
			SlipDraw3D_SetRecordPrev(recordBase, freeFirst, movingOffset);
			SlipDraw3D_SetRecordNext(recordBase, movingOffset, freeFirst);
			SlipDraw3D_SetRecordPrev(recordBase, movingOffset, freeHeadOffset);
		}
		movingOffset = previous;
		++result->movedInteriorCount;
		++visits;
		if (visits >= maxVisits) {
			return 0;
		}
	}
}

uint32_t SlipDraw3D_ProjectDrawRecordPoint(SlipDraw3DDrawRecord *record, SlipDraw3DVec32 point,
                                           SlipDraw3DProjectState *state) {
	uint32_t flags = SLIP_VERTEX_TRANSFORMED;
	if (point.z < state->minZ)
		flags |= SLIP_CLIP_NEAR;
	if (point.z > state->maxZ)
		flags |= SLIP_CLIP_FAR;
	if ((state->renderFlags & SLIP_SHAPE_CLIP_AUXILIARY) != 0) {

		const int32_t x = (int32_t)((uint32_t)point.x - (uint32_t)state->auxiliaryClipPlaneOrigin.x);
		const int32_t y = (int32_t)((uint32_t)point.y - (uint32_t)state->auxiliaryClipPlaneOrigin.y);
		const int32_t z = (int32_t)((uint32_t)point.z - (uint32_t)state->auxiliaryClipPlaneOrigin.z);
		uint64_t dot = (uint64_t)((int64_t)x * state->auxiliaryClipPlaneNormal.x);
		dot += (uint64_t)((int64_t)y * state->auxiliaryClipPlaneNormal.y);
		dot += (uint64_t)((int64_t)z * state->auxiliaryClipPlaneNormal.z);
		record->depth = (int32_t)((uint32_t)(dot >> SLIP_Q14_FRACTION_BITS) +
		                          (uint32_t)((dot >> (SLIP_Q14_FRACTION_BITS - 1)) & 1u));
		if (record->depth < 0)
			flags |= SLIP_CLIP_AUXILIARY;
	}
	record->world = point;

	if ((flags & SLIP_CLIP_BEFORE_PROJECTION) != 0) {
		record->flags = flags;
		return flags;
	}
	int32_t x, y;
	if ((state->renderFlags & SLIP_SHAPE_FORCE_SECONDARY_PROJECTION) != 0)
		state->projectSecondary(point, &x, &y, state);
	else
		state->projectPrimary(point, &x, &y, state);

	flags = SLIP_VERTEX_PROJECTED;
	if (x < state->minX)
		flags |= SLIP_CLIP_LEFT;
	if (x > state->maxX)
		flags |= SLIP_CLIP_RIGHT;
	if (y < state->minY)
		flags |= SLIP_CLIP_TOP;
	if (y > state->maxY)
		flags |= SLIP_CLIP_BOTTOM;
	if ((flags & SLIP_CLIP_SCREEN) != 0 && x < SLIP_SCREEN_CLIP_COORDINATE_LIMIT &&
	    y < SLIP_SCREEN_CLIP_COORDINATE_LIMIT && x > -SLIP_SCREEN_CLIP_COORDINATE_LIMIT &&
	    y > -SLIP_SCREEN_CLIP_COORDINATE_LIMIT)
		flags |= SLIP_VERTEX_SCREEN_CLIP_IN_RANGE;
	record->flags = flags;
	record->screenX = x;
	record->screenY = y;
	return flags;
}

int SlipDraw3D_ProjectFlags(uint8_t *recordBase, size_t recordBytes, uint32_t recordOffset, uint32_t renderFlags,
                            int32_t projectedScreenX, int32_t projectedScreenY, int32_t limitXMin, int32_t limitXMax,
                            int32_t limitYMin, int32_t limitYMax, SlipDraw3DProjectFlags *result) {
	uint8_t *record;
	uint32_t screenClipFlags;
	uint32_t flagsIn;

	if (recordBase == NULL || result == NULL || (size_t)recordOffset > recordBytes ||
	    recordBytes - (size_t)recordOffset < SLIP_DRAW3D_DRAW_RECORD_SIZE) {
		return 0;
	}
	record = recordBase + recordOffset;
	*result =
	    (SlipDraw3DProjectFlags){.calledPrimaryProjection = (renderFlags & SLIP_SHAPE_SECONDARY_PROJECTION) == 0,
	                             .calledSecondaryProjection = (renderFlags & SLIP_SHAPE_SECONDARY_PROJECTION) != 0,
	                             .screenX = projectedScreenX,
	                             .screenY = projectedScreenY,
	                             .screenClipFlagsInitial = SLIP_VERTEX_PROJECTED,
	                             .returned = true};
	SlipDraw3D_WriteI32(record + SLIP_DRAW3D_VERTEX_RECORD_SCREEN_OFFSET, projectedScreenX);
	SlipDraw3D_WriteI32(record + offsetof(SlipDraw3DVertexRecord, screenY), projectedScreenY);
	screenClipFlags = SLIP_VERTEX_PROJECTED;
	result->xBelow = projectedScreenX < limitXMin;
	if (result->xBelow) {
		screenClipFlags |= SLIP_CLIP_LEFT;
	}
	result->xAbove = projectedScreenX > limitXMax;
	if (result->xAbove) {
		screenClipFlags |= SLIP_CLIP_RIGHT;
	}
	result->yBelow = projectedScreenY < limitYMin;
	if (result->yBelow) {
		screenClipFlags |= SLIP_CLIP_TOP;
	}
	result->yAbove = projectedScreenY > limitYMax;
	if (result->yAbove) {
		screenClipFlags |= SLIP_CLIP_BOTTOM;
	}
	result->hasScreenClipFlags = (screenClipFlags & SLIP_CLIP_SCREEN) != 0;
	if (result->hasScreenClipFlags) {
		result->withinPositiveSentinel = projectedScreenX < SLIP_SCREEN_CLIP_COORDINATE_LIMIT &&
		                                 projectedScreenY < SLIP_SCREEN_CLIP_COORDINATE_LIMIT;
		result->withinNegativeSentinel = projectedScreenX > -SLIP_SCREEN_CLIP_COORDINATE_LIMIT &&
		                                 projectedScreenY > -SLIP_SCREEN_CLIP_COORDINATE_LIMIT;
		if (result->withinPositiveSentinel && result->withinNegativeSentinel) {
			screenClipFlags |= SLIP_VERTEX_SCREEN_CLIP_IN_RANGE;
			result->finiteOffscreenFlag = true;
		}
	}
	flagsIn = SlipBytes_ReadLE32(record + SLIP_DRAW3D_VERTEX_RECORD_FLAGS_OFFSET);
	result->screenClipFlagsOut = screenClipFlags;
	result->flagsIn = flagsIn;
	result->flagsOut = flagsIn | screenClipFlags;
	SlipDraw3D_WriteLE32(record + SLIP_DRAW3D_VERTEX_RECORD_FLAGS_OFFSET, result->flagsOut);
	return 1;
}

int SlipDraw3D_ProjectFlagsWithCallback(uint8_t *recordBase, size_t recordBytes, uint32_t recordOffset,
                                        uint32_t renderFlags, SlipDraw3DProjectFn projectPrimary,
                                        SlipDraw3DProjectFn projectSecondary, void *userData, int32_t limitXMin,
                                        int32_t limitXMax, int32_t limitYMin, int32_t limitYMax,
                                        SlipDraw3DProjectFlags *result) {
	const uint8_t *record;
	SlipDraw3DProjectFn project;
	SlipDraw3DVec32 world;
	int32_t projectedScreenX;
	int32_t projectedScreenY;

	if (recordBase == NULL || (size_t)recordOffset > recordBytes ||
	    recordBytes - (size_t)recordOffset < SLIP_DRAW3D_DRAW_RECORD_SIZE) {
		return 0;
	}
	project = (renderFlags & SLIP_SHAPE_SECONDARY_PROJECTION) != 0 ? projectSecondary : projectPrimary;
	if (project == NULL) {
		return 0;
	}
	record = recordBase + recordOffset;
	world = (SlipDraw3DVec32){SlipBytes_ReadLEI32(record + offsetof(SlipDraw3DDrawRecord, world.x)),
	                          SlipBytes_ReadLEI32(record + offsetof(SlipDraw3DDrawRecord, world.y)),
	                          SlipBytes_ReadLEI32(record + offsetof(SlipDraw3DDrawRecord, world.z))};
	project(world, &projectedScreenX, &projectedScreenY, userData);
	return SlipDraw3D_ProjectFlags(recordBase, recordBytes, recordOffset, renderFlags, projectedScreenX,
	                               projectedScreenY, limitXMin, limitXMax, limitYMin, limitYMax, result);
}

int SlipDraw3D_ScreenPointFlags(SlipDraw3DDrawRecord *record, int32_t screenX, int32_t screenY, int32_t minX,
                                int32_t maxX, int32_t minY, int32_t maxY, SlipDraw3DScreenPointFlags *result) {
	uint32_t screenClipFlags;

	if (record == NULL || result == NULL) {
		return 0;
	}
	*result = (SlipDraw3DScreenPointFlags){
	    .screenX = screenX, .screenY = screenY, .screenClipFlagsInitial = SLIP_VERTEX_PROJECTED, .returned = true};
	screenClipFlags = SLIP_VERTEX_PROJECTED;
	result->xBelow = screenX < minX;
	if (result->xBelow) {
		screenClipFlags |= SLIP_CLIP_LEFT;
	}
	result->xAbove = screenX > maxX;
	if (result->xAbove) {
		screenClipFlags |= SLIP_CLIP_RIGHT;
	}
	result->yBelow = screenY < minY;
	if (result->yBelow) {
		screenClipFlags |= SLIP_CLIP_TOP;
	}
	result->yAbove = screenY > maxY;
	if (result->yAbove) {
		screenClipFlags |= SLIP_CLIP_BOTTOM;
	}
	result->hasScreenClipFlags = (screenClipFlags & SLIP_CLIP_SCREEN) != 0;
	if (result->hasScreenClipFlags) {
		result->withinPositiveSentinel =
		    screenX < SLIP_SCREEN_CLIP_COORDINATE_LIMIT && screenY < SLIP_SCREEN_CLIP_COORDINATE_LIMIT;
		result->withinNegativeSentinel =
		    screenX > -SLIP_SCREEN_CLIP_COORDINATE_LIMIT && screenY > -SLIP_SCREEN_CLIP_COORDINATE_LIMIT;
		if (result->withinPositiveSentinel && result->withinNegativeSentinel) {
			screenClipFlags |= SLIP_VERTEX_SCREEN_CLIP_IN_RANGE;
			result->finiteOffscreenFlag = true;
		}
	}
	result->screenClipFlagsOut = screenClipFlags;
	record->flags = screenClipFlags;
	record->screenX = screenX;
	record->screenY = screenY;
	return 1;
}

static int SlipDraw3D_SplitDepthRecordInternal(uint8_t *recordBase, size_t recordBytes, uint32_t targetOffset,
                                               uint32_t otherOffset, uint32_t renderFlags, uint32_t projectionMode,
                                               int32_t clipPlaneZ, int32_t projectedScreenX, int32_t projectedScreenY,
                                               SlipDraw3DProjectFn projectPrimary, SlipDraw3DProjectFn projectSecondary,
                                               void *userData, int useProjectCallback, int32_t limitXMin,
                                               int32_t limitXMax, int32_t limitYMin, int32_t limitYMax,
                                               SlipDraw3DSplitDepth *result) {
	uint8_t *target;
	const uint8_t *other;
	uint32_t zDeltaToPlane;
	uint32_t zDeltaBetweenRecords;
	uint32_t interpolationRatio;
	int32_t targetX;
	int32_t targetY;
	int32_t targetZ;
	int32_t otherZ;

	if (recordBase == NULL || result == NULL || (size_t)targetOffset > recordBytes ||
	    recordBytes - (size_t)targetOffset < SLIP_DRAW3D_DRAW_RECORD_SIZE || (size_t)otherOffset > recordBytes ||
	    recordBytes - (size_t)otherOffset < SLIP_DRAW3D_DRAW_RECORD_SIZE) {
		return 0;
	}
	target = recordBase + targetOffset;
	other = recordBase + otherOffset;
	targetX = SlipBytes_ReadLEI32(target + offsetof(SlipDraw3DDrawRecord, world.x));
	targetY = SlipBytes_ReadLEI32(target + offsetof(SlipDraw3DDrawRecord, world.y));
	targetZ = SlipBytes_ReadLEI32(target + offsetof(SlipDraw3DDrawRecord, world.z));
	otherZ = SlipBytes_ReadLEI32(other + offsetof(SlipDraw3DDrawRecord, world.z));
	zDeltaToPlane = (uint32_t)clipPlaneZ - (uint32_t)targetZ;
	zDeltaBetweenRecords = (uint32_t)otherZ - (uint32_t)targetZ;
	*result = (SlipDraw3DSplitDepth){.highPrecisionPath = (renderFlags & SLIP_SHAPE_SHORT_COORDINATES) != 0,
	                                 .targetOffset = targetOffset,
	                                 .otherOffset = otherOffset,
	                                 .clipPlaneZ = clipPlaneZ,
	                                 .zDeltaToPlane = (int32_t)zDeltaToPlane,
	                                 .zDeltaBetweenRecords = (int32_t)zDeltaBetweenRecords,
	                                 .returned = true};
	if ((int32_t)zDeltaBetweenRecords < 0) {
		zDeltaBetweenRecords = 0u - zDeltaBetweenRecords;
		zDeltaToPlane = 0u - zDeltaToPlane;
		result->negatedDeltas = true;
	}
	if (result->highPrecisionPath) {
		const uint32_t numerator = SlipDraw3D_SignedWordToFixed14(zDeltaToPlane);
		const uint16_t divisor = (uint16_t)zDeltaBetweenRecords;
		uint32_t quotient;
		int32_t xStep;
		int32_t yStep;

		if (divisor == 0) {
			return 0;
		}
		quotient = numerator / divisor;
		if (quotient > UINT16_MAX) {
			return 0;
		}
		interpolationRatio = quotient;
		yStep = ((int32_t)(int16_t)((uint32_t)SlipBytes_ReadLEI32(other + offsetof(SlipDraw3DDrawRecord, world.y)) -
		                            (uint32_t)targetY) *
		         (int32_t)(int16_t)interpolationRatio) >>
		        SLIP_Q14_FRACTION_BITS;
		xStep = ((int32_t)(int16_t)((uint32_t)SlipBytes_ReadLEI32(other + offsetof(SlipDraw3DDrawRecord, world.x)) -
		                            (uint32_t)targetX) *
		         (int32_t)(int16_t)interpolationRatio) >>
		        SLIP_Q14_FRACTION_BITS;
		SlipDraw3D_WriteI32(target + offsetof(SlipDraw3DDrawRecord, world.y),
		                    (int32_t)((uint32_t)targetY + (uint32_t)yStep));
		SlipDraw3D_WriteI32(target + offsetof(SlipDraw3DDrawRecord, world.x),
		                    (int32_t)((uint32_t)targetX + (uint32_t)xStep));
		result->interpolationRatio = interpolationRatio;
		result->xStep = xStep;
		result->yStep = yStep;
		if ((projectionMode & SLIP_INTERPOLATE_SHADE) != 0) {
			const uint16_t wordStep = SlipDraw3D_MultiplySigned16Shift14LowWord(
			    (int16_t)(SlipBytes_ReadLE16(other + offsetof(SlipDraw3DDrawRecord, shade)) -
			              SlipBytes_ReadLE16(target + offsetof(SlipDraw3DDrawRecord, shade))),
			    (int16_t)interpolationRatio);

			SlipDraw3D_WriteLE16(
			    target + offsetof(SlipDraw3DDrawRecord, shade),
			    (uint16_t)(SlipBytes_ReadLE16(target + offsetof(SlipDraw3DDrawRecord, shade)) + wordStep));
			result->interpolatedShade = true;
			result->shadeStep = wordStep;
		}
		result->calledInterpolateTextureQ14 = true;
		if (!SlipDraw3D_InterpolateExtraFields16(recordBase, recordBytes, targetOffset, otherOffset, projectionMode,
		                                         (uint16_t)interpolationRatio, &result->textureInterpolation)) {
			return 0;
		}
	} else {
		const uint64_t numerator = SlipDraw3D_SignedHighHalfShiftRightTwo(zDeltaToPlane);
		uint64_t quotient;
		int32_t xStep;
		int32_t yStep;

		if (zDeltaBetweenRecords == 0) {
			return 0;
		}
		quotient = numerator / zDeltaBetweenRecords;
		if (quotient > UINT32_MAX) {
			return 0;
		}
		interpolationRatio = (uint32_t)quotient;
		xStep = SlipDraw3D_MultiplySigned32Shift30(
		    (int32_t)((uint32_t)SlipBytes_ReadLEI32(other + offsetof(SlipDraw3DDrawRecord, world.x)) -
		              (uint32_t)targetX),
		    interpolationRatio);
		SlipDraw3D_WriteI32(target + offsetof(SlipDraw3DDrawRecord, world.x),
		                    (int32_t)((uint32_t)targetX + (uint32_t)xStep));
		yStep = SlipDraw3D_MultiplySigned32Shift30(
		    (int32_t)((uint32_t)SlipBytes_ReadLEI32(other + offsetof(SlipDraw3DDrawRecord, world.y)) -
		              (uint32_t)targetY),
		    interpolationRatio);
		SlipDraw3D_WriteI32(target + offsetof(SlipDraw3DDrawRecord, world.y),
		                    (int32_t)((uint32_t)targetY + (uint32_t)yStep));
		result->interpolationRatio = interpolationRatio;
		result->xStep = xStep;
		result->yStep = yStep;
		if ((projectionMode & SLIP_INTERPOLATE_SHADE) != 0) {
			const uint16_t wordStep = SlipDraw3D_MultiplySigned16Shift14LowWord(
			    (int16_t)(SlipBytes_ReadLE16(other + offsetof(SlipDraw3DDrawRecord, shade)) -
			              SlipBytes_ReadLE16(target + offsetof(SlipDraw3DDrawRecord, shade))),
			    (int16_t)((int32_t)interpolationRatio >> SLIP_DRAW3D_INTERPOLATION_TO_Q14_SHIFT));

			SlipDraw3D_WriteLE16(
			    target + offsetof(SlipDraw3DDrawRecord, shade),
			    (uint16_t)(SlipBytes_ReadLE16(target + offsetof(SlipDraw3DDrawRecord, shade)) + wordStep));
			result->interpolatedShade = true;
			result->shadeStep = wordStep;
		}
		result->calledInterpolateTextureQ30 = true;
		if (!SlipDraw3D_InterpolateExtraFields32(recordBase, recordBytes, targetOffset, otherOffset, projectionMode,
		                                         interpolationRatio, &result->textureInterpolation)) {
			return 0;
		}
	}
	SlipDraw3D_WriteI32(target + offsetof(SlipDraw3DDrawRecord, world.z), clipPlaneZ);
	result->wroteClipPlaneZ = true;
	result->calledProjectFlags = true;
	if (useProjectCallback) {
		return SlipDraw3D_ProjectFlagsWithCallback(recordBase, recordBytes, targetOffset, renderFlags, projectPrimary,
		                                           projectSecondary, userData, limitXMin, limitXMax, limitYMin,
		                                           limitYMax, &result->projectFlags);
	}
	return SlipDraw3D_ProjectFlags(recordBase, recordBytes, targetOffset, renderFlags, projectedScreenX,
	                               projectedScreenY, limitXMin, limitXMax, limitYMin, limitYMax, &result->projectFlags);
}

int SlipDraw3D_SplitDepthRecord(uint8_t *recordBase, size_t recordBytes, uint32_t targetOffset, uint32_t otherOffset,
                                uint32_t renderFlags, uint32_t projectionMode, int32_t clipPlaneZ,
                                int32_t projectedScreenX, int32_t projectedScreenY, int32_t limitXMin,
                                int32_t limitXMax, int32_t limitYMin, int32_t limitYMax, SlipDraw3DSplitDepth *result) {
	return SlipDraw3D_SplitDepthRecordInternal(recordBase, recordBytes, targetOffset, otherOffset, renderFlags,
	                                           projectionMode, clipPlaneZ, projectedScreenX, projectedScreenY, NULL,
	                                           NULL, NULL, 0, limitXMin, limitXMax, limitYMin, limitYMax, result);
}

int SlipDraw3D_SplitDepthRecordWithCallback(uint8_t *recordBase, size_t recordBytes, uint32_t targetOffset,
                                            uint32_t otherOffset, uint32_t renderFlags, uint32_t projectionMode,
                                            int32_t clipPlaneZ, SlipDraw3DProjectFn projectPrimary,
                                            SlipDraw3DProjectFn projectSecondary, void *userData, int32_t limitXMin,
                                            int32_t limitXMax, int32_t limitYMin, int32_t limitYMax,
                                            SlipDraw3DSplitDepth *result) {
	return SlipDraw3D_SplitDepthRecordInternal(recordBase, recordBytes, targetOffset, otherOffset, renderFlags,
	                                           projectionMode, clipPlaneZ, 0, 0, projectPrimary, projectSecondary,
	                                           userData, 1, limitXMin, limitXMax, limitYMin, limitYMax, result);
}

static int SlipDraw3D_SplitDepthMathRecordInternal(
    uint8_t *recordBase, size_t recordBytes, uint32_t targetOffset, uint32_t otherOffset, uint32_t renderFlags,
    uint32_t projectionMode, int32_t projectedScreenX, int32_t projectedScreenY, SlipDraw3DProjectFn projectPrimary,
    SlipDraw3DProjectFn projectSecondary, void *userData, int useProjectCallback, int32_t limitZMin, int32_t limitZMax,
    int32_t limitXMin, int32_t limitXMax, int32_t limitYMin, int32_t limitYMax, SlipDraw3DSplitDepthMath *result) {
	uint8_t *target;
	const uint8_t *other;
	int32_t deltaX;
	int32_t deltaY;
	int32_t deltaZ;
	int32_t targetDepth;
	int32_t otherDepth;
	int32_t xStep;
	int32_t yStep;
	int32_t zStep;
	uint32_t interpolationRatio;
	int32_t xAfter;
	int32_t yAfter;
	int32_t zAfter;
	uint32_t flags;

	if (recordBase == NULL || result == NULL || (size_t)targetOffset > recordBytes ||
	    recordBytes - (size_t)targetOffset < SLIP_DRAW3D_DRAW_RECORD_SIZE || (size_t)otherOffset > recordBytes ||
	    recordBytes - (size_t)otherOffset < SLIP_DRAW3D_DRAW_RECORD_SIZE) {
		return 0;
	}
	target = recordBase + targetOffset;
	other = recordBase + otherOffset;
	deltaX = (int32_t)((uint32_t)SlipBytes_ReadLEI32(other + offsetof(SlipDraw3DDrawRecord, world.x)) -
	                   (uint32_t)SlipBytes_ReadLEI32(target + offsetof(SlipDraw3DDrawRecord, world.x)));
	deltaY = (int32_t)((uint32_t)SlipBytes_ReadLEI32(other + offsetof(SlipDraw3DDrawRecord, world.y)) -
	                   (uint32_t)SlipBytes_ReadLEI32(target + offsetof(SlipDraw3DDrawRecord, world.y)));
	deltaZ = (int32_t)((uint32_t)SlipBytes_ReadLEI32(other + offsetof(SlipDraw3DDrawRecord, world.z)) -
	                   (uint32_t)SlipBytes_ReadLEI32(target + offsetof(SlipDraw3DDrawRecord, world.z)));
	targetDepth = SlipBytes_ReadLEI32(target + offsetof(SlipDraw3DDrawRecord, depth));
	otherDepth = SlipBytes_ReadLEI32(other + offsetof(SlipDraw3DDrawRecord, depth));
	if ((renderFlags & SLIP_SHAPE_SHORT_COORDINATES) != 0) {
		if (!SlipDraw3D_InterpolatePlaneIntersectionQ14(deltaX, deltaY, deltaZ, targetDepth, otherDepth, &xStep, &yStep,
		                                                &zStep, &interpolationRatio)) {
			return 0;
		}
	} else if (!SlipDraw3D_InterpolatePlaneIntersectionQ30(deltaX, deltaY, deltaZ, targetDepth, otherDepth, &xStep,
	                                                       &yStep, &zStep, &interpolationRatio)) {
		return 0;
	}
	*result = (SlipDraw3DSplitDepthMath){.highPrecisionPath = (renderFlags & SLIP_SHAPE_SHORT_COORDINATES) != 0,
	                                     .targetOffset = targetOffset,
	                                     .otherOffset = otherOffset,
	                                     .inputDeltaX = deltaX,
	                                     .inputDeltaY = deltaY,
	                                     .inputDeltaZ = deltaZ,
	                                     .targetDepth = targetDepth,
	                                     .otherDepth = otherDepth,
	                                     .calledIntersectPlaneQ14 = (renderFlags & SLIP_SHAPE_SHORT_COORDINATES) != 0,
	                                     .calledIntersectPlaneQ30 = (renderFlags & SLIP_SHAPE_SHORT_COORDINATES) == 0,
	                                     .xStepApplied = xStep,
	                                     .yStepApplied = yStep,
	                                     .zStepApplied = zStep,
	                                     .interpolationRatio = interpolationRatio,
	                                     .returned = true};
	xAfter =
	    (int32_t)((uint32_t)SlipBytes_ReadLEI32(target + offsetof(SlipDraw3DDrawRecord, world.x)) + (uint32_t)xStep);
	yAfter =
	    (int32_t)((uint32_t)SlipBytes_ReadLEI32(target + offsetof(SlipDraw3DDrawRecord, world.y)) + (uint32_t)yStep);
	zAfter =
	    (int32_t)((uint32_t)SlipBytes_ReadLEI32(target + offsetof(SlipDraw3DDrawRecord, world.z)) + (uint32_t)zStep);
	SlipDraw3D_WriteI32(target + offsetof(SlipDraw3DDrawRecord, world.x), xAfter);
	SlipDraw3D_WriteI32(target + offsetof(SlipDraw3DDrawRecord, world.y), yAfter);
	SlipDraw3D_WriteI32(target + offsetof(SlipDraw3DDrawRecord, world.z), zAfter);
	if ((projectionMode & SLIP_INTERPOLATE_SHADE) != 0) {
		uint16_t wordStep;
		int16_t ratio;

		ratio = result->highPrecisionPath
		            ? (int16_t)interpolationRatio
		            : (int16_t)((int32_t)interpolationRatio >> SLIP_DRAW3D_INTERPOLATION_TO_Q14_SHIFT);
		wordStep = SlipDraw3D_MultiplySigned16Shift14LowWord(
		    (int16_t)(SlipBytes_ReadLE16(other + offsetof(SlipDraw3DDrawRecord, shade)) -
		              SlipBytes_ReadLE16(target + offsetof(SlipDraw3DDrawRecord, shade))),
		    ratio);
		SlipDraw3D_WriteLE16(target + offsetof(SlipDraw3DDrawRecord, shade),
		                     (uint16_t)(SlipBytes_ReadLE16(target + offsetof(SlipDraw3DDrawRecord, shade)) + wordStep));
		result->interpolatedShade = true;
		result->shadeStep = wordStep;
	}
	if (result->highPrecisionPath) {
		result->calledInterpolateTextureQ14 = true;
		if (!SlipDraw3D_InterpolateExtraFields16(recordBase, recordBytes, targetOffset, otherOffset, projectionMode,
		                                         (uint16_t)interpolationRatio, &result->textureInterpolation)) {
			return 0;
		}
	} else {
		result->calledInterpolateTextureQ30 = true;
		if (!SlipDraw3D_InterpolateExtraFields32(recordBase, recordBytes, targetOffset, otherOffset, projectionMode,
		                                         interpolationRatio, &result->textureInterpolation)) {
			return 0;
		}
	}
	zAfter = SlipBytes_ReadLEI32(target + offsetof(SlipDraw3DDrawRecord, world.z));
	flags = SlipBytes_ReadLE32(target + SLIP_DRAW3D_VERTEX_RECORD_FLAGS_OFFSET) & ~SLIP_CLIP_BEFORE_PROJECTION;
	SlipDraw3D_WriteLE32(target + SLIP_DRAW3D_VERTEX_RECORD_FLAGS_OFFSET, flags);
	result->flagsAfterClear = flags;
	result->zAfterAdd = zAfter;
	result->zBelow = zAfter < limitZMin;
	if (result->zBelow) {
		SlipDraw3D_WriteLE32(target + SLIP_DRAW3D_VERTEX_RECORD_FLAGS_OFFSET,
		                     SlipBytes_ReadLE32(target + SLIP_DRAW3D_VERTEX_RECORD_FLAGS_OFFSET) | SLIP_CLIP_NEAR);
		return 1;
	}
	result->zAbove = zAfter > limitZMax;
	if (result->zAbove) {
		SlipDraw3D_WriteLE32(target + SLIP_DRAW3D_VERTEX_RECORD_FLAGS_OFFSET,
		                     SlipBytes_ReadLE32(target + SLIP_DRAW3D_VERTEX_RECORD_FLAGS_OFFSET) | SLIP_CLIP_FAR);
		return 1;
	}
	result->calledProjectFlags = true;
	if (useProjectCallback) {
		return SlipDraw3D_ProjectFlagsWithCallback(recordBase, recordBytes, targetOffset, renderFlags, projectPrimary,
		                                           projectSecondary, userData, limitXMin, limitXMax, limitYMin,
		                                           limitYMax, &result->projectFlags);
	}
	return SlipDraw3D_ProjectFlags(recordBase, recordBytes, targetOffset, renderFlags, projectedScreenX,
	                               projectedScreenY, limitXMin, limitXMax, limitYMin, limitYMax, &result->projectFlags);
}

int SlipDraw3D_SplitDepthMathRecord(uint8_t *recordBase, size_t recordBytes, uint32_t targetOffset,
                                    uint32_t otherOffset, uint32_t renderFlags, uint32_t projectionMode,
                                    int32_t projectedScreenX, int32_t projectedScreenY, int32_t limitZMin,
                                    int32_t limitZMax, int32_t limitXMin, int32_t limitXMax, int32_t limitYMin,
                                    int32_t limitYMax, SlipDraw3DSplitDepthMath *result) {
	return SlipDraw3D_SplitDepthMathRecordInternal(recordBase, recordBytes, targetOffset, otherOffset, renderFlags,
	                                               projectionMode, projectedScreenX, projectedScreenY, NULL, NULL, NULL,
	                                               0, limitZMin, limitZMax, limitXMin, limitXMax, limitYMin, limitYMax,
	                                               result);
}

int SlipDraw3D_SplitDepthMathRecordWithCallback(uint8_t *recordBase, size_t recordBytes, uint32_t targetOffset,
                                                uint32_t otherOffset, uint32_t renderFlags, uint32_t projectionMode,
                                                SlipDraw3DProjectFn projectPrimary,
                                                SlipDraw3DProjectFn projectSecondary, void *userData, int32_t limitZMin,
                                                int32_t limitZMax, int32_t limitXMin, int32_t limitXMax,
                                                int32_t limitYMin, int32_t limitYMax,
                                                SlipDraw3DSplitDepthMath *result) {
	return SlipDraw3D_SplitDepthMathRecordInternal(
	    recordBase, recordBytes, targetOffset, otherOffset, renderFlags, projectionMode, 0, 0, projectPrimary,
	    projectSecondary, userData, 1, limitZMin, limitZMax, limitXMin, limitXMax, limitYMin, limitYMax, result);
}

int SlipDraw3D_InterpolateExtraFields16(uint8_t *recordBase, size_t recordBytes, uint32_t targetOffset,
                                        uint32_t otherOffset, uint32_t projectionMode, uint16_t textureRatio,
                                        SlipDraw3DExtraFieldInterpolation *result) {
	SlipDraw3DDrawRecord *target;
	const SlipDraw3DDrawRecord *other;
	int32_t textureUDelta;
	int32_t textureVDelta;
	int32_t textureUStep;
	int32_t textureVStep;

	if (recordBase == NULL || result == NULL || (size_t)targetOffset > recordBytes ||
	    recordBytes - (size_t)targetOffset < SLIP_DRAW3D_DRAW_RECORD_SIZE || (size_t)otherOffset > recordBytes ||
	    recordBytes - (size_t)otherOffset < SLIP_DRAW3D_DRAW_RECORD_SIZE) {
		return 0;
	}
	target = (SlipDraw3DDrawRecord *)(void *)(recordBase + targetOffset);
	other = (const SlipDraw3DDrawRecord *)(const void *)(recordBase + otherOffset);
	*result = (SlipDraw3DExtraFieldInterpolation){.targetOffset = targetOffset,
	                                              .otherOffset = otherOffset,
	                                              .projectionMode = projectionMode,
	                                              .ratio = textureRatio,
	                                              .returned = true};
	if ((projectionMode & SLIP_INTERPOLATE_TEXTURE) == 0) {
		return 1;
	}
	textureUDelta = (int32_t)(other->textureU - target->textureU);
	textureUStep = SlipDraw3D_MultiplySigned16Shift14PreserveDeltaHigh(textureUDelta, (int16_t)textureRatio);
	target->textureU += (uint32_t)textureUStep;
	textureVDelta = (int32_t)(other->textureV - target->textureV);
	textureVStep = SlipDraw3D_MultiplySigned16Shift14PreserveDeltaHigh(textureVDelta, (int16_t)textureRatio);
	target->textureV += (uint32_t)textureVStep;
	result->interpolatedTexture = true;
	result->textureUDelta = textureUDelta;
	result->textureUStep = textureUStep;
	result->textureVDelta = textureVDelta;
	result->textureVStep = textureVStep;
	return 1;
}

int SlipDraw3D_InterpolateExtraFields32(uint8_t *recordBase, size_t recordBytes, uint32_t targetOffset,
                                        uint32_t otherOffset, uint32_t projectionMode, uint32_t interpolationRatio,
                                        SlipDraw3DExtraFieldInterpolation *result) {
	if (!SlipDraw3D_InterpolateExtraFields16(recordBase, recordBytes, targetOffset, otherOffset, projectionMode,
	                                         (uint16_t)(interpolationRatio >> SLIP_DRAW3D_INTERPOLATION_TO_Q14_SHIFT),
	                                         result)) {
		return 0;
	}
	result->ratio = interpolationRatio;
	return 1;
}

int SlipDraw3D_InterpolateDepthAndExtra16(uint8_t *recordBase, size_t recordBytes, uint32_t targetOffset,
                                          uint32_t otherOffset, uint32_t projectionMode, uint32_t interpolationRatio,
                                          SlipDraw3DDepthExtraInterpolation *result) {
	SlipDraw3DDrawRecord *target;
	const SlipDraw3DDrawRecord *other;
	uint32_t ratio;
	int32_t targetDepth;
	int32_t otherDepth;

	if (recordBase == NULL || result == NULL || (size_t)targetOffset > recordBytes ||
	    recordBytes - (size_t)targetOffset < SLIP_DRAW3D_DRAW_RECORD_SIZE || (size_t)otherOffset > recordBytes ||
	    recordBytes - (size_t)otherOffset < SLIP_DRAW3D_DRAW_RECORD_SIZE) {
		return 0;
	}
	target = (SlipDraw3DDrawRecord *)(void *)(recordBase + targetOffset);
	other = (const SlipDraw3DDrawRecord *)(const void *)(recordBase + otherOffset);
	*result = (SlipDraw3DDepthExtraInterpolation){.targetOffset = targetOffset,
	                                              .otherOffset = otherOffset,
	                                              .projectionMode = projectionMode,
	                                              .entryRatio = interpolationRatio,
	                                              .returned = true};
	ratio = (uint16_t)interpolationRatio;
	if ((projectionMode & SLIP_INTERPOLATE_PERSPECTIVE_DEPTH) != 0) {
		int32_t denominatorDepth;
		int32_t denominatorStep;
		int32_t depthRatio;
		int32_t depthStep;
		int64_t targetDepthTimesRatio;

		targetDepth = target->world.z;
		otherDepth = other->world.z;
		denominatorStep =
		    SlipDraw3D_MultiplySigned32RoundShift14((int32_t)((uint32_t)targetDepth - (uint32_t)otherDepth), ratio);
		denominatorDepth = (int32_t)((uint32_t)otherDepth + (uint32_t)denominatorStep);
		targetDepthTimesRatio = (int64_t)targetDepth * (int64_t)(int32_t)ratio;
		if (!SlipDraw3D_DivideSignedChecked32(targetDepthTimesRatio, denominatorDepth, &depthRatio)) {
			return 0;
		}
		depthStep = SlipDraw3D_MultiplySigned32RoundShift14((int32_t)((uint32_t)otherDepth - (uint32_t)targetDepth),
		                                                    (uint32_t)depthRatio);
		target->world.z = (int32_t)((uint32_t)targetDepth + (uint32_t)depthStep);
		ratio = (uint32_t)depthRatio;
		result->interpolatedDepth = true;
		result->firstMultiplyRatio = (uint16_t)interpolationRatio;
		result->targetDepthBefore = targetDepth;
		result->otherDepth = otherDepth;
		result->depthDeltaTargetMinusOther = (int32_t)((uint32_t)targetDepth - (uint32_t)otherDepth);
		result->roundedTargetMinusOtherStep = denominatorStep;
		result->denominatorDepth = denominatorDepth;
		result->targetDepthTimesRatio = targetDepthTimesRatio;
		result->depthAdjustedRatio = depthRatio;
		result->depthDeltaOtherMinusTarget = (int32_t)((uint32_t)otherDepth - (uint32_t)targetDepth);
		result->roundedOtherMinusTargetStep = depthStep;
		result->targetDepthAfter = target->world.z;
	}
	result->calledInterpolateTextureQ14 = true;
	return SlipDraw3D_InterpolateExtraFields16(recordBase, recordBytes, targetOffset, otherOffset, projectionMode,
	                                           (uint16_t)ratio, &result->textureInterpolation);
}

int SlipDraw3D_InterpolateDepthAndExtra32(uint8_t *recordBase, size_t recordBytes, uint32_t targetOffset,
                                          uint32_t otherOffset, uint32_t projectionMode, uint32_t interpolationRatio,
                                          SlipDraw3DDepthExtraInterpolation *result) {
	SlipDraw3DDrawRecord *target;
	const SlipDraw3DDrawRecord *other;
	uint32_t ratio;
	int32_t targetDepth;
	int32_t otherDepth;

	if (recordBase == NULL || result == NULL || (size_t)targetOffset > recordBytes ||
	    recordBytes - (size_t)targetOffset < SLIP_DRAW3D_DRAW_RECORD_SIZE || (size_t)otherOffset > recordBytes ||
	    recordBytes - (size_t)otherOffset < SLIP_DRAW3D_DRAW_RECORD_SIZE) {
		return 0;
	}
	target = (SlipDraw3DDrawRecord *)(void *)(recordBase + targetOffset);
	other = (const SlipDraw3DDrawRecord *)(const void *)(recordBase + otherOffset);
	*result = (SlipDraw3DDepthExtraInterpolation){.targetOffset = targetOffset,
	                                              .otherOffset = otherOffset,
	                                              .projectionMode = projectionMode,
	                                              .entryRatio = interpolationRatio,
	                                              .returned = true};
	ratio = interpolationRatio;
	if ((projectionMode & SLIP_INTERPOLATE_PERSPECTIVE_DEPTH) != 0) {
		int32_t denominatorDepth;
		int32_t denominatorStep;
		int32_t depthRatio;
		int32_t depthStep;
		int64_t targetDepthTimesRatio;

		targetDepth = target->world.z;
		otherDepth = other->world.z;
		denominatorStep =
		    SlipDraw3D_MultiplySigned32RoundShift30((int32_t)((uint32_t)targetDepth - (uint32_t)otherDepth), ratio);
		denominatorDepth = (int32_t)((uint32_t)otherDepth + (uint32_t)denominatorStep);
		targetDepthTimesRatio = (int64_t)targetDepth * (int64_t)(int32_t)ratio;
		if (!SlipDraw3D_DivideSignedChecked32(targetDepthTimesRatio, denominatorDepth, &depthRatio)) {
			return 0;
		}
		depthStep = SlipDraw3D_MultiplySigned32RoundShift30((int32_t)((uint32_t)otherDepth - (uint32_t)targetDepth),
		                                                    (uint32_t)depthRatio);
		target->world.z = (int32_t)((uint32_t)targetDepth + (uint32_t)depthStep);
		ratio = (uint32_t)depthRatio;
		result->interpolatedDepth = true;
		result->firstMultiplyRatio = interpolationRatio;
		result->targetDepthBefore = targetDepth;
		result->otherDepth = otherDepth;
		result->depthDeltaTargetMinusOther = (int32_t)((uint32_t)targetDepth - (uint32_t)otherDepth);
		result->roundedTargetMinusOtherStep = denominatorStep;
		result->denominatorDepth = denominatorDepth;
		result->targetDepthTimesRatio = targetDepthTimesRatio;
		result->depthAdjustedRatio = depthRatio;
		result->depthDeltaOtherMinusTarget = (int32_t)((uint32_t)otherDepth - (uint32_t)targetDepth);
		result->roundedOtherMinusTargetStep = depthStep;
		result->targetDepthAfter = target->world.z;
	}
	result->calledInterpolateTextureQ30 = true;
	return SlipDraw3D_InterpolateExtraFields32(recordBase, recordBytes, targetOffset, otherOffset, projectionMode,
	                                           ratio, &result->textureInterpolation);
}

int SlipDraw3D_SplitScreenXRecord(uint8_t *recordBase, size_t recordBytes, uint32_t targetOffset, uint32_t otherOffset,
                                  uint32_t projectionMode, int32_t clipPlaneX, int32_t limitYMin, int32_t limitYMax,
                                  SlipDraw3DSplitScreenX *result) {
	if (SlipRaceGpu_Active())
		return SlipRaceGpu_SplitScreenXRecord(recordBase, recordBytes, targetOffset, otherOffset, projectionMode,
		                                      clipPlaneX, limitYMin, limitYMax, result);
	SlipDraw3DDrawRecord *target;
	const SlipDraw3DDrawRecord *other;
	uint32_t targetFlags;
	uint32_t otherFlags;
	uint32_t xDeltaToPlane;
	uint32_t xDeltaBetweenRecords;
	uint32_t ratio;
	int32_t yStep;
	int32_t yAfter;
	uint32_t flags;

	if (recordBase == NULL || result == NULL || (size_t)targetOffset > recordBytes ||
	    recordBytes - (size_t)targetOffset < SLIP_DRAW3D_DRAW_RECORD_SIZE || (size_t)otherOffset > recordBytes ||
	    recordBytes - (size_t)otherOffset < SLIP_DRAW3D_DRAW_RECORD_SIZE) {
		return 0;
	}
	target = (SlipDraw3DDrawRecord *)(void *)(recordBase + targetOffset);
	other = (const SlipDraw3DDrawRecord *)(const void *)(recordBase + otherOffset);
	targetFlags = target->flags;
	otherFlags = other->flags;
	xDeltaToPlane = (uint32_t)clipPlaneX - (uint32_t)target->screenX;
	xDeltaBetweenRecords = (uint32_t)other->screenX - (uint32_t)target->screenX;
	*result = (SlipDraw3DSplitScreenX){.highPrecisionPath =
	                                       ((targetFlags & SLIP_VERTEX_SCREEN_CLIP_IN_RANGE) & otherFlags) != 0,
	                                   .targetOffset = targetOffset,
	                                   .otherOffset = otherOffset,
	                                   .clipPlaneX = clipPlaneX,
	                                   .xDeltaToPlane = (int32_t)xDeltaToPlane,
	                                   .xDeltaBetweenRecords = (int32_t)xDeltaBetweenRecords,
	                                   .returned = true};
	if ((int32_t)xDeltaBetweenRecords < 0) {
		xDeltaBetweenRecords = 0u - xDeltaBetweenRecords;
		xDeltaToPlane = 0u - xDeltaToPlane;
		result->negatedDeltas = true;
	}
	if (result->highPrecisionPath) {
		const uint32_t numerator = SlipDraw3D_SignedWordToFixed15(xDeltaToPlane);
		const uint16_t divisor = (uint16_t)xDeltaBetweenRecords;
		uint32_t quotient;

		if (divisor == 0) {
			return 0;
		}
		quotient = numerator / divisor;
		if (quotient > UINT16_MAX) {
			return 0;
		}
		ratio = (uint16_t)((quotient >> 1) + (quotient & 1u));
		yStep = SlipDraw3D_MultiplySigned16RoundShift14((int16_t)((uint32_t)other->screenY - (uint32_t)target->screenY),
		                                                (uint16_t)ratio);
		target->screenY = (int32_t)((uint32_t)target->screenY + (uint32_t)yStep);
	} else {
		const uint64_t numerator = SlipDraw3D_SignedHighHalfShiftRightTwo(xDeltaToPlane);
		uint64_t quotient;

		if (xDeltaBetweenRecords == 0) {
			return 0;
		}
		quotient = numerator / xDeltaBetweenRecords;
		if (quotient > UINT32_MAX) {
			return 0;
		}
		ratio = (uint32_t)quotient;
		yStep = SlipDraw3D_MultiplySigned32RoundShift30((int32_t)((uint32_t)other->screenY - (uint32_t)target->screenY),
		                                                ratio);
		target->screenY = (int32_t)((uint32_t)target->screenY + (uint32_t)yStep);
	}
	result->interpolationRatio = ratio;
	result->yStep = yStep;
	if ((projectionMode & SLIP_INTERPOLATE_SHADE) != 0) {
		const int16_t wordRatio = result->highPrecisionPath
		                              ? (int16_t)(uint16_t)ratio
		                              : (int16_t)((int32_t)ratio >> SLIP_DRAW3D_INTERPOLATION_TO_Q14_SHIFT);
		const uint16_t wordStep =
		    SlipDraw3D_MultiplySigned16Shift14LowWord((int16_t)(other->shade - target->shade), wordRatio);

		target->shade = (uint16_t)(target->shade + wordStep);
		result->interpolatedShade = true;
		result->shadeStep = wordStep;
	}
	if (result->highPrecisionPath) {
		result->calledInterpolateDepthAndTextureQ14 = true;
		if (!SlipDraw3D_InterpolateDepthAndExtra16(recordBase, recordBytes, targetOffset, otherOffset, projectionMode,
		                                           ratio, &result->depthAndTextureInterpolation)) {
			return 0;
		}
	} else {
		result->calledInterpolateDepthAndTextureQ30 = true;
		if (!SlipDraw3D_InterpolateDepthAndExtra32(recordBase, recordBytes, targetOffset, otherOffset, projectionMode,
		                                           ratio, &result->depthAndTextureInterpolation)) {
			return 0;
		}
	}
	target->screenX = clipPlaneX;
	result->wroteClipPlaneX = true;
	yAfter = target->screenY;
	flags = target->flags;
	result->flagsBefore = flags;
	flags &= ~SLIP_VERTEX_VERTICAL_CLIP_STATUS;
	result->flagsAfterClear = flags;
	result->yBelow = yAfter < limitYMin;
	if (result->yBelow) {
		flags |= SLIP_CLIP_TOP;
	}
	result->yAbove = yAfter > limitYMax;
	if (result->yAbove) {
		flags |= SLIP_CLIP_BOTTOM;
	}
	result->yInsideFiniteSentinel =
	    yAfter < SLIP_SCREEN_CLIP_COORDINATE_LIMIT && yAfter > -SLIP_SCREEN_CLIP_COORDINATE_LIMIT;
	if (result->yInsideFiniteSentinel) {
		flags |= SLIP_VERTEX_SCREEN_CLIP_IN_RANGE;
	}
	target->flags = flags;
	result->flagsOut = flags;
	return 1;
}

int SlipDraw3D_SplitScreenYRecord(uint8_t *recordBase, size_t recordBytes, uint32_t targetOffset, uint32_t otherOffset,
                                  uint32_t projectionMode, int32_t clipPlaneY, SlipDraw3DSplitScreenY *result) {
	if (SlipRaceGpu_Active())
		return SlipRaceGpu_SplitScreenYRecord(recordBase, recordBytes, targetOffset, otherOffset, projectionMode,
		                                      clipPlaneY, result);
	SlipDraw3DDrawRecord *target;
	const SlipDraw3DDrawRecord *other;
	uint32_t targetFlags;
	uint32_t otherFlags;
	uint32_t yDeltaToPlane;
	uint32_t yDeltaBetweenRecords;
	uint32_t ratio;
	int32_t xStep;

	if (recordBase == NULL || result == NULL || (size_t)targetOffset > recordBytes ||
	    recordBytes - (size_t)targetOffset < SLIP_DRAW3D_DRAW_RECORD_SIZE || (size_t)otherOffset > recordBytes ||
	    recordBytes - (size_t)otherOffset < SLIP_DRAW3D_DRAW_RECORD_SIZE) {
		return 0;
	}
	target = (SlipDraw3DDrawRecord *)(void *)(recordBase + targetOffset);
	other = (const SlipDraw3DDrawRecord *)(const void *)(recordBase + otherOffset);
	targetFlags = target->flags;
	otherFlags = other->flags;
	yDeltaToPlane = (uint32_t)clipPlaneY - (uint32_t)target->screenY;
	yDeltaBetweenRecords = (uint32_t)other->screenY - (uint32_t)target->screenY;
	*result = (SlipDraw3DSplitScreenY){.highPrecisionPath =
	                                       ((targetFlags & SLIP_VERTEX_SCREEN_CLIP_IN_RANGE) & otherFlags) != 0,
	                                   .targetOffset = targetOffset,
	                                   .otherOffset = otherOffset,
	                                   .clipPlaneY = clipPlaneY,
	                                   .yDeltaToPlane = (int32_t)yDeltaToPlane,
	                                   .yDeltaBetweenRecords = (int32_t)yDeltaBetweenRecords,
	                                   .returned = true};
	if ((int32_t)yDeltaBetweenRecords < 0) {
		yDeltaBetweenRecords = 0u - yDeltaBetweenRecords;
		yDeltaToPlane = 0u - yDeltaToPlane;
		result->negatedDeltas = true;
	}
	if (result->highPrecisionPath) {
		const uint32_t numerator = SlipDraw3D_SignedWordToFixed15(yDeltaToPlane);
		const uint16_t divisor = (uint16_t)yDeltaBetweenRecords;
		uint32_t quotient;

		if (divisor == 0) {
			return 0;
		}
		quotient = numerator / divisor;
		if (quotient > UINT16_MAX) {
			return 0;
		}
		ratio = (uint16_t)((quotient >> 1) + (quotient & 1u));
		xStep = SlipDraw3D_MultiplySigned16RoundShift14((int16_t)((uint32_t)other->screenX - (uint32_t)target->screenX),
		                                                (uint16_t)ratio);
		target->screenX = (int32_t)((uint32_t)target->screenX + (uint32_t)xStep);
	} else {
		const uint64_t numerator = SlipDraw3D_SignedHighHalfShiftRightTwo(yDeltaToPlane);
		uint64_t quotient;

		if (yDeltaBetweenRecords == 0) {
			return 0;
		}
		quotient = numerator / yDeltaBetweenRecords;
		if (quotient > UINT32_MAX) {
			return 0;
		}
		ratio = (uint32_t)quotient;
		xStep = SlipDraw3D_MultiplySigned32RoundShift30((int32_t)((uint32_t)other->screenX - (uint32_t)target->screenX),
		                                                ratio);
		target->screenX = (int32_t)((uint32_t)target->screenX + (uint32_t)xStep);
	}
	result->interpolationRatio = ratio;
	result->xStep = xStep;
	if ((projectionMode & SLIP_INTERPOLATE_SHADE) != 0) {
		const int16_t wordRatio = result->highPrecisionPath
		                              ? (int16_t)(uint16_t)ratio
		                              : (int16_t)((int32_t)ratio >> SLIP_DRAW3D_INTERPOLATION_TO_Q14_SHIFT);
		const uint16_t wordStep =
		    SlipDraw3D_MultiplySigned16Shift14LowWord((int16_t)(other->shade - target->shade), wordRatio);

		target->shade = (uint16_t)(target->shade + wordStep);
		result->interpolatedShade = true;
		result->shadeStep = wordStep;
	}
	if (result->highPrecisionPath) {
		result->calledInterpolateDepthAndTextureQ14 = true;
		if (!SlipDraw3D_InterpolateDepthAndExtra16(recordBase, recordBytes, targetOffset, otherOffset, projectionMode,
		                                           ratio, &result->depthAndTextureInterpolation)) {
			return 0;
		}
	} else {
		result->calledInterpolateDepthAndTextureQ30 = true;
		if (!SlipDraw3D_InterpolateDepthAndExtra32(recordBase, recordBytes, targetOffset, otherOffset, projectionMode,
		                                           ratio, &result->depthAndTextureInterpolation)) {
			return 0;
		}
	}
	target->screenY = clipPlaneY;
	result->wroteClipPlaneY = true;
	return 1;
}

int SlipDraw3D_PostPlaneBounds(const uint8_t *recordBase, size_t recordBytes, uint32_t headOffset, int32_t limitXMin,
                               int32_t limitXMax, int32_t limitYMin, int32_t limitYMax,
                               SlipDraw3DPostPlaneBoundsVisit *visits, size_t visitCapacity,
                               SlipDraw3DPostPlaneBounds *result) {
	uint32_t recordOffset;
	uint32_t loopHeadOffset;
	uint32_t allFlags;
	size_t visitCount;

	if (recordBase == NULL || visits == NULL || result == NULL ||
	    !SlipDraw3D_LinkedRecordOffsetValid(recordBytes, headOffset)) {
		return 0;
	}
	recordOffset = headOffset;
	loopHeadOffset = recordOffset;
	allFlags = UINT32_MAX;
	visitCount = 0;
	do {
		const uint8_t *record;
		int32_t screenX;
		int32_t screenY;
		uint32_t screenFlags;
		uint32_t nextOffset;
		SlipDraw3DPostPlaneBoundsVisit *visit;

		if (visitCount >= visitCapacity || !SlipDraw3D_LinkedRecordOffsetValid(recordBytes, recordOffset)) {
			return 0;
		}
		record = recordBase + recordOffset;
		screenFlags = 0;
		screenX = SlipBytes_ReadLEI32(record + offsetof(SlipDraw3DDrawRecord, screenX));
		if (screenX < limitXMin) {
			screenFlags = SLIP_CLIP_LEFT;
		}
		if (screenX > limitXMax) {
			screenFlags = SLIP_CLIP_RIGHT;
		}
		screenY = SlipBytes_ReadLEI32(record + offsetof(SlipDraw3DDrawRecord, screenY));
		if (screenY < limitYMin) {
			screenFlags |= SLIP_CLIP_TOP;
		}
		if (screenY > limitYMax) {
			screenFlags |= SLIP_CLIP_BOTTOM;
		}
		allFlags &= screenFlags;
		nextOffset = SlipBytes_ReadLE32(record + SLIP_DRAW3D_RECORD_NEXT_OFFSET);
		visit = visits + visitCount;
		*visit = (SlipDraw3DPostPlaneBoundsVisit){.recordOffset = recordOffset,
		                                          .screenX = screenX,
		                                          .screenY = screenY,
		                                          .screenFlags = screenFlags,
		                                          .allFlagsAfterAnd = allFlags,
		                                          .nextRecordOffset = nextOffset,
		                                          .loop = nextOffset != loopHeadOffset};
		recordOffset = nextOffset;
		++visitCount;
	} while (recordOffset != loopHeadOffset);
	*result = (SlipDraw3DPostPlaneBounds){.headOffset = headOffset,
	                                      .allFlagsInitial = UINT32_MAX,
	                                      .limitXMin = limitXMin,
	                                      .limitXMax = limitXMax,
	                                      .limitYMin = limitYMin,
	                                      .limitYMax = limitYMax,
	                                      .allFlagsOut = allFlags,
	                                      .visitCount = visitCount,
	                                      .returned = true,
	                                      .carryOut = allFlags != 0};
	if (result->carryOut) {
		result->setRejectCarry = true;
	} else {
		result->clearedRejectCarry = true;
	}
	return 1;
}

int SlipDraw3D_SplitPostPlaneRecord(uint8_t *recordBase, size_t recordBytes, uint32_t targetOffset,
                                    uint32_t otherOffset, SlipDraw3DSplitPostPlane *result) {
	uint8_t *target;
	const uint8_t *other;
	int32_t targetDepth;
	int32_t otherDepth;
	uint32_t depthDelta;
	uint64_t numerator;
	uint64_t quotient;
	uint32_t ratio;
	int32_t xStep;
	int32_t yStep;

	if (recordBase == NULL || result == NULL || (size_t)targetOffset > recordBytes ||
	    recordBytes - (size_t)targetOffset < SLIP_DRAW3D_DRAW_RECORD_SIZE || (size_t)otherOffset > recordBytes ||
	    recordBytes - (size_t)otherOffset < SLIP_DRAW3D_DRAW_RECORD_SIZE) {
		return 0;
	}
	target = recordBase + targetOffset;
	other = recordBase + otherOffset;
	targetDepth = SlipBytes_ReadLEI32(target + SLIP_DRAW3D_PLANE_DISTANCE_OFFSET);
	otherDepth = SlipBytes_ReadLEI32(other + SLIP_DRAW3D_PLANE_DISTANCE_OFFSET);
	depthDelta = (uint32_t)otherDepth - (uint32_t)targetDepth;
	if (depthDelta == 0) {
		return 0;
	}
	numerator = SlipDraw3D_UnsignedHighHalfShiftRightTwo(0u - (uint32_t)targetDepth);
	quotient = numerator / depthDelta;
	if (quotient > UINT32_MAX) {
		return 0;
	}
	ratio = (uint32_t)quotient;
	xStep = SlipDraw3D_MultiplySigned32RoundShift30(
	    (int32_t)((uint32_t)SlipBytes_ReadLEI32(other + offsetof(SlipDraw3DDrawRecord, screenX)) -
	              (uint32_t)SlipBytes_ReadLEI32(target + offsetof(SlipDraw3DDrawRecord, screenX))),
	    ratio);
	SlipDraw3D_WriteI32(
	    target + offsetof(SlipDraw3DDrawRecord, screenX),
	    (int32_t)((uint32_t)SlipBytes_ReadLEI32(target + offsetof(SlipDraw3DDrawRecord, screenX)) + (uint32_t)xStep));
	yStep = SlipDraw3D_MultiplySigned32RoundShift30(
	    (int32_t)((uint32_t)SlipBytes_ReadLEI32(other + offsetof(SlipDraw3DDrawRecord, screenY)) -
	              (uint32_t)SlipBytes_ReadLEI32(target + offsetof(SlipDraw3DDrawRecord, screenY))),
	    ratio);
	SlipDraw3D_WriteI32(
	    target + offsetof(SlipDraw3DDrawRecord, screenY),
	    (int32_t)((uint32_t)SlipBytes_ReadLEI32(target + offsetof(SlipDraw3DDrawRecord, screenY)) + (uint32_t)yStep));
	*result = (SlipDraw3DSplitPostPlane){.targetOffset = targetOffset,
	                                     .otherOffset = otherOffset,
	                                     .targetDepth = targetDepth,
	                                     .otherDepth = otherDepth,
	                                     .depthDelta = depthDelta,
	                                     .interpolationRatio = ratio,
	                                     .xStep = xStep,
	                                     .yStep = yStep,
	                                     .returned = true};
	return 1;
}

int SlipDraw3D_PostPlaneClip(uint8_t *recordBase, size_t recordBytes, uint32_t inputActiveHeadOffset,
                             uint32_t freeHeadOffset, const uint8_t *planeBase, size_t planeBytes,
                             uint32_t planeHeadOffset, size_t maxRecordVisitsPerPlane, size_t maxClipEdgeVisits,
                             SlipDraw3DPostPlaneClipRecordVisit *recordVisits, size_t recordVisitCapacity,
                             SlipDraw3DPostPlaneClipPlaneVisit *planeVisits, size_t planeVisitCapacity,
                             SlipDraw3DPostPlaneClip *result) {
	uint32_t planeOffset;
	uint32_t activeHeadOffset;
	size_t planeVisitCount;
	size_t recordVisitCount;

	if (recordBase == NULL || planeBase == NULL || recordVisits == NULL || planeVisits == NULL || result == NULL ||
	    maxRecordVisitsPerPlane == 0 || !SlipDraw3D_LinkedRecordOffsetValid(recordBytes, inputActiveHeadOffset) ||
	    !SlipDraw3D_LinkedRecordOffsetValid(recordBytes, freeHeadOffset) || (size_t)planeHeadOffset > planeBytes ||
	    planeBytes - (size_t)planeHeadOffset < SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE) {
		return 0;
	}
	planeOffset = planeHeadOffset;
	activeHeadOffset = inputActiveHeadOffset;
	planeVisitCount = 0;
	recordVisitCount = 0;
	for (;;) {
		const uint8_t *plane;
		uint32_t recordOffset;
		uint32_t allFlags;
		uint32_t anyFlags;
		size_t perPlaneRecordVisits;
		SlipDraw3DPostPlaneClipPlaneVisit *planeVisit;

		if (planeVisitCount >= planeVisitCapacity || (size_t)planeOffset > planeBytes ||
		    planeBytes - (size_t)planeOffset < SLIP_DRAW3D_LINKED_DRAW_RECORD_SIZE ||
		    !SlipDraw3D_LinkedRecordOffsetValid(recordBytes, activeHeadOffset)) {
			return 0;
		}
		plane = planeBase + planeOffset;
		planeVisit = planeVisits + planeVisitCount;
		*planeVisit =
		    (SlipDraw3DPostPlaneClipPlaneVisit){.planeOffset = planeOffset, .activeHeadOffset = activeHeadOffset};
		allFlags = UINT32_MAX;
		anyFlags = 0;
		recordOffset = activeHeadOffset;
		perPlaneRecordVisits = 0;
		do {
			uint8_t *record;
			int32_t recordX;
			int32_t recordY;
			int32_t planeX;
			int32_t planeY;
			int16_t planeNormalX;
			int16_t planeNormalY;
			int32_t planeDepth;
			uint32_t flag;
			uint32_t nextRecordOffset;
			SlipDraw3DPostPlaneClipRecordVisit *recordVisit;

			if (recordVisitCount >= recordVisitCapacity || perPlaneRecordVisits >= maxRecordVisitsPerPlane ||
			    !SlipDraw3D_LinkedRecordOffsetValid(recordBytes, recordOffset)) {
				return 0;
			}
			record = recordBase + recordOffset;
			recordX = SlipBytes_ReadLEI32(record + offsetof(SlipDraw3DDrawRecord, screenX));
			recordY = SlipBytes_ReadLEI32(record + offsetof(SlipDraw3DDrawRecord, screenY));
			planeX = SlipBytes_ReadLEI32(plane + offsetof(SlipDraw3DDrawRecord, screenX));
			planeY = SlipBytes_ReadLEI32(plane + offsetof(SlipDraw3DDrawRecord, screenY));
			planeNormalX = (int16_t)SlipBytes_ReadLE16(plane + SLIP_DRAW3D_EDGE_NORMAL_X_OFFSET);
			planeNormalY = (int16_t)SlipBytes_ReadLE16(plane + SLIP_DRAW3D_EDGE_NORMAL_Y_OFFSET);
			planeDepth = SlipDraw3D_PostPlaneDepth(recordX, planeX, planeNormalX, recordY, planeY, planeNormalY);
			SlipDraw3D_WriteI32(record + SLIP_DRAW3D_PLANE_DISTANCE_OFFSET, planeDepth);
			flag = planeDepth < 0 ? SLIP_CLIP_AUXILIARY : 0;
			allFlags &= flag;
			anyFlags |= flag;
			SlipDraw3D_WriteLE32(record + SLIP_DRAW3D_VERTEX_RECORD_FLAGS_OFFSET, flag);
			nextRecordOffset = SlipBytes_ReadLE32(record + SLIP_DRAW3D_RECORD_NEXT_OFFSET);
			recordVisit = recordVisits + recordVisitCount;
			*recordVisit =
			    (SlipDraw3DPostPlaneClipRecordVisit){.planeOffset = planeOffset,
			                                         .recordOffset = recordOffset,
			                                         .screenXDelta = (int32_t)((uint32_t)recordX - (uint32_t)planeX),
			                                         .planeNormalX = planeNormalX,
			                                         .screenYDelta = (int32_t)((uint32_t)recordY - (uint32_t)planeY),
			                                         .planeNormalY = planeNormalY,
			                                         .planeDepth = planeDepth,
			                                         .flag = flag,
			                                         .allFlagsAfterAnd = allFlags,
			                                         .anyFlagsAfter = anyFlags,
			                                         .nextRecordOffset = nextRecordOffset,
			                                         .loop = nextRecordOffset != activeHeadOffset};
			recordOffset = nextRecordOffset;
			++recordVisitCount;
			++perPlaneRecordVisits;
		} while (recordOffset != activeHeadOffset);
		planeVisit->allFlagsAfterLoop = allFlags;
		planeVisit->anyFlagsAfterLoop = anyFlags;
		if (allFlags != 0) {
			planeVisit->rejectAllOutside = true;
			*result = (SlipDraw3DPostPlaneClip){.planeHeadOffset = planeHeadOffset,
			                                    .activeHeadOffsetIn = inputActiveHeadOffset,
			                                    .activeHeadOffsetOut = activeHeadOffset,
			                                    .freeHeadOffset = freeHeadOffset,
			                                    .planeVisitCount = planeVisitCount + 1u,
			                                    .recordVisitCount = recordVisitCount,
			                                    .setRejectCarry = true,
			                                    .returned = true,
			                                    .carryOut = true,
			                                    .branch = SLIP_DRAW3D_POST_PLANE_CLIP_BRANCH_REJECT_ALL_OUTSIDE};
			return 1;
		}
		planeVisit->anyFlagsNonzero = anyFlags != 0;
		if (anyFlags != 0) {
			planeVisit->calledClipEdge = true;
			if (!SlipDraw3D_ClipEdgeList(recordBase, recordBytes, activeHeadOffset, freeHeadOffset, SLIP_CLIP_AUXILIARY,
			                             maxClipEdgeVisits, &planeVisit->clipEdge)) {
				return 0;
			}
			planeVisit->clipEdgeRejected = planeVisit->clipEdge.carryOut;
			if (planeVisit->clipEdgeRejected) {
				*result = (SlipDraw3DPostPlaneClip){.planeHeadOffset = planeHeadOffset,
				                                    .activeHeadOffsetIn = inputActiveHeadOffset,
				                                    .activeHeadOffsetOut = activeHeadOffset,
				                                    .freeHeadOffset = freeHeadOffset,
				                                    .planeVisitCount = planeVisitCount + 1u,
				                                    .recordVisitCount = recordVisitCount,
				                                    .setRejectCarry = true,
				                                    .returned = true,
				                                    .carryOut = true,
				                                    .branch = SLIP_DRAW3D_POST_PLANE_CLIP_BRANCH_REJECT_CLIP_EDGE};
				return 1;
			}
			activeHeadOffset = planeVisit->clipEdge.headOffsetOut;
			planeVisit->calledSplitFirstIntersection = true;
			if (!SlipDraw3D_SplitPostPlaneRecord(recordBase, recordBytes, planeVisit->clipEdge.firstInsideOffset,
			                                     planeVisit->clipEdge.firstOutsideOffset, &planeVisit->splitFirst)) {
				return 0;
			}
			planeVisit->calledSplitSecondIntersection = true;
			if (!SlipDraw3D_SplitPostPlaneRecord(recordBase, recordBytes, planeVisit->clipEdge.targetOffsetOut,
			                                     planeVisit->clipEdge.otherOffsetOut, &planeVisit->splitSecond)) {
				return 0;
			}
		}
		planeVisit->nextPlaneOffset = SlipBytes_ReadLE32(plane + SLIP_DRAW3D_RECORD_NEXT_OFFSET);
		planeVisit->loop = planeVisit->nextPlaneOffset != planeHeadOffset;
		planeOffset = planeVisit->nextPlaneOffset;
		++planeVisitCount;
		if (planeOffset == planeHeadOffset) {
			break;
		}
	}
	*result = (SlipDraw3DPostPlaneClip){.planeHeadOffset = planeHeadOffset,
	                                    .activeHeadOffsetIn = inputActiveHeadOffset,
	                                    .activeHeadOffsetOut = activeHeadOffset,
	                                    .freeHeadOffset = freeHeadOffset,
	                                    .planeVisitCount = planeVisitCount,
	                                    .recordVisitCount = recordVisitCount,
	                                    .clearedRejectCarry = true,
	                                    .returned = true,
	                                    .carryOut = false,
	                                    .branch = SLIP_DRAW3D_POST_PLANE_CLIP_BRANCH_ACCEPT};
	return 1;
}

int SlipDraw3D_ScreenPlaneExecute(uint8_t *recordBase, size_t recordBytes, uint32_t inputActiveHeadOffset,
                                  uint32_t freeHeadOffset, uint32_t anyFlagsEntry, uint32_t projectionMode,
                                  int32_t limitXMin, int32_t limitXMax, int32_t limitYMin, int32_t limitYMax,
                                  int hasPostPlanes, const uint8_t *planeBase, size_t planeBytes,
                                  uint32_t planeHeadOffset, int32_t postLimitXMin, int32_t postLimitXMax,
                                  int32_t postLimitYMin, int32_t postLimitYMax, size_t maxClipEdgeVisits,
                                  SlipDraw3DClipFlagVisit *clipFlagVisits, size_t clipFlagVisitCapacity,
                                  SlipDraw3DPostPlaneBoundsVisit *postBoundsVisits, size_t postBoundsVisitCapacity,
                                  SlipDraw3DPostPlaneClipRecordVisit *postClipRecordVisits,
                                  size_t postClipRecordVisitCapacity,
                                  SlipDraw3DPostPlaneClipPlaneVisit *postClipPlaneVisits,
                                  size_t postClipPlaneVisitCapacity, SlipDraw3DScreenPlaneExecute *result) {
	uint32_t activeHeadOffset;
	uint32_t anyClipFlags;
	SlipDraw3DScreenPlaneDispatch *dispatch;

	if (recordBase == NULL || result == NULL || maxClipEdgeVisits == 0 ||
	    !SlipDraw3D_LinkedRecordOffsetValid(recordBytes, inputActiveHeadOffset) ||
	    !SlipDraw3D_LinkedRecordOffsetValid(recordBytes, freeHeadOffset)) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	activeHeadOffset = inputActiveHeadOffset;
	result->activeHeadOffsetIn = inputActiveHeadOffset;
	result->activeHeadOffsetOut = inputActiveHeadOffset;
	result->freeHeadOffset = freeHeadOffset;
	dispatch = &result->dispatch;
	dispatch->returned = true;
	/* Keep the original ring for GPU triangulation. Clipping a quad first changes
	 * its diagonal as the camera moves, changing the texture mapping. Portal
	 * bounds still require a viewport-clipped ring in the DOS visibility path. */
	dispatch->hasScreenClipFlags =
	    !(SlipRaceGpu_Active() && !hasPostPlanes && (projectionMode & SLIP_INTERPOLATE_TEXTURE)) &&
	    (anyFlagsEntry & SLIP_CLIP_SCREEN) != 0;
	if (dispatch->hasScreenClipFlags) {
		anyClipFlags = anyFlagsEntry;
		dispatch->anyFlagsInitial = anyClipFlags;
		dispatch->needsLeftClip = (anyClipFlags & SLIP_CLIP_LEFT) != 0;
		if (dispatch->needsLeftClip) {
			SlipDraw3DSplitScreenX splitFirst;
			SlipDraw3DSplitScreenX splitSecond;

			dispatch->calledClipLeftEdge = true;
			if (!SlipDraw3D_ClipEdgeList(recordBase, recordBytes, activeHeadOffset, freeHeadOffset, SLIP_CLIP_LEFT,
			                             maxClipEdgeVisits, &result->leftClip)) {
				return 0;
			}
			dispatch->rejectedLeftClip = result->leftClip.carryOut;
			if (dispatch->rejectedLeftClip) {
				dispatch->setRejectCarry = true;
				dispatch->carryOut = true;
				dispatch->branch = SLIP_DRAW3D_SCREEN_PLANE_BRANCH_REJECT_0008;
				result->activeHeadOffsetOut = activeHeadOffset;
				return 1;
			}
			activeHeadOffset = result->leftClip.headOffsetOut;
			dispatch->targetOffsetAfterLeftClip = result->leftClip.targetOffsetOut;
			dispatch->otherOffsetAfterLeftClip = result->leftClip.otherOffsetOut;
			dispatch->firstLeftScreenLimit = (uint32_t)limitXMin;
			dispatch->secondLeftScreenLimit = (uint32_t)limitXMin;
			dispatch->calledSplitFirstLeftIntersection = true;
			if (!SlipDraw3D_SplitScreenXRecord(recordBase, recordBytes, result->leftClip.firstInsideOffset,
			                                   result->leftClip.firstOutsideOffset, projectionMode, limitXMin,
			                                   limitYMin, limitYMax, &splitFirst)) {
				return 0;
			}
			dispatch->calledSplitSecondLeftIntersection = true;
			if (!SlipDraw3D_SplitScreenXRecord(recordBase, recordBytes, result->leftClip.targetOffsetOut,
			                                   result->leftClip.otherOffsetOut, projectionMode, limitXMin, limitYMin,
			                                   limitYMax, &splitSecond)) {
				return 0;
			}
		}
		dispatch->needsRightClip = (dispatch->anyFlagsInitial & SLIP_CLIP_RIGHT) != 0;
		if (dispatch->needsRightClip) {
			SlipDraw3DSplitScreenX splitFirst;
			SlipDraw3DSplitScreenX splitSecond;

			dispatch->calledClipRightEdge = true;
			if (!SlipDraw3D_ClipEdgeList(recordBase, recordBytes, activeHeadOffset, freeHeadOffset, SLIP_CLIP_RIGHT,
			                             maxClipEdgeVisits, &result->rightClip)) {
				return 0;
			}
			dispatch->rejectedRightClip = result->rightClip.carryOut;
			if (dispatch->rejectedRightClip) {
				dispatch->setRejectCarry = true;
				dispatch->carryOut = true;
				dispatch->branch = SLIP_DRAW3D_SCREEN_PLANE_BRANCH_REJECT_0010;
				result->activeHeadOffsetOut = activeHeadOffset;
				return 1;
			}
			activeHeadOffset = result->rightClip.headOffsetOut;
			dispatch->targetOffsetAfterRightClip = result->rightClip.targetOffsetOut;
			dispatch->otherOffsetAfterRightClip = result->rightClip.otherOffsetOut;
			dispatch->firstRightScreenLimit = (uint32_t)limitXMax;
			dispatch->secondRightScreenLimit = (uint32_t)limitXMax;
			dispatch->calledSplitFirstRightIntersection = true;
			if (!SlipDraw3D_SplitScreenXRecord(recordBase, recordBytes, result->rightClip.firstInsideOffset,
			                                   result->rightClip.firstOutsideOffset, projectionMode, limitXMax,
			                                   limitYMin, limitYMax, &splitFirst)) {
				return 0;
			}
			dispatch->calledSplitSecondRightIntersection = true;
			if (!SlipDraw3D_SplitScreenXRecord(recordBase, recordBytes, result->rightClip.targetOffsetOut,
			                                   result->rightClip.otherOffsetOut, projectionMode, limitXMax, limitYMin,
			                                   limitYMax, &splitSecond)) {
				return 0;
			}
		}
		dispatch->calledScanHorizontalClipFlags = true;
		if (!SlipDraw3D_CollectClipFlags(recordBase, recordBytes, activeHeadOffset, clipFlagVisits,
		                                 clipFlagVisitCapacity, &result->clipFlagsAfter)) {
			return 0;
		}
		dispatch->allFlagsAfterHorizontalScan = result->clipFlagsAfter.allFlagsOut;
		dispatch->anyFlagsAfterHorizontalScan = result->clipFlagsAfter.anyFlagsOut;
		if ((dispatch->allFlagsAfterHorizontalScan & SLIP_CLIP_VERTICAL) != 0) {
			dispatch->rejectAfterHorizontalScan = true;
			dispatch->setRejectCarry = true;
			dispatch->carryOut = true;
			dispatch->branch = SLIP_DRAW3D_SCREEN_PLANE_BRANCH_REJECT_AFTER_0018_SCAN;
			result->activeHeadOffsetOut = activeHeadOffset;
			return 1;
		}
		anyClipFlags = result->clipFlagsAfter.anyFlagsOut;
		dispatch->anyFlagsAfterHorizontalScan = anyClipFlags;
		dispatch->needsTopClip = (anyClipFlags & SLIP_CLIP_TOP) != 0;
		if (dispatch->needsTopClip) {
			SlipDraw3DSplitScreenY splitFirst;
			SlipDraw3DSplitScreenY splitSecond;

			dispatch->calledClipTopEdge = true;
			if (!SlipDraw3D_ClipEdgeList(recordBase, recordBytes, activeHeadOffset, freeHeadOffset, SLIP_CLIP_TOP,
			                             maxClipEdgeVisits, &result->topClip)) {
				return 0;
			}
			dispatch->rejectedTopClip = result->topClip.carryOut;
			if (dispatch->rejectedTopClip) {
				dispatch->setRejectCarry = true;
				dispatch->carryOut = true;
				dispatch->branch = SLIP_DRAW3D_SCREEN_PLANE_BRANCH_REJECT_0020;
				result->activeHeadOffsetOut = activeHeadOffset;
				return 1;
			}
			activeHeadOffset = result->topClip.headOffsetOut;
			dispatch->targetOffsetAfterTopClip = result->topClip.targetOffsetOut;
			dispatch->otherOffsetAfterTopClip = result->topClip.otherOffsetOut;
			dispatch->firstTopScreenLimit = (uint32_t)limitYMin;
			dispatch->secondTopScreenLimit = (uint32_t)limitYMin;
			dispatch->calledSplitFirstTopIntersection = true;
			if (!SlipDraw3D_SplitScreenYRecord(recordBase, recordBytes, result->topClip.firstInsideOffset,
			                                   result->topClip.firstOutsideOffset, projectionMode, limitYMin,
			                                   &splitFirst)) {
				return 0;
			}
			dispatch->calledSplitSecondTopIntersection = true;
			if (!SlipDraw3D_SplitScreenYRecord(recordBase, recordBytes, result->topClip.targetOffsetOut,
			                                   result->topClip.otherOffsetOut, projectionMode, limitYMin,
			                                   &splitSecond)) {
				return 0;
			}
		}
		dispatch->needsBottomClip = (dispatch->anyFlagsAfterHorizontalScan & SLIP_CLIP_BOTTOM) != 0;
		if (dispatch->needsBottomClip) {
			SlipDraw3DSplitScreenY splitFirst;
			SlipDraw3DSplitScreenY splitSecond;

			dispatch->calledClipBottomEdge = true;
			if (!SlipDraw3D_ClipEdgeList(recordBase, recordBytes, activeHeadOffset, freeHeadOffset, SLIP_CLIP_BOTTOM,
			                             maxClipEdgeVisits, &result->bottomClip)) {
				return 0;
			}
			dispatch->rejectedBottomClip = result->bottomClip.carryOut;
			if (dispatch->rejectedBottomClip) {
				dispatch->setRejectCarry = true;
				dispatch->carryOut = true;
				dispatch->branch = SLIP_DRAW3D_SCREEN_PLANE_BRANCH_REJECT_0040;
				result->activeHeadOffsetOut = activeHeadOffset;
				return 1;
			}
			activeHeadOffset = result->bottomClip.headOffsetOut;
			dispatch->targetOffsetAfterBottomClip = result->bottomClip.targetOffsetOut;
			dispatch->otherOffsetAfterBottomClip = result->bottomClip.otherOffsetOut;
			dispatch->firstBottomScreenLimit = (uint32_t)limitYMax;
			dispatch->secondBottomScreenLimit = (uint32_t)limitYMax;
			dispatch->calledSplitFirstBottomIntersection = true;
			if (!SlipDraw3D_SplitScreenYRecord(recordBase, recordBytes, result->bottomClip.firstInsideOffset,
			                                   result->bottomClip.firstOutsideOffset, projectionMode, limitYMax,
			                                   &splitFirst)) {
				return 0;
			}
			dispatch->calledSplitSecondBottomIntersection = true;
			if (!SlipDraw3D_SplitScreenYRecord(recordBase, recordBytes, result->bottomClip.targetOffsetOut,
			                                   result->bottomClip.otherOffsetOut, projectionMode, limitYMax,
			                                   &splitSecond)) {
				return 0;
			}
		}
	}
	dispatch->hasPostPlanes = hasPostPlanes != 0;
	if (dispatch->hasPostPlanes) {
		dispatch->calledPostPlaneBounds = true;
		if (!SlipDraw3D_PostPlaneBounds(recordBase, recordBytes, activeHeadOffset, postLimitXMin, postLimitXMax,
		                                postLimitYMin, postLimitYMax, postBoundsVisits, postBoundsVisitCapacity,
		                                &result->postPlaneBounds)) {
			return 0;
		}
		dispatch->postPlaneBoundsRejected = result->postPlaneBounds.carryOut;
		if (dispatch->postPlaneBoundsRejected) {
			dispatch->setRejectCarry = true;
			dispatch->carryOut = true;
			dispatch->branch = SLIP_DRAW3D_SCREEN_PLANE_BRANCH_REJECT_1DE5C;
			result->activeHeadOffsetOut = activeHeadOffset;
			return 1;
		}
		dispatch->calledPostPlaneClip = true;
		if (!SlipDraw3D_PostPlaneClip(recordBase, recordBytes, activeHeadOffset, freeHeadOffset, planeBase, planeBytes,
		                              planeHeadOffset, postClipRecordVisitCapacity, maxClipEdgeVisits,
		                              postClipRecordVisits, postClipRecordVisitCapacity, postClipPlaneVisits,
		                              postClipPlaneVisitCapacity, &result->postPlaneClip)) {
			return 0;
		}
		activeHeadOffset = result->postPlaneClip.activeHeadOffsetOut;
		dispatch->postPlaneClipRejected = result->postPlaneClip.carryOut;
		if (dispatch->postPlaneClipRejected) {
			dispatch->setRejectCarry = true;
			dispatch->carryOut = true;
			dispatch->branch = SLIP_DRAW3D_SCREEN_PLANE_BRANCH_REJECT_1DEB6;
			result->activeHeadOffsetOut = activeHeadOffset;
			return 1;
		}
	}
	dispatch->clearedRejectCarry = true;
	dispatch->carryOut = false;
	dispatch->branch = SLIP_DRAW3D_SCREEN_PLANE_BRANCH_ACCEPT;
	result->activeHeadOffsetOut = activeHeadOffset;
	return 1;
}

int SlipDraw3D_ClipDispatchExecute(
    uint8_t *recordBase, size_t recordBytes, uint32_t inputActiveHeadOffset, uint32_t freeHeadOffset, uint32_t anyFlags,
    uint32_t allFlags, uint32_t renderFlags, uint32_t projectionMode, int32_t limitZMin, int32_t limitZMax,
    int32_t limitXMin, int32_t limitXMax, int32_t limitYMin, int32_t limitYMax, SlipDraw3DProjectFn projectPrimary,
    SlipDraw3DProjectFn projectSecondary, void *userData, int hasPostPlanes, const uint8_t *planeBase,
    size_t planeBytes, uint32_t planeHeadOffset, int32_t postLimitXMin, int32_t postLimitXMax, int32_t postLimitYMin,
    int32_t postLimitYMax, size_t maxClipEdgeVisits, SlipDraw3DClipFlagVisit *clipFlagVisits,
    size_t clipFlagVisitCapacity, SlipDraw3DPostPlaneBoundsVisit *postBoundsVisits, size_t postBoundsVisitCapacity,
    SlipDraw3DPostPlaneClipRecordVisit *postClipRecordVisits, size_t postClipRecordVisitCapacity,
    SlipDraw3DPostPlaneClipPlaneVisit *postClipPlaneVisits, size_t postClipPlaneVisitCapacity,
    SlipDraw3DClipDispatchExecute *result) {
	uint32_t activeHeadOffset;
	uint32_t screenPlaneFlags;
	bool anyMaskedZero;
	bool allMaskedNonzero;
	SlipDraw3DClipDispatch *dispatch;

	if (recordBase == NULL || result == NULL || maxClipEdgeVisits == 0 ||
	    !SlipDraw3D_LinkedRecordOffsetValid(recordBytes, inputActiveHeadOffset) ||
	    !SlipDraw3D_LinkedRecordOffsetValid(recordBytes, freeHeadOffset)) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	activeHeadOffset = inputActiveHeadOffset;
	screenPlaneFlags = anyFlags;
	result->activeHeadOffsetIn = inputActiveHeadOffset;
	result->activeHeadOffsetOut = inputActiveHeadOffset;
	result->freeHeadOffset = freeHeadOffset;
	result->anyFlagsOut = anyFlags;
	dispatch = &result->dispatch;
	anyMaskedZero = (anyFlags & SLIP_DRAW3D_CLIP_MASK) == 0;
	allMaskedNonzero = (allFlags & SLIP_DRAW3D_CLIP_MASK) != 0;
	*dispatch = (SlipDraw3DClipDispatch){
	    .anyFlags = anyFlags, .anyMaskedZero = anyMaskedZero, .allFlags = allFlags, .returned = true};
	if (!anyMaskedZero) {
		dispatch->allMaskedNonzero = allMaskedNonzero;
		if (allMaskedNonzero) {
			dispatch->setRejectCarry = true;
			dispatch->carryOut = true;
			dispatch->branch = SLIP_DRAW3D_CLIP_DISPATCH_BRANCH_REJECT;
			result->activeHeadOffsetOut = activeHeadOffset;
			result->anyFlagsOut = screenPlaneFlags;
			return 1;
		}
		dispatch->calledClipDepth = true;
		if (!SlipDraw3D_ClippedDepthExecute(recordBase, recordBytes, activeHeadOffset, freeHeadOffset, allFlags,
		                                    anyFlags, renderFlags, projectionMode, limitZMin, limitZMax, limitXMin,
		                                    limitXMax, limitYMin, limitYMax, projectPrimary, projectSecondary, userData,
		                                    maxClipEdgeVisits, clipFlagVisits, clipFlagVisitCapacity,
		                                    &result->clippedDepth)) {
			return 0;
		}
		activeHeadOffset = result->clippedDepth.activeHeadOffsetOut;
		screenPlaneFlags = result->clippedDepth.anyFlagsOut;
		dispatch->depthClipRejected = result->clippedDepth.dispatch.carryOut;
		if (dispatch->depthClipRejected) {
			dispatch->carryOut = true;
			dispatch->branch = SLIP_DRAW3D_CLIP_DISPATCH_BRANCH_CLIPPED_RETURN;
			result->activeHeadOffsetOut = activeHeadOffset;
			result->anyFlagsOut = screenPlaneFlags;
			return 1;
		}
	}
	dispatch->calledClipScreen = true;
	if (!SlipDraw3D_ScreenPlaneExecute(
	        recordBase, recordBytes, activeHeadOffset, freeHeadOffset, screenPlaneFlags, projectionMode, limitXMin,
	        limitXMax, limitYMin, limitYMax, hasPostPlanes, planeBase, planeBytes, planeHeadOffset, postLimitXMin,
	        postLimitXMax, postLimitYMin, postLimitYMax, maxClipEdgeVisits, clipFlagVisits, clipFlagVisitCapacity,
	        postBoundsVisits, postBoundsVisitCapacity, postClipRecordVisits, postClipRecordVisitCapacity,
	        postClipPlaneVisits, postClipPlaneVisitCapacity, &result->screenPlane)) {
		return 0;
	}
	activeHeadOffset = result->screenPlane.activeHeadOffsetOut;
	dispatch->screenClipRejected = result->screenPlane.dispatch.carryOut;
	dispatch->carryOut = dispatch->screenClipRejected;
	dispatch->branch = anyMaskedZero ? SLIP_DRAW3D_CLIP_DISPATCH_BRANCH_UNCLIPPED
	                                 : SLIP_DRAW3D_CLIP_DISPATCH_BRANCH_CLIPPED_THEN_UNCLIPPED;
	result->activeHeadOffsetOut = activeHeadOffset;
	result->anyFlagsOut = screenPlaneFlags;
	return 1;
}

int SlipDraw3D_LoadStateRecord(const SlipDraw3DStateRecord *recordBase, size_t recordCount, uint16_t recordIndex,
                               SlipDraw3DStateLoad *result) {
	if (recordBase == NULL || result == NULL || (size_t)recordIndex >= recordCount) {
		return 0;
	}
	result->recordIndex = recordIndex;
	result->recordPointer = &recordBase[recordIndex];
	return 1;
}

int SlipDraw3D_SetOrigin(SlipDraw3DStateRecord *drawStateRecord, const SlipView3DMatrix *destinationMatrix,
                         const SlipView3DMatrix *originMatrix, SlipDraw3DVec32 cameraOrigin,
                         SlipDraw3DVec32 drawPosition, bool lightEnabled, SlipDraw3DVec32 lightInput,
                         SlipDraw3DOriginSetup *result) {
	SlipDraw3DVec32 originDelta;
	SlipDraw3DVec32 origin;
	SlipDraw3DVec32 lightStore = {0, 0, 0};
	bool directOriginBranch;

	if (drawStateRecord == 0 || destinationMatrix == 0 || result == 0) {
		return 0;
	}

	drawStateRecord->matrix = *destinationMatrix;
	directOriginBranch = originMatrix == 0;
	originDelta = (SlipDraw3DVec32){(int32_t)((uint32_t)cameraOrigin.x - (uint32_t)drawPosition.x),
	                                (int32_t)((uint32_t)cameraOrigin.y - (uint32_t)drawPosition.y),
	                                (int32_t)((uint32_t)cameraOrigin.z - (uint32_t)drawPosition.z)};
	if (directOriginBranch) {
		origin = originDelta;
		if (lightEnabled) {
			lightStore = lightInput;
		}
	} else {
		SlipView3DVec32 transformedOrigin = SlipView3D_TransformPositionByRows(
		    originMatrix, (SlipView3DVec32){originDelta.x, originDelta.y, originDelta.z});

		origin = (SlipDraw3DVec32){transformedOrigin.x, transformedOrigin.y, transformedOrigin.z};
		if (lightEnabled) {
			SlipView3DVec32 transformedLight =
			    SlipView3D_TransformVector(originMatrix, (SlipView3DVec32){lightInput.x, lightInput.y, lightInput.z});

			lightStore = (SlipDraw3DVec32){transformedLight.x, transformedLight.y, transformedLight.z};
		}
	}
	drawStateRecord->origin = origin;
	if (lightEnabled) {
		drawStateRecord->lightVector = lightStore;
	}
	*result = (SlipDraw3DOriginSetup){drawPosition,       destinationMatrix, originMatrix, true,
	                                  directOriginBranch, cameraOrigin,      originDelta,  origin,
	                                  lightEnabled,       lightInput,        lightStore,   true};
	return 1;
}

int SlipDraw3D_RestoreVertexBufferCursor(uint32_t vertexBufferCursor, const SlipDraw3DStateRecord *drawStateRecord,
                                         SlipDraw3DRestoreVertexBufferCursor *result) {
	uint32_t savedVertexBufferCursor;
	uint32_t discardedVertexBufferBytes;

	if (drawStateRecord == NULL || result == NULL) {
		return 0;
	}
	savedVertexBufferCursor = drawStateRecord->vertexBufferCursor;
	discardedVertexBufferBytes = vertexBufferCursor - savedVertexBufferCursor;
	*result =
	    (SlipDraw3DRestoreVertexBufferCursor){vertexBufferCursor, savedVertexBufferCursor, discardedVertexBufferBytes,
	                                          vertexBufferCursor - discardedVertexBufferBytes};
	return 1;
}

uint32_t SlipDraw3D_CurrentStateRecordIndex(uint32_t recordIndex) { return recordIndex; }

int SlipDraw3D_BackgroundSetup(const uint8_t *materialTable, size_t materialTableBytes,
                               const SlipView3DMatrix *viewMatrix, uint32_t backgroundDistance, uint32_t backgroundSpan,
                               uint16_t backgroundMaterialIndex, uint8_t materialStripCount, uint8_t fixedStripCount,
                               uint16_t stripMaterialIndex, uint16_t stripCurvature, uint32_t projectionScale,
                               uint32_t cachedProjectionScale, uint16_t projectionRevision,
                               uint8_t cachedFixedStripCount, uint8_t cachedEndValueByte, uint16_t cachedStripCurvature,
                               uint16_t materialFillValue, SlipDraw3DBackgroundSetup *result) {
	SlipDraw3DBackgroundSetup out;
	uint16_t materialIndex;
	uint16_t materialCount;
	uint32_t materialOffset;
	uint16_t projectionRevisionAfterScale;
	uint16_t viewTilt;
	uint16_t fixedFillThreshold;

	if (materialTable == NULL || viewMatrix == NULL || result == NULL || materialTableBytes < sizeof(uint16_t)) {
		return 0;
	}

	projectionRevisionAfterScale = projectionRevision;
	if (projectionScale != cachedProjectionScale) {
		projectionRevisionAfterScale = (uint16_t)(projectionRevisionAfterScale + 1u);
	}
	materialIndex = stripMaterialIndex;
	materialCount = SlipBytes_ReadLE16(materialTable);
	if (materialIndex < materialCount) {
		materialOffset = ((SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE * materialIndex) & UINT16_MAX) +
		                 SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES;
	} else {
		materialOffset = SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES;
	}
	if ((size_t)materialOffset + SLIP_DRAW3D_MATERIAL_RAMP_END_FIRST_BYTE_END > materialTableBytes) {
		return 0;
	}

	viewTilt = (uint16_t)viewMatrix->m[7];

	fixedFillThreshold = (uint16_t)(stripCurvature + SLIP_BACKGROUND_FIXED_FILL_TILT_BIAS_Q14);
	out = (SlipDraw3DBackgroundSetup){backgroundDistance,
	                                  backgroundSpan,
	                                  backgroundMaterialIndex,
	                                  materialStripCount,
	                                  projectionScale,
	                                  cachedProjectionScale,
	                                  projectionScale != cachedProjectionScale,
	                                  projectionScale,
	                                  projectionRevisionAfterScale,
	                                  materialIndex,
	                                  materialCount,
	                                  materialOffset,
	                                  materialTable[materialOffset + SLIP_DRAW3D_MATERIAL_RAMP_START_OFFSET],
	                                  materialTable[materialOffset + SLIP_DRAW3D_MATERIAL_RAMP_END_OFFSET],
	                                  fixedStripCount,
	                                  cachedFixedStripCount,
	                                  (uint8_t)projectionRevisionAfterScale,
	                                  cachedEndValueByte,
	                                  stripCurvature,
	                                  cachedStripCurvature,
	                                  false,
	                                  true,
	                                  true,
	                                  viewTilt,
	                                  fixedFillThreshold,
	                                  SLIP_DRAW3D_BACKGROUND_SETUP_STRIPS,
	                                  false,
	                                  0,
	                                  0,
	                                  {0},
	                                  0,
	                                  0,
	                                  0,
	                                  0,
	                                  0,
	                                  0,
	                                  0,
	                                  0,
	                                  false,
	                                  false,
	                                  false,
	                                  false,
	                                  0,
	                                  0,
	                                  0,
	                                  true};
	out.callDraw3DBackgroundStripBuild =
	    out.fixedStripCount != out.cachedFixedStripCount || out.materialStartValueByte != out.cachedStartValueByte ||
	    out.materialEndValueByte != out.cachedEndValueByte || out.stripCurvature != out.cachedStripCurvature;

	if ((int16_t)viewTilt >= (int16_t)fixedFillThreshold) {
		out.branch = SLIP_DRAW3D_BACKGROUND_SETUP_FIXED_FILL;
		out.materialFillFlag = 0;
		out.fixedFillFlag = SLIP_BACKGROUND_FILL_ENABLED;
	} else if ((int16_t)viewTilt <= SLIP_BACKGROUND_MATERIAL_FILL_TILT_MAXIMUM_Q14) {
		out.branch = SLIP_DRAW3D_BACKGROUND_SETUP_MATERIAL_FILL;
		out.callDraw3DBackgroundMaterial = true;
		out.callDraw3DBackgroundValue = true;
		out.materialFillValue = materialFillValue;
		out.materialFillFlag = SLIP_BACKGROUND_FILL_ENABLED;
		out.fixedFillFlag = 0;
	} else {
		out.branch = SLIP_DRAW3D_BACKGROUND_SETUP_STRIPS;
		out.callDraw3DNormalizeVector2D = true;
		out.viewRollX = (uint16_t)viewMatrix->m[1];
		out.viewRollY = (uint16_t)viewMatrix->m[4];
		if (!SlipDraw3D_NormalizeVector2D(out.viewRollX, out.viewRollY, &out.normalize)) {
			return 0;
		}
		out.spriteScaleX = (uint16_t)(0u - (uint16_t)out.normalize.unitXQ14);
		out.spriteScaleY = (uint16_t)out.normalize.unitYQ14;
		out.spriteHalfWidth = (uint16_t)((int16_t)out.spriteScaleY >> 1);
		out.spriteHalfHeight = (uint16_t)(out.normalize.unitXQ14 >> 1);

		g_spriteScaleX = out.spriteScaleX;
		g_spriteScaleY = out.spriteScaleY;
		g_spriteHalfWidth = out.spriteHalfWidth;
		g_spriteHalfHeight = out.spriteHalfHeight;
		out.tiltComponent = (uint16_t)(viewTilt - SLIP_BACKGROUND_TILT_CENTRE_BIAS_Q14);
		out.tiltComponentSquare = (uint32_t)((int32_t)(int16_t)out.tiltComponent * (int32_t)(int16_t)out.tiltComponent);
		out.complementSquare = (SLIP_Q14_ONE * SLIP_Q14_ONE) - out.tiltComponentSquare;
		out.complementComponent = SlipDraw3D_Root32(out.complementSquare);
		out.callDraw3DBackgroundStripTableFixed = true;
		out.callDraw3DBackgroundStripTableMaterial = true;
		out.fixedFillFlag = 0;
		out.materialFillFlag = 0;
	}

	*result = out;
	return 1;
}

int SlipDraw3D_BackgroundStripBuild(const SlipView3DMaths *maths, uint8_t *fixedStripTable, size_t stripTableBytes,
                                    uint8_t fixedStripCount, uint8_t startValueByte, uint8_t endValueByte,
                                    uint16_t stripCurvature, uint32_t scale, SlipDraw3DBackgroundStripBuild *result) {
	uint16_t countWord;
	uint32_t divisor;
	uint16_t stepLow = 0;
	uint16_t stepHigh = 0;
	uint16_t currentLow = 0;
	uint16_t currentWord;
	uint16_t previousStripValue = SLIP_BACKGROUND_NO_PREVIOUS_STRIP;
	uint16_t angle = 0;
	uint16_t angleStep;
	uint16_t scaleStep;
	uint32_t remainingStrips;
	size_t stripOffset;
	uint16_t outCount;
	uint16_t diagnosticCount = 0;
	uint16_t diagnosticAngles[SLIP_BACKGROUND_STRIP_DIAGNOSTIC_CAPACITY] = {0};
	uint16_t diagnosticTrig[SLIP_BACKGROUND_STRIP_DIAGNOSTIC_CAPACITY] = {0};
	uint16_t diagnosticStripValues[SLIP_BACKGROUND_STRIP_DIAGNOSTIC_CAPACITY] = {0};

	if (maths == NULL || fixedStripTable == NULL || result == NULL || fixedStripCount == 0 ||
	    stripTableBytes < SLIP_BACKGROUND_STRIP_TABLE_HEADER_BYTES +
	                          ((size_t)fixedStripCount + SLIP_BACKGROUND_FIXED_STRIP_RESERVED_ENTRIES) *
	                              SLIP_BACKGROUND_STRIP_BYTES) {
		return 0;
	}

	countWord = fixedStripCount;
	stripOffset = SLIP_BACKGROUND_STRIP_TABLE_HEADER_BYTES;
	SlipDraw3D_WriteLE16(fixedStripTable, 0);
	divisor = (uint32_t)countWord - 1u;
	currentWord = startValueByte;
	if (divisor != 0u) {
		const int32_t dividend = (int32_t)(int16_t)((uint16_t)startValueByte - (uint16_t)endValueByte);
		const int32_t quotient = (int32_t)((dividend << SLIP_BACKGROUND_STRIP_VALUE_FRACTION_BITS) / (int32_t)divisor);

		stepLow = (uint16_t)quotient;
		stepHigh = (uint16_t)(quotient >> SLIP_BACKGROUND_STRIP_VALUE_FRACTION_BITS);
		currentWord = endValueByte;
	}

	scaleStep = (uint16_t)((((int32_t)(int16_t)stripCurvature
	                         << (SLIP_Q14_FRACTION_BITS + SLIP_BACKGROUND_STRIP_CURVATURE_PRESCALE_BITS)) >>
	                        SLIP_BACKGROUND_STRIP_CURVATURE_PRESCALE_BITS) /
	                       SLIP_BACKGROUND_STRIP_CURVATURE_DIVISOR);
	angleStep = (uint16_t)(SLIP_BACKGROUND_STRIP_ANGLE_SPAN / countWord);
	outCount = 0;
	remainingStrips = countWord;
	while (remainingStrips != 0u) {
		const int16_t tangent = SlipView3D_TanQ14(maths, (int16_t)angle);
		uint16_t stripValue;

		SlipDraw3D_WriteLE16(fixedStripTable + stripOffset, currentWord);
		stripValue = SlipDraw3D_SignedProductShift14LowWord((int32_t)tangent * tangent);
		stripValue = SlipDraw3D_UnsignedProductShift14LowWord((uint32_t)stripValue * (uint16_t)scale);
		stripValue = (uint16_t)(0u - stripValue);
		stripValue = SlipDraw3D_SignedProductShift14LowWord((int32_t)(int16_t)stripValue * (int32_t)(int16_t)scaleStep);
		if (diagnosticCount < SLIP_BACKGROUND_STRIP_DIAGNOSTIC_CAPACITY) {
			diagnosticAngles[diagnosticCount] = angle;
			diagnosticTrig[diagnosticCount] = (uint16_t)tangent;
			diagnosticStripValues[diagnosticCount] = stripValue;
			++diagnosticCount;
		}
		SlipDraw3D_WriteLE16(fixedStripTable + stripOffset + SLIP_BACKGROUND_STRIP_CENTRE_OFFSET, stripValue);
		--remainingStrips;
		if (stripValue != previousStripValue) {
			previousStripValue = stripValue;
			stripOffset += SLIP_BACKGROUND_STRIP_BYTES;
			++outCount;
			SlipDraw3D_WriteLE16(fixedStripTable, outCount);
		}
		{
			const uint32_t sumLow = (uint32_t)currentLow + stepLow;
			const uint16_t carry = sumLow > UINT16_MAX ? 1u : 0u;

			currentLow = (uint16_t)sumLow;
			currentWord = (uint16_t)(currentWord + stepHigh + carry);
		}
		angle = (uint16_t)(angle + angleStep);
	}
	++outCount;
	SlipDraw3D_WriteLE16(fixedStripTable, outCount);
	if (stripOffset == SLIP_BACKGROUND_STRIP_TABLE_HEADER_BYTES) {
		stripOffset += sizeof(uint16_t);
	}
	SlipDraw3D_WriteLE16(fixedStripTable + stripOffset + SLIP_BACKGROUND_STRIP_CENTRE_OFFSET,
	                     (uint16_t)SLIP_BACKGROUND_STRIP_FINAL_CENTRE_Q14);
	SlipDraw3D_WriteLE16(fixedStripTable + stripOffset - SLIP_BACKGROUND_STRIP_BYTES, startValueByte);
	*result = (SlipDraw3DBackgroundStripBuild){fixedStripCount,
	                                           startValueByte,
	                                           endValueByte,
	                                           stripCurvature,
	                                           scale,
	                                           outCount,
	                                           startValueByte,
	                                           diagnosticCount,
	                                           {0},
	                                           {0},
	                                           {0},
	                                           true};
	memcpy(result->diagnosticAngles, diagnosticAngles, sizeof(diagnosticAngles));
	memcpy(result->diagnosticTangents, diagnosticTrig, sizeof(diagnosticTrig));
	memcpy(result->diagnosticStripOffsets, diagnosticStripValues, sizeof(diagnosticStripValues));
	return 1;
}

int SlipDraw3D_BackgroundSetupExecute(
    const SlipView3DMaths *maths, uint8_t *materialStripTable, size_t materialStripTableBytes, uint8_t *fixedStripTable,
    size_t fixedStripTableBytes, const uint8_t *materialTable, size_t materialTableBytes,
    const SlipView3DMatrix *viewMatrix, uint32_t backgroundDistance, uint32_t backgroundSpan,
    uint16_t backgroundMaterialIndex, uint8_t materialStripCount, uint8_t fixedStripCount, uint16_t stripMaterialIndex,
    uint16_t stripCurvature, uint32_t projectionScale, uint32_t cachedProjectionScale, uint16_t projectionRevision,
    uint8_t cachedFixedStripCount, uint8_t cachedEndValueByte, uint16_t cachedStripCurvature, uint32_t viewportX,
    uint32_t viewportY, uint32_t detailScale, const SlipDraw3DStateRecord *stateRecord, uint32_t fadeStart,
    uint32_t fadeEnd, uint32_t fadeRange, uint32_t ambientLight, uint32_t fadeColour, uint32_t limitEnabled,
    uint32_t limitStart, uint32_t limitEnd, SlipDraw3DBackgroundStripTableVisitFixed *fixedStripVisits,
    size_t fixedStripVisitCapacity, SlipDraw3DBackgroundStripTableVisitMaterial *materialStripVisits,
    size_t materialStripVisitCapacity, SlipDraw3DBackgroundSetupExecute *result) {
	SlipDraw3DBackgroundSetupExecute out;

	if (result == NULL) {
		return 0;
	}
	memset(&out, 0, sizeof(out));
	if (!SlipDraw3D_BackgroundSetup(materialTable, materialTableBytes, viewMatrix, backgroundDistance, backgroundSpan,
	                                backgroundMaterialIndex, materialStripCount, fixedStripCount, stripMaterialIndex,
	                                stripCurvature, projectionScale, cachedProjectionScale, projectionRevision,
	                                cachedFixedStripCount, cachedEndValueByte, cachedStripCurvature, 0, &out.setup)) {
		return 0;
	}
	out.fixedFillFlag = out.setup.fixedFillFlag;
	out.materialFillFlag = out.setup.materialFillFlag;
	out.materialFillValue = out.setup.materialFillValue;
	out.fixedFillValue = out.setup.projectionRevisionAfterScaleCheck;
	out.callDraw3DBackgroundStripBuild = out.setup.callDraw3DBackgroundStripBuild;
	if (out.callDraw3DBackgroundStripBuild && maths != NULL) {
		if (!SlipDraw3D_BackgroundStripBuild(maths, fixedStripTable, fixedStripTableBytes, fixedStripCount,
		                                     out.setup.materialStartValueByte, out.setup.materialEndValueByte,
		                                     stripCurvature, out.setup.cachedProjectionScaleOut, &out.stripBuild)) {
			return 0;
		}
		out.fixedFillValue = out.stripBuild.projectionRevisionAfterBuild;
	}

	if (out.setup.branch == SLIP_DRAW3D_BACKGROUND_SETUP_MATERIAL_FILL) {
		const uint8_t *materialRecord;
		size_t materialRecordBytes;

		out.callBackgroundMaterialForFill = true;
		if (!SlipDraw3D_BackgroundMaterial(materialTable, materialTableBytes, backgroundMaterialIndex, detailScale,
		                                   stateRecord, &out.materialForFill)) {
			return 0;
		}
		if ((size_t)out.materialForFill.selectedMaterialRecordOffset > materialTableBytes) {
			return 0;
		}
		materialRecord = materialTable + out.materialForFill.selectedMaterialRecordOffset;
		materialRecordBytes = materialTableBytes - (size_t)out.materialForFill.selectedMaterialRecordOffset;
		out.callBackgroundValueForFill = true;
		if (!SlipDraw3D_BackgroundValueExecute(materialRecord, materialRecordBytes, backgroundDistance,
		                                       out.materialForFill.lightValue, fadeStart, fadeEnd, fadeRange,
		                                       detailScale, ambientLight, fadeColour, limitEnabled, limitStart,
		                                       limitEnd, &out.valueForFill)) {
			return 0;
		}
		out.materialFillValue = out.valueForFill.value;
		out.setup.materialFillValue = out.materialFillValue;
	} else if (out.setup.branch == SLIP_DRAW3D_BACKGROUND_SETUP_STRIPS) {
		out.callDraw3DBackgroundStripTableFixed = true;
		if (!SlipDraw3D_BackgroundStripTableFixed(
		        fixedStripTable, fixedStripTableBytes, backgroundSpan, out.setup.tiltComponent,
		        out.setup.complementComponent, backgroundDistance, projectionScale, viewportX, viewportY,
		        out.setup.spriteScaleX, out.setup.spriteScaleY, out.setup.spriteHalfWidth, out.setup.spriteHalfHeight,
		        fixedStripVisits, fixedStripVisitCapacity, &out.fixedStripTable)) {
			return 0;
		}
		out.callDraw3DBackgroundStripTableMaterial = true;
		if (!SlipDraw3D_BackgroundStripTableMaterial(
		        materialStripTable, materialStripTableBytes, materialTable, materialTableBytes, backgroundMaterialIndex,
		        detailScale, stateRecord, materialStripCount, fadeStart, fadeEnd, fadeRange, ambientLight, fadeColour,
		        backgroundSpan, backgroundDistance, out.setup.tiltComponent, out.setup.complementComponent,
		        projectionScale, viewportX, viewportY, out.setup.spriteScaleX, out.setup.spriteScaleY,
		        out.setup.spriteHalfWidth, out.setup.spriteHalfHeight, limitEnabled, limitStart, limitEnd,
		        materialStripVisits, materialStripVisitCapacity, &out.materialStripTable)) {
			return 0;
		}
	}

	out.returned = out.setup.returned;
	*result = out;
	return 1;
}

static void SlipDraw3D_MultiplySigned32ToHalves(int32_t a, int32_t b, uint32_t *low, uint32_t *high) {
	const int64_t product = (int64_t)a * (int64_t)b;

	*low = (uint32_t)product;
	*high = (uint32_t)((uint64_t)product >> 32);
}

static void SlipDraw3D_Add64Words(uint32_t lowA, uint32_t highA, uint32_t lowB, uint32_t highB, uint32_t *lowOut,
                                  uint32_t *highOut) {
	const uint32_t low = lowA + lowB;
	const uint32_t carry = low < lowA ? 1u : 0u;

	*lowOut = low;
	*highOut = highA + highB + carry;
}

static uint32_t SlipDraw3D_RoundShift14Low32FromHalves(uint32_t low, uint32_t high, bool *carryOut) {
	const uint32_t shifted = (low >> SLIP_Q14_FRACTION_BITS) | (high << SLIP_Q14_DWORD_HIGH_SHIFT);
	const uint32_t carry = (low >> (SLIP_Q14_FRACTION_BITS - 1)) & 1u;

	if (carryOut != NULL) {
		*carryOut = carry != 0u;
	}
	return shifted + carry;
}

static int64_t SlipDraw3D_I64FromU64(uint64_t value) {
	if (value <= (uint64_t)INT64_MAX) {
		return (int64_t)value;
	}
	if (value == (UINT64_C(1) << 63)) {
		return INT64_MIN;
	}
	return -(int64_t)(~value + 1u);
}

static uint32_t SlipDraw3D_RoundSignedHalf(uint32_t value) {
	const uint32_t shifted = (value >> 1) | (value & SLIP_DRAW3D_DWORD_SIGN_BIT);
	const uint32_t carry = value & 1u;

	return shifted + carry;
}

static void SlipDraw3D_MultiplySignedWords(uint16_t multiplicand, uint16_t operand, uint16_t *productLowWordOut,
                                           uint16_t *productHighWordOut) {
	const int32_t product = (int32_t)(int16_t)multiplicand * (int32_t)(int16_t)operand;

	*productLowWordOut = (uint16_t)product;
	*productHighWordOut = (uint16_t)((uint32_t)product >> 16);
}

static void SlipDraw3D_MultiplyUnsignedWords(uint16_t multiplicand, uint16_t operand, uint16_t *productLowWordOut,
                                             uint16_t *productHighWordOut) {
	const uint32_t product = (uint32_t)multiplicand * (uint32_t)operand;

	*productLowWordOut = (uint16_t)product;
	*productHighWordOut = (uint16_t)(product >> 16);
}

static uint16_t SlipDraw3D_Shift14LowWordFromHalves(uint16_t productLowWord, uint16_t productHighWord) {
	return (uint16_t)((uint16_t)(productLowWord >> SLIP_Q14_FRACTION_BITS) |
	                  (uint16_t)(productHighWord << SLIP_Q14_WORD_HIGH_SHIFT));
}

static uint32_t SlipDraw3D_MultiplyUnsignedLowWordPreserveHigh(uint32_t multiplicand, uint16_t operand) {
	uint16_t productLowWord;
	uint16_t productHighWord;

	SlipDraw3D_MultiplyUnsignedWords((uint16_t)multiplicand, operand, &productLowWord, &productHighWord);
	return (multiplicand & SLIP_DRAW3D_UPPER_WORD_MASK) |
	       SlipDraw3D_Shift14LowWordFromHalves(productLowWord, productHighWord);
}

static uint32_t SlipDraw3D_MultiplySignedLowWordPreserveHigh(uint32_t multiplicand, uint16_t operand) {
	const int32_t product = (int32_t)(int16_t)(uint16_t)multiplicand * (int32_t)(int16_t)operand;
	const uint16_t productLowWord = (uint16_t)product;
	const uint16_t productHighWord = (uint16_t)((uint32_t)product >> 16);

	return (multiplicand & SLIP_DRAW3D_UPPER_WORD_MASK) |
	       SlipDraw3D_Shift14LowWordFromHalves(productLowWord, productHighWord);
}

int SlipDraw3D_LightingMaterial(const SlipDraw3DMaterialRecord *materialRecord, size_t materialRecordBytes,
                                uint32_t inputFadeBlend, uint16_t diffuseLight, uint16_t specularLight,
                                uint32_t directLight, uint32_t ambientLight, uint32_t fadeColour, uint32_t fadeStart,
                                SlipDraw3DLightingMaterial *result) {
	SlipDraw3DLightingMaterial out;
	uint32_t shadeAccumulator;
	uint32_t scaledContribution;
	uint32_t fadeBlend = inputFadeBlend;
	const SlipDraw3DMaterialRecord *const material = materialRecord;

	if (materialRecord == NULL ||
	    materialRecordBytes < offsetof(SlipDraw3DMaterialRecord, specularCoefficient) + sizeof(uint32_t) ||
	    result == NULL) {
		return 0;
	}

	out = (SlipDraw3DLightingMaterial){inputFadeBlend,
	                                   diffuseLight,
	                                   specularLight,
	                                   material->fixedShade,
	                                   material->ambientCoefficient,
	                                   material->diffuseCoefficient,
	                                   material->specularCoefficient,
	                                   directLight,
	                                   ambientLight,
	                                   fadeColour,
	                                   fadeStart,
	                                   SLIP_DRAW3D_LIGHTING_MATERIAL_BRANCH_VECTOR,
	                                   0,
	                                   fadeBlend,
	                                   false,
	                                   false,
	                                   false,
	                                   false,
	                                   false,
	                                   false,
	                                   false,
	                                   0,
	                                   true};

	if (out.fixedValue != 0u) {
		out.branch = SLIP_DRAW3D_LIGHTING_MATERIAL_BRANCH_FIXED;
		shadeAccumulator = out.fixedValue;
		scaledContribution = SLIP_Q14_ONE - shadeAccumulator;
		scaledContribution =
		    SlipDraw3D_MultiplyUnsignedLowWordPreserveHigh(scaledContribution, (uint16_t)inputFadeBlend);
		fadeBlend = scaledContribution;
	} else if ((out.ambientCoefficient | out.diffuseCoefficient | out.specularCoefficient) == 0u) {
		out.branch = SLIP_DRAW3D_LIGHTING_MATERIAL_BRANCH_AMBIENT;
		shadeAccumulator = directLight + ambientLight;
	} else {
		shadeAccumulator = 0;
		if (out.ambientCoefficient != 0u) {
			scaledContribution =
			    SlipDraw3D_MultiplyUnsignedLowWordPreserveHigh(ambientLight, (uint16_t)out.ambientCoefficient);
			shadeAccumulator = scaledContribution;
		}
		if (diffuseLight != 0u) {
			scaledContribution = SlipDraw3D_MultiplyUnsignedLowWordPreserveHigh(out.diffuseCoefficient, diffuseLight);
			shadeAccumulator += scaledContribution;
		}
		if (specularLight != 0u && out.specularCoefficient != 0u) {
			scaledContribution =
			    SlipDraw3D_MultiplyUnsignedLowWordPreserveHigh(specularLight, (uint16_t)out.specularCoefficient);
			shadeAccumulator += scaledContribution;
		}
		if (shadeAccumulator > SLIP_Q14_ONE) {
			shadeAccumulator = SLIP_Q14_ONE;
			out.clampedUnsigned = true;
		}
	}

	out.shadeBeforeFade = shadeAccumulator;
	out.fadeBlend = fadeBlend;
	if (fadeBlend == 0u) {
		out.skippedZeroFadeBlend = true;
		out.shade = shadeAccumulator;
		*result = out;
		return 1;
	}
	if (fadeStart == 0u) {
		out.skippedBlendNoStart = true;
		out.shade = shadeAccumulator;
		*result = out;
		return 1;
	}
	if (fadeBlend == SLIP_Q14_ONE) {
		out.fullBlend = true;
		out.shade = fadeColour;
		*result = out;
		return 1;
	}

	out.interpolatedBlend = true;
	scaledContribution = fadeColour - shadeAccumulator;
	scaledContribution = SlipDraw3D_MultiplySignedLowWordPreserveHigh(scaledContribution, (uint16_t)fadeBlend);
	shadeAccumulator += scaledContribution;
	if ((int32_t)shadeAccumulator < 0) {
		shadeAccumulator = 0;
		out.clampedNegative = true;
	} else if ((int32_t)shadeAccumulator > SLIP_Q14_ONE) {
		shadeAccumulator = SLIP_Q14_ONE;
		out.clampedHigh = true;
	}
	out.shade = shadeAccumulator;
	*result = out;
	return 1;
}

int SlipDraw3D_PolygonColor(const SlipDraw3DMaterialRecord *material, int16_t normalX, int16_t normalY, int16_t normalZ,
                            const SlipDraw3DVertexLighting *state, SlipDraw3DVertexRecord *vertices, size_t vertexCount,
                            const uint8_t *indices, size_t indexBytes, uint16_t count, uint32_t inverseProjectionScale,
                            uint32_t *color) {
	uint32_t depth = 0;
	uint32_t blend = 0;
	uint16_t diffuse = (uint16_t)normalY;
	if (state->fadeStart != 0) {
		SlipDraw3DPerspectiveDepthVisit visits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
		SlipDraw3DPerspectiveDepth result;
		if (!SlipDraw3D_PerspectiveDepth(vertices, vertexCount, indices, indexBytes, count, inverseProjectionScale,
		                                 state->transform, state->transformContext, visits,
		                                 sizeof(visits) / sizeof(visits[0]), &result))
			return 0;
		depth = result.fadeDepth;
	}

	if (material->fixedShade == 0 && material->diffuseCoefficient != 0) {
		diffuse = 0;
		if (state->direct != 0) {
			const int64_t dot = (int64_t)normalX * (int16_t)state->light.x +
			                    (int64_t)normalY * (int16_t)state->light.y + (int64_t)normalZ * (int16_t)state->light.z;
			const int32_t projected = -(int32_t)(int16_t)((uint64_t)dot >> SLIP_Q14_FRACTION_BITS);
			if (projected >= 0)
				diffuse =
				    (uint16_t)(((uint32_t)(uint16_t)projected * (uint16_t)state->direct) >> SLIP_Q14_FRACTION_BITS);
		}
	}
	if (state->fadeStart != 0) {
		SlipDraw3DLightDepthBlend result;
		SlipDraw3D_LightDepthBlend(depth, state->fadeStart, state->fadeEnd, state->fadeRange, &result);
		blend = result.fadeBlendQ14;
	}
	SlipDraw3DLightingMaterial lighting;
	SlipDraw3D_LightingMaterial(material, sizeof(*material), blend, diffuse, 0, state->direct, state->ambient,
	                            state->fadeShade, state->fadeStart, &lighting);
	const uint32_t start = state->overrideRamp == 0 ? material->rampStart : state->rampStart;
	const uint32_t end = state->overrideRamp == 0 ? material->rampEnd : state->rampEnd;
	*color = SlipDraw3D_MultiplyUnsignedLowWordPreserveHigh(end - start, (uint16_t)lighting.shade) + start;
	return 1;
}

static uint16_t standaloneSpecularTable[SLIP_Q14_ONE + 1];
static const uint16_t *boundSpecularTable = standaloneSpecularTable;
static uint32_t specularThreshold;

void SlipDraw3D_BindSpecularTable(const uint16_t *table, uint32_t threshold) {
	boundSpecularTable = table != NULL ? table : standaloneSpecularTable;
	specularThreshold = threshold;
}

void SlipDraw3D_InstallSpecularTable(void) {
	boundSpecularTable = standaloneSpecularTable;
	uint32_t value = 0;
	uint32_t power;
	do {
		power = value;
		for (unsigned step = 0; step < SLIP_DRAW3D_SPECULAR_SQUARING_STEPS; ++step) {
			const uint16_t doubled = (uint16_t)(power << 1);
			power = ((uint32_t)doubled * doubled) >> SLIP_WORD_BITS;
		}
		if (power == 0)
			++value;
	} while (power == 0);
	specularThreshold = value;
	for (; value <= SLIP_Q14_ONE; ++value) {
		power = value;
		for (unsigned step = 0; step < SLIP_DRAW3D_SPECULAR_SQUARING_STEPS; ++step) {
			const uint16_t doubled = (uint16_t)(power << 1);
			power = ((uint32_t)doubled * doubled) >> SLIP_WORD_BITS;
		}
		standaloneSpecularTable[value - specularThreshold] = (uint16_t)power;
	}
}

static uint16_t SlipDraw3D_Specular(SlipDraw3DVec32 relative, int16_t normalX, int16_t normalY, int16_t normalZ,
                                    const SlipDraw3DVertexLighting *state) {
	if (state->direct == 0)
		return 0;
	const int32_t x = (int32_t)((uint32_t)relative.x + (uint32_t)state->light.x);
	const int32_t y = (int32_t)((uint32_t)relative.y + (uint32_t)state->light.y);
	const int32_t z = (int32_t)((uint32_t)relative.z + (uint32_t)state->light.z);
	const int64_t dot = (int64_t)x * (int16_t)(0u - (uint16_t)normalX) +
	                    (int64_t)y * (int16_t)(0u - (uint16_t)normalY) + (int64_t)z * (int16_t)(0u - (uint16_t)normalZ);

	const uint16_t high = (uint16_t)((uint64_t)dot >> 32);
	if ((int16_t)high < 0)
		return 0;
	SlipDraw3DApproxAbsVectorLength length;
	SlipDraw3D_ApproxAbsVectorLength((uint32_t)x, (uint32_t)y, (uint32_t)z, &length);
	uint32_t ratio;
	if (high >= length.approximateLength) {
		ratio = SLIP_Q14_ONE;
	} else {
		ratio = (uint32_t)((((uint64_t)high << 32) | (uint32_t)dot) / length.approximateLength);
		if (ratio > SLIP_Q14_ONE)
			ratio = SLIP_Q14_ONE;
	}
	uint32_t index = ratio - specularThreshold;
	if ((int32_t)index < 0)
		return 0;
	index = ((index << 1) & SLIP_DRAW3D_WORD_ALIGNED_INDEX_MASK) >> 1;
	return (uint16_t)(((uint32_t)boundSpecularTable[index] * (uint16_t)state->direct) >> SLIP_Q14_FRACTION_BITS);
}

uint32_t SlipDraw3D_VertexColor(const SlipDraw3DMaterialRecord *material, SlipDraw3DVertexRecord *vertex,
                                int16_t normalX, int16_t normalY, int16_t normalZ,
                                const SlipDraw3DVertexLighting *state) {
	uint16_t specular = 0;
	uint16_t diffuse = 0;

	if ((state->flags & SLIP_RENDER_DISABLE_SPECULAR) == 0 && material->specularCoefficient != 0) {
		SlipDraw3DVec32 point = state->transform((uint16_t)vertex->sourceX, (uint16_t)vertex->sourceY,
		                                         (uint16_t)vertex->sourceZ, vertex, state->transformContext);
		SlipDraw3DVec32 relative = {(int32_t)((uint32_t)point.x - (uint32_t)state->origin.x),
		                            (int32_t)((uint32_t)point.y - (uint32_t)state->origin.y),
		                            (int32_t)((uint32_t)point.z - (uint32_t)state->origin.z)};
		specular = SlipDraw3D_Specular(relative, normalX, normalY, normalZ, state);
	}

	if (state->direct != 0) {
		const int64_t dot = (int64_t)normalX * (int16_t)state->light.x + (int64_t)normalY * (int16_t)state->light.y +
		                    (int64_t)normalZ * (int16_t)state->light.z;
		const int32_t projected = -(int32_t)(int16_t)((uint64_t)dot >> SLIP_Q14_FRACTION_BITS);
		if (projected >= 0)
			diffuse = (uint16_t)(((uint32_t)(uint16_t)projected * (uint16_t)state->direct) >> SLIP_Q14_FRACTION_BITS);
	}

	if (state->fadeStart != 0) {
		if ((vertex->flags & SLIP_VERTEX_DEPTH_BLEND_CACHED) == 0) {
			SlipDraw3DLightDepthBlend blend;
			vertex->flags |= SLIP_VERTEX_DEPTH_BLEND_CACHED;
			SlipDraw3D_LightDepthBlend((uint32_t)vertex->world.z, state->fadeStart, state->fadeEnd, state->fadeRange,
			                           &blend);
			vertex->depthFadeBlend = blend.fadeBlendQ14;
		}
	} else {
		vertex->depthFadeBlend = 0;
		vertex->flags |= SLIP_VERTEX_DEPTH_BLEND_CACHED;
	}
	SlipDraw3DLightingMaterial lighting;
	SlipDraw3D_LightingMaterial(material, sizeof(*material), vertex->depthFadeBlend, diffuse, specular, state->direct,
	                            state->ambient, state->fadeShade, state->fadeStart, &lighting);

	const uint32_t start = state->overrideRamp == 0 ? material->rampStart : state->rampStart;
	const uint32_t end = state->overrideRamp == 0 ? material->rampEnd : state->rampEnd;
	return SlipDraw3D_MultiplyUnsignedLowWordPreserveHigh(end - start, (uint16_t)lighting.shade) + start;
}

RasterPoint SlipDraw3D_ProjectUnclippedVertex(SlipDraw3DVertexRecord *vertex, const SlipDraw3DProjectState *projection,
                                              SlipDraw3DTransformFn transform, SlipDraw3DProjectFn project,
                                              void *context) {
	if ((vertex->flags & SLIP_VERTEX_PROJECTED) != 0)
		return (RasterPoint){vertex->screenX, vertex->screenY};
	if ((vertex->flags & SLIP_VERTEX_TRANSFORMED) == 0) {
		const uint32_t x = (uint16_t)vertex->sourceX | ((uint32_t)(uint16_t)vertex->sourceY << SLIP_WORD_BITS);
		const uint32_t y = (uint16_t)vertex->sourceY | ((uint32_t)(uint16_t)vertex->sourceZ << SLIP_WORD_BITS);
		const uint32_t z = (uint16_t)vertex->sourceZ | ((uint32_t)vertex->sourceFollowingWord << SLIP_WORD_BITS);
		vertex->world = transform(x, y, z, vertex, context);
		vertex->flags |= SLIP_VERTEX_TRANSFORMED;
		if ((vertex->flags & SLIP_VERTEX_PROJECTED) != 0)
			return (RasterPoint){vertex->screenX, vertex->screenY};
	}
	RasterPoint screen;
	project(vertex->world, &screen.x, &screen.y, context);
	if (screen.x < projection->minX)
		screen.x = projection->minX;
	else if (screen.x > projection->maxX)
		screen.x = projection->maxX;
	if (screen.y < projection->minY)
		screen.y = projection->minY;
	else if (screen.y > projection->maxY)
		screen.y = projection->maxY;
	vertex->screenX = screen.x;
	vertex->screenY = screen.y;
	vertex->flags |= SLIP_VERTEX_PROJECTED;
	return screen;
}

int SlipDraw3D_DrawUnclippedPolygon(const SlipDraw3DMaterialTable *materials, uint16_t materialIndex,
                                    uint16_t countAndFlags, int16_t normalX, int16_t normalY, int16_t normalZ,
                                    SlipDraw3DVertexRecord *vertices, size_t vertexCount, const uint8_t *stream,
                                    size_t streamBytes, const SlipDraw3DProjectState *projection,
                                    SlipDraw3DTransformFn transform, SlipDraw3DProjectFn project,
                                    const SlipDraw3DVertexLighting *lighting, uint32_t *materialColor) {
	const uint16_t count = countAndFlags & SLIP_PRIMITIVE_VERTEX_COUNT_MASK;

	const uint16_t offset =
	    materialIndex < (uint16_t)materials->count ? (uint16_t)(materialIndex * sizeof(SlipDraw3DMaterialRecord)) : 0;
	const SlipDraw3DMaterialRecord *const material = (const void *)((const uint8_t *)materials->records + offset);
	bool shaded = material->vertexShading != 0 && (lighting->flags & SLIP_RENDER_DISABLE_VERTEX_SHADING) == 0 &&
	              (countAndFlags & SLIP_PRIMITIVE_VERTEX_NORMALS) != 0;
	RasterShadedPoint shades[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	RasterPoint points[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
	if (!shaded) {
		SlipDraw3DVertexLighting flatLighting = *lighting;
		flatLighting.transform = transform;

		(void)SlipDraw3D_PolygonColor(material, normalX, normalY, (int16_t)count, &flatLighting, vertices, vertexCount,
		                              stream, streamBytes, countAndFlags, projection->inverseProjectionScale,
		                              materialColor);
	}
	uint32_t vertexIndex = 0;
	uint32_t remaining = count;
	do {
		const uint16_t index = SlipBytes_ReadLE16(
		    stream + (size_t)vertexIndex * SLIP_SERIALIZED_INDEX_BYTES); /* Serialized SHP indices. */
		SlipDraw3DVertexRecord *const vertex = &vertices[index];
		SlipDraw3D_ProjectUnclippedVertex(vertex, projection, transform, project, lighting->transformContext);

		uint8_t *const point = boundPointBuffer + (size_t)vertexIndex * sizeof(RasterTexturedPoint);
		SlipDraw3D_WriteLE32(point, (uint32_t)vertex->screenX);
		SlipDraw3D_WriteLE32(point + offsetof(RasterPoint, y), (uint32_t)vertex->screenY);
		if (shaded) {
			const uint8_t *const normal = stream + (size_t)count * SLIP_SERIALIZED_INDEX_BYTES +
			                              (size_t)vertexIndex * SLIP_SERIALIZED_NORMAL_BYTES;
			const uint32_t color = SlipDraw3D_VertexColor(
			    material, vertex, (int16_t)SlipBytes_ReadLE16(normal + SLIP_SERIALIZED_NORMAL_X_OFFSET),
			    (int16_t)SlipBytes_ReadLE16(normal + SLIP_SERIALIZED_NORMAL_Y_OFFSET),
			    (int16_t)SlipBytes_ReadLE16(normal + SLIP_SERIALIZED_NORMAL_Z_OFFSET), lighting);

			const uint32_t rotated = (color & SLIP_DRAW3D_UPPER_WORD_MASK) |
			                         ((color << 8) & SLIP_DRAW3D_SHADE_HIGH_BYTE_MASK) | ((color >> 8) & UINT8_MAX);
			SlipDraw3D_WriteLE32(point + offsetof(RasterShadedPoint, shade), rotated);
		}
		++vertexIndex;
	} while (--remaining != 0);

	for (uint16_t index = 0; index < count; ++index) {
		const uint8_t *const point = boundPointBuffer + (size_t)index * sizeof(RasterTexturedPoint);
		if (shaded)
			shades[index] =
			    (RasterShadedPoint){SlipBytes_ReadLEI32(point), SlipBytes_ReadLEI32(point + offsetof(RasterPoint, y)),
			                        SlipBytes_ReadLE16(point + offsetof(RasterShadedPoint, shade))};
		else
			points[index] =
			    (RasterPoint){SlipBytes_ReadLEI32(point), SlipBytes_ReadLEI32(point + offsetof(RasterPoint, y))};
	}
	if (shaded)
		Raster_DrawShadedFlatPolygon(shades, count);
	else
		Raster_DrawSolidFlatPolygon((uint8_t)*materialColor, points, count);
	return 1;
}

static uint16_t SlipDraw3D_MultiplySigned16RoundShift14AddCenter(uint16_t scaleWord, uint16_t centerOffset,
                                                                 uint16_t centerWord, uint16_t *shiftedWord,
                                                                 bool *carryOut) {
	int32_t product;
	uint16_t productLowWord;
	uint16_t productHighWord;
	uint16_t shifted;
	uint16_t carry;

	product = (int32_t)(int16_t)scaleWord * (int32_t)(int16_t)centerOffset;
	productLowWord = (uint16_t)product;
	productHighWord = (uint16_t)((uint32_t)product >> 16);
	shifted = (uint16_t)((uint16_t)(productLowWord >> SLIP_Q14_FRACTION_BITS) |
	                     (uint16_t)(productHighWord << SLIP_Q14_WORD_HIGH_SHIFT));
	carry = (uint16_t)((productLowWord >> (SLIP_Q14_FRACTION_BITS - 1)) & 1u);
	if (shiftedWord != NULL) {
		*shiftedWord = shifted;
	}
	if (carryOut != NULL) {
		*carryOut = carry != 0u;
	}
	return (uint16_t)(shifted + centerWord + carry);
}

int SlipDraw3D_BackgroundCenter(uint32_t horizontalOffset, uint16_t tiltComponent, uint16_t complementComponent,
                                uint32_t backgroundDistance, uint32_t scale, uint32_t viewportX, uint32_t viewportY,
                                uint16_t scaleX, uint16_t scaleY, SlipDraw3DBackgroundCenter *result) {
	SlipDraw3DBackgroundCenter out;
	uint32_t savedLow;
	uint32_t savedHigh;
	uint32_t low;
	uint32_t high;
	uint32_t productLow;
	uint32_t productHigh;
	uint32_t carry;
	int64_t dividend;
	int32_t divisor;
	int64_t quotient;

	if (result == NULL) {
		return 0;
	}
	out = (SlipDraw3DBackgroundCenter){horizontalOffset,
	                                   backgroundDistance,
	                                   tiltComponent,
	                                   complementComponent,
	                                   scale,
	                                   viewportX,
	                                   viewportY,
	                                   scaleX,
	                                   scaleY,
	                                   0,
	                                   0,
	                                   0,
	                                   false,
	                                   0,
	                                   0,
	                                   0,
	                                   false,
	                                   0,
	                                   0,
	                                   0,
	                                   0,
	                                   0,
	                                   0,
	                                   0,
	                                   0,
	                                   false,
	                                   0,
	                                   false,
	                                   0,
	                                   0,
	                                   false};
	SlipDraw3D_MultiplySigned32ToHalves((int16_t)complementComponent, (int32_t)horizontalOffset, &savedLow, &savedHigh);
	SlipDraw3D_MultiplySigned32ToHalves((int16_t)tiltComponent, (int32_t)horizontalOffset, &low, &high);
	SlipDraw3D_MultiplySigned32ToHalves((int16_t)complementComponent, (int32_t)backgroundDistance, &productLow,
	                                    &productHigh);
	SlipDraw3D_Add64Words(productLow, productHigh, low, high, &out.forwardLow, &out.forwardHigh);
	out.shiftedForward = SlipDraw3D_RoundShift14Low32FromHalves(out.forwardLow, out.forwardHigh, &out.carryForward);

	SlipDraw3D_MultiplySigned32ToHalves(-(int32_t)(int16_t)tiltComponent, (int32_t)backgroundDistance, &productLow,
	                                    &productHigh);
	SlipDraw3D_Add64Words(savedLow, savedHigh, productLow, productHigh, &out.depthLow, &out.depthHigh);
	out.shiftedDepth = SlipDraw3D_RoundShift14Low32FromHalves(out.depthLow, out.depthHigh, &out.carryDepth);
	if (out.shiftedDepth == 0u) {
		return 0;
	}

	SlipDraw3D_MultiplySigned32ToHalves((int32_t)out.shiftedForward, (int32_t)scale, &out.scaledForwardLow,
	                                    &out.scaledForwardHigh);
	carry = (out.scaledForwardLow >> 31) & 1u;
	out.doubledLow = out.scaledForwardLow << 1;
	out.doubledHigh = (out.scaledForwardHigh << 1) | carry;
	dividend = SlipDraw3D_I64FromU64(((uint64_t)out.doubledHigh << 32) | out.doubledLow);
	divisor = (int32_t)out.shiftedDepth;
	quotient = dividend / divisor;
	if (quotient < INT32_MIN || quotient > INT32_MAX) {
		return 0;
	}
	out.quotient = (uint32_t)(int32_t)quotient;
	out.rounded = SlipDraw3D_RoundSignedHalf(out.quotient);
	out.projectedOffset = (uint16_t)out.rounded;
	out.centerX = SlipDraw3D_MultiplySigned16RoundShift14AddCenter(scaleX, out.projectedOffset, (uint16_t)viewportX,
	                                                               &out.shiftedX, &out.carryX);
	out.centerY = SlipDraw3D_MultiplySigned16RoundShift14AddCenter(scaleY, out.projectedOffset, (uint16_t)viewportY,
	                                                               &out.shiftedY, &out.carryY);
	out.returned = true;
	*result = out;
	return 1;
}

int SlipDraw3D_BackgroundMaterial(const uint8_t *materialTable, size_t materialTableBytes, uint16_t materialIndex,
                                  uint32_t detailScale, const SlipDraw3DStateRecord *stateRecord,
                                  SlipDraw3DBackgroundMaterial *result) {
	SlipDraw3DBackgroundMaterial out;
	uint16_t productLowWord;
	uint16_t productHighWord;
	uint16_t lightSumLow;
	uint32_t lightSumHigh;
	uint16_t wordAdditionCarry;
	uint32_t negated;

	if (materialTable == NULL || result == NULL || materialTableBytes < sizeof(uint16_t)) {
		return 0;
	}
	out = (SlipDraw3DBackgroundMaterial){materialIndex,
	                                     SlipBytes_ReadLE16(materialTable),
	                                     false,
	                                     offsetof(SlipDraw3DMaterialTable, records),
	                                     SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES,
	                                     detailScale,
	                                     detailScale == 0u,
	                                     0,
	                                     0,
	                                     0,
	                                     0,
	                                     0,
	                                     false,
	                                     0,
	                                     0,
	                                     true};
	out.materialIndexInRange = materialIndex < out.materialCount;
	if (out.materialIndexInRange) {
		out.materialRecordOffset = ((SLIP_DRAW3D_EXPANDED_MATERIAL_RECORD_SIZE * materialIndex) & UINT16_MAX) +
		                           SLIP_DRAW3D_MATERIAL_TABLE_HEADER_BYTES;
		out.selectedMaterialRecordOffset = out.materialRecordOffset;
	}
	if ((size_t)out.selectedMaterialRecordOffset > materialTableBytes) {
		return 0;
	}
	if (detailScale == 0u) {
		*result = out;
		return 1;
	}
	if (stateRecord == NULL) {
		return 0;
	}
	out.lightVectorX = (uint16_t)stateRecord->lightVector.x;
	out.lightVectorY = (uint32_t)stateRecord->lightVector.y;
	out.lightVectorZ = (uint32_t)stateRecord->lightVector.z;

	SlipDraw3D_MultiplySignedWords(0u, out.lightVectorX, &productLowWord, &productHighWord);
	lightSumLow = productLowWord;
	lightSumHigh = productHighWord;
	SlipDraw3D_MultiplySignedWords((uint16_t)out.lightVectorY, SLIP_Q14_ONE, &productLowWord, &productHighWord);
	wordAdditionCarry = (uint16_t)((uint32_t)lightSumLow + productLowWord > UINT16_MAX ? 1u : 0u);
	lightSumLow = (uint16_t)(lightSumLow + productLowWord);
	lightSumHigh = lightSumHigh + productHighWord + wordAdditionCarry;
	SlipDraw3D_MultiplySignedWords((uint16_t)out.lightVectorZ, 0u, &productLowWord, &productHighWord);
	wordAdditionCarry = (uint16_t)((uint32_t)productLowWord + lightSumLow > UINT16_MAX ? 1u : 0u);
	productLowWord = (uint16_t)(productLowWord + lightSumLow);
	productHighWord = (uint16_t)((uint32_t)productHighWord + lightSumHigh + wordAdditionCarry);
	out.projectedLight = SlipDraw3D_Shift14LowWordFromHalves(productLowWord, productHighWord);
	negated = (uint32_t)(-(int32_t)(int16_t)out.projectedLight);
	out.negatedProjectedLight = negated;
	out.negativeBranch = (negated & SLIP_DRAW3D_DWORD_SIGN_BIT) != 0u;
	if (out.negativeBranch) {
		*result = out;
		return 1;
	}
	SlipDraw3D_MultiplyUnsignedWords((uint16_t)negated, (uint16_t)detailScale, &productLowWord, &productHighWord);
	out.scaledLight = SlipDraw3D_Shift14LowWordFromHalves(productLowWord, productHighWord);
	out.lightValue = out.scaledLight;
	*result = out;
	return 1;
}

int SlipDraw3D_BackgroundValue(const uint8_t *materialRecord, size_t materialRecordBytes, uint16_t depthBlend,
                               uint16_t lightValue, uint32_t limitEnabled, uint32_t limitStart, uint32_t limitEnd,
                               SlipDraw3DBackgroundValue *result) {
	SlipDraw3DBackgroundValue out;
	uint16_t productLowWord;
	uint16_t productHighWord;

	if (materialRecord == NULL ||
	    materialRecordBytes < offsetof(SlipDraw3DMaterialRecord, rampEnd) + sizeof(uint32_t) || result == NULL) {
		return 0;
	}
	out = (SlipDraw3DBackgroundValue){true,
	                                  lightValue,
	                                  depthBlend,
	                                  true,
	                                  limitEnabled,
	                                  limitEnabled == 0u ? SLIP_DRAW3D_BACKGROUND_VALUE_BRANCH_MATERIAL
	                                                     : SLIP_DRAW3D_BACKGROUND_VALUE_BRANCH_LIMIT,
	                                  0,
	                                  0,
	                                  0,
	                                  0,
	                                  0,
	                                  0,
	                                  0,
	                                  true};
	if (limitEnabled == 0u) {
		out.baseValue = SlipBytes_ReadLE32(materialRecord + offsetof(SlipDraw3DMaterialRecord, rampStart));
		out.endValue = SlipBytes_ReadLE32(materialRecord + offsetof(SlipDraw3DMaterialRecord, rampEnd));
	} else {
		out.baseValue = limitStart;
		out.endValue = limitEnd;
	}
	out.valueDifference = out.endValue - out.baseValue;
	SlipDraw3D_MultiplyUnsignedWords((uint16_t)out.valueDifference, depthBlend, &productLowWord, &productHighWord);
	out.scaledDifferenceLow = SlipDraw3D_Shift14LowWordFromHalves(productLowWord, productHighWord);
	out.mergedDifference = (out.valueDifference & SLIP_DRAW3D_UPPER_WORD_MASK) | out.scaledDifferenceLow;
	out.interpolatedValue = out.mergedDifference + out.baseValue;
	out.value = (uint16_t)out.interpolatedValue;
	*result = out;
	return 1;
}

int SlipDraw3D_BackgroundValueExecute(const uint8_t *materialRecord, size_t materialRecordBytes, uint32_t depth,
                                      uint16_t lightValue, uint32_t fadeStart, uint32_t fadeEnd, uint32_t fadeRange,
                                      uint32_t directLight, uint32_t ambientLight, uint32_t fadeColour,
                                      uint32_t limitEnabled, uint32_t limitStart, uint32_t limitEnd,
                                      SlipDraw3DBackgroundValueExecute *result) {
	SlipDraw3DBackgroundValueExecute out;

	if (materialRecord == NULL ||
	    materialRecordBytes < offsetof(SlipDraw3DMaterialRecord, specularCoefficient) + sizeof(uint32_t) ||
	    result == NULL) {
		return 0;
	}

	out = (SlipDraw3DBackgroundValueExecute){depth, true, {0}, lightValue, true, true, {0}, {0}, 0, true};
	if (!SlipDraw3D_LightDepthBlend(depth, fadeStart, fadeEnd, fadeRange, &out.depthBlend)) {
		return 0;
	}
	if (!SlipDraw3D_LightingMaterial((const void *)materialRecord, materialRecordBytes,
	                                 (uint16_t)out.depthBlend.fadeBlendQ14, lightValue, 0, directLight, ambientLight,
	                                 fadeColour, fadeStart, &out.lighting)) {
		return 0;
	}
	if (!SlipDraw3D_BackgroundValue(materialRecord, materialRecordBytes, (uint16_t)out.lighting.shade, lightValue,
	                                limitEnabled, limitStart, limitEnd, &out.interpolation)) {
		return 0;
	}
	out.value = out.interpolation.value;
	*result = out;
	return 1;
}

int SlipDraw3D_BackgroundStripEntry(uint8_t *stripRecord, size_t stripRecordBytes, uint16_t centerOffset,
                                    uint16_t centerX, uint16_t centerY, uint16_t scaleX, uint16_t scaleY,
                                    uint16_t halfWidth, uint16_t halfHeight, SlipDraw3DBackgroundStripEntry *result) {
	SlipDraw3DBackgroundStripEntry out;
	uint16_t adjustedX;
	uint16_t adjustedY;

	if (stripRecord == NULL || stripRecordBytes < SLIP_BACKGROUND_STRIP_BYTES || result == NULL) {
		return 0;
	}
	out = (SlipDraw3DBackgroundStripEntry){centerOffset,
	                                       centerOffset == 0u,
	                                       centerX,
	                                       centerY,
	                                       scaleX,
	                                       scaleY,
	                                       halfWidth,
	                                       halfHeight,
	                                       0,
	                                       0,
	                                       false,
	                                       false,
	                                       0,
	                                       0,
	                                       0,
	                                       0,
	                                       0,
	                                       0,
	                                       false,
	                                       false};
	if (centerOffset != 0u) {
		adjustedY =
		    SlipDraw3D_MultiplySigned16RoundShift14AddCenter(scaleY, centerOffset, centerY, &out.shiftedY, &out.carryY);
		adjustedX =
		    SlipDraw3D_MultiplySigned16RoundShift14AddCenter(scaleX, centerOffset, centerX, &out.shiftedX, &out.carryX);
		out.adjustedY = adjustedY;
		out.adjustedX = adjustedX;
		out.recordRight = (uint16_t)(adjustedX + halfWidth);
		out.recordBottom = (uint16_t)(adjustedY + halfHeight);
		out.recordLeft = (uint16_t)(adjustedX - halfWidth);
		out.recordTop = (uint16_t)(adjustedY - halfHeight);
		out.returnedWithOffset = true;
	} else {
		out.adjustedX = centerX;
		out.adjustedY = centerY;
		out.recordRight = (uint16_t)(centerX + halfWidth);
		out.recordBottom = (uint16_t)(centerY + halfHeight);
		out.recordLeft = (uint16_t)(centerX - halfWidth);
		out.recordTop = (uint16_t)(centerY - halfHeight);
		out.returnedWithoutOffset = true;
	}
	SlipDraw3D_WriteLE16(stripRecord + SLIP_BACKGROUND_STRIP_RIGHT_OFFSET, out.recordRight);
	SlipDraw3D_WriteLE16(stripRecord + SLIP_BACKGROUND_STRIP_BOTTOM_OFFSET, out.recordBottom);
	SlipDraw3D_WriteLE16(stripRecord + SLIP_BACKGROUND_STRIP_LEFT_OFFSET, out.recordLeft);
	SlipDraw3D_WriteLE16(stripRecord + SLIP_BACKGROUND_STRIP_TOP_OFFSET, out.recordTop);
	*result = out;
	return 1;
}

int SlipDraw3D_BackgroundStripTableFixed(uint8_t *fixedStripTable, size_t stripTableBytes, uint32_t backgroundSpan,
                                         uint16_t tiltComponent, uint16_t complementComponent,
                                         uint32_t backgroundDistance, uint32_t scale, uint32_t viewportX,
                                         uint32_t viewportY, uint16_t scaleX, uint16_t scaleY, uint16_t halfWidth,
                                         uint16_t halfHeight, SlipDraw3DBackgroundStripTableVisitFixed *visits,
                                         size_t visitCapacity, SlipDraw3DBackgroundStripTableFixed *result) {
	SlipDraw3DBackgroundStripTableFixed out;
	uint16_t sourceCount;
	size_t i;
	uint32_t stripEntryAddress;

	if (fixedStripTable == NULL || result == NULL || stripTableBytes < SLIP_BACKGROUND_STRIP_TABLE_HEADER_BYTES) {
		return 0;
	}
	sourceCount = SlipBytes_ReadLE16(fixedStripTable);
	if (sourceCount == 0u || sourceCount > visitCapacity || visits == NULL ||
	    SLIP_BACKGROUND_STRIP_TABLE_HEADER_BYTES + (size_t)sourceCount * SLIP_BACKGROUND_STRIP_BYTES >
	        stripTableBytes) {
		return 0;
	}
	out = (SlipDraw3DBackgroundStripTableFixed){backgroundSpan,
	                                            true,
	                                            {0},
	                                            0,
	                                            0,
	                                            0,
	                                            0,
	                                            SLIP_BACKGROUND_FIXED_STRIP_TABLE_DOS_ADDRESS,
	                                            sourceCount,
	                                            SLIP_BACKGROUND_FIXED_STRIP_FIRST_DOS_ADDRESS,
	                                            0,
	                                            true};
	if (!SlipDraw3D_BackgroundCenter(backgroundSpan, tiltComponent, complementComponent, backgroundDistance, scale,
	                                 viewportX, viewportY, scaleX, scaleY, &out.center)) {
		return 0;
	}
	out.centerX = out.center.centerX;
	out.centerY = out.center.centerY;
	out.cachedCenterX = out.center.centerX;
	out.cachedCenterY = out.center.centerY;
	stripEntryAddress = SLIP_BACKGROUND_FIXED_STRIP_FIRST_DOS_ADDRESS;
	for (i = 0; i < sourceCount; ++i) {
		const size_t tableOffset = SLIP_BACKGROUND_STRIP_TABLE_HEADER_BYTES + i * SLIP_BACKGROUND_STRIP_BYTES;
		SlipDraw3DBackgroundStripTableVisitFixed *const visit = visits + i;

		*visit = (SlipDraw3DBackgroundStripTableVisitFixed){
		    stripEntryAddress,
		    SlipBytes_ReadLE16(fixedStripTable + tableOffset + SLIP_BACKGROUND_STRIP_CENTRE_OFFSET),
		    true,
		    {0},
		    stripEntryAddress + SLIP_BACKGROUND_STRIP_BYTES,
		    (uint32_t)sourceCount - (uint32_t)i - 1u,
		    i + 1u < sourceCount};
		if (!SlipDraw3D_BackgroundStripEntry(fixedStripTable + tableOffset, stripTableBytes - tableOffset,
		                                     visit->stripOffset, out.centerX, out.centerY, scaleX, scaleY, halfWidth,
		                                     halfHeight, &visit->entry)) {
			return 0;
		}
		stripEntryAddress += SLIP_BACKGROUND_STRIP_BYTES;
	}
	out.visitCount = sourceCount;
	*result = out;
	return 1;
}

int SlipDraw3D_BackgroundStripTableMaterial(
    uint8_t *materialStripTable, size_t stripTableBytes, const uint8_t *materialTable, size_t materialTableBytes,
    uint16_t materialIndex, uint32_t detailScale, const SlipDraw3DStateRecord *stateRecord, uint16_t count,
    uint32_t fadeStart, uint32_t fadeEnd, uint32_t fadeRange, uint32_t ambientLight, uint32_t fadeColour,
    uint32_t backgroundSpan, uint32_t backgroundDistance, uint16_t tiltComponent, uint16_t complementComponent,
    uint32_t scale, uint32_t viewportX, uint32_t viewportY, uint16_t scaleX, uint16_t scaleY, uint16_t halfWidth,
    uint16_t halfHeight, uint32_t limitEnabled, uint32_t limitStart, uint32_t limitEnd,
    SlipDraw3DBackgroundStripTableVisitMaterial *visits, size_t visitCapacity,
    SlipDraw3DBackgroundStripTableMaterial *result) {
	SlipDraw3DBackgroundStripTableMaterial out;
	const uint8_t *materialRecord;
	size_t materialRecordBytes;
	size_t stripEntryOffset;
	uint32_t cursor;
	uint32_t step;
	uint16_t previousStripValue;

	if (materialStripTable == NULL || materialTable == NULL || result == NULL ||
	    materialTableBytes < sizeof(uint16_t)) {
		return 0;
	}

	out = (SlipDraw3DBackgroundStripTableMaterial){SLIP_BACKGROUND_MATERIAL_STRIP_TABLE_DOS_ADDRESS,
	                                               count,
	                                               fadeStart,
	                                               (count != 1u && fadeStart != 0u)
	                                                   ? SLIP_DRAW3D_BACKGROUND_STRIP_TABLE_BRANCH_MULTI
	                                                   : SLIP_DRAW3D_BACKGROUND_STRIP_TABLE_BRANCH_FALLBACK,
	                                               0,
	                                               0,
	                                               true,
	                                               {0},
	                                               true,
	                                               {0},
	                                               0,
	                                               0,
	                                               true,
	                                               {0},
	                                               true,
	                                               {0},
	                                               0,
	                                               0,
	                                               UINT16_MAX,
	                                               0,
	                                               false,
	                                               {0},
	                                               true};
	if (!SlipDraw3D_BackgroundMaterial(materialTable, materialTableBytes, materialIndex, detailScale, stateRecord,
	                                   &out.material)) {
		return 0;
	}
	if ((size_t)out.material.selectedMaterialRecordOffset > materialTableBytes) {
		return 0;
	}
	materialRecord = materialTable + out.material.selectedMaterialRecordOffset;
	materialRecordBytes = materialTableBytes - (size_t)out.material.selectedMaterialRecordOffset;

	if (out.branch == SLIP_DRAW3D_BACKGROUND_STRIP_TABLE_BRANCH_MULTI) {
		const uint32_t dividendHigh = (backgroundSpan & SLIP_DRAW3D_DWORD_SIGN_BIT) ? UINT32_MAX : 0u;
		size_t i;

		if (count == 0u || dividendHigh >= (uint32_t)count || count > visitCapacity || visits == NULL ||
		    (SLIP_BACKGROUND_STRIP_TABLE_HEADER_BYTES + SLIP_BACKGROUND_STRIP_BYTES) +
		            (size_t)count * SLIP_BACKGROUND_STRIP_BYTES >
		        stripTableBytes) {
			return 0;
		}

		step = backgroundSpan / (uint32_t)count;
		cursor = step;
		out.spanStep = step;
		out.spanPosition = cursor;
		stripEntryOffset = SLIP_BACKGROUND_STRIP_TABLE_HEADER_BYTES;
		if (!SlipDraw3D_BackgroundCenter(cursor, tiltComponent, complementComponent, backgroundDistance, scale,
		                                 viewportX, viewportY, scaleX, scaleY, &out.initialCenter)) {
			return 0;
		}
		out.centerX = out.initialCenter.centerX;
		out.centerY = out.initialCenter.centerY;
		if (!SlipDraw3D_BackgroundStripEntry(materialStripTable + stripEntryOffset, stripTableBytes - stripEntryOffset,
		                                     SLIP_BACKGROUND_STRIP_INITIAL_CENTRE_Q14, out.centerX, out.centerY, scaleX,
		                                     scaleY, halfWidth, halfHeight, &out.initialEntry)) {
			return 0;
		}
		if (!SlipDraw3D_BackgroundValueExecute(materialRecord, materialRecordBytes, backgroundDistance,
		                                       out.material.lightValue, fadeStart, fadeEnd, fadeRange, detailScale,
		                                       ambientLight, fadeColour, limitEnabled, limitStart, limitEnd,
		                                       &out.initialValue)) {
			return 0;
		}
		SlipDraw3D_WriteLE16(materialStripTable + stripEntryOffset, out.initialValue.value);
		SlipDraw3D_WriteLE16(materialStripTable, 1u);
		out.outputCount = 1u;
		stripEntryOffset += SLIP_BACKGROUND_STRIP_BYTES;
		out.entryAddressAfterInitial = SLIP_BACKGROUND_MATERIAL_STRIP_TABLE_DOS_ADDRESS + (uint32_t)stripEntryOffset;
		previousStripValue = SLIP_BACKGROUND_NO_PREVIOUS_STRIP;
		out.previousValue = previousStripValue;

		for (i = 0; i < count; ++i) {
			SlipDraw3DBackgroundStripTableVisitMaterial *const visit = visits + i;

			*visit = (SlipDraw3DBackgroundStripTableVisitMaterial){SLIP_BACKGROUND_MATERIAL_STRIP_TABLE_DOS_ADDRESS +
			                                                           (uint32_t)stripEntryOffset,
			                                                       cursor,
			                                                       true,
			                                                       {0},
			                                                       false,
			                                                       false,
			                                                       {0},
			                                                       false,
			                                                       {0},
			                                                       false,
			                                                       {0},
			                                                       previousStripValue,
			                                                       false,
			                                                       0,
			                                                       0,
			                                                       (uint32_t)count - (uint32_t)i - 1u,
			                                                       i + 1u < count};
			if (!SlipDraw3D_BackgroundCenter(cursor, tiltComponent, complementComponent, backgroundDistance, scale,
			                                 viewportX, viewportY, scaleX, scaleY, &visit->center)) {
				return 0;
			}
			visit->centerUnchanged = visit->center.centerX == out.centerX && visit->center.centerY == out.centerY;
			if (!visit->centerUnchanged) {
				out.centerX = visit->center.centerX;
				out.centerY = visit->center.centerY;
				visit->callDraw3DBackgroundStripEntry = true;
				if (!SlipDraw3D_BackgroundStripEntry(materialStripTable + stripEntryOffset,
				                                     stripTableBytes - stripEntryOffset, 0, out.centerX, out.centerY,
				                                     scaleX, scaleY, halfWidth, halfHeight, &visit->entry)) {
					return 0;
				}
				visit->callDraw3DApproxAbsVectorLength = true;
				if (!SlipDraw3D_ApproxAbsVectorLength(backgroundDistance, cursor, 0, &visit->approx)) {
					return 0;
				}
				visit->callDraw3DBackgroundValue = true;
				if (!SlipDraw3D_BackgroundValueExecute(
				        materialRecord, materialRecordBytes, visit->approx.approximateLength, out.material.lightValue,
				        fadeStart, fadeEnd, fadeRange, detailScale, ambientLight, fadeColour, limitEnabled, limitStart,
				        limitEnd, &visit->backgroundValue)) {
					return 0;
				}
				visit->duplicateValue = visit->backgroundValue.value == previousStripValue;
				if (visit->duplicateValue) {
					SlipDraw3D_WriteLE16(
					    materialStripTable + stripEntryOffset - SLIP_BACKGROUND_STRIP_BYTES +
					        SLIP_BACKGROUND_STRIP_LEFT_OFFSET,
					    SlipBytes_ReadLE16(materialStripTable + stripEntryOffset + SLIP_BACKGROUND_STRIP_LEFT_OFFSET));
					SlipDraw3D_WriteLE16(
					    materialStripTable + stripEntryOffset - SLIP_BACKGROUND_STRIP_BYTES +
					        SLIP_BACKGROUND_STRIP_TOP_OFFSET,
					    SlipBytes_ReadLE16(materialStripTable + stripEntryOffset + SLIP_BACKGROUND_STRIP_TOP_OFFSET));
					SlipDraw3D_WriteLE16(
					    materialStripTable + stripEntryOffset - SLIP_BACKGROUND_STRIP_BYTES +
					        SLIP_BACKGROUND_STRIP_RIGHT_OFFSET,
					    SlipBytes_ReadLE16(materialStripTable + stripEntryOffset + SLIP_BACKGROUND_STRIP_RIGHT_OFFSET));
					SlipDraw3D_WriteLE16(materialStripTable + stripEntryOffset - SLIP_BACKGROUND_STRIP_BYTES +
					                         SLIP_BACKGROUND_STRIP_BOTTOM_OFFSET,
					                     SlipBytes_ReadLE16(materialStripTable + stripEntryOffset +
					                                        SLIP_BACKGROUND_STRIP_BOTTOM_OFFSET));
				} else {
					SlipDraw3D_WriteLE16(materialStripTable + stripEntryOffset, visit->backgroundValue.value);
					previousStripValue = visit->backgroundValue.value;
					++out.outputCount;
					SlipDraw3D_WriteLE16(materialStripTable, out.outputCount);
					stripEntryOffset += SLIP_BACKGROUND_STRIP_BYTES;
				}
			}
			cursor += step;
			visit->nextEntryAddress = SLIP_BACKGROUND_MATERIAL_STRIP_TABLE_DOS_ADDRESS + (uint32_t)stripEntryOffset;
			visit->nextSpanPosition = cursor;
		}
		out.spanPosition = cursor;
		out.previousValue = previousStripValue;
		out.visitCount = count;
	} else {
		if (stripTableBytes < SLIP_BACKGROUND_STRIP_TABLE_HEADER_BYTES +
		                          SLIP_BACKGROUND_STRIP_BOUNDARY_PAIR_COUNT * SLIP_BACKGROUND_STRIP_BYTES) {
			return 0;
		}

		cursor = backgroundSpan;
		out.spanPosition = cursor;
		if (!SlipDraw3D_BackgroundCenter(cursor, tiltComponent, complementComponent, backgroundDistance, scale,
		                                 viewportX, viewportY, scaleX, scaleY, &out.initialCenter)) {
			return 0;
		}
		out.centerX = out.initialCenter.centerX;
		out.centerY = out.initialCenter.centerY;
		SlipDraw3D_WriteLE16(materialStripTable, SLIP_BACKGROUND_STRIP_BOUNDARY_PAIR_COUNT);
		out.outputCount = SLIP_BACKGROUND_STRIP_BOUNDARY_PAIR_COUNT;
		stripEntryOffset = SLIP_BACKGROUND_STRIP_TABLE_HEADER_BYTES;
		if (!SlipDraw3D_BackgroundStripEntry(materialStripTable + stripEntryOffset, stripTableBytes - stripEntryOffset,
		                                     SLIP_BACKGROUND_STRIP_INITIAL_CENTRE_Q14, out.centerX, out.centerY, scaleX,
		                                     scaleY, halfWidth, halfHeight, &out.initialEntry)) {
			return 0;
		}
		if (!SlipDraw3D_BackgroundValueExecute(materialRecord, materialRecordBytes, backgroundDistance,
		                                       out.material.lightValue, fadeStart, fadeEnd, fadeRange, detailScale,
		                                       ambientLight, fadeColour, limitEnabled, limitStart, limitEnd,
		                                       &out.initialValue)) {
			return 0;
		}
		SlipDraw3D_WriteLE16(materialStripTable + stripEntryOffset, out.initialValue.value);
		stripEntryOffset += SLIP_BACKGROUND_STRIP_BYTES;
		out.entryAddressAfterInitial = SLIP_BACKGROUND_MATERIAL_STRIP_TABLE_DOS_ADDRESS + (uint32_t)stripEntryOffset;
		out.callFallbackBackgroundStripEntry = true;
		if (!SlipDraw3D_BackgroundStripEntry(materialStripTable + stripEntryOffset, stripTableBytes - stripEntryOffset,
		                                     0, out.centerX, out.centerY, scaleX, scaleY, halfWidth, halfHeight,
		                                     &out.fallbackSecondEntry)) {
			return 0;
		}
	}

	*result = out;
	return 1;
}

void SlipDraw3D_ProjectSpriteCorner(int16_t cornerSide, int16_t cornerOffset, uint16_t originX, uint16_t originY,
                                    int16_t *outX, int16_t *outY) {
	bool carry;
	uint16_t accumX;
	uint16_t accumY;
	uint16_t term;

	term = SlipDraw3D_MultiplySignedWordsShift14WithRoundingBit((uint16_t)cornerOffset, g_spriteScaleX, &carry);
	accumX = (uint16_t)(term + originX + (carry ? 1u : 0u));

	term = SlipDraw3D_MultiplySignedWordsShift14WithRoundingBit((uint16_t)cornerOffset, g_spriteScaleY, &carry);
	accumY = (uint16_t)(term + originY + (carry ? 1u : 0u));

	term = SlipDraw3D_MultiplySignedWordsShift13WithRoundingBit((uint16_t)cornerSide, g_spriteHalfWidth, &carry);
	accumX = (uint16_t)(accumX + term + (carry ? 1u : 0u));

	term = SlipDraw3D_MultiplySignedWordsShift13WithRoundingBit((uint16_t)cornerSide, g_spriteHalfHeight, &carry);
	accumY = (uint16_t)(term + accumY + (carry ? 1u : 0u));

	if (outX != NULL) {
		*outX = (int16_t)accumX;
	}
	if (outY != NULL) {
		*outY = (int16_t)accumY;
	}
}

int SlipDraw3D_BackgroundPassMaterial(uint16_t materialFillFlag, uint16_t fixedFillFlag, uint16_t materialFillTag,
                                      SlipDraw3DBackgroundPassMaterial *result) {
	SlipDraw3DBackgroundPassMaterial out;

	if (result == NULL) {
		return 0;
	}
	out = (SlipDraw3DBackgroundPassMaterial){materialFillFlag,
	                                         fixedFillFlag,
	                                         materialFillTag,
	                                         SLIP_DRAW3D_BACKGROUND_PASS_SKIP,
	                                         false,
	                                         0,
	                                         false,
	                                         0,
	                                         false,
	                                         true};
	if (materialFillFlag != 0u) {
		*result = out;
		return 1;
	}
	if (fixedFillFlag != 0u) {
		out.branch = SLIP_DRAW3D_BACKGROUND_PASS_FILL;
		out.callTrackWorldLoadClipRegisters = true;
		out.fillValue = materialFillTag;
		out.callRasterFillRectClipped = true;
		*result = out;
		return 1;
	}
	out.branch = SLIP_DRAW3D_BACKGROUND_PASS_STRIP;
	out.stripTableAddress = SLIP_BACKGROUND_MATERIAL_STRIP_TABLE_DOS_ADDRESS;
	out.callDraw3DStripDispatch = true;
	*result = out;
	return 1;
}

int SlipDraw3D_BackgroundPassFixed(uint16_t materialFillFlag, uint16_t fixedFillFlag, uint16_t fixedFillTag,
                                   SlipDraw3DBackgroundPassFixed *result) {
	SlipDraw3DBackgroundPassFixed out;

	if (result == NULL) {
		return 0;
	}
	out = (SlipDraw3DBackgroundPassFixed){materialFillFlag,
	                                      fixedFillFlag,
	                                      fixedFillTag,
	                                      SLIP_DRAW3D_BACKGROUND_PASS_SKIP,
	                                      false,
	                                      0,
	                                      false,
	                                      0,
	                                      false,
	                                      true};
	if (fixedFillFlag != 0u) {
		*result = out;
		return 1;
	}
	if (materialFillFlag != 0u) {
		out.branch = SLIP_DRAW3D_BACKGROUND_PASS_FILL;
		out.callDraw3DLoadClipAndCenter = true;
		out.fillValue = fixedFillTag;
		out.callRasterFillRectClipped = true;
		*result = out;
		return 1;
	}
	out.branch = SLIP_DRAW3D_BACKGROUND_PASS_STRIP;
	out.stripTableAddress = SLIP_BACKGROUND_FIXED_STRIP_TABLE_DOS_ADDRESS;
	out.callDraw3DStripDispatch = true;
	*result = out;
	return 1;
}

int SlipDraw3D_BackgroundPassExecuteMaterial(
    SlipDraw3DRecordPool *pool, uint16_t materialFillFlag, uint16_t fixedFillFlag, uint16_t materialFillTag,
    const uint8_t *materialStripTable, size_t stripTableBytes, uint32_t renderFlags, uint32_t initialMaterialColor,
    int32_t minX, int32_t maxX, int32_t minY, int32_t maxY, int depthClipRejected, int screenClipRejected,
    SlipDraw3DReturnActiveVisit *returnVisits, size_t returnVisitCapacity,
    SlipDraw3DPointPointerRingVisit *pointRingVisits, size_t pointRingVisitCapacity,
    SlipDraw3DStripDispatchVisit *stripVisits, size_t stripVisitCapacity,
    SlipDraw3DBackgroundPassExecuteMaterial *result) {
	SlipDraw3DBackgroundPassExecuteMaterial out;

	if (result == NULL) {
		return 0;
	}
	out = (SlipDraw3DBackgroundPassExecuteMaterial){0};
	if (!SlipDraw3D_BackgroundPassMaterial(materialFillFlag, fixedFillFlag, materialFillTag, &out.materialPass)) {
		return 0;
	}
	if (out.materialPass.branch == SLIP_DRAW3D_BACKGROUND_PASS_FILL) {

		Raster_FillRectClipped((uint8_t)out.materialPass.fillValue, (int16_t)minX, (int16_t)minY, (int16_t)maxX,
		                       (int16_t)maxY);
	} else if (out.materialPass.branch == SLIP_DRAW3D_BACKGROUND_PASS_STRIP) {
		out.callDraw3DStripDispatch = true;
		if (!SlipDraw3D_StripDispatchWithPointPointerRing(
		        pool, materialStripTable, stripTableBytes, SLIP_BACKGROUND_MATERIAL_STRIP_TABLE_DOS_ADDRESS,
		        renderFlags, initialMaterialColor, minX, maxX, minY, maxY, depthClipRejected, screenClipRejected,
		        returnVisits, returnVisitCapacity, pointRingVisits, pointRingVisitCapacity, stripVisits,
		        stripVisitCapacity, &out.strip)) {
			return 0;
		}
		out.returnActiveVisitCount = out.strip.returnActiveVisitCount;
		out.pointRingVisitCount = out.strip.visitCount * SLIP_BACKGROUND_STRIP_CORNER_COUNT;
		out.stripVisitCount = out.strip.visitCount;
	}
	*result = out;
	return 1;
}

int SlipDraw3D_BackgroundPassExecuteFixed(SlipDraw3DRecordPool *pool, uint16_t materialFillFlag, uint16_t fixedFillFlag,
                                          uint16_t fixedFillTag, const uint8_t *fixedStripTable, size_t stripTableBytes,
                                          uint32_t renderFlags, uint32_t initialMaterialColor, int32_t minX,
                                          int32_t maxX, int32_t minY, int32_t maxY, int depthClipRejected,
                                          int screenClipRejected, SlipDraw3DReturnActiveVisit *returnVisits,
                                          size_t returnVisitCapacity, SlipDraw3DPointPointerRingVisit *pointRingVisits,
                                          size_t pointRingVisitCapacity, SlipDraw3DStripDispatchVisit *stripVisits,
                                          size_t stripVisitCapacity, SlipDraw3DBackgroundPassExecuteFixed *result) {
	SlipDraw3DBackgroundPassExecuteFixed out;

	if (result == NULL) {
		return 0;
	}
	out = (SlipDraw3DBackgroundPassExecuteFixed){0};
	if (!SlipDraw3D_BackgroundPassFixed(materialFillFlag, fixedFillFlag, fixedFillTag, &out.fixedPass)) {
		return 0;
	}
	if (out.fixedPass.branch == SLIP_DRAW3D_BACKGROUND_PASS_FILL) {

		Raster_FillRectClipped((uint8_t)out.fixedPass.fillValue, (int16_t)minX, (int16_t)minY, (int16_t)maxX,
		                       (int16_t)maxY);
	} else if (out.fixedPass.branch == SLIP_DRAW3D_BACKGROUND_PASS_STRIP) {
		out.callDraw3DStripDispatch = true;
		if (!SlipDraw3D_StripDispatchWithPointPointerRing(
		        pool, fixedStripTable, stripTableBytes, SLIP_BACKGROUND_FIXED_STRIP_TABLE_DOS_ADDRESS, renderFlags,
		        initialMaterialColor, minX, maxX, minY, maxY, depthClipRejected, screenClipRejected, returnVisits,
		        returnVisitCapacity, pointRingVisits, pointRingVisitCapacity, stripVisits, stripVisitCapacity,
		        &out.strip)) {
			return 0;
		}
		out.returnActiveVisitCount = out.strip.returnActiveVisitCount;
		out.pointRingVisitCount = out.strip.visitCount * SLIP_BACKGROUND_STRIP_CORNER_COUNT;
		out.stripVisitCount = out.strip.visitCount;
	}
	*result = out;
	return 1;
}

int SlipDraw3D_BackgroundDispatch(uint16_t materialFillFlag, uint16_t fixedFillFlag, uint16_t materialFillTag,
                                  uint16_t fixedFillTag, SlipDraw3DBackgroundDispatch *result) {
	SlipDraw3DBackgroundDispatch out;

	if (result == NULL) {
		return 0;
	}
	out = (SlipDraw3DBackgroundDispatch){true, true, true, {0}, true, {0}, true, true};
	if (!SlipDraw3D_BackgroundPassMaterial(materialFillFlag, fixedFillFlag, materialFillTag, &out.materialPass) ||
	    !SlipDraw3D_BackgroundPassFixed(materialFillFlag, fixedFillFlag, fixedFillTag, &out.fixedPass)) {
		return 0;
	}
	*result = out;
	return 1;
}

int SlipDraw3D_BackgroundDispatchExecute(SlipDraw3DRecordPool *pool, uint16_t materialFillFlag, uint16_t fixedFillFlag,
                                         uint16_t materialFillTag, uint16_t fixedFillTag,
                                         const uint8_t *materialStripTable, size_t materialStripTableBytes,
                                         const uint8_t *fixedStripTable, size_t fixedStripTableBytes,
                                         uint32_t renderFlags, uint32_t initialMaterialColor, int32_t minX,
                                         int32_t maxX, int32_t minY, int32_t maxY, int depthClipRejected,
                                         int screenClipRejected, SlipDraw3DReturnActiveVisit *returnVisits,
                                         size_t returnVisitCapacity, SlipDraw3DPointPointerRingVisit *pointRingVisits,
                                         size_t pointRingVisitCapacity, SlipDraw3DStripDispatchVisit *stripVisits,
                                         size_t stripVisitCapacity, SlipDraw3DBackgroundDispatchExecute *result) {
	SlipDraw3DBackgroundDispatchExecute out;
	size_t pointRingVisitOffset;
	size_t stripVisitOffset;

	if (result == NULL) {
		return 0;
	}
	out = (SlipDraw3DBackgroundDispatchExecute){true, true, true, {0}, true, {0}, true, true};
	if (!SlipDraw3D_BackgroundPassExecuteMaterial(
	        pool, materialFillFlag, fixedFillFlag, materialFillTag, materialStripTable, materialStripTableBytes,
	        renderFlags, initialMaterialColor, minX, maxX, minY, maxY, depthClipRejected, screenClipRejected,
	        returnVisits, returnVisitCapacity, pointRingVisits, pointRingVisitCapacity, stripVisits, stripVisitCapacity,
	        &out.materialPass) ||
	    out.materialPass.returnActiveVisitCount > returnVisitCapacity ||
	    out.materialPass.pointRingVisitCount > pointRingVisitCapacity ||
	    out.materialPass.stripVisitCount > stripVisitCapacity) {
		return 0;
	}
	pointRingVisitOffset = out.materialPass.pointRingVisitCount;
	stripVisitOffset = out.materialPass.stripVisitCount;

	if (!SlipDraw3D_BackgroundPassExecuteFixed(
	        pool, materialFillFlag, fixedFillFlag, fixedFillTag, fixedStripTable, fixedStripTableBytes, renderFlags,
	        initialMaterialColor, minX, maxX, minY, maxY, depthClipRejected, screenClipRejected, returnVisits,
	        returnVisitCapacity, pointRingVisits == NULL ? NULL : pointRingVisits + pointRingVisitOffset,
	        pointRingVisitCapacity - pointRingVisitOffset, stripVisits == NULL ? NULL : stripVisits + stripVisitOffset,
	        stripVisitCapacity - stripVisitOffset, &out.fixedPass)) {
		return 0;
	}
	*result = out;
	return 1;
}

int SlipDraw3D_BackgroundDispatchSetupExecute(
    const SlipView3DMaths *maths, SlipDraw3DRecordPool *pool, uint8_t *materialStripTable,
    size_t materialStripTableBytes, uint8_t *fixedStripTable, size_t fixedStripTableBytes, const uint8_t *materialTable,
    size_t materialTableBytes, const SlipView3DMatrix *viewMatrix, uint32_t backgroundDistance, uint32_t backgroundSpan,
    uint16_t backgroundMaterialIndex, uint8_t materialStripCount, uint8_t fixedStripCount, uint16_t stripMaterialIndex,
    uint16_t stripCurvature, uint32_t projectionScale, uint32_t cachedProjectionScale, uint16_t projectionRevision,
    uint8_t cachedFixedStripCount, uint8_t cachedEndValueByte, uint16_t cachedStripCurvature, uint32_t viewportX,
    uint32_t viewportY, uint32_t detailScale, const SlipDraw3DStateRecord *stateRecord, uint32_t fadeStart,
    uint32_t fadeEnd, uint32_t fadeRange, uint32_t ambientLight, uint32_t fadeColour, uint32_t limitEnabled,
    uint32_t limitStart, uint32_t limitEnd, uint32_t renderFlags, uint32_t initialMaterialColor, int32_t minX,
    int32_t maxX, int32_t minY, int32_t maxY, int depthClipRejected, int screenClipRejected,
    SlipDraw3DReturnActiveVisit *returnVisits, size_t returnVisitCapacity,
    SlipDraw3DPointPointerRingVisit *pointRingVisits, size_t pointRingVisitCapacity,
    SlipDraw3DStripDispatchVisit *stripVisits, size_t stripVisitCapacity,
    SlipDraw3DBackgroundStripTableVisitFixed *fixedStripVisits, size_t fixedStripVisitCapacity,
    SlipDraw3DBackgroundStripTableVisitMaterial *materialStripVisits, size_t materialStripVisitCapacity,
    SlipDraw3DBackgroundDispatchSetupExecute *result) {
	SlipDraw3DBackgroundDispatchSetupExecute out;

	if (result == NULL) {
		return 0;
	}
	memset(&out, 0, sizeof(out));
	out.savedRegisters = true;
	out.callDraw3DBackgroundSetup = true;
	if (!SlipDraw3D_BackgroundSetupExecute(
	        maths, materialStripTable, materialStripTableBytes, fixedStripTable, fixedStripTableBytes, materialTable,
	        materialTableBytes, viewMatrix, backgroundDistance, backgroundSpan, backgroundMaterialIndex,
	        materialStripCount, fixedStripCount, stripMaterialIndex, stripCurvature, projectionScale,
	        cachedProjectionScale, projectionRevision, cachedFixedStripCount, cachedEndValueByte, cachedStripCurvature,
	        viewportX, viewportY, detailScale, stateRecord, fadeStart, fadeEnd, fadeRange, ambientLight, fadeColour,
	        limitEnabled, limitStart, limitEnd, fixedStripVisits, fixedStripVisitCapacity, materialStripVisits,
	        materialStripVisitCapacity, &out.setup)) {
		return 0;
	}
	out.callDraw3DBackgroundPassMaterial = true;
	out.callDraw3DBackgroundPassFixed = true;
	if (!SlipDraw3D_BackgroundDispatchExecute(
	        pool, out.setup.fixedFillFlag, out.setup.materialFillFlag, out.setup.materialFillValue,
	        out.setup.fixedFillValue, materialStripTable, materialStripTableBytes, fixedStripTable,
	        fixedStripTableBytes, renderFlags, initialMaterialColor, minX, maxX, minY, maxY, depthClipRejected,
	        screenClipRejected, returnVisits, returnVisitCapacity, pointRingVisits, pointRingVisitCapacity, stripVisits,
	        stripVisitCapacity, &out.dispatch)) {
		return 0;
	}
	out.restoredRegisters = true;
	out.returned = true;
	*result = out;
	return 1;
}

int SlipDraw3D_StripDispatch(const uint8_t *stripTable, size_t stripTableBytes, uint32_t stripTableBaseAddress,
                             uint32_t renderFlags, const bool *carryFromByVisit, size_t carryCount,
                             SlipDraw3DStripDispatchVisit *visits, size_t visitCapacity,
                             SlipDraw3DStripDispatch *result) {
	uint16_t sourceCount;
	uint32_t loopCount;
	uint32_t stripOffset;
	size_t visitCount;

	if (stripTable == NULL || carryFromByVisit == NULL || visits == NULL || result == NULL ||
	    stripTableBytes < SLIP_BACKGROUND_STRIP_TABLE_HEADER_BYTES) {
		return 0;
	}
	sourceCount = SlipBytes_ReadLE16(stripTable);
	if (sourceCount <= 1u) {
		return 0;
	}
	loopCount = (uint32_t)sourceCount - 1u;
	if ((size_t)loopCount > carryCount || (size_t)loopCount > visitCapacity) {
		return 0;
	}
	stripOffset = SLIP_BACKGROUND_STRIP_TABLE_HEADER_BYTES;
	visitCount = 0;
	while (loopCount != 0u) {
		SlipDraw3DStripDispatchVisit *visit;
		uint32_t currentStripAddress;
		bool draw3DPointPointerRingCarry;

		if ((size_t)stripOffset + SLIP_BACKGROUND_STRIP_BOUNDARY_PAIR_COUNT * SLIP_BACKGROUND_STRIP_BYTES >
		    stripTableBytes) {
			return 0;
		}
		currentStripAddress = stripTableBaseAddress + stripOffset;
		draw3DPointPointerRingCarry = carryFromByVisit[visitCount];
		visit = visits + visitCount;
		*visit = (SlipDraw3DStripDispatchVisit){
		    currentStripAddress,
		    loopCount,
		    currentStripAddress + SLIP_BACKGROUND_STRIP_LEFT_OFFSET,
		    currentStripAddress + (SLIP_BACKGROUND_STRIP_BYTES + SLIP_BACKGROUND_STRIP_LEFT_OFFSET),
		    currentStripAddress + (SLIP_BACKGROUND_STRIP_BYTES + SLIP_BACKGROUND_STRIP_RIGHT_OFFSET),
		    currentStripAddress + SLIP_BACKGROUND_STRIP_RIGHT_OFFSET,
		    SLIP_BACKGROUND_STRIP_CORNER_COUNT,
		    SlipBytes_ReadLE16(stripTable + stripOffset),
		    SLIP_BACKGROUND_STRIP_CORNER_POINTER_TABLE_DOS_ADDRESS,
		    true,
		    true,
		    draw3DPointPointerRingCarry,
		    !draw3DPointPointerRingCarry,
		    {0},
		    {0},
		    currentStripAddress + SLIP_BACKGROUND_STRIP_BYTES,
		    loopCount - 1u,
		    loopCount != 1u,
		    SlipBytes_ReadLE16(stripTable + stripOffset),
		    0,
		    0};
		stripOffset += SLIP_BACKGROUND_STRIP_BYTES;
		--loopCount;
		++visitCount;
	}
	*result = (SlipDraw3DStripDispatch){renderFlags,
	                                    0,
	                                    stripTableBaseAddress,
	                                    sourceCount,
	                                    (uint32_t)sourceCount - 1u,
	                                    stripTableBaseAddress + SLIP_BACKGROUND_STRIP_TABLE_HEADER_BYTES,
	                                    visitCount,
	                                    renderFlags,
	                                    true,
	                                    0,
	                                    0};
	return 1;
}

int SlipDraw3D_StripDispatchWithPointPointerRing(
    SlipDraw3DRecordPool *pool, const uint8_t *stripTable, size_t stripTableBytes, uint32_t stripTableBaseAddress,
    uint32_t renderFlags, uint32_t initialMaterialColor, int32_t minX, int32_t maxX, int32_t minY, int32_t maxY,
    int depthClipRejected, int screenClipRejected, SlipDraw3DReturnActiveVisit *returnVisits,
    size_t returnVisitCapacity, SlipDraw3DPointPointerRingVisit *pointRingVisits, size_t pointRingVisitCapacity,
    SlipDraw3DStripDispatchVisit *visits, size_t visitCapacity, SlipDraw3DStripDispatch *result) {
	uint16_t sourceCount;
	uint32_t loopCount;
	uint32_t stripOffset;
	size_t visitCount;
	size_t returnVisitCount;
	size_t pointRingVisitCount;
	size_t rasterizedCount;
	SlipDraw3DRasterPoint flatPoints[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];

	if (pool == NULL || stripTable == NULL || pointRingVisits == NULL || visits == NULL || result == NULL ||
	    stripTableBytes < SLIP_BACKGROUND_STRIP_TABLE_HEADER_BYTES) {
		return 0;
	}
	sourceCount = SlipBytes_ReadLE16(stripTable);
	if (sourceCount <= 1u) {
		return 0;
	}
	loopCount = (uint32_t)sourceCount - 1u;
	if ((size_t)loopCount > visitCapacity ||
	    (size_t)loopCount > pointRingVisitCapacity / SLIP_BACKGROUND_STRIP_CORNER_COUNT) {
		return 0;
	}
	stripOffset = SLIP_BACKGROUND_STRIP_TABLE_HEADER_BYTES;
	visitCount = 0;
	returnVisitCount = 0;
	pointRingVisitCount = 0;
	rasterizedCount = 0;
	while (loopCount != 0u) {
		SlipDraw3DStripDispatchVisit *visit;
		uint8_t stripCornerPointers[SLIP_BACKGROUND_STRIP_CORNER_COUNT * SLIP_BACKGROUND_STRIP_CORNER_POINTER_BYTES];
		uint32_t currentStripAddress;
		uint16_t stripShade;
		uint32_t stripMaterialColor;
		SlipDraw3DReturnActiveRing returnActive;
		SlipDraw3DPointPointerRing pointRing;
		SlipDraw3DClipDispatchExecute clipDispatch;
		SlipDraw3DClipFlagVisit clipFlagVisits[SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT];
		SlipDraw3DFlatRingDispatch flatDispatch;
		size_t returnVisitCountBefore;

		if ((size_t)stripOffset + SLIP_BACKGROUND_STRIP_BOUNDARY_PAIR_COUNT * SLIP_BACKGROUND_STRIP_BYTES >
		    stripTableBytes) {
			return 0;
		}
		currentStripAddress = stripTableBaseAddress + stripOffset;
		SlipDraw3D_WriteLE32(stripCornerPointers, currentStripAddress + SLIP_BACKGROUND_STRIP_LEFT_OFFSET);
		SlipDraw3D_WriteLE32(stripCornerPointers + SLIP_BACKGROUND_STRIP_CORNER_POINTER_BYTES,
		                     currentStripAddress + (SLIP_BACKGROUND_STRIP_BYTES + SLIP_BACKGROUND_STRIP_LEFT_OFFSET));
		SlipDraw3D_WriteLE32(stripCornerPointers + 2 * SLIP_BACKGROUND_STRIP_CORNER_POINTER_BYTES,
		                     currentStripAddress + (SLIP_BACKGROUND_STRIP_BYTES + SLIP_BACKGROUND_STRIP_RIGHT_OFFSET));
		SlipDraw3D_WriteLE32(stripCornerPointers + 3 * SLIP_BACKGROUND_STRIP_CORNER_POINTER_BYTES,
		                     currentStripAddress + SLIP_BACKGROUND_STRIP_RIGHT_OFFSET);
		stripShade = SlipBytes_ReadLE16(stripTable + stripOffset);
		stripMaterialColor = (initialMaterialColor & SLIP_DRAW3D_UPPER_WORD_MASK) | (uint32_t)stripShade;
		returnVisitCountBefore = returnVisitCount;

		if (!SlipDraw3D_ReturnActiveRing(pool, returnVisits, returnVisitCapacity, &returnActive)) {
			return 0;
		}
		returnVisitCount += returnActive.visitCount;
		if (!SlipDraw3D_PointPointerRingWithScreenPointFlags(
		        pool, stripCornerPointers, sizeof(stripCornerPointers), stripTableBaseAddress, stripTable,
		        stripTableBytes, SLIP_BACKGROUND_STRIP_CORNER_COUNT, stripMaterialColor, minX, maxX, minY, maxY,
		        depthClipRejected, screenClipRejected, pointRingVisits + pointRingVisitCount,
		        pointRingVisitCapacity - pointRingVisitCount, &pointRing)) {
			return 0;
		}
		pointRingVisitCount += pointRing.visitCount;
		memset(&clipDispatch, 0, sizeof(clipDispatch));
		if (!pointRing.carryOut && (pointRing.calledClipDepth || pointRing.calledClipScreen)) {
			uint8_t *const recordPoolBytes = SlipDraw3D_RecordPoolBytes(pool);
			const size_t recordPoolBytesCount = SlipDraw3D_RecordPoolByteSize();

			if (recordPoolBytes == NULL ||
			    !SlipDraw3D_ClipDispatchExecute(
			        recordPoolBytes, recordPoolBytesCount, pointRing.inputActiveHeadOffset, pool->freeHeadOffset,
			        pointRing.anyFlagsBeforeDispatch, pointRing.allFlagsBeforeDispatch, 0, pointRing.mode, 0, 0, minX,
			        maxX, minY, maxY, NULL, NULL, NULL, 0, NULL, 0, 0, 0, 0, 0, 0, SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT,
			        clipFlagVisits, sizeof(clipFlagVisits) / sizeof(clipFlagVisits[0]), NULL, 0, NULL, 0, NULL, 0,
			        &clipDispatch)) {
				return 0;
			}
			pool->inputActiveHeadOffset = clipDispatch.activeHeadOffsetOut;
			pointRing.inputActiveHeadOffset = clipDispatch.activeHeadOffsetOut;
			pointRing.calledClipDepth = clipDispatch.dispatch.calledClipDepth;
			pointRing.depthClipRejected = clipDispatch.dispatch.depthClipRejected;
			pointRing.calledClipScreen = clipDispatch.dispatch.calledClipScreen;
			pointRing.screenClipRejected = clipDispatch.dispatch.screenClipRejected;
			pointRing.carryOut = clipDispatch.dispatch.carryOut;
		}
		memset(&flatDispatch, 0, sizeof(flatDispatch));
		if (!pointRing.carryOut) {
			if (!SlipDraw3D_RasterizeFlatRing(pool, pointRing.inputActiveHeadOffset, pointRing.drawMode, renderFlags,
			                                  pointRing.materialColor, 0, SLIP_DRAW3D_RECORD_NEXT_OFFSET, flatPoints,
			                                  sizeof(flatPoints) / sizeof(flatPoints[0]), &flatDispatch)) {
				return 0;
			}
			if (flatDispatch.rasterized) {
				++rasterizedCount;
			}
		}
		visit = visits + visitCount;
		*visit = (SlipDraw3DStripDispatchVisit){
		    currentStripAddress,
		    loopCount,
		    currentStripAddress + SLIP_BACKGROUND_STRIP_LEFT_OFFSET,
		    currentStripAddress + (SLIP_BACKGROUND_STRIP_BYTES + SLIP_BACKGROUND_STRIP_LEFT_OFFSET),
		    currentStripAddress + (SLIP_BACKGROUND_STRIP_BYTES + SLIP_BACKGROUND_STRIP_RIGHT_OFFSET),
		    currentStripAddress + SLIP_BACKGROUND_STRIP_RIGHT_OFFSET,
		    SLIP_BACKGROUND_STRIP_CORNER_COUNT,
		    stripShade,
		    SLIP_BACKGROUND_STRIP_CORNER_POINTER_TABLE_DOS_ADDRESS,
		    true,
		    true,
		    pointRing.carryOut,
		    !pointRing.carryOut,
		    flatDispatch,
		    {0},
		    currentStripAddress + SLIP_BACKGROUND_STRIP_BYTES,
		    loopCount - 1u,
		    loopCount != 1u,
		    stripMaterialColor,
		    returnVisitCountBefore,
		    returnVisitCount};
		if (flatDispatch.pointCount <= SLIP_DRAW3D_RECORD_POOL_USABLE_COUNT) {
			memcpy(visit->flatPoints, flatPoints, (size_t)flatDispatch.pointCount * sizeof(flatPoints[0]));
		}
		stripOffset += SLIP_BACKGROUND_STRIP_BYTES;
		--loopCount;
		++visitCount;
	}
	*result = (SlipDraw3DStripDispatch){renderFlags,
	                                    0,
	                                    stripTableBaseAddress,
	                                    sourceCount,
	                                    (uint32_t)sourceCount - 1u,
	                                    stripTableBaseAddress + SLIP_BACKGROUND_STRIP_TABLE_HEADER_BYTES,
	                                    visitCount,
	                                    renderFlags,
	                                    true,
	                                    returnVisitCount,
	                                    rasterizedCount};
	return 1;
}

uint32_t SlipDraw3D_materialCallbackCount;
static SlipDraw3DMaterialCallback materialCallbacks[SLIP_DRAW3D_MATERIAL_CALLBACK_CAPACITY];

void SlipDraw3D_RegisterMaterialCallback(SlipDraw3DMaterialCallback callback) {
	if (SlipDraw3D_materialCallbackCount == SLIP_DRAW3D_MATERIAL_CALLBACK_CAPACITY) {
		return;
	}
	for (uint32_t i = 0; i < SlipDraw3D_materialCallbackCount; ++i) {
		if (materialCallbacks[i] == callback) {
			return;
		}
	}
	materialCallbacks[SlipDraw3D_materialCallbackCount++] = callback;
}

void SlipDraw3D_NotifyMaterials(void) {
	const uint32_t count = SlipDraw3D_materialCallbackCount;
	for (uint32_t i = 0; i < count; ++i) {
		materialCallbacks[i]();
	}
}
