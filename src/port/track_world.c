#include "track_world.h"
#include "artic_slot.h"
#include "byte_order.h"
#include "fixed_point.h"
#include "frame_timer.h"
#include "race_collision.h"
#include "random_sequence.h"
#include "raster/raster.h"
#include "renderer_flags.h"
#include "resource_host.h"
#include "resource_storage.h"
#include "runtime.h"
#include "shape_format.h"
#include "text_layout.h"
#include "track_format.h"
#include "track_view_render.h"

#include <stdlib.h>
#include <string.h>

enum {
	SLIP_TRACK_WORLD_WORLD_MATRIX_TOKEN = 0x00037b20u,
	SLIP_TRACK_WORLD_VIEW_MATRIX_TOKEN = 0x00037b34u,
	SLIP_TRACK_WORLD_CAMERA_MATRIX_TOKEN = 0x00033d48u,
	SLIP_TRACK_WORLD_MATRIX_CONTINUATION_TOKEN = 0x00037a46u,
	SLIP_TRACK_WORLD_DRAW_FLAGS_CONTINUATION_TOKEN = 0x0003a7e7u,
	SLIP_TRACK_WORLD_REPLAY_LIST_TOKEN = 0x00033ef4u,
	SLIP_TRACK_WORLD_POLYGON_INDICES_TOKEN = 0x0003fdf2u,
	SLIP_TRACK_WORLD_SHADE_ENTRIES_TOKEN = 0x0003fd90u,
	SLIP_TRACK_WORLD_PRIMITIVE_DRAW_LIST_TOKEN = 0x00033e74u,
	SLIP_TRACK_WORLD_OBJECT_SCHEDULING_CALLBACK_TOKEN = 0x00037c93u,
	SLIP_TRACK_MATERIAL_HANDLER_MAXIMUM_DEPTH = 1561600,
	SLIP_TRACK_SHADE_ENTRY_BYTES = 6,
	SLIP_TRACK_SHADE_SELECTOR_OFFSET = 4,
	SLIP_TRACK_COMPONENT_RANGE_MINIMUM_DEPTH = -256,
	SLIP_TRACK_PRIMITIVE_RANGE_DISTANCE = 19520,
	SLIP_TRACK_SPECIAL_RENDER_MODE = SLIP_TRC_COMPONENT_SPECIAL_PASS,
	SLIP_TRACK_SPECIAL_RENDER_MASK = SLIP_TRC_COMPONENT_SPECIAL_PASS_MASK,
	SLIP_TRACK_DEPTH_FADE_THRESHOLD = 4096,
	SLIP_REFUEL_FACE_SKIP_INTERSECTION = SLIP_TRC_PRIMITIVE_TRENCH,
	SLIP_REFUEL_SHADE_MASK = SLIP_Q14_ONE - 1,
	SLIP_TRACK_DEFERRED_VISIT_CAPACITY = 128,
	SLIP_TRACK_REGISTER_UPPER_WORD_MASK = 0xffff0000u,
	SLIP_TRACK_REGISTER_UPPER_BYTES_MASK = 0xffffff00u,
	SLIP_TRACK_DWORD_SIGN_BIT = 0x80000000u,
	SLIP_TRACK_WORD_SIGN_BIT = 0x8000u,
	SLIP_TRACK_SIGN_EXTENSION_20 = 0xfff00000u,
	SLIP_TRACK_LOCAL_VERTEX_TRANSFORM_SHIFT = SLIP_Q14_FRACTION_BITS - SLIP_TRC_POINT_COORDINATE_SHIFT,
	SLIP_TRACK_LOCAL_VERTEX_SIGN_EXTENSION = UINT32_MAX << (32 - SLIP_TRACK_LOCAL_VERTEX_TRANSFORM_SHIFT),
	SLIP_TRACK_AXIS_WRAPPED_LOOP_COUNT = UINT16_MAX + 1u,
	SLIP_TRACK_REFLECTION_MATERIAL_NAME_TOKEN = 0x34436,
	SLIP_TRACK_REFLECTION_SAVED_MATRIX_TOKEN = 0x34424,
	SLIP_TRACK_REFLECTION_VIEWPORT_READ_MODE = 16,
	SLIP_TRACK_REFLECTION_MAXIMUM_DEPTH = 4880000,
	SLIP_TRACK_CAMERA_VIEWPORT_SELECTOR = 0x8000,
	SLIP_TRACK_FRAME_STATE_TOKEN = 0x3d0,
	SLIP_TRACK_WEAPON_LABEL_COLOUR = 255,
	SLIP_TRACK_WEAPON_LABEL_TOP_INSET = 2,
	SLIP_TRACK_RAY_MINIMUM_FACING_Q14 = 16,
	SLIP_TRACK_COLLISION_BISECTION_STEPS = 16,
	/* Scale both operands before division, retaining a Q14 result. */
	SLIP_TRACK_COLLISION_DIVISOR_SHIFT = 16,
	SLIP_TRACK_COLLISION_DIVIDEND_SHIFT = SLIP_TRACK_COLLISION_DIVISOR_SHIFT + SLIP_Q14_FRACTION_BITS,
	SLIP_TRACK_SLOT_DRAW_CAPACITY_MULTIPLIER = 2,
	SLIP_TRACK_COLLISION_SURFACE_CLEARANCE = 488,
	SLIP_TRACK_REFUEL_PLANE_OFFSET = 976,
	SLIP_TRACK_COLLISION_ALIGNED_FACING_Q14 = SLIP_Q14_ONE - 16,
	SLIP_TRACK_COLLISION_ALIGNED_SAMPLE_COUNT = 4,
	SLIP_TRACK_COLLISION_BACKOFF_SHIFT = 4,
	SLIP_TRACK_COLLISION_BACKOFF_STEPS = (1 << SLIP_TRACK_COLLISION_BACKOFF_SHIFT) - 1,
	SLIP_TRACK_RECORD_BOUNDARY_BISECTION_STEPS = 12,
	SLIP_TRACK_OBJECT_LIST_RECORD_BYTES = 52,
	SLIP_TRACK_OBJECT_LIST_WORLD_X_OFFSET = 4,
	SLIP_TRACK_OBJECT_LIST_WORLD_Y_OFFSET = 8,
	SLIP_TRACK_OBJECT_LIST_WORLD_Z_OFFSET = 12,
	SLIP_TRACK_OBJECT_LIST_POSITION_END = 16,
	SLIP_TRACK_OBJECT_LIST_CONTINUATION_TOKEN = 0x3db46,
	SLIP_TRACK_MATCHED_COMPONENT_LIMIT_START = 64,
	SLIP_TRACK_MATCHED_COMPONENT_LIMIT_END = 79,
	SLIP_OBJECT_SERVER_COUNT = 10,
	SLIP_OBJECT_SERVER_DOS_STRIDE = 6,
	SLIP_OBJECT_SERVER_TABLE_TOKEN = 0x2670c,
	SLIP_OBJECT_DEFERRED_SLOTS_TOKEN = 0x26644,
	SLIP_OBJECT_DEFERRED_SLOT_BYTES = sizeof(uint16_t),
	SLIP_OBJECT_DOS_MATRIX_OFFSET = 72,
	SLIP_OBJECT_DRAW_MATRIX_TOKEN = 0x27254
};

SlipView3DVec32 SlipTrackWorld_doorPosition;
SlipView3DVec16 SlipTrackWorld_doorDirection;
uint32_t SlipTrackWorld_doorsInitialized;
uint16_t SlipTrackWorld_doorCount;
SlipTrackDoorRecord SlipTrackWorld_doors[SLIP_TRACK_DOOR_CAPACITY];
SlipResourcePayload SlipTrackWorld_doorShapes[SLIP_TRACK_DOOR_CAPACITY];

uint32_t SlipTrackWorld_refuelInitialized;
uint32_t SlipTrackWorld_lastRecord;
uint16_t SlipTrackWorld_slotObject;
static uint16_t SlipTrackWorld_hitPrimitive = UINT16_MAX;
static int16_t SlipTrackWorld_hitNormalX;
static int16_t SlipTrackWorld_hitNormalY;
static int16_t SlipTrackWorld_hitNormalZ;

static void SlipTrackWorld_WriteLE16(uint8_t *p, uint16_t value) {
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
}

static void SlipTrackWorld_WriteLE32(uint8_t *p, uint32_t value) {
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
	p[2] = (uint8_t)(value >> 16);
	p[3] = (uint8_t)(value >> 24);
}

static bool SlipTrackWorld_DosAddressToOffset(uint32_t dosAddress, uint32_t baseAddress, size_t bufferBytes,
                                              uint32_t *offset) {
	uint32_t delta;

	if (dosAddress < baseAddress || offset == 0) {
		return false;
	}
	delta = dosAddress - baseAddress;
	if ((size_t)delta >= bufferBytes) {
		return false;
	}
	*offset = delta;
	return true;
}

static uint32_t SlipTrackWorld_SignedShiftRight20(uint32_t value) {
	return (value >> 20) | ((value & SLIP_TRACK_DWORD_SIGN_BIT) != 0 ? SLIP_TRACK_SIGN_EXTENSION_20 : 0);
}

static int32_t SlipTrackWorld_ScaleSignedWordBy64(uint16_t value) {
	return (int32_t)((uint32_t)(int32_t)(int16_t)value << SLIP_TRC_POINT_COORDINATE_SHIFT);
}

static int32_t SlipTrackWorld_MultiplySignedShift(uint32_t lhs, uint32_t rhs, unsigned shift) {
	return (int32_t)(((int64_t)(int32_t)lhs * (int64_t)(int32_t)rhs) >> shift);
}

static int32_t SlipTrackWorld_DotProduct32x16Shift14(uint32_t x, uint32_t y, uint32_t z, uint16_t nx, uint16_t ny,
                                                     uint16_t nz) {
	uint64_t product;
	uint32_t low;
	uint32_t high;
	uint32_t nextLow;
	uint32_t nextHigh;
	uint32_t carry;

	product = (uint64_t)((int64_t)(int32_t)x * (int64_t)(int16_t)nx);
	low = (uint32_t)product;
	high = (uint32_t)(product >> 32);

	product = (uint64_t)((int64_t)(int32_t)y * (int64_t)(int16_t)ny);
	nextLow = (uint32_t)product;
	nextHigh = (uint32_t)(product >> 32);
	carry = low + nextLow < low;
	low += nextLow;
	high = (high & SLIP_TRACK_REGISTER_UPPER_WORD_MASK) | (uint16_t)((uint16_t)high + (uint16_t)nextHigh + carry);

	product = (uint64_t)((int64_t)(int32_t)z * (int64_t)(int16_t)nz);
	nextLow = (uint32_t)product;
	nextHigh = (uint32_t)(product >> 32);
	carry = nextLow + low < nextLow;
	low += nextLow;
	high = (nextHigh & SLIP_TRACK_REGISTER_UPPER_WORD_MASK) | (uint16_t)((uint16_t)nextHigh + (uint16_t)high + carry);

	return (int32_t)((low >> SLIP_Q14_FRACTION_BITS) | (high << SLIP_Q14_DWORD_HIGH_SHIFT));
}

static uint32_t SlipTrackWorld_RoundedDotProductShift14(uint32_t x, uint32_t y, uint32_t z, uint32_t nx, uint32_t ny,
                                                        uint32_t nz) {
	uint64_t sum;
	uint32_t low;
	uint32_t high;
	uint32_t shifted;

	sum = (uint64_t)((int64_t)(int32_t)x * (int64_t)(int32_t)nx);
	sum += (uint64_t)((int64_t)(int32_t)y * (int64_t)(int32_t)ny);
	sum += (uint64_t)((int64_t)(int32_t)z * (int64_t)(int32_t)nz);
	low = (uint32_t)sum;
	high = (uint32_t)(sum >> 32);
	shifted = (low >> SLIP_Q14_FRACTION_BITS) | (high << SLIP_Q14_DWORD_HIGH_SHIFT);
	if ((low & (1u << (SLIP_Q14_FRACTION_BITS - 1))) != 0) {
		++shifted;
	}
	return shifted;
}

static uint16_t SlipTrackWorld_MultiplyWordsShift14(uint32_t source, uint16_t multiplier, uint32_t *product) {
	const uint32_t p = (uint32_t)(uint16_t)source * (uint32_t)multiplier;

	if (product != 0) {
		*product = p;
	}
	return (uint16_t)(p >> SLIP_Q14_FRACTION_BITS);
}

static bool SlipTrackWorld_ReplaySourceChild(uint16_t offset, const uint8_t *chunkBase, size_t chunkBaseBytes,
                                             SlipTrackWorldReplaySourceChild *child) {
	if (child == 0) {
		return false;
	}
	*child = (SlipTrackWorldReplaySourceChild){.offset = offset, .zeroBranch = offset == 0};
	if (offset == 0) {
		return true;
	}
	if (chunkBase == 0 || (size_t)offset > chunkBaseBytes) {
		return false;
	}
	child->childRecord = chunkBase + offset;
	child->callScanReplaySources = true;
	return true;
}

bool SlipTrackWorld_BindHostBuffers(uint8_t *cellTable, size_t cellTableBytes, uint8_t *objectList,
                                    size_t objectListBytes, uint8_t *deferredList, size_t deferredListBytes,
                                    uint8_t *deferredScan, size_t deferredScanBytes,
                                    SlipTrackWorldHostBuffers *result) {
	if (result == 0 || cellTable == 0 || objectList == 0 || deferredList == 0 || deferredScan == 0 ||
	    cellTableBytes < SLIP_TRACK_WORLD_CELL_TABLE_BYTES || objectListBytes < SLIP_TRACK_WORLD_OBJECT_LIST_BYTES ||
	    deferredListBytes < SLIP_TRACK_WORLD_DEFERRED_LIST_BYTES ||
	    deferredScanBytes < SLIP_TRACK_WORLD_DEFERRED_SCAN_BYTES) {
		return false;
	}
	*result = (SlipTrackWorldHostBuffers){
	    cellTable,         cellTableBytes, objectList, objectListBytes, deferredList, deferredListBytes, deferredScan,
	    deferredScanBytes, true,           true};
	return true;
}

void SlipTrackWorld_ClearSlotRecordLinks(uint16_t trackHandle, uint8_t *trdBase) {
	uint8_t *groupCursor;
	uint32_t groupAdvance;
	uint32_t remainingGroups;

	if (trackHandle == 0) {
		return;
	}
	groupCursor = trdBase;
	groupAdvance = SlipBytes_ReadLE16(groupCursor + SLIP_TRD_GROUP_TABLE_OFFSET);
	groupCursor += groupAdvance;
	remainingGroups = SlipBytes_ReadLE16(groupCursor);
	if (remainingGroups == 0) {
		return;
	}
	groupCursor += SLIP_TRD_TABLE_COUNT_BYTES;
	do {
		uint8_t *const savedGroupCursor = groupCursor;
		const uint32_t childOffset = SlipBytes_ReadLE16(groupCursor + SLIP_TRACK_LINKED_OFFSET);

		if (childOffset != 0) {
			uint8_t *child = trdBase + childOffset;
			uint32_t childCount = SlipBytes_ReadLE16(child);

			child += SLIP_TRD_TABLE_COUNT_BYTES;
			do {
				SlipTrackWorld_WriteLE16(child + SLIP_TRD_SECTION_DRAW_LIST_OFFSET, 0);
				child += SLIP_TRD_SECTION_BYTES;
				--childCount;
			} while (childCount != 0);
		}
		groupCursor = savedGroupCursor;
		groupAdvance = SlipBytes_ReadLE16(groupCursor);
		groupCursor += groupAdvance;
		--remainingGroups;
	} while (remainingGroups != 0);
}

void SlipTrackWorld_FreeAllSlotListEntries(uint16_t trackHandle, uint8_t *slotDrawBase, size_t slotDrawBytes,
                                           uint32_t slotDrawBaseAddress, uint32_t slotDrawFreeListAddress,
                                           uint8_t *trdBase, size_t trackDataSize, uint32_t trdBaseAddress,
                                           uint8_t *slotListBase, size_t slotListBytes, uint32_t slotListBaseAddress,
                                           uint32_t slotListActiveAddress, uint32_t slotListFreeAddress) {
	uint32_t activeOffset;
	uint32_t slotAddress;

	if (!SlipTrackWorld_DosAddressToOffset(slotListActiveAddress, slotListBaseAddress, slotListBytes, &activeOffset)) {
		return;
	}
	const SlipTrackSlotRecord *const active = (const SlipTrackSlotRecord *)(const void *)(slotListBase + activeOffset);
	slotAddress = active->nextSlotAddress;
	while (slotAddress != slotListActiveAddress) {
		uint32_t offset;
		uint8_t *slotBytes;
		uint32_t nextAddress;

		if (!SlipTrackWorld_DosAddressToOffset(slotAddress, slotListBaseAddress, slotListBytes, &offset)) {
			return;
		}
		slotBytes = slotListBase + offset;
		const SlipTrackSlotRecord *const slot = (const SlipTrackSlotRecord *)(const void *)slotBytes;
		nextAddress = slot->nextSlotAddress;
		if (!SlipTrackWorld_FreeSlotListEntry(trackHandle, slotDrawBase, slotDrawBytes, slotDrawBaseAddress,
		                                      slotDrawFreeListAddress, trdBase, trackDataSize, trdBaseAddress,
		                                      slotListBase, slotListBytes, slotListBaseAddress, slotListFreeAddress,
		                                      slotBytes)) {
			return;
		}
		slotAddress = nextAddress;
	}
}

void SlipTrackWorld_ResetSlots(uint16_t trackHandle, uint8_t *trdBase, size_t trackDataSize, uint32_t trdBaseAddress,
                               uint8_t *slotDrawBase, size_t slotDrawBytes, uint32_t slotDrawBaseAddress,
                               uint32_t slotDrawFreeListAddress, uint16_t slotDrawCount, uint8_t *slotListBase,
                               size_t slotListBytes, uint32_t slotListBaseAddress, uint32_t slotListActiveAddress,
                               uint32_t slotListFreeAddress) {
	uint32_t slotDrawFreeOffset;
	SlipTrackWorldSlotDrawRing ring;

	SlipTrackWorld_FreeAllSlotListEntries(
	    trackHandle, slotDrawBase, slotDrawBytes, slotDrawBaseAddress, slotDrawFreeListAddress, trdBase, trackDataSize,
	    trdBaseAddress, slotListBase, slotListBytes, slotListBaseAddress, slotListActiveAddress, slotListFreeAddress);
	SlipTrackWorld_ClearSlotRecordLinks(trackHandle, trdBase);
	if (!SlipTrackWorld_DosAddressToOffset(slotDrawFreeListAddress, slotDrawBaseAddress, slotDrawBytes,
	                                       &slotDrawFreeOffset)) {
		return;
	}
	SlipTrackWorld_InitSlotDrawRing(slotDrawBase + slotDrawFreeOffset, slotDrawBytes - slotDrawFreeOffset,
	                                slotDrawFreeListAddress, slotDrawCount, NULL, 0, &ring);
}

bool SlipTrackWorld_BindAxisRampWorkspace(uint8_t *workspace, size_t workspaceBytes, uint16_t allocationHandle,
                                          SlipTrackWorldAxisRampWorkspace *result) {
	uint16_t rampPointCount;
	uint16_t pointStrideBytes;
	uint32_t allocationBytes;
	uint8_t *base;

	if (workspace == 0 || result == 0 || workspaceBytes < SLIP_TRACK_WORLD_AXIS_RAMP_BYTES) {
		return false;
	}
	rampPointCount = SLIP_TRACK_WORLD_AXIS_RAMP_POINT_COUNT;
	pointStrideBytes = SLIP_TRACK_WORLD_AXIS_RAMP_POINT_BYTES;
	allocationBytes = (uint32_t)pointStrideBytes * (uint32_t)rampPointCount;
	base = workspace;
	*result = (SlipTrackWorldAxisRampWorkspace){.rampPointCount = rampPointCount,
	                                            .pointStrideBytes = pointStrideBytes,
	                                            .allocationBytes = allocationBytes,
	                                            .callResourceAllocateAnonymous = true,
	                                            .allocationHandle = allocationHandle,
	                                            .callLockResource = true,
	                                            .base = base,
	                                            .rampX = base,
	                                            .rampY = base + SLIP_TRACK_WORLD_AXIS_RAMP_Y_OFFSET,
	                                            .rampZ = base + SLIP_TRACK_WORLD_AXIS_RAMP_Z_OFFSET,
	                                            .clearsCarry = true,
	                                            .ret = true};
	return allocationBytes == SLIP_TRACK_WORLD_AXIS_RAMP_BYTES;
}

bool SlipTrackWorld_BindAxisTestWorkspace(uint8_t *workspace, size_t workspaceBytes, uint16_t allocationHandle,
                                          SlipTrackWorldAxisTestWorkspace *result) {
	uint16_t testWordCount;
	uint16_t testWordBytes;
	uint32_t allocationBytes;
	uint8_t *tableBase;
	uint32_t secondTableOffset;
	uint32_t thirdTableOffset;

	if (workspace == 0 || result == 0 || workspaceBytes < SLIP_TRACK_WORLD_AXIS_TEST_BYTES) {
		return false;
	}
	testWordCount = SLIP_TRACK_WORLD_AXIS_TEST_COUNT;
	testWordBytes = sizeof(uint16_t);
	allocationBytes = (uint32_t)testWordBytes * (uint32_t)testWordCount;
	tableBase = workspace;
	secondTableOffset = (uint32_t)(uint16_t)((SLIP_TRACK_WORLD_AXIS_TEST_X_COUNT)*testWordBytes);
	thirdTableOffset = secondTableOffset + (uint32_t)(uint16_t)((SLIP_TRACK_WORLD_AXIS_TEST_Y_COUNT)*testWordBytes);
	*result = (SlipTrackWorldAxisTestWorkspace){.testWordCount = testWordCount,
	                                            .testWordBytes = testWordBytes,
	                                            .allocationBytes = allocationBytes,
	                                            .callResourceAllocateAnonymous = true,
	                                            .allocationHandle = allocationHandle,
	                                            .callLockResource = true,
	                                            .tableBase = tableBase,
	                                            .testTableX = tableBase,
	                                            .testTableY = tableBase + secondTableOffset,
	                                            .testTableZ = tableBase + thirdTableOffset,
	                                            .clearsCarry = true,
	                                            .ret = true};
	return allocationBytes == SLIP_TRACK_WORLD_AXIS_TEST_BYTES;
}

bool SlipTrackWorld_ClearRecordClassificationCache(uint8_t *trkBase, size_t trkSize, uint16_t *clearedCount) {
	uint32_t listOffset;
	uint32_t recordCount;
	uint32_t recordOffset;

	if (clearedCount != 0) {
		*clearedCount = 0;
	}
	if (trkBase == 0 || trkSize < SLIP_TRK_CLASSIFICATION_LIST_END) {
		return false;
	}
	listOffset = SlipBytes_ReadLE16(trkBase + SLIP_TRK_CLASSIFICATION_LIST_OFFSET);
	if (listOffset == 0) {
		return true;
	}
	if ((size_t)listOffset + SLIP_TRK_TABLE_COUNT_BYTES > trkSize) {
		return false;
	}
	recordOffset = listOffset;
	recordCount = SlipBytes_ReadLE16(trkBase + recordOffset);
	recordOffset += SLIP_TRK_TABLE_COUNT_BYTES;
	if (recordCount == 0) {
		return false;
	}
	for (; recordCount != 0; --recordCount) {
		if ((size_t)recordOffset + SLIP_TRK_CLASSIFICATION_CACHE_OFFSET + 1u > trkSize) {
			return false;
		}
		trkBase[recordOffset + SLIP_TRK_CLASSIFICATION_CACHE_OFFSET] = 0;
		if (clearedCount != 0) {
			++*clearedCount;
		}
		recordOffset += SLIP_TRK_CLASSIFICATION_RECORD_BYTES;
	}
	return true;
}

bool SlipTrackWorld_TraversalEntry(uint8_t *trkBase, size_t trkSize, SlipTrackWorldTraversalContext *traversalContext,
                                   SlipTrackWorldTraversalEntry *result) {
	uint16_t clearedCount;
	uint16_t rootListOffset;
	uint32_t rootRecordOffset;
	SlipTrackWorldTraversal traversal;

	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldTraversalEntry){.callClearRecordCache = true, .ret = true};
	if (trkBase == 0 || trkSize < SLIP_TRK_ROOT_LIST_END) {
		return false;
	}
	if (!SlipTrackWorld_ClearRecordClassificationCache(trkBase, trkSize, &clearedCount)) {
		return false;
	}
	result->clearedCount = clearedCount;
	rootListOffset = SlipBytes_ReadLE16(trkBase + SLIP_TRK_ROOT_LIST_OFFSET);
	result->rootListOffset = rootListOffset;
	result->zeroRootBranch = rootListOffset == 0;
	if (rootListOffset == 0) {
		return true;
	}
	rootRecordOffset = (uint32_t)rootListOffset + SLIP_TRK_TABLE_COUNT_BYTES;
	if ((size_t)rootRecordOffset > trkSize) {
		return false;
	}
	result->rootRecordOffset = rootRecordOffset;
	result->rootRecord = trkBase + rootRecordOffset;
	result->callTraverseRecords = true;
	if (traversalContext != 0) {
		if (!SlipTrackWorld_TraverseClassified(
		        trkBase, trkSize, rootRecordOffset, traversalContext->trkBasePointer, traversalContext->tableBase,
		        traversalContext->tableSize, traversalContext->pointBase, traversalContext->pointBaseSize,
		        traversalContext->origin, traversalContext->minZ, traversalContext->maxZ, traversalContext->projectMask,
		        traversalContext->projectMaskUserData, traversalContext->chunkBase, traversalContext->chunkBaseSize,
		        traversalContext->filter, traversalContext->chunkCounter, traversalContext->chunkProcessExecution,
		        traversalContext->drawStateIndex, traversalContext->visits, traversalContext->visitCapacity,
		        traversalContext->stackOffsets, traversalContext->stackDepths, traversalContext->stackCapacity,
		        &traversal)) {
			return false;
		}
		result->executed = true;
		result->traversalVisitCount = traversal.visitCount;
		result->traversalMaxDepth = traversal.maxDepth;
		result->traversalHitVisitCapacity = traversal.hitVisitCapacity;
		result->traversalHitStackCapacity = traversal.hitStackCapacity;
		traversalContext->chunkCounter = traversal.finalChunkCounter;
	}
	return true;
}

SlipView3DVec32 SlipTrackWorld_SourceChunkPoint(uint16_t sourceX, uint16_t sourceY, uint16_t sourceZ,
                                                SlipView3DVec32 currentChunkOrigin) {
	uint32_t worldX;
	uint32_t worldY;
	uint32_t worldZ;

	worldX = (uint32_t)sourceX;
	worldY = (uint32_t)sourceY;
	worldZ = (uint32_t)sourceZ;
	worldX <<= SLIP_TRK_CHUNK_POINT_COORDINATE_SHIFT;
	worldY <<= SLIP_TRK_CHUNK_POINT_COORDINATE_SHIFT;
	worldZ <<= SLIP_TRK_CHUNK_POINT_COORDINATE_SHIFT;
	worldX += (uint32_t)currentChunkOrigin.x;
	worldY += (uint32_t)currentChunkOrigin.y;
	worldZ += (uint32_t)currentChunkOrigin.z;
	return (SlipView3DVec32){(int32_t)worldX, (int32_t)worldY, (int32_t)worldZ};
}

bool SlipTrackWorld_NodeTest(const uint8_t *tableBase, size_t tableSize, uint16_t nodeWordOffset,
                             SlipTrackWorldNodeTest *result) {
	uint32_t tableOffset;
	uint16_t tableWord;

	if (tableBase == 0 || result == 0 || (size_t)nodeWordOffset + sizeof(uint16_t) > tableSize) {
		return false;
	}
	tableOffset = (uint32_t)nodeWordOffset;
	tableWord = SlipBytes_ReadLE16(tableBase + tableOffset);
	result->nodeWordOffset = (uint16_t)tableOffset;
	result->carryFromSar = (tableWord & 1u) != 0;
	result->classificationWord = (uint16_t)((tableWord >> 1) | (tableWord & SLIP_TRACK_WORD_SIGN_BIT));
	return true;
}

bool SlipTrackWorld_SumThreePoints(const uint8_t *record, size_t recordBytesRemaining, const uint8_t *pointBase,
                                   size_t pointBaseSize, SlipView3DVec32 origin, SlipView3DVec32 *result) {
	uint32_t pointOffset;
	uint32_t sumX;
	uint32_t sumY;
	uint32_t sumZ;

	if (record == 0 || pointBase == 0 || result == 0 || recordBytesRemaining < SLIP_TRK_POINT_TRIPLE_BYTES) {
		return false;
	}
	pointOffset = SlipBytes_ReadLE16(record);
	if ((size_t)pointOffset + SLIP_TRK_POSITION_BYTES > pointBaseSize) {
		return false;
	}
	sumX = SlipBytes_ReadLE32(pointBase + pointOffset);
	sumZ = SlipBytes_ReadLE32(pointBase + pointOffset + SLIP_TRK_POSITION_Z_OFFSET);
	sumY = SlipBytes_ReadLE32(pointBase + pointOffset + SLIP_TRK_POSITION_Y_OFFSET);
	pointOffset = SlipBytes_ReadLE16(record + SLIP_TRK_POINT_TRIPLE_SECOND_OFFSET);
	if ((size_t)pointOffset + SLIP_TRK_POSITION_BYTES > pointBaseSize) {
		return false;
	}
	sumX += SlipBytes_ReadLE32(pointBase + pointOffset);
	sumY += SlipBytes_ReadLE32(pointBase + pointOffset + SLIP_TRK_POSITION_Y_OFFSET);
	sumZ += SlipBytes_ReadLE32(pointBase + pointOffset + SLIP_TRK_POSITION_Z_OFFSET);
	pointOffset = SlipBytes_ReadLE16(record + SLIP_TRK_POINT_TRIPLE_THIRD_OFFSET);
	if ((size_t)pointOffset + SLIP_TRK_POSITION_BYTES > pointBaseSize) {
		return false;
	}
	sumX += SlipBytes_ReadLE32(pointBase + pointOffset);
	sumY += SlipBytes_ReadLE32(pointBase + pointOffset + SLIP_TRK_POSITION_Y_OFFSET);
	sumZ += SlipBytes_ReadLE32(pointBase + pointOffset + SLIP_TRK_POSITION_Z_OFFSET);
	sumX += (uint32_t)origin.x;
	sumY += (uint32_t)origin.y;
	sumZ += (uint32_t)origin.z;
	*result = (SlipView3DVec32){(int32_t)sumX, (int32_t)sumY, (int32_t)sumZ};
	return true;
}

bool SlipTrackWorld_TransformPoint(SlipView3DVec32 input, const SlipView3DMatrix *viewMatrix, SlipView3DVec32 offset,
                                   SlipView3DVec32 *result) {
	uint32_t xFromX;
	uint32_t xFromY;
	uint32_t xFromZ;
	uint32_t yFromX;
	uint32_t yFromY;
	uint32_t yFromZ;
	uint32_t zFromX;
	uint32_t zFromY;
	uint32_t zFromZ;
	int16_t inputX;
	int16_t inputY;
	int16_t inputZ;
	uint32_t outputX;
	uint32_t outputY;
	uint32_t outputZ;

	if (viewMatrix == 0 || result == 0) {
		return false;
	}
	inputX = (int16_t)input.x;
	inputY = (int16_t)input.y;
	inputZ = (int16_t)input.z;
	xFromX = (uint32_t)((int32_t)inputX * viewMatrix->m[0]);
	xFromY = (uint32_t)((int32_t)inputY * viewMatrix->m[3]);
	xFromZ = (uint32_t)((int32_t)inputZ * viewMatrix->m[6]);
	yFromX = (uint32_t)((int32_t)inputX * viewMatrix->m[1]);
	yFromY = (uint32_t)((int32_t)inputY * viewMatrix->m[4]);
	yFromZ = (uint32_t)((int32_t)inputZ * viewMatrix->m[7]);
	zFromX = (uint32_t)((int32_t)inputX * viewMatrix->m[2]);
	zFromY = (uint32_t)((int32_t)inputY * viewMatrix->m[5]);
	zFromZ = (uint32_t)((int32_t)inputZ * viewMatrix->m[8]);
	outputX = xFromZ + xFromY + xFromX;
	outputY = yFromZ + yFromY + yFromX;
	outputZ = zFromZ + zFromY + zFromX;
	outputX = (outputX >> SLIP_TRACK_LOCAL_VERTEX_TRANSFORM_SHIFT) |
	          ((outputX & SLIP_TRACK_DWORD_SIGN_BIT) != 0 ? SLIP_TRACK_LOCAL_VERTEX_SIGN_EXTENSION : 0);
	outputY = (outputY >> SLIP_TRACK_LOCAL_VERTEX_TRANSFORM_SHIFT) |
	          ((outputY & SLIP_TRACK_DWORD_SIGN_BIT) != 0 ? SLIP_TRACK_LOCAL_VERTEX_SIGN_EXTENSION : 0);
	outputZ = (outputZ >> SLIP_TRACK_LOCAL_VERTEX_TRANSFORM_SHIFT) |
	          ((outputZ & SLIP_TRACK_DWORD_SIGN_BIT) != 0 ? SLIP_TRACK_LOCAL_VERTEX_SIGN_EXTENSION : 0);
	outputX += (uint32_t)offset.x;
	outputY += (uint32_t)offset.y;
	outputZ += (uint32_t)offset.z;
	*result = (SlipView3DVec32){(int32_t)outputX, (int32_t)outputY, (int32_t)outputZ};
	return true;
}

bool SlipTrackWorld_ChunkSetup(const uint8_t *currentChunk, size_t chunkBytesRemaining, const uint8_t *filter,
                               uint32_t chunkCounter, SlipTrackWorldChunkSetup *result) {
	uint32_t chunkOriginX;
	uint32_t chunkOriginY;
	uint32_t chunkOriginZ;

	if (currentChunk == 0 || result == 0) {
		return false;
	}
	*result = (SlipTrackWorldChunkSetup){false, chunkCounter, 0, {0, 0, 0}, 0};
	if (filter != 0 && filter != currentChunk) {
		result->skippedByFilter = true;
		return true;
	}
	if (chunkBytesRemaining < SLIP_TRK_CHUNK_ORIGIN_END) {
		return false;
	}
	result->chunkCounter = chunkCounter + 1u;
	result->currentChunk = currentChunk;
	chunkOriginX = SlipBytes_ReadLE32(currentChunk + SLIP_TRK_CHUNK_ORIGIN_X_OFFSET);
	chunkOriginY = SlipBytes_ReadLE32(currentChunk + SLIP_TRK_CHUNK_ORIGIN_Y_OFFSET);
	chunkOriginZ = SlipBytes_ReadLE32(currentChunk + SLIP_TRK_CHUNK_ORIGIN_Z_OFFSET);
	result->currentChunkOrigin = (SlipView3DVec32){(int32_t)chunkOriginX, (int32_t)chunkOriginY, (int32_t)chunkOriginZ};
	result->savedChunkPointer = currentChunk;
	return true;
}

bool SlipTrackWorld_StoreRecordCacheResult(uint8_t *record, size_t recordBytesRemaining, uint8_t classificationMask) {
	if (record == 0 || recordBytesRemaining < SLIP_TRK_RECORD_CACHE_END) {
		return false;
	}
	record[SLIP_TRK_RECORD_CLASSIFICATION_OFFSET] = classificationMask;
	record[SLIP_TRK_RECORD_CACHE_VALID_OFFSET] = UINT8_MAX;
	return true;
}

bool SlipTrackWorld_ClassifyChildRecord(const uint8_t *childRecord, size_t childRecordBytesRemaining,
                                        const uint8_t *pointBase, size_t pointBaseSize, SlipView3DVec32 origin,
                                        int32_t minZ, int32_t maxZ, SlipTrackWorldProjectMask projectMask,
                                        void *userData, SlipTrackWorldClassifyChild *result) {
	SlipView3DVec32 point;
	uint32_t projectedMask;
	uint32_t classificationMask;

	if (childRecord == 0 || pointBase == 0 || projectMask == 0 || result == 0) {
		return false;
	}
	*result = (SlipTrackWorldClassifyChild){childRecord, true, {0, 0, 0}, false, 0, 0, 0};
	if (!SlipTrackWorld_SumThreePoints(childRecord, childRecordBytesRemaining, pointBase, pointBaseSize, origin,
	                                   &point)) {
		return false;
	}
	projectedMask = projectMask(point, userData);
	classificationMask = SlipTrackWorld_ClassifyPoint(point, projectedMask, minZ, maxZ);
	result->point = point;
	result->callTrackWorldClassifyPoint = true;
	result->projectedMask = projectedMask;
	result->classificationMask = classificationMask;
	result->classificationByte = (uint8_t)classificationMask;
	return true;
}

bool SlipTrackWorld_ScanClassificationMask(uint8_t combinedMask, uint8_t classificationMask, uintptr_t scanPosition,
                                           uint32_t remainingRecordCount, SlipTrackWorldDlTest *result) {
	uint8_t currentCombinedMask;
	uint32_t currentRemainingRecordCount;

	if (result == 0) {
		return false;
	}
	*result =
	    (SlipTrackWorldDlTest){combinedMask, scanPosition, remainingRecordCount, SLIP_TRACK_WORLD_RECORD_MASK_CLEAR};
	if (classificationMask == 0) {
		return true;
	}
	currentCombinedMask = (uint8_t)(combinedMask & classificationMask);
	scanPosition += 2u;
	currentRemainingRecordCount = remainingRecordCount - 1u;
	result->combinedMask = currentCombinedMask;
	result->scanPosition = scanPosition;
	result->remainingRecordCount = currentRemainingRecordCount;
	if (currentRemainingRecordCount != 0) {
		result->branch = SLIP_TRACK_WORLD_RECORD_SCAN_CONTINUE;
		return true;
	}
	if (currentCombinedMask != 0) {
		result->branch = SLIP_TRACK_WORLD_RECORD_MASK_REJECT;
	}
	return true;
}

bool SlipTrackWorld_RecordScan(uint8_t *trkBase, size_t trkSize, uint32_t recordOffset,
                               const uint8_t classificationByVisit[SLIP_TRACK_WORLD_RECORD_SCAN_VISITS],
                               SlipTrackWorldRecordScan *result) {
	uintptr_t scanOffset;
	uint8_t combinedMask;
	uint32_t remainingChildCount;
	uint16_t visitIndex;

	if (trkBase == 0 || result == 0 || (size_t)recordOffset + SLIP_TRK_RECORD_CHILD_LIST_END > trkSize) {
		return false;
	}
	scanOffset = (uintptr_t)recordOffset + SLIP_TRK_RECORD_CHILD_LIST_OFFSET;
	combinedMask = UINT8_MAX;
	remainingChildCount = SLIP_TRK_RECORD_CHILD_COUNT;
	*result = (SlipTrackWorldRecordScan){recordOffset,
	                                     (uint32_t)scanOffset,
	                                     combinedMask,
	                                     remainingChildCount,
	                                     0,
	                                     {{0}},
	                                     SLIP_TRACK_WORLD_RECORD_MASK_CLEAR,
	                                     combinedMask,
	                                     scanOffset,
	                                     remainingChildCount,
	                                     false};
	for (visitIndex = 0; visitIndex < SLIP_TRACK_WORLD_RECORD_SCAN_VISITS; ++visitIndex) {
		uint16_t childRecordOffset;
		uint32_t childRecordPointer;
		uint8_t classificationByte;
		SlipTrackWorldRecordScanVisit *const visit = &result->visits[visitIndex];

		childRecordOffset = SlipBytes_ReadLE16(trkBase + scanOffset);
		childRecordPointer = (uint32_t)childRecordOffset;
		if ((size_t)childRecordPointer + SLIP_TRK_RECORD_CACHE_END > trkSize) {
			return false;
		}
		*visit = (SlipTrackWorldRecordScanVisit){scanOffset,
		                                         childRecordOffset,
		                                         childRecordPointer,
		                                         trkBase[childRecordPointer + SLIP_TRK_RECORD_CACHE_VALID_OFFSET],
		                                         false,
		                                         false,
		                                         0,
		                                         {0, false, {0, 0, 0}, false, 0, 0, 0},
		                                         false,
		                                         0,
		                                         {0, 0, 0, SLIP_TRACK_WORLD_RECORD_MASK_CLEAR}};
		if (visit->cacheValidByte == 0) {
			if (classificationByVisit == 0) {
				return false;
			}
			classificationByte = classificationByVisit[visitIndex];
			visit->callTrackWorldSumThreePoints = true;
			visit->callTrackWorldClassifyPoint = true;
			visit->classificationByte = classificationByte;
			if (!SlipTrackWorld_StoreRecordCacheResult(trkBase + childRecordPointer,
			                                           trkSize - (size_t)childRecordPointer, classificationByte)) {
				return false;
			}
			visit->callTrackWorldStoreRecordCacheResult = true;
		} else {
			classificationByte = trkBase[childRecordPointer + SLIP_TRK_RECORD_CLASSIFICATION_OFFSET];
			visit->cachedClassificationByte = classificationByte;
		}
		if (!SlipTrackWorld_ScanClassificationMask(combinedMask, classificationByte, scanOffset, remainingChildCount,
		                                           &visit->dlTest)) {
			return false;
		}
		result->visitCount = (uint16_t)(visitIndex + 1u);
		combinedMask = visit->dlTest.combinedMask;
		scanOffset = visit->dlTest.scanPosition;
		remainingChildCount = visit->dlTest.remainingRecordCount;
		result->branch = visit->dlTest.branch;
		result->combinedMask = combinedMask;
		result->scanPosition = scanOffset;
		result->remainingChildCount = remainingChildCount;
		result->ret = visit->dlTest.branch == SLIP_TRACK_WORLD_RECORD_MASK_REJECT;
		if (visit->dlTest.branch != SLIP_TRACK_WORLD_RECORD_SCAN_CONTINUE) {
			return true;
		}
	}
	return true;
}

bool SlipTrackWorld_RecordScanClassified(uint8_t *trkBase, size_t trkSize, uint32_t recordOffset,
                                         const uint8_t *pointBase, size_t pointBaseSize, SlipView3DVec32 origin,
                                         int32_t minZ, int32_t maxZ, SlipTrackWorldProjectMask projectMask,
                                         void *userData, SlipTrackWorldRecordScan *result) {
	uintptr_t scanOffset;
	uint8_t combinedMask;
	uint32_t remainingChildCount;
	uint16_t visitIndex;

	if (trkBase == 0 || result == 0 || (size_t)recordOffset + SLIP_TRK_RECORD_CHILD_LIST_END > trkSize) {
		return false;
	}
	scanOffset = (uintptr_t)recordOffset + SLIP_TRK_RECORD_CHILD_LIST_OFFSET;
	combinedMask = UINT8_MAX;
	remainingChildCount = SLIP_TRK_RECORD_CHILD_COUNT;
	*result = (SlipTrackWorldRecordScan){recordOffset,
	                                     (uint32_t)scanOffset,
	                                     combinedMask,
	                                     remainingChildCount,
	                                     0,
	                                     {{0}},
	                                     SLIP_TRACK_WORLD_RECORD_MASK_CLEAR,
	                                     combinedMask,
	                                     scanOffset,
	                                     remainingChildCount,
	                                     false};
	for (visitIndex = 0; visitIndex < SLIP_TRACK_WORLD_RECORD_SCAN_VISITS; ++visitIndex) {
		uint16_t childRecordOffset;
		uint32_t childRecordPointer;
		uint8_t classificationByte;
		SlipTrackWorldRecordScanVisit *const visit = &result->visits[visitIndex];

		childRecordOffset = SlipBytes_ReadLE16(trkBase + scanOffset);
		childRecordPointer = (uint32_t)childRecordOffset;
		if ((size_t)childRecordPointer + SLIP_TRK_RECORD_CACHE_END > trkSize) {
			return false;
		}
		*visit = (SlipTrackWorldRecordScanVisit){scanOffset,
		                                         childRecordOffset,
		                                         childRecordPointer,
		                                         trkBase[childRecordPointer + SLIP_TRK_RECORD_CACHE_VALID_OFFSET],
		                                         false,
		                                         false,
		                                         0,
		                                         {0, false, {0, 0, 0}, false, 0, 0, 0},
		                                         false,
		                                         0,
		                                         {0, 0, 0, SLIP_TRACK_WORLD_RECORD_MASK_CLEAR}};
		if (visit->cacheValidByte == 0) {
			SlipTrackWorldClassifyChild classifyChild;

			if (!SlipTrackWorld_ClassifyChildRecord(trkBase + childRecordPointer, trkSize - (size_t)childRecordPointer,
			                                        pointBase, pointBaseSize, origin, minZ, maxZ, projectMask, userData,
			                                        &classifyChild)) {
				return false;
			}
			classificationByte = classifyChild.classificationByte;
			visit->callTrackWorldSumThreePoints = true;
			visit->callTrackWorldClassifyPoint = true;
			visit->classificationByte = classificationByte;
			visit->classifyChild = classifyChild;
			if (!SlipTrackWorld_StoreRecordCacheResult(trkBase + childRecordPointer,
			                                           trkSize - (size_t)childRecordPointer, classificationByte)) {
				return false;
			}
			visit->callTrackWorldStoreRecordCacheResult = true;
		} else {
			classificationByte = trkBase[childRecordPointer + SLIP_TRK_RECORD_CLASSIFICATION_OFFSET];
			visit->cachedClassificationByte = classificationByte;
		}
		if (!SlipTrackWorld_ScanClassificationMask(combinedMask, classificationByte, scanOffset, remainingChildCount,
		                                           &visit->dlTest)) {
			return false;
		}
		result->visitCount = (uint16_t)(visitIndex + 1u);
		combinedMask = visit->dlTest.combinedMask;
		scanOffset = visit->dlTest.scanPosition;
		remainingChildCount = visit->dlTest.remainingRecordCount;
		result->branch = visit->dlTest.branch;
		result->combinedMask = combinedMask;
		result->scanPosition = scanOffset;
		result->remainingChildCount = remainingChildCount;
		result->ret = visit->dlTest.branch == SLIP_TRACK_WORLD_RECORD_MASK_REJECT;
		if (visit->dlTest.branch != SLIP_TRACK_WORLD_RECORD_SCAN_CONTINUE) {
			return true;
		}
	}
	return true;
}

bool SlipTrackWorld_NodeBranch(const uint8_t *record, size_t recordBytesRemaining, const uint8_t *tableBase,
                               size_t tableSize, SlipTrackWorldNodeBranch *result) {
	uint16_t nodeOffset;
	SlipTrackWorldNodeTest nodeTest;

	if (record == 0 || result == 0 || recordBytesRemaining < 2u) {
		return false;
	}
	nodeOffset = SlipBytes_ReadLE16(record);
	*result = (SlipTrackWorldNodeBranch){nodeOffset, {0, 0, false}, SLIP_TRACK_WORLD_NODE_POSITIVE};
	if (nodeOffset == UINT16_MAX) {
		result->branch = SLIP_TRACK_WORLD_NODE_SENTINEL;
		return true;
	}
	if (!SlipTrackWorld_NodeTest(tableBase, tableSize, nodeOffset, &nodeTest)) {
		return false;
	}
	result->nodeTest = nodeTest;
	if (nodeTest.carryFromSar) {
		result->branch = SLIP_TRACK_WORLD_NODE_NEGATIVE;
	}
	return true;
}

bool SlipTrackWorld_DispatchPositiveNodeChild(const uint8_t *record, size_t recordBytesRemaining, uint32_t trkBase,
                                              SlipTrackWorldPositiveNodeChildDispatch *result) {
	uint32_t childOffset;
	uint32_t childPointer;

	if (record == 0 || result == 0 || recordBytesRemaining < SLIP_TRK_NODE_CHILDREN_END) {
		return false;
	}
	*result = (SlipTrackWorldPositiveNodeChildDispatch){0, false, 0, 0, false, 0};
	childOffset = (uint32_t)SlipBytes_ReadLE16(record + SLIP_TRK_NODE_FIRST_CHILD_OFFSET);
	result->firstOffset = (uint16_t)childOffset;
	childPointer = childOffset + trkBase;
	if (childPointer != 0) {
		result->firstCall = true;
		result->firstChildPointer = childPointer;
	}
	childOffset = (uint32_t)SlipBytes_ReadLE16(record + SLIP_TRK_NODE_SECOND_CHILD_OFFSET);
	result->secondOffset = (uint16_t)childOffset;
	if (childOffset != 0) {
		childPointer = childOffset + trkBase;
		result->secondCall = true;
		result->secondChildPointer = childPointer;
	}
	return true;
}

bool SlipTrackWorld_DispatchNegativeNodeChild(const uint8_t *record, size_t recordBytesRemaining, uint32_t trkBase,
                                              SlipTrackWorldNegativeNodeChildDispatch *result) {
	uint32_t childOffset;
	uint32_t childPointer;

	if (record == 0 || result == 0 || recordBytesRemaining < SLIP_TRK_NODE_CHILDREN_END) {
		return false;
	}
	*result = (SlipTrackWorldNegativeNodeChildDispatch){0, false, 0, 0, false, 0};
	childOffset = (uint32_t)SlipBytes_ReadLE16(record + SLIP_TRK_NODE_SECOND_CHILD_OFFSET);
	result->firstOffset = (uint16_t)childOffset;
	if (childOffset != 0) {
		childPointer = childOffset + trkBase;
		result->firstCall = true;
		result->firstChildPointer = childPointer;
	}
	childOffset = (uint32_t)SlipBytes_ReadLE16(record + SLIP_TRK_NODE_FIRST_CHILD_OFFSET);
	result->secondOffset = (uint16_t)childOffset;
	if (childOffset != 0) {
		childPointer = childOffset + trkBase;
		result->secondCall = true;
		result->secondChildPointer = childPointer;
	}
	return true;
}

static uint32_t SlipTrackWorld_ChildRecordOffset(uint32_t childPointer, uint32_t trkBasePointer) {
	return childPointer - trkBasePointer;
}

bool SlipTrackWorld_RecordChunk(const uint8_t *record, size_t recordBytesRemaining, const uint8_t *trkBase,
                                size_t trkSize, const uint8_t *pointBase, size_t pointBaseSize, SlipView3DVec32 origin,
                                const uint8_t *chunkBase, size_t chunkBaseSize, const uint8_t *filter,
                                uint32_t chunkCounter, SlipTrackWorldChunkProcessExecution *chunkProcessExecution,
                                SlipTrackWorldRecordChunk *result) {
	uint32_t pointRecordOffset;
	uint32_t chunkOffset;
	SlipView3DVec32 summedPointOffset;
	SlipTrackWorldChunkProcess chunkProcess;
	SlipTrackWorldChunkProcessVertexCache chunkProcessVertexCache;

	if (record == 0 || trkBase == 0 || chunkBase == 0 || result == 0 ||
	    recordBytesRemaining < SLIP_TRK_CHUNK_REFERENCES_END) {
		return false;
	}
	*result = (SlipTrackWorldRecordChunk){.chunkSetup = {false, chunkCounter, 0, {0, 0, 0}, 0}};
	pointRecordOffset = (uint32_t)SlipBytes_ReadLE16(record + SLIP_TRK_CHUNK_POINT_TRIPLE_OFFSET);
	result->pointRecordOffset = (uint16_t)pointRecordOffset;
	if ((size_t)pointRecordOffset + SLIP_TRK_POINT_TRIPLE_BYTES > trkSize) {
		return false;
	}
	result->pointRecord = trkBase + pointRecordOffset;
	if (!SlipTrackWorld_SumThreePoints(result->pointRecord, trkSize - (size_t)pointRecordOffset, pointBase,
	                                   pointBaseSize, origin, &summedPointOffset)) {
		return false;
	}
	result->offset = summedPointOffset;
	if (chunkProcessExecution != 0 && chunkProcessExecution->offset != 0) {
		*chunkProcessExecution->offset = summedPointOffset;
	}
	chunkOffset = (uint32_t)SlipBytes_ReadLE16(record + SLIP_TRK_CHUNK_REFERENCE_OFFSET);
	result->chunkOffset = (uint16_t)chunkOffset;
	if ((size_t)chunkOffset > chunkBaseSize) {
		return false;
	}
	result->chunkPointer = chunkBase + chunkOffset;
	result->chunkPointerForSetup = result->chunkPointer;
	result->callSetupChunk = true;
	if (chunkProcessExecution != 0 && chunkProcessExecution->vertexRecords != 0) {
		if (!SlipTrackWorld_ChunkProcessVertexCache(
		        result->chunkPointerForSetup, chunkBaseSize - (size_t)chunkOffset, chunkBase, chunkBaseSize, filter,
		        chunkCounter, chunkProcessExecution->vertexRecords, chunkProcessExecution->vertexRecordCapacity,
		        chunkProcessExecution->buildVertexRecords, chunkProcessExecution->buildVertexRecordsUserData,
		        chunkProcessExecution->restoreVertexBuffer, chunkProcessExecution->restoreVertexBufferUserData,
		        chunkProcessExecution->classify, chunkProcessExecution->callback, chunkProcessExecution->rawBspUserData,
		        chunkProcessExecution->rawBspDepthCapacity, chunkProcessExecution->currentChunkOrigin,
		        chunkProcessExecution->chunkFallback, chunkProcessExecution->chunkFallbackUserData,
		        &chunkProcessVertexCache)) {
			return false;
		}
		result->chunkSetup = chunkProcessVertexCache.chunkSetup;
		result->chunkPointListCall = chunkProcessVertexCache.callPrepareChunkPointList;
		result->chunkBspProcessCall = chunkProcessVertexCache.callProcessChunkBsp;
		result->chunkVertexBuildCall = chunkProcessVertexCache.callBuildVertexRecords;
		result->chunkBspTraversalCall = chunkProcessVertexCache.callTraverseRawBsp;
		result->chunkProcessRawBspNodeCount = chunkProcessVertexCache.rawBspNodeCount;
		result->chunkProcessRawBspClassifyCount = chunkProcessVertexCache.rawBspClassifyCount;
		result->chunkProcessRawBspCallbackCount = chunkProcessVertexCache.rawBspCallbackCount;
		result->chunkProcessRet = chunkProcessVertexCache.ret;
	} else {
		if (!SlipTrackWorld_ChunkProcess(result->chunkPointerForSetup, chunkBaseSize - (size_t)chunkOffset, chunkBase,
		                                 chunkBaseSize, filter, chunkCounter, &chunkProcess)) {
			return false;
		}
		result->chunkSetup = chunkProcess.chunkSetup;
		result->chunkPointListCall = chunkProcess.callPrepareChunkPointList;
		result->chunkBspProcessCall = chunkProcess.callProcessChunkBsp;
		result->chunkProcessRet = chunkProcess.ret;
	}
	return true;
}

bool SlipTrackWorld_NodeDispatch(uint8_t *trkBase, size_t trkSize, uint32_t recordOffset, uint32_t trkBasePointer,
                                 const uint8_t *tableBase, size_t tableSize, const uint8_t *pointBase,
                                 size_t pointBaseSize, SlipView3DVec32 origin, const uint8_t *chunkBase,
                                 size_t chunkBaseSize, const uint8_t *filter, uint32_t chunkCounter,
                                 SlipTrackWorldChunkProcessExecution *chunkProcessExecution,
                                 uint32_t drawStateIndexBeforeChunk, SlipTrackWorldNodeDispatch *result) {
	const uint8_t *record;
	size_t recordBytesRemaining;
	SlipTrackWorldNodeBranch nodeBranch;

	if (trkBase == 0 || result == 0 || (size_t)recordOffset + sizeof(uint16_t) > trkSize) {
		return false;
	}
	record = trkBase + recordOffset;
	recordBytesRemaining = trkSize - (size_t)recordOffset;
	if (!SlipTrackWorld_NodeBranch(record, recordBytesRemaining, tableBase, tableSize, &nodeBranch)) {
		return false;
	}
	*result = (SlipTrackWorldNodeDispatch){0};
	result->recordOffset = recordOffset;
	result->nodeBranch = nodeBranch;
	result->ret = true;
	if (nodeBranch.branch == SLIP_TRACK_WORLD_NODE_POSITIVE) {
		result->callDispatchPositiveChildren = true;
		if (!SlipTrackWorld_DispatchPositiveNodeChild(record, recordBytesRemaining, trkBasePointer,
		                                              &result->positiveChildDispatch)) {
			return false;
		}
		result->returnedAfterPositiveChildren = true;
		result->branch = SLIP_TRACK_WORLD_NODE_DISPATCH_BRANCH_POSITIVE_CHILD;
		return true;
	}
	if (nodeBranch.branch == SLIP_TRACK_WORLD_NODE_NEGATIVE) {
		result->callDispatchNegativeChildren = true;
		if (!SlipTrackWorld_DispatchNegativeNodeChild(record, recordBytesRemaining, trkBasePointer,
		                                              &result->negativeChildDispatch)) {
			return false;
		}
		result->returnedAfterNegativeChildren = true;
		result->branch = SLIP_TRACK_WORLD_NODE_DISPATCH_BRANCH_NEGATIVE_CHILD;
		return true;
	}
	result->callGetDrawStateBeforeChunk = true;
	result->drawStateIndexBeforeChunk = drawStateIndexBeforeChunk;
	result->drawStateIndexForChunk = drawStateIndexBeforeChunk + 1u;
	result->callLoadDrawStateForChunk = true;
	if (chunkProcessExecution != NULL && chunkProcessExecution->drawStateLoad != NULL &&
	    !chunkProcessExecution->drawStateLoad(result->drawStateIndexForChunk,
	                                          chunkProcessExecution->drawStateLoadUserData)) {
		return false;
	}
	result->callProcessRecordChunk = true;
	if (!SlipTrackWorld_RecordChunk(record, recordBytesRemaining, trkBase, trkSize, pointBase, pointBaseSize, origin,
	                                chunkBase, chunkBaseSize, filter, chunkCounter, chunkProcessExecution,
	                                &result->recordChunk)) {
		return false;
	}
	result->callGetDrawStateAfterChunk = true;
	result->drawStateIndexAfterChunk = drawStateIndexBeforeChunk + 1u;
	result->restoredDrawStateIndex = drawStateIndexBeforeChunk;
	result->callRestoreDrawStateAfterChunk = true;
	if (chunkProcessExecution != NULL && chunkProcessExecution->drawStateLoad != NULL &&
	    !chunkProcessExecution->drawStateLoad(result->restoredDrawStateIndex,
	                                          chunkProcessExecution->drawStateLoadUserData)) {
		return false;
	}
	result->branch = SLIP_TRACK_WORLD_NODE_DISPATCH_BRANCH_CHUNK;
	return true;
}

bool SlipTrackWorld_RecordStep(uint8_t *trkBase, size_t trkSize, uint32_t recordOffset, uint32_t trkBasePointer,
                               const uint8_t classificationByVisit[SLIP_TRACK_WORLD_RECORD_SCAN_VISITS],
                               const uint8_t *tableBase, size_t tableSize, const uint8_t *pointBase,
                               size_t pointBaseSize, SlipView3DVec32 origin, const uint8_t *chunkBase,
                               size_t chunkBaseSize, const uint8_t *filter, uint32_t chunkCounter,
                               SlipTrackWorldChunkProcessExecution *chunkProcessExecution,
                               uint32_t drawStateIndexBeforeChunk, SlipTrackWorldRecordStep *result) {
	SlipTrackWorldRecordScan recordScan;

	if (result == 0) {
		return false;
	}
	if (!SlipTrackWorld_RecordScan(trkBase, trkSize, recordOffset, classificationByVisit, &recordScan)) {
		return false;
	}
	*result = (SlipTrackWorldRecordStep){0};
	result->recordOffset = recordOffset;
	result->callRecordScan = true;
	result->recordScan = recordScan;
	result->ret = true;
	if (recordScan.branch == SLIP_TRACK_WORLD_RECORD_MASK_REJECT) {
		result->branch = SLIP_TRACK_WORLD_RECORD_STEP_BRANCH_REJECTED;
		return true;
	}
	if (recordScan.branch != SLIP_TRACK_WORLD_RECORD_MASK_CLEAR) {
		return false;
	}
	result->callNodeDispatch = true;
	if (!SlipTrackWorld_NodeDispatch(trkBase, trkSize, recordOffset, trkBasePointer, tableBase, tableSize, pointBase,
	                                 pointBaseSize, origin, chunkBase, chunkBaseSize, filter, chunkCounter,
	                                 chunkProcessExecution, drawStateIndexBeforeChunk, &result->nodeDispatch)) {
		return false;
	}
	result->branch = SLIP_TRACK_WORLD_RECORD_STEP_BRANCH_DISPATCHED;
	return true;
}

bool SlipTrackWorld_RecordStepClassified(uint8_t *trkBase, size_t trkSize, uint32_t recordOffset,
                                         uint32_t trkBasePointer, const uint8_t *tableBase, size_t tableSize,
                                         const uint8_t *pointBase, size_t pointBaseSize, SlipView3DVec32 origin,
                                         int32_t minZ, int32_t maxZ, SlipTrackWorldProjectMask projectMask,
                                         void *userData, const uint8_t *chunkBase, size_t chunkBaseSize,
                                         const uint8_t *filter, uint32_t chunkCounter,
                                         SlipTrackWorldChunkProcessExecution *chunkProcessExecution,
                                         uint32_t drawStateIndexBeforeChunk, SlipTrackWorldRecordStep *result) {
	SlipTrackWorldRecordScan recordScan;

	if (result == 0) {
		return false;
	}
	if (!SlipTrackWorld_RecordScanClassified(trkBase, trkSize, recordOffset, pointBase, pointBaseSize, origin, minZ,
	                                         maxZ, projectMask, userData, &recordScan)) {
		return false;
	}
	*result = (SlipTrackWorldRecordStep){0};
	result->recordOffset = recordOffset;
	result->callRecordScan = true;
	result->recordScan = recordScan;
	result->ret = true;
	if (recordScan.branch == SLIP_TRACK_WORLD_RECORD_MASK_REJECT) {
		result->branch = SLIP_TRACK_WORLD_RECORD_STEP_BRANCH_REJECTED;
		return true;
	}
	if (recordScan.branch != SLIP_TRACK_WORLD_RECORD_MASK_CLEAR) {
		return false;
	}
	result->callNodeDispatch = true;
	if (!SlipTrackWorld_NodeDispatch(trkBase, trkSize, recordOffset, trkBasePointer, tableBase, tableSize, pointBase,
	                                 pointBaseSize, origin, chunkBase, chunkBaseSize, filter, chunkCounter,
	                                 chunkProcessExecution, drawStateIndexBeforeChunk, &result->nodeDispatch)) {
		return false;
	}
	result->branch = SLIP_TRACK_WORLD_RECORD_STEP_BRANCH_DISPATCHED;
	return true;
}

bool SlipTrackWorld_Traverse(uint8_t *trkBase, size_t trkSize, uint32_t rootRecordOffset, uint32_t trkBasePointer,
                             const uint8_t classificationByVisit[SLIP_TRACK_WORLD_RECORD_SCAN_VISITS],
                             const uint8_t *tableBase, size_t tableSize, const uint8_t *pointBase, size_t pointBaseSize,
                             SlipView3DVec32 origin, const uint8_t *chunkBase, size_t chunkBaseSize,
                             const uint8_t *filter, uint32_t chunkCounter,
                             SlipTrackWorldChunkProcessExecution *chunkProcessExecution,
                             uint32_t drawStateIndexBeforeChunk, SlipTrackWorldTraversalVisit *visits,
                             uint16_t visitCapacity, uint32_t *stackOffsets, uint16_t *stackDepths,
                             uint16_t stackCapacity, SlipTrackWorldTraversal *result) {
	uint16_t stackCount;

	if (visits == 0 || stackOffsets == 0 || stackDepths == 0 || result == 0 || visitCapacity == 0 ||
	    stackCapacity == 0) {
		return false;
	}
	*result = (SlipTrackWorldTraversal){rootRecordOffset, 0, 0, false, false, chunkCounter, true};
	stackOffsets[0] = rootRecordOffset;
	stackDepths[0] = 0;
	stackCount = 1u;
	while (stackCount != 0) {
		uint32_t recordOffset;
		uint16_t depth;
		SlipTrackWorldRecordStep step;
		SlipTrackWorldTraversalVisit *visit;

		--stackCount;
		recordOffset = stackOffsets[stackCount];
		depth = stackDepths[stackCount];
		if (result->visitCount >= visitCapacity) {
			result->hitVisitCapacity = true;
			return false;
		}
		if (!SlipTrackWorld_RecordStep(trkBase, trkSize, recordOffset, trkBasePointer, classificationByVisit, tableBase,
		                               tableSize, pointBase, pointBaseSize, origin, chunkBase, chunkBaseSize, filter,
		                               chunkCounter, chunkProcessExecution, drawStateIndexBeforeChunk, &step)) {
			return false;
		}
		visit = &visits[result->visitCount];
		*visit = (SlipTrackWorldTraversalVisit){depth, recordOffset, step};
		++result->visitCount;
		if (depth > result->maxDepth) {
			result->maxDepth = depth;
		}
		if (step.callNodeDispatch) {
			const SlipTrackWorldNodeDispatch *const node = &step.nodeDispatch;

			if (node->branch == SLIP_TRACK_WORLD_NODE_DISPATCH_BRANCH_POSITIVE_CHILD) {
				if (node->positiveChildDispatch.secondCall) {
					if (stackCount >= stackCapacity) {
						result->hitStackCapacity = true;
						return false;
					}
					stackOffsets[stackCount] = SlipTrackWorld_ChildRecordOffset(
					    node->positiveChildDispatch.secondChildPointer, trkBasePointer);
					stackDepths[stackCount] = (uint16_t)(depth + 1u);
					++stackCount;
				}
				if (node->positiveChildDispatch.firstCall) {
					if (stackCount >= stackCapacity) {
						result->hitStackCapacity = true;
						return false;
					}
					stackOffsets[stackCount] =
					    SlipTrackWorld_ChildRecordOffset(node->positiveChildDispatch.firstChildPointer, trkBasePointer);
					stackDepths[stackCount] = (uint16_t)(depth + 1u);
					++stackCount;
				}
			} else if (node->branch == SLIP_TRACK_WORLD_NODE_DISPATCH_BRANCH_NEGATIVE_CHILD) {
				if (node->negativeChildDispatch.secondCall) {
					if (stackCount >= stackCapacity) {
						result->hitStackCapacity = true;
						return false;
					}
					stackOffsets[stackCount] = SlipTrackWorld_ChildRecordOffset(
					    node->negativeChildDispatch.secondChildPointer, trkBasePointer);
					stackDepths[stackCount] = (uint16_t)(depth + 1u);
					++stackCount;
				}
				if (node->negativeChildDispatch.firstCall) {
					if (stackCount >= stackCapacity) {
						result->hitStackCapacity = true;
						return false;
					}
					stackOffsets[stackCount] =
					    SlipTrackWorld_ChildRecordOffset(node->negativeChildDispatch.firstChildPointer, trkBasePointer);
					stackDepths[stackCount] = (uint16_t)(depth + 1u);
					++stackCount;
				}
			} else if (node->branch == SLIP_TRACK_WORLD_NODE_DISPATCH_BRANCH_CHUNK) {
				chunkCounter = node->recordChunk.chunkSetup.chunkCounter;
				result->finalChunkCounter = chunkCounter;
			}
		}
	}
	result->finalChunkCounter = chunkCounter;
	return true;
}

bool SlipTrackWorld_TraverseClassified(
    uint8_t *trkBase, size_t trkSize, uint32_t rootRecordOffset, uint32_t trkBasePointer, const uint8_t *tableBase,
    size_t tableSize, const uint8_t *pointBase, size_t pointBaseSize, SlipView3DVec32 origin, int32_t minZ,
    int32_t maxZ, SlipTrackWorldProjectMask projectMask, void *userData, const uint8_t *chunkBase, size_t chunkBaseSize,
    const uint8_t *filter, uint32_t chunkCounter, SlipTrackWorldChunkProcessExecution *chunkProcessExecution,
    uint32_t drawStateIndexBeforeChunk, SlipTrackWorldTraversalVisit *visits, uint16_t visitCapacity,
    uint32_t *stackOffsets, uint16_t *stackDepths, uint16_t stackCapacity, SlipTrackWorldTraversal *result) {
	uint16_t stackCount;

	if (visits == 0 || stackOffsets == 0 || stackDepths == 0 || result == 0 || visitCapacity == 0 ||
	    stackCapacity == 0) {
		return false;
	}
	*result = (SlipTrackWorldTraversal){rootRecordOffset, 0, 0, false, false, chunkCounter, true};
	stackOffsets[0] = rootRecordOffset;
	stackDepths[0] = 0;
	stackCount = 1u;
	while (stackCount != 0) {
		uint32_t recordOffset;
		uint16_t depth;
		SlipTrackWorldRecordStep step;
		SlipTrackWorldTraversalVisit *visit;

		--stackCount;
		recordOffset = stackOffsets[stackCount];
		depth = stackDepths[stackCount];
		if (result->visitCount >= visitCapacity) {
			result->hitVisitCapacity = true;
			return false;
		}
		if (!SlipTrackWorld_RecordStepClassified(trkBase, trkSize, recordOffset, trkBasePointer, tableBase, tableSize,
		                                         pointBase, pointBaseSize, origin, minZ, maxZ, projectMask, userData,
		                                         chunkBase, chunkBaseSize, filter, chunkCounter, chunkProcessExecution,
		                                         drawStateIndexBeforeChunk, &step)) {
			return false;
		}
		visit = &visits[result->visitCount];
		*visit = (SlipTrackWorldTraversalVisit){depth, recordOffset, step};
		++result->visitCount;
		if (depth > result->maxDepth) {
			result->maxDepth = depth;
		}
		if (step.callNodeDispatch) {
			const SlipTrackWorldNodeDispatch *const node = &step.nodeDispatch;

			if (node->branch == SLIP_TRACK_WORLD_NODE_DISPATCH_BRANCH_POSITIVE_CHILD) {
				if (node->positiveChildDispatch.secondCall) {
					if (stackCount >= stackCapacity) {
						result->hitStackCapacity = true;
						return false;
					}
					stackOffsets[stackCount] = SlipTrackWorld_ChildRecordOffset(
					    node->positiveChildDispatch.secondChildPointer, trkBasePointer);
					stackDepths[stackCount] = (uint16_t)(depth + 1u);
					++stackCount;
				}
				if (node->positiveChildDispatch.firstCall) {
					if (stackCount >= stackCapacity) {
						result->hitStackCapacity = true;
						return false;
					}
					stackOffsets[stackCount] =
					    SlipTrackWorld_ChildRecordOffset(node->positiveChildDispatch.firstChildPointer, trkBasePointer);
					stackDepths[stackCount] = (uint16_t)(depth + 1u);
					++stackCount;
				}
			} else if (node->branch == SLIP_TRACK_WORLD_NODE_DISPATCH_BRANCH_NEGATIVE_CHILD) {
				if (node->negativeChildDispatch.secondCall) {
					if (stackCount >= stackCapacity) {
						result->hitStackCapacity = true;
						return false;
					}
					stackOffsets[stackCount] = SlipTrackWorld_ChildRecordOffset(
					    node->negativeChildDispatch.secondChildPointer, trkBasePointer);
					stackDepths[stackCount] = (uint16_t)(depth + 1u);
					++stackCount;
				}
				if (node->negativeChildDispatch.firstCall) {
					if (stackCount >= stackCapacity) {
						result->hitStackCapacity = true;
						return false;
					}
					stackOffsets[stackCount] =
					    SlipTrackWorld_ChildRecordOffset(node->negativeChildDispatch.firstChildPointer, trkBasePointer);
					stackDepths[stackCount] = (uint16_t)(depth + 1u);
					++stackCount;
				}
			} else if (node->branch == SLIP_TRACK_WORLD_NODE_DISPATCH_BRANCH_CHUNK) {
				chunkCounter = node->recordChunk.chunkSetup.chunkCounter;
				result->finalChunkCounter = chunkCounter;
			}
		}
	}
	result->finalChunkCounter = chunkCounter;
	return true;
}

bool SlipTrackWorld_ChunkPointList(const uint8_t *chunk, size_t chunkBytesRemaining, const uint8_t *chunkBase,
                                   size_t chunkBaseSize, SlipTrackWorldChunkPointList *result) {
	uint32_t pointListOffset;
	const uint8_t *pointListEntries;
	uint32_t pointListCount;

	if (chunk == 0 || result == 0 || chunkBytesRemaining < SLIP_TRK_CHUNK_POINT_LIST_END) {
		return false;
	}
	*result = (SlipTrackWorldChunkPointList){.branch = SLIP_TRACK_WORLD_CHUNK_POINTS_ABSENT};
	pointListOffset = (uint32_t)SlipBytes_ReadLE16(chunk + SLIP_TRK_CHUNK_POINT_LIST_OFFSET);
	result->pointListOffset = (uint16_t)pointListOffset;
	if (pointListOffset == 0) {
		return true;
	}
	if (chunkBase == 0 || (size_t)pointListOffset + SLIP_TRK_TABLE_COUNT_BYTES > chunkBaseSize) {
		return false;
	}
	pointListEntries = chunkBase + pointListOffset;
	pointListCount = (uint32_t)SlipBytes_ReadLE16(pointListEntries);
	pointListEntries += SLIP_TRK_TABLE_COUNT_BYTES;
	result->callBuildVertexRecords = true;
	result->pointListEntries = pointListEntries;
	result->pointListCount = pointListCount;
	result->pointRecordStride = SLIP_TRK_CHUNK_POINT_RECORD_BYTES;
	result->branch = SLIP_TRACK_WORLD_CHUNK_POINTS_PRESENT;
	return true;
}

bool SlipTrackWorld_ChunkBsp(const uint8_t *currentChunkAfterBsp, size_t chunkBytesRemaining, const uint8_t *chunkBase,
                             size_t chunkBaseSize, SlipTrackWorldChunkBsp *result) {
	uint32_t bspOffset;

	if (currentChunkAfterBsp == 0 || result == 0 || chunkBytesRemaining < SLIP_TRK_CHUNK_BSP_END) {
		return false;
	}
	*result = (SlipTrackWorldChunkBsp){0};
	bspOffset = (uint32_t)SlipBytes_ReadLE16(currentChunkAfterBsp + SLIP_TRK_CHUNK_BSP_OFFSET);
	result->bspOffset = (uint16_t)bspOffset;
	if (bspOffset != 0) {
		if (chunkBase == 0 || (size_t)bspOffset > chunkBaseSize) {
			return false;
		}
		result->callTraverseRawBsp = true;
		result->bspTreePointer = chunkBase + bspOffset;
		result->currentChunkAfterBsp = currentChunkAfterBsp;
	} else {
		result->callChunkFallback = true;
	}
	result->callRestoreVertexBuffer = true;
	return true;
}

bool SlipTrackWorld_ChunkProcess(const uint8_t *chunk, size_t chunkBytesRemaining, const uint8_t *chunkBase,
                                 size_t chunkBaseSize, const uint8_t *filter, uint32_t chunkCounter,
                                 SlipTrackWorldChunkProcess *result) {
	SlipTrackWorldChunkSetup chunkSetup;
	SlipTrackWorldChunkPointList pointListSetup;
	SlipTrackWorldChunkBsp bspTraversalSetup;

	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldChunkProcess){
	    .callSetupChunk = true,
	    .chunkSetup = {.chunkCounter = chunkCounter},
	    .pointListSetup = {.branch = SLIP_TRACK_WORLD_CHUNK_POINTS_ABSENT},
	    .ret = true,
	};
	if (!SlipTrackWorld_ChunkSetup(chunk, chunkBytesRemaining, filter, chunkCounter, &chunkSetup)) {
		return false;
	}
	result->chunkSetup = chunkSetup;
	if (chunkSetup.skippedByFilter) {
		return true;
	}
	result->callPrepareChunkPointList = true;
	if (!SlipTrackWorld_ChunkPointList(chunk, chunkBytesRemaining, chunkBase, chunkBaseSize, &pointListSetup)) {
		return false;
	}
	result->pointListSetup = pointListSetup;
	if (pointListSetup.branch == SLIP_TRACK_WORLD_CHUNK_POINTS_PRESENT) {
		result->callProcessChunkBsp = true;
		if (!SlipTrackWorld_ChunkBsp(chunk, chunkBytesRemaining, chunkBase, chunkBaseSize, &bspTraversalSetup)) {
			return false;
		}
		result->bspSetup = bspTraversalSetup;
	}
	return true;
}

bool SlipTrackWorld_ChunkProcessVertexCache(
    const uint8_t *chunk, size_t chunkBytesRemaining, const uint8_t *chunkBase, size_t chunkBaseSize,
    const uint8_t *filter, uint32_t chunkCounter, SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCapacity,
    SlipTrackWorldBuildVertexRecords buildVertexRecords, void *buildVertexRecordsUserData,
    SlipTrackWorldRestoreVertexBuffer restoreVertexBuffer, void *restoreVertexBufferUserData,
    SlipTrackWorldRawBspClassify classify, SlipTrackWorldRawBspCallback callback, void *rawBspUserData,
    uint16_t rawBspDepthCapacity, SlipView3DVec32 *currentChunkOrigin,
    bool (*chunkFallback)(const uint8_t *chunk, size_t chunkBytesRemaining, void *userData),
    void *chunkFallbackUserData, SlipTrackWorldChunkProcessVertexCache *result) {
	SlipTrackWorldChunkSetup chunkSetup;
	SlipTrackWorldChunkPointList pointListSetup;
	SlipTrackWorldChunkBsp bspTraversalSetup;
	SlipTrackWorldRawBsp rawBsp;

	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldChunkProcessVertexCache){
	    .callSetupChunk = true,
	    .chunkSetup = {.chunkCounter = chunkCounter},
	    .pointListSetup = {.branch = SLIP_TRACK_WORLD_CHUNK_POINTS_ABSENT},
	    .ret = true,
	};
	if (!SlipTrackWorld_ChunkSetup(chunk, chunkBytesRemaining, filter, chunkCounter, &chunkSetup)) {
		return false;
	}
	result->chunkSetup = chunkSetup;
	if (currentChunkOrigin != 0) {
		*currentChunkOrigin = chunkSetup.currentChunkOrigin;
	}
	if (chunkSetup.skippedByFilter) {
		return true;
	}
	result->callPrepareChunkPointList = true;
	if (!SlipTrackWorld_ChunkPointList(chunk, chunkBytesRemaining, chunkBase, chunkBaseSize, &pointListSetup)) {
		return false;
	}
	result->pointListSetup = pointListSetup;
	if (pointListSetup.callBuildVertexRecords) {
		size_t sourceOffset;
		size_t sourceSize;

		if (chunkBase == 0 || pointListSetup.pointListEntries < chunkBase ||
		    pointListSetup.pointListEntries > chunkBase + chunkBaseSize) {
			return false;
		}
		sourceOffset = (size_t)(pointListSetup.pointListEntries - chunkBase);
		sourceSize = chunkBaseSize - sourceOffset;
		result->callBuildVertexRecords = true;
		if (buildVertexRecords != NULL) {
			if (!buildVertexRecords(pointListSetup.pointListEntries, sourceSize,
			                        (uint16_t)pointListSetup.pointListCount, (int16_t)pointListSetup.pointRecordStride,
			                        buildVertexRecordsUserData)) {
				return false;
			}
		} else if (!SlipDraw3D_BuildVertexRecords(vertexRecords, vertexRecordCapacity, pointListSetup.pointListEntries,
		                                          sourceSize, (uint16_t)pointListSetup.pointListCount,
		                                          (int16_t)pointListSetup.pointRecordStride, NULL, NULL, NULL, 0, 0,
		                                          &result->buildVertexRecords)) {
			return false;
		}
	}
	if (pointListSetup.branch == SLIP_TRACK_WORLD_CHUNK_POINTS_PRESENT) {
		result->callProcessChunkBsp = true;
		if (!SlipTrackWorld_ChunkBsp(chunk, chunkBytesRemaining, chunkBase, chunkBaseSize, &bspTraversalSetup)) {
			return false;
		}
		result->bspSetup = bspTraversalSetup;
		if (bspTraversalSetup.callTraverseRawBsp && classify != NULL && callback != NULL && rawBspDepthCapacity != 0) {
			const size_t bspOffset = (size_t)(bspTraversalSetup.bspTreePointer - chunkBase);

			result->callTraverseRawBsp = true;
			if (!SlipTrackWorld_TraverseRawBsp(bspTraversalSetup.bspTreePointer, chunkBaseSize - bspOffset, classify,
			                                   callback, rawBspUserData, rawBspDepthCapacity, &rawBsp)) {
				return false;
			}
			result->rawBspNodeCount = rawBsp.nodeCount;
			result->rawBspClassifyCount = rawBsp.classifyCount;
			result->rawBspCallbackCount = rawBsp.callbackCount;
			result->rawBspHitDepthCapacity = rawBsp.hitDepthCapacity;
		} else if (bspTraversalSetup.callChunkFallback && chunkFallback != NULL &&
		           !chunkFallback(chunk, chunkBytesRemaining, chunkFallbackUserData)) {
			return false;
		}

		if (bspTraversalSetup.callRestoreVertexBuffer && restoreVertexBuffer != NULL &&
		    !restoreVertexBuffer(restoreVertexBufferUserData)) {
			return false;
		}
	}
	return true;
}

static bool SlipTrackWorld_TraverseRawBspNode(const uint8_t *bspBase, size_t bspBytesRemaining, uint32_t relativeOffset,
                                              SlipTrackWorldRawBspClassify classify,
                                              SlipTrackWorldRawBspCallback callback, void *userData, uint16_t depth,
                                              uint16_t depthCapacity, SlipTrackWorldRawBsp *result) {
	const uint8_t *node;
	uint32_t negativeChild;
	uint32_t positiveChild;
	uint32_t callbackValue;
	uint16_t recordKind;
	uint16_t normalX;
	uint16_t normalY;
	uint16_t normalZ;
	uint16_t nodeFlags;
	bool carry;

	if (depth >= depthCapacity) {
		result->hitDepthCapacity = true;
		return false;
	}
	if ((size_t)relativeOffset > bspBytesRemaining ||
	    bspBytesRemaining - (size_t)relativeOffset < SLIP_TRACK_BSP_NODE_BYTES) {
		return false;
	}
	node = bspBase + relativeOffset;
	result->nodeCount = (uint16_t)(result->nodeCount + 1u);
	normalX = SlipBytes_ReadLE16(node + SLIP_TRACK_BSP_NORMAL_X_OFFSET);
	if (normalX == UINT16_MAX) {
		callbackValue = SlipBytes_ReadLE32(node + SLIP_TRACK_BSP_CALLBACK_VALUE_OFFSET);
		recordKind = SlipBytes_ReadLE16(node + SLIP_TRACK_BSP_RECORD_KIND_OFFSET);
		result->callbackCount = (uint16_t)(result->callbackCount + 1u);
		return callback(node, callbackValue, recordKind, 0, userData);
	}
	normalY = SlipBytes_ReadLE16(node + SLIP_TRACK_BSP_NORMAL_Y_OFFSET);
	normalZ = SlipBytes_ReadLE16(node + SLIP_TRACK_BSP_NORMAL_Z_OFFSET);
	nodeFlags = SlipBytes_ReadLE16(node + SLIP_TRACK_BSP_FLAGS_OFFSET);
	result->classifyCount = (uint16_t)(result->classifyCount + 1u);
	if (!classify(node, normalX, normalY, normalZ, nodeFlags, userData, &carry)) {
		return false;
	}
	if (carry) {

		negativeChild = SlipBytes_ReadLE32(node + SLIP_TRACK_BSP_CHILD_0_OFFSET);
		if (negativeChild != 0 &&
		    !SlipTrackWorld_TraverseRawBspNode(bspBase, bspBytesRemaining, negativeChild, classify, callback, userData,
		                                       (uint16_t)(depth + 1u), depthCapacity, result)) {
			return false;
		}
		callbackValue = SlipBytes_ReadLE32(node + SLIP_TRACK_BSP_CALLBACK_VALUE_OFFSET);
		if (callbackValue != 0) {
			recordKind = SlipBytes_ReadLE16(node + SLIP_TRACK_BSP_RECORD_KIND_OFFSET);
			result->callbackCount = (uint16_t)(result->callbackCount + 1u);
			if (!callback(node, callbackValue, recordKind, -1, userData)) {
				return false;
			}
		}

		positiveChild = SlipBytes_ReadLE32(node + SLIP_TRACK_BSP_CHILD_1_OFFSET);
		if (positiveChild != 0 &&
		    !SlipTrackWorld_TraverseRawBspNode(bspBase, bspBytesRemaining, positiveChild, classify, callback, userData,
		                                       (uint16_t)(depth + 1u), depthCapacity, result)) {
			return false;
		}
		return true;
	}

	positiveChild = SlipBytes_ReadLE32(node + SLIP_TRACK_BSP_CHILD_1_OFFSET);
	if (positiveChild != 0 &&
	    !SlipTrackWorld_TraverseRawBspNode(bspBase, bspBytesRemaining, positiveChild, classify, callback, userData,
	                                       (uint16_t)(depth + 1u), depthCapacity, result)) {
		return false;
	}
	callbackValue = SlipBytes_ReadLE32(node + SLIP_TRACK_BSP_CALLBACK_VALUE_OFFSET);
	if (callbackValue != 0) {
		recordKind = SlipBytes_ReadLE16(node + SLIP_TRACK_BSP_RECORD_KIND_OFFSET);
		result->callbackCount = (uint16_t)(result->callbackCount + 1u);
		if (!callback(node, callbackValue, recordKind, 1, userData)) {
			return false;
		}
	}
	negativeChild = SlipBytes_ReadLE32(node + SLIP_TRACK_BSP_CHILD_0_OFFSET);
	if (negativeChild != 0 &&
	    !SlipTrackWorld_TraverseRawBspNode(bspBase, bspBytesRemaining, negativeChild, classify, callback, userData,
	                                       (uint16_t)(depth + 1u), depthCapacity, result)) {
		return false;
	}
	return true;
}

bool SlipTrackWorld_TraverseRawBsp(const uint8_t *bspBase, size_t bspBytesRemaining,
                                   SlipTrackWorldRawBspClassify classify, SlipTrackWorldRawBspCallback callback,
                                   void *userData, uint16_t depthCapacity, SlipTrackWorldRawBsp *result) {
	if (bspBase == 0 || classify == 0 || callback == 0 || result == 0 || depthCapacity == 0) {
		return false;
	}
	*result = (SlipTrackWorldRawBsp){
	    .pushOldBase = true,
	    .pushOldCallback = true,
	    .base = bspBase,
	    .rootRelativeOffset = SLIP_TRACK_BSP_ROOT_OFFSET,
	};
	if (!SlipTrackWorld_TraverseRawBspNode(bspBase, bspBytesRemaining, SLIP_TRACK_BSP_ROOT_OFFSET, classify, callback,
	                                       userData, 0, depthCapacity, result)) {
		return false;
	}
	result->restoreCallback = true;
	result->restoreBase = true;
	result->ret = true;
	return true;
}

bool SlipTrackWorld_ClassifyPlaneFromSource(uint32_t mode, uint16_t vertexIndex, uint16_t inputPlaneX,
                                            uint16_t inputPlaneY, uint16_t inputPlaneZ, const uint8_t *vertexCacheBase,
                                            size_t vertexCacheBytes, SlipView3DVec32 origin,
                                            SlipTrackWorldSourcePoint sourcePoint, void *userData,
                                            const SlipView3DMatrix *matrix, SlipTrackWorldPlaneClassify *result) {
	uint32_t vertexOffset;
	const SlipDraw3DVertexRecord *vertexRecord;
	int16_t sourceX;
	int16_t sourceY;
	int16_t sourceZ;
	SlipView3DVec32 point;
	SlipView3DVec32 relativePoint;
	int16_t planeX;
	int16_t planeY;
	int16_t planeZ;
	int64_t dot;

	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldPlaneClassify){mode,
	                                        mode == SLIP_DRAW3D_PROJECTION_ORTHOGRAPHIC,
	                                        vertexIndex,
	                                        0,
	                                        0,
	                                        0,
	                                        0,
	                                        false,
	                                        {0, 0, 0},
	                                        {0, 0, 0},
	                                        (int16_t)inputPlaneX,
	                                        (int16_t)inputPlaneY,
	                                        (int16_t)inputPlaneZ,
	                                        0,
	                                        false};
	if (mode == SLIP_DRAW3D_PROJECTION_ORTHOGRAPHIC) {
		uint32_t sum;
		int16_t facing;
		if (matrix == NULL)
			return false;

		sum = (uint32_t)((int32_t)(int16_t)inputPlaneX * matrix->m[2]);
		sum += (uint32_t)((int32_t)(int16_t)inputPlaneY * matrix->m[5]);
		sum += (uint32_t)((int32_t)(int16_t)inputPlaneZ * matrix->m[8]);
		facing = (int16_t)((int32_t)sum >> SLIP_Q14_FRACTION_BITS);
		result->carry = facing > 0;
		return true;
	}
	if (vertexCacheBase == 0 || sourcePoint == 0) {
		return false;
	}
	vertexOffset = (uint32_t)vertexIndex * SLIP_DRAW3D_VERTEX_RECORD_SIZE;
	if ((size_t)vertexOffset > vertexCacheBytes ||
	    vertexCacheBytes - (size_t)vertexOffset < offsetof(SlipDraw3DVertexRecord, sourceZ) + sizeof(int16_t)) {
		return false;
	}

	vertexRecord = &((const SlipDraw3DVertexRecord *)(const void *)vertexCacheBase)[vertexIndex];
	sourceX = vertexRecord->sourceX;
	sourceY = vertexRecord->sourceY;
	sourceZ = vertexRecord->sourceZ;
	point = sourcePoint(sourceX, sourceY, sourceZ, userData);

	relativePoint = (SlipView3DVec32){(int32_t)((uint32_t)point.x - (uint32_t)origin.x),
	                                  (int32_t)((uint32_t)point.y - (uint32_t)origin.y),
	                                  (int32_t)((uint32_t)point.z - (uint32_t)origin.z)};
	planeX = (int16_t)inputPlaneX;
	planeY = (int16_t)inputPlaneY;
	planeZ = (int16_t)inputPlaneZ;
	dot = (int64_t)relativePoint.x * planeX + (int64_t)relativePoint.y * planeY + (int64_t)relativePoint.z * planeZ;
	result->vertexRecordOffset = vertexOffset;
	result->sourceX = sourceX;
	result->sourceY = sourceY;
	result->sourceZ = sourceZ;
	result->callTransformSourcePoint = true;
	result->transformedSourcePoint = point;
	result->pointMinusOrigin = relativePoint;
	result->planeX = planeX;
	result->planeY = planeY;
	result->planeZ = planeZ;
	result->dotProduct = dot;
	result->carry = dot >= 0;
	return true;
}

bool SlipTrackWorld_ClassifyAxisPlane(uint32_t mode, uint32_t pointX, uint32_t pointY, uint32_t pointZ, uint16_t axisX,
                                      uint16_t axisY, uint16_t axisZ, SlipTrackWorldAxisPlaneClassify *result) {
	int64_t dotProductX;
	int64_t dotProductY;
	int64_t dotProductZ;
	uint32_t productXLow;
	uint32_t productXHigh;
	uint32_t productYLow;
	uint32_t productYHigh;
	uint32_t productZLow;
	uint32_t productZHigh;
	uint32_t partialDotLow;
	uint16_t partialDotHigh;
	uint16_t dotProductHighWord;
	uint32_t carryFromFirstAdd;
	uint32_t carryFromSecondAdd;
	bool signFlag;

	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldAxisPlaneClassify){.projectionMode = mode,
	                                            .perspectiveBranch = mode == SLIP_DRAW3D_PROJECTION_PERSPECTIVE,
	                                            .pointX = pointX,
	                                            .pointY = pointY,
	                                            .pointZ = pointZ,
	                                            .axisX = axisX,
	                                            .axisY = axisY,
	                                            .axisZ = axisZ};
	if (mode != SLIP_DRAW3D_PROJECTION_PERSPECTIVE) {
		result->clearsCarry = axisZ == 0 || ((axisZ & SLIP_TRACK_WORD_SIGN_BIT) != 0);
		result->setsCarry = !result->clearsCarry;
		result->carry = result->setsCarry;
		return true;
	}
	dotProductX = (int64_t)(int32_t)pointX * (int64_t)(int32_t)(int16_t)axisX;
	dotProductY = (int64_t)(int32_t)(int16_t)axisY * (int64_t)(int32_t)pointY;
	dotProductZ = (int64_t)(int32_t)(int16_t)axisZ * (int64_t)(int32_t)pointZ;
	productXLow = (uint32_t)(uint64_t)dotProductX;
	productXHigh = (uint32_t)((uint64_t)dotProductX >> 32);
	productYLow = (uint32_t)(uint64_t)dotProductY;
	productYHigh = (uint32_t)((uint64_t)dotProductY >> 32);
	productZLow = (uint32_t)(uint64_t)dotProductZ;
	productZHigh = (uint32_t)((uint64_t)dotProductZ >> 32);
	carryFromFirstAdd = ((uint64_t)productZLow + (uint64_t)productYLow) >> 32;
	partialDotLow = productZLow + productYLow;
	partialDotHigh = (uint16_t)((uint16_t)productZHigh + (uint16_t)productYHigh + carryFromFirstAdd);
	carryFromSecondAdd = ((uint64_t)partialDotLow + (uint64_t)productXLow) >> 32;
	dotProductHighWord = (uint16_t)(partialDotHigh + (uint16_t)productXHigh + carryFromSecondAdd);
	signFlag = (dotProductHighWord & SLIP_TRACK_WORD_SIGN_BIT) != 0;
	result->dotProductX = dotProductX;
	result->dotProductY = dotProductY;
	result->dotProductZ = dotProductZ;
	result->dotProductHighWord = dotProductHighWord;
	result->dotProductNegative = signFlag;
	result->clearsCarry = signFlag;
	result->setsCarry = !signFlag;
	result->carry = !signFlag;
	return true;
}

bool SlipTrackWorld_ChunkAlternatePaths(const uint8_t *chunk, size_t chunkBytesRemaining, const uint8_t *chunkBase,
                                        size_t chunkBaseSize, bool trackWorldDeferredMembershipCarry,
                                        SlipTrackWorldChunkAlternatePaths *result) {
	uint32_t firstPathOffset;
	uint32_t secondPathOffset;

	if (chunk == 0 || result == 0 || chunkBytesRemaining < SLIP_TRK_CHUNK_CALLBACK_LINKS_END) {
		return false;
	}
	*result = (SlipTrackWorldChunkAlternatePaths){
	    0, 0, 0, false, false, 0, 0, false, trackWorldDeferredMembershipCarry, false};
	firstPathOffset = (uint32_t)SlipBytes_ReadLE16(chunk + SLIP_TRK_CHUNK_RECORD_CALLBACK_OFFSET);
	result->recordCallbackOffset = (uint16_t)firstPathOffset;
	if (firstPathOffset != 0) {
		if (chunkBase == 0 ||
		    (size_t)firstPathOffset + SLIP_TRK_CALLBACK_LIST_HEADER_BYTES + SLIP_TRK_CALLBACK_VERTEX_END >
		        chunkBaseSize) {
			return false;
		}
		result->recordCallbackRecord = chunkBase + firstPathOffset + SLIP_TRK_CALLBACK_LIST_HEADER_BYTES;
		result->vertexIndex = SlipBytes_ReadLE16(result->recordCallbackRecord + SLIP_TRK_CALLBACK_VERTEX_OFFSET);
		result->callGetVertexPosition = true;
		result->callRecordCallback = true;
		return true;
	}
	secondPathOffset = (uint32_t)SlipBytes_ReadLE16(chunk + SLIP_TRK_CHUNK_TRAVERSAL_CALLBACK_OFFSET);
	result->traversalCallbackOffset = (uint16_t)secondPathOffset;
	if (secondPathOffset == 0) {
		return true;
	}
	if (chunkBase == 0 || (size_t)secondPathOffset + SLIP_TRK_CALLBACK_LIST_HEADER_BYTES > chunkBaseSize) {
		return false;
	}
	result->traversalCallbackRecord = chunkBase + secondPathOffset + SLIP_TRK_CALLBACK_LIST_HEADER_BYTES;
	result->callTrackWorldDeferredMembership = true;
	if (!trackWorldDeferredMembershipCarry) {
		result->callTraversalCallback = true;
	}
	return true;
}

bool SlipTrackWorld_ChunkDispatch(uint16_t recordKind, uint16_t recordOffset, const uint8_t *chunkBase,
                                  size_t chunkBaseSize, bool trackWorldDeferredMembershipCarry,
                                  SlipTrackWorldChunkDispatch *result) {
	uint32_t chunkOffset;

	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldChunkDispatch){
	    recordKind, recordOffset, 0, false, trackWorldDeferredMembershipCarry, false, false, false, 0, false, false};
	chunkOffset = (uint32_t)recordOffset;
	if (chunkBase == 0 || (size_t)chunkOffset > chunkBaseSize) {
		return false;
	}
	result->callbackRecord = chunkBase + chunkOffset;
	result->callTrackWorldDeferredMembership = true;
	if (trackWorldDeferredMembershipCarry) {
		return true;
	}
	if (recordKind == SLIP_TRACK_BSP_TRAVERSAL_CALLBACK_KIND) {
		result->savedChunkBeforeTraversalCallback = true;
		result->callTraversalCallback = true;
		return true;
	}
	if ((size_t)chunkOffset + SLIP_TRK_CALLBACK_VERTEX_END > chunkBaseSize) {
		return false;
	}
	result->savedChunkBeforeRecordCallback = true;
	result->vertexIndex = SlipBytes_ReadLE16(result->callbackRecord + SLIP_TRK_CALLBACK_VERTEX_OFFSET);
	result->callGetVertexPosition = true;
	result->callRecordCallback = true;
	return true;
}

bool SlipTrackWorld_RecordVisibility(const uint8_t *record, size_t recordBytesRemaining, uint32_t viewPositionX,
                                     uint32_t viewPositionY, uint32_t viewPositionZ, uint16_t mask, uint32_t mode,
                                     uint32_t minDepth, int32_t detailThreshold,
                                     SlipTrackWorldRecordVisibility *result) {
	uint16_t recordMaskWord;
	uint32_t radius;
	int32_t detailValue;

	if (record == 0 || result == 0 || recordBytesRemaining < SLIP_TRK_SHAPE_HEADER_BYTES) {
		return false;
	}
	*result = (SlipTrackWorldRecordVisibility){0, 0, false, 0, 0, 0, 0, 0, false, 0, detailThreshold, false, false};
	recordMaskWord = SlipBytes_ReadLE16(record + SLIP_TRK_SHAPE_VISIBILITY_MASK_OFFSET);
	result->recordMaskWord = recordMaskWord;
	recordMaskWord = (uint16_t)(recordMaskWord & mask);
	result->maskedRecordWord = recordMaskWord;
	if (recordMaskWord == 0) {
		result->skippedByMask = true;
		return true;
	}
	radius = SlipBytes_ReadLE32(record + SLIP_TRK_SHAPE_RADIUS_OFFSET);
	result->radius = radius;
	result->cachedRadius = radius;
	result->viewPositionX = viewPositionX;
	result->viewPositionY = viewPositionY;
	result->viewPositionZ = viewPositionZ;
	result->callDraw3DDetailValue = true;
	detailValue = (int32_t)SlipDraw3D_DetailValue(mode, minDepth, radius, viewPositionZ);
	result->detailValue = detailValue;
	if (detailValue <= detailThreshold) {
		result->skippedByDetail = true;
		return true;
	}
	result->continues = true;
	return true;
}

bool SlipTrackWorld_RecordTransformSetup(const uint8_t *record, size_t recordBytesRemaining,
                                         uint32_t currentRecordIndex, SlipTrackWorldRecordTransformSetup *result) {
	uint32_t centerY;
	uint32_t positionYWithCenter;
	uint16_t facingTransformFlag;

	if (record == 0 || result == 0 || recordBytesRemaining < SLIP_TRK_SHAPE_WITH_FACING_BYTES) {
		return false;
	}
	*result = (SlipTrackWorldRecordTransformSetup){
	    0, false, false, currentRecordIndex, false, 0, 0, 0, 0, 0, 0, 0, 0, SLIP_TRACK_WORLD_RECORD_TRANSLATION};
	result->shapeHandle = SlipBytes_ReadLE16(record + SLIP_TRK_SHAPE_HANDLE_OFFSET);
	result->savedRecordPointer = true;
	result->callGetDrawStateIndex = true;
	result->drawStateIndexAfterAdvance = currentRecordIndex + 2u;
	result->callLoadDrawState = true;
	centerY = SlipBytes_ReadLE32(record + SLIP_TRK_SHAPE_CENTER_Y_OFFSET);
	result->centerY = centerY;
	result->cachedCenterY = centerY;
	positionYWithCenter = SlipBytes_ReadLE32(record + SLIP_TRK_SHAPE_POSITION_Y_OFFSET);
	result->positionY = positionYWithCenter;
	positionYWithCenter += centerY;
	result->positionYWithCenter = positionYWithCenter;
	result->savedPositionX = SlipBytes_ReadLE32(record + SLIP_TRK_SHAPE_POSITION_X_OFFSET);
	result->savedPositionYWithCenter = positionYWithCenter;
	result->savedPositionZ = SlipBytes_ReadLE32(record + SLIP_TRK_SHAPE_POSITION_Z_OFFSET);
	facingTransformFlag = SlipBytes_ReadLE16(record + SLIP_TRK_SHAPE_FACING_FLAG_OFFSET);
	result->facingTransformFlag = facingTransformFlag;
	if (facingTransformFlag != 0) {
		result->branch = SLIP_TRACK_WORLD_RECORD_MATRIX;
	}
	return true;
}

bool SlipTrackWorld_RecordMatrixTransform(const uint8_t *record, size_t recordBytesRemaining,
                                          SlipView3DMatrix *worldMatrix, SlipView3DMatrix *objectViewMatrix,
                                          const SlipView3DMatrix *viewMatrix,
                                          SlipTrackWorldRecordMatrixTransform *result) {
	if (record == 0 || worldMatrix == 0 || objectViewMatrix == 0 || viewMatrix == 0 || result == 0 ||
	    recordBytesRemaining < SLIP_TRK_SHAPE_VISIBILITY_MASK_OFFSET) {
		return false;
	}
	*result = (SlipTrackWorldRecordMatrixTransform){0, false, 0, 0, false, 0, 0, 0, 0, false, 0};
	result->facingModeFlag = 0;
	result->savedShapeHandle = true;
	result->recordMatrix = record + SLIP_TRK_SHAPE_MATRIX_OFFSET;
	result->worldMatrixAddress = SLIP_TRACK_WORLD_WORLD_MATRIX_TOKEN;
	result->callView3DCopyMatrixWords = true;
	if (!SlipView3D_CopyMatrixWords((uint8_t *)worldMatrix, sizeof(*worldMatrix), record + SLIP_TRK_SHAPE_MATRIX_OFFSET,
	                                recordBytesRemaining - SLIP_TRK_SHAPE_MATRIX_OFFSET)) {
		return false;
	}
	result->worldMatrixAddressAfterCopy = SLIP_TRACK_WORLD_WORLD_MATRIX_TOKEN;
	result->worldMatrixSourceAddress = result->worldMatrixAddressAfterCopy;
	result->viewMatrixDestinationAddress = SLIP_TRACK_WORLD_VIEW_MATRIX_TOKEN;
	result->cameraMatrixAddress = SLIP_TRACK_WORLD_CAMERA_MATRIX_TOKEN;
	result->callMultiplyMatrix = true;
	SlipView3D_MultiplyMatrix(worldMatrix, viewMatrix, objectViewMatrix);
	result->continuationAddress = SLIP_TRACK_WORLD_MATRIX_CONTINUATION_TOKEN;
	return true;
}

bool SlipTrackWorld_RecordFacingTransform(const uint8_t *record, size_t recordBytesRemaining, uint32_t cameraWorldX,
                                          uint32_t cameraWorldZ, SlipView3DMatrix *worldMatrix,
                                          SlipView3DMatrix *objectViewMatrix, const SlipView3DMatrix *viewMatrix,
                                          SlipTrackWorldRecordFacingTransform *result) {
	uint32_t facingAxisX;
	uint32_t facingAxisZ;
	uint32_t zeroMatrixEntry;

	if (record == 0 || worldMatrix == 0 || objectViewMatrix == 0 || viewMatrix == 0 || result == 0 ||
	    recordBytesRemaining < SLIP_TRK_SHAPE_POSITION_END) {
		return false;
	}
	*result = (SlipTrackWorldRecordFacingTransform){0, false, 0, 0, 0, 0, false, {0}, 0,     0, 0, 0, 0, 0, 0, 0,
	                                                0, 0,     0, 0, 0, 0, 0,     0,   false, 0, 0, 0, 0, 0, 0};
	result->facingModeFlag = UINT32_MAX;
	result->savedShapeHandle = true;
	facingAxisX = SlipBytes_ReadLE32(record + SLIP_TRK_SHAPE_POSITION_X_OFFSET);
	facingAxisZ = SlipBytes_ReadLE32(record + SLIP_TRK_SHAPE_POSITION_Z_OFFSET);
	result->positionX = facingAxisX;
	result->positionZ = facingAxisZ;
	facingAxisX -= cameraWorldX;
	facingAxisZ -= cameraWorldZ;
	result->cameraDeltaX = facingAxisX;
	result->cameraDeltaZ = facingAxisZ;
	result->callView3DNormalizeScaledVector2D = true;
	if (!SlipView3D_NormalizeScaledVector2D(facingAxisX, facingAxisZ, &result->normalize)) {
		return false;
	}
	facingAxisX = (uint32_t)(uint16_t)result->normalize.unitXQ14;
	facingAxisZ = (uint32_t)(uint16_t)result->normalize.unitYQ14;
	result->facingAxisX = facingAxisX;
	result->facingAxisZ = facingAxisZ;
	zeroMatrixEntry = 0;
	result->zeroMatrixEntry = zeroMatrixEntry;
	result->worldMatrixAddress = SLIP_TRACK_WORLD_WORLD_MATRIX_TOKEN;
	result->facingMatrixZX = (uint16_t)facingAxisX;
	result->facingMatrixZY = (uint16_t)zeroMatrixEntry;
	result->facingMatrixZZ = (uint16_t)facingAxisZ;
	facingAxisX = 0u - facingAxisX;
	result->negatedFacingAxisX = facingAxisX;
	result->facingMatrixXX = (uint16_t)facingAxisZ;
	result->facingMatrixXY = (uint16_t)zeroMatrixEntry;
	result->facingMatrixXZ = (uint16_t)facingAxisX;
	result->facingMatrixYX = (uint16_t)zeroMatrixEntry;
	result->facingMatrixYY = SLIP_Q14_ONE;
	result->facingMatrixYZ = (uint16_t)zeroMatrixEntry;
	worldMatrix->m[6] = (int16_t)result->facingMatrixZX;
	worldMatrix->m[7] = (int16_t)result->facingMatrixZY;
	worldMatrix->m[8] = (int16_t)result->facingMatrixZZ;
	worldMatrix->m[0] = (int16_t)result->facingMatrixXX;
	worldMatrix->m[1] = (int16_t)result->facingMatrixXY;
	worldMatrix->m[2] = (int16_t)result->facingMatrixXZ;
	worldMatrix->m[3] = (int16_t)result->facingMatrixYX;
	worldMatrix->m[4] = (int16_t)result->facingMatrixYY;
	worldMatrix->m[5] = (int16_t)result->facingMatrixYZ;
	result->viewMatrixDestinationAddress = SLIP_TRACK_WORLD_VIEW_MATRIX_TOKEN;
	result->cameraMatrixAddress = SLIP_TRACK_WORLD_CAMERA_MATRIX_TOKEN;
	result->callMultiplyMatrix = true;
	SlipView3D_MultiplyMatrix(worldMatrix, viewMatrix, objectViewMatrix);
	result->viewMatrixAddressAfterMultiply = SLIP_TRACK_WORLD_VIEW_MATRIX_TOKEN;
	objectViewMatrix->m[2] = 0;
	objectViewMatrix->m[5] = 0;
	objectViewMatrix->m[6] = 0;
	objectViewMatrix->m[7] = 0;
	objectViewMatrix->m[8] = SLIP_Q14_ONE;
	result->planarViewMatrixXZ = 0;
	result->planarViewMatrixYZ = 0;
	result->planarViewMatrixZX = 0;
	result->planarViewMatrixZY = 0;
	result->planarViewMatrixZZ = SLIP_Q14_ONE;
	return true;
}

bool SlipTrackWorld_RecordScaledCenter(const SlipView3DMatrix *objectViewMatrix, uint32_t inputCenterY,
                                       uint32_t viewPositionX, uint32_t viewPositionY, uint32_t viewPositionZ,
                                       uint32_t restoredPositionZ, uint32_t restoredPositionYWithCenter,
                                       uint32_t restoredPositionX, SlipTrackWorldRecordScaledCenter *result) {
	uint32_t centerY;
	uint32_t centerOffsetX;
	uint32_t centerOffsetY;
	uint32_t centerOffsetZ;
	int16_t matrixWord;
	uint64_t product;

	if (objectViewMatrix == 0 || result == 0) {
		return false;
	}
	*result = (SlipTrackWorldRecordScaledCenter){0};
	result->restoredShapeHandle = true;
	result->viewMatrixAddress = SLIP_TRACK_WORLD_VIEW_MATRIX_TOKEN;
	centerY = inputCenterY;
	result->centerY = centerY;
	matrixWord = objectViewMatrix->m[5];
	result->matrixYZ = matrixWord;
	product = (uint64_t)((int64_t)(int32_t)matrixWord * (int64_t)(int32_t)centerY);
	centerOffsetZ = (uint32_t)(product >> SLIP_Q14_FRACTION_BITS);
	result->centerOffsetZ = centerOffsetZ;
	result->centerOffsetZCopy = centerOffsetZ;
	matrixWord = objectViewMatrix->m[4];
	result->matrixYY = matrixWord;
	product = (uint64_t)((int64_t)(int32_t)matrixWord * (int64_t)(int32_t)centerY);
	centerOffsetY = (uint32_t)(product >> SLIP_Q14_FRACTION_BITS);
	result->centerOffsetY = centerOffsetY;
	result->centerOffsetYCopy = centerOffsetY;
	matrixWord = objectViewMatrix->m[3];
	result->matrixYX = matrixWord;
	product = (uint64_t)((int64_t)(int32_t)matrixWord * (int64_t)(int32_t)centerY);
	centerOffsetX = (uint32_t)(product >> SLIP_Q14_FRACTION_BITS);
	result->centerOffsetX = centerOffsetX;
	centerOffsetX += viewPositionX;
	centerOffsetY += viewPositionY;
	centerOffsetZ += viewPositionZ;
	result->viewCenterX = centerOffsetX;
	result->viewCenterY = centerOffsetY;
	result->viewCenterZ = centerOffsetZ;
	result->cachedViewCenterZ = centerOffsetZ;
	result->restorePositionZ = true;
	result->restoredPositionZ = restoredPositionZ;
	result->restorePositionYWithCenter = true;
	result->restoredPositionYWithCenter = restoredPositionYWithCenter;
	result->restorePositionX = true;
	result->restoredPositionX = restoredPositionX;
	result->callDrawSetup = true;
	if (!SlipShape3D_DrawSetup(result->viewCenterX, result->viewCenterY, result->viewCenterZ, result->restoredPositionX,
	                           result->restoredPositionYWithCenter, result->restoredPositionZ, &result->drawSetup)) {
		return false;
	}
	return true;
}

bool SlipTrackWorld_RecordSphereCull(SlipView3DVec32 center, uint32_t cullingRadius,
                                     SlipTrackWorldSphereCull sphereCull, void *sphereCullUserData,
                                     uint32_t visibleRecordCount, uint32_t viewDepth,
                                     SlipTrackWorldRecordSphereCull *result) {
	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldRecordSphereCull){0};
	result->center = center;
	result->cullingRadius = cullingRadius;
	result->callTrackWorldIndirectCull = true;
	if (!SlipTrackWorld_IndirectCull(center, (int32_t)cullingRadius, sphereCull, sphereCullUserData,
	                                 &result->indirectCull)) {
		return false;
	}
	result->trackWorldIndirectCullCarry = result->indirectCull.sphereCullCarry;
	result->visibleRecordCount = visibleRecordCount;
	result->branch = SLIP_TRACK_WORLD_RECORD_CULLED;
	if (result->trackWorldIndirectCullCarry) {
		return true;
	}
	result->visibleRecordCount = visibleRecordCount + 1u;
	result->facingMatrixToken = SLIP_TRACK_WORLD_WORLD_MATRIX_TOKEN;
	result->planarViewMatrixToken = SLIP_TRACK_WORLD_VIEW_MATRIX_TOKEN;
	result->viewDepth = viewDepth;
	result->callTrackWorldUpdateDrawFlags = true;
	result->branch = SLIP_TRACK_WORLD_RECORD_DRAW_READY;
	return true;
}

bool SlipTrackWorld_RecordDrawDispatch(uint16_t mask, uint32_t renderFlagsBeforeMask, uint32_t facingModeFlag,
                                       uint32_t renderFlagsForFacing, uint32_t frameRenderFlags,
                                       SlipTrackWorldRecordDrawDispatch *result) {
	uint32_t renderFlagsAfterMask;

	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldRecordDrawDispatch){
	    mask,  false, false, renderFlagsBeforeMask, 0,    false, 0, facingModeFlag, false, false, renderFlagsForFacing,
	    false, false, false, frameRenderFlags,      false};
	if (mask != SLIP_TRACK_SPECIAL_RENDER_MODE) {
		result->skipMaskFlagUpdate = true;
	} else {
		result->callGetRenderFlagsForMask = true;
		renderFlagsAfterMask = renderFlagsBeforeMask;
		renderFlagsAfterMask &= ~SLIP_RENDER_FLAT_SHADING;
		result->renderFlagsAfterMask = renderFlagsAfterMask;
		result->callSetRenderFlagsAfterMask = true;
		result->renderFlagsToStore = renderFlagsAfterMask;
	}
	if (facingModeFlag == 0) {
		result->drawWithoutFacingFlagTest = true;
		result->callDrawShape = true;
	} else {
		result->callGetRenderFlagsForFacing = true;
		result->facingSkipFlagSet = (renderFlagsForFacing & SLIP_RENDER_DISABLE_TEXTURES) != 0;
		if (result->facingSkipFlagSet) {
			result->skipShapeDraw = true;
		} else {
			result->callDrawShape = true;
		}
	}
	result->restoredFrameRenderFlags = frameRenderFlags;
	result->callRestoreFrameRenderFlags = true;
	return true;
}

bool SlipTrackWorld_RecordDrawRestore(uint32_t drawStateIndex, SlipTrackWorldRecordDrawRestore *result) {
	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldRecordDrawRestore){true, drawStateIndex - 2u, true, true, true};
	return true;
}

bool SlipTrackWorld_ObjectRecordLookup(uint32_t attachmentListOffset, uint32_t objectListCount,
                                       uint32_t slotDrawBaseToken, SlipTrackWorldObjectRecordLookup *result) {
	uint32_t attachmentListAddress;

	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldObjectRecordLookup){
	    true, attachmentListOffset, attachmentListOffset == 0, 0, false, 0, SLIP_TRACK_WORLD_OBJECT_ATTACHMENT_FOUND};
	attachmentListAddress = attachmentListOffset;
	if (attachmentListAddress != 0) {
		attachmentListAddress += slotDrawBaseToken;
		result->attachmentListAddress = attachmentListAddress;
		return true;
	}
	result->objectListCount = objectListCount;
	if (objectListCount == 0) {
		result->branch = SLIP_TRACK_WORLD_OBJECT_LIST_EMPTY;
		return true;
	}
	result->callDraw3DListPushFrame = true;
	result->branch = SLIP_TRACK_WORLD_OBJECT_LIST_READY;
	return true;
}

bool SlipTrackWorld_ObjectAttachmentDraw(const uint8_t *drawRecord, size_t recordBytesRemaining,
                                         SlipTrackWorldObjectAttachmentDraw *result) {
	uint32_t cmp;

	if (drawRecord == 0 || result == 0 || recordBytesRemaining < SLIP_TRACK_SLOT_DOOR_ADDRESS_END) {
		return false;
	}
	*result = (SlipTrackWorldObjectAttachmentDraw){drawRecord,
	                                               true,
	                                               drawRecord,
	                                               drawRecord,
	                                               0,
	                                               drawRecord,
	                                               true,
	                                               true,
	                                               0,
	                                               true,
	                                               false,
	                                               false,
	                                               false,
	                                               false,
	                                               0,
	                                               false,
	                                               false,
	                                               false,
	                                               false,
	                                               false,
	                                               false,
	                                               false,
	                                               SLIP_TRACK_WORLD_OBJECT_ATTACHMENT_SKIPPED};
	result->objectOffset = SlipBytes_ReadLE32(drawRecord + offsetof(SlipTrackDrawRecord, objectOffset));
	cmp = SlipBytes_ReadLE32(drawRecord + SLIP_TRACK_SLOT_DOOR_ADDRESS_OFFSET);
	result->slotAttachmentReference = cmp;
	if (cmp == 0) {
		return true;
	}
	result->callDraw3DListPushFrame = true;
	result->savedRecordForCallbackRead = true;
	result->callObjectGetSlotDrawCallback = true;
	result->savedDrawCallback = true;
	result->schedulingCallbackAddress = SLIP_TRACK_WORLD_OBJECT_SCHEDULING_CALLBACK_TOKEN;
	result->callInstallSchedulingCallback = true;
	result->callDrawObject = true;
	result->callDraw3DListTraverse = true;
	result->callDraw3DListPopFrame = true;
	result->restoredDrawCallback = true;
	result->callRestoreDrawCallback = true;
	result->restoredDrawRecord = true;
	result->branch = SLIP_TRACK_WORLD_OBJECT_ATTACHMENT_DRAWN;
	return true;
}

bool SlipTrackWorld_ObjectAttachmentMatch(const uint8_t *record, size_t recordBytesRemaining,
                                          uint32_t attachmentListHeadAddress,
                                          SlipTrackWorldObjectAttachmentMatch *result) {
	uint32_t nextDrawRecordAddress;

	if (record == 0 || result == 0 || recordBytesRemaining < sizeof(uint32_t)) {
		return false;
	}
	nextDrawRecordAddress = SlipBytes_ReadLE32(record);
	*result = (SlipTrackWorldObjectAttachmentMatch){nextDrawRecordAddress,
	                                                attachmentListHeadAddress,
	                                                false,
	                                                false,
	                                                false,
	                                                SLIP_TRACK_WORLD_OBJECT_ATTACHMENT_DIFFERENT};
	if (nextDrawRecordAddress != attachmentListHeadAddress) {
		return true;
	}
	result->restoredComponentContext = true;
	result->restoredAttachmentListHead = true;
	result->callDraw3DListPushFrame = true;
	result->branch = SLIP_TRACK_WORLD_OBJECT_ATTACHMENT_MATCHED;
	return true;
}

bool SlipTrackWorld_ObjectCallbackDraw(const uint8_t *drawRecord, size_t recordBytesRemaining, uint32_t counter,
                                       SlipTrackWorldObjectCallbackDraw *result) {
	uint32_t cmp;

	if (drawRecord == 0 || result == 0 || recordBytesRemaining < SLIP_TRACK_SLOT_DOOR_ADDRESS_END) {
		return false;
	}
	*result = (SlipTrackWorldObjectCallbackDraw){drawRecord,
	                                             drawRecord,
	                                             0,
	                                             drawRecord,
	                                             true,
	                                             true,
	                                             0,
	                                             true,
	                                             false,
	                                             false,
	                                             false,
	                                             0,
	                                             false,
	                                             false,
	                                             false,
	                                             false,
	                                             false,
	                                             counter,
	                                             SLIP_TRACK_WORLD_OBJECT_CALLBACK_SKIPPED};
	result->objectOffset = SlipBytes_ReadLE32(drawRecord + offsetof(SlipTrackDrawRecord, objectOffset));
	cmp = SlipBytes_ReadLE32(drawRecord + SLIP_TRACK_SLOT_DOOR_ADDRESS_OFFSET);
	result->slotAttachmentReference = cmp;
	if (cmp != 0) {
		return true;
	}
	result->savedRecordForCallbackRead = true;
	result->callObjectGetSlotDrawCallback = true;
	result->savedDrawCallback = true;
	result->schedulingCallbackAddress = SLIP_TRACK_WORLD_OBJECT_SCHEDULING_CALLBACK_TOKEN;
	result->callInstallSchedulingCallback = true;
	result->callDrawObject = true;
	result->restoredDrawCallback = true;
	result->callRestoreDrawCallback = true;
	result->restoredDrawRecord = true;
	result->drawnObjectCount = counter + 1u;
	result->branch = SLIP_TRACK_WORLD_OBJECT_CALLBACK_DRAWN;
	return true;
}

bool SlipTrackWorld_ObjectCallbackMatch(const uint8_t *record, size_t recordBytesRemaining,
                                        uint32_t attachmentListHeadAddress, SlipTrackWorldObjectCallbackMatch *result) {
	uint32_t nextDrawRecordAddress;

	if (record == 0 || result == 0 || recordBytesRemaining < sizeof(uint32_t)) {
		return false;
	}
	nextDrawRecordAddress = SlipBytes_ReadLE32(record);
	*result = (SlipTrackWorldObjectCallbackMatch){nextDrawRecordAddress, attachmentListHeadAddress,
	                                              SLIP_TRACK_WORLD_OBJECT_CALLBACK_MATCHED};
	if (nextDrawRecordAddress != attachmentListHeadAddress) {
		result->branch = SLIP_TRACK_WORLD_OBJECT_CALLBACK_DIFFERENT;
	}
	return true;
}

bool SlipTrackWorld_ObjectListHead(uint32_t objectListCount, const uint8_t *recordBase, size_t recordBytesRemaining,
                                   uint32_t componentRecord, SlipTrackWorldObjectListHead *result) {
	uint32_t currentObjectListCount;
	uint32_t recordComponentAddress;

	if (result == 0) {
		return false;
	}
	currentObjectListCount = objectListCount;
	*result = (SlipTrackWorldObjectListHead){currentObjectListCount,
	                                         currentObjectListCount == 0,
	                                         0,
	                                         false,
	                                         false,
	                                         0,
	                                         componentRecord,
	                                         SLIP_TRACK_WORLD_OBJECT_LIST_HEAD_EMPTY};
	if (currentObjectListCount == 0) {
		return true;
	}
	if (recordBase == 0 || recordBytesRemaining < sizeof(uint32_t)) {
		return false;
	}
	result->objectListRecord = recordBase;
	result->savedRemainingCount = true;
	result->savedObjectListRecord = true;
	recordComponentAddress = SlipBytes_ReadLE32(recordBase);
	result->recordComponentAddress = recordComponentAddress;
	if (recordComponentAddress == componentRecord) {
		result->branch = SLIP_TRACK_WORLD_OBJECT_LIST_HEAD_MATCHED;
	} else {
		result->branch = SLIP_TRACK_WORLD_OBJECT_LIST_HEAD_DIFFERENT;
	}
	return true;
}

bool SlipTrackWorld_ObjectRelativePosition(const uint8_t *objectRecord, size_t recordBytesRemaining,
                                           uint32_t cameraWorldX, uint32_t cameraWorldY, uint32_t cameraWorldZ,
                                           SlipTrackWorldObjectRelativePosition *result) {
	uint32_t worldX;
	uint32_t worldY;
	uint32_t worldZ;

	if (objectRecord == 0 || result == 0 || recordBytesRemaining < SLIP_TRACK_OBJECT_LIST_POSITION_END) {
		return false;
	}
	worldX = SlipBytes_ReadLE32(objectRecord + SLIP_TRACK_OBJECT_LIST_WORLD_X_OFFSET);
	worldY = SlipBytes_ReadLE32(objectRecord + SLIP_TRACK_OBJECT_LIST_WORLD_Y_OFFSET);
	worldZ = SlipBytes_ReadLE32(objectRecord + SLIP_TRACK_OBJECT_LIST_WORLD_Z_OFFSET);
	*result = (SlipTrackWorldObjectRelativePosition){worldX,
	                                                 worldY,
	                                                 worldZ,
	                                                 worldX - cameraWorldX,
	                                                 worldY - cameraWorldY,
	                                                 worldZ - cameraWorldZ,
	                                                 0,
	                                                 true,
	                                                 true,
	                                                 true,
	                                                 worldZ - cameraWorldZ,
	                                                 true,
	                                                 objectRecord,
	                                                 SLIP_TRACK_OBJECT_LIST_CONTINUATION_TOKEN,
	                                                 true};
	return true;
}

bool SlipTrackWorld_ObjectListAdvance(uintptr_t currentRecordAddress, uint32_t remainingCountBefore,
                                      SlipTrackWorldObjectListAdvance *result) {
	uint32_t remainingCountAfter;

	if (result == 0) {
		return false;
	}
	remainingCountAfter = remainingCountBefore - 1u;
	*result = (SlipTrackWorldObjectListAdvance){
	    currentRecordAddress, remainingCountBefore, currentRecordAddress + SLIP_TRACK_OBJECT_LIST_RECORD_BYTES,
	    remainingCountAfter,
	    remainingCountAfter != 0 ? SLIP_TRACK_WORLD_OBJECT_LIST_CONTINUE : SLIP_TRACK_WORLD_OBJECT_LIST_FINISHED};
	return true;
}

bool SlipTrackWorld_ObjectListFinalize(uint32_t componentRecord, uint32_t currentComponentToken,
                                       SlipTrackWorldObjectListFinalize *result) {
	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldObjectListFinalize){
	    componentRecord,
	    currentComponentToken,
	    componentRecord == currentComponentToken ? SLIP_TRACK_WORLD_OBJECT_LIST_CURRENT_MATCHED
	                                             : SLIP_TRACK_WORLD_OBJECT_LIST_CURRENT_DIFFERENT,
	    componentRecord == currentComponentToken ? SLIP_TRACK_MATCHED_COMPONENT_LIMIT_START : 0,
	    componentRecord == currentComponentToken ? SLIP_TRACK_MATCHED_COMPONENT_LIMIT_END : 0,
	    componentRecord == currentComponentToken,
	    true,
	    true,
	    true,
	    true,
	    true};
	return true;
}

SlipObject *SlipObject_table;
uint16_t SlipObject_count;
static SlipObject *g_objectActiveHead;
static SlipObject *g_objectActiveTail;
static uint32_t g_objectLockDepth;
static uint16_t objectResource;

void SlipObject_ExhaustedMatrix(SlipView3DMatrix *matrix) {
	const SlipResourceBlock *const block = SlipResource_handles[objectResource / SLIP_RESOURCE_DOS_HANDLE_BYTES].block;
	if (block->capacityBytes != (uint32_t)SlipObject_count * SLIP_OBJECT_DOS_STRIDE || block->physicalNext == NULL)
		SlipRuntime_Fatal("Object pool end is not represented by the next resource header");
	SlipResourceStorage_HeaderMatrix(block->physicalNext, matrix);
}

static uint32_t g_objectDeferredList;
static uint16_t g_objectDeferredSlots[SLIP_OBJECT_COUNT];

typedef struct SlipObjectServer {
	uint16_t id;
	SlipObjectEventCallback callback;
} SlipObjectServer;

static SlipObjectServer g_objectServers[SLIP_OBJECT_SERVER_COUNT];

static uint16_t SlipTrackWorld_ObjectId(const SlipObject *object) {
	return (uint16_t)((size_t)(object - SlipObject_table) * SLIP_OBJECT_DOS_STRIDE);
}

static SlipObject *SlipTrackWorld_ObjectFromId(uint16_t objectId) {
	if (SlipObject_table == NULL || objectId % SLIP_OBJECT_DOS_STRIDE != 0)
		return NULL;
	const size_t objectIndex = objectId / SLIP_OBJECT_DOS_STRIDE;
	return objectIndex < SlipObject_count ? &SlipObject_table[objectIndex] : NULL;
}

static SlipObject *SlipTrackWorld_ObjectFromTable(SlipObject *objectTable, size_t dosTableBytes, size_t objectId) {
	if (objectTable == NULL || objectId > UINT16_MAX || objectId % SLIP_OBJECT_DOS_STRIDE != 0 ||
	    objectId + SLIP_OBJECT_DOS_STRIDE > dosTableBytes)
		return NULL;
	return &objectTable[objectId / SLIP_OBJECT_DOS_STRIDE];
}

static const SlipObject *SlipTrackWorld_ConstObjectFromTable(const SlipObject *objectTable, size_t dosTableBytes,
                                                             size_t objectId) {
	return SlipTrackWorld_ObjectFromTable((SlipObject *)objectTable, dosTableBytes, objectId);
}

void SlipObject_SetServer(uint16_t serverId, SlipObjectEventCallback callback) {
	SlipObjectServer *server = g_objectServers;
	uint32_t remaining = SLIP_OBJECT_SERVER_COUNT;

	do {
		if (server->id != 0 && server->callback == callback) {
			server->id = serverId;
			return;
		}
		++server;
		--remaining;
	} while (remaining != 0);
	server = g_objectServers;
	remaining = SLIP_OBJECT_SERVER_COUNT;
	do {
		if (server->id == 0) {
			server->id = serverId;
			server->callback = callback;
			return;
		}
		++server;
		--remaining;
	} while (remaining != 0);
	SlipRuntime_Fatal("SlotsAddServer: No room.");
}

uint32_t SlipObject_DispatchEvent(uint16_t objectOffset, uint32_t eventValue, uint32_t primaryPayload,
                                  uint32_t secondaryPayload, uint32_t auxiliaryPayload, uintptr_t contextToken,
                                  uint32_t contextValue) {
	if (objectOffset == UINT16_MAX) {
		SlipObject *object = SlipObject_table;
		uint32_t broadcastRemainingValue = (auxiliaryPayload & SLIP_TRACK_REGISTER_UPPER_WORD_MASK) | SlipObject_count;

		do {
			if (object->allocated != 0 && object->eventCallback != NULL) {
				object->eventCallback(eventValue, primaryPayload, secondaryPayload, broadcastRemainingValue,
				                      SlipTrackWorld_ObjectId(object), contextToken, contextValue);
			}
			++object;
			broadcastRemainingValue = (broadcastRemainingValue & SLIP_TRACK_REGISTER_UPPER_WORD_MASK) |
			                          (uint16_t)((uint16_t)broadcastRemainingValue - 1u);
		} while ((uint16_t)broadcastRemainingValue != 0);
		return eventValue;
	} else {
		SlipObject *const object = SlipTrackWorld_ObjectFromId(objectOffset);

		if (object != NULL && object->eventCallback != NULL) {
			return object->eventCallback(eventValue, primaryPayload, secondaryPayload, auxiliaryPayload, objectOffset,
			                             contextToken, contextValue);
		}
		return eventValue;
	}
}

void SlipObject_DispatchUpdate(uintptr_t contextToken, uint32_t contextValue) {
	uint32_t remaining = SlipObject_count;
	SlipObject *object = SlipObject_table;

	do {
		if (object->allocated != 0 && object->eventCallback != NULL) {
			SlipFrameTimerValues timer = SlipFrameTimer_Values();

			SlipObject_DispatchEvent(
			    SlipTrackWorld_ObjectId(object),
			    (timer.deltaMilliseconds & SLIP_TRACK_REGISTER_UPPER_WORD_MASK) | SLIP_OBJECT_EVENT_UPDATE,
			    (uint16_t)timer.stepQ14, (uint16_t)timer.deltaMilliseconds, 0, contextToken, contextValue);
		}
		++object;
		--remaining;
	} while (remaining != 0);
}

void SlipObject_DispatchPostUpdate(uint32_t eventValue, uint32_t primaryPayload, uint32_t secondaryPayload,
                                   uint32_t auxiliaryPayload, uintptr_t contextToken, uint32_t contextValue) {
	SlipObject *object;

	SlipObject_BeginDeferredSection();
	object = g_objectActiveHead;
	while (object != 0) {
		if (object->eventCallback != NULL) {
			SlipFrameTimerValues timer = SlipFrameTimer_Values();

			primaryPayload = (uint16_t)timer.stepQ14;
			secondaryPayload = (uint16_t)timer.deltaMilliseconds;
			auxiliaryPayload = 0;
			eventValue = SlipObject_DispatchEvent(
			    SlipTrackWorld_ObjectId(object),
			    (timer.deltaMilliseconds & SLIP_TRACK_REGISTER_UPPER_WORD_MASK) | SLIP_OBJECT_EVENT_HANDLE_ACTION,
			    primaryPayload, secondaryPayload, auxiliaryPayload, contextToken, contextValue);
		}
		object = object->next;
	}
	SlipObject_EndDeferredSection(eventValue, primaryPayload, secondaryPayload, auxiliaryPayload, contextToken,
	                              contextValue);
}

uint32_t SlipObject_Stop(uint16_t objectOffset, uint32_t eventValue, uint32_t primaryPayload, uint32_t secondaryPayload,
                         uint32_t auxiliaryPayload, uintptr_t contextToken, uint32_t contextValue) {
	SlipObject *const object = SlipTrackWorld_ObjectFromId(objectOffset);

	if (object != NULL)
		object->speed = 0;
	return SlipObject_DispatchEvent(objectOffset,
	                                (eventValue & SLIP_TRACK_REGISTER_UPPER_WORD_MASK) | SLIP_OBJECT_EVENT_RESET_MOTION,
	                                primaryPayload, secondaryPayload, auxiliaryPayload, contextToken, contextValue);
}

void SlipObject_BeginDeferredSection(void) {
	++g_objectLockDepth;
	if (g_objectLockDepth == 1) {
		g_objectDeferredList = 0;
	}
}

void SlipObject_FreeImmediate(uint16_t objectOffset, uint32_t eventValue, uint32_t primaryPayload,
                              uint32_t secondaryPayload, uint32_t auxiliaryPayload, uintptr_t contextToken,
                              uint32_t contextValue) {
	const uint32_t savedLockDepth = g_objectLockDepth;
	g_objectLockDepth = 0;
	SlipObject_Free(objectOffset, eventValue, primaryPayload, secondaryPayload, auxiliaryPayload, contextToken,
	                contextValue);
	g_objectLockDepth = savedLockDepth;
}

void SlipObject_Free(uint16_t objectOffset, uint32_t eventValue, uint32_t primaryPayload, uint32_t secondaryPayload,
                     uint32_t auxiliaryPayload, uintptr_t contextToken, uint32_t contextValue) {
	SlipObject *object;
	SlipObject *next;
	SlipObject *previous;
	uint32_t nextLink;
	uint32_t previousLink;
	uint32_t serverIndex;
	bool wasHead;
	bool wasTail;

	if (g_objectLockDepth != 0) {
		g_objectDeferredSlots[g_objectDeferredList] = objectOffset;
		++g_objectDeferredList;
		if (g_objectDeferredList >= SLIP_OBJECT_COUNT) {
			SlipRuntime_Fatal("SlotFree: Deferred list full.");
		}
		return;
	}

	eventValue = SlipObject_DispatchEvent(
	    objectOffset, (eventValue & SLIP_TRACK_REGISTER_UPPER_WORD_MASK) | SLIP_OBJECT_EVENT_FREE, primaryPayload,
	    secondaryPayload, auxiliaryPayload, contextToken, contextValue);
	for (serverIndex = 0; serverIndex < SLIP_OBJECT_SERVER_COUNT; ++serverIndex) {
		const SlipObjectServer *const server = &g_objectServers[serverIndex];

		if (server->id != 0 && server->callback != NULL) {
			eventValue = server->callback(
			    (eventValue & SLIP_TRACK_REGISTER_UPPER_WORD_MASK) | SLIP_OBJECT_SERVER_EVENT_FREE,
			    SLIP_OBJECT_SERVER_TABLE_TOKEN + serverIndex * SLIP_OBJECT_SERVER_DOS_STRIDE,
			    SLIP_OBJECT_SERVER_COUNT - serverIndex, auxiliaryPayload, objectOffset, contextToken, contextValue);
		}
	}

	object = SlipTrackWorld_ObjectFromId(objectOffset);
	if (object == NULL)
		return;
	object->allocated = 0;
	next = object->next;
	previous = object->previous;
	nextLink = next != NULL ? SlipTrackWorld_ObjectId(next) + 1u : 0;
	previousLink = previous != NULL ? SlipTrackWorld_ObjectId(previous) + 1u : 0;
	wasHead = object == g_objectActiveHead;
	wasTail = object == g_objectActiveTail;
	if (wasHead)
		g_objectActiveHead = next;
	if (wasTail)
		g_objectActiveTail = previous;
	if (previous != 0)
		previous->next = next;
	if (next != 0)
		next->previous = previous;
	SlipObject_DispatchEvent(UINT16_MAX, (nextLink & SLIP_TRACK_REGISTER_UPPER_WORD_MASK) | SLIP_OBJECT_EVENT_FREED,
	                         previousLink, 0, auxiliaryPayload, contextToken, contextValue);
}

void SlipObject_EndDeferredSection(uint32_t eventValue, uint32_t primaryPayload, uint32_t secondaryPayload,
                                   uint32_t auxiliaryPayload, uintptr_t contextToken, uint32_t contextValue) {
	if (g_objectLockDepth != 0) {
		--g_objectLockDepth;
		if (g_objectLockDepth == 0) {
			uint32_t deferredCount = g_objectDeferredList;
			uint32_t deferredAddress = SLIP_OBJECT_DEFERRED_SLOTS_TOKEN;
			uint32_t deferredIndex = 0;

			while (deferredCount != 0) {
				SlipObject_Free(g_objectDeferredSlots[deferredIndex], eventValue, primaryPayload, deferredCount,
				                auxiliaryPayload, deferredAddress, contextValue);
				deferredAddress += SLIP_OBJECT_DEFERRED_SLOT_BYTES;
				++deferredIndex;
				--deferredCount;
			}
		}
	}
}

bool SlipObject_IsLive(uint16_t object) {
	const SlipObject *const slot = SlipTrackWorld_ObjectFromId(object);
	if (slot == NULL || slot->allocated == 0)
		return false;
	if (g_objectLockDepth != 0) {
		for (uint32_t index = 0; index < g_objectDeferredList; ++index) {
			if (g_objectDeferredSlots[index] == object)
				return false;
		}
	}
	return true;
}

uint16_t SlipObject_Next(uint16_t previousObjectOffset) {
	const int32_t signedOffset = (int16_t)previousObjectOffset;
	size_t objectOffset;

	if (SlipObject_table == NULL || SlipObject_count < 2u) {
		return UINT16_MAX;
	}
	if (signedOffset < 0) {
		objectOffset = SLIP_OBJECT_DOS_STRIDE;
	} else {
		objectOffset = (size_t)(uint16_t)signedOffset + SLIP_OBJECT_DOS_STRIDE;
	}
	for (;;) {
		uint32_t deferredIndex;
		bool deferredObject = false;

		if (objectOffset / SLIP_OBJECT_DOS_STRIDE >= SlipObject_count)
			return UINT16_MAX;
		if (SlipTrackWorld_ObjectFromId((uint16_t)objectOffset)->allocated == 0) {
			objectOffset += SLIP_OBJECT_DOS_STRIDE;
			continue;
		}
		if (g_objectLockDepth == 0)
			return (uint16_t)objectOffset;
		deferredIndex = g_objectDeferredList;
		while (deferredIndex != 0) {
			--deferredIndex;
			if (g_objectDeferredSlots[deferredIndex] == (uint16_t)objectOffset) {
				deferredObject = true;
				objectOffset += SLIP_OBJECT_DOS_STRIDE;
				break;
			}
		}
		if (!deferredObject)
			return (uint16_t)objectOffset;
	}
}

SlipView3DVec32 SlipObject_ExtrapolatedPosition(uint16_t objectHandle) {
	const SlipObject *const object = SlipTrackWorld_ObjectFromId(objectHandle);
	SlipView3DVec32 result;
	const int32_t speed = object->speed;

	if (speed != 0) {
		const SlipFrameTimerValues timer = SlipFrameTimer_Values();
		const uint16_t frameStep = (uint16_t)timer.stepQ14;
		const int32_t distance = (int32_t)(((int64_t)speed * frameStep) >> SLIP_Q14_FRACTION_BITS);

		result.y = (int32_t)((uint32_t)object->position.y +
		                     (uint32_t)(int32_t)(((int64_t)object->direction.y * distance) >> SLIP_Q14_FRACTION_BITS));
		result.z = (int32_t)((uint32_t)object->position.z +
		                     (uint32_t)(int32_t)(((int64_t)object->direction.z * distance) >> SLIP_Q14_FRACTION_BITS));
		result.x = (int32_t)((uint32_t)object->position.x +
		                     (uint32_t)(int32_t)(((int64_t)object->direction.x * distance) >> SLIP_Q14_FRACTION_BITS));
	} else {
		result = object->position;
	}
	return result;
}

bool SlipTrack_StartRecord(const uint8_t *trkBase, size_t trkSize, uint16_t startRecordIndex,
                           SlipTrackStartRecord *result) {
	size_t recordOffset;

	if (trkBase == 0 || result == 0) {
		return false;
	}
	recordOffset = (size_t)(uint16_t)(SLIP_TRK_POSITION_BYTES * startRecordIndex);
	if (recordOffset + SLIP_TRK_START_POSITIONS_OFFSET + SLIP_TRK_POSITION_BYTES > trkSize) {
		return false;
	}
	*result = (SlipTrackStartRecord){
	    .recordOffset = recordOffset,
	    .startPositionX = SlipBytes_ReadLE32(trkBase + recordOffset + SLIP_TRK_START_POSITIONS_OFFSET),
	    .startPositionY =
	        SlipBytes_ReadLE32(trkBase + recordOffset + SLIP_TRK_START_POSITIONS_OFFSET + SLIP_TRK_POSITION_Y_OFFSET),
	    .startPositionZ =
	        SlipBytes_ReadLE32(trkBase + recordOffset + SLIP_TRK_START_POSITIONS_OFFSET + SLIP_TRK_POSITION_Z_OFFSET),
	    .returned = true};
	return true;
}

bool SlipTrack_StartRecordAlternate(const uint8_t *trkBase, size_t trkSize, uint16_t startRecordIndex,
                                    SlipTrackStartRecord *result) {
	size_t recordOffset;

	if (trkBase == 0 || result == 0) {
		return false;
	}
	recordOffset = (size_t)(uint16_t)(SLIP_TRK_POSITION_BYTES * startRecordIndex);
	if (recordOffset + SLIP_TRK_ALTERNATE_START_POSITIONS_OFFSET + SLIP_TRK_POSITION_BYTES > trkSize) {
		return false;
	}
	*result = (SlipTrackStartRecord){
	    .recordOffset = recordOffset,
	    .startPositionX = SlipBytes_ReadLE32(trkBase + recordOffset + SLIP_TRK_ALTERNATE_START_POSITIONS_OFFSET),
	    .startPositionY = SlipBytes_ReadLE32(trkBase + recordOffset + SLIP_TRK_ALTERNATE_START_POSITIONS_OFFSET +
	                                         SLIP_TRK_POSITION_Y_OFFSET),
	    .startPositionZ = SlipBytes_ReadLE32(trkBase + recordOffset + SLIP_TRK_ALTERNATE_START_POSITIONS_OFFSET +
	                                         SLIP_TRK_POSITION_Z_OFFSET),
	    .returned = true};
	return true;
}

bool SlipTrack_StartHeading(const uint8_t *record, size_t recordBytes, SlipTrackStartHeading *result) {
	if (record == 0 || result == 0 || recordBytes < SLIP_TRK_START_HEADING_END) {
		return false;
	}
	*result = (SlipTrackStartHeading){.headingXQ14 = SlipBytes_ReadLE16(record + SLIP_TRK_START_HEADING_X_OFFSET),
	                                  .headingYQ14 = SlipBytes_ReadLE16(record + SLIP_TRK_START_HEADING_Y_OFFSET),
	                                  .headingZQ14 = SlipBytes_ReadLE16(record + SLIP_TRK_START_HEADING_Z_OFFSET),
	                                  .returned = true};
	return true;
}

bool SlipObject_SetDrawCallback(uint16_t objectOffset, SlipObjectDrawCallback drawCallback, uint32_t drawCallbackData,
                                SlipObjectSetCallback *result) {
	SlipObject *object;

	if (result == 0 || SlipObject_table == 0) {
		return false;
	}
	object = SlipTrackWorld_ObjectFromId(objectOffset);
	if (object == NULL) {
		return false;
	}
	object->drawCallback = drawCallback;
	object->drawCallbackData = drawCallbackData;
	*result = (SlipObjectSetCallback){.objectOffset = objectOffset,
	                                  .drawCallback = drawCallback,
	                                  .drawCallbackData = drawCallbackData,
	                                  .returned = true};
	return true;
}

bool SlipObject_SetTrackSlot(SlipObject *objectTableBase, size_t objectTableSize, uint16_t objectOffset,
                             uint16_t trackSlotOffset) {
	if (objectTableBase == NULL || (size_t)objectOffset + SLIP_OBJECT_DOS_TRACK_SLOT_END > objectTableSize ||
	    objectOffset % SLIP_OBJECT_DOS_STRIDE != 0) {
		return false;
	}
	objectTableBase[objectOffset / SLIP_OBJECT_DOS_STRIDE].trackSlotOffset = trackSlotOffset;
	return true;
}

uint8_t *SlipObject_PrivateState(uint16_t objectOffset) {
	SlipObject *const object = SlipTrackWorld_ObjectFromId(objectOffset);
	return object != NULL ? object->privateState : NULL;
}

SlipTimedEffectObjectState *SlipObject_TimedEffectState(uint16_t objectOffset) {
	SlipObject *const object = SlipTrackWorld_ObjectFromId(objectOffset);
	return object != NULL ? &object->timedEffect : NULL;
}

SlipCrossEffectState *SlipObject_CrossEffectState(uint16_t objectOffset) {
	SlipObject *const object = SlipTrackWorld_ObjectFromId(objectOffset);
	return object != NULL ? &object->crossEffect : NULL;
}

SlipAnimatedState *SlipObject_AnimatedState(uint16_t objectOffset) {
	SlipObject *const object = SlipTrackWorld_ObjectFromId(objectOffset);
	return object != NULL ? &object->animatedEffect : NULL;
}

uint32_t SlipObject_GetActorHandle(uint16_t objectOffset) {
	const SlipObject *const object = SlipTrackWorld_ObjectFromId(objectOffset);
	return object != NULL ? object->actorHandle : 0;
}

bool SlipObject_SetActorHandle(uint16_t objectOffset, uint32_t actorHandle, SlipObjectActorHandleWriteResult *result) {
	SlipObject *object;

	if (result == 0 || SlipObject_table == 0) {
		return false;
	}
	object = SlipTrackWorld_ObjectFromId(objectOffset);
	if (object == NULL) {
		return false;
	}
	object->actorHandle = actorHandle;
	*result =
	    (SlipObjectActorHandleWriteResult){.objectOffset = objectOffset, .actorHandle = actorHandle, .returned = true};
	return true;
}

bool SlipObject_SlotAllocate(SlipObjectSlotAllocate *result) {
	SlipObject *object;
	uint16_t remaining;
	size_t i;

	if (result == 0 || SlipObject_table == 0) {
		return false;
	}
	*result = (SlipObjectSlotAllocate){0};

	remaining = SlipObject_count;
	object = SlipObject_table;
	for (i = 0; i < remaining; ++i) {
		result->scannedSlots = (uint16_t)(i + 1u);
		if (object->allocated == 0) {
			break;
		}
		++object;
	}
	if (i >= remaining) {

		result->exhausted = true;
		SlipRuntime_error = SLIP_RUNTIME_ERROR_CAPACITY_EXHAUSTED;
		result->errorCode = SLIP_RUNTIME_ERROR_CAPACITY_EXHAUSTED;
		result->carryOut = true;
		result->returned = true;
		return true;
	}

	*object = (SlipObject){.allocated = 1,
	                       .position = object->position,
	                       .viewPosition = object->viewPosition,
	                       .matrix = object->matrix,
	                       .direction = object->direction};

	if (g_objectActiveHead != 0) {
		result->previousTailOffset = SlipTrackWorld_ObjectId(g_objectActiveTail);
		g_objectActiveTail->next = object;
		object->previous = g_objectActiveTail;
		g_objectActiveTail = object;
	} else {
		result->firstRecord = true;
		g_objectActiveHead = object;
		g_objectActiveTail = object;
		object->previous = NULL;
	}

	result->objectOffset = SlipTrackWorld_ObjectId(object);
	result->carryOut = false;
	result->returned = true;
	return true;
}

bool SlipObject_SlotFill(const SlipView3DMatrix *savedTemplate, uint32_t x, uint32_t y, uint32_t z,
                         SlipObjectDrawCallback slotDrawCallback, uint32_t drawData,
                         SlipObjectEventCallback eventCallback, SlipObjectSlotFill *result) {
	SlipObjectSlotAllocate allocate;
	SlipObject *object;

	if (savedTemplate == 0 || result == 0 || SlipObject_table == 0) {
		return false;
	}
	*result = (SlipObjectSlotFill){0};
	result->savedTemplate = savedTemplate;

	result->callAllocateObject = true;
	if (!SlipObject_SlotAllocate(&allocate)) {
		return false;
	}
	result->allocate = allocate;
	if (allocate.carryOut) {
		result->carryOut = true;
		result->returned = true;
		return true;
	}

	result->objectOffset = allocate.objectOffset;
	object = SlipTrackWorld_ObjectFromId((uint16_t)result->objectOffset);

	object->position = (SlipView3DVec32){(int32_t)x, (int32_t)y, (int32_t)z};
	object->eventCallback = eventCallback;
	object->drawData = drawData;
	object->slotDrawCallback = slotDrawCallback;
	result->positionX = x;
	result->positionY = y;
	result->positionZ = z;
	result->eventCallback = eventCallback;
	result->drawData = drawData;
	result->slotDrawCallback = slotDrawCallback;

	result->directionXQ14 = (uint16_t)savedTemplate->m[6];
	result->directionYQ14 = (uint16_t)savedTemplate->m[7];
	result->directionZQ14 = (uint16_t)savedTemplate->m[8];
	object->direction = (SlipView3DVec16){(int16_t)result->directionXQ14, (int16_t)result->directionYQ14,
	                                      (int16_t)result->directionZQ14};

	object->matrix = *savedTemplate;
	result->copyMatrix = true;

	result->initializeEventCode = SLIP_OBJECT_EVENT_INITIALIZE;
	result->callObjectDispatchEvent = true;
	(void)SlipObject_DispatchEvent((uint16_t)result->objectOffset,
	                               (x & SLIP_TRACK_REGISTER_UPPER_WORD_MASK) | SLIP_OBJECT_EVENT_INITIALIZE, y, z, 0, 0,
	                               drawData);
	result->carryOut = false;
	result->returned = true;
	return true;
}

bool SlipObject_Block(uint32_t objectHandleBeforeMask, uint32_t objectTableToken,
                      SlipObjectMatrixBindingResult *result) {
	uint32_t objectOffset;
	uint32_t objectAddress;

	if (result == 0) {
		return false;
	}
	objectOffset = objectHandleBeforeMask & UINT16_MAX;
	objectAddress = objectOffset + objectTableToken;
	*result = (SlipObjectMatrixBindingResult){
	    objectHandleBeforeMask,        objectOffset, objectAddress, objectAddress + SLIP_OBJECT_DOS_MATRIX_OFFSET,
	    SLIP_OBJECT_DRAW_MATRIX_TOKEN, true,         true,          true};
	return true;
}

const SlipView3DMatrix *SlipObject_DrawMatrix(const SlipObject *objects, uint16_t object) {
	static SlipView3DMatrix drawMatrix;
	SlipView3D_ComposeMatrix(&objects[object / SLIP_OBJECT_DOS_STRIDE].matrix, &objects[0].matrix, &drawMatrix);
	return &drawMatrix;
}

bool SlipObject_MatrixCopy(const SlipObject *objectTableBase, size_t objectTableSize, uint32_t objectHandleBeforeMask,
                           SlipView3DMatrix *objectTransformMatrix, SlipObjectMatrixCopy *result) {
	size_t objectOffset;
	const SlipView3DMatrix *sourceMatrix;
	const SlipObject *object;

	if (objectTableBase == 0 || objectTransformMatrix == 0 || result == 0) {
		return false;
	}
	objectOffset = (size_t)(objectHandleBeforeMask & UINT16_MAX);
	object = SlipTrackWorld_ConstObjectFromTable(objectTableBase, objectTableSize, objectOffset);
	if (object == NULL) {
		return false;
	}
	sourceMatrix = &object->matrix;

	*objectTransformMatrix = *sourceMatrix;
	*result = (SlipObjectMatrixCopy){.objectHandleBeforeMask = objectHandleBeforeMask,
	                                 .objectOffset = objectOffset,
	                                 .sourceMatrix = sourceMatrix,
	                                 .destinationMatrix = objectTransformMatrix,
	                                 .callView3DCopyMatrixWords = true,
	                                 .restoredObjectHandle = true,
	                                 .returned = true};
	return true;
}

SlipObjectEventCallback SlipObject_Callback(uint16_t objectOffset) {
	const SlipObject *const object = SlipTrackWorld_ObjectFromId(objectOffset);
	return object != NULL ? object->eventCallback : NULL;
}

SlipObjectDirection SlipObject_Direction(const SlipObject *objectTableBase, uint16_t objectOffset) {
	const SlipObject *const object = &objectTableBase[objectOffset / SLIP_OBJECT_DOS_STRIDE];
	SlipObjectDirection result;

	result.directionXQ14 = object->direction.x;
	result.directionYQ14 = object->direction.y;
	result.directionZQ14 = object->direction.z;
	return result;
}

void SlipObject_SetDirectionQ14(SlipObject *objectTableBase, uint16_t objectOffset, uint16_t x, uint16_t y,
                                uint16_t z) {
	SlipObject *const object = &objectTableBase[objectOffset / SLIP_OBJECT_DOS_STRIDE];
	object->direction = (SlipView3DVec16){(int16_t)x, (int16_t)y, (int16_t)z};
}

void SlipObject_CopyMatrixForwardToDirection(SlipObject *objectTableBase, uint16_t objectOffset) {
	SlipObject *const object = &objectTableBase[objectOffset / SLIP_OBJECT_DOS_STRIDE];

	object->direction = (SlipView3DVec16){object->matrix.m[6], object->matrix.m[7], object->matrix.m[8]};
}

uint16_t SlipObject_PhysicsOffset(const SlipObject *objectTableBase, uint16_t objectOffset) {
	return objectTableBase[objectOffset / SLIP_OBJECT_DOS_STRIDE].physicsRecordOffset;
}

void SlipObject_SetPhysicsOffset(SlipObject *objectTableBase, uint16_t objectOffset, uint16_t physicsRecordOffset) {
	objectTableBase[objectOffset / SLIP_OBJECT_DOS_STRIDE].physicsRecordOffset = physicsRecordOffset;
}

int32_t SlipObject_Speed(const SlipObject *objectTableBase, uint16_t objectOffset) {
	return objectTableBase[objectOffset / SLIP_OBJECT_DOS_STRIDE].speed;
}

void SlipObject_SetSpeed(SlipObject *objectTableBase, uint16_t objectOffset, uint32_t speed) {
	objectTableBase[objectOffset / SLIP_OBJECT_DOS_STRIDE].speed = (int32_t)speed;
}

SlipView3DVec32 SlipObject_Velocity(const SlipObject *objectTableBase, uint16_t objectOffset) {
	const SlipObject *const object = &objectTableBase[objectOffset / SLIP_OBJECT_DOS_STRIDE];
	const int32_t magnitude = object->speed;
	SlipView3DVec32 result;

	result.x = (int32_t)(((uint64_t)((int64_t)object->direction.x * magnitude)) >> SLIP_Q14_FRACTION_BITS);
	result.y = (int32_t)(((uint64_t)((int64_t)object->direction.y * magnitude)) >> SLIP_Q14_FRACTION_BITS);
	result.z = (int32_t)(((uint64_t)((int64_t)object->direction.z * magnitude)) >> SLIP_Q14_FRACTION_BITS);
	return result;
}

static uint16_t SlipTrackWorld_SlipObjectInvalidateViewPositions(SlipObject *objectTableBase, size_t objectTableSize) {
	const size_t objectCount = objectTableSize / SLIP_OBJECT_DOS_STRIDE;
	uint16_t clearedFlagSlots = 0;
	size_t objectIndex;

	for (objectIndex = 1; objectIndex < objectCount; ++objectIndex) {
		objectTableBase[objectIndex].flags &= ~SLIP_OBJECT_VIEW_POSITION_VALID;
		++clearedFlagSlots;
	}
	return clearedFlagSlots;
}

bool SlipObject_Rotate(SlipObject *objectTableBase, size_t objectTableSize, uint32_t objectHandle, int16_t rotationX,
                       int16_t rotationY, int16_t rotationZ, int16_t rotationMode, const SlipView3DMaths *maths,
                       SlipObjectRotate *result) {
	size_t objectOffset;
	SlipView3DMatrix matrix;
	SlipObject *object;

	if (objectTableBase == NULL || maths == NULL || result == NULL) {
		return false;
	}
	objectOffset = (size_t)(objectHandle & UINT16_MAX);
	object = SlipTrackWorld_ObjectFromTable(objectTableBase, objectTableSize, objectOffset);
	if (object == NULL) {
		return false;
	}
	if (objectOffset == 0) {
		(void)SlipTrackWorld_SlipObjectInvalidateViewPositions(objectTableBase, objectTableSize);
	}
	matrix = object->matrix;
	if (rotationX != 0) {
		SlipView3D_ApplyPitchMatrix(maths, rotationX, &matrix);
	}
	if (rotationY != 0) {
		SlipView3D_ApplyRow0Row1Rotation(maths, rotationY, &matrix);
	}
	if (rotationZ != 0) {
		SlipView3D_ApplyRow0Row2Rotation(maths, rotationZ, &matrix);
	}
	if (rotationMode != 0) {
		SlipView3D_ApplyColumn0Column2Rotation(maths, rotationMode, &matrix);
	}
	SlipView3D_OrthonormalizeForwardBasis(&matrix);
	object->matrix = matrix;
	*result = (SlipObjectRotate){.objectOffset = objectOffset,
	                             .callTrackWorldSlipObjectInvalidateViewPositions = objectOffset == 0,
	                             .callView3DApplyPitchMatrix = rotationX != 0,
	                             .callView3DApplyRow0Row1Rotation = rotationY != 0,
	                             .callView3DApplyRow0Row2Rotation = rotationZ != 0,
	                             .callView3DApplyColumn0Column2Rotation = rotationMode != 0,
	                             .callView3DOrthonormalizeForwardBasis = true};
	return true;
}

bool SlipObject_SetDirection(SlipObject *objectTableBase, size_t objectTableSize, uint32_t objectHandle,
                             int32_t directionX, int32_t directionY, int32_t directionZ,
                             SlipObjectSetDirection *result) {
	size_t objectOffset;
	uint16_t x;
	uint16_t y;
	uint16_t z;
	uint32_t magnitude;
	bool zeroVector;
	SlipView3DNormalizeVector3D normalized;
	SlipObject *object;

	if (objectTableBase == NULL || result == NULL) {
		return false;
	}
	objectOffset = (size_t)(objectHandle & UINT16_MAX);
	object = SlipTrackWorld_ObjectFromTable(objectTableBase, objectTableSize, objectOffset);
	if (object == NULL) {
		return false;
	}
	zeroVector = ((uint32_t)directionX | (uint32_t)directionY | (uint32_t)directionZ) == 0;
	if (zeroVector) {
		x = (uint16_t)object->matrix.m[6];
		y = (uint16_t)object->matrix.m[7];
		z = (uint16_t)object->matrix.m[8];
		magnitude = 0;
	} else {
		if (!SlipView3D_NormalizeVector3D((uint32_t)directionX, (uint32_t)directionY, (uint32_t)directionZ,
		                                  &normalized)) {
			return false;
		}
		x = (uint16_t)normalized.unitXQ14;
		y = (uint16_t)normalized.unitYQ14;
		z = (uint16_t)normalized.unitZQ14;
		magnitude = normalized.vectorLength;
	}
	object->direction = (SlipView3DVec16){(int16_t)x, (int16_t)y, (int16_t)z};
	object->speed = (int32_t)magnitude;
	*result = (SlipObjectSetDirection){.objectOffset = objectOffset,
	                                   .zeroVector = zeroVector,
	                                   .callView3DNormalizeVector3D = !zeroVector,
	                                   .directionX = x,
	                                   .directionY = y,
	                                   .directionZ = z,
	                                   .magnitude = magnitude};
	return true;
}

bool SlipObject_MatrixInstall(SlipObject *objectTableBase, size_t objectTableSize, uint32_t objectHandleBeforeMask,
                              const SlipView3DMatrix *savedSourceMatrix, SlipObjectMatrixInstall *result) {
	size_t objectOffset;
	uint16_t clearedFlagSlots = 0;
	SlipObject *object;

	if (objectTableBase == 0 || savedSourceMatrix == 0 || result == 0) {
		return false;
	}
	objectOffset = (size_t)(objectHandleBeforeMask & UINT16_MAX);
	object = SlipTrackWorld_ObjectFromTable(objectTableBase, objectTableSize, objectOffset);
	if (object == NULL) {
		return false;
	}
	if ((objectHandleBeforeMask & UINT16_MAX) == 0) {
		clearedFlagSlots = SlipTrackWorld_SlipObjectInvalidateViewPositions(objectTableBase, objectTableSize);
	}

	object->matrix = *savedSourceMatrix;
	*result = (SlipObjectMatrixInstall){.objectHandleBeforeMask = objectHandleBeforeMask,
	                                    .savedSourceMatrix = savedSourceMatrix,
	                                    .callTrackWorldSlipObjectInvalidateViewPositions =
	                                        (objectHandleBeforeMask & UINT16_MAX) == 0,
	                                    .clearedFlagSlots = clearedFlagSlots,
	                                    .objectOffset = objectOffset,
	                                    .sourceMatrix = savedSourceMatrix,
	                                    .destinationMatrix = &object->matrix,
	                                    .callView3DCopyMatrixWords = true,
	                                    .restoredSourceMatrix = true,
	                                    .restoredObjectHandle = true,
	                                    .returned = true};
	return true;
}

bool SlipObject_Position(const SlipObject *objectTableBase, size_t objectTableSize, uint32_t objectHandle,
                         SlipObjectPosition *result) {
	size_t objectOffset;
	const SlipObject *object;

	if (objectTableBase == 0 || result == 0) {
		return false;
	}
	objectOffset = (size_t)(objectHandle & UINT16_MAX);
	object = SlipTrackWorld_ConstObjectFromTable(objectTableBase, objectTableSize, objectOffset);
	if (object == NULL) {
		return false;
	}
	*result = (SlipObjectPosition){.objectId = objectHandle & UINT16_MAX,
	                               .objectOffset = objectOffset,
	                               .positionX = (uint32_t)object->position.x,
	                               .positionY = (uint32_t)object->position.y,
	                               .positionZ = (uint32_t)object->position.z,
	                               .returned = true};
	return true;
}

bool SlipObject_ViewPosition(SlipObject *objectTableBase, size_t objectTableSize, uint16_t objectOffset,
                             SlipView3DVec32 *viewPosition) {
	SlipObject *object;
	SlipObject *camera;
	SlipView3DVec32 relative;

	if (objectTableBase == NULL || viewPosition == NULL) {
		return false;
	}
	object = SlipTrackWorld_ObjectFromTable(objectTableBase, objectTableSize, objectOffset);
	camera = SlipTrackWorld_ObjectFromTable(objectTableBase, objectTableSize, 0);
	if (object == NULL || camera == NULL) {
		return false;
	}
	if ((object->flags & SLIP_OBJECT_VIEW_POSITION_VALID) == 0) {
		relative = (SlipView3DVec32){(int32_t)((uint32_t)object->position.x - (uint32_t)camera->position.x),
		                             (int32_t)((uint32_t)object->position.y - (uint32_t)camera->position.y),
		                             (int32_t)((uint32_t)object->position.z - (uint32_t)camera->position.z)};
		object->viewPosition = SlipView3D_TransformPositionByRows(&camera->matrix, relative);
		object->flags |= SLIP_OBJECT_VIEW_POSITION_VALID;
	}
	*viewPosition = object->viewPosition;
	return true;
}

bool SlipObject_SetPosition(SlipObject *objectTableBase, size_t objectTableSize, uint32_t objectHandleBeforeMask,
                            uint32_t x, uint32_t y, uint32_t z, SlipObjectSetPosition *result) {
	size_t objectOffset;
	uint16_t flagsBefore;
	uint16_t flagsAfter;
	SlipObject *object;

	if (objectTableBase == 0 || result == 0) {
		return false;
	}
	objectOffset = (size_t)(objectHandleBeforeMask & UINT16_MAX);
	object = SlipTrackWorld_ObjectFromTable(objectTableBase, objectTableSize, objectOffset);
	if (object == NULL) {
		return false;
	}
	flagsBefore = object->flags;
	flagsAfter = (uint16_t)(flagsBefore & ~SLIP_OBJECT_VIEW_POSITION_VALID);
	object->position = (SlipView3DVec32){(int32_t)x, (int32_t)y, (int32_t)z};
	object->flags = flagsAfter;
	if (objectOffset == 0) {
		(void)SlipTrackWorld_SlipObjectInvalidateViewPositions(objectTableBase, objectTableSize);
	}
	*result = (SlipObjectSetPosition){.objectHandleBeforeMask = objectHandleBeforeMask,
	                                  .maskedObjectHandle = objectHandleBeforeMask & UINT16_MAX,
	                                  .objectOffset = objectOffset,
	                                  .positionX = x,
	                                  .positionY = y,
	                                  .positionZ = z,
	                                  .flagsBefore = flagsBefore,
	                                  .flagsAfter = flagsAfter,
	                                  .callTrackWorldSlipObjectInvalidateViewPositions = objectOffset == 0,
	                                  .restoredObjectHandle = true,
	                                  .returned = true};
	return true;
}

void SlipObject_Hide(SlipObject *objectTableBase, uint16_t objectOffset) {
	objectTableBase[objectOffset / SLIP_OBJECT_DOS_STRIDE].flags |= SLIP_OBJECT_RENDER_HIDDEN;
}

void SlipObject_Show(SlipObject *objectTableBase, uint16_t objectOffset) {
	objectTableBase[objectOffset / SLIP_OBJECT_DOS_STRIDE].flags &= SLIP_OBJECT_RENDER_VISIBLE_MASK;
}

void SlipObject_ResetActiveList(uint32_t eventPayload, uint32_t eventFlags, uintptr_t dispatchData,
                                uint32_t dispatchFrame) {
	g_objectActiveHead = NULL;
	uint16_t remaining = SlipObject_count;
	uint16_t object = 0;
	do {
		if (SlipTrackWorld_ObjectFromId(object)->allocated != 0)
			SlipObject_Free(object, 0, eventPayload, remaining, eventFlags, dispatchData, dispatchFrame);
		object = (uint16_t)(object + SLIP_OBJECT_DOS_STRIDE);
	} while (--remaining != 0);
	SlipObjectSlotAllocate allocated;
	SlipObject_SlotAllocate(&allocated);
	SlipObject *const identity = SlipTrackWorld_ObjectFromId((uint16_t)allocated.objectOffset);
	identity->matrix = (SlipView3DMatrix){.m = {SLIP_Q14_ONE, 0, 0, 0, SLIP_Q14_ONE, 0, 0, 0, SLIP_Q14_ONE}};
	identity->position = (SlipView3DVec32){0, 0, 0};
	identity->flags = 0;
}

void SlipObject_Shutdown(void) {
	if (SlipObject_table != NULL) {
		SlipObject_EndDeferredSection(0, 0, 0, 0, 0, 0);
		SlipObject_ResetActiveList(0, 0, 0, 0);
		SlipResourceHost_Unlock(NULL, objectResource);
		SlipResourceHost_Release(NULL, objectResource);
		SlipObject_table = NULL;
	}
}

bool SlipObject_ResetActiveListFresh(SlipObject *objectTableBase, size_t objectTableSize, uint16_t objectCount,
                                     SlipObjectResetActiveList *result) {
	size_t objectTableBytes;

	if (objectTableBase == 0 || result == 0 || objectCount == 0) {
		return false;
	}
	objectTableBytes = (size_t)objectCount * SLIP_OBJECT_DOS_STRIDE;
	if (objectTableBytes > objectTableSize) {
		return false;
	}
	SlipObject_table = objectTableBase;
	SlipObject_count = objectCount;
	SlipObject_ResetActiveList(0, 0, 0, 0);

	*result = (SlipObjectResetActiveList){.savedAllocationInput = 0,
	                                      .objectCount = objectCount,
	                                      .clearedActiveRecords = objectCount,
	                                      .callAllocateObject = true,
	                                      .allocatedObjectOffset = 0,
	                                      .activeHeadOffset = 0,
	                                      .activeTailOffset = 0,
	                                      .carryOut = false,
	                                      .returned = true};
	return true;
}

bool SlipObject_InitTableFresh(SlipObject *objectTableBase, size_t objectTableSize, uint16_t objectCount,
                               SlipObjectInitTable *result) {
	size_t tableBytes;
	size_t i;
	SlipObjectResetActiveList reset;

	if (objectTableBase == 0 || result == 0 || objectCount == 0) {
		return false;
	}
	tableBytes = (size_t)objectCount * SLIP_OBJECT_DOS_STRIDE;
	if (tableBytes > objectTableSize) {
		return false;
	}

	SlipObject_Shutdown();
	SlipObject_count = objectCount;

	const uint32_t resourceBytes = (uint16_t)((uint32_t)objectCount * SLIP_OBJECT_DOS_STRIDE);
	if (!SlipResourceHost_Allocate(NULL, resourceBytes, 0, &objectResource))
		SlipRuntime_Fatal("SlotsInstall: Memory error.");

	(void)SlipResourceHost_LockReserved(NULL, objectResource);
	SlipObject_table = objectTableBase;
	SlipRuntime_RegisterExit(SlipObject_Shutdown);
	for (i = 0; i < objectCount; ++i) {
		objectTableBase[i].allocated = 0;
	}
	if (!SlipObject_ResetActiveListFresh(objectTableBase, objectTableSize, objectCount, &reset)) {
		return false;
	}

	g_objectLockDepth = 0;

	*result = (SlipObjectInitTable){.objectCount = objectCount,
	                                .tableBytes = tableBytes,
	                                .zeroedRecords = objectCount,
	                                .callResetActiveList = true,
	                                .reset = reset,
	                                .returned = true};
	return true;
}

void SlipObject_BindHostTable(SlipObject *objectTableBase, size_t objectTableSize, uint16_t objectCount) {
	SlipObject_table = objectTableBase;
	(void)objectTableSize;
	SlipObject_count = objectCount;
}

bool SlipObject_SetSlotDrawCallback(SlipObject *objectTableBase, size_t objectTableSize,
                                    uint32_t objectHandleBeforeMask, SlipObjectDrawCallback slotDrawCallback,
                                    SlipObjectDrawCallbackWriteResult *result) {
	size_t objectOffset;
	SlipObject *object;

	if (objectTableBase == 0 || result == 0) {
		return false;
	}
	objectOffset = (size_t)(objectHandleBeforeMask & UINT16_MAX);
	object = SlipTrackWorld_ObjectFromTable(objectTableBase, objectTableSize, objectOffset);
	if (object == NULL) {
		return false;
	}
	object->slotDrawCallback = slotDrawCallback;
	*result = (SlipObjectDrawCallbackWriteResult){
	    objectHandleBeforeMask, objectHandleBeforeMask & UINT16_MAX, objectOffset, slotDrawCallback, true, true};
	return true;
}

bool SlipObject_GetSlotDrawCallback(const SlipObject *objectTableBase, size_t objectTableSize, uint32_t objectHandle,
                                    SlipObjectDrawCallbackReadResult *result) {
	size_t objectOffset;
	SlipObjectDrawCallback slotDrawCallback;
	const SlipObject *object;

	if (objectTableBase == 0 || result == 0) {
		return false;
	}
	objectOffset = (size_t)(objectHandle & UINT16_MAX);
	object = SlipTrackWorld_ConstObjectFromTable(objectTableBase, objectTableSize, objectOffset);
	if (object == NULL) {
		return false;
	}
	slotDrawCallback = object->slotDrawCallback;
	*result = (SlipObjectDrawCallbackReadResult){objectHandle & UINT16_MAX, objectOffset, slotDrawCallback, true};
	return true;
}

bool SlipObject_SetDrawData(SlipObject *objectTableBase, size_t objectTableSize, uint32_t objectHandleBeforeMask,
                            uint32_t drawData, SlipObjectSlotDataWriteResult *result) {
	size_t objectOffset;
	SlipObject *object;

	if (objectTableBase == 0 || result == 0) {
		return false;
	}
	objectOffset = (size_t)(objectHandleBeforeMask & UINT16_MAX);
	object = SlipTrackWorld_ObjectFromTable(objectTableBase, objectTableSize, objectOffset);
	if (object == NULL) {
		return false;
	}
	object->drawData = drawData;
	*result = (SlipObjectSlotDataWriteResult){
	    objectHandleBeforeMask, objectHandleBeforeMask & UINT16_MAX, objectOffset, drawData, true, true};
	return true;
}

bool SlipObject_SetDrawExtent(SlipObject *objectTableBase, size_t objectTableSize, uint32_t objectHandleBeforeMask,
                              uint32_t drawExtent, SlipObjectExtentWriteResult *result) {
	size_t objectOffset;
	SlipObject *object;

	if (objectTableBase == 0 || result == 0) {
		return false;
	}
	objectOffset = (size_t)(objectHandleBeforeMask & UINT16_MAX);
	object = SlipTrackWorld_ObjectFromTable(objectTableBase, objectTableSize, objectOffset);
	if (object == NULL) {
		return false;
	}
	object->drawExtent = drawExtent;
	*result = (SlipObjectExtentWriteResult){
	    objectHandleBeforeMask, objectHandleBeforeMask & UINT16_MAX, objectOffset, drawExtent, true, true};
	return true;
}

bool SlipObject_SetEventCallback(SlipObject *objectTableBase, size_t objectTableSize, uint32_t objectHandleBeforeMask,
                                 SlipObjectEventCallback eventCallback, SlipObjectEventCallbackWriteResult *result) {
	size_t objectOffset;
	SlipObject *object;

	if (objectTableBase == 0 || result == 0) {
		return false;
	}
	objectOffset = (size_t)(objectHandleBeforeMask & UINT16_MAX);
	object = SlipTrackWorld_ObjectFromTable(objectTableBase, objectTableSize, objectOffset);
	if (object == NULL) {
		return false;
	}
	object->eventCallback = eventCallback;
	*result = (SlipObjectEventCallbackWriteResult){
	    objectHandleBeforeMask, objectHandleBeforeMask & UINT16_MAX, objectOffset, eventCallback, true, true};
	return true;
}

bool SlipObject_GetDrawData(const SlipObject *objectTableBase, size_t objectTableSize, uint32_t objectHandle,
                            SlipObjectSlotDataReadResult *result) {
	size_t objectOffset;
	uint32_t drawData;
	const SlipObject *object;

	if (objectTableBase == 0 || result == 0) {
		return false;
	}
	objectOffset = (size_t)(objectHandle & UINT16_MAX);
	object = SlipTrackWorld_ConstObjectFromTable(objectTableBase, objectTableSize, objectOffset);
	if (object == NULL) {
		return false;
	}
	drawData = (uint32_t)object->drawData;
	*result = (SlipObjectSlotDataReadResult){objectHandle & UINT16_MAX, objectOffset, drawData, true};
	return true;
}

bool SlipObject_GetDrawExtent(const SlipObject *objectTableBase, size_t objectTableSize, uint32_t objectHandle,
                              SlipObjectExtentReadResult *result) {
	size_t objectOffset;
	uint32_t drawExtent;
	const SlipObject *object;

	if (objectTableBase == 0 || result == 0) {
		return false;
	}
	objectOffset = (size_t)(objectHandle & UINT16_MAX);
	object = SlipTrackWorld_ConstObjectFromTable(objectTableBase, objectTableSize, objectOffset);
	if (object == NULL) {
		return false;
	}
	drawExtent = object->drawExtent;
	*result = (SlipObjectExtentReadResult){objectHandle & UINT16_MAX, objectOffset, drawExtent, true};
	return true;
}

bool SlipTrackWorld_ScheduleDrawCallback(SlipDraw3DListState *drawList, SlipDraw3DListNode *nodePool,
                                         size_t nodePoolBytes, uint32_t drawDepth, uint32_t drawRecordAddress,
                                         uint32_t slotDrawBaseToken, SlipTrackWorldDrawSchedule *result) {
	uint32_t drawRecordOffset;

	if (drawList == 0 || nodePool == 0 || result == 0) {
		return false;
	}
	drawRecordOffset = drawRecordAddress - slotDrawBaseToken;
	bool carryOut = !SlipDraw3D_ListInsert(drawList, nodePool, nodePoolBytes, drawDepth, TrackView_DrawSlotRecord,
	                                       drawRecordOffset);
	*result = (SlipTrackWorldDrawSchedule){.callGetObjectViewPosition = true,
	                                       .drawDepth = drawDepth,
	                                       .drawRecordAddress = drawRecordAddress,
	                                       .drawRecordOffset = drawRecordOffset,
	                                       .callback = TrackView_DrawSlotRecord,
	                                       .callDraw3DListInsert = true,
	                                       .carryOut = carryOut,
	                                       .returned = true};
	return true;
}

bool SlipTrackWorld_DrawCallbackHeader(const uint8_t *objectBase, size_t objectBaseBytes, uint16_t drawRecordOffset,
                                       SlipObjectDrawCallback slotDrawCallback, uint32_t componentBaseToken,
                                       SlipTrackWorldDrawCallbackHeader *result) {
	const uint8_t *record;
	uint32_t edgeReference;
	uint32_t cmp;

	if (objectBase == 0 || result == 0 || (size_t)drawRecordOffset > objectBaseBytes) {
		return false;
	}
	record = objectBase + drawRecordOffset;
	if (objectBaseBytes - (size_t)drawRecordOffset < SLIP_TRACK_DRAW_CALLBACK_END) {
		return false;
	}
	*result = (SlipTrackWorldDrawCallbackHeader){.savedDispatchContext = true,
	                                             .drawRecordOffset = drawRecordOffset,
	                                             .drawRecord = record,
	                                             .branch = SLIP_TRACK_WORLD_OBJECT_DRAW_CALLBACK_ABSENT};
	if (slotDrawCallback == NULL) {
		return true;
	}
	if (objectBaseBytes - (size_t)drawRecordOffset < SLIP_TRACK_DRAW_ATTACHMENT_READY_END) {
		return false;
	}
	const SlipTrackDrawRecord *const draw = (const SlipTrackDrawRecord *)(const void *)record;
	edgeReference = draw->edgeReference;
	result->edgeReference = edgeReference;
	if (edgeReference == 0) {
		result->branch = SLIP_TRACK_WORLD_OBJECT_EDGE_ABSENT;
		return true;
	}
	edgeReference += componentBaseToken;
	result->edgeAddress = edgeReference;
	cmp = draw->attachmentTransformReady;
	result->attachmentState = cmp;
	result->branch =
	    (cmp == 0) ? SLIP_TRACK_WORLD_OBJECT_EDGE_DEFAULT_VALUE : SLIP_TRACK_WORLD_OBJECT_EDGE_EXPLICIT_VALUE;
	return true;
}

bool SlipTrackWorld_BuildAttachmentTransform(const uint8_t *record, size_t recordBytesRemaining, uint8_t *inputChild,
                                             size_t childBytesRemaining, const uint8_t *attachment,
                                             size_t attachmentBytesRemaining, const SlipView3DMatrix *viewMatrix,
                                             SlipView3DVec32 after, SlipTrackWorldBuildAttachment *result) {
	uint32_t pairedDrawRecordAddress;
	SlipView3DVec32 source;
	uint32_t pushed;
	SlipView3DVec32 transformed;
	uint32_t negX;
	uint32_t negY;
	uint32_t negZ;

	if (record == 0 || inputChild == 0 || attachment == 0 || viewMatrix == 0 || result == 0 ||
	    recordBytesRemaining < SLIP_TRACK_DRAW_PAIRED_ADDRESS_END ||
	    childBytesRemaining < SLIP_TRACK_DRAW_ATTACHMENT_NORMAL_END ||
	    attachmentBytesRemaining < SLIP_TRC_PRIMITIVE_FIRST_INDEX_DWORD_END) {
		return false;
	}

	const SlipTrackDrawRecord *const draw = (const SlipTrackDrawRecord *)(const void *)record;
	SlipTrackDrawRecord *const child = (SlipTrackDrawRecord *)(void *)inputChild;
	pairedDrawRecordAddress = draw->pairedDrawAddress;
	child->attachmentTransformReady = UINT32_MAX;
	source = (SlipView3DVec32){(int16_t)SlipBytes_ReadLE16(attachment + SLIP_TRC_PRIMITIVE_NORMAL_X_OFFSET),
	                           (int16_t)SlipBytes_ReadLE16(attachment + SLIP_TRC_PRIMITIVE_NORMAL_Y_OFFSET),
	                           (int16_t)SlipBytes_ReadLE16(attachment + SLIP_TRC_PRIMITIVE_NORMAL_Z_OFFSET)};
	pushed = SlipBytes_ReadLE32(attachment + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET);
	transformed = SlipView3D_TransformPosition16(viewMatrix, source);
	child->attachmentOrigin.x = after.x;
	child->attachmentOrigin.y = after.y;
	child->attachmentOrigin.z = after.z;
	negX = 0u - (uint32_t)transformed.x;
	negY = 0u - (uint32_t)transformed.y;
	negZ = 0u - (uint32_t)transformed.z;
	child->attachmentNormal.x = (int32_t)negX;
	child->attachmentNormal.y = (int32_t)negY;
	child->attachmentNormal.z = (int32_t)negZ;

	*result = (SlipTrackWorldBuildAttachment){pairedDrawRecordAddress,
	                                          UINT32_MAX,
	                                          true,
	                                          true,
	                                          source,
	                                          pushed,
	                                          true,
	                                          transformed,
	                                          pushed,
	                                          true,
	                                          true,
	                                          after,
	                                          negX,
	                                          negY,
	                                          negZ,
	                                          true};
	return true;
}

bool SlipTrackWorld_UseAttachmentTransform(uint8_t *record, size_t recordBytesRemaining,
                                           SlipTrackWorldUseAttachment *result) {
	uint32_t originX;
	uint32_t originY;
	uint32_t originZ;
	uint32_t normalX;
	uint32_t normalY;
	uint32_t normalZ;

	if (record == 0 || result == 0 || recordBytesRemaining < SLIP_TRACK_DRAW_ATTACHMENT_NORMAL_END) {
		return false;
	}

	SlipTrackDrawRecord *const draw = (SlipTrackDrawRecord *)(void *)record;
	draw->attachmentTransformReady = 0;
	originX = (uint32_t)draw->attachmentOrigin.x;
	originY = (uint32_t)draw->attachmentOrigin.y;
	originZ = (uint32_t)draw->attachmentOrigin.z;
	normalX = (uint32_t)draw->attachmentNormal.x;
	normalY = (uint32_t)draw->attachmentNormal.y;
	normalZ = (uint32_t)draw->attachmentNormal.z;
	*result =
	    (SlipTrackWorldUseAttachment){0, originX, originY, originZ, normalX, normalY, true, normalZ, true, true, true};
	return true;
}

bool SlipTrackWorld_InvokeDrawCallback(const uint8_t *record, size_t recordBytesRemaining, uint32_t drawStateIndex,
                                       uint32_t renderMode, uint32_t frameRenderFlags, bool clear,
                                       SlipTrackWorldInvokeDrawCallback *result) {
	uint32_t objectOffset;
	uint32_t actorMode;
	uint32_t test;
	uint32_t recordIndexDuringCallback;

	if (record == 0 || result == 0 || recordBytesRemaining < sizeof(SlipTrackDrawRecord)) {
		return false;
	}
	const SlipTrackDrawRecord *const draw = (const SlipTrackDrawRecord *)(const void *)record;
	objectOffset = draw->objectOffset;
	actorMode = 1u;
	test = 0;

	if ((int32_t)renderMode >= SLIP_TRACK_ACTOR_MODE_DISABLED_DETAIL_MINIMUM) {
		actorMode = 0;
	} else if (renderMode != 0) {
		if (recordBytesRemaining < SLIP_TRACK_DRAW_NO_SLOT_TEST_END) {
			return false;
		}
		/* The no-slot path retains the draw-record pointer: +a0 is
		 * the second following 38-byte record's
		 * normal Z at +30. */
		test = (uint32_t)draw[SLIP_TRACK_DRAW_NO_SLOT_TEST_RECORD_INDEX].attachmentNormal.z &
		       SLIP_TRACK_SLOT_DISABLE_ACTOR_MODE;
		if (test != 0) {
			actorMode = 0;
		}
	}
	recordIndexDuringCallback = SlipDraw3D_CurrentStateRecordIndex(drawStateIndex) + 1u;
	*result = (SlipTrackWorldInvokeDrawCallback){objectOffset,
	                                             true,
	                                             true,
	                                             true,
	                                             recordIndexDuringCallback,
	                                             true,
	                                             true,
	                                             true,
	                                             true,
	                                             1u,
	                                             renderMode,
	                                             test,
	                                             actorMode,
	                                             true,
	                                             true,
	                                             true,
	                                             frameRenderFlags,
	                                             true,
	                                             true,
	                                             recordIndexDuringCallback - 1u,
	                                             true,
	                                             clear,
	                                             true};
	return true;
}

bool SlipTrackWorld_InvokeDrawCallbackExecute(const uint8_t *record, size_t recordBytesRemaining,
                                              uint32_t recordAddress, uint32_t callerValue, uint32_t drawStateIndex,
                                              uint32_t renderMode, uint32_t frameRenderFlags, bool clear,
                                              const uint8_t *slotListBase, size_t slotListBytes,
                                              uint32_t slotListBaseAddress, const SlipObject *objectTableBase,
                                              size_t objectTableBytes,
                                              SlipTrackWorldInvokeDrawCallbackExecute *result) {
	uint32_t objectOffset;
	uint16_t currentObjectOffset;
	uint32_t actorMode;
	uint32_t test;
	uint32_t recordIndexDuringCallback;
	const SlipTrackSlotRecord *selectedSlot = NULL;
	uint32_t testAddress;
	uint32_t testOffset;

	if (record == 0 || result == 0 || recordBytesRemaining < sizeof(SlipTrackDrawRecord)) {
		return false;
	}
	const SlipTrackDrawRecord *const draw = (const SlipTrackDrawRecord *)(const void *)record;
	objectOffset = draw->objectOffset;
	currentObjectOffset = (uint16_t)objectOffset;
	if (!SlipTrackWorld_SelectSlotListEntry(callerValue, slotListBaseAddress, objectTableBase, objectTableBytes,
	                                        currentObjectOffset, &result->select)) {
		return false;
	}
	testAddress = recordAddress;
	testOffset = 0;
	result->testUsesSlotListEntry = false;
	if (!result->select.carry) {
		if (slotListBase == 0 || !SlipTrackWorld_DosAddressToOffset(result->select.slotAddress, slotListBaseAddress,
		                                                            slotListBytes, &testOffset)) {
			return false;
		}
		selectedSlot = (const SlipTrackSlotRecord *)(const void *)(slotListBase + testOffset);
		testAddress = result->select.slotAddress;
		result->testUsesSlotListEntry = true;
	}
	actorMode = 1u;
	test = 0;

	if ((int32_t)renderMode >= SLIP_TRACK_ACTOR_MODE_DISABLED_DETAIL_MINIMUM) {
		actorMode = 0;
	} else if (renderMode != 0) {
		if (result->testUsesSlotListEntry) {
			if (testOffset + SLIP_TRACK_SLOT_FLAGS_END > slotListBytes) {
				return false;
			}
			test = selectedSlot->flags & SLIP_TRACK_SLOT_DISABLE_ACTOR_MODE;
		} else {
			if (recordBytesRemaining < SLIP_TRACK_DRAW_NO_SLOT_TEST_END) {
				return false;
			}

			test = (uint32_t)draw[SLIP_TRACK_DRAW_NO_SLOT_TEST_RECORD_INDEX].attachmentNormal.z &
			       SLIP_TRACK_SLOT_DISABLE_ACTOR_MODE;
		}
		if (test != 0) {
			actorMode = 0;
		}
	}
	recordIndexDuringCallback = SlipDraw3D_CurrentStateRecordIndex(drawStateIndex) + 1u;
	result->block = (SlipTrackWorldInvokeDrawCallback){objectOffset,
	                                                   true,
	                                                   true,
	                                                   true,
	                                                   recordIndexDuringCallback,
	                                                   true,
	                                                   true,
	                                                   true,
	                                                   true,
	                                                   1u,
	                                                   renderMode,
	                                                   test,
	                                                   actorMode,
	                                                   true,
	                                                   true,
	                                                   true,
	                                                   frameRenderFlags,
	                                                   true,
	                                                   true,
	                                                   recordIndexDuringCallback - 1u,
	                                                   true,
	                                                   clear,
	                                                   true};
	result->callerValue = callerValue;
	result->objectOffset = currentObjectOffset;
	result->testedAddress = testAddress;
	result->testedOffset = testOffset;
	return true;
}

bool SlipTrackWorld_ComponentGate(const uint8_t *record, size_t recordBytesRemaining, const uint8_t *componentBase,
                                  size_t componentBaseBytes, uint32_t componentBaseToken, uint32_t valueBeforeGate,
                                  uint16_t mask, SlipTrackWorldComponentGate *result) {
	uint16_t componentOffset;
	const uint8_t *componentRecord;
	uint16_t flags;
	uint32_t flagsMergedWithInput;
	uint16_t maskedFlags;
	uint32_t maskedFlagsMergedWithInput;
	uint16_t maskBit8;
	uint32_t maskedFlagsWithComponentBase;
	uint16_t componentBit8;
	uint16_t sourcePointIndex;

	if (record == 0 || componentBase == 0 || result == 0 || recordBytesRemaining < SLIP_TRD_SECTION_COMPONENT_END) {
		return false;
	}
	componentOffset = SlipBytes_ReadLE16(record + SLIP_TRD_SECTION_COMPONENT_OFFSET);
	if ((size_t)componentOffset > componentBaseBytes ||
	    componentBaseBytes - (size_t)componentOffset < SLIP_TRC_COMPONENT_FLAGS_END) {
		return false;
	}
	componentRecord = componentBase + componentOffset;
	flags = SlipBytes_ReadLE16(componentRecord + SLIP_TRC_COMPONENT_FLAGS_OFFSET);
	flagsMergedWithInput = (valueBeforeGate & SLIP_TRACK_REGISTER_UPPER_WORD_MASK) | flags;
	maskedFlags = (uint16_t)(flags & mask);
	maskedFlagsMergedWithInput = (flagsMergedWithInput & SLIP_TRACK_REGISTER_UPPER_WORD_MASK) | maskedFlags;
	*result = (SlipTrackWorldComponentGate){componentOffset,
	                                        componentRecord,
	                                        flagsMergedWithInput,
	                                        maskedFlagsMergedWithInput,
	                                        maskedFlags,
	                                        0,
	                                        0,
	                                        0,
	                                        0,
	                                        SLIP_TRACK_WORLD_COMPONENT_BRANCH_SKIPPED};
	if (maskedFlags == 0) {
		return true;
	}
	maskBit8 = (uint16_t)(mask & SLIP_TRC_COMPONENT_EXCLUSIVE_PASS);
	result->maskBit8 = maskBit8;
	if (maskBit8 == 0) {
		maskedFlagsWithComponentBase = maskedFlagsMergedWithInput + componentBaseToken;
		result->maskedFlagsWithComponentBase = maskedFlagsWithComponentBase;
		componentBit8 = (uint16_t)(flags & SLIP_TRC_COMPONENT_EXCLUSIVE_PASS);
		result->componentBit8 = componentBit8;
		if (componentBit8 != 0) {
			return true;
		}
	}
	sourcePointIndex = SlipBytes_ReadLE16(record);
	result->sourcePointIndex = sourcePointIndex;
	result->branch = SLIP_TRACK_WORLD_COMPONENT_BRANCH_VISIBLE;
	return true;
}

bool SlipTrackWorld_ComponentProject(const uint8_t *record, size_t recordBytesRemaining, SlipView3DVec32 after,
                                     bool trackWorldCullBoundsCarry, SlipTrackWorldComponentProject *result) {
	uint16_t sourcePointIndex;

	if (record == 0 || result == 0 || recordBytesRemaining < 2u) {
		return false;
	}
	sourcePointIndex = SlipBytes_ReadLE16(record);
	*result = (SlipTrackWorldComponentProject){sourcePointIndex,
	                                           true,
	                                           after,
	                                           true,
	                                           trackWorldCullBoundsCarry,
	                                           trackWorldCullBoundsCarry ? SLIP_TRACK_WORLD_COMPONENT_BRANCH_REJECTED
	                                                                     : SLIP_TRACK_WORLD_COMPONENT_BRANCH_PROJECTED};
	return true;
}

uint32_t SlipTrackWorld_ClassifyPoint(SlipView3DVec32 point, uint32_t projectionMask, int32_t minZ, int32_t maxZ) {
	uint32_t classificationMask;

	classificationMask = projectionMask;
	if (point.z <= minZ) {
		classificationMask |= SLIP_BOX_CLIP_NEAR;
	}
	if (point.z >= maxZ) {
		classificationMask |= SLIP_BOX_CLIP_FAR;
	}
	return classificationMask;
}

bool SlipTrackWorld_IndirectCull(SlipView3DVec32 center, int32_t radius, SlipTrackWorldSphereCull callback,
                                 void *userData, SlipTrackWorldIndirectCull *result) {
	if (callback == 0 || result == 0) {
		return false;
	}
	*result = (SlipTrackWorldIndirectCull){center, radius, true, callback(center, radius, userData), true};
	return true;
}

bool SlipTrackWorld_CullBounds(const uint8_t *component, size_t componentBytesRemaining,
                               const SlipView3DMatrix *viewMatrix, SlipView3DVec32 center, int32_t minZ, int32_t maxZ,
                               SlipTrackWorldSphereCull sphereCull, SlipTrackWorldProjectMask projectMask,
                               void *userData, SlipTrackWorldCullBounds *result) {
	static const uint8_t cornerOffsets[SLIP_TRACK_BOUNDING_CORNER_COUNT][3] = {
	    {SLIP_TRC_COMPONENT_MINIMUM_X_OFFSET, SLIP_TRC_COMPONENT_MINIMUM_Y_OFFSET, SLIP_TRC_COMPONENT_MINIMUM_Z_OFFSET},
	    {SLIP_TRC_COMPONENT_MINIMUM_X_OFFSET, SLIP_TRC_COMPONENT_MINIMUM_Y_OFFSET, SLIP_TRC_COMPONENT_MAXIMUM_Z_OFFSET},
	    {SLIP_TRC_COMPONENT_MAXIMUM_X_OFFSET, SLIP_TRC_COMPONENT_MINIMUM_Y_OFFSET, SLIP_TRC_COMPONENT_MAXIMUM_Z_OFFSET},
	    {SLIP_TRC_COMPONENT_MAXIMUM_X_OFFSET, SLIP_TRC_COMPONENT_MINIMUM_Y_OFFSET, SLIP_TRC_COMPONENT_MINIMUM_Z_OFFSET},
	    {SLIP_TRC_COMPONENT_MINIMUM_X_OFFSET, SLIP_TRC_COMPONENT_MAXIMUM_Y_OFFSET, SLIP_TRC_COMPONENT_MINIMUM_Z_OFFSET},
	    {SLIP_TRC_COMPONENT_MINIMUM_X_OFFSET, SLIP_TRC_COMPONENT_MAXIMUM_Y_OFFSET, SLIP_TRC_COMPONENT_MAXIMUM_Z_OFFSET},
	    {SLIP_TRC_COMPONENT_MAXIMUM_X_OFFSET, SLIP_TRC_COMPONENT_MAXIMUM_Y_OFFSET, SLIP_TRC_COMPONENT_MAXIMUM_Z_OFFSET},
	    {SLIP_TRC_COMPONENT_MAXIMUM_X_OFFSET, SLIP_TRC_COMPONENT_MAXIMUM_Y_OFFSET,
	     SLIP_TRC_COMPONENT_MINIMUM_Z_OFFSET}};
	int32_t radius;
	uint32_t mask;
	int i;

	if (component == 0 || viewMatrix == 0 || sphereCull == 0 || projectMask == 0 || result == 0 ||
	    componentBytesRemaining < SLIP_TRC_COMPONENT_RADIUS_END) {
		return false;
	}
	radius = (int32_t)((int16_t)SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_RADIUS_OFFSET));

	radius = (int32_t)((uint32_t)radius << SLIP_TRC_COMPONENT_RADIUS_SHIFT);
	*result = (SlipTrackWorldCullBounds){radius,      center, true, false,
	                                     {{0, 0, 0}}, {0},    0,    SLIP_TRACK_WORLD_CULL_BRANCH_VISIBLE};
	if (sphereCull(center, radius, userData)) {
		result->trackWorldIndirectCullCarry = true;
		result->branch = SLIP_TRACK_WORLD_CULL_BRANCH_OUTSIDE_SPHERE;
		return true;
	}
	mask = 0;
	for (i = 0; i < SLIP_TRACK_BOUNDING_CORNER_COUNT; ++i) {
		SlipView3DVec32 source;
		SlipView3DVec32 point;
		uint32_t cornerClassificationMask;

		source = (SlipView3DVec32){(int16_t)SlipBytes_ReadLE16(component + cornerOffsets[i][0]),
		                           (int16_t)SlipBytes_ReadLE16(component + cornerOffsets[i][1]),
		                           (int16_t)SlipBytes_ReadLE16(component + cornerOffsets[i][2])};
		if (!SlipTrackWorld_TransformPoint(source, viewMatrix, center, &point)) {
			return false;
		}
		cornerClassificationMask = SlipTrackWorld_ClassifyPoint(point, projectMask(point, userData), minZ, maxZ);
		result->points[i] = point;
		result->lastPoint = point;
		result->cornerClipMasks[i] = cornerClassificationMask;
		if (cornerClassificationMask == 0) {
			result->mask = mask;
			result->branch = SLIP_TRACK_WORLD_CULL_BRANCH_VISIBLE;
			return true;
		}
		if (i == 0) {
			mask = cornerClassificationMask;
		} else {
			mask &= cornerClassificationMask;
		}
	}
	result->mask = mask;
	result->branch = mask != 0 ? SLIP_TRACK_WORLD_CULL_BRANCH_OUTSIDE_PLANES : SLIP_TRACK_WORLD_CULL_BRANCH_VISIBLE;
	return true;
}

uint16_t SlipTrackWorld_RandomStep(uint16_t state) {
	uint16_t nextState;
	bool carry;

	nextState = (uint16_t)(state + 1u);
	carry = (nextState & 1u) != 0;
	nextState >>= 1;
	if (carry) {
		nextState ^= SLIP_RANDOM_LFSR_FEEDBACK_MASK;
	}
	return nextState;
}

bool SlipTrackWorld_ProjectMask(SlipView3DVec32 point, const SlipTrackWorldProjectFrustum *frustum,
                                uint32_t *clipMaskOut) {
	uint32_t clipMask;
	uint32_t planeBoundary;
	uint32_t pointX;

	if (frustum == 0 || clipMaskOut == 0) {
		return false;
	}
	clipMask = 0;
	pointX = (uint32_t)point.x;
	planeBoundary = (uint32_t)SlipTrackWorld_MultiplySignedShift((uint32_t)frustum->maxXStep, (uint32_t)point.z,
	                                                             SLIP_PROJECTION_SLOPE_FRACTION_BITS);
	if ((int32_t)pointX >= (int32_t)planeBoundary) {
		clipMask |= SLIP_BOUNDS_CLIP_RIGHT;
	}
	planeBoundary = (uint32_t)SlipTrackWorld_MultiplySignedShift((uint32_t)frustum->minXStep, (uint32_t)point.z,
	                                                             SLIP_PROJECTION_SLOPE_FRACTION_BITS);
	if ((int32_t)pointX < (int32_t)planeBoundary) {
		clipMask |= SLIP_BOUNDS_CLIP_LEFT;
	}
	planeBoundary = (uint32_t)SlipTrackWorld_MultiplySignedShift((uint32_t)frustum->minYStep, (uint32_t)point.z,
	                                                             SLIP_PROJECTION_SLOPE_FRACTION_BITS);
	if (point.y >= (int32_t)planeBoundary) {
		clipMask |= SLIP_BOUNDS_CLIP_TOP;
	}
	planeBoundary = (uint32_t)SlipTrackWorld_MultiplySignedShift((uint32_t)frustum->maxYStep, (uint32_t)point.z,
	                                                             SLIP_PROJECTION_SLOPE_FRACTION_BITS);
	if (point.y < (int32_t)planeBoundary) {
		clipMask |= SLIP_BOUNDS_CLIP_BOTTOM;
	}
	*clipMaskOut = clipMask;
	return true;
}

bool SlipTrackWorld_SphereCull(SlipView3DVec32 center, int32_t radius, const SlipTrackWorldProjectFrustum *frustum,
                               bool *carry) {
	uint32_t planeDistance;
	uint32_t centerY;
	uint32_t centerZ;
	uint32_t sphereRadius;
	uint32_t planeRadius;
	uint32_t depthLimitOrCenterX;

	if (frustum == 0 || carry == 0) {
		return false;
	}
	planeDistance = (uint32_t)center.x;
	centerY = (uint32_t)center.y;
	centerZ = (uint32_t)center.z;
	sphereRadius = (uint32_t)radius;
	depthLimitOrCenterX = centerZ + sphereRadius;
	if ((int32_t)depthLimitOrCenterX <= frustum->minZ) {
		*carry = true;
		return true;
	}
	depthLimitOrCenterX = centerZ - sphereRadius;
	if ((int32_t)depthLimitOrCenterX >= frustum->maxZ) {
		*carry = true;
		return true;
	}
	planeRadius = sphereRadius;
	depthLimitOrCenterX = planeDistance;
	planeDistance = (uint32_t)SlipTrackWorld_MultiplySignedShift((uint32_t)(int32_t)frustum->minXPlaneDepthQ14,
	                                                             depthLimitOrCenterX, SLIP_Q14_FRACTION_BITS);
	planeDistance += (uint32_t)SlipTrackWorld_MultiplySignedShift((uint32_t)(int32_t)frustum->minXPlaneNegXQ14, centerZ,
	                                                              SLIP_Q14_FRACTION_BITS);
	planeDistance += planeRadius;
	if ((int32_t)planeDistance < 0) {
		*carry = true;
		return true;
	}
	planeDistance = (uint32_t)SlipTrackWorld_MultiplySignedShift((uint32_t)(int32_t)frustum->maxXPlaneNegDepthQ14,
	                                                             depthLimitOrCenterX, SLIP_Q14_FRACTION_BITS);
	planeDistance += (uint32_t)SlipTrackWorld_MultiplySignedShift((uint32_t)(int32_t)frustum->maxXPlaneXQ14, centerZ,
	                                                              SLIP_Q14_FRACTION_BITS);
	planeDistance += planeRadius;
	if ((int32_t)planeDistance < 0) {
		*carry = true;
		return true;
	}
	planeDistance = (uint32_t)SlipTrackWorld_MultiplySignedShift((uint32_t)(int32_t)frustum->maxYPlaneDepthQ14, centerY,
	                                                             SLIP_Q14_FRACTION_BITS);
	planeDistance += (uint32_t)SlipTrackWorld_MultiplySignedShift((uint32_t)(int32_t)frustum->maxYPlaneYQ14, centerZ,
	                                                              SLIP_Q14_FRACTION_BITS);
	planeDistance += planeRadius;
	if ((int32_t)planeDistance < 0) {
		*carry = true;
		return true;
	}
	planeDistance = (uint32_t)SlipTrackWorld_MultiplySignedShift((uint32_t)(int32_t)frustum->minYPlaneNegDepthQ14,
	                                                             centerY, SLIP_Q14_FRACTION_BITS);
	planeDistance += (uint32_t)SlipTrackWorld_MultiplySignedShift((uint32_t)(int32_t)frustum->minYPlaneNegYQ14, centerZ,
	                                                              SLIP_Q14_FRACTION_BITS);
	planeDistance += planeRadius;
	if ((int32_t)planeDistance < 0) {
		*carry = true;
		return true;
	}
	*carry = false;
	return true;
}

bool SlipTrackWorld_UpdateDrawFlags(uint32_t renderFlagsValue, int32_t componentDepth, uint32_t textureMode,
                                    uint32_t shading, int32_t componentDistance, uint32_t shadingSecondary,
                                    int32_t componentRadius, SlipTrackWorldDrawFlags *result) {
	uint32_t flagsWithTextureBit;

	if (result == 0) {
		return false;
	}
	flagsWithTextureBit = renderFlagsValue;
	flagsWithTextureBit |= SLIP_RENDER_MASKED_TEXTURE;
	*result = (SlipTrackWorldDrawFlags){true,
	                                    renderFlagsValue,
	                                    flagsWithTextureBit,
	                                    textureMode,
	                                    0,
	                                    0,
	                                    SLIP_TRACK_WORLD_DRAW_FLAGS_CONTINUATION_TOKEN,
	                                    shading,
	                                    (uint32_t)componentDistance,
	                                    0,
	                                    0,
	                                    shadingSecondary,
	                                    (uint32_t)componentRadius,
	                                    0,
	                                    true,
	                                    0};
	if (textureMode != SLIP_RENDER_TEXTURE_MASKING_ENABLED) {
		flagsWithTextureBit &= ~SLIP_RENDER_MASKED_TEXTURE;
	}
	flagsWithTextureBit &= ~SLIP_RENDER_DISABLE_TEXTURES;
	result->flagsAfterTextureModeAndClearBit8 = flagsWithTextureBit;
	flagsWithTextureBit |= SLIP_RENDER_FLAT_SHADING;
	result->flagsWithBit2 = flagsWithTextureBit;
	if (shading != 0 && componentDepth <= componentDistance) {
		flagsWithTextureBit &= ~SLIP_RENDER_FLAT_SHADING;
	}
	result->flagsAfterShadingGate = flagsWithTextureBit;
	flagsWithTextureBit |= SLIP_RENDER_DISABLE_SPECULAR;
	result->flagsWithBit4 = flagsWithTextureBit;
	if (shadingSecondary != 0 && componentDepth <= componentRadius) {
		flagsWithTextureBit &= ~SLIP_RENDER_DISABLE_SPECULAR;
	}
	result->flagsAfterSecondaryShadingGate = flagsWithTextureBit;
	result->rendererFlags = flagsWithTextureBit;
	return true;
}

bool SlipTrackWorld_FrameDrawState(uint32_t drawFlagsFrom, uint32_t shading, uint32_t shadingSecondary,
                                   SlipTrackWorldFrameDrawState *result) {
	uint32_t flags;

	if (result == 0) {
		return false;
	}
	flags = drawFlagsFrom;
	flags &= ~SLIP_RENDER_DISABLE_TEXTURES;
	*result = (SlipTrackWorldFrameDrawState){.loadedRenderFlags = true,
	                                         .drawFlagsFrom = drawFlagsFrom,
	                                         .drawFlagsAfterClearBit8 = flags,
	                                         .bit2Gate = shading,
	                                         .bit4Gate = shadingSecondary,
	                                         .callRendererSetFlags = true,
	                                         .returned = true};
	flags &= ~SLIP_RENDER_FLAT_SHADING;
	result->drawFlagsAfterClearBit2 = flags;
	if (shading == 0) {
		flags |= SLIP_RENDER_FLAT_SHADING;
	}
	result->drawFlagsAfterOptionalBit2 = flags;
	flags &= ~SLIP_RENDER_DISABLE_SPECULAR;
	result->drawFlagsAfterClearBit4 = flags;
	if (shadingSecondary == 0) {
		flags |= SLIP_RENDER_DISABLE_SPECULAR;
	}
	result->drawFlagsAfterOptionalBit4 = flags;
	result->storedFrameFlags = flags;
	result->drawFlagsTo = flags;
	return true;
}

SlipTrackBeamState SlipTrackWorld_beams;

SlipTrackBeamRecord *SlipTrackWorld_AllocateBeam(SlipTrackBeamState *beams) {
	if (beams->recordCount == SLIP_TRACK_BEAM_RECORD_CAPACITY)
		return NULL;
	SlipTrackBeamRecord *const record =
	    &(beams->resourceRecords ? beams->resourceRecords : beams->records)[beams->recordCount++];
	record->type = SLIP_TRACK_BEAM_BLASTER;
	return record;
}

bool SlipTrackWorld_PreFrameScale(SlipView3DVec32 input, SlipView3DVec32 nodeOrigin,
                                  SlipTrackWorldRangePlane rangePlane, uint16_t radius, uint16_t axisX, uint16_t axisY,
                                  uint16_t axisZ, SlipTrackWorldPreFrameScale *result) {
	uint32_t dotRounded;
	uint32_t shiftedLow;
	uint32_t shiftedHigh;
	uint32_t shiftStep;
	uint32_t divisor;
	uint32_t scale;
	SlipView3DVec32 nodeDelta;
	SlipView3DVec32 rangeDelta;
	SlipView3DVec32 scaled;

	if (result == 0) {
		return false;
	}
	nodeDelta = (SlipView3DVec32){(int32_t)((uint32_t)input.x - (uint32_t)nodeOrigin.x),
	                              (int32_t)((uint32_t)input.y - (uint32_t)nodeOrigin.y),
	                              (int32_t)((uint32_t)input.z - (uint32_t)nodeOrigin.z)};
	rangeDelta = (SlipView3DVec32){(int32_t)((uint32_t)nodeDelta.x - (uint32_t)rangePlane.origin.x),
	                               (int32_t)((uint32_t)nodeDelta.y - (uint32_t)rangePlane.origin.y),
	                               (int32_t)((uint32_t)nodeDelta.z - (uint32_t)rangePlane.origin.z)};
	dotRounded = SlipTrackWorld_RoundedDotProductShift14((uint32_t)rangeDelta.x, (uint32_t)rangeDelta.y,
	                                                     (uint32_t)rangeDelta.z, (uint32_t)rangePlane.normal.x,
	                                                     (uint32_t)rangePlane.normal.y, (uint32_t)rangePlane.normal.z);
	shiftedHigh = dotRounded;
	shiftedLow = 0;
	for (shiftStep = 0; shiftStep < 2u; ++shiftStep) {
		const uint32_t carryFrom = shiftedHigh & 1u;
		shiftedHigh = (shiftedHigh >> 1) | (shiftedHigh & SLIP_TRACK_DWORD_SIGN_BIT);
		shiftedLow = (shiftedLow >> 1) | (carryFrom << 31);
	}
	divisor = (uint32_t)radius << SLIP_TRACK_COLLISION_DIVISOR_SHIFT;
	*result = (SlipTrackWorldPreFrameScale){
	    input,      input,      nodeDelta,   rangeDelta, dotRounded,
	    dotRounded, shiftedLow, shiftedHigh, divisor,    shiftedHigh > divisor,
	    false,      dotRounded, {0, 0, 0},   {0, 0, 0},  SLIP_TRACK_WORLD_PRE_FRAME_SCALE_BRANCH_DOT_PRODUCT,
	    true};
	if (shiftedHigh <= divisor) {
		uint32_t quotient;

		if (divisor == 0) {
			return false;
		}
		quotient = (uint32_t)((((uint64_t)shiftedHigh << 32) | shiftedLow) / divisor);
		result->negativeQuotient = (quotient & SLIP_TRACK_DWORD_SIGN_BIT) != 0;
		if ((quotient & SLIP_TRACK_DWORD_SIGN_BIT) == 0) {
			scale = quotient;
			result->branch = SLIP_TRACK_WORLD_PRE_FRAME_SCALE_BRANCH_QUOTIENT;
		} else {
			scale = dotRounded;
		}
	} else {
		scale = dotRounded;
	}
	scaled = SlipView3D_ScaleAxesQ14(axisX, axisY, axisZ, (int32_t)scale);
	result->scale = scale;
	result->scaled = scaled;
	result->output = (SlipView3DVec32){(int32_t)((uint32_t)scaled.x + (uint32_t)input.x),
	                                   (int32_t)((uint32_t)scaled.y + (uint32_t)input.y),
	                                   (int32_t)((uint32_t)scaled.z + (uint32_t)input.z)};
	return true;
}

void SlipTrackWorld_TrackSlotPlaneDistance(const uint8_t *trackRecord, const uint8_t *plane,
                                           const uint8_t *componentBase, size_t componentBaseBytes,
                                           SlipView3DVec32 direction, SlipView3DVec32 queryPoint,
                                           SlipTrackWorldTrackSlotPlaneDistance *result) {
	const uint8_t *componentList;
	uint16_t normalX;
	uint16_t normalY;
	uint16_t normalZ;
	uint32_t facingDotLowWord;
	uint32_t pointX;
	uint32_t pointY;
	uint32_t pointZ;
	uint64_t dotBits;

	memset(result, 0, sizeof(*result));
	result->trackRecordOrigin =
	    (SlipView3DVec32){(int32_t)SlipBytes_ReadLE32(trackRecord + SLIP_TRD_SECTION_ORIGIN_X_OFFSET),
	                      (int32_t)SlipBytes_ReadLE32(trackRecord + SLIP_TRD_SECTION_ORIGIN_Y_OFFSET),
	                      (int32_t)SlipBytes_ReadLE32(trackRecord + SLIP_TRD_SECTION_ORIGIN_Z_OFFSET)};
	result->componentListOffset = SlipBytes_ReadLE16(trackRecord + SLIP_TRD_SECTION_COMPONENT_OFFSET);
	componentList = componentBase + result->componentListOffset;
	normalX = SlipBytes_ReadLE16(plane + SLIP_TRC_PRIMITIVE_NORMAL_X_OFFSET);
	normalY = SlipBytes_ReadLE16(plane + SLIP_TRC_PRIMITIVE_NORMAL_Y_OFFSET);
	normalZ = SlipBytes_ReadLE16(plane + SLIP_TRC_PRIMITIVE_NORMAL_Z_OFFSET);
	facingDotLowWord = SlipView3D_DotProductQ14((uint16_t)direction.x, (uint16_t)direction.y, (uint16_t)direction.z,
	                                            normalX, normalY, normalZ, &result->facingDot);
	result->facingDotLowWord = (int16_t)(uint16_t)facingDotLowWord;
	if (result->facingDotLowWord >= 0) {
		result->rejected = true;
		result->returned = true;
		return;
	}

	SlipTrackWorld_PointLookup(componentList, componentBase, componentBaseBytes,
	                           SlipBytes_ReadLE16(plane + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET),
	                           result->facingDot.dotProductQ14, result->facingDot.xySumHigh, result->facingDot.inputZ,
	                           &result->point);
	pointX = result->point.pointXOrInput + (uint32_t)result->trackRecordOrigin.x;
	pointY = result->point.pointYOrInput + (uint32_t)result->trackRecordOrigin.y;
	pointZ = result->point.pointZOrCountMergedWithInput + (uint32_t)result->trackRecordOrigin.z;
	SlipTrackWorld_StoreRangePlane(pointX, pointY, pointZ, normalX, normalY, normalZ, &result->rangePlane);
	result->queryDelta =
	    (SlipView3DVec32){(int32_t)((uint32_t)queryPoint.x - pointX), (int32_t)((uint32_t)queryPoint.y - pointY),
	                      (int32_t)((uint32_t)queryPoint.z - pointZ)};
	dotBits = (uint64_t)((int64_t)result->queryDelta.x * (int64_t)result->rangePlane.normal.x) +
	          (uint64_t)((int64_t)result->queryDelta.y * (int64_t)result->rangePlane.normal.y) +
	          (uint64_t)((int64_t)result->queryDelta.z * (int64_t)result->rangePlane.normal.z);
	result->dotBits = dotBits;
	result->planeDistance = (uint32_t)(dotBits >> SLIP_Q14_FRACTION_BITS);
	result->planeDistance += (uint32_t)((dotBits >> (SLIP_Q14_FRACTION_BITS - 1)) & 1u);
	result->rejected = false;
	result->returned = true;
}

void SlipTrackWorld_CheckTrackRecordPrimitives(const uint8_t *trackRecord, const uint8_t *componentBase,
                                               size_t componentBaseBytes, SlipView3DVec32 queryPoint,
                                               SlipTrackWorldCollisionQuery *query) {
	SlipView3DVec32 relativeStart;
	const uint8_t *componentList;
	const uint8_t *primitive;
	uint32_t primitiveCount;

	relativeStart = (SlipView3DVec32){
	    (int32_t)((uint32_t)queryPoint.x - SlipBytes_ReadLE32(trackRecord + SLIP_TRD_SECTION_ORIGIN_X_OFFSET)),
	    (int32_t)((uint32_t)queryPoint.y - SlipBytes_ReadLE32(trackRecord + SLIP_TRD_SECTION_ORIGIN_Y_OFFSET)),
	    (int32_t)((uint32_t)queryPoint.z - SlipBytes_ReadLE32(trackRecord + SLIP_TRD_SECTION_ORIGIN_Z_OFFSET))};
	componentList = componentBase + SlipBytes_ReadLE16(trackRecord + SLIP_TRD_SECTION_COMPONENT_OFFSET);
	if (SlipBytes_ReadLE16(componentList + SLIP_TRC_COMPONENT_PRIMITIVE_LIST_OFFSET) == 0) {
		return;
	}
	primitive = componentBase + SlipBytes_ReadLE16(componentList + SLIP_TRC_COMPONENT_PRIMITIVE_LIST_OFFSET);
	primitiveCount = SlipBytes_ReadLE16(primitive);
	primitive += SLIP_TRC_TABLE_COUNT_BYTES;
	do {
		uint16_t descriptor;

		do {
			if ((primitive[SLIP_TRC_PRIMITIVE_FLAGS_OFFSET] &
			     (SLIP_TRC_PRIMITIVE_TRENCH | SLIP_TRC_PRIMITIVE_RANGE_PLANE)) == 0) {
				const uint16_t normalX = SlipBytes_ReadLE16(primitive + SLIP_TRC_PRIMITIVE_NORMAL_X_OFFSET);
				const uint16_t normalY = SlipBytes_ReadLE16(primitive + SLIP_TRC_PRIMITIVE_NORMAL_Y_OFFSET);
				const uint16_t normalZ = SlipBytes_ReadLE16(primitive + SLIP_TRC_PRIMITIVE_NORMAL_Z_OFFSET);
				const uint32_t facingBits = (uint32_t)((int32_t)(int16_t)normalX * (int32_t)query->directionX) +
				                            (uint32_t)((int32_t)(int16_t)normalY * (int32_t)query->directionY) +
				                            (uint32_t)((int32_t)(int16_t)normalZ * (int32_t)query->directionZ);

				if ((int32_t)facingBits < 0) {
					const int16_t radius = (int16_t)(uint16_t)(0u - (uint16_t)(facingBits >> SLIP_Q14_FRACTION_BITS));

					if (radius >= SLIP_TRACK_RAY_MINIMUM_FACING_Q14) {
						SlipTrackWorldPointLookup point;
						SlipView3DVec32 planeDelta;
						int32_t planeDistance;
						int32_t adjustedDistance;
						const uint32_t divisor = (uint32_t)(uint16_t)radius << SLIP_TRACK_COLLISION_DIVISOR_SHIFT;
						uint32_t planeTime;
						SlipView3DVec32 candidate;
						SlipTrackWorldSideTest sideTest;

						memset(&point, 0, sizeof(point));
						SlipTrackWorld_PointLookup(
						    componentList, componentBase, componentBaseBytes,
						    SlipBytes_ReadLE16(primitive + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET), 0, 0, 0, &point);
						planeDelta = (SlipView3DVec32){
						    (int32_t)((uint32_t)relativeStart.x - point.pointXOrInput),
						    (int32_t)((uint32_t)relativeStart.y - point.pointYOrInput),
						    (int32_t)((uint32_t)relativeStart.z - point.pointZOrCountMergedWithInput)};
						planeDistance =
						    SlipTrackWorld_DotProduct32x16Shift14((uint32_t)planeDelta.x, (uint32_t)planeDelta.y,
						                                          (uint32_t)planeDelta.z, normalX, normalY, normalZ);
						if (planeDistance < 0) {
							break;
						}
						adjustedDistance = (int32_t)((uint32_t)planeDistance - SLIP_TRACK_COLLISION_SURFACE_CLEARANCE -
						                             (uint32_t)query->surfaceOffset);
						if (adjustedDistance >= 0) {
							uint64_t adjustedDividend;
							uint32_t adjustedTime;
							uint64_t candidatePlaneDividend;

							if (adjustedDistance >= query->bestDistance) {
								break;
							}
							adjustedDividend = (uint64_t)(uint32_t)adjustedDistance
							                   << SLIP_TRACK_COLLISION_DIVIDEND_SHIFT;
							if ((uint32_t)(adjustedDividend >> 32) > divisor) {
								break;
							}
							adjustedTime = (uint32_t)(adjustedDividend / divisor);
							if ((int32_t)adjustedTime < 0 || (int32_t)adjustedTime >= query->bestDistance) {
								break;
							}
							candidatePlaneDividend = (uint64_t)(uint32_t)planeDistance
							                         << SLIP_TRACK_COLLISION_DIVIDEND_SHIFT;
							if ((uint32_t)(candidatePlaneDividend >> 32) > divisor) {
								break;
							}
							planeTime = (uint32_t)(candidatePlaneDividend / divisor);
							if ((int32_t)planeTime < 0) {
								break;
							}
							candidate =
							    (SlipView3DVec32){(int32_t)((uint32_t)relativeStart.x +
							                                (uint32_t)((uint64_t)((int64_t)query->directionX *
							                                                      (int64_t)(int32_t)planeTime) >>
							                                           SLIP_Q14_FRACTION_BITS)),
							                      (int32_t)((uint32_t)relativeStart.y +
							                                (uint32_t)((uint64_t)((int64_t)query->directionY *
							                                                      (int64_t)(int32_t)planeTime) >>
							                                           SLIP_Q14_FRACTION_BITS)),
							                      (int32_t)((uint32_t)relativeStart.z +
							                                (uint32_t)((uint64_t)((int64_t)query->directionZ *
							                                                      (int64_t)(int32_t)planeTime) >>
							                                           SLIP_Q14_FRACTION_BITS))};
							memset(&sideTest, 0, sizeof(sideTest));
							SlipTrackWorld_SideTest(componentList, componentBase, componentBaseBytes, primitive,
							                        componentBaseBytes - (size_t)(primitive - componentBase), candidate,
							                        0, 0, &sideTest);
							if (sideTest.outside) {
								break;
							}
							query->bestDistance = (int32_t)adjustedTime;
						} else {
							const uint64_t fallbackPlaneDividend = (uint64_t)(uint32_t)planeDistance
							                                       << SLIP_TRACK_COLLISION_DIVIDEND_SHIFT;
							uint32_t inwardDistance;
							uint64_t inwardDividend;

							if ((uint32_t)(fallbackPlaneDividend >> 32) > divisor) {
								break;
							}
							planeTime = (uint32_t)(fallbackPlaneDividend / divisor);
							if ((int32_t)planeTime < 0) {
								break;
							}
							candidate =
							    (SlipView3DVec32){(int32_t)((uint32_t)relativeStart.x +
							                                (uint32_t)((uint64_t)((int64_t)query->directionX *
							                                                      (int64_t)(int32_t)planeTime) >>
							                                           SLIP_Q14_FRACTION_BITS)),
							                      (int32_t)((uint32_t)relativeStart.y +
							                                (uint32_t)((uint64_t)((int64_t)query->directionY *
							                                                      (int64_t)(int32_t)planeTime) >>
							                                           SLIP_Q14_FRACTION_BITS)),
							                      (int32_t)((uint32_t)relativeStart.z +
							                                (uint32_t)((uint64_t)((int64_t)query->directionZ *
							                                                      (int64_t)(int32_t)planeTime) >>
							                                           SLIP_Q14_FRACTION_BITS))};
							memset(&sideTest, 0, sizeof(sideTest));
							SlipTrackWorld_SideTest(componentList, componentBase, componentBaseBytes, primitive,
							                        componentBaseBytes - (size_t)(primitive - componentBase), candidate,
							                        0, 0, &sideTest);
							if (sideTest.outside) {
								break;
							}
							inwardDistance = 0u - (uint32_t)adjustedDistance;
							inwardDividend = (uint64_t)inwardDistance << SLIP_TRACK_COLLISION_DIVIDEND_SHIFT;
							if ((uint32_t)(inwardDividend >> 32) > divisor) {
								break;
							}
							query->bestDistance = (int32_t)(0u - (uint32_t)(inwardDividend / divisor));
							if (query->bestDistance < 0) {
								query->bestDistance = 0;
							}
						}

						query->hitFraction = (int32_t)((((uint64_t)(uint32_t)query->bestDistance
						                                 << SLIP_TRACK_COLLISION_DIVIDEND_SHIFT) /
						                                query->hitFractionDivisor) >>
						                               SLIP_TRACK_COLLISION_DIVISOR_SHIFT);
						query->hitNormalX = (int16_t)normalX;
						query->hitNormalY = (int16_t)normalY;
						query->hitNormalZ = (int16_t)normalZ;
						query->hitPrimitiveValue =
						    (uint16_t)(SlipBytes_ReadLE16(primitive + SLIP_TRC_PRIMITIVE_MATERIAL_OFFSET) &
						               SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK);
						query->hitPoint = (SlipView3DVec32){
						    (int32_t)((uint32_t)candidate.x +
						              SlipBytes_ReadLE32(trackRecord + SLIP_TRD_SECTION_ORIGIN_X_OFFSET)),
						    (int32_t)((uint32_t)candidate.y +
						              SlipBytes_ReadLE32(trackRecord + SLIP_TRD_SECTION_ORIGIN_Y_OFFSET)),
						    (int32_t)((uint32_t)candidate.z +
						              SlipBytes_ReadLE32(trackRecord + SLIP_TRD_SECTION_ORIGIN_Z_OFFSET))};
						return;
					}
				}
			}

		} while (false);
		descriptor = SlipBytes_ReadLE16(primitive);
		if ((descriptor & SLIP_TRC_PRIMITIVE_TEXTURED) == 0) {
			primitive += SLIP_TRC_PRIMITIVE_HEADER_BYTES + (size_t)descriptor * 2u;
		} else {
			primitive += SLIP_TRC_PRIMITIVE_HEADER_BYTES +
			             (size_t)(descriptor & SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK) * SLIP_TRC_TEXTURED_VERTEX_BYTES;
		}
		--primitiveCount;
	} while (primitiveCount != 0);
}

void SlipTrackWorld_CheckSlotSamples(uint8_t *inputSlot, const SlipObject *objectTable, size_t objectTableBytes,
                                     const uint8_t *trdBase, size_t trackDataSize, uint32_t trdBaseAddress,
                                     const uint8_t *componentBase, size_t componentBaseBytes,
                                     uint32_t componentBaseAddress, const uint8_t *table, size_t tableBytes,
                                     SlipTrackWorldCollisionQuery *query) {
	const SlipTrackSlotRecord *const slot = (const SlipTrackSlotRecord *)(const void *)inputSlot;
	SlipView3DDotProductQ14 dot;
	uint32_t sampleCount;
	uint32_t sampleIndex;

	query->surfaceOffset = 0;
	SlipTrackWorld_UpdateSlotRecord(inputSlot, objectTable, objectTableBytes, trdBase, trackDataSize, trdBaseAddress,
	                                componentBase, componentBaseBytes, componentBaseAddress, table, tableBytes);
	SlipView3D_DotProductQ14((uint16_t)slot->cachedObjectMatrix.m[6], (uint16_t)slot->cachedObjectMatrix.m[7],
	                         (uint16_t)slot->cachedObjectMatrix.m[8], (uint16_t)query->directionX,
	                         (uint16_t)query->directionY, (uint16_t)query->directionZ, &dot);
	sampleCount = (int16_t)(uint16_t)dot.dotProductQ14 > (int16_t)SLIP_TRACK_COLLISION_ALIGNED_FACING_Q14
	                  ? SLIP_TRACK_COLLISION_ALIGNED_SAMPLE_COUNT
	                  : SLIP_TRACK_BOUNDING_CORNER_COUNT;
	for (sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
		uint32_t trackRecordAddress;
		const uint8_t *trackRecord;
		const int32_t *const sample = &slot->boundsAndCorners[SLIP_TRACK_BOUND_COORDINATE_COUNT +
		                                                      sampleIndex * SLIP_TRACK_CORNER_COORDINATE_COUNT];
		SlipView3DVec32 queryPoint;

		if (query->bestDistance == 0) {
			return;
		}
		trackRecordAddress = slot->cornerTrackRecords[sampleIndex];
		if (trackRecordAddress == 0) {
			continue;
		}
		trackRecord = trdBase + (trackRecordAddress - trdBaseAddress);
		queryPoint = (SlipView3DVec32){sample[0], sample[1], sample[2]};
		SlipTrackWorld_CheckTrackRecordPrimitives(trackRecord, componentBase, componentBaseBytes, queryPoint, query);
		if (query->hitFraction != -1) {
			continue;
		}

		{
			const uint16_t firstExitPlaneOffset =
			    SlipBytes_ReadLE16(trackRecord + SLIP_TRD_SECTION_FIRST_EXIT_PLANE_OFFSET);
			const uint16_t firstExitRecordOffset = SlipBytes_ReadLE16(trackRecord + SLIP_TRD_SECTION_FIRST_EXIT_OFFSET);

			if (firstExitPlaneOffset != 0) {
				SlipTrackWorldTrackSlotPlaneDistance distance;

				SlipTrackWorld_TrackSlotPlaneDistance(
				    trackRecord, componentBase + firstExitPlaneOffset, componentBase, componentBaseBytes,
				    (SlipView3DVec32){query->directionX, query->directionY, query->directionZ}, queryPoint, &distance);
				if (!distance.rejected && (int32_t)distance.planeDistance < query->bestDistance) {
					SlipTrackWorld_CheckTrackRecordPrimitives(trdBase + firstExitRecordOffset, componentBase,
					                                          componentBaseBytes, queryPoint, query);
				}
			}
		}
		if (query->hitFraction != -1) {
			continue;
		}

		{
			const uint16_t secondExitPlaneOffset =
			    SlipBytes_ReadLE16(trackRecord + SLIP_TRD_SECTION_SECOND_EXIT_PLANE_OFFSET);
			const uint16_t secondExitRecordOffset =
			    SlipBytes_ReadLE16(trackRecord + SLIP_TRD_SECTION_SECOND_EXIT_OFFSET);

			if (secondExitPlaneOffset != 0) {
				SlipTrackWorldTrackSlotPlaneDistance distance;

				SlipTrackWorld_TrackSlotPlaneDistance(
				    trackRecord, componentBase + secondExitPlaneOffset, componentBase, componentBaseBytes,
				    (SlipView3DVec32){query->directionX, query->directionY, query->directionZ}, queryPoint, &distance);
				if (!distance.rejected && (int32_t)distance.planeDistance < query->bestDistance) {
					SlipTrackWorld_CheckTrackRecordPrimitives(trdBase + secondExitRecordOffset, componentBase,
					                                          componentBaseBytes, queryPoint, query);
				}
			}
		}
		if (query->hitFraction != -1) {
			continue;
		}

		{
			const uint16_t thirdExitPlaneOffset =
			    SlipBytes_ReadLE16(trackRecord + SLIP_TRD_SECTION_THIRD_EXIT_PLANE_OFFSET);
			const uint16_t thirdExitRecordOffset = SlipBytes_ReadLE16(trackRecord + SLIP_TRD_SECTION_THIRD_EXIT_OFFSET);

			if (thirdExitPlaneOffset != 0) {
				SlipTrackWorldTrackSlotPlaneDistance distance;

				SlipTrackWorld_TrackSlotPlaneDistance(
				    trackRecord, componentBase + thirdExitPlaneOffset, componentBase, componentBaseBytes,
				    (SlipView3DVec32){query->directionX, query->directionY, query->directionZ}, queryPoint, &distance);
				if (!distance.rejected && (int32_t)distance.planeDistance < query->bestDistance) {
					SlipTrackWorld_CheckTrackRecordPrimitives(trdBase + thirdExitRecordOffset, componentBase,
					                                          componentBaseBytes, queryPoint, query);
				}
			}
		}
	}
}

void SlipTrackWorld_PreCollisionStep(uint16_t frameStep, uint8_t *slotListBase, uint32_t slotListBaseAddress,
                                     uint32_t slotListSentinelAddress, SlipObject *objectTable, size_t objectTableBytes,
                                     const uint8_t *trdBase, size_t trackDataSize, uint32_t trdBaseAddress,
                                     const uint8_t *componentBase, size_t componentBaseBytes,
                                     uint32_t componentBaseAddress, const uint8_t *table, size_t tableBytes) {
	uint32_t slotAddress = slotListSentinelAddress;

	for (;;) {
		uint8_t *currentSlot = slotListBase + (slotAddress - slotListBaseAddress);
		SlipTrackSlotRecord *slot = (SlipTrackSlotRecord *)(void *)currentSlot;
		uint16_t objectOffset;
		int32_t objectSpeed;
		SlipObjectPosition objectPosition;
		SlipObjectDirection direction;
		SlipTrackWorldCollisionQuery query;

		slotAddress = slot->nextSlotAddress;
		if (slotAddress == slotListSentinelAddress) {
			return;
		}
		currentSlot = slotListBase + (slotAddress - slotListBaseAddress);
		slot = (SlipTrackSlotRecord *)(void *)currentSlot;
		if ((slot->flags & SLIP_TRACK_SLOT_BOX_COLLISION) == 0) {
			continue;
		}
		objectOffset = (uint16_t)slot->ownerObjectOffset;
		objectSpeed = SlipObject_Speed(objectTable, objectOffset);
		if (objectSpeed == 0) {
			continue;
		}
		memset(&query, 0, sizeof(query));
		query.hitFractionDivisor = (uint32_t)objectSpeed;
		query.bestDistance = (int32_t)(uint32_t)(((uint64_t)(uint32_t)frameStep * (uint64_t)(uint32_t)objectSpeed) >>
		                                         SLIP_Q14_FRACTION_BITS);
		query.hitFraction = -1;
		SlipObject_Position(objectTable, objectTableBytes, objectOffset, &objectPosition);
		slot->projectedPosition.x = (int32_t)objectPosition.positionX;
		slot->projectedPosition.y = (int32_t)objectPosition.positionY;
		slot->projectedPosition.z = (int32_t)objectPosition.positionZ;
		direction = SlipObject_Direction(objectTable, objectOffset);
		query.directionX = direction.directionXQ14;
		query.directionY = direction.directionYQ14;
		query.directionZ = direction.directionZQ14;
		if ((SlipRaceCollision_BodyFlags(objectOffset) & SLIP_COLLISION_BODY_TRACK_ENABLED) == 0) {
			continue;
		}
		SlipTrackWorld_CheckSlotSamples(currentSlot, objectTable, objectTableBytes, trdBase, trackDataSize,
		                                trdBaseAddress, componentBase, componentBaseBytes, componentBaseAddress, table,
		                                tableBytes, &query);
		if (query.hitFraction != -1) {
			SlipView3DVec32 scaled =
			    SlipView3D_ScaleVector((uint32_t)(int32_t)query.directionX, (uint32_t)(int32_t)query.directionY,
			                           (uint32_t)(int32_t)query.directionZ, query.bestDistance);
			SlipObjectSetPosition setPosition;
			bool carry;

			SlipObject_SetPosition(objectTable, objectTableBytes, objectOffset,
			                       (uint32_t)slot->projectedPosition.x + (uint32_t)scaled.x,
			                       (uint32_t)slot->projectedPosition.y + (uint32_t)scaled.y,
			                       (uint32_t)slot->projectedPosition.z + (uint32_t)scaled.z, &setPosition);
			carry = SlipTrackWorld_QuerySlotCollision(currentSlot, objectTable, objectTableBytes, trdBase,
			                                          trackDataSize, trdBaseAddress, componentBase, componentBaseBytes,
			                                          componentBaseAddress, table, tableBytes, NULL);
			if (carry) {
				SlipView3DVec32 positionStep;
				int32_t fractionStep;
				uint32_t remaining = SLIP_TRACK_COLLISION_BACKOFF_STEPS;

				SlipObject_Position(objectTable, objectTableBytes, objectOffset, &objectPosition);
				positionStep =
				    (SlipView3DVec32){((int32_t)(objectPosition.positionX - (uint32_t)slot->projectedPosition.x)) >>
				                          SLIP_TRACK_COLLISION_BACKOFF_SHIFT,
				                      ((int32_t)(objectPosition.positionY - (uint32_t)slot->projectedPosition.y)) >>
				                          SLIP_TRACK_COLLISION_BACKOFF_SHIFT,
				                      ((int32_t)(objectPosition.positionZ - (uint32_t)slot->projectedPosition.z)) >>
				                          SLIP_TRACK_COLLISION_BACKOFF_SHIFT};
				fractionStep = query.hitFraction >> SLIP_TRACK_COLLISION_BACKOFF_SHIFT;
				do {
					query.hitFraction = (int32_t)((uint32_t)query.hitFraction - (uint32_t)fractionStep);
					SlipObject_Position(objectTable, objectTableBytes, objectOffset, &objectPosition);
					SlipObject_SetPosition(objectTable, objectTableBytes, objectOffset,
					                       objectPosition.positionX - (uint32_t)positionStep.x,
					                       objectPosition.positionY - (uint32_t)positionStep.y,
					                       objectPosition.positionZ - (uint32_t)positionStep.z, &setPosition);
					carry = SlipTrackWorld_QuerySlotCollision(
					    currentSlot, objectTable, objectTableBytes, trdBase, trackDataSize, trdBaseAddress,
					    componentBase, componentBaseBytes, componentBaseAddress, table, tableBytes, NULL);
					if (!carry) {
						break;
					}
					--remaining;
				} while (remaining != 0);
				if (carry) {
					query.hitFraction = 0;
				}
			}

			SlipObject_SetPosition(objectTable, objectTableBytes, objectOffset, (uint32_t)slot->projectedPosition.x,
			                       (uint32_t)slot->projectedPosition.y, (uint32_t)slot->projectedPosition.z,
			                       &setPosition);
			SlipRaceCollision_RecordTrackContact(
			    objectOffset, (uint16_t)query.hitFraction, (uint16_t)query.hitNormalX, (uint16_t)query.hitNormalY,
			    (uint16_t)query.hitNormalZ, query.hitPrimitiveValue, (const int32_t *)(const void *)&query.hitPoint);
		}
		if (query.hitFraction == 0) {
			return;
		}
	}
}

SlipView3DVec32 SlipTrackWorld_BisectRecordBoundary(const uint8_t *trdBase, size_t trackDataSize,
                                                    uint32_t trdBaseAddress, const uint8_t *componentBase,
                                                    size_t componentBaseBytes, const uint8_t *table, size_t tableBytes,
                                                    SlipView3DVec32 first, SlipView3DVec32 second) {
	SlipTrackWorldRecordSearch recordSearch;
	uint32_t firstRecordAddress;
	uint32_t remaining;

	SlipTrackWorld_RecordSearch(trdBase, trackDataSize, componentBase, componentBaseBytes, table, tableBytes,
	                            trdBaseAddress, 0, first.x, first.y, first.z, &recordSearch);
	firstRecordAddress = recordSearch.selectedRecordAddress;
	if (firstRecordAddress == 0) {
		return second;
	}
	remaining = SLIP_TRACK_RECORD_BOUNDARY_BISECTION_STEPS;
	do {
		SlipView3DVec32 midpoint = {(int32_t)(((uint32_t)first.x + (uint32_t)second.x) >> 1),
		                            (int32_t)(((uint32_t)first.y + (uint32_t)second.y) >> 1),
		                            (int32_t)(((uint32_t)first.z + (uint32_t)second.z) >> 1)};

		SlipTrackWorld_RecordSearch(trdBase, trackDataSize, componentBase, componentBaseBytes, table, tableBytes,
		                            trdBaseAddress, 0, midpoint.x, midpoint.y, midpoint.z, &recordSearch);
		if (recordSearch.selectedRecordAddress == firstRecordAddress) {
			first = midpoint;
		} else {
			second = midpoint;
		}
		--remaining;
	} while (remaining != 0);
	return first;
}

bool SlipTrackWorld_ClipRefuelBeam(const uint8_t *trackData, size_t trackDataSize, uint32_t trackDataAddress,
                                   const uint8_t *components, size_t componentBytes, const uint8_t *searchTable,
                                   size_t searchTableBytes, SlipView3DVec32 start, SlipView3DVec32 end,
                                   SlipView3DVec32 *clippedEnd) {

	SlipTrackWorldRecordSearch search;
	SlipView3DNormalizeVector3D direction;
	uint32_t startSection;
	const uint8_t *section;
	const uint8_t *componentList;
	const uint8_t *primitive;
	uint32_t remaining;
	SlipView3DVec32 origin, relativeStart;

	*clippedEnd = end;
	SlipTrackWorld_RecordSearch(trackData, trackDataSize, components, componentBytes, searchTable, searchTableBytes,
	                            trackDataAddress, 0, start.x, start.y, start.z, &search);
	startSection = search.selectedRecordAddress;
	if (startSection == 0)
		return false;
	SlipTrackWorld_RecordSearch(trackData, trackDataSize, components, componentBytes, searchTable, searchTableBytes,
	                            trackDataAddress, 0, end.x, end.y, end.z, &search);
	if (search.selectedRecordAddress == startSection)
		return false;
	SlipView3D_NormalizeVector3D((uint32_t)end.x - (uint32_t)start.x, (uint32_t)end.y - (uint32_t)start.y,
	                             (uint32_t)end.z - (uint32_t)start.z, &direction);
	section = trackData + (startSection - trackDataAddress);
	origin = (SlipView3DVec32){(int32_t)SlipBytes_ReadLE32(section + SLIP_TRD_SECTION_ORIGIN_X_OFFSET),
	                           (int32_t)SlipBytes_ReadLE32(section + SLIP_TRD_SECTION_ORIGIN_Y_OFFSET),
	                           (int32_t)SlipBytes_ReadLE32(section + SLIP_TRD_SECTION_ORIGIN_Z_OFFSET)};
	relativeStart = (SlipView3DVec32){(int32_t)((uint32_t)start.x - (uint32_t)origin.x),
	                                  (int32_t)((uint32_t)start.y - (uint32_t)origin.y),
	                                  (int32_t)((uint32_t)start.z - (uint32_t)origin.z)};
	componentList = components + SlipBytes_ReadLE16(section + SLIP_TRD_SECTION_COMPONENT_OFFSET);
	if (SlipBytes_ReadLE16(componentList + SLIP_TRC_COMPONENT_PRIMITIVE_LIST_OFFSET) == 0)
		return false;
	primitive = components + SlipBytes_ReadLE16(componentList + SLIP_TRC_COMPONENT_PRIMITIVE_LIST_OFFSET);
	remaining = SlipBytes_ReadLE16(primitive);
	primitive += SLIP_TRC_TABLE_COUNT_BYTES;
	do {
		uint16_t descriptor;
		do {
			uint16_t normalX, normalY, normalZ;
			SlipView3DDotProductQ14 facing;
			int16_t incidence;
			SlipTrackWorldPointLookup point;
			int32_t distance;
			uint64_t dividend;
			uint32_t divisor, planeTime;
			SlipView3DVec32 candidate, normalOffset;
			SlipTrackWorldSideTest side;
			if ((primitive[SLIP_PRIMITIVE_MATERIAL_OFFSET] & SLIP_REFUEL_FACE_SKIP_INTERSECTION) != 0)
				break;
			normalX = SlipBytes_ReadLE16(primitive + SLIP_PRIMITIVE_NORMAL_X_OFFSET);
			normalY = SlipBytes_ReadLE16(primitive + SLIP_PRIMITIVE_NORMAL_Y_OFFSET);
			normalZ = SlipBytes_ReadLE16(primitive + SLIP_PRIMITIVE_NORMAL_Z_OFFSET);
			SlipView3D_DotProductQ14(normalX, normalY, normalZ, (uint16_t)direction.unitXQ14,
			                         (uint16_t)direction.unitYQ14, (uint16_t)direction.unitZQ14, &facing);
			if ((int16_t)facing.dotProductQ14 >= 0)
				break;
			incidence = (int16_t)(uint16_t)(0u - (uint16_t)facing.dotProductQ14);
			if (incidence < SLIP_TRACK_RAY_MINIMUM_FACING_Q14)
				break;
			SlipTrackWorld_PointLookup(
			    componentList, components, componentBytes, SlipBytes_ReadLE16(primitive + SLIP_PRIMITIVE_HEADER_BYTES),
			    (facing.dotProductQ14 & SLIP_TRACK_REGISTER_UPPER_WORD_MASK) | (uint16_t)incidence, facing.xySumHigh,
			    facing.inputZ, &point);
			distance = SlipTrackWorld_DotProduct32x16Shift14(
			    (uint32_t)relativeStart.x - point.pointXOrInput, (uint32_t)relativeStart.y - point.pointYOrInput,
			    (uint32_t)relativeStart.z - point.pointZOrCountMergedWithInput, normalX, normalY, normalZ);
			if (distance < 0)
				break;
			dividend = (uint64_t)(uint32_t)distance << SLIP_TRACK_COLLISION_DIVIDEND_SHIFT;
			divisor = (uint32_t)(uint16_t)incidence << SLIP_TRACK_COLLISION_DIVISOR_SHIFT;
			if ((uint32_t)(dividend >> 32) > divisor)
				break;
			planeTime = (uint32_t)(dividend / divisor);
			if ((int32_t)planeTime < 0)
				break;
			candidate =
			    (SlipView3DVec32){(int32_t)((uint32_t)relativeStart.x +
			                                (uint32_t)(((int64_t)(int16_t)direction.unitXQ14 * (int32_t)planeTime) >>
			                                           SLIP_Q14_FRACTION_BITS)),
			                      (int32_t)((uint32_t)relativeStart.y +
			                                (uint32_t)(((int64_t)(int16_t)direction.unitYQ14 * (int32_t)planeTime) >>
			                                           SLIP_Q14_FRACTION_BITS)),
			                      (int32_t)((uint32_t)relativeStart.z +
			                                (uint32_t)(((int64_t)(int16_t)direction.unitZQ14 * (int32_t)planeTime) >>
			                                           SLIP_Q14_FRACTION_BITS))};
			SlipTrackWorld_SideTest(componentList, components, componentBytes, primitive,
			                        componentBytes - (size_t)(primitive - components), candidate, 0, 0, &side);
			if (side.outside)
				break;
			normalOffset = SlipView3D_ScaleVector(normalX, normalY, normalZ, SLIP_TRACK_REFUEL_PLANE_OFFSET);
			*clippedEnd =
			    (SlipView3DVec32){(int32_t)((uint32_t)candidate.x + (uint32_t)normalOffset.x + (uint32_t)origin.x),
			                      (int32_t)((uint32_t)candidate.y + (uint32_t)normalOffset.y + (uint32_t)origin.y),
			                      (int32_t)((uint32_t)candidate.z + (uint32_t)normalOffset.z + (uint32_t)origin.z)};
			return true;
		} while (false);

		descriptor = SlipBytes_ReadLE16(primitive);
		if ((descriptor & SLIP_PRIMITIVE_TEXTURE_COORDINATES) != 0)
			primitive += SLIP_PRIMITIVE_HEADER_BYTES +
			             (descriptor & SLIP_PRIMITIVE_SOLID_COUNT_FLAGS_MASK) *
			                 (SLIP_SERIALIZED_INDEX_BYTES + SLIP_SERIALIZED_TEXTURE_COORDINATE_BYTES);
		else
			primitive += SLIP_PRIMITIVE_HEADER_BYTES + descriptor * SLIP_SERIALIZED_INDEX_BYTES;
		--remaining;
	} while (remaining != 0);
	*clippedEnd = SlipTrackWorld_BisectRecordBoundary(trackData, trackDataSize, trackDataAddress, components,
	                                                  componentBytes, searchTable, searchTableBytes, start, end);
	return true;
}

void SlipTrackWorld_CheckSegmentTransition(const uint8_t *trdBase, size_t trackDataSize, uint32_t trdBaseAddress,
                                           const uint8_t *componentBase, size_t componentBaseBytes,
                                           const uint8_t *table, size_t tableBytes, SlipView3DVec32 segmentStart,
                                           SlipView3DVec32 segmentEnd, SlipTrackWorldSegmentCollision *result) {
	SlipTrackWorldRecordSearch recordSearch;
	uint32_t currentRecordAddress;
	uint32_t endRecordAddress;
	SlipView3DNormalizeVector3D direction;
	SlipView3DVec32 first = segmentStart;
	const SlipView3DVec32 second = segmentEnd;

	memset(result, 0, sizeof(*result));
	result->hitPrimitive = UINT16_MAX;
	result->returned = true;
	SlipTrackWorld_RecordSearch(trdBase, trackDataSize, componentBase, componentBaseBytes, table, tableBytes,
	                            trdBaseAddress, 0, first.x, first.y, first.z, &recordSearch);
	currentRecordAddress = recordSearch.selectedRecordAddress;
	if (currentRecordAddress == 0) {
		result->outputPosition = second;
		return;
	}
	SlipTrackWorld_RecordSearch(trdBase, trackDataSize, componentBase, componentBaseBytes, table, tableBytes,
	                            trdBaseAddress, 0, second.x, second.y, second.z, &recordSearch);
	endRecordAddress = recordSearch.selectedRecordAddress;
	if (endRecordAddress == currentRecordAddress) {
		result->outputPosition = second;
		return;
	}
	SlipView3D_NormalizeVector3D((uint32_t)second.x - (uint32_t)first.x, (uint32_t)second.y - (uint32_t)first.y,
	                             (uint32_t)second.z - (uint32_t)first.z, &direction);

	for (;;) {
		const uint8_t *const currentRecord = trdBase + (currentRecordAddress - trdBaseAddress);
		SlipView3DVec32 relativeStart = {
		    (int32_t)((uint32_t)first.x - SlipBytes_ReadLE32(currentRecord + SLIP_TRD_SECTION_ORIGIN_X_OFFSET)),
		    (int32_t)((uint32_t)first.y - SlipBytes_ReadLE32(currentRecord + SLIP_TRD_SECTION_ORIGIN_Y_OFFSET)),
		    (int32_t)((uint32_t)first.z - SlipBytes_ReadLE32(currentRecord + SLIP_TRD_SECTION_ORIGIN_Z_OFFSET))};
		const uint8_t *const componentList =
		    componentBase + SlipBytes_ReadLE16(currentRecord + SLIP_TRD_SECTION_COMPONENT_OFFSET);
		const uint8_t *primitive;
		uint32_t primitiveCount;
		bool restartRecord = false;

		if (SlipBytes_ReadLE16(componentList + SLIP_TRC_COMPONENT_PRIMITIVE_LIST_OFFSET) == 0) {
			result->outputPosition = second;
			return;
		}
		primitive = componentBase + SlipBytes_ReadLE16(componentList + SLIP_TRC_COMPONENT_PRIMITIVE_LIST_OFFSET);
		primitiveCount = SlipBytes_ReadLE16(primitive);
		primitive += SLIP_TRC_TABLE_COUNT_BYTES;
		do {
			uint16_t descriptor;

			do {
				if ((primitive[SLIP_TRC_PRIMITIVE_FLAGS_OFFSET] & SLIP_TRC_PRIMITIVE_TRENCH) == 0) {
					const uint16_t normalX = SlipBytes_ReadLE16(primitive + SLIP_TRC_PRIMITIVE_NORMAL_X_OFFSET);
					const uint16_t normalY = SlipBytes_ReadLE16(primitive + SLIP_TRC_PRIMITIVE_NORMAL_Y_OFFSET);
					const uint16_t normalZ = SlipBytes_ReadLE16(primitive + SLIP_TRC_PRIMITIVE_NORMAL_Z_OFFSET);
					SlipView3DDotProductQ14 facing;
					int16_t currentFacing;

					SlipView3D_DotProductQ14(normalX, normalY, normalZ, (uint16_t)direction.unitXQ14,
					                         (uint16_t)direction.unitYQ14, (uint16_t)direction.unitZQ14, &facing);
					currentFacing = (int16_t)(uint16_t)facing.dotProductQ14;
					if (currentFacing < 0) {
						const int16_t radius = (int16_t)(uint16_t)(0u - (uint16_t)currentFacing);

						if (radius >= SLIP_TRACK_RAY_MINIMUM_FACING_Q14) {
							SlipTrackWorldPointLookup point;
							SlipView3DVec32 planeDelta;
							int32_t planeDistance;
							uint64_t dividend;
							const uint32_t divisor = (uint32_t)(uint16_t)radius << SLIP_TRACK_COLLISION_DIVISOR_SHIFT;
							uint32_t planeTime;
							SlipView3DVec32 candidate;
							SlipTrackWorldSideTest sideTest;

							memset(&point, 0, sizeof(point));
							SlipTrackWorld_PointLookup(
							    componentList, componentBase, componentBaseBytes,
							    SlipBytes_ReadLE16(primitive + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET),
							    facing.dotProductQ14, facing.xySumHigh, facing.inputZ, &point);
							planeDelta = (SlipView3DVec32){
							    (int32_t)((uint32_t)relativeStart.x - point.pointXOrInput),
							    (int32_t)((uint32_t)relativeStart.y - point.pointYOrInput),
							    (int32_t)((uint32_t)relativeStart.z - point.pointZOrCountMergedWithInput)};
							planeDistance = SlipTrackWorld_DotProduct32x16Shift14(
							    (uint32_t)planeDelta.x, (uint32_t)planeDelta.y, (uint32_t)planeDelta.z, normalX,
							    normalY, normalZ);
							if (planeDistance < 0) {
								break;
							}
							dividend = (uint64_t)(uint32_t)planeDistance << SLIP_TRACK_COLLISION_DIVIDEND_SHIFT;
							if ((uint32_t)(dividend >> 32) > divisor) {
								break;
							}
							planeTime = (uint32_t)(dividend / divisor);
							if ((int32_t)planeTime < 0) {
								break;
							}
							candidate =
							    (SlipView3DVec32){(int32_t)((uint32_t)relativeStart.x +
							                                (uint32_t)((uint64_t)((int64_t)(int16_t)direction.unitXQ14 *
							                                                      (int64_t)(int32_t)planeTime) >>
							                                           SLIP_Q14_FRACTION_BITS)),
							                      (int32_t)((uint32_t)relativeStart.y +
							                                (uint32_t)((uint64_t)((int64_t)(int16_t)direction.unitYQ14 *
							                                                      (int64_t)(int32_t)planeTime) >>
							                                           SLIP_Q14_FRACTION_BITS)),
							                      (int32_t)((uint32_t)relativeStart.z +
							                                (uint32_t)((uint64_t)((int64_t)(int16_t)direction.unitZQ14 *
							                                                      (int64_t)(int32_t)planeTime) >>
							                                           SLIP_Q14_FRACTION_BITS))};
							memset(&sideTest, 0, sizeof(sideTest));
							SlipTrackWorld_SideTest(componentList, componentBase, componentBaseBytes, primitive,
							                        componentBaseBytes - (size_t)(primitive - componentBase), candidate,
							                        0, 0, &sideTest);
							if (sideTest.outside) {
								break;
							}
							if ((primitive[SLIP_TRC_PRIMITIVE_FLAGS_OFFSET] & SLIP_TRC_PRIMITIVE_RANGE_PLANE) == 0) {
								SlipView3DVec32 normalScale = SlipView3D_ScaleVector(
								    (uint32_t)(int32_t)(int16_t)normalX, (uint32_t)(int32_t)(int16_t)normalY,
								    (uint32_t)(int32_t)(int16_t)normalZ, SLIP_TRACK_REFUEL_PLANE_OFFSET);

								result->hitPrimitive =
								    SlipBytes_ReadLE16(primitive + SLIP_TRC_PRIMITIVE_MATERIAL_OFFSET);
								result->hitNormalX = (int16_t)normalX;
								result->hitNormalY = (int16_t)normalY;
								result->hitNormalZ = (int16_t)normalZ;
								candidate.x = (int32_t)((uint32_t)candidate.x + (uint32_t)normalScale.x);
								candidate.y = (int32_t)((uint32_t)candidate.y + (uint32_t)normalScale.y);
								candidate.z = (int32_t)((uint32_t)candidate.z + (uint32_t)normalScale.z);
								result->outputPosition = (SlipView3DVec32){
								    (int32_t)((uint32_t)candidate.x +
								              SlipBytes_ReadLE32(currentRecord + SLIP_TRD_SECTION_ORIGIN_X_OFFSET)),
								    (int32_t)((uint32_t)candidate.y +
								              SlipBytes_ReadLE32(currentRecord + SLIP_TRD_SECTION_ORIGIN_Y_OFFSET)),
								    (int32_t)((uint32_t)candidate.z +
								              SlipBytes_ReadLE32(currentRecord + SLIP_TRD_SECTION_ORIGIN_Z_OFFSET))};
								result->transitionBlocked = true;
								return;
							}
							first = (SlipView3DVec32){
							    (int32_t)((uint32_t)candidate.x +
							              SlipBytes_ReadLE32(currentRecord + SLIP_TRD_SECTION_ORIGIN_X_OFFSET)),
							    (int32_t)((uint32_t)candidate.y +
							              SlipBytes_ReadLE32(currentRecord + SLIP_TRD_SECTION_ORIGIN_Y_OFFSET)),
							    (int32_t)((uint32_t)candidate.z +
							              SlipBytes_ReadLE32(currentRecord + SLIP_TRD_SECTION_ORIGIN_Z_OFFSET))};
							if (componentBase +
							        SlipBytes_ReadLE16(currentRecord + SLIP_TRD_SECTION_FIRST_EXIT_PLANE_OFFSET) ==
							    primitive) {
								currentRecordAddress =
								    trdBaseAddress +
								    SlipBytes_ReadLE16(currentRecord + SLIP_TRD_SECTION_FIRST_EXIT_OFFSET);
							} else if (componentBase + SlipBytes_ReadLE16(currentRecord +
							                                              SLIP_TRD_SECTION_SECOND_EXIT_PLANE_OFFSET) ==
							           primitive) {
								currentRecordAddress =
								    trdBaseAddress +
								    SlipBytes_ReadLE16(currentRecord + SLIP_TRD_SECTION_SECOND_EXIT_OFFSET);
							} else if (componentBase + SlipBytes_ReadLE16(currentRecord +
							                                              SLIP_TRD_SECTION_THIRD_EXIT_PLANE_OFFSET) ==
							           primitive) {
								currentRecordAddress =
								    trdBaseAddress +
								    SlipBytes_ReadLE16(currentRecord + SLIP_TRD_SECTION_THIRD_EXIT_OFFSET);
							} else {
								result->outputPosition = SlipTrackWorld_BisectRecordBoundary(
								    trdBase, trackDataSize, trdBaseAddress, componentBase, componentBaseBytes, table,
								    tableBytes, first, second);
								result->transitionBlocked = true;
								return;
							}
							if (currentRecordAddress == endRecordAddress) {
								result->outputPosition = second;
								return;
							}
							{
								SlipView3DNormalizeVector3D nextDirection;
								SlipView3DDotProductQ14 directionDot;

								SlipView3D_NormalizeVector3D((uint32_t)second.x - (uint32_t)first.x,
								                             (uint32_t)second.y - (uint32_t)first.y,
								                             (uint32_t)second.z - (uint32_t)first.z, &nextDirection);
								SlipView3D_DotProductQ14(
								    (uint16_t)nextDirection.unitXQ14, (uint16_t)nextDirection.unitYQ14,
								    (uint16_t)nextDirection.unitZQ14, (uint16_t)direction.unitXQ14,
								    (uint16_t)direction.unitYQ14, (uint16_t)direction.unitZQ14, &directionDot);
								if ((int16_t)(uint16_t)directionDot.dotProductQ14 < 0) {
									result->outputPosition = (SlipView3DVec32){

									    (int32_t)((direction.unitXQ14 & SLIP_TRACK_REGISTER_UPPER_WORD_MASK) +
									              (nextDirection.unitYQ14 & SLIP_TRACK_REGISTER_UPPER_WORD_MASK) +
									              (direction.unitXQ14 & SLIP_TRACK_REGISTER_UPPER_WORD_MASK) +
									              directionDot.productXHigh + directionDot.productYHigh +
									              ((uint32_t)directionDot.productXLow + directionDot.productYLow >
									               UINT16_MAX) +
									              (directionDot.productZ >> 16) +
									              ((uint32_t)(uint16_t)directionDot.sumXY +
									                   (uint16_t)directionDot.productZ >
									               UINT16_MAX)),
									    (int32_t)((direction.unitYQ14 & SLIP_TRACK_REGISTER_UPPER_WORD_MASK) |
									              (uint16_t)directionDot.sumXY),
									    (int32_t)direction.unitZQ14};
									result->transitionBlocked = true;
									return;
								}
							}
							restartRecord = true;
							break;
						}
					}
				}

			} while (false);
			if (restartRecord) {
				break;
			}
			descriptor = SlipBytes_ReadLE16(primitive);
			if ((descriptor & SLIP_TRC_PRIMITIVE_TEXTURED) == 0) {
				primitive += SLIP_TRC_PRIMITIVE_HEADER_BYTES + (size_t)descriptor * 2u;
			} else {
				primitive +=
				    SLIP_TRC_PRIMITIVE_HEADER_BYTES +
				    (size_t)(descriptor & SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK) * SLIP_TRC_TEXTURED_VERTEX_BYTES;
			}
			--primitiveCount;
		} while (primitiveCount != 0);
		if (restartRecord) {
			continue;
		}
		break;
	}
	result->outputPosition = SlipTrackWorld_BisectRecordBoundary(trdBase, trackDataSize, trdBaseAddress, componentBase,
	                                                             componentBaseBytes, table, tableBytes, first, second);
	result->transitionBlocked = true;
}

bool SlipTrackWorld_CheckLineOfSight(uint16_t firstObject, uint16_t secondObject, uint8_t *slotListBase,
                                     uint32_t slotListBaseAddress, SlipObject *objectTable, size_t objectTableBytes,
                                     const uint8_t *trdBase, size_t trackDataSize, uint32_t trdBaseAddress,
                                     const uint8_t *componentBase, size_t componentBaseBytes,
                                     uint32_t componentBaseAddress, const uint8_t *table, size_t tableBytes) {
	SlipObjectPosition firstPosition;
	SlipObjectPosition secondPosition;
	SlipTrackWorldSlotListSelect select;
	SlipView3DNormalizeVector3D direction;
	uint32_t firstRecordAddress;
	uint32_t secondRecordAddress;

	SlipObject_Position(objectTable, objectTableBytes, firstObject, &firstPosition);
	SlipTrackWorld_SelectSlotListEntry(firstPosition.positionX, slotListBaseAddress, objectTable, objectTableBytes,
	                                   firstObject, &select);
	if (select.carry) {
		SlipRuntime_Fatal("TrackSlotsCheckLOS - not a track slot");
	}
	{
		uint8_t *const startSlot = slotListBase + (select.slotAddress - slotListBaseAddress);

		SlipTrackWorld_UpdateSlotRecord(startSlot, objectTable, objectTableBytes, trdBase, trackDataSize,
		                                trdBaseAddress, componentBase, componentBaseBytes, componentBaseAddress, table,
		                                tableBytes);
		firstRecordAddress = SlipBytes_ReadLE32(startSlot + offsetof(SlipTrackSlotRecord, currentTrackRecordAddress));
	}
	if (firstRecordAddress == 0) {
		return true;
	}

	SlipObject_Position(objectTable, objectTableBytes, secondObject, &secondPosition);
	SlipTrackWorld_SelectSlotListEntry(secondPosition.positionX, slotListBaseAddress, objectTable, objectTableBytes,
	                                   secondObject, &select);
	if (select.carry) {
		SlipRuntime_Fatal("TrackSlotsCheckLOS - not a track slot");
	}
	{
		uint8_t *const endSlot = slotListBase + (select.slotAddress - slotListBaseAddress);

		SlipTrackWorld_UpdateSlotRecord(endSlot, objectTable, objectTableBytes, trdBase, trackDataSize, trdBaseAddress,
		                                componentBase, componentBaseBytes, componentBaseAddress, table, tableBytes);
		secondRecordAddress = SlipBytes_ReadLE32(endSlot + offsetof(SlipTrackSlotRecord, currentTrackRecordAddress));
	}
	if (secondRecordAddress == 0) {
		return true;
	}
	if (secondRecordAddress == firstRecordAddress) {
		return false;
	}

	SlipView3D_NormalizeVector3D(secondPosition.positionX - firstPosition.positionX,
	                             secondPosition.positionY - firstPosition.positionY,
	                             secondPosition.positionZ - firstPosition.positionZ, &direction);

	for (;;) {
		const uint8_t *const record = trdBase + (firstRecordAddress - trdBaseAddress);
		SlipView3DVec32 relative = {
		    (int32_t)(firstPosition.positionX - SlipBytes_ReadLE32(record + SLIP_TRD_SECTION_ORIGIN_X_OFFSET)),
		    (int32_t)(firstPosition.positionY - SlipBytes_ReadLE32(record + SLIP_TRD_SECTION_ORIGIN_Y_OFFSET)),
		    (int32_t)(firstPosition.positionZ - SlipBytes_ReadLE32(record + SLIP_TRD_SECTION_ORIGIN_Z_OFFSET))};
		const uint8_t *const componentList =
		    componentBase + SlipBytes_ReadLE16(record + SLIP_TRD_SECTION_COMPONENT_OFFSET);
		const uint8_t *primitive;
		uint32_t remaining;
		bool restartRecord = false;

		if (SlipBytes_ReadLE16(componentList + SLIP_TRC_COMPONENT_PRIMITIVE_LIST_OFFSET) == 0) {
			return false;
		}
		primitive = componentBase + SlipBytes_ReadLE16(componentList + SLIP_TRC_COMPONENT_PRIMITIVE_LIST_OFFSET);
		remaining = SlipBytes_ReadLE16(primitive);
		primitive += SLIP_TRC_TABLE_COUNT_BYTES;
		do {
			if ((primitive[SLIP_TRC_PRIMITIVE_FLAGS_OFFSET] & SLIP_TRC_PRIMITIVE_RANGE_PLANE) != 0) {
				const uint16_t normalX = SlipBytes_ReadLE16(primitive + SLIP_TRC_PRIMITIVE_NORMAL_X_OFFSET);
				const uint16_t normalY = SlipBytes_ReadLE16(primitive + SLIP_TRC_PRIMITIVE_NORMAL_Y_OFFSET);
				const uint16_t normalZ = SlipBytes_ReadLE16(primitive + SLIP_TRC_PRIMITIVE_NORMAL_Z_OFFSET);
				SlipView3DDotProductQ14 facing;
				int16_t currentFacing;

				SlipView3D_DotProductQ14(normalX, normalY, normalZ, (uint16_t)direction.unitXQ14,
				                         (uint16_t)direction.unitYQ14, (uint16_t)direction.unitZQ14, &facing);
				currentFacing = (int16_t)(uint16_t)facing.dotProductQ14;
				if (currentFacing < 0) {
					const uint16_t facingMagnitude = (uint16_t)(0u - (uint16_t)currentFacing);

					if ((int16_t)facingMagnitude >= SLIP_TRACK_RAY_MINIMUM_FACING_Q14) {
						SlipTrackWorldPointLookup point;
						uint64_t productX;
						uint64_t productY;
						uint64_t productZ;
						uint32_t low;
						uint32_t high;
						uint32_t addend;
						uint32_t carry;
						uint32_t planeDistance;
						uint64_t dividend;
						uint32_t divisor;
						uint32_t fraction;
						SlipView3DVec32 candidate;
						SlipTrackWorldSideTest sideTest;

						SlipTrackWorld_PointLookup(
						    componentList, componentBase, componentBaseBytes,
						    SlipBytes_ReadLE16(primitive + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET), relative.x,
						    relative.y, relative.z, &point);
						productX = (uint64_t)((int64_t)(int32_t)((uint32_t)relative.x - point.pointXOrInput) *
						                      (int64_t)(int32_t)(int16_t)normalX);
						productY = (uint64_t)((int64_t)(int32_t)((uint32_t)relative.y - point.pointYOrInput) *
						                      (int64_t)(int32_t)(int16_t)normalY);
						productZ =
						    (uint64_t)((int64_t)(int32_t)((uint32_t)relative.z - point.pointZOrCountMergedWithInput) *
						               (int64_t)(int32_t)(int16_t)normalZ);
						low = (uint32_t)productX;
						high = (uint32_t)(productX >> 32);
						addend = (uint32_t)productY;
						carry = low + addend < low;
						low += addend;
						high = (high & SLIP_TRACK_REGISTER_UPPER_WORD_MASK) |
						       (uint16_t)((uint16_t)high + (uint16_t)(productY >> 32) + carry);
						addend = (uint32_t)productZ;
						carry = addend + low < addend;
						low += addend;
						high = ((uint32_t)(productZ >> 32) & SLIP_TRACK_REGISTER_UPPER_WORD_MASK) |
						       (uint16_t)((uint16_t)(productZ >> 32) + (uint16_t)high + carry);
						planeDistance = (low >> SLIP_Q14_FRACTION_BITS) | (high << SLIP_Q14_DWORD_HIGH_SHIFT);
						if ((int32_t)planeDistance >= 0) {
							dividend = (uint64_t)planeDistance << SLIP_TRACK_COLLISION_DIVIDEND_SHIFT;
							divisor = (uint32_t)facingMagnitude << SLIP_TRACK_COLLISION_DIVISOR_SHIFT;
							if ((uint32_t)(dividend >> 32) <= divisor) {
								fraction = (uint32_t)(dividend / divisor);
								if ((int32_t)fraction >= 0) {
									candidate = (SlipView3DVec32){
									    (int32_t)((uint32_t)relative.x +
									              (uint32_t)SlipTrackWorld_MultiplySignedShift(
									                  (uint32_t)(int32_t)(int16_t)(uint16_t)direction.unitXQ14,
									                  fraction, SLIP_Q14_FRACTION_BITS)),
									    (int32_t)((uint32_t)relative.y +
									              (uint32_t)SlipTrackWorld_MultiplySignedShift(
									                  (uint32_t)(int32_t)(int16_t)(uint16_t)direction.unitYQ14,
									                  fraction, SLIP_Q14_FRACTION_BITS)),
									    (int32_t)((uint32_t)relative.z +
									              (uint32_t)SlipTrackWorld_MultiplySignedShift(
									                  (uint32_t)(int32_t)(int16_t)(uint16_t)direction.unitZQ14,
									                  fraction, SLIP_Q14_FRACTION_BITS))};
									SlipTrackWorld_SideTest(componentList, componentBase, componentBaseBytes, primitive,
									                        componentBaseBytes - (size_t)(primitive - componentBase),
									                        candidate, 0, 0, &sideTest);
									if (!sideTest.outside) {
										SlipView3DNormalizeVector3D nextDirection;
										SlipView3DDotProductQ14 directionDot;
										uint32_t linkedRecordAddress;

										firstPosition.positionX =
										    SlipBytes_ReadLE32(record + SLIP_TRD_SECTION_ORIGIN_X_OFFSET) +
										    (uint32_t)candidate.x;
										firstPosition.positionY =
										    SlipBytes_ReadLE32(record + SLIP_TRD_SECTION_ORIGIN_Y_OFFSET) +
										    (uint32_t)candidate.y;
										firstPosition.positionZ =
										    SlipBytes_ReadLE32(record + SLIP_TRD_SECTION_ORIGIN_Z_OFFSET) +
										    (uint32_t)candidate.z;
										if (componentBase +
										        SlipBytes_ReadLE16(record + SLIP_TRD_SECTION_FIRST_EXIT_PLANE_OFFSET) ==
										    primitive) {
											linkedRecordAddress =
											    trdBaseAddress +
											    SlipBytes_ReadLE16(record + SLIP_TRD_SECTION_FIRST_EXIT_OFFSET);
										} else if (componentBase +
										               SlipBytes_ReadLE16(record +
										                                  SLIP_TRD_SECTION_SECOND_EXIT_PLANE_OFFSET) ==
										           primitive) {
											linkedRecordAddress =
											    trdBaseAddress +
											    SlipBytes_ReadLE16(record + SLIP_TRD_SECTION_SECOND_EXIT_OFFSET);
										} else if (componentBase +
										               SlipBytes_ReadLE16(record +
										                                  SLIP_TRD_SECTION_THIRD_EXIT_PLANE_OFFSET) ==
										           primitive) {
											linkedRecordAddress =
											    trdBaseAddress +
											    SlipBytes_ReadLE16(record + SLIP_TRD_SECTION_THIRD_EXIT_OFFSET);
										} else {
											return true;
										}
										if (linkedRecordAddress == secondRecordAddress) {
											return false;
										}
										firstRecordAddress = linkedRecordAddress;
										SlipView3D_NormalizeVector3D(secondPosition.positionX - firstPosition.positionX,
										                             secondPosition.positionY - firstPosition.positionY,
										                             secondPosition.positionZ - firstPosition.positionZ,
										                             &nextDirection);
										SlipView3D_DotProductQ14(
										    (uint16_t)nextDirection.unitXQ14, (uint16_t)nextDirection.unitYQ14,
										    (uint16_t)nextDirection.unitZQ14, (uint16_t)direction.unitXQ14,
										    (uint16_t)direction.unitYQ14, (uint16_t)direction.unitZQ14, &directionDot);
										if ((int16_t)(uint16_t)directionDot.dotProductQ14 < 0) {
											return true;
										}
										restartRecord = true;
										break;
									}
								}
							}
						}
					}
				}
			}

			{
				const uint32_t descriptor = SlipBytes_ReadLE16(primitive);

				if ((descriptor & SLIP_TRC_PRIMITIVE_TEXTURED) == 0) {
					primitive += SLIP_TRC_PRIMITIVE_HEADER_BYTES + (size_t)descriptor * 2u;
				} else {
					primitive +=
					    SLIP_TRC_PRIMITIVE_HEADER_BYTES +
					    (size_t)(descriptor & SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK) * SLIP_TRC_TEXTURED_VERTEX_BYTES;
				}
			}
			--remaining;
		} while (remaining != 0);
		if (restartRecord) {
			continue;
		}
		break;
	}
	return true;
}

void SlipTrackWorld_PostCollisionStep(uint8_t *slotListBase, uint32_t slotListBaseAddress,
                                      uint32_t slotListSentinelAddress, SlipObject *objectTable,
                                      size_t objectTableBytes, const uint8_t *trdBase, size_t trackDataSize,
                                      uint32_t trdBaseAddress, const uint8_t *componentBase, size_t componentBaseBytes,
                                      uint32_t componentBaseAddress, const uint8_t *table, size_t tableBytes) {
	uint32_t slotAddress = slotListSentinelAddress;

	for (;;) {
		uint8_t *currentSlot = slotListBase + (slotAddress - slotListBaseAddress);
		SlipTrackSlotRecord *slot = (SlipTrackSlotRecord *)(void *)currentSlot;
		uint16_t objectOffset;
		SlipView3DVec32 lower;
		SlipView3DVec32 upper;
		SlipView3DVec32 midpoint = {0, 0, 0};
		SlipObjectPosition objectPosition;
		SlipObjectSetPosition setPosition;
		bool carry;
		uint32_t cornerIndex;

		slotAddress = slot->nextSlotAddress;
		if (slotAddress == slotListSentinelAddress) {
			return;
		}
		currentSlot = slotListBase + (slotAddress - slotListBaseAddress);
		slot = (SlipTrackSlotRecord *)(void *)currentSlot;
		if ((slot->flags & SLIP_TRACK_SLOT_BOX_COLLISION) == 0) {
			continue;
		}
		objectOffset = (uint16_t)slot->ownerObjectOffset;
		if (SlipObject_Speed(objectTable, objectOffset) == 0) {
			continue;
		}
		carry = SlipTrackWorld_QuerySlotCollision(currentSlot, objectTable, objectTableBytes, trdBase, trackDataSize,
		                                          trdBaseAddress, componentBase, componentBaseBytes,
		                                          componentBaseAddress, table, tableBytes, NULL);
		if (!carry) {
			continue;
		}
		SlipObject_Position(objectTable, objectTableBytes, objectOffset, &objectPosition);
		upper = (SlipView3DVec32){(int32_t)objectPosition.positionX, (int32_t)objectPosition.positionY,
		                          (int32_t)objectPosition.positionZ};
		lower = slot->projectedPosition;
		for (cornerIndex = 0; cornerIndex < SLIP_TRACK_BOUNDING_CORNER_COUNT; ++cornerIndex) {
			if (slot->cornerTrackRecords[cornerIndex] == 0) {
				SlipTrackWorldSegmentCollision segment;
				const int32_t *const corner = &slot->boundsAndCorners[SLIP_TRACK_BOUND_COORDINATE_COUNT +
				                                                      cornerIndex * SLIP_TRACK_CORNER_COORDINATE_COUNT];

				SlipTrackWorld_CheckSegmentTransition(trdBase, trackDataSize, trdBaseAddress, componentBase,
				                                      componentBaseBytes, table, tableBytes, lower,
				                                      (SlipView3DVec32){corner[0], corner[1], corner[2]}, &segment);
				SlipTrackWorld_hitPrimitive = segment.hitPrimitive;
				SlipTrackWorld_hitNormalX = segment.hitNormalX;
				SlipTrackWorld_hitNormalY = segment.hitNormalY;
				SlipTrackWorld_hitNormalZ = segment.hitNormalZ;
				break;
			}
		}
		if (cornerIndex == SLIP_TRACK_BOUNDING_CORNER_COUNT) {
			lower = slot->projectedPosition;
			SlipObject_SetPosition(objectTable, objectTableBytes, objectOffset, (uint32_t)lower.x, (uint32_t)lower.y,
			                       (uint32_t)lower.z, &setPosition);
		} else {
			uint32_t remaining = SLIP_TRACK_COLLISION_BISECTION_STEPS;

			do {
				midpoint = (SlipView3DVec32){((int32_t)((uint32_t)lower.x + (uint32_t)upper.x)) >> 1,
				                             ((int32_t)((uint32_t)lower.y + (uint32_t)upper.y)) >> 1,
				                             ((int32_t)((uint32_t)lower.z + (uint32_t)upper.z)) >> 1};
				SlipObject_SetPosition(objectTable, objectTableBytes, objectOffset, (uint32_t)midpoint.x,
				                       (uint32_t)midpoint.y, (uint32_t)midpoint.z, &setPosition);
				carry = SlipTrackWorld_QuerySlotCollision(
				    currentSlot, objectTable, objectTableBytes, trdBase, trackDataSize, trdBaseAddress, componentBase,
				    componentBaseBytes, componentBaseAddress, table, tableBytes, NULL);
				if (!carry) {
					lower = midpoint;
				} else {
					upper = midpoint;
				}
				--remaining;
			} while (remaining != 0);

			SlipObject_SetPosition(objectTable, objectTableBytes, objectOffset, (uint32_t)lower.z, (uint32_t)midpoint.y,
			                       remaining, &setPosition);
			carry = SlipTrackWorld_QuerySlotCollision(currentSlot, objectTable, objectTableBytes, trdBase,
			                                          trackDataSize, trdBaseAddress, componentBase, componentBaseBytes,
			                                          componentBaseAddress, table, tableBytes, NULL);
			if (carry) {
				lower = slot->projectedPosition;
				SlipObject_SetPosition(objectTable, objectTableBytes, objectOffset, (uint32_t)lower.x,
				                       (uint32_t)lower.y, (uint32_t)lower.z, &setPosition);
			}
		}
		if (SlipTrackWorld_hitPrimitive != UINT16_MAX) {
			SlipRaceCollision_RecordTrackContact(objectOffset, 1, (uint16_t)SlipTrackWorld_hitNormalX,
			                                     (uint16_t)SlipTrackWorld_hitNormalY,
			                                     (uint16_t)SlipTrackWorld_hitNormalZ, SlipTrackWorld_hitPrimitive,
			                                     (const int32_t *)(const void *)&lower);
		}
	}
}

bool SlipTrackWorld_SideTest(const uint8_t *componentList, const uint8_t *componentBase, size_t componentBaseBytes,
                             const uint8_t *plane, size_t planeBytesRemaining, SlipView3DVec32 candidate,
                             SlipTrackWorldSideTestVisit *visits, size_t visitCapacity,
                             SlipTrackWorldSideTest *result) {
	uint16_t normalX;
	uint16_t normalY;
	uint16_t normalZ;
	uint16_t vertexCount;
	const uint8_t *firstIndexCursor;
	const uint8_t *indexCursor;
	uint16_t remaining;
	uint16_t visitIndex = 0;

	if (componentList == 0 || plane == 0 || result == 0 || planeBytesRemaining < SLIP_TRC_PRIMITIVE_FIRST_INDEX_END) {
		return false;
	}
	normalX = SlipBytes_ReadLE16(plane + SLIP_TRC_PRIMITIVE_NORMAL_X_OFFSET);
	normalY = SlipBytes_ReadLE16(plane + SLIP_TRC_PRIMITIVE_NORMAL_Y_OFFSET);
	normalZ = SlipBytes_ReadLE16(plane + SLIP_TRC_PRIMITIVE_NORMAL_Z_OFFSET);
	vertexCount = (uint16_t)(SlipBytes_ReadLE16(plane) & SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK);
	if (vertexCount == 0 ||
	    planeBytesRemaining < SLIP_TRC_PRIMITIVE_HEADER_BYTES + (size_t)vertexCount * SLIP_TRC_VERTEX_INDEX_BYTES) {
		return false;
	}
	*result = (SlipTrackWorldSideTest){
	    .candidate = candidate,
	    .normal = {(int32_t)(int16_t)normalX, (int32_t)(int16_t)normalY, (int32_t)(int16_t)normalZ},
	    .vertexCount = vertexCount,
	    .outside = false,
	    .returned = true};
	firstIndexCursor = plane + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET;
	indexCursor = firstIndexCursor;
	remaining = vertexCount;
	while (remaining != 0) {
		SlipTrackWorldSideTestVisit visit;
		SlipView3DVec32 firstPoint;
		SlipView3DVec32 secondPoint;
		SlipView3DNormalizeVector3D edgeNormalize;
		SlipView3DCrossProduct cross;
		SlipView3DDotProduct32 dot;
		uint16_t firstIndex;
		uint16_t secondIndex;
		const uint8_t *secondIndexCursor;

		memset(&visit, 0, sizeof(visit));
		visit.remainingCount = remaining;
		firstIndex = SlipBytes_ReadLE16(indexCursor);
		visit.firstIndex = firstIndex;
		if (!SlipTrackWorld_PointLookup(componentList, componentBase, componentBaseBytes, firstIndex,
		                                (uint32_t)candidate.x, (uint32_t)candidate.y, (uint32_t)candidate.z,
		                                &visit.firstPoint)) {
			return false;
		}
		firstPoint = (SlipView3DVec32){(int32_t)visit.firstPoint.pointXOrInput, (int32_t)visit.firstPoint.pointYOrInput,
		                               (int32_t)visit.firstPoint.pointZOrCountMergedWithInput};
		secondIndexCursor = remaining == 1u ? firstIndexCursor : indexCursor + SLIP_TRC_VERTEX_INDEX_BYTES;
		secondIndex = SlipBytes_ReadLE16(secondIndexCursor);
		visit.secondIndex = secondIndex;
		if (!SlipTrackWorld_PointLookup(componentList, componentBase, componentBaseBytes, secondIndex,
		                                visit.firstPoint.pointXOrInput, visit.firstPoint.pointYOrInput,
		                                visit.firstPoint.pointZOrCountMergedWithInput, &visit.secondPoint)) {
			return false;
		}
		secondPoint =
		    (SlipView3DVec32){(int32_t)visit.secondPoint.pointXOrInput, (int32_t)visit.secondPoint.pointYOrInput,
		                      (int32_t)visit.secondPoint.pointZOrCountMergedWithInput};
		visit.edgeDelta = (SlipView3DVec32){(int32_t)((uint32_t)secondPoint.x - (uint32_t)firstPoint.x),
		                                    (int32_t)((uint32_t)secondPoint.y - (uint32_t)firstPoint.y),
		                                    (int32_t)((uint32_t)secondPoint.z - (uint32_t)firstPoint.z)};
		if (!SlipView3D_NormalizeVector3D((uint32_t)visit.edgeDelta.x, (uint32_t)visit.edgeDelta.y,
		                                  (uint32_t)visit.edgeDelta.z, &edgeNormalize)) {
			return false;
		}
		visit.edgeNormalize = edgeNormalize;
		SlipView3D_CrossProduct(edgeNormalize.unitXQ14, edgeNormalize.unitYQ14, edgeNormalize.unitZQ14, normalX,
		                        normalY, normalZ, &cross);
		visit.cross = cross;
		visit.negatedCross =
		    (SlipView3DVec32){(int32_t)(0u - cross.crossX), (int32_t)(0u - cross.crossY), (int32_t)(0u - cross.crossZ)};
		visit.candidateDelta = (SlipView3DVec32){(int32_t)((uint32_t)candidate.x - (uint32_t)firstPoint.x),
		                                         (int32_t)((uint32_t)candidate.y - (uint32_t)firstPoint.y),
		                                         (int32_t)((uint32_t)candidate.z - (uint32_t)firstPoint.z)};
		SlipView3D_DotProduct32((uint32_t)visit.negatedCross.x, (uint32_t)visit.negatedCross.y,
		                        (uint32_t)visit.negatedCross.z, (uint32_t)visit.candidateDelta.x,
		                        (uint32_t)visit.candidateDelta.y, (uint32_t)visit.candidateDelta.z, &dot);
		visit.dot = dot;
		visit.negativeSideDot = (int32_t)dot.dotProductHigh < 0;
		if (visits != 0 && visitIndex < visitCapacity) {
			visits[visitIndex] = visit;
		} else if (visits != 0) {
			result->hitVisitCapacity = true;
		}
		++visitIndex;
		result->visitsStored = visitIndex;
		if (visit.negativeSideDot) {
			result->outside = true;
			return true;
		}
		indexCursor += SLIP_TRC_VERTEX_INDEX_BYTES;
		--remaining;
	}
	return true;
}

bool SlipTrackWorld_PreFrameBuild(SlipTrackBeamState *beams, const uint8_t *trackData, size_t trackBytes,
                                  const uint8_t *components, size_t componentBytes, const uint8_t *cellTable,
                                  size_t cellTableBytes, uint32_t trackBase) {
	if (beams->built != 0) {
		beams->queueCount = 0;
		return true;
	}
	beams->built = UINT32_MAX;
	beams->recordCount = 0;

	for (uint32_t requestIndex = 0; requestIndex < beams->queueCount; ++requestIndex) {
		SlipTrackBeamRecord *record = SlipTrackWorld_AllocateBeam(beams);
		if (record == NULL)
			break;
		const SlipTrackBeamRequest *const request = &beams->queue[requestIndex];
		SlipView3DVec32 start = request->start;
		SlipView3DVec32 end = request->end;
		SlipView3DNormalizeVector3D direction;
		SlipView3DMatrix basis;
		SlipTrackWorldRecordSearch search;
		if (!SlipView3D_NormalizeVector3D((uint32_t)end.x - (uint32_t)start.x, (uint32_t)end.y - (uint32_t)start.y,
		                                  (uint32_t)end.z - (uint32_t)start.z, &direction))
			return false;
		SlipView3D_BuildMatrixFromVector(&basis, (int16_t)direction.unitXQ14, (int16_t)direction.unitYQ14,
		                                 (int16_t)direction.unitZQ14);
		if (!SlipTrackWorld_RecordSearch(trackData, trackBytes, components, componentBytes, cellTable, cellTableBytes,
		                                 trackBase, 0, start.x, start.y, start.z, &search))
			return false;
		uint32_t section = search.selectedRecordAddress;
		record->start = start;
		record->continuation = UINT32_MAX;
		bool complete = false;
		for (;;) {

			record->section = section;
			record->material = request->material;
			record->midpoint = (SlipView3DVec32){(int32_t)((uint32_t)start.x + (uint32_t)end.x) >> 1,
			                                     (int32_t)((uint32_t)start.y + (uint32_t)end.y) >> 1,
			                                     (int32_t)((uint32_t)start.z + (uint32_t)end.z) >> 1};
			if (!SlipTrackWorld_RecordSearch(trackData, trackBytes, components, componentBytes, cellTable,
			                                 cellTableBytes, trackBase, 0, end.x, end.y, end.z, &search))
				return false;
			if (search.selectedRecordAddress == section) {
				complete = true;
				break;
			}
			uint32_t sectionOffset;
			if (!SlipTrackWorld_DosAddressToOffset(section, trackBase, trackBytes, &sectionOffset) ||
			    (size_t)sectionOffset + SLIP_TRD_SECTION_ORIGIN_END > trackBytes)
				return false;
			const uint8_t *const sectionData = trackData + sectionOffset;
			SlipView3DVec32 origin = {(int32_t)SlipBytes_ReadLE32(sectionData + SLIP_TRD_SECTION_ORIGIN_X_OFFSET),
			                          (int32_t)SlipBytes_ReadLE32(sectionData + SLIP_TRD_SECTION_ORIGIN_Y_OFFSET),
			                          (int32_t)SlipBytes_ReadLE32(sectionData + SLIP_TRD_SECTION_ORIGIN_Z_OFFSET)};
			const uint16_t componentOffset = SlipBytes_ReadLE16(sectionData + SLIP_TRD_SECTION_COMPONENT_OFFSET);
			if ((size_t)componentOffset + SLIP_TRC_COMPONENT_PRIMITIVE_LIST_END > componentBytes)
				return false;
			const uint8_t *const component = components + componentOffset;
			uint32_t planeOffset = SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_PRIMITIVE_LIST_OFFSET);
			if ((size_t)planeOffset + SLIP_TRC_TABLE_COUNT_BYTES > componentBytes)
				return false;
			const uint16_t planeCount = SlipBytes_ReadLE16(components + planeOffset);
			planeOffset += SLIP_TRC_TABLE_COUNT_BYTES;
			bool nextSection = false;
			for (uint16_t planeIndex = 0; planeIndex < planeCount; ++planeIndex) {
				if ((size_t)planeOffset + SLIP_TRC_PRIMITIVE_FIRST_INDEX_END > componentBytes)
					return false;
				const uint8_t *const plane = components + planeOffset;
				const uint16_t descriptor = SlipBytes_ReadLE16(plane);
				const uint32_t nextPlane = planeOffset + SLIP_TRC_PRIMITIVE_HEADER_BYTES +
				                           ((descriptor & SLIP_TRC_PRIMITIVE_TEXTURED) != 0
				                                ? (uint32_t)(descriptor & SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK) *
				                                      SLIP_TRC_TEXTURED_VERTEX_BYTES
				                                : (uint32_t)descriptor * SLIP_TRC_VERTEX_INDEX_BYTES);
				planeOffset = nextPlane;
				if ((plane[SLIP_TRC_PRIMITIVE_FLAGS_OFFSET] & SLIP_TRC_PRIMITIVE_TRENCH) != 0)
					continue;
				const uint16_t normalX = SlipBytes_ReadLE16(plane + SLIP_TRC_PRIMITIVE_NORMAL_X_OFFSET);
				const uint16_t normalY = SlipBytes_ReadLE16(plane + SLIP_TRC_PRIMITIVE_NORMAL_Y_OFFSET);
				const uint16_t normalZ = SlipBytes_ReadLE16(plane + SLIP_TRC_PRIMITIVE_NORMAL_Z_OFFSET);
				SlipView3DDotProductQ14 facing;
				uint32_t dot = SlipView3D_DotProductQ14(normalX, normalY, normalZ, (uint16_t)basis.m[6],
				                                        (uint16_t)basis.m[7], (uint16_t)basis.m[8], &facing);
				if ((int16_t)dot >= 0)
					continue;
				const uint16_t denominator = (uint16_t)(0u - dot);

				if ((int16_t)denominator < SLIP_TRACK_RAY_MINIMUM_FACING_Q14)
					continue;
				SlipTrackWorldPointLookup point;
				SlipTrackWorldRangePlane rangePlane;
				if (!SlipTrackWorld_PointLookup(component, components, componentBytes,
				                                SlipBytes_ReadLE16(plane + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET),
				                                0u - dot, facing.xySumHigh, facing.inputZ, &point) ||
				    !SlipTrackWorld_StoreRangePlane(point.pointXOrInput, point.pointYOrInput,
				                                    point.pointZOrCountMergedWithInput, normalX, normalY, normalZ,
				                                    &rangePlane))
					return false;
				SlipView3DVec32 delta = {
				    (int32_t)((uint32_t)start.x - (uint32_t)origin.x - (uint32_t)rangePlane.origin.x),
				    (int32_t)((uint32_t)start.y - (uint32_t)origin.y - (uint32_t)rangePlane.origin.y),
				    (int32_t)((uint32_t)start.z - (uint32_t)origin.z - (uint32_t)rangePlane.origin.z)};
				const uint32_t distance = SlipTrackWorld_RoundedDotProductShift14(
				    (uint32_t)delta.x, (uint32_t)delta.y, (uint32_t)delta.z, (uint32_t)rangePlane.normal.x,
				    (uint32_t)rangePlane.normal.y, (uint32_t)rangePlane.normal.z);

				const uint32_t high = (uint32_t)((int32_t)distance >> (32 - SLIP_TRACK_COLLISION_DIVIDEND_SHIFT));
				const uint32_t low = distance << SLIP_TRACK_COLLISION_DIVIDEND_SHIFT;
				const uint32_t divisor = (uint32_t)denominator << SLIP_TRACK_COLLISION_DIVISOR_SHIFT;
				if (high > divisor)
					continue;
				const uint64_t quotient = (((uint64_t)high << 32) | low) / divisor;
				if (quotient > UINT32_MAX)
					return false;
				if (((uint32_t)quotient & SLIP_TRACK_DWORD_SIGN_BIT) != 0)
					continue;
				SlipView3DVec32 candidate = SlipView3D_ScaleAxesQ14((uint16_t)basis.m[6], (uint16_t)basis.m[7],
				                                                    (uint16_t)basis.m[8], (int32_t)quotient);
				candidate.x = (int32_t)((uint32_t)candidate.x + (uint32_t)start.x);
				candidate.y = (int32_t)((uint32_t)candidate.y + (uint32_t)start.y);
				candidate.z = (int32_t)((uint32_t)candidate.z + (uint32_t)start.z);

				SlipView3DVec32 local = {(int32_t)((uint32_t)candidate.x - (uint32_t)origin.x),
				                         (int32_t)((uint32_t)candidate.y - (uint32_t)origin.y),
				                         (int32_t)((uint32_t)candidate.z - (uint32_t)origin.z)};
				SlipTrackWorldSideTest side;
				if (!SlipTrackWorld_SideTest(component, components, componentBytes, plane,
				                             componentBytes - (size_t)(plane - components), local, NULL, 0, &side))
					return false;
				if (side.outside)
					continue;
				if ((plane[SLIP_TRC_PRIMITIVE_FLAGS_OFFSET] & SLIP_TRC_PRIMITIVE_RANGE_PLANE) == 0) {
					end = candidate;
					complete = true;
					break;
				}
				SlipTrackWorldPreFrameScale intersection;
				if (!SlipTrackWorld_PreFrameScale(record->start, origin, rangePlane, denominator, (uint16_t)basis.m[6],
				                                  (uint16_t)basis.m[7], (uint16_t)basis.m[8], &intersection))
					return false;
				record->end = intersection.output;
				bool linked = false;
				for (unsigned link = 0; link < SLIP_TRD_SECTION_EXIT_COUNT; ++link) {
					if (SlipBytes_ReadLE16(sectionData + SLIP_TRD_SECTION_FIRST_EXIT_PLANE_OFFSET +
					                       link * SLIP_TRD_SECTION_EXIT_BYTES) == (size_t)(plane - components)) {
						section = trackBase + SlipBytes_ReadLE16(sectionData + SLIP_TRD_SECTION_FIRST_EXIT_OFFSET +
						                                         link * SLIP_TRD_SECTION_EXIT_BYTES);
						linked = true;
						break;
					}
				}
				if (!linked)
					return false;
				if (!SlipView3D_NormalizeVector3D((uint32_t)end.x - (uint32_t)start.x,
				                                  (uint32_t)end.y - (uint32_t)start.y,
				                                  (uint32_t)end.z - (uint32_t)start.z, &direction))
					return false;
				dot = SlipView3D_DotProductQ14((uint16_t)direction.unitXQ14, (uint16_t)direction.unitYQ14,
				                               (uint16_t)direction.unitZQ14, (uint16_t)basis.m[6], (uint16_t)basis.m[7],
				                               (uint16_t)basis.m[8], &facing);
				if ((int16_t)dot < 0) {
					complete = true;
					break;
				}
				start = candidate;
				SlipTrackBeamRecord *const next = SlipTrackWorld_AllocateBeam(beams);
				if (next == NULL)
					break;
				next->start = record->end;
				next->continuation = UINT32_MAX;
				record = next;
				nextSection = true;
				break;
			}
			if (!nextSection)
				break;
		}
		if (complete) {

			record->material = request->material;
			record->end = end;
			record->continuation = 0;
		}
	}
	beams->queueCount = 0;
	return true;
}

bool SlipTrackWorld_FrameCaller(SlipRaceTrackFrameCallback frameCallback, uint16_t overlayEnable,
                                uint16_t actorReplayMode, uint16_t secondaryActorDrawParameter,
                                uint16_t auxiliaryActorDrawParameter, uint32_t savedClipLeft, uint32_t savedClipTop,
                                uint32_t savedClipRight, uint32_t savedClipBottom, int32_t trkHeaderGate,
                                SlipTrackWorldFrameCaller *result) {
	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldFrameCaller){.savedCallerState = true,
	                                      .overlayEnable = overlayEnable,
	                                      .actorReplayMode = actorReplayMode,
	                                      .secondaryActorDrawParameter = secondaryActorDrawParameter,
	                                      .auxiliaryActorDrawParameter = auxiliaryActorDrawParameter,
	                                      .callGetRasterClip = true,
	                                      .savedClipLeft = savedClipLeft,
	                                      .savedClipTop = savedClipTop,
	                                      .savedClipRight = savedClipRight,
	                                      .savedClipBottom = savedClipBottom,
	                                      .callTrackWorldLoadClipRegisters = true,
	                                      .callSetFrameRasterClip = true,
	                                      .callTrackWorldInitRefuel = true,
	                                      .callBuildReplaySources = true,
	                                      .drawStateIndexForChunk = 0,
	                                      .callLoadDrawStateForChunk = true,
	                                      .trackHeaderGate = trkHeaderGate,
	                                      .callTrackWorldPreFrameBuild = true,
	                                      .cameraDrawStateIndex = 0,
	                                      .callTrackWorldPreFrameCameraPrefix = true,
	                                      .restoredDrawStateIndex = 0,
	                                      .callRestoreDrawStateAfterChunk = true,
	                                      .frameCallback = frameCallback,
	                                      .callTrackWorldFrameEntry = true,
	                                      .restoredClipBottom = savedClipBottom,
	                                      .restoredClipRight = savedClipRight,
	                                      .restoredClipTop = savedClipTop,
	                                      .restoredClipLeft = savedClipLeft,
	                                      .callRestoreRasterClip = true,
	                                      .restoredCallerState = true,
	                                      .ret = true};
	return true;
}

bool SlipTrackWorld_PreFrameCameraPrefix(uint32_t globalGate, uint32_t savedFrameCallback, uint32_t savedViewportLeft,
                                         uint32_t savedViewportTop, uint32_t savedViewportRight,
                                         uint32_t savedViewportBottom, uint32_t savedViewportCenterX,
                                         uint32_t savedViewportCenterY, uint16_t materialFrameOffset,
                                         uint32_t materialWidth, uint32_t materialHeight,
                                         uint16_t materialViewportWidth, uint32_t savedDepth, uint32_t rangeOriginY,
                                         SlipView3DVec32 objectPositionFrom,
                                         SlipTrackWorldPreFrameCameraPrefix *result) {
	SlipTrackWorldRangePlane rangePlane;
	uint32_t viewportMaxX;
	uint32_t viewportMaxY;
	uint32_t dotRounded;
	uint32_t dotNegated;
	uint32_t scaledDistance;
	SlipView3DVec32 scaledForward;

	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldPreFrameCameraPrefix){.savedCallerState = true,
	                                               .reflectionEnabled = globalGate,
	                                               .branch = SLIP_TRACK_WORLD_PRE_FRAME_CAMERA_BRANCH_GLOBAL_DISABLED,
	                                               .restoredCallerState = true,
	                                               .ret = true};
	if (globalGate == 0) {
		return true;
	}
	result->savedFrameCallback = savedFrameCallback;
	result->callDraw3DLoadClipAndCenter = true;
	result->savedViewportLeft = savedViewportLeft;
	result->savedViewportTop = savedViewportTop;
	result->savedViewportRight = savedViewportRight;
	result->savedViewportBottom = savedViewportBottom;
	result->savedViewportCenterX = savedViewportCenterX;
	result->savedViewportCenterY = savedViewportCenterY;
	result->materialNameAddress = SLIP_TRACK_REFLECTION_MATERIAL_NAME_TOKEN;
	result->callLookupMaterial = true;
	result->callMaterialGetFrame = true;
	result->materialFrameOffset = materialFrameOffset;
	result->branch = SLIP_TRACK_WORLD_PRE_FRAME_CAMERA_BRANCH_MATERIAL_MISSING;
	if (materialFrameOffset == 0) {
		return true;
	}

	result->callReadMaterialDimensions = true;
	viewportMaxX = materialWidth - 1u;
	viewportMaxY = materialHeight - 1u;
	result->viewportMaxX = viewportMaxX;
	result->viewportMaxY = viewportMaxY;
	result->viewportCenterX = (int32_t)viewportMaxX >> 1;
	result->viewportCenterY = (int32_t)viewportMaxY >> 1;
	result->callSetMaterialViewport = true;
	result->materialViewportWidth = materialViewportWidth;
	result->viewportReadMode = SLIP_TRACK_REFLECTION_VIEWPORT_READ_MODE;
	result->callReadMaximumDepth = true;
	result->savedDepth = savedDepth;
	result->reflectionMaximumDepth = SLIP_TRACK_REFLECTION_MAXIMUM_DEPTH;
	result->callDraw3DSetMaximumDepth = true;
	result->rangePlaneObjectOffset = 0;
	result->callReadRangePlaneObjectPosition = true;
	result->rangeOriginXTo = 0;
	result->rangeOriginYTo = rangeOriginY;
	result->rangeOriginZTo = 0;
	result->rangeNormalXTo = 0;
	result->rangeNormalYTo = SLIP_Q14_ONE;
	result->rangeNormalZTo = 0;
	result->callTrackWorldStoreRangePlane = true;
	if (!SlipTrackWorld_StoreRangePlane(result->rangeOriginXTo, result->rangeOriginYTo, result->rangeOriginZTo,
	                                    result->rangeNormalXTo, result->rangeNormalYTo, result->rangeNormalZTo,
	                                    &rangePlane)) {
		return false;
	}
	result->rangePlane = rangePlane;
	result->reflectedObjectOffset = 0;
	result->callReadReflectedObjectPosition = true;
	result->objectPosition = objectPositionFrom;
	result->rangeDelta = (SlipView3DVec32){(int32_t)((uint32_t)objectPositionFrom.x - (uint32_t)rangePlane.origin.x),
	                                       (int32_t)((uint32_t)objectPositionFrom.y - (uint32_t)rangePlane.origin.y),
	                                       (int32_t)((uint32_t)objectPositionFrom.z - (uint32_t)rangePlane.origin.z)};
	dotRounded = SlipTrackWorld_RoundedDotProductShift14((uint32_t)result->rangeDelta.x, (uint32_t)result->rangeDelta.y,
	                                                     (uint32_t)result->rangeDelta.z, (uint32_t)rangePlane.normal.x,
	                                                     (uint32_t)rangePlane.normal.y, (uint32_t)rangePlane.normal.z);
	dotNegated = (uint32_t)(0u - dotRounded);
	scaledDistance = dotNegated << 1;
	scaledForward = SlipView3D_ScaleAxesQ14(0, SLIP_Q14_ONE, 0, (int32_t)scaledDistance);
	result->rangeDotRounded = dotRounded;
	result->rangeDotNegated = dotNegated;
	result->forwardScale = (int32_t)scaledDistance;
	result->scaledForward = scaledForward;
	result->callScaleReflectionDisplacement = true;
	result->adjustedObjectPositionTo =
	    (SlipView3DVec32){(int32_t)((uint32_t)scaledForward.x + (uint32_t)objectPositionFrom.x),
	                      (int32_t)((uint32_t)scaledForward.y + (uint32_t)objectPositionFrom.y),
	                      (int32_t)((uint32_t)scaledForward.z + (uint32_t)objectPositionFrom.z)};
	result->positionInstallObjectOffset = 0;
	result->callObjectSetPosition = true;
	result->callReadObjectWorldMatrix = true;
	result->matrixSaveSource = savedViewportCenterY;
	result->matrixSaveDestination = SLIP_TRACK_REFLECTION_SAVED_MATRIX_TOKEN;
	result->callView3DCopyMatrixWords = true;
	result->branch = SLIP_TRACK_WORLD_PRE_FRAME_CAMERA_BRANCH_ACTIVE_PREFIX;
	return true;
}

bool SlipTrackWorld_PreFrameCameraSuffix(uint32_t frameCallbackTo, SlipView3DVec32 savedObjectPosition,
                                         uint32_t savedDepth, uint32_t restoredViewportLeft,
                                         uint32_t restoredViewportTop, uint32_t restoredViewportRight,
                                         uint32_t restoredViewportBottom, uint32_t restoredViewportCenterX,
                                         uint32_t restoredViewportCenterY, SlipTrackWorldPreFrameCameraSuffix *result) {
	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldPreFrameCameraSuffix){.reflectionMatrixObjectOffset = 0,
	                                               .callReadReflectionObjectMatrix = true,
	                                               .reflectionNormalX = 0,
	                                               .reflectionNormalY = SLIP_Q14_ONE,
	                                               .reflectionNormalZ = 0,
	                                               .callView3DReflectMatrixRows = true,
	                                               .callEnablePostPlaneMode = true,
	                                               .reflectionMatrixInstallObjectOffset = 0,
	                                               .callInstallReflectionObjectMatrix = true,
	                                               .reflectionCameraObjectOffset = 0,
	                                               .callReadReflectionCameraMatrix = true,
	                                               .callReadReflectionCameraPosition = true,
	                                               .callSetReflectionCameraOrigin = true,
	                                               .frameCallbackTo = frameCallbackTo,
	                                               .callTrackWorldFrameEntry = true,
	                                               .restoreObjectPositionTo = savedObjectPosition,
	                                               .positionRestoreObjectOffset = 0,
	                                               .callRestoreObjectPosition = true,
	                                               .restoreDepthTo = savedDepth,
	                                               .callRestoreMaximumDepth = true,
	                                               .matrixRestoreAddress = SLIP_TRACK_REFLECTION_SAVED_MATRIX_TOKEN,
	                                               .matrixRestoreObjectOffset = 0,
	                                               .callRestoreObjectMatrix = true,
	                                               .restoredCameraObjectOffset = 0,
	                                               .callReadRestoredCameraMatrix = true,
	                                               .callReadRestoredCameraPosition = true,
	                                               .callRestoreCameraOrigin = true,
	                                               .callDisablePostPlaneMode = true,
	                                               .restoredViewportLeft = restoredViewportLeft,
	                                               .restoredViewportTop = restoredViewportTop,
	                                               .restoredViewportRight = restoredViewportRight,
	                                               .restoredViewportBottom = restoredViewportBottom,
	                                               .restoredViewportCenterX = restoredViewportCenterX,
	                                               .restoredViewportCenterY = restoredViewportCenterY,
	                                               .callDraw3DSetViewport = true,
	                                               .restoredCallerState = true,
	                                               .ret = true};
	return true;
}

bool SlipTrackWorld_CameraFrame(uint16_t cameraObjectOffset, uint32_t viewportLeftBeforeInset,
                                uint32_t viewportTopBeforeInset, uint32_t viewportRightBeforeInset,
                                uint32_t viewportBottomBeforeInset, uint32_t pushedProjection,
                                SlipRaceTrackFrameCallback frameCallbackFrom, uint32_t viewportLeftAfterInset,
                                uint32_t viewportTopAfterInset, uint32_t viewportRightAfterInset,
                                uint32_t viewportBottomAfterInset, int32_t trkHeaderGate, uint16_t weaponLabelResource,
                                uint32_t labelTopBeforeInset, uint32_t labelHeight, uint32_t labelWidth,
                                SlipTrackWorldCameraFrame *result) {
	SlipTrackWorldFrameCaller frameCaller;
	uint32_t viewportLeft;
	uint32_t viewportTop;
	uint32_t viewportRight;
	uint32_t viewportBottom;

	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldCameraFrame){.savedCallerState = true,
	                                      .cameraObjectOffset = cameraObjectOffset,
	                                      .missingCameraObject = cameraObjectOffset == 0,
	                                      .branch = SLIP_TRACK_WORLD_CAMERA_FRAME_BRANCH_MISSING_OBJECT,
	                                      .setCarry = cameraObjectOffset == 0,
	                                      .restoredCallerState = true,
	                                      .ret = true};
	if (cameraObjectOffset == 0) {
		return true;
	}

	if (!SlipTrackWorld_FrameCaller(frameCallbackFrom, 0, 0, 0, 0, viewportLeftAfterInset, viewportTopAfterInset,
	                                viewportRightAfterInset, viewportBottomAfterInset, trkHeaderGate, &frameCaller)) {
		return false;
	}

	viewportLeft = viewportLeftBeforeInset + 1u;
	viewportTop = viewportTopBeforeInset + 1u;
	viewportRight = viewportRightBeforeInset - 1u;
	viewportBottom = viewportBottomBeforeInset - 1u;
	result->callObjectHide = true;
	result->cameraViewportSelector = SLIP_TRACK_CAMERA_VIEWPORT_SELECTOR;
	result->callGetCameraViewport = true;
	result->viewportLeft = viewportLeft;
	result->viewportTop = viewportTop;
	result->viewportRight = viewportRight;
	result->viewportBottom = viewportBottom;
	result->viewportCenterX = (int32_t)((uint32_t)viewportRight + viewportLeft) >> 1;
	result->viewportCenterY = (int32_t)((uint32_t)viewportBottom + viewportTop) >> 1;
	result->callDraw3DSetViewport = true;
	result->callRendererBegin = true;
	result->callReadProjectionScale = true;
	result->pushedProjection = pushedProjection;
	result->projectionScaleTo = SLIP_Q14_ONE;
	result->callSetProjectionScale = true;
	result->callReadCameraObjectPosition = true;
	result->callReadCameraObjectMatrix = true;
	result->positionInstallObjectOffset = 0;
	result->callObjectSetPosition = true;
	result->matrixInstallObjectOffset = 0;
	result->callObjectMatrixInstall = true;
	result->callRendererSetCamera = true;
	result->frameCallbackFrom = frameCallbackFrom;
	result->frameOverlayEnable = 0;
	result->frameActorReplayMode = 0;
	result->frameSecondaryActorDrawParameter = 0;
	result->frameAuxiliaryActorDrawParameter = 0;
	result->callFrameCaller = true;
	result->frameCaller = frameCaller;
	result->callAfterFrame = true;
	result->savedLabelTop = true;
	result->weaponLabelResource = weaponLabelResource;
	result->callReadWeaponLabelDimensions = true;
	result->labelHeight = labelHeight;
	result->labelWidth = labelWidth;
	result->textStyle = SLIP_TEXT_CENTERED;
	result->textBackgroundColour = UINT32_MAX;
	result->callTextSetStyle = true;
	result->textColour = SLIP_TRACK_WEAPON_LABEL_COLOUR;
	result->callTextSetColor = true;
	result->labelTop = labelTopBeforeInset + SLIP_TRACK_WEAPON_LABEL_TOP_INSET;
	result->callRacePlayerProjectileWeaponIndex = true;
	result->callRacePlayerBuildWeaponLabel = true;
	result->callDrawWeaponLabel = true;
	result->restoredProjection = pushedProjection;
	result->callRestoreProjectionScale = true;
	result->shownObjectOffset = cameraObjectOffset;
	result->callObjectShow = true;
	result->clearCarry = true;
	result->setCarry = false;
	result->branch = SLIP_TRACK_WORLD_CAMERA_FRAME_BRANCH_CALLED_FRAME;
	return true;
}

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
    SlipTrackWorldSphereCull sphereCull, void *sphereCullUserData, SlipTrackWorldShapeDraw shapeDraw,
    void *shapeDrawUserData, SlipTrackWorldComponentLight scaledLight, SlipTrackWorldComponentLight restoreLight,
    void *lightUserData, SlipTrackWorldFrameEntry *result) {
	SlipTrackWorldFrameDrawState frameDrawState;
	SlipTrackWorldFrameDispatch frameDispatch;
	SlipTrackWorldClearGlobals clearGlobals;
	SlipTrackWorldStateReset stateReset;
	SlipTrackWorldCameraSetupExecute cameraSetup;
	SlipTrackWorldBuildAxisRamps axisRamps;
	SlipTrackWorldBuildAxisTests axisTests;
	SlipTrackWorldListSetup listSetup;
	SlipTrackWorldSlotDrawClear slotDrawClear;
	SlipTrackWorldDeferredListDirectExecute deferredDirect;
	SlipTrackWorldPostFrameOverlay postFrameOverlay;
	SlipView3DMatrix worldMatrix;
	SlipView3DMatrix objectViewMatrix;

	if (result == 0) {
		return false;
	}
	if (!SlipTrackWorld_FrameDrawState(drawFlagsFrom, shading, shadingSecondary, &frameDrawState)) {
		return false;
	}
	if (!SlipTrackWorld_CameraSetupExecute(objectTableBase, objectTableSize, objectTransformMatrix, viewMatrix,
	                                       &cameraSetup)) {
		return false;
	}

	if (traversalContext != 0) {
		traversalContext->origin = cameraSetup.origin;
	}
	if (!SlipTrackWorld_StateReset(recordIndex, &stateReset)) {
		return false;
	}
	if (!SlipTrackWorld_BuildAxisRamps((const uint8_t *)viewMatrix, sizeof(*viewMatrix), rampX, rampXBytes, rampY,
	                                   rampYBytes, rampZ, rampZBytes, &axisRamps)) {
		return false;
	}
	if (!SlipTrackWorld_BuildAxisTests(mode, (const uint8_t *)viewMatrix, sizeof(*viewMatrix), rampX, rampXBytes, rampY,
	                                   rampYBytes, rampZ, rampZBytes, tableFirst, tableFirstBytes, tableSecond,
	                                   tableSecondBytes, tableThird, tableThirdBytes, cameraSetup.origin.x,
	                                   cameraSetup.origin.y, cameraSetup.origin.z, axisTestVisits,
	                                   axisTestVisitCapacity, &axisTests)) {
		return false;
	}
	if (!SlipTrackWorld_ListSetup(
	        objectList, objectListBytes, objectListBaseAddress, deferredList, deferredListBytes, frameRenderFlags,
	        defaultTraversalGate, recordIndex, savedClipLeft, savedClipTop, savedClipRight, savedClipBottom, clipMinX,
	        clipMinY, clipMaxX, clipMaxY, traversalCallback, recordCallback, depthFrom, cameraSetup.cameraPositionX,
	        cameraSetup.cameraPositionY, cameraSetup.cameraPositionZ, (const uint8_t *)viewMatrix, sizeof(*viewMatrix),
	        objectTableBase, objectTableSize, trkBase, trkSize, trdBase, trackDataSize, componentBase,
	        componentBaseBytes, table, tableBytes, trdBaseAddress, recordAddress, traversalContext, &listSetup)) {
		return false;
	}
	if (!SlipTrackWorld_ClearSlotDrawLinks(slotDrawBase, slotDrawBytes, slotDrawCount, slotDrawClearVisits,
	                                       slotDrawClearVisitCapacity, &slotDrawClear)) {
		return false;
	}
	if (!SlipTrackWorld_ClearGlobals(&clearGlobals)) {
		return false;
	}

	if (listSetup.renderContextCount != 0 &&
	    (storeClipBoundsFunction == 0 ||
	     !storeClipBoundsFunction(listSetup.primaryLeft, listSetup.primaryTop, listSetup.primaryRight,
	                              listSetup.primaryBottom, storeClipBoundsUserData))) {
		return false;
	}
	if (!SlipTrackWorld_FrameDispatch(listSetup.renderContextCount, listSetup.primaryLeft, listSetup.primaryTop,
	                                  listSetup.primaryRight, listSetup.primaryBottom, listSetup.componentMask,
	                                  underSeaColor, traversalCallback, depthFrom, savedFadeStart, savedFadeEnd,
	                                  savedFadeColour, frameCallback, listSetup.storedDefaultTraversalGate, trkBase,
	                                  trkSize, traversalContext, callbackArgs, &frameDispatch)) {
		return false;
	}
	if (!SlipTrackWorld_DeferredListDirectExecute(
	        frameRenderFlags, savedClipLeft, savedClipTop, savedClipRight, savedClipBottom, deferredList,
	        deferredListBytes, deferredEntryMap, deferredEntryMapCount, deferredRecordMap, deferredRecordMapCount,
	        componentBaseAddress, componentBase, componentBaseBytes, listSetup.componentMask, mode,
	        SLIP_TRACK_FRAME_STATE_TOKEN, detailThreshold, recordIndex, &worldMatrix, &objectViewMatrix, viewMatrix,
	        cameraSetup.cameraPositionX, cameraSetup.cameraPositionZ, sphereCull, sphereCullUserData, shapeDraw,
	        shapeDrawUserData, 0, frameDrawState.drawFlagsTo, textureMode, shading, componentDistance, shadingSecondary,
	        componentRadius, frameDrawState.storedFrameFlags, depthFrom, savedFadeStart, savedFadeEnd, savedFadeColour,
	        specialRecord, globalAfter, randomState, shadows, processedComponentCount, actorReplayMode,
	        ambientLightScaleQ14, scaledLightX, scaledLightY, scaledLightZ, directLightScaleQ14,
	        listSetup.renderContextCount, primitiveCallback, storeClipBoundsFunction, storeClipBoundsUserData,
	        callbackFunction, callbackUserData, componentActorDraw, componentActorDrawUserData, vertexRecords,
	        vertexRecordCapacity, drawStateLoad, drawStateLoadUserData, buildVertexRecords, buildVertexRecordsUserData,
	        restoreVertexBuffer, restoreVertexBufferUserData, deferredDirectVisits, deferredDirectVisitCapacity,
	        scaledLight, restoreLight, lightUserData, traversalContext ? traversalContext->refuelCalls : NULL,
	        &deferredDirect)) {
		*result = (SlipTrackWorldFrameEntry){
		    .frameCallback = frameCallback,
		    .storedFrameCallback = frameCallback,
		    .callTrackWorldFrameDrawState = true,
		    .frameDrawState = frameDrawState,
		    .callDraw3DLoadClipAndCenter = true,
		    .savedClipLeft = savedClipLeft,
		    .savedClipTop = savedClipTop,
		    .savedClipRight = savedClipRight,
		    .savedClipBottom = savedClipBottom,
		    .callReadMaximumDepth = true,
		    .savedDepth = depthFrom,
		    .callReadDepthFade = true,
		    .savedFadeStart = savedFadeStart,
		    .savedFadeEnd = savedFadeEnd,
		    .savedFadeColour = savedFadeColour,
		    .callTrackWorldCameraSetup = true,
		    .callTrackWorldStateReset = true,
		    .stateReset = stateReset,
		    .callReadShapeFlags = true,
		    .maskedShapeFlags = (uint16_t)(timerValue & ~(SLIP_SHAPE_SHORT_COORDINATES | SLIP_SHAPE_CLIP_AUXILIARY |
		                                                  SLIP_SHAPE_FORCE_SECONDARY_PROJECTION)),
		    .callRendererSetShapeFlags = true,
		    .callReadStateToken = true,
		    .pushedStateToken = stateTokenFrom,
		    .temporaryStateToken = SLIP_TRACK_FRAME_STATE_TOKEN,
		    .callInstallTemporaryStateToken = true,
		    .callTrackWorldBuildAxisRamps = true,
		    .callTrackWorldBuildAxisTests = true,
		    .callSetupObjectList = true,
		    .listSetup = listSetup,
		    .callTrackWorldClearSlotDrawLinks = true,
		    .callTrackWorldClearGlobals = true,
		    .clear = clearGlobals,
		    .callTrackWorldFrameDispatch = true,
		    .frameDispatch = frameDispatch,
		    .callExecuteDeferredList = true,
		    .deferredDirect = deferredDirect};
		return false;
	}

	if (storeClipBoundsFunction == 0 || !storeClipBoundsFunction(savedClipLeft, savedClipTop, savedClipRight,
	                                                             savedClipBottom, storeClipBoundsUserData)) {
		return false;
	}
	if (!SlipTrackWorld_PostFrameOverlay(overlayEnable, listSetup.renderContextCount, listSetup.primaryLeft,
	                                     listSetup.primaryTop, listSetup.primaryRight, listSetup.primaryBottom,
	                                     secondaryLeft, secondaryRight, secondaryTop, secondaryBottom,
	                                     &postFrameOverlay)) {
		return false;
	}
	*result = (SlipTrackWorldFrameEntry){
	    .frameCallback = frameCallback,
	    .storedFrameCallback = frameCallback,
	    .callTrackWorldFrameDrawState = true,
	    .frameDrawState = frameDrawState,
	    .callDraw3DLoadClipAndCenter = true,
	    .savedClipLeft = savedClipLeft,
	    .savedClipTop = savedClipTop,
	    .savedClipRight = savedClipRight,
	    .savedClipBottom = savedClipBottom,
	    .callReadMaximumDepth = true,
	    .savedDepth = depthFrom,
	    .callReadDepthFade = true,
	    .savedFadeStart = savedFadeStart,
	    .savedFadeEnd = savedFadeEnd,
	    .savedFadeColour = savedFadeColour,
	    .callTrackWorldCameraSetup = true,
	    .callTrackWorldStateReset = true,
	    .stateReset = stateReset,
	    .callReadShapeFlags = true,
	    .maskedShapeFlags = (uint16_t)(timerValue & ~(SLIP_SHAPE_SHORT_COORDINATES | SLIP_SHAPE_CLIP_AUXILIARY |
	                                                  SLIP_SHAPE_FORCE_SECONDARY_PROJECTION)),
	    .callRendererSetShapeFlags = true,
	    .callReadStateToken = true,
	    .pushedStateToken = stateTokenFrom,
	    .temporaryStateToken = SLIP_TRACK_FRAME_STATE_TOKEN,
	    .callInstallTemporaryStateToken = true,
	    .callTrackWorldBuildAxisRamps = true,
	    .callTrackWorldBuildAxisTests = true,
	    .callSetupObjectList = true,
	    .listSetup = listSetup,
	    .callTrackWorldClearSlotDrawLinks = true,
	    .callTrackWorldClearGlobals = true,
	    .clear = clearGlobals,
	    .callTrackWorldFrameDispatch = true,
	    .frameDispatch = frameDispatch,
	    .callExecuteDeferredList = true,
	    .deferredDirect = deferredDirect,
	    .restoredClipLeft = savedClipLeft,
	    .restoredClipTop = savedClipTop,
	    .restoredClipRight = savedClipRight,
	    .restoredClipBottom = savedClipBottom,
	    .callRestoreClipBounds = true,
	    .restoredStateToken = stateTokenFrom,
	    .callRestoreStateToken = true,
	    .callTrackWorldPostFrameOverlay = true,
	    .ret = true};
	return true;
}

enum {
	SLIP_TRACK_BACKGROUND_FIXED_STRIP_COUNT = 16,
	SLIP_TRACK_BACKGROUND_MATERIAL_STRIP_COUNT = 1,
	SLIP_TRACK_BACKGROUND_DESERT_HEIGHT_OFFSET = 1952000,
	SLIP_TRACK_BACKGROUND_DEFAULT_HEIGHT_OFFSET = 1048576,
	SLIP_TRACK_BACKGROUND_LONDON_HEIGHT_OFFSET = 976000,
	SLIP_TRACK_BACKGROUND_DEFAULT_COLOUR = 0x03120388,
	SLIP_TRACK_BACKGROUND_CHICAGO_COLOUR = 0x40000000,
	SLIP_TRACK_BACKGROUND_HAWAII_FRANCE_COLOUR = 0x28000000,
	SLIP_TRACK_BACKGROUND_NEW_YORK_COLOUR = 0x01000000,
	SLIP_TRACK_BACKGROUND_AMAZON_COLOUR = 1,
	SLIP_TRACK_BACKGROUND_DEFAULT_CURVATURE = 1536,
	SLIP_TRACK_BACKGROUND_CHICAGO_CURVATURE = 6144,
	SLIP_TRACK_BACKGROUND_COASTAL_CURVATURE = 4608,
	SLIP_TRACK_BACKGROUND_LONDON_CURVATURE = 3072,
	SLIP_TRACK_BACKGROUND_LONDON_CLOUD_DETAIL = 3,
	SLIP_TRACK_SKY_NAME_DOS_TOKEN = 0x42c9e,
	SLIP_TRACK_GROUND_NAME_DOS_TOKEN = 0x42ca2
};

#define SLIP_RACE_TRACK_BACKGROUND_BODY(EBX_AFTER_OFFSET, BACKGROUND_EAX, SCALE_EBP)                                   \
	SlipRaceTrackFrameCallbackExecute out;                                                                             \
	if (args == NULL || result == NULL) {                                                                              \
		return false;                                                                                                  \
	}                                                                                                                  \
	memset(&out, 0, sizeof(out));                                                                                      \
	out.frame = (SlipRaceTrackBackgroundState){.savedCallerState = true,                                               \
	                                           .cameraObjectOffset = 0,                                                \
	                                           .callObjectPosition = true,                                             \
	                                           .callObjectMatrix = true,                                               \
	                                           .cameraHeight = args->cameraHeight,                                     \
	                                           .skyHeight = (EBX_AFTER_OFFSET),                                        \
	                                           .backgroundColour = (BACKGROUND_EAX),                                   \
	                                           .stripCurvature = (SCALE_EBP),                                          \
	                                           .skyMaterial = args->skyMaterial,                                       \
	                                           .fixedStripCount = SLIP_TRACK_BACKGROUND_FIXED_STRIP_COUNT,             \
	                                           .materialStripCount = SLIP_TRACK_BACKGROUND_MATERIAL_STRIP_COUNT,       \
	                                           .groundMaterial = args->groundMaterial,                                 \
	                                           .callDraw3DBackgroundDispatch = true};                                  \
	out.callDraw3DBackgroundDispatch = true;                                                                           \
	if (!SlipDraw3D_BackgroundDispatchSetupExecute(                                                                    \
	        args->maths, args->pool, args->materialStripTable, args->materialStripTableBytes, args->fixedStripTable,   \
	        args->fixedStripTableBytes, args->materialTable, args->materialTableBytes, args->viewMatrix,               \
	        out.frame.skyHeight, out.frame.backgroundColour, out.frame.groundMaterial, out.frame.materialStripCount,   \
	        out.frame.fixedStripCount, out.frame.skyMaterial, (uint16_t)out.frame.stripCurvature,                      \
	        args->projectionScale, args->cachedProjectionScale, args->projectionRevision, args->cachedFixedStripCount, \
	        args->cachedMaterialEndValue, args->cachedStripCurvature, args->viewportX, args->viewportY,                \
	        args->detailScale, args->stateRecord, args->fadeStart, args->fadeEnd, args->fadeRange, args->ambientLight, \
	        args->fadeColour, args->limitEnabled, args->limitStart, args->limitEnd, args->renderFlags,                 \
	        args->materialFlagsWithPreservedHighWord, args->minX, args->maxX, args->minY, args->maxY,                  \
	        args->depthClipCarry, args->screenClipCarry, args->returnVisits, args->returnVisitCapacity,                \
	        args->pointRingVisits, args->pointRingVisitCapacity, args->stripVisits, args->stripVisitCapacity,          \
	        args->fixedStripVisits, args->fixedStripVisitCapacity, args->materialStripVisits,                          \
	        args->materialStripVisitCapacity, &out.dispatch)) {                                                        \
		return false;                                                                                                  \
	}

bool SlipRaceTrack_DrawEgyptBackground(const SlipRaceTrackFrameCallbackExecuteArgs *args,
                                       SlipRaceTrackFrameCallbackExecute *result) {
	SLIP_RACE_TRACK_BACKGROUND_BODY(args->cameraHeight - SLIP_TRACK_BACKGROUND_DESERT_HEIGHT_OFFSET,
	                                SLIP_TRACK_BACKGROUND_DEFAULT_COLOUR, SLIP_TRACK_BACKGROUND_DEFAULT_CURVATURE);
	out.frame.savedBackgroundColour = out.frame.backgroundColour;
	out.savedBackgroundColour = out.frame.savedBackgroundColour;
	out.frame.callConfigCloudsEnabled = true;
	out.frame.cloudSetting = args->cloudSetting;
	out.frame.callCloudHook = args->cloudSetting != 0;
	out.callConfigCloudsEnabled = true;
	out.callCloudHook = out.frame.callCloudHook;
	if (out.callCloudHook && args->cloudHook != NULL) {
		args->cloudHook(args->cloudHookUserData);
	}
	out.frame.ret = true;
	out.ret = true;
	*result = out;
	return true;
}

bool SlipRaceTrack_DrawArizonaNorwayBackground(const SlipRaceTrackFrameCallbackExecuteArgs *args,
                                               SlipRaceTrackFrameCallbackExecute *result) {
	SLIP_RACE_TRACK_BACKGROUND_BODY(args->cameraHeight - SLIP_TRACK_BACKGROUND_DESERT_HEIGHT_OFFSET,
	                                SLIP_TRACK_BACKGROUND_DEFAULT_COLOUR, SLIP_TRACK_BACKGROUND_DEFAULT_CURVATURE);
	out.frame.ret = true;
	out.ret = true;
	*result = out;
	return true;
}

bool SlipRaceTrack_DrawChicagoBackground(const SlipRaceTrackFrameCallbackExecuteArgs *args,
                                         SlipRaceTrackFrameCallbackExecute *result) {
	SLIP_RACE_TRACK_BACKGROUND_BODY(args->cameraHeight - SLIP_TRACK_BACKGROUND_DEFAULT_HEIGHT_OFFSET,
	                                SLIP_TRACK_BACKGROUND_CHICAGO_COLOUR, SLIP_TRACK_BACKGROUND_CHICAGO_CURVATURE);
	out.frame.savedBackgroundColour = out.frame.backgroundColour;
	out.savedBackgroundColour = out.frame.savedBackgroundColour;
	out.frame.callConfigCloudsEnabled = true;
	out.frame.cloudSetting = args->cloudSetting;
	out.frame.callCloudHook = args->cloudSetting != 0;
	out.callConfigCloudsEnabled = true;
	out.callCloudHook = out.frame.callCloudHook;
	if (out.callCloudHook && args->cloudHook != NULL) {
		args->cloudHook(args->cloudHookUserData);
	}
	out.frame.ret = true;
	out.ret = true;
	*result = out;
	return true;
}

bool SlipRaceTrack_DrawHawaiiBackground(const SlipRaceTrackFrameCallbackExecuteArgs *args,
                                        SlipRaceTrackFrameCallbackExecute *result) {
	SLIP_RACE_TRACK_BACKGROUND_BODY(args->cameraHeight - SLIP_TRACK_BACKGROUND_DEFAULT_HEIGHT_OFFSET,
	                                SLIP_TRACK_BACKGROUND_HAWAII_FRANCE_COLOUR,
	                                SLIP_TRACK_BACKGROUND_COASTAL_CURVATURE);
	out.frame.savedBackgroundColour = out.frame.backgroundColour;
	out.savedBackgroundColour = out.frame.savedBackgroundColour;
	out.frame.callConfigCloudsEnabled = true;
	out.frame.cloudSetting = args->cloudSetting;
	out.frame.callCloudHook = args->cloudSetting != 0;
	out.callConfigCloudsEnabled = true;
	out.callCloudHook = out.frame.callCloudHook;
	if (out.callCloudHook && args->cloudHook != NULL) {
		args->cloudHook(args->cloudHookUserData);
	}
	out.frame.ret = true;
	out.ret = true;
	*result = out;
	return true;
}

bool SlipRaceTrack_DrawTokyoBackground(const SlipRaceTrackFrameCallbackExecuteArgs *args,
                                       SlipRaceTrackFrameCallbackExecute *result) {
	SLIP_RACE_TRACK_BACKGROUND_BODY(args->cameraHeight - SLIP_TRACK_BACKGROUND_DEFAULT_HEIGHT_OFFSET,
	                                SLIP_TRACK_BACKGROUND_DEFAULT_COLOUR, SLIP_TRACK_BACKGROUND_DEFAULT_CURVATURE);
	out.frame.savedBackgroundColour = out.frame.backgroundColour;
	out.savedBackgroundColour = out.frame.savedBackgroundColour;
	out.frame.callConfigCloudsEnabled = true;
	out.frame.cloudSetting = args->cloudSetting;
	out.frame.callCloudHook = args->cloudSetting != 0;
	out.callConfigCloudsEnabled = true;
	out.callCloudHook = out.frame.callCloudHook;
	if (out.callCloudHook && args->cloudHook != NULL) {
		args->cloudHook(args->cloudHookUserData);
	}
	out.frame.ret = true;
	out.ret = true;
	*result = out;
	return true;
}

bool SlipRaceTrack_DrawLondonBackground(const SlipRaceTrackFrameCallbackExecuteArgs *args,
                                        SlipRaceTrackFrameCallbackExecute *result) {
	SLIP_RACE_TRACK_BACKGROUND_BODY(args->cameraHeight + SLIP_TRACK_BACKGROUND_LONDON_HEIGHT_OFFSET,
	                                SLIP_TRACK_BACKGROUND_DEFAULT_COLOUR, SLIP_TRACK_BACKGROUND_LONDON_CURVATURE);
	out.callReadDetailLevel = true;
	out.detailLevel = args->detailLevel;
	if (args->detailLevel == SLIP_TRACK_BACKGROUND_LONDON_CLOUD_DETAIL) {
		out.frame.callConfigCloudsEnabled = true;
		out.frame.cloudSetting = args->cloudSetting;
		out.frame.callCloudHook = args->cloudSetting != 0;
		out.callConfigCloudsEnabled = true;
		out.callCloudHook = out.frame.callCloudHook;
		if (out.callCloudHook && args->cloudHook != NULL) {
			args->cloudHook(args->cloudHookUserData);
		}
	}
	out.frame.ret = true;
	out.ret = true;
	*result = out;
	return true;
}

bool SlipRaceTrack_DrawFranceBackground(const SlipRaceTrackFrameCallbackExecuteArgs *args,
                                        SlipRaceTrackFrameCallbackExecute *result) {
	SLIP_RACE_TRACK_BACKGROUND_BODY(args->cameraHeight - SLIP_TRACK_BACKGROUND_DEFAULT_HEIGHT_OFFSET,
	                                SLIP_TRACK_BACKGROUND_HAWAII_FRANCE_COLOUR,
	                                SLIP_TRACK_BACKGROUND_COASTAL_CURVATURE);
	out.frame.savedBackgroundColour = out.frame.backgroundColour;
	out.savedBackgroundColour = out.frame.savedBackgroundColour;
	out.frame.callConfigCloudsEnabled = true;
	out.frame.cloudSetting = args->cloudSetting;
	out.frame.callCloudHook = args->cloudSetting != 0;
	out.callConfigCloudsEnabled = true;
	out.callCloudHook = out.frame.callCloudHook;
	if (out.callCloudHook && args->cloudHook != NULL) {
		args->cloudHook(args->cloudHookUserData);
	}
	out.frame.ret = true;
	out.ret = true;
	*result = out;
	return true;
}

bool SlipRaceTrack_DrawNewYorkBackground(const SlipRaceTrackFrameCallbackExecuteArgs *args,
                                         SlipRaceTrackFrameCallbackExecute *result) {
	SLIP_RACE_TRACK_BACKGROUND_BODY(args->cameraHeight, SLIP_TRACK_BACKGROUND_NEW_YORK_COLOUR,
	                                SLIP_TRACK_BACKGROUND_COASTAL_CURVATURE);
	out.frame.savedBackgroundColour = out.frame.backgroundColour;
	out.savedBackgroundColour = out.frame.savedBackgroundColour;
	out.frame.callConfigCloudsEnabled = true;
	out.frame.cloudSetting = args->cloudSetting;
	out.frame.callCloudHook = args->cloudSetting != 0;
	out.callConfigCloudsEnabled = true;
	out.callCloudHook = out.frame.callCloudHook;
	if (out.callCloudHook && args->cloudHook != NULL) {
		args->cloudHook(args->cloudHookUserData);
	}
	out.frame.ret = true;
	out.ret = true;
	*result = out;
	return true;
}

#undef SLIP_RACE_TRACK_BACKGROUND_BODY

bool SlipRaceTrack_DrawAmazonBackground(const SlipRaceTrackFrameCallbackExecuteArgs *args,
                                        SlipRaceTrackFrameCallbackExecute *result) {
	SlipDraw3DClipAndCenter clip;

	if (args == NULL || result == NULL || !SlipDraw3D_LoadClipAndCenter(args->projectState, &clip)) {
		return false;
	}
	*result = (SlipRaceTrackFrameCallbackExecute){.callLoadClipAndCenter = true, .callFillClipRect = true, .ret = true};
	Raster_FillRectClipped(SLIP_TRACK_BACKGROUND_AMAZON_COLOUR, (int16_t)clip.clipMinX, (int16_t)clip.clipMinY,
	                       (int16_t)clip.clipMaxX, (int16_t)clip.clipMaxY);
	return true;
}

bool SlipRaceTrack_MaterialGlobals(const uint8_t *materialTable, size_t materialTableBytes, uint16_t materialGlobal,
                                   SlipRaceTrackMaterialGlobals *result) {
	static const uint8_t skyName[] = {'S', 'k', 'y', 0};
	static const uint8_t groundName[] = {'G', 'r', 'o', 'u', 'n', 'd', 0};
	SlipDraw3DMaterialNumber skyLookup;
	SlipDraw3DMaterialNumber groundLookup;

	if (result == 0) {
		return false;
	}

	memset(result, 0, sizeof(*result));
	result->skyNameAddress = SLIP_TRACK_SKY_NAME_DOS_TOKEN;
	result->groundNameAddress = SLIP_TRACK_GROUND_NAME_DOS_TOKEN;
	result->callSkyLookup = true;
	if (!SlipDraw3D_GetMaterialNumber(materialTable, materialTableBytes, materialGlobal, skyName, sizeof(skyName),
	                                  &skyLookup)) {
		return false;
	}
	result->skyLookup = skyLookup;
	if (skyLookup.jumpNoMaterials) {
		return true;
	}
	result->skyLookupCarry = skyLookup.carryOut;
	if (!skyLookup.carryOut) {
		result->skyMaterial = skyLookup.materialIndex;
		result->storeSky = true;
	}

	result->callGroundLookup = true;
	if (!SlipDraw3D_GetMaterialNumber(materialTable, materialTableBytes, materialGlobal, groundName, sizeof(groundName),
	                                  &groundLookup)) {
		return false;
	}
	result->groundLookup = groundLookup;
	if (groundLookup.jumpNoMaterials) {
		return true;
	}
	result->groundLookupCarry = groundLookup.carryOut;
	if (!groundLookup.carryOut) {
		result->groundMaterial = groundLookup.materialIndex;
		result->storeGround = true;
	}
	result->ret = true;
	return true;
}

bool SlipTrackWorld_CameraSetup(uint32_t cameraPositionX, uint32_t cameraPositionY, uint32_t cameraPositionZ,
                                uint32_t viewOriginX, uint32_t viewOriginY, uint32_t viewOriginZ,
                                SlipTrackWorldCameraSetup *result) {
	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldCameraSetup){.cameraObjectOffset = 0,
	                                      .callObjectPosition = true,
	                                      .cameraPositionX = cameraPositionX,
	                                      .cameraPositionY = cameraPositionY,
	                                      .cameraPositionZ = cameraPositionZ,
	                                      .negatedPositionX = (uint32_t)(0u - cameraPositionX),
	                                      .negatedPositionY = (uint32_t)(0u - cameraPositionY),
	                                      .negatedPositionZ = (uint32_t)(0u - cameraPositionZ),
	                                      .callObjectMatrix = true,
	                                      .callTransformPosition = true,
	                                      .viewOriginX = viewOriginX,
	                                      .viewOriginY = viewOriginY,
	                                      .viewOriginZ = viewOriginZ,
	                                      .callTrackWorldMatrixInstall = true,
	                                      .ret = true};
	return true;
}

bool SlipTrackWorld_CameraSetupExecute(const SlipObject *objectTableBase, size_t objectTableSize,
                                       SlipView3DMatrix *objectTransformMatrix, SlipView3DMatrix *viewMatrix,
                                       SlipTrackWorldCameraSetupExecute *result) {
	SlipObjectPosition objectPosition;
	SlipObjectMatrixCopy objectMatrixCopy;
	SlipTrackWorldMatrixInstall matrixInstall;
	SlipView3DVec32 origin;
	SlipView3DVec32 negatedPosition;

	if (result == 0 || objectTransformMatrix == 0 || viewMatrix == 0) {
		return false;
	}
	if (!SlipObject_Position(objectTableBase, objectTableSize, 0, &objectPosition)) {
		return false;
	}
	negatedPosition = (SlipView3DVec32){(int32_t)(uint32_t)(0u - objectPosition.positionX),
	                                    (int32_t)(uint32_t)(0u - objectPosition.positionY),
	                                    (int32_t)(uint32_t)(0u - objectPosition.positionZ)};
	if (!SlipObject_MatrixCopy(objectTableBase, objectTableSize, 0, objectTransformMatrix, &objectMatrixCopy)) {
		return false;
	}
	origin = SlipView3D_TransformPositionByRows(objectTransformMatrix, negatedPosition);
	if (!SlipTrackWorld_MatrixInstall(viewMatrix, objectTransformMatrix, &matrixInstall)) {
		return false;
	}
	*result = (SlipTrackWorldCameraSetupExecute){.cameraObjectOffset = 0,
	                                             .objectPosition = objectPosition,
	                                             .cameraPositionX = objectPosition.positionX,
	                                             .cameraPositionY = objectPosition.positionY,
	                                             .cameraPositionZ = objectPosition.positionZ,
	                                             .negatedPositionX = (uint32_t)negatedPosition.x,
	                                             .negatedPositionY = (uint32_t)negatedPosition.y,
	                                             .negatedPositionZ = (uint32_t)negatedPosition.z,
	                                             .objectMatrixCopy = objectMatrixCopy,
	                                             .origin = origin,
	                                             .matrixInstall = matrixInstall,
	                                             .ret = true};
	return true;
}

bool SlipTrackWorld_MatrixInstall(SlipView3DMatrix *viewMatrix, const SlipView3DMatrix *sourceMatrix,
                                  SlipTrackWorldMatrixInstall *result) {
	if (viewMatrix == 0 || sourceMatrix == 0 || result == 0) {
		return false;
	}

	*viewMatrix = *sourceMatrix;
	SlipView3D_TransposeMatrix(viewMatrix);
	*result = (SlipTrackWorldMatrixInstall){.sourceMatrix = sourceMatrix,
	                                        .viewMatrix = viewMatrix,
	                                        .matrixWordCount = 9u,
	                                        .copiedMatrixWords = true,
	                                        .callView3DTransposeMatrix = true,
	                                        .ret = true};
	return true;
}

bool SlipTrackWorld_StateReset(uint32_t recordIndex, SlipTrackWorldStateReset *result) {
	uint32_t stateIndex;
	uint32_t loopCount;
	size_t i;

	if (result == 0) {
		return false;
	}
	stateIndex = SlipDraw3D_CurrentStateRecordIndex(recordIndex);
	loopCount = SLIP_TRACK_STATE_RESET_COUNT;
	*result = (SlipTrackWorldStateReset){true, stateIndex, loopCount, {{0}}, stateIndex, true, true};
	for (i = 0; i < SLIP_TRACK_STATE_RESET_COUNT; ++i) {
		SlipTrackWorldStateResetVisit *const visit = &result->visits[i];

		*visit = (SlipTrackWorldStateResetVisit){
		    loopCount, stateIndex,      true,          SLIP_TRACK_WORLD_CAMERA_MATRIX_TOKEN, 0, 0, 0, UINT32_MAX,
		    true,      stateIndex + 1u, loopCount - 1u};
		++stateIndex;
		--loopCount;
	}
	return true;
}

bool SlipTrackWorld_FillAxisRamp(uint8_t *ramp, size_t rampBytes, uint16_t directionX, uint16_t directionY,
                                 uint16_t directionZ, uint16_t lastTripletIndex, SlipTrackWorldAxisRamp *result) {
	uint16_t loopCount;
	size_t tripletCount;
	uint32_t stepX;
	uint32_t stepY;
	uint32_t stepZ;
	uint32_t rampX;
	uint32_t rampY;
	uint32_t rampZ;
	size_t i;

	if (ramp == 0 || result == 0) {
		return false;
	}
	loopCount = (uint16_t)(lastTripletIndex + 1u);
	tripletCount = loopCount == 0 ? SLIP_TRACK_AXIS_WRAPPED_LOOP_COUNT : (size_t)loopCount;
	if (tripletCount > (SIZE_MAX / SLIP_TRACK_WORLD_AXIS_RAMP_POINT_BYTES) ||
	    rampBytes < tripletCount * SLIP_TRACK_WORLD_AXIS_RAMP_POINT_BYTES) {
		return false;
	}
	stepX = ((uint32_t)(int32_t)(int16_t)directionX) << SLIP_TRC_POINT_COORDINATE_SHIFT;
	stepY = ((uint32_t)(int32_t)(int16_t)directionY) << SLIP_TRC_POINT_COORDINATE_SHIFT;
	stepZ = ((uint32_t)(int32_t)(int16_t)directionZ) << SLIP_TRC_POINT_COORDINATE_SHIFT;
	rampX = 0;
	rampY = 0;
	rampZ = 0;
	for (i = 0; i < tripletCount; ++i) {
		SlipTrackWorld_WriteLE32(ramp + i * SLIP_TRACK_WORLD_AXIS_RAMP_POINT_BYTES, rampX);
		SlipTrackWorld_WriteLE32(ramp + i * SLIP_TRACK_WORLD_AXIS_RAMP_POINT_BYTES + offsetof(SlipView3DVec32, y),
		                         rampY);
		SlipTrackWorld_WriteLE32(ramp + i * SLIP_TRACK_WORLD_AXIS_RAMP_POINT_BYTES + offsetof(SlipView3DVec32, z),
		                         rampZ);
		rampX += stepX;
		rampY += stepY;
		rampZ += stepZ;
	}
	*result = (SlipTrackWorldAxisRamp){.directionX = directionX,
	                                   .directionY = directionY,
	                                   .directionZ = directionZ,
	                                   .lastTripletIndex = lastTripletIndex,
	                                   .loopCount = loopCount,
	                                   .stepX = stepX,
	                                   .stepY = stepY,
	                                   .stepZ = stepZ,
	                                   .tripletsStored = tripletCount,
	                                   .ret = true};
	return true;
}

bool SlipTrackWorld_BuildAxisRamps(const uint8_t *viewMatrix, size_t matrixBytes, uint8_t *rampX, size_t rampXBytes,
                                   uint8_t *rampY, size_t rampYBytes, uint8_t *rampZ, size_t rampZBytes,
                                   SlipTrackWorldBuildAxisRamps *result) {
	SlipTrackWorldAxisRamp first;
	SlipTrackWorldAxisRamp second;
	SlipTrackWorldAxisRamp third;

	if (viewMatrix == 0 || rampX == 0 || rampY == 0 || rampZ == 0 || result == 0 ||
	    matrixBytes < sizeof(SlipView3DMatrix)) {
		return false;
	}
	if (!SlipTrackWorld_FillAxisRamp(rampX, rampXBytes, SlipBytes_ReadLE16(viewMatrix),
	                                 SlipBytes_ReadLE16(viewMatrix + offsetof(SlipView3DMatrix, m[1])),
	                                 SlipBytes_ReadLE16(viewMatrix + offsetof(SlipView3DMatrix, m[2])),
	                                 SLIP_TRACK_WORLD_AXIS_RAMP_X_LAST_INDEX, &first)) {
		return false;
	}
	if (!SlipTrackWorld_FillAxisRamp(rampY, rampYBytes,
	                                 SlipBytes_ReadLE16(viewMatrix + offsetof(SlipView3DMatrix, m[3])),
	                                 SlipBytes_ReadLE16(viewMatrix + offsetof(SlipView3DMatrix, m[4])),
	                                 SlipBytes_ReadLE16(viewMatrix + offsetof(SlipView3DMatrix, m[5])),
	                                 SLIP_TRACK_WORLD_AXIS_RAMP_Y_LAST_INDEX, &second)) {
		return false;
	}
	if (!SlipTrackWorld_FillAxisRamp(rampZ, rampZBytes,
	                                 SlipBytes_ReadLE16(viewMatrix + offsetof(SlipView3DMatrix, m[6])),
	                                 SlipBytes_ReadLE16(viewMatrix + offsetof(SlipView3DMatrix, m[7])),
	                                 SlipBytes_ReadLE16(viewMatrix + offsetof(SlipView3DMatrix, m[8])),
	                                 SLIP_TRACK_WORLD_AXIS_RAMP_Z_LAST_INDEX, &third)) {
		return false;
	}
	*result = (SlipTrackWorldBuildAxisRamps){.viewMatrix = viewMatrix,
	                                         .rampX = rampX,
	                                         .rampY = rampY,
	                                         .rampZ = rampZ,
	                                         .callBuildXRamp = true,
	                                         .xRamp = first,
	                                         .callBuildYRamp = true,
	                                         .yRamp = second,
	                                         .callBuildZRamp = true,
	                                         .zRamp = third,
	                                         .ret = true};
	return true;
}

bool SlipTrackWorld_AxisTestWord(uint32_t mode, uint32_t positionX, uint32_t positionY, uint32_t positionZ,
                                 uint16_t normalX, uint16_t normalY, uint16_t normalZ,
                                 SlipTrackWorldAxisTestWord *result) {
	SlipTrackWorldAxisPlaneClassify classify;

	if (result == 0) {
		return false;
	}
	if (!SlipTrackWorld_ClassifyAxisPlane(mode, positionX, positionY, positionZ, normalX, normalY, normalZ,
	                                      &classify)) {
		return false;
	}
	*result = (SlipTrackWorldAxisTestWord){.callTrackWorldClassifyAxisPlane = true,
	                                       .classify = classify,
	                                       .trackWorldClassifyAxisPlaneCarry = classify.carry,
	                                       .classificationMask = classify.carry ? 0 : UINT16_MAX,
	                                       .ret = true};
	return true;
}

bool SlipTrackWorld_BuildAxisTests(uint32_t mode, const uint8_t *viewMatrix, size_t matrixBytes, const uint8_t *rampX,
                                   size_t rampXBytes, const uint8_t *rampY, size_t rampYBytes, const uint8_t *rampZ,
                                   size_t rampZBytes, uint8_t *tableFirst, size_t tableFirstBytes, uint8_t *tableSecond,
                                   size_t tableSecondBytes, uint8_t *tableThird, size_t tableThirdBytes, uint32_t addX,
                                   uint32_t addY, uint32_t addZ, SlipTrackWorldAxisTestVisit *visits,
                                   size_t visitCapacity, SlipTrackWorldBuildAxisTests *result) {
	const uint8_t *rampTriplet;
	uint8_t *tableEntry;
	size_t visitIndex;
	size_t i;

	if (viewMatrix == 0 || rampX == 0 || rampY == 0 || rampZ == 0 || tableFirst == 0 || tableSecond == 0 ||
	    tableThird == 0 || visits == 0 || result == 0 || matrixBytes < sizeof(SlipView3DMatrix) ||
	    rampXBytes < (SLIP_TRACK_WORLD_AXIS_TEST_X_COUNT + 1u) * SLIP_TRACK_WORLD_AXIS_RAMP_POINT_BYTES ||
	    rampYBytes < (SLIP_TRACK_WORLD_AXIS_TEST_Y_COUNT + 1u) * SLIP_TRACK_WORLD_AXIS_RAMP_POINT_BYTES ||
	    rampZBytes < (SLIP_TRACK_WORLD_AXIS_TEST_Z_COUNT + 1u) * SLIP_TRACK_WORLD_AXIS_RAMP_POINT_BYTES ||
	    tableFirstBytes < SLIP_TRACK_WORLD_AXIS_TEST_X_COUNT * sizeof(uint16_t) ||
	    tableSecondBytes < SLIP_TRACK_WORLD_AXIS_TEST_Y_COUNT * sizeof(uint16_t) ||
	    tableThirdBytes < SLIP_TRACK_WORLD_AXIS_TEST_Z_COUNT * sizeof(uint16_t) ||
	    visitCapacity < SLIP_TRACK_WORLD_AXIS_TEST_COUNT) {
		return false;
	}
	visitIndex = 0;
	for (i = 0; i < SLIP_TRACK_WORLD_AXIS_TEST_X_COUNT; ++i) {
		SlipTrackWorldAxisTestVisit *const visit = &visits[visitIndex];

		rampTriplet = rampX + SLIP_TRACK_WORLD_AXIS_RAMP_POINT_BYTES + i * SLIP_TRACK_WORLD_AXIS_RAMP_POINT_BYTES;
		tableEntry = tableFirst + i * 2u;
		*visit =
		    (SlipTrackWorldAxisTestVisit){.loopCountBefore = (uint32_t)(SLIP_TRACK_WORLD_AXIS_TEST_X_COUNT - i),
		                                  .rampTriplet = rampTriplet,
		                                  .tableEntry = tableEntry,
		                                  .rampX = SlipBytes_ReadLE32(rampTriplet),
		                                  .rampY = SlipBytes_ReadLE32(rampTriplet + offsetof(SlipView3DVec32, y)),
		                                  .rampZ = SlipBytes_ReadLE32(rampTriplet + offsetof(SlipView3DVec32, z)),
		                                  .normalX = SlipBytes_ReadLE16(viewMatrix),
		                                  .normalY = SlipBytes_ReadLE16(viewMatrix + offsetof(SlipView3DMatrix, m[1])),
		                                  .normalZ = SlipBytes_ReadLE16(viewMatrix + offsetof(SlipView3DMatrix, m[2])),
		                                  .callTrackWorldAxisTestWord = true};
		visit->positionX = visit->rampX + addX;
		visit->positionY = visit->rampY + addY;
		visit->positionZ = visit->rampZ + addZ;
		if (!SlipTrackWorld_AxisTestWord(mode, visit->positionX, visit->positionY, visit->positionZ, visit->normalX,
		                                 visit->normalY, visit->normalZ, &visit->word)) {
			return false;
		}
		visit->classificationMask = visit->word.classificationMask;
		SlipTrackWorld_WriteLE16(tableEntry, visit->classificationMask);
		++visitIndex;
	}
	for (i = 0; i < SLIP_TRACK_WORLD_AXIS_TEST_Y_COUNT; ++i) {
		SlipTrackWorldAxisTestVisit *const visit = &visits[visitIndex];

		rampTriplet = rampY + SLIP_TRACK_WORLD_AXIS_RAMP_POINT_BYTES + i * SLIP_TRACK_WORLD_AXIS_RAMP_POINT_BYTES;
		tableEntry = tableSecond + i * 2u;
		*visit =
		    (SlipTrackWorldAxisTestVisit){.loopCountBefore = (uint32_t)(SLIP_TRACK_WORLD_AXIS_TEST_Y_COUNT - i),
		                                  .rampTriplet = rampTriplet,
		                                  .tableEntry = tableEntry,
		                                  .rampX = SlipBytes_ReadLE32(rampTriplet),
		                                  .rampY = SlipBytes_ReadLE32(rampTriplet + offsetof(SlipView3DVec32, y)),
		                                  .rampZ = SlipBytes_ReadLE32(rampTriplet + offsetof(SlipView3DVec32, z)),
		                                  .normalX = SlipBytes_ReadLE16(viewMatrix + offsetof(SlipView3DMatrix, m[3])),
		                                  .normalY = SlipBytes_ReadLE16(viewMatrix + offsetof(SlipView3DMatrix, m[4])),
		                                  .normalZ = SlipBytes_ReadLE16(viewMatrix + offsetof(SlipView3DMatrix, m[5])),
		                                  .callTrackWorldAxisTestWord = true};
		visit->positionX = visit->rampX + addX;
		visit->positionY = visit->rampY + addY;
		visit->positionZ = visit->rampZ + addZ;
		if (!SlipTrackWorld_AxisTestWord(mode, visit->positionX, visit->positionY, visit->positionZ, visit->normalX,
		                                 visit->normalY, visit->normalZ, &visit->word)) {
			return false;
		}
		visit->classificationMask = visit->word.classificationMask;
		SlipTrackWorld_WriteLE16(tableEntry, visit->classificationMask);
		++visitIndex;
	}
	for (i = 0; i < SLIP_TRACK_WORLD_AXIS_TEST_Z_COUNT; ++i) {
		SlipTrackWorldAxisTestVisit *const visit = &visits[visitIndex];

		rampTriplet = rampZ + SLIP_TRACK_WORLD_AXIS_RAMP_POINT_BYTES + i * SLIP_TRACK_WORLD_AXIS_RAMP_POINT_BYTES;
		tableEntry = tableThird + i * 2u;
		*visit =
		    (SlipTrackWorldAxisTestVisit){.loopCountBefore = (uint32_t)(SLIP_TRACK_WORLD_AXIS_TEST_Z_COUNT - i),
		                                  .rampTriplet = rampTriplet,
		                                  .tableEntry = tableEntry,
		                                  .rampX = SlipBytes_ReadLE32(rampTriplet),
		                                  .rampY = SlipBytes_ReadLE32(rampTriplet + offsetof(SlipView3DVec32, y)),
		                                  .rampZ = SlipBytes_ReadLE32(rampTriplet + offsetof(SlipView3DVec32, z)),
		                                  .normalX = SlipBytes_ReadLE16(viewMatrix + offsetof(SlipView3DMatrix, m[6])),
		                                  .normalY = SlipBytes_ReadLE16(viewMatrix + offsetof(SlipView3DMatrix, m[7])),
		                                  .normalZ = SlipBytes_ReadLE16(viewMatrix + offsetof(SlipView3DMatrix, m[8])),
		                                  .callTrackWorldAxisTestWord = true};
		visit->positionX = visit->rampX + addX;
		visit->positionY = visit->rampY + addY;
		visit->positionZ = visit->rampZ + addZ;
		if (!SlipTrackWorld_AxisTestWord(mode, visit->positionX, visit->positionY, visit->positionZ, visit->normalX,
		                                 visit->normalY, visit->normalZ, &visit->word)) {
			return false;
		}
		visit->classificationMask = visit->word.classificationMask;
		SlipTrackWorld_WriteLE16(tableEntry, visit->classificationMask);
		++visitIndex;
	}
	*result = (SlipTrackWorldBuildAxisTests){.mode = mode,
	                                         .viewMatrix = viewMatrix,
	                                         .rampX = rampX,
	                                         .rampY = rampY,
	                                         .rampZ = rampZ,
	                                         .xTestTable = tableFirst,
	                                         .yTestTable = tableSecond,
	                                         .zTestTable = tableThird,
	                                         .addX = addX,
	                                         .addY = addY,
	                                         .addZ = addZ,
	                                         .visitsStored = visitIndex,
	                                         .ret = true};
	return true;
}

bool SlipTrackWorld_ClearGlobals(SlipTrackWorldClearGlobals *result) {
	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldClearGlobals){0, 0, 0, 0, true};
	return true;
}

bool SlipTrackWorld_InitSlotDrawRing(uint8_t *slotDrawBase, size_t slotDrawBytes, uint32_t slotDrawBaseAddress,
                                     uint16_t slotDrawCount, SlipTrackWorldSlotDrawRingVisit *visits,
                                     uint16_t visitCapacity, SlipTrackWorldSlotDrawRing *result) {
	uint32_t currentOffset;
	uint16_t i;

	if (slotDrawBase == 0 || result == 0 || slotDrawCount == 0) {
		return false;
	}
	*result = (SlipTrackWorldSlotDrawRing){slotDrawCount, slotDrawBaseAddress, 0, 0, 0, 0, false, true};
	currentOffset = 0;
	for (i = 0; i < slotDrawCount; ++i) {
		const uint32_t nextOffset = currentOffset + sizeof(SlipTrackDrawRecord);

		if ((size_t)currentOffset + offsetof(SlipTrackDrawRecord, nextAddress) + sizeof(uint32_t) > slotDrawBytes ||
		    (size_t)nextOffset + offsetof(SlipTrackDrawRecord, previousAddress) + sizeof(uint32_t) > slotDrawBytes) {
			return false;
		}
		if (visits != 0 && i < visitCapacity) {
			visits[i] = (SlipTrackWorldSlotDrawRingVisit){
			    i, currentOffset, nextOffset, slotDrawBaseAddress + nextOffset, slotDrawBaseAddress + currentOffset};
		} else if (visits != 0) {
			result->hitVisitCapacity = true;
		}
		((SlipTrackDrawRecord *)(void *)(slotDrawBase + currentOffset))->nextAddress = slotDrawBaseAddress + nextOffset;
		((SlipTrackDrawRecord *)(void *)(slotDrawBase + nextOffset))->previousAddress =
		    slotDrawBaseAddress + currentOffset;
		currentOffset = nextOffset;
	}
	((SlipTrackDrawRecord *)(void *)(slotDrawBase + currentOffset))->nextAddress = slotDrawBaseAddress;
	((SlipTrackDrawRecord *)(void *)(slotDrawBase))->previousAddress = slotDrawBaseAddress + currentOffset;
	result->linkCount = slotDrawCount;
	result->finalOffset = currentOffset;
	result->finalNextAddress = slotDrawBaseAddress;
	result->basePrevAddress = slotDrawBaseAddress + currentOffset;
	return true;
}

bool SlipTrackWorld_AllocSlotDrawRecord(uint8_t *slotDrawBase, size_t slotDrawBytes, uint32_t slotDrawBaseAddress,
                                        uint32_t freeListAddress, uint8_t *slotListEntry, size_t slotListEntryBytes,
                                        uint32_t slotListEntryAddress, SlipTrackWorldSlotDrawAlloc *result) {
	uint32_t freeListOffset;
	uint32_t allocatedAddress;
	uint32_t allocatedOffset;
	uint32_t nextFreeAddress;
	uint32_t nextFreeOffset;
	uint16_t slotDrawOffsetWord;

	if (slotDrawBase == 0 || slotListEntry == 0 || result == 0 || slotListEntryBytes < SLIP_TRD_SECTION_DRAW_LIST_END) {
		return false;
	}
	if (!SlipTrackWorld_DosAddressToOffset(freeListAddress, slotDrawBaseAddress, slotDrawBytes, &freeListOffset) ||
	    freeListOffset + SLIP_TRACK_DRAW_LINKS_END > slotDrawBytes) {
		return false;
	}
	allocatedAddress = ((const SlipTrackDrawRecord *)(const void *)(slotDrawBase + freeListOffset))->nextAddress;
	*result = (SlipTrackWorldSlotDrawAlloc){.freeListAddress = freeListAddress,
	                                        .freeListOffset = freeListOffset,
	                                        .allocatedAddress = allocatedAddress,
	                                        .emptyFreeList = allocatedAddress == freeListAddress,
	                                        .slotListEntryAddress = slotListEntryAddress,
	                                        .ret = allocatedAddress != freeListAddress};
	if (allocatedAddress == freeListAddress) {

		SlipRuntime_Fatal("SlotDrawAlloc - out of SlotDraw records");
	}
	if (!SlipTrackWorld_DosAddressToOffset(allocatedAddress, slotDrawBaseAddress, slotDrawBytes, &allocatedOffset) ||
	    allocatedOffset + SLIP_TRACK_DRAW_OWNER_END > slotDrawBytes) {
		return false;
	}
	nextFreeAddress = ((const SlipTrackDrawRecord *)(const void *)(slotDrawBase + allocatedOffset))->nextAddress;
	if (!SlipTrackWorld_DosAddressToOffset(nextFreeAddress, slotDrawBaseAddress, slotDrawBytes, &nextFreeOffset) ||
	    nextFreeOffset + SLIP_TRACK_DRAW_LINKS_END > slotDrawBytes) {
		return false;
	}
	((SlipTrackDrawRecord *)(void *)(slotDrawBase + freeListOffset))->nextAddress = nextFreeAddress;
	((SlipTrackDrawRecord *)(void *)(slotDrawBase + nextFreeOffset))->previousAddress = freeListAddress;

	slotDrawOffsetWord = SlipBytes_ReadLE16(slotListEntry + offsetof(SlipTrackSectionDrawLinks, firstDrawOffset));
	result->allocatedOffset = allocatedOffset;
	result->nextFreeAddress = nextFreeAddress;
	result->nextFreeOffset = nextFreeOffset;
	result->slotDrawOffsetWord = slotDrawOffsetWord;
	if (slotDrawOffsetWord != 0) {
		const uint32_t ringOffset = slotDrawOffsetWord;
		const uint32_t ringAddress = slotDrawBaseAddress + ringOffset;
		uint32_t ringNextAddress;
		uint32_t ringNextOffset;

		if (ringOffset + SLIP_TRACK_DRAW_LINKS_END > slotDrawBytes) {
			return false;
		}
		ringNextAddress = ((const SlipTrackDrawRecord *)(const void *)(slotDrawBase + ringOffset))->nextAddress;
		if (!SlipTrackWorld_DosAddressToOffset(ringNextAddress, slotDrawBaseAddress, slotDrawBytes, &ringNextOffset) ||
		    ringNextOffset + SLIP_TRACK_DRAW_LINKS_END > slotDrawBytes) {
			return false;
		}
		((SlipTrackDrawRecord *)(void *)(slotDrawBase + ringOffset))->nextAddress = allocatedAddress;
		((SlipTrackDrawRecord *)(void *)(slotDrawBase + ringNextOffset))->previousAddress = allocatedAddress;
		((SlipTrackDrawRecord *)(void *)(slotDrawBase + allocatedOffset))->nextAddress = ringNextAddress;
		((SlipTrackDrawRecord *)(void *)(slotDrawBase + allocatedOffset))->previousAddress = ringAddress;
		result->branch = SLIP_TRACK_WORLD_SLOT_DRAW_ALLOC_BRANCH_EXISTING_RING;
		result->existingRingAddress = ringAddress;
		result->existingRingOffset = ringOffset;
		result->existingRingNextAddress = ringNextAddress;
		result->existingRingNextOffset = ringNextOffset;
	} else {
		const uint16_t allocatedOffsetWord = (uint16_t)allocatedOffset;

		((SlipTrackDrawRecord *)(void *)(slotDrawBase + allocatedOffset))->nextAddress = allocatedAddress;
		((SlipTrackDrawRecord *)(void *)(slotDrawBase + allocatedOffset))->previousAddress = allocatedAddress;
		SlipTrackWorld_WriteLE16(slotListEntry + offsetof(SlipTrackSectionDrawLinks, firstDrawOffset),
		                         allocatedOffsetWord);
		result->branch = SLIP_TRACK_WORLD_SLOT_DRAW_ALLOC_BRANCH_NEW_RING;
		result->storedSlotDrawOffset = allocatedOffsetWord;
	}
	((SlipTrackDrawRecord *)(void *)(slotDrawBase + allocatedOffset))->ownerTrackRecordAddress = slotListEntryAddress;
	result->storedOwnerAddress = slotListEntryAddress;
	return true;
}

bool SlipTrackWorld_FreeSlotDrawRecord(uint8_t *slotDrawBase, size_t slotDrawBytes, uint32_t slotDrawBaseAddress,
                                       uint32_t freeListAddress, uint8_t *trdBase, size_t trackDataSize,
                                       uint32_t trdBaseAddress, uint32_t drawRecordAddress,
                                       SlipTrackWorldSlotDrawFree *result) {
	uint32_t drawRecordOffset;
	uint32_t ownerAddress;
	uint32_t ownerOffset;
	uint32_t nextAddress;
	uint32_t nextOffset;
	uint32_t prevAddress;
	uint32_t prevOffset;
	uint32_t ownerSlotNextAddress;
	uint16_t ownerSlotOffsetWord;
	uint32_t freeListOffset;
	uint32_t freeNextAddress;
	uint32_t freeNextOffset;

	if (slotDrawBase == 0 || trdBase == 0 || result == 0) {
		return false;
	}
	if (!SlipTrackWorld_DosAddressToOffset(drawRecordAddress, slotDrawBaseAddress, slotDrawBytes, &drawRecordOffset) ||
	    drawRecordOffset + SLIP_TRACK_DRAW_OWNER_END > slotDrawBytes) {
		return false;
	}
	ownerAddress =
	    ((const SlipTrackDrawRecord *)(const void *)(slotDrawBase + drawRecordOffset))->ownerTrackRecordAddress;
	if (!SlipTrackWorld_DosAddressToOffset(ownerAddress, trdBaseAddress, trackDataSize, &ownerOffset) ||
	    ownerOffset + SLIP_TRD_SECTION_DRAW_LIST_END > trackDataSize) {
		return false;
	}
	nextAddress = ((const SlipTrackDrawRecord *)(const void *)(slotDrawBase + drawRecordOffset))->nextAddress;
	prevAddress = ((const SlipTrackDrawRecord *)(const void *)(slotDrawBase + drawRecordOffset))->previousAddress;
	if (!SlipTrackWorld_DosAddressToOffset(nextAddress, slotDrawBaseAddress, slotDrawBytes, &nextOffset) ||
	    nextOffset + SLIP_TRACK_DRAW_LINKS_END > slotDrawBytes ||
	    !SlipTrackWorld_DosAddressToOffset(prevAddress, slotDrawBaseAddress, slotDrawBytes, &prevOffset) ||
	    prevOffset + SLIP_TRACK_DRAW_LINKS_END > slotDrawBytes) {
		return false;
	}
	((SlipTrackDrawRecord *)(void *)(slotDrawBase + prevOffset))->nextAddress = nextAddress;
	((SlipTrackDrawRecord *)(void *)(slotDrawBase + nextOffset))->previousAddress = prevAddress;
	ownerSlotNextAddress = nextAddress;
	if (nextAddress == drawRecordAddress) {
		ownerSlotNextAddress = slotDrawBaseAddress;
	}
	ownerSlotOffsetWord = (uint16_t)(ownerSlotNextAddress - slotDrawBaseAddress);
	SlipTrackWorld_WriteLE16(trdBase + ownerOffset + offsetof(SlipTrackSectionDrawLinks, firstDrawOffset),
	                         ownerSlotOffsetWord);
	if (!SlipTrackWorld_DosAddressToOffset(freeListAddress, slotDrawBaseAddress, slotDrawBytes, &freeListOffset) ||
	    freeListOffset + SLIP_TRACK_DRAW_LINKS_END > slotDrawBytes) {
		return false;
	}
	freeNextAddress = ((const SlipTrackDrawRecord *)(const void *)(slotDrawBase + freeListOffset))->nextAddress;
	if (!SlipTrackWorld_DosAddressToOffset(freeNextAddress, slotDrawBaseAddress, slotDrawBytes, &freeNextOffset) ||
	    freeNextOffset + SLIP_TRACK_DRAW_LINKS_END > slotDrawBytes) {
		return false;
	}
	((SlipTrackDrawRecord *)(void *)(slotDrawBase + freeListOffset))->nextAddress = drawRecordAddress;
	((SlipTrackDrawRecord *)(void *)(slotDrawBase + freeNextOffset))->previousAddress = drawRecordAddress;
	((SlipTrackDrawRecord *)(void *)(slotDrawBase + drawRecordOffset))->nextAddress = freeNextAddress;
	((SlipTrackDrawRecord *)(void *)(slotDrawBase + drawRecordOffset))->previousAddress = freeListAddress;
	*result = (SlipTrackWorldSlotDrawFree){.drawRecordAddress = drawRecordAddress,
	                                       .drawRecordOffset = drawRecordOffset,
	                                       .ownerAddress = ownerAddress,
	                                       .ownerOffset = ownerOffset,
	                                       .nextAddress = nextAddress,
	                                       .nextOffset = nextOffset,
	                                       .prevAddress = prevAddress,
	                                       .prevOffset = prevOffset,
	                                       .ownerSlotNextAddress = ownerSlotNextAddress,
	                                       .ownerSlotOffsetWord = ownerSlotOffsetWord,
	                                       .freeListAddress = freeListAddress,
	                                       .freeListOffset = freeListOffset,
	                                       .freeNextAddress = freeNextAddress,
	                                       .freeNextOffset = freeNextOffset,
	                                       .ret = true};
	return true;
}

bool SlipTrackWorld_ClearOwnerDrawLinks(uint16_t trackHandle, uint8_t *slotDrawBase, size_t slotDrawBytes,
                                        uint32_t slotDrawBaseAddress, uint32_t freeListAddress, uint8_t *trdBase,
                                        size_t trackDataSize, uint32_t trdBaseAddress, uint8_t *slotListEntry,
                                        size_t slotListEntryBytes, SlipTrackWorldOwnerDrawLinksClear *result) {
	uint32_t firstDrawAddress;
	uint32_t secondDrawAddress;

	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldOwnerDrawLinksClear){.trackHandle = trackHandle, .skipped = trackHandle == 0, .ret = true};
	if (trackHandle == 0) {
		return true;
	}
	if (slotListEntry == 0 || slotListEntryBytes < SLIP_TRACK_SLOT_DRAW_ADDRESSES_END) {
		return false;
	}
	SlipTrackSlotRecord *const slot = (SlipTrackSlotRecord *)(void *)slotListEntry;
	firstDrawAddress = slot->firstDrawAddress;
	result->firstDrawAddress = firstDrawAddress;
	if (firstDrawAddress != 0) {
		if (!SlipTrackWorld_FreeSlotDrawRecord(slotDrawBase, slotDrawBytes, slotDrawBaseAddress, freeListAddress,
		                                       trdBase, trackDataSize, trdBaseAddress, firstDrawAddress,
		                                       &result->firstFree)) {
			return false;
		}
		slot->firstDrawAddress = 0;
		result->callFreeFirstDraw = true;
		result->clearedFirst = true;
	}
	secondDrawAddress = slot->secondDrawAddress;
	result->secondDrawAddress = secondDrawAddress;
	if (secondDrawAddress != 0) {
		if (!SlipTrackWorld_FreeSlotDrawRecord(slotDrawBase, slotDrawBytes, slotDrawBaseAddress, freeListAddress,
		                                       trdBase, trackDataSize, trdBaseAddress, secondDrawAddress,
		                                       &result->secondFree)) {
			return false;
		}
		slot->secondDrawAddress = 0;
		result->callFreeSecondDraw = true;
		result->clearedSecond = true;
	}
	return true;
}

bool SlipTrackWorld_SelectSlotListEntry(uint32_t callerValue, uint32_t slotListBaseAddress,
                                        const SlipObject *objectTableBase, size_t objectTableBytes,
                                        uint16_t objectOffset, SlipTrackWorldSlotListSelect *result) {
	uint16_t slotOffset;

	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldSlotListSelect){.callerValue = callerValue,
	                                         .slotListBaseAddress = slotListBaseAddress,
	                                         .objectOffset = objectOffset,
	                                         .carry = true,
	                                         .restoredCallerValue = callerValue,
	                                         .ret = true};
	if (slotListBaseAddress == 0) {
		result->skippedNoSlotList = true;
		return true;
	}
	if (objectTableBase == 0 || (size_t)objectOffset + SLIP_OBJECT_DOS_TRACK_SLOT_END > objectTableBytes) {
		return false;
	}
	slotOffset = objectTableBase[objectOffset / SLIP_OBJECT_DOS_STRIDE].trackSlotOffset;
	result->callObjectTrackSlot = true;
	result->slotOffset = slotOffset;
	if (slotOffset == 0) {
		result->zeroSlotOffset = true;
		return true;
	}
	result->slotAddress = slotListBaseAddress + (uint32_t)slotOffset;
	result->carry = false;
	return true;
}

const uint8_t *SlipTrackWorld_GetCurrentName(uint32_t currentToken, uint16_t objectOffset, uint8_t *slotListBase,
                                             size_t slotListBytes, uint32_t slotListBaseAddress,
                                             const SlipObject *objectTable, size_t objectTableBytes,
                                             const uint8_t *trdBase, size_t trackDataSize, uint32_t trdBaseAddress,
                                             const uint8_t *componentBase, size_t componentBaseBytes,
                                             uint32_t componentBaseAddress, const uint8_t *table, size_t tableBytes) {
	SlipTrackWorldSlotListSelect select;
	uint8_t *slot;
	const uint8_t *record;
	const uint8_t *component;
	uint32_t slotOffset;
	uint32_t recordOffset;
	uint16_t componentOffset;

	(void)slotListBytes;
	SlipTrackWorld_SelectSlotListEntry(currentToken, slotListBaseAddress, objectTable, objectTableBytes, objectOffset,
	                                   &select);
	if (select.carry)
		SlipRuntime_Fatal("TrackSlotGetName - not a track slot");
	slotOffset = select.slotAddress - slotListBaseAddress;
	slot = slotListBase + slotOffset;
	if (!SlipTrackWorld_UpdateSlotRecord(slot, objectTable, objectTableBytes, trdBase, trackDataSize, trdBaseAddress,
	                                     componentBase, componentBaseBytes, componentBaseAddress, table, tableBytes))
		return NULL;
	recordOffset = SlipBytes_ReadLE32(slot + offsetof(SlipTrackSlotRecord, currentTrackRecordAddress)) - trdBaseAddress;
	if ((size_t)recordOffset > trackDataSize || trackDataSize - recordOffset < SLIP_TRD_SECTION_COMPONENT_END)
		return NULL;
	record = trdBase + recordOffset;
	componentOffset = SlipBytes_ReadLE16(record + SLIP_TRD_SECTION_COMPONENT_OFFSET);
	if ((size_t)componentOffset > componentBaseBytes ||
	    componentBaseBytes - componentOffset < SLIP_TRC_COMPONENT_NAME_PRESENT_END)
		return NULL;
	component = componentBase + componentOffset;
	if (SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_NAME_PRESENT_OFFSET) == 0u)
		return NULL;
	if (componentBaseBytes - componentOffset < SLIP_TRC_COMPONENT_NAME_PREFIX_END)
		return NULL;
	return component + SLIP_TRC_COMPONENT_NAME_OFFSET;
}

void SlipTrackWorld_CurrentSlot(uint32_t currentToken, uint16_t objectOffset, uint8_t *slotListBase,
                                size_t slotListBytes, uint32_t slotListBaseAddress, const SlipObject *objectTable,
                                size_t objectTableBytes, const uint8_t *trdBase, size_t trackDataSize,
                                uint32_t trdBaseAddress, const uint8_t *componentBase, size_t componentBaseBytes,
                                uint32_t componentBaseAddress, const uint8_t *table, size_t tableBytes,
                                uint32_t currentComponentToken, SlipTrackWorldCurrentSlot *result) {
	SlipTrackWorldSlotListSelect select;
	uint8_t *slot;
	uint32_t slotOffset;

	SlipTrackWorld_SelectSlotListEntry(currentToken, slotListBaseAddress, objectTable, objectTableBytes, objectOffset,
	                                   &select);
	if (select.carry) {
		SlipRuntime_Fatal("TrackSlotCheckRefuel - not a track slot");
	}
	slotOffset = select.slotAddress - slotListBaseAddress;
	slot = slotListBase + slotOffset;
	SlipTrackWorld_UpdateSlotRecord(slot, objectTable, objectTableBytes, trdBase, trackDataSize, trdBaseAddress,
	                                componentBase, componentBaseBytes, componentBaseAddress, table, tableBytes);
	result->slotRecordAddress = select.slotAddress;
	result->trackRecordAddress = SlipBytes_ReadLE32(slot + offsetof(SlipTrackSlotRecord, currentTrackRecordAddress));
	result->carryOut = result->trackRecordAddress == currentComponentToken;
}

uint32_t SlipTrackWorld_CurrentComponent(uint32_t currentToken, uint16_t objectOffset, uint8_t *slotListBase,
                                         size_t slotListBytes, uint32_t slotListBaseAddress,
                                         const SlipObject *objectTable, size_t objectTableBytes, const uint8_t *trdBase,
                                         size_t trackDataSize, uint32_t trdBaseAddress, const uint8_t *componentBase,
                                         size_t componentBaseBytes, uint32_t componentBaseAddress, const uint8_t *table,
                                         size_t tableBytes) {
	SlipTrackWorldSlotListSelect select;
	uint32_t slotOffset;
	uint8_t *currentSlot;

	(void)slotListBytes;
	SlipTrackWorld_SelectSlotListEntry(currentToken, slotListBaseAddress, objectTable, objectTableBytes, objectOffset,
	                                   &select);
	if (select.carry) {
		SlipRuntime_Fatal("TrackSlotCompGetCurrent - not a track slot");
	}
	slotOffset = select.slotAddress - slotListBaseAddress;
	currentSlot = slotListBase + slotOffset;
	SlipTrackWorld_UpdateSlotRecord(currentSlot, objectTable, objectTableBytes, trdBase, trackDataSize, trdBaseAddress,
	                                componentBase, componentBaseBytes, componentBaseAddress, table, tableBytes);

	const SlipTrackSlotRecord *const slot = (const SlipTrackSlotRecord *)(const void *)currentSlot;
	return slot->currentTrackRecordAddress - trdBaseAddress;
}

uint16_t SlipTrackWorld_StartComponent(const uint8_t *trdBase) {
	return SlipBytes_ReadLE16(trdBase + SLIP_TRD_START_COMPONENT_OFFSET);
}

uint16_t SlipTrackWorld_PreviousComponent(const uint8_t *trdBase) {
	return SlipBytes_ReadLE16(trdBase + SLIP_TRD_PREVIOUS_COMPONENT_OFFSET);
}

uint32_t SlipTrackWorld_RaceProgress(uint16_t objectOffset, uint8_t *slotListBase, size_t slotListBytes,
                                     uint32_t slotListBaseAddress, const SlipObject *objectTable,
                                     size_t objectTableBytes, const uint8_t *trdBase, size_t trackDataSize,
                                     uint32_t trdBaseAddress, const uint8_t *componentBase, size_t componentBaseBytes,
                                     uint32_t componentBaseAddress, const uint8_t *table, size_t tableBytes) {
	SlipTrackWorldSlotListSelect selected;
	SlipObjectPosition objectPosition;
	SlipDraw3DApproxAbsVectorLength distance;
	uint32_t slotOffset;
	uint32_t trackRecordAddress;
	uint32_t trackRecordOffset;
	uint8_t *currentSlot;
	const uint8_t *waypoint;

	(void)slotListBytes;
	(void)trackDataSize;
	SlipTrackWorld_SelectSlotListEntry(0, slotListBaseAddress, objectTable, objectTableBytes, objectOffset, &selected);
	if (selected.carry) {
		SlipRuntime_Fatal("TrackPosGet - not a track slot");
	}
	slotOffset = selected.slotAddress - slotListBaseAddress;
	currentSlot = slotListBase + slotOffset;
	SlipTrackWorld_UpdateSlotRecord(currentSlot, objectTable, objectTableBytes, trdBase, trackDataSize, trdBaseAddress,
	                                componentBase, componentBaseBytes, componentBaseAddress, table, tableBytes);

	const SlipTrackSlotRecord *const slot = (const SlipTrackSlotRecord *)(const void *)currentSlot;
	trackRecordAddress = slot->currentTrackRecordAddress;
	trackRecordOffset = trackRecordAddress - trdBaseAddress;
	waypoint = trdBase + SlipBytes_ReadLE16(trdBase + trackRecordOffset + SLIP_TRD_SECTION_ROUTE_OFFSET);
	SlipObject_Position(objectTable, objectTableBytes, objectOffset, &objectPosition);
	SlipDraw3D_ApproxAbsVectorLength(
	    objectPosition.positionX - SlipBytes_ReadLE32(waypoint + SLIP_TRD_POSITION_X_OFFSET),
	    objectPosition.positionY - SlipBytes_ReadLE32(waypoint + SLIP_TRD_POSITION_Y_OFFSET),
	    objectPosition.positionZ - SlipBytes_ReadLE32(waypoint + SLIP_TRD_POSITION_Z_OFFSET), &distance);
	return distance.approximateLength + SlipBytes_ReadLE32(waypoint + SLIP_TRD_WAYPOINT_LAP_DISTANCE_OFFSET);
}

void SlipTrackWorld_SetLapDistance(uint8_t *trdBase) {
	uint8_t *waypoint = trdBase + SlipBytes_ReadLE16(trdBase + SLIP_TRD_ROUTE_TABLE_OFFSET);
	uint32_t waypointCount = SlipBytes_ReadLE16(waypoint);
	uint8_t *firstWaypoint;

	waypoint += SLIP_TRD_TABLE_COUNT_BYTES;
	firstWaypoint = waypoint;
	do {
		uint8_t *const targetWaypoint = waypoint;
		uint32_t lapDistance = 0;

		while (waypoint != firstWaypoint) {
			uint32_t previousX = SlipBytes_ReadLE32(waypoint + SLIP_TRD_POSITION_X_OFFSET);
			uint32_t previousY = SlipBytes_ReadLE32(waypoint + SLIP_TRD_POSITION_Y_OFFSET);
			uint32_t previousZ = SlipBytes_ReadLE32(waypoint + SLIP_TRD_POSITION_Z_OFFSET);

			if (SlipBytes_ReadLE16(waypoint + SLIP_TRD_ROUTE_SECOND_LINK_OFFSET) != 0) {
				uint8_t *const savedWaypoint = waypoint;
				uint8_t *joinWaypoint;
				uint32_t branchDistance = 0;
				uint32_t forwardDistance = 0;

				waypoint = trdBase + SlipBytes_ReadLE16(waypoint + SLIP_TRD_ROUTE_SECOND_LINK_OFFSET);
				do {
					branchDistance += SlipView3D_VectorLength(
					    (int32_t)(SlipBytes_ReadLE32(waypoint + SLIP_TRD_POSITION_X_OFFSET) - previousX),
					    (int32_t)(SlipBytes_ReadLE32(waypoint + SLIP_TRD_POSITION_Y_OFFSET) - previousY),
					    (int32_t)(SlipBytes_ReadLE32(waypoint + SLIP_TRD_POSITION_Z_OFFSET) - previousZ));
					if ((SlipBytes_ReadLE16(waypoint + SLIP_TRD_ROUTE_SECOND_LINK_OFFSET) |
					     SlipBytes_ReadLE16(waypoint)) == 0) {
						SlipRuntime_Fatal("TrackSetLapDist - fwd node not linked!");
					}
					waypoint = trdBase + SlipBytes_ReadLE16(waypoint);
				} while (SlipBytes_ReadLE16(waypoint + SLIP_TRD_ROUTE_LAP_JOIN_OFFSET) == 0);
				joinWaypoint = waypoint;
				waypoint = savedWaypoint;
				do {
					waypoint = trdBase + SlipBytes_ReadLE16(waypoint);
					forwardDistance += SlipView3D_VectorLength(
					    (int32_t)(SlipBytes_ReadLE32(waypoint + SLIP_TRD_POSITION_X_OFFSET) - previousX),
					    (int32_t)(SlipBytes_ReadLE32(waypoint + SLIP_TRD_POSITION_Y_OFFSET) - previousY),
					    (int32_t)(SlipBytes_ReadLE32(waypoint + SLIP_TRD_POSITION_Z_OFFSET) - previousZ));
					previousX = SlipBytes_ReadLE32(waypoint + SLIP_TRD_POSITION_X_OFFSET);
					previousY = SlipBytes_ReadLE32(waypoint + SLIP_TRD_POSITION_Y_OFFSET);
					previousZ = SlipBytes_ReadLE32(waypoint + SLIP_TRD_POSITION_Z_OFFSET);
				} while (waypoint != joinWaypoint);
				lapDistance += branchDistance < forwardDistance ? branchDistance : forwardDistance;
			} else {
				const uint16_t nextWaypoint = SlipBytes_ReadLE16(waypoint);

				if (nextWaypoint == 0) {
					break;
				}
				waypoint = trdBase + nextWaypoint;
				lapDistance += SlipView3D_VectorLength(
				    (int32_t)(SlipBytes_ReadLE32(waypoint + SLIP_TRD_POSITION_X_OFFSET) - previousX),
				    (int32_t)(SlipBytes_ReadLE32(waypoint + SLIP_TRD_POSITION_Y_OFFSET) - previousY),
				    (int32_t)(SlipBytes_ReadLE32(waypoint + SLIP_TRD_POSITION_Z_OFFSET) - previousZ));
			}
		}
		waypoint = targetWaypoint;
		SlipTrackWorld_WriteLE32(waypoint + SLIP_TRD_WAYPOINT_LAP_DISTANCE_OFFSET, lapDistance);
		waypoint += SLIP_TRD_ROUTE_RECORD_BYTES;
	} while (--waypointCount != 0);
}

static uint32_t SlipTrackWorld_totalLength;

uint32_t SlipTrackWorld_TotalLength(const uint8_t *trdBase) {
	SlipDraw3DApproxAbsVectorLength distance;
	uint32_t currentOffset;
	uint32_t firstOffset;
	uint32_t nextOffset;
	uint32_t previousX;
	uint32_t previousY;
	uint32_t previousZ;

	currentOffset = (uint32_t)SlipBytes_ReadLE16(trdBase + SLIP_TRD_ROUTE_TABLE_OFFSET) + SLIP_TRD_TABLE_COUNT_BYTES;
	firstOffset = currentOffset;
	previousX = SlipBytes_ReadLE32(trdBase + currentOffset + SLIP_TRD_POSITION_X_OFFSET);
	previousY = SlipBytes_ReadLE32(trdBase + currentOffset + SLIP_TRD_POSITION_Y_OFFSET);
	previousZ = SlipBytes_ReadLE32(trdBase + currentOffset + SLIP_TRD_POSITION_Z_OFFSET);

	for (;;) {
		nextOffset = SlipBytes_ReadLE16(trdBase + currentOffset);
		if (nextOffset == 0) {
			return SlipTrackWorld_totalLength;
		}
		currentOffset = nextOffset;
		SlipDraw3D_ApproxAbsVectorLength(
		    SlipBytes_ReadLE32(trdBase + currentOffset + SLIP_TRD_POSITION_X_OFFSET) - previousX,
		    SlipBytes_ReadLE32(trdBase + currentOffset + SLIP_TRD_POSITION_Y_OFFSET) - previousY,
		    SlipBytes_ReadLE32(trdBase + currentOffset + SLIP_TRD_POSITION_Z_OFFSET) - previousZ, &distance);
		SlipTrackWorld_totalLength += distance.approximateLength;
		previousX = SlipBytes_ReadLE32(trdBase + currentOffset + SLIP_TRD_POSITION_X_OFFSET);
		previousY = SlipBytes_ReadLE32(trdBase + currentOffset + SLIP_TRD_POSITION_Y_OFFSET);
		previousZ = SlipBytes_ReadLE32(trdBase + currentOffset + SLIP_TRD_POSITION_Z_OFFSET);
		if (currentOffset == firstOffset) {
			return SlipTrackWorld_totalLength;
		}
	}
}

uint32_t SlipTrackWorld_TrackFloor(const uint8_t *trkBase) {
	return SlipBytes_ReadLE32(trkBase + SLIP_TRK_FLOOR_HEIGHT_OFFSET);
}

bool SlipTrackWorld_ExecuteObjectAttachmentDraw(const uint8_t *drawRecord, size_t recordBytesRemaining,
                                                uint32_t callerValue, const uint8_t *slotListBase, size_t slotListBytes,
                                                uint32_t slotListBaseAddress, const SlipObject *objectTableBase,
                                                size_t objectTableBytes,
                                                SlipTrackWorldObjectAttachmentDrawExecution *result) {
	uint32_t objectOffset;
	uint16_t currentObjectOffset;
	const uint8_t *cmpBase;
	uint32_t cmpAddress;
	uint32_t cmpOffset;
	uint32_t cmp;

	if (drawRecord == 0 || result == 0 || recordBytesRemaining < SLIP_TRACK_SLOT_DOOR_ADDRESS_END) {
		return false;
	}
	objectOffset = SlipBytes_ReadLE32(drawRecord + offsetof(SlipTrackDrawRecord, objectOffset));
	currentObjectOffset = (uint16_t)objectOffset;
	if (!SlipTrackWorld_SelectSlotListEntry(callerValue, slotListBaseAddress, objectTableBase, objectTableBytes,
	                                        currentObjectOffset, &result->select)) {
		return false;
	}
	cmpBase = drawRecord;
	cmpAddress = callerValue;
	cmpOffset = 0;
	result->cmpUsesSlotListEntry = false;
	if (!result->select.carry) {
		if (slotListBase == 0 ||
		    !SlipTrackWorld_DosAddressToOffset(result->select.slotAddress, slotListBaseAddress, slotListBytes,
		                                       &cmpOffset) ||
		    cmpOffset + SLIP_TRACK_SLOT_DOOR_ADDRESS_END > slotListBytes) {
			return false;
		}
		cmpBase = slotListBase + cmpOffset;
		cmpAddress = result->select.slotAddress;
		result->cmpUsesSlotListEntry = true;
	}
	cmp = SlipBytes_ReadLE32(cmpBase + SLIP_TRACK_SLOT_DOOR_ADDRESS_OFFSET);
	result->block = (SlipTrackWorldObjectAttachmentDraw){drawRecord,
	                                                     true,
	                                                     drawRecord,
	                                                     drawRecord,
	                                                     objectOffset,
	                                                     drawRecord,
	                                                     true,
	                                                     true,
	                                                     cmp,
	                                                     true,
	                                                     false,
	                                                     false,
	                                                     false,
	                                                     false,
	                                                     0,
	                                                     false,
	                                                     false,
	                                                     false,
	                                                     false,
	                                                     false,
	                                                     false,
	                                                     false,
	                                                     SLIP_TRACK_WORLD_OBJECT_ATTACHMENT_SKIPPED};
	result->callerValue = callerValue;
	result->objectOffset = currentObjectOffset;
	result->comparisonAddress = cmpAddress;
	result->comparisonOffset = cmpOffset;
	result->ret = true;
	if (cmp == 0) {
		return true;
	}
	result->block.callDraw3DListPushFrame = true;
	result->block.savedRecordForCallbackRead = true;
	result->block.callObjectGetSlotDrawCallback = true;
	result->block.savedDrawCallback = true;
	result->block.schedulingCallbackAddress = SLIP_TRACK_WORLD_OBJECT_SCHEDULING_CALLBACK_TOKEN;
	result->block.callInstallSchedulingCallback = true;
	result->block.callDrawObject = true;
	result->block.callDraw3DListTraverse = true;
	result->block.callDraw3DListPopFrame = true;
	result->block.restoredDrawCallback = true;
	result->block.callRestoreDrawCallback = true;
	result->block.restoredDrawRecord = true;
	result->block.branch = SLIP_TRACK_WORLD_OBJECT_ATTACHMENT_DRAWN;
	return true;
}

bool SlipTrackWorld_ExecuteObjectCallbackDraw(const uint8_t *drawRecord, size_t recordBytesRemaining,
                                              uint32_t callerValue, uint32_t counter, const uint8_t *slotListBase,
                                              size_t slotListBytes, uint32_t slotListBaseAddress,
                                              const SlipObject *objectTableBase, size_t objectTableBytes,
                                              SlipTrackWorldObjectCallbackDrawExecution *result) {
	uint32_t objectOffset;
	uint16_t currentObjectOffset;
	const uint8_t *cmpBase;
	uint32_t cmpAddress;
	uint32_t cmpOffset;
	uint32_t cmp;

	if (drawRecord == 0 || result == 0 || recordBytesRemaining < SLIP_TRACK_SLOT_DOOR_ADDRESS_END) {
		return false;
	}
	objectOffset = SlipBytes_ReadLE32(drawRecord + offsetof(SlipTrackDrawRecord, objectOffset));
	currentObjectOffset = (uint16_t)objectOffset;
	if (!SlipTrackWorld_SelectSlotListEntry(callerValue, slotListBaseAddress, objectTableBase, objectTableBytes,
	                                        currentObjectOffset, &result->select)) {
		return false;
	}
	cmpBase = drawRecord;
	cmpAddress = callerValue;
	cmpOffset = 0;
	result->cmpUsesSlotListEntry = false;
	if (!result->select.carry) {
		if (slotListBase == 0 ||
		    !SlipTrackWorld_DosAddressToOffset(result->select.slotAddress, slotListBaseAddress, slotListBytes,
		                                       &cmpOffset) ||
		    cmpOffset + SLIP_TRACK_SLOT_DOOR_ADDRESS_END > slotListBytes) {
			return false;
		}
		cmpBase = slotListBase + cmpOffset;
		cmpAddress = result->select.slotAddress;
		result->cmpUsesSlotListEntry = true;
	}
	cmp = SlipBytes_ReadLE32(cmpBase + SLIP_TRACK_SLOT_DOOR_ADDRESS_OFFSET);
	result->block = (SlipTrackWorldObjectCallbackDraw){drawRecord,
	                                                   drawRecord,
	                                                   objectOffset,
	                                                   drawRecord,
	                                                   true,
	                                                   true,
	                                                   cmp,
	                                                   true,
	                                                   false,
	                                                   false,
	                                                   false,
	                                                   0,
	                                                   false,
	                                                   false,
	                                                   false,
	                                                   false,
	                                                   false,
	                                                   counter,
	                                                   SLIP_TRACK_WORLD_OBJECT_CALLBACK_SKIPPED};
	result->callerValue = callerValue;
	result->objectOffset = currentObjectOffset;
	result->comparisonAddress = cmpAddress;
	result->comparisonOffset = cmpOffset;
	result->ret = true;
	if (cmp != 0) {
		return true;
	}
	result->block.savedRecordForCallbackRead = true;
	result->block.callObjectGetSlotDrawCallback = true;
	result->block.savedDrawCallback = true;
	result->block.schedulingCallbackAddress = SLIP_TRACK_WORLD_OBJECT_SCHEDULING_CALLBACK_TOKEN;
	result->block.callInstallSchedulingCallback = true;
	result->block.callDrawObject = true;
	result->block.restoredDrawCallback = true;
	result->block.callRestoreDrawCallback = true;
	result->block.restoredDrawRecord = true;
	result->block.drawnObjectCount = counter + 1u;
	result->block.branch = SLIP_TRACK_WORLD_OBJECT_CALLBACK_DRAWN;
	return true;
}

bool SlipTrackWorld_BindSlotDraw(uint16_t requestedDrawCount, uint8_t *slotDrawBase, size_t slotDrawBytes,
                                 uint16_t allocationHandle, uint32_t slotDrawBaseAddress,
                                 SlipTrackWorldSlotDrawRingVisit *visits, uint16_t visitCapacity,
                                 SlipTrackWorldSlotDrawInstall *result) {
	uint16_t slotDrawCount;
	uint16_t drawCountWithSentinel;
	uint32_t allocationBytes;
	SlipTrackWorldSlotDrawRing ring;

	if (slotDrawBase == 0 || result == 0) {
		return false;
	}
	slotDrawCount = (uint16_t)(requestedDrawCount * SLIP_TRACK_SLOT_DRAW_CAPACITY_MULTIPLIER);
	drawCountWithSentinel = (uint16_t)(slotDrawCount + 1u);
	allocationBytes = drawCountWithSentinel * sizeof(SlipTrackDrawRecord);
	if (slotDrawCount == 0 || allocationBytes > slotDrawBytes) {
		return false;
	}
	if (!SlipTrackWorld_InitSlotDrawRing(slotDrawBase, slotDrawBytes, slotDrawBaseAddress, slotDrawCount, visits,
	                                     visitCapacity, &ring)) {
		return false;
	}
	*result = (SlipTrackWorldSlotDrawInstall){requestedDrawCount,
	                                          slotDrawCount,
	                                          drawCountWithSentinel,
	                                          (uint32_t)sizeof(SlipTrackDrawRecord),
	                                          allocationBytes,
	                                          true,
	                                          allocationHandle,
	                                          true,
	                                          slotDrawBaseAddress,
	                                          true,
	                                          ring,
	                                          true};
	return true;
}

bool SlipTrackWorld_BindSlotList(uint16_t requestedSlotCount, uint8_t *slotListBase, size_t slotListBytes,
                                 uint16_t allocationHandle, uint32_t slotListBaseAddress,
                                 SlipTrackWorldSlotListVisit *visits, uint16_t visitCapacity,
                                 SlipTrackWorldSlotListInstall *result) {
	uint32_t countPlusEight;
	uint32_t countPlusTen;
	uint32_t allocationBytes;
	uint32_t freeListOffset;
	uint32_t currentOffset;
	uint32_t freeListAddress;
	uint16_t i;
	bool hitVisitCapacity;

	if (slotListBase == 0 || result == 0) {
		return false;
	}
	countPlusEight = (uint32_t)requestedSlotCount + SLIP_TRACK_SLOT_SPARE_COUNT;
	countPlusTen = countPlusEight + SLIP_TRACK_SLOT_SENTINEL_COUNT;
	allocationBytes = countPlusTen * sizeof(SlipTrackSlotRecord);
	if (countPlusEight == 0 || allocationBytes > slotListBytes) {
		return false;
	}
	freeListOffset = sizeof(SlipTrackSlotRecord);
	if (freeListOffset + SLIP_TRACK_SLOT_LINKS_END > slotListBytes) {
		return false;
	}
	freeListAddress = slotListBaseAddress + freeListOffset;
	SlipTrackSlotRecord *const slots = (SlipTrackSlotRecord *)(void *)slotListBase;
	slots[0].nextSlotAddress = slotListBaseAddress;
	slots[0].previousSlotAddress = slotListBaseAddress;
	currentOffset = freeListOffset;
	hitVisitCapacity = false;
	for (i = 0; i < (uint16_t)countPlusEight; ++i) {
		const uint32_t nextOffset = currentOffset + sizeof(SlipTrackSlotRecord);

		if ((size_t)currentOffset + SLIP_TRACK_SLOT_NEXT_ADDRESS_END > slotListBytes ||
		    (size_t)nextOffset + SLIP_TRACK_SLOT_LINKS_END > slotListBytes) {
			return false;
		}
		if (visits != 0 && i < visitCapacity) {
			visits[i] = (SlipTrackWorldSlotListVisit){i, currentOffset, nextOffset, slotListBaseAddress + nextOffset,
			                                          slotListBaseAddress + currentOffset};
		} else if (visits != 0) {
			hitVisitCapacity = true;
		}
		slots[currentOffset / sizeof(*slots)].nextSlotAddress = slotListBaseAddress + nextOffset;
		slots[nextOffset / sizeof(*slots)].previousSlotAddress = slotListBaseAddress + currentOffset;
		currentOffset = nextOffset;
	}
	slots[currentOffset / sizeof(*slots)].nextSlotAddress = freeListAddress;
	slots[freeListOffset / sizeof(*slots)].previousSlotAddress = slotListBaseAddress + currentOffset;
	*result = (SlipTrackWorldSlotListInstall){requestedSlotCount,
	                                          countPlusEight,
	                                          (uint16_t)countPlusEight,
	                                          countPlusTen,
	                                          (uint32_t)sizeof(SlipTrackSlotRecord),
	                                          allocationBytes,
	                                          true,
	                                          allocationHandle,
	                                          true,
	                                          slotListBaseAddress,
	                                          slotListBaseAddress,
	                                          freeListAddress,
	                                          (uint16_t)countPlusEight,
	                                          currentOffset,
	                                          freeListAddress,
	                                          slotListBaseAddress + currentOffset,
	                                          hitVisitCapacity,
	                                          true};
	return true;
}

bool SlipTrackWorld_AllocSlotListEntry(uint8_t *slotListBase, size_t slotListBytes, uint32_t slotListBaseAddress,
                                       uint32_t activeListAddress, uint32_t freeListAddress,
                                       SlipTrackWorldSlotListAlloc *result) {
	uint32_t activeListOffset;
	uint32_t freeListOffset;
	uint32_t allocatedAddress;
	uint32_t allocatedOffset;
	uint32_t nextFreeAddress;
	uint32_t nextFreeOffset;
	uint32_t activeNextAddress;
	uint32_t activeNextOffset;
	uint32_t zeroIndex;

	if (slotListBase == 0 || result == 0) {
		return false;
	}
	if (!SlipTrackWorld_DosAddressToOffset(activeListAddress, slotListBaseAddress, slotListBytes, &activeListOffset) ||
	    activeListOffset + SLIP_TRACK_SLOT_LINKS_END > slotListBytes ||
	    !SlipTrackWorld_DosAddressToOffset(freeListAddress, slotListBaseAddress, slotListBytes, &freeListOffset) ||
	    freeListOffset + SLIP_TRACK_SLOT_LINKS_END > slotListBytes) {
		return false;
	}
	SlipTrackSlotRecord *const freeHead = (SlipTrackSlotRecord *)(void *)(slotListBase + freeListOffset);
	allocatedAddress = freeHead->nextSlotAddress;
	*result = (SlipTrackWorldSlotListAlloc){.freeListAddress = freeListAddress,
	                                        .freeListOffset = freeListOffset,
	                                        .allocatedAddress = allocatedAddress,
	                                        .activeListAddress = activeListAddress,
	                                        .activeListOffset = activeListOffset,
	                                        .carry = allocatedAddress == freeListAddress,
	                                        .ret = true};
	if (allocatedAddress == freeListAddress) {
		return true;
	}
	if (!SlipTrackWorld_DosAddressToOffset(allocatedAddress, slotListBaseAddress, slotListBytes, &allocatedOffset) ||
	    allocatedOffset + SLIP_TRACK_SLOT_CURRENT_RECORD_END > slotListBytes) {
		return false;
	}
	SlipTrackSlotRecord *const slot = (SlipTrackSlotRecord *)(void *)(slotListBase + allocatedOffset);
	nextFreeAddress = slot->nextSlotAddress;
	if (!SlipTrackWorld_DosAddressToOffset(nextFreeAddress, slotListBaseAddress, slotListBytes, &nextFreeOffset) ||
	    nextFreeOffset + SLIP_TRACK_SLOT_LINKS_END > slotListBytes) {
		return false;
	}
	SlipTrackSlotRecord *const activeHead = (SlipTrackSlotRecord *)(void *)(slotListBase + activeListOffset);
	activeNextAddress = activeHead->nextSlotAddress;
	if (!SlipTrackWorld_DosAddressToOffset(activeNextAddress, slotListBaseAddress, slotListBytes, &activeNextOffset) ||
	    activeNextOffset + SLIP_TRACK_SLOT_LINKS_END > slotListBytes) {
		return false;
	}

	freeHead->nextSlotAddress = nextFreeAddress;
	SlipTrackSlotRecord *const nextFree = (SlipTrackSlotRecord *)(void *)(slotListBase + nextFreeOffset);
	nextFree->previousSlotAddress = freeListAddress;
	activeHead->nextSlotAddress = allocatedAddress;
	SlipTrackSlotRecord *const activeNext = (SlipTrackSlotRecord *)(void *)(slotListBase + activeNextOffset);
	activeNext->previousSlotAddress = allocatedAddress;
	slot->nextSlotAddress = activeNextAddress;
	slot->previousSlotAddress = activeListAddress;
	slot->firstDrawAddress = 0;
	slot->secondDrawAddress = 0;
	slot->currentTrackRecordAddress = 0;
	for (zeroIndex = 0; zeroIndex < SLIP_TRACK_BOUNDING_CORNER_COUNT; ++zeroIndex) {
		slot->cornerTrackRecords[zeroIndex] = 0;
	}
	result->allocatedOffset = allocatedOffset;
	result->nextFreeAddress = nextFreeAddress;
	result->nextFreeOffset = nextFreeOffset;
	result->activeNextAddress = activeNextAddress;
	result->activeNextOffset = activeNextOffset;
	result->clearedTail = true;
	result->zeroedDwords = SLIP_TRACK_BOUNDING_CORNER_COUNT;
	result->carry = false;
	return true;
}

bool SlipTrackWorld_ClearSlotDrawLinks(uint8_t *slotDrawBase, size_t slotDrawBytes, uint16_t slotDrawCount,
                                       SlipTrackWorldSlotDrawClearVisit *visits, uint16_t visitCapacity,
                                       SlipTrackWorldSlotDrawClear *result) {
	uint16_t i;

	if (slotDrawBase == 0 || result == 0 || slotDrawCount == 0) {
		return false;
	}
	*result = (SlipTrackWorldSlotDrawClear){
	    slotDrawCount, (uint32_t)sizeof(SlipTrackDrawRecord), (uint32_t)sizeof(SlipTrackDrawRecord), 0, false, true};
	for (i = 0; i < slotDrawCount; ++i) {
		const uint32_t clearOffset = (uint32_t)sizeof(SlipTrackDrawRecord) +
		                             (uint32_t)i * (uint32_t)sizeof(SlipTrackDrawRecord) +
		                             SLIP_TRACK_DRAW_ATTACHMENT_READY_OFFSET;

		if ((size_t)clearOffset + sizeof(uint32_t) > slotDrawBytes) {
			return false;
		}
		SlipTrackDrawRecord *const record = &((SlipTrackDrawRecord *)(void *)slotDrawBase)[i + 1u];
		if (visits != 0 && result->clearedCount < visitCapacity) {
			visits[result->clearedCount] =
			    (SlipTrackWorldSlotDrawClearVisit){i, clearOffset, record->attachmentTransformReady};
		} else if (visits != 0) {
			result->hitVisitCapacity = true;
		}
		record->attachmentTransformReady = 0;
		++result->clearedCount;
	}
	return true;
}

bool SlipTrackWorld_PostFrameOverlay(uint16_t overlayEnable, uint32_t renderContextCount, uint32_t primaryLeft,
                                     uint32_t primaryTop, uint32_t primaryRight, uint32_t primaryBottom,
                                     uint32_t secondaryLeft, uint32_t secondaryRight, uint32_t secondaryTop,
                                     uint32_t secondaryBottom, SlipTrackWorldPostFrameOverlay *result) {
	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldPostFrameOverlay){
	    .overlayEnable = overlayEnable,
	    .skippedByZeroEnable = overlayEnable == 0,
	    .renderContextCount = renderContextCount,
	    .branch = overlayEnable == 0 ? SLIP_TRACK_WORLD_POST_FRAME_OVERLAY_BRANCH_DISABLED
	                                 : SLIP_TRACK_WORLD_POST_FRAME_OVERLAY_BRANCH_ENABLED_NO_LINES,
	    .restoredCallerState = true,
	    .ret = true};
	if (overlayEnable == 0) {
		return true;
	}
	if ((int32_t)renderContextCount >= 1) {
		result->drawsPrimary = true;
		result->primaryLines[0] = (SlipTrackWorldOverlayLineCall){.startX = primaryLeft,
		                                                          .startY = primaryTop,
		                                                          .endX = primaryRight,
		                                                          .endY = primaryTop,
		                                                          .callLineDraw = true};
		result->primaryLines[1] = (SlipTrackWorldOverlayLineCall){.startX = primaryLeft,
		                                                          .startY = primaryBottom,
		                                                          .endX = primaryRight,
		                                                          .endY = primaryBottom,
		                                                          .callLineDraw = true};
		result->primaryLines[2] = (SlipTrackWorldOverlayLineCall){.startX = primaryLeft,
		                                                          .startY = primaryTop,
		                                                          .endX = primaryLeft,
		                                                          .endY = primaryBottom,
		                                                          .callLineDraw = true};
		result->primaryLines[3] = (SlipTrackWorldOverlayLineCall){.startX = primaryRight,
		                                                          .startY = primaryTop,
		                                                          .endX = primaryRight,
		                                                          .endY = primaryBottom,
		                                                          .callLineDraw = true};
		result->branch = SLIP_TRACK_WORLD_POST_FRAME_OVERLAY_BRANCH_PRIMARY_ONLY;
	}
	if ((int32_t)renderContextCount >= 2) {
		result->drawsSecondary = true;
		result->secondaryLines[0] = (SlipTrackWorldOverlayLineCall){.startX = secondaryLeft,
		                                                            .startY = secondaryTop,
		                                                            .endX = secondaryRight,
		                                                            .endY = secondaryTop,
		                                                            .callLineDraw = true};
		result->secondaryLines[1] = (SlipTrackWorldOverlayLineCall){.startX = secondaryLeft,
		                                                            .startY = secondaryBottom,
		                                                            .endX = secondaryRight,
		                                                            .endY = secondaryBottom,
		                                                            .callLineDraw = true};
		result->secondaryLines[2] = (SlipTrackWorldOverlayLineCall){.startX = secondaryLeft,
		                                                            .startY = secondaryTop,
		                                                            .endX = secondaryLeft,
		                                                            .endY = secondaryBottom,
		                                                            .callLineDraw = true};
		result->secondaryLines[3] = (SlipTrackWorldOverlayLineCall){.startX = secondaryRight,
		                                                            .startY = secondaryTop,
		                                                            .endX = secondaryRight,
		                                                            .endY = secondaryBottom,
		                                                            .callLineDraw = true};
		result->branch = SLIP_TRACK_WORLD_POST_FRAME_OVERLAY_BRANCH_PRIMARY_AND_SECONDARY;
	}
	return true;
}

bool SlipTrackWorld_ScaledCallSetup(uint16_t lightMultiplierQ14, uint32_t ambientLightScaleQ14,
                                    uint32_t directLightScaleQ14, uint32_t scaledLightX, uint32_t scaledLightY,
                                    uint32_t scaledLightZ, SlipTrackWorldScaledCallSetup *result) {
	uint32_t ambientLightProduct;
	uint32_t directLightProduct;
	uint16_t shiftedProductWord;
	uint16_t directLightScaledQ14;

	if (result == 0) {
		return false;
	}
	shiftedProductWord =
	    SlipTrackWorld_MultiplyWordsShift14(ambientLightScaleQ14, lightMultiplierQ14, &ambientLightProduct);
	directLightScaledQ14 =
	    SlipTrackWorld_MultiplyWordsShift14(directLightScaleQ14, lightMultiplierQ14, &directLightProduct);
	*result = (SlipTrackWorldScaledCallSetup){.lightMultiplierQ14 = lightMultiplierQ14,
	                                          .ambientLightScaleQ14 = ambientLightScaleQ14,
	                                          .ambientLightProduct = ambientLightProduct,
	                                          .ambientLightWithPreservedHighWord =
	                                              (ambientLightScaleQ14 & SLIP_TRACK_REGISTER_UPPER_WORD_MASK) |
	                                              shiftedProductWord,
	                                          .callDraw3DSetAmbientLight = true,
	                                          .directLightScaleQ14 = directLightScaleQ14,
	                                          .directLightProduct = directLightProduct,
	                                          .directLightScaledQ14 = directLightScaledQ14,
	                                          .directLightScaleTo = directLightScaledQ14,
	                                          .lightDirectionX = scaledLightX,
	                                          .lightDirectionY = scaledLightY,
	                                          .lightDirectionZ = scaledLightZ,
	                                          .callDraw3DSetLightVector = true,
	                                          .ret = true};
	result->ambientLightTo = result->ambientLightWithPreservedHighWord;
	return true;
}

bool SlipTrackWorld_RecordAdvance(const uint8_t *record, size_t recordBytesRemaining, size_t recordOffsetBefore,
                                  uint32_t savedCallbackValue, SlipTrackWorldRecordAdvance *result) {
	uint32_t callbackValue;
	uint16_t descriptor;

	if (record == 0 || result == 0 || recordBytesRemaining < 2u) {
		return false;
	}
	descriptor = SlipBytes_ReadLE16(record);
	callbackValue = descriptor;
	*result = (SlipTrackWorldRecordAdvance){.recordOffsetBefore = recordOffsetBefore,
	                                        .savedCallbackValue = savedCallbackValue,
	                                        .vertexDescriptor = descriptor,
	                                        .wideIndices = (descriptor & SLIP_TRC_PRIMITIVE_TEXTURED) != 0,
	                                        .restoredCallbackValue = savedCallbackValue};
	if ((descriptor & SLIP_TRC_PRIMITIVE_TEXTURED) != 0) {
		callbackValue &= SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK;
		result->savedMultiplierState = true;
		result->indexStride = SLIP_TRC_TEXTURED_VERTEX_BYTES;
		result->recordAdvanceBytes = (callbackValue * SLIP_TRC_TEXTURED_VERTEX_BYTES) + SLIP_TRC_PRIMITIVE_HEADER_BYTES;
		result->restoredMultiplierState = true;
		result->branch = SLIP_TRACK_WORLD_RECORD_ADVANCE_BRANCH_HIGH_DESCRIPTOR;
	} else {
		result->recordAdvanceBytes = (callbackValue * SLIP_SERIALIZED_INDEX_BYTES) + SLIP_TRC_PRIMITIVE_HEADER_BYTES;
		result->branch = SLIP_TRACK_WORLD_RECORD_ADVANCE_BRANCH_LOW_DESCRIPTOR;
	}
	result->recordOffsetAfter = recordOffsetBefore + (size_t)result->recordAdvanceBytes;
	return true;
}

bool SlipTrackWorld_PrimitiveWalker(const uint8_t *component, size_t componentBytesRemaining,
                                    const uint8_t *componentBase, size_t componentBaseBytes, uint16_t count,
                                    SlipTrackWorldPrimitiveWalkerVisit *visits, size_t visitCapacity,
                                    SlipTrackWorldPrimitiveWalker *result) {
	uint16_t childOffset;
	uint16_t primitiveCount;
	uint16_t remainingPrimitiveCount;
	size_t listOffset;
	size_t visitCount;
	const uint8_t *record;

	if (component == 0 || componentBase == 0 || result == 0 ||
	    componentBytesRemaining < SLIP_TRC_COMPONENT_PRIMITIVE_LIST_END) {
		return false;
	}
	memset(result, 0, sizeof(*result));
	childOffset = SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_PRIMITIVE_LIST_OFFSET);
	result->childOffset = childOffset;
	result->childZeroBranch = childOffset == 0;
	result->count = count;

	result->directCallbackBranch = childOffset != 0 && count == 0;
	result->restoredSourcePointer = true;
	result->ret = true;
	if (childOffset == 0 || count == 0) {
		return true;
	}
	if ((size_t)childOffset > componentBaseBytes ||
	    componentBaseBytes - (size_t)childOffset < SLIP_TRC_TABLE_COUNT_BYTES) {
		return false;
	}
	listOffset = childOffset;
	result->listOffset = listOffset;
	primitiveCount = SlipBytes_ReadLE16(componentBase + listOffset);
	result->primitiveCount = primitiveCount;
	listOffset += 2u;
	result->firstRecordOffset = listOffset;
	if (primitiveCount == 0) {
		return true;
	}
	if (visits == 0 || visitCapacity < (size_t)primitiveCount) {
		return false;
	}
	remainingPrimitiveCount = primitiveCount;
	visitCount = 0;
	while (remainingPrimitiveCount != 0) {
		SlipTrackWorldPrimitiveWalkerVisit *visit;
		uint8_t flags;
		uint8_t materialFlags;
		uint16_t vertexCountAndFlags;
		uint16_t materialIndex;

		if (listOffset > componentBaseBytes || componentBaseBytes - listOffset < SLIP_TRC_PRIMITIVE_HEADER_BYTES) {
			return false;
		}
		record = componentBase + listOffset;
		visit = visits + visitCount;
		memset(visit, 0, sizeof(*visit));
		flags = record[SLIP_TRC_PRIMITIVE_FLAGS_OFFSET];
		visit->recordOffset = listOffset;
		visit->remainingPrimitiveCount = remainingPrimitiveCount;
		visit->flags = flags;
		visit->skipOnFlags = (flags & SLIP_TRC_PRIMITIVE_WALK_SKIP_MASK) != 0;
		if (!visit->skipOnFlags) {
			materialIndex = SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_MATERIAL_OFFSET);
			visit->materialIndexHighBitCleared = (materialIndex & SLIP_TRC_PRIMITIVE_MATERIAL_UNRESOLVED) != 0;
			if (visit->materialIndexHighBitCleared) {
				materialIndex = 0;
			}
			visit->materialIndex = materialIndex;
			vertexCountAndFlags = SlipBytes_ReadLE16(record);
			materialFlags = record[SLIP_TRC_PRIMITIVE_MATERIAL_FLAGS_OFFSET];
			visit->vertexCountAndFlags = vertexCountAndFlags;
			visit->normalX = SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_NORMAL_X_OFFSET);
			visit->normalY = SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_NORMAL_Y_OFFSET);
			visit->normalZ = SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_NORMAL_Z_OFFSET);
			visit->indexStreamOffset = listOffset + SLIP_TRC_PRIMITIVE_HEADER_BYTES;
			visit->callPlaneVisible = true;
			visit->specialPlaneFlag = (flags & SLIP_TRC_PRIMITIVE_SPECIAL_PLANE) != 0;
			visit->callTrackWorldGlobalCarryGate = (flags & SLIP_TRC_PRIMITIVE_SPECIAL_PLANE) != 0;
			visit->texturedPath = (vertexCountAndFlags & SLIP_TRC_PRIMITIVE_TEXTURED) != 0;
			if (!visit->texturedPath) {
				if (materialFlags == 0) {
					visit->callSolidEmitWithoutMaterial = true;
				} else if ((materialFlags & SLIP_TRC_MATERIAL_EXTENDED) == 0) {
					visit->callSolidEmitWithMaterial = true;
					if ((flags & SLIP_TRC_PRIMITIVE_REPLAY_PLANE) == 0) {
						visit->callMaterialDispatchRegular = true;
					}
				} else if ((flags & SLIP_TRC_PRIMITIVE_REPLAY_PLANE) != 0) {
					visit->callPolygonStatusOptional = true;
				} else {
					visit->callPolygonStatusMaterial = true;
					visit->callMaterialDispatchStatus = true;
				}
			}
		}
		--remainingPrimitiveCount;
		++visitCount;
		if (remainingPrimitiveCount == 0) {
			break;
		}
		if (!SlipTrackWorld_RecordAdvance(record, componentBaseBytes - listOffset, listOffset, 0, &visit->advance)) {
			return false;
		}
		visit->loop = true;
		listOffset = visit->advance.recordOffsetAfter;
	}
	result->visitCount = visitCount;
	return true;
}

bool SlipTrackWorld_PrimitiveCallbackDispatch(
    const uint8_t *record, size_t recordBytesRemaining, size_t recordOffset, uint32_t callbackValueEntry,
    bool planeVisibleCarry, bool trackWorldGlobalCarryGateCarry, bool solidEmitCarry, bool signFlagFrom,
    uint32_t polygonStatusValue, uint16_t materialFrameOffset, uint32_t perspectiveDepth, uint32_t farTextureDepth,
    uint32_t affineDepthThreshold, uint32_t renderFlagsValue, SlipTrackWorldPrimitiveCallbackDispatch *result) {
	uint32_t callbackValueResult;
	uint8_t flags;
	uint8_t materialFlags;
	uint16_t materialIndex;
	uint16_t vertexCountAndFlags;

	if (record == 0 || result == 0 || recordBytesRemaining < SLIP_TRC_PRIMITIVE_HEADER_BYTES) {
		return false;
	}
	memset(result, 0, sizeof(*result));
	flags = record[SLIP_TRC_PRIMITIVE_FLAGS_OFFSET];
	callbackValueResult = (callbackValueEntry & SLIP_TRACK_REGISTER_UPPER_BYTES_MASK) | flags;
	result->callbackValueEntry = callbackValueEntry;
	result->flags = flags;
	result->callbackValueWithFlags = callbackValueResult;
	result->skipOnFlags = (flags & SLIP_TRC_PRIMITIVE_CALLBACK_SKIP_MASK) != 0;
	result->ret = true;
	if (result->skipOnFlags) {
		result->returnStateKnown = true;
		result->callbackValueResult = callbackValueResult;
		result->carryOut = false;
		return true;
	}
	materialIndex = SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_MATERIAL_OFFSET);
	result->materialIndexHighBitCleared = (materialIndex & SLIP_TRC_PRIMITIVE_MATERIAL_UNRESOLVED) != 0;
	if (result->materialIndexHighBitCleared) {
		materialIndex = 0;
	}
	result->materialIndex = materialIndex;
	vertexCountAndFlags = SlipBytes_ReadLE16(record);
	materialFlags = record[SLIP_TRC_PRIMITIVE_MATERIAL_FLAGS_OFFSET];
	result->vertexCountAndFlags = vertexCountAndFlags;
	result->normalX = SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_NORMAL_X_OFFSET);
	result->normalY = SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_NORMAL_Y_OFFSET);
	result->normalZ = SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_NORMAL_Z_OFFSET);
	result->indexStreamOffset = recordOffset + SLIP_TRC_PRIMITIVE_HEADER_BYTES;
	callbackValueResult = (callbackValueResult & SLIP_TRACK_REGISTER_UPPER_WORD_MASK) | result->normalX;
	result->callbackValueWithNormalX = callbackValueResult;
	result->callPlaneVisible = true;
	result->planeVisibleCarry = planeVisibleCarry;
	if (planeVisibleCarry) {
		result->returnStateKnown = true;
		result->callbackValueResult = callbackValueResult;
		result->carryOut = true;
		return true;
	}
	result->specialPlaneFlag = (flags & SLIP_TRC_PRIMITIVE_SPECIAL_PLANE) != 0;
	result->callTrackWorldGlobalCarryGate = (flags & SLIP_TRC_PRIMITIVE_SPECIAL_PLANE) != 0;
	result->trackWorldGlobalCarryGateCarry = result->callTrackWorldGlobalCarryGate && trackWorldGlobalCarryGateCarry;
	if (result->trackWorldGlobalCarryGateCarry) {
		result->returnStateKnown = true;
		result->callbackValueResult = callbackValueResult;
		result->carryOut = true;
		return true;
	}
	result->texturedPath = (vertexCountAndFlags & SLIP_TRC_PRIMITIVE_TEXTURED) != 0;
	if (result->texturedPath) {
		result->pushIndexStream = true;
		result->callMaterialFramePointer = true;
		result->testMaterialFrame = true;
		result->popIndexStream = true;
		result->materialFrameOffset = materialFrameOffset;
		if (materialFrameOffset == 0) {
			result->materialFrameZeroFallback = true;
		} else {
			result->savedCallbackValueBeforeDepth = true;
			result->callPerspectiveDepth = true;
			result->perspectiveDepth = perspectiveDepth;
			result->farTextureDepth = farTextureDepth;
			if ((int32_t)perspectiveDepth > (int32_t)farTextureDepth) {
				result->farDepthFallback = true;
			} else {
				result->nearTextureDepth = affineDepthThreshold;
				result->depthGreaterThanNear = (int32_t)perspectiveDepth > (int32_t)affineDepthThreshold;
				result->callRenderFlagsRead = true;
				result->renderFlagsFrom = renderFlagsValue;
				result->savedRenderFlags = renderFlagsValue;
				if (result->depthGreaterThanNear) {
					result->renderFlagsTo = renderFlagsValue | SLIP_RENDER_ALTERNATE_TEXTURE_RASTER;
				} else {
					result->renderFlagsTo = renderFlagsValue & ~SLIP_RENDER_ALTERNATE_TEXTURE_RASTER;
				}
				result->callRenderFlagsWrite = true;
				result->restoredCallbackValueBeforeEmit = true;
				result->callTextureRowScroll = true;
				result->callTexturedEmit = true;
				result->zeroTextureScroll = true;
				result->callTextureScrollReset = true;
				result->restoreRenderFlags = true;
				result->callRestoreRenderFlags = true;
				result->returnStateKnown = true;
				result->callbackValueResult = renderFlagsValue & UINT16_MAX;
				result->carryOut = false;
				return true;
			}
		}
	}
	vertexCountAndFlags &= SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK;
	result->vertexCountAndFlags = vertexCountAndFlags;
	if (materialFlags == 0) {
		result->callSolidEmitWithoutMaterial = true;
		result->solidEmitCarry = solidEmitCarry;
		result->returnStateKnown = true;
		result->callbackValueResult = callbackValueResult;
		result->carryOut = solidEmitCarry;
	} else if ((materialFlags & SLIP_TRC_MATERIAL_EXTENDED) == 0) {
		result->callSolidEmitWithMaterial = true;
		result->solidEmitCarry = solidEmitCarry;
		if (solidEmitCarry) {
			result->returnStateKnown = true;
			result->callbackValueResult = callbackValueResult;
			result->carryOut = true;
			return true;
		}
		result->callMaterialDispatchRegular = true;
		result->returnStateKnown = true;
		result->callbackValueResult = 0;
		result->carryOut = false;
	} else {
		result->callPolygonStatus = true;
		result->polygonStatusSign = signFlagFrom;
		result->polygonStatusValue = polygonStatusValue;
		if (signFlagFrom) {
			result->returnStateKnown = true;
			result->callbackValueResult = polygonStatusValue;
			result->carryOut = false;
			return true;
		}
		result->callMaterialDispatchStatus = true;
		result->returnStateKnown = true;
		result->callbackValueResult = 0;
		result->carryOut = false;
	}
	return true;
}

bool SlipTrackWorld_DirectCallbackLoop(const uint8_t *component, size_t componentBytesRemaining,
                                       const uint8_t *componentBase, size_t componentBaseBytes,
                                       SlipTrackWorldPrimitiveCallback primitiveCallback, uint32_t callbackValueEntry,
                                       SlipTrackWorldDirectCallbackFunction callbackFunction, void *callbackUserData,
                                       const SlipTrackWorldDirectCallbackEnvironment *callbackEnvironment,
                                       const SlipTrackWorldDirectCallbackInput *callbackInputs,
                                       size_t callbackInputCount, SlipTrackWorldDirectCallbackVisit *visits,
                                       size_t visitCapacity, SlipTrackWorldDirectCallbackLoop *result) {
	uint16_t childOffset;
	uint16_t primitiveCount;
	size_t recordOffset;
	const uint8_t *firstPrimitive;
	size_t i;
	uint32_t callbackValue;

	if (component == 0 || componentBytesRemaining < SLIP_TRC_COMPONENT_PRIMITIVE_LIST_END || componentBase == 0 ||
	    visits == 0 || result == 0) {
		return false;
	}

	childOffset = SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_PRIMITIVE_LIST_OFFSET);
	if ((size_t)childOffset > componentBaseBytes ||
	    componentBaseBytes - (size_t)childOffset < SLIP_TRC_TABLE_COUNT_BYTES) {
		return false;
	}
	recordOffset = childOffset;
	firstPrimitive = componentBase + recordOffset;
	primitiveCount = SlipBytes_ReadLE16(firstPrimitive);
	if (primitiveCount == 0 ||
	    (callbackFunction == 0 && (callbackInputs == 0 || callbackInputCount < (size_t)primitiveCount)) ||
	    visitCapacity < (size_t)primitiveCount) {
		return false;
	}
	firstPrimitive += 2u;
	recordOffset += 2u;
	callbackValue = callbackValueEntry;
	*result = (SlipTrackWorldDirectCallbackLoop){.childOffset = childOffset,
	                                             .primitiveList = componentBase + childOffset,
	                                             .primitiveCount = primitiveCount,
	                                             .firstPrimitive = firstPrimitive,
	                                             .visitCount = primitiveCount,
	                                             .restoredSourcePointer = true,
	                                             .ret = true};
	for (i = 0; i < (size_t)primitiveCount; ++i) {
		SlipTrackWorldDirectCallbackVisit *const visit = visits + i;
		const uint16_t remainingBeforeCallback = (uint16_t)(primitiveCount - (uint16_t)i);
		const uint16_t remainingAfterCallback = (uint16_t)(remainingBeforeCallback - 1u);
		uint32_t callbackValueResult;
		bool carryFromCallback;

		if (recordOffset > componentBaseBytes) {
			return false;
		}
		if (callbackFunction != 0) {
			if (!callbackFunction(firstPrimitive, componentBaseBytes - recordOffset, recordOffset, callbackEnvironment,
			                      callbackValue, callbackUserData, &callbackValueResult, &carryFromCallback)) {
				return false;
			}
		} else {
			callbackValueResult = callbackInputs[i].callbackValueResult;
			carryFromCallback = callbackInputs[i].carryFromCallback;
		}
		callbackValue = callbackValueResult;
		*visit = (SlipTrackWorldDirectCallbackVisit){.recordOffset = recordOffset,
		                                             .remainingBeforeCallback = remainingBeforeCallback,
		                                             .savedRemainingCount = true,
		                                             .primitiveCallback = primitiveCallback,
		                                             .callCallback = true,
		                                             .carryFromCallback = carryFromCallback,
		                                             .restoredRemainingCount = true,
		                                             .remainingAfterCallback = remainingAfterCallback,
		                                             .callbackTerminatedLoop = remainingAfterCallback == 0};
		if (remainingAfterCallback == 0) {
			continue;
		}
		visit->savedCallbackValue = true;
		if (!SlipTrackWorld_RecordAdvance(firstPrimitive, componentBaseBytes - recordOffset, recordOffset,
		                                  callbackValueResult, &visit->advance)) {
			return false;
		}
		recordOffset = visit->advance.recordOffsetAfter;
		firstPrimitive = componentBase + recordOffset;
		visit->advanceToNextRecord = true;
	}
	return true;
}

bool SlipTrackWorld_GlobalCarryGate(uint32_t reflectionEnabled, SlipTrackWorldGlobalCarryGate *result) {
	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldGlobalCarryGate){.savedCallerState = true,
	                                          .reflectionEnabled = reflectionEnabled,
	                                          .zeroBranch = reflectionEnabled == 0,
	                                          .setCarry = reflectionEnabled != 0,
	                                          .clearCarry = reflectionEnabled == 0,
	                                          .carryOut = reflectionEnabled != 0,
	                                          .restoredCallerState = true,
	                                          .ret = true,
	                                          .branch = reflectionEnabled == 0
	                                                        ? SLIP_TRACK_WORLD_GLOBAL_CARRY_GATE_BRANCH_CLEAR
	                                                        : SLIP_TRACK_WORLD_GLOBAL_CARRY_GATE_BRANCH_SET};
	return true;
}

bool SlipTrackWorld_ReplayList(uint16_t count, const uint8_t *list, size_t listBytes,
                               uint32_t drawStateIndexBeforeChunk, uint32_t drawStateIndexAfterChunk,
                               SlipTrackWorldReplayListVisit *visits, size_t visitCapacity,
                               SlipTrackWorldReplayList *result) {
	size_t i;
	uint16_t remainingActors;

	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldReplayList){.count = count,
	                                     .emptyBranch = count == 0,
	                                     .ret = true,
	                                     .branch = count == 0 ? SLIP_TRACK_WORLD_REPLAY_LIST_BRANCH_EMPTY
	                                                          : SLIP_TRACK_WORLD_REPLAY_LIST_BRANCH_LOOP};
	if (count == 0) {
		return true;
	}
	if (list == 0 || visits == 0 || visitCapacity < (size_t)count ||
	    listBytes < (size_t)count * SLIP_TRACK_REPLAY_OBJECT_OFFSET_BYTES) {
		return false;
	}
	result->callGetDrawStateBeforeChunk = true;
	result->drawStateIndexBeforeChunk = drawStateIndexBeforeChunk;
	result->drawStateIndexForChunk = drawStateIndexBeforeChunk + 1u;
	result->callLoadDrawStateForChunk = true;
	result->savedSourcePointer = true;
	result->listAddress = SLIP_TRACK_WORLD_REPLAY_LIST_TOKEN;
	result->visitCount = count;
	remainingActors = count;
	for (i = 0; i < (size_t)count; ++i) {
		SlipTrackWorldReplayListVisit *const visit = visits + i;

		*visit = (SlipTrackWorldReplayListVisit){
		    .listOffset = i * SLIP_TRACK_REPLAY_OBJECT_OFFSET_BYTES,
		    .objectOffset = SlipBytes_ReadLE16(list + i * SLIP_TRACK_REPLAY_OBJECT_OFFSET_BYTES),
		    .callObjectDraw = true,
		    .nextListOffset = (i + 1u) * SLIP_TRACK_REPLAY_OBJECT_OFFSET_BYTES,
		    .remainingObjectCount = (uint16_t)(remainingActors - 1u),
		    .loop = remainingActors != 1u};
		--remainingActors;
	}
	result->restoredSourcePointer = true;
	result->callGetDrawStateAfterChunk = true;
	result->drawStateIndexAfterChunk = drawStateIndexAfterChunk;
	result->restoredDrawStateIndex = drawStateIndexAfterChunk - 1u;
	result->callRestoreDrawStateAfterChunk = true;
	return true;
}

bool SlipTrackWorld_OptionalRecord(const uint8_t *record, size_t recordBytesRemaining, uint16_t planeNormalY,
                                   uint16_t planeNormalZ, uint32_t materialFrameAddress, uint32_t transformedNormalX,
                                   uint32_t transformedNormalY, uint32_t transformedNormalZ,
                                   bool draw3DCapturePostPlaneRingCarry, uint16_t replayCount,
                                   const uint8_t *replayList, size_t replayListBytes, uint32_t replayDrawStateBefore,
                                   uint32_t replayDrawStateAfter, SlipTrackWorldReplayListVisit *replayVisits,
                                   size_t replayVisitCapacity, SlipTrackWorldOptionalRecord *result) {
	uint8_t flags;
	uint32_t pushedDword;

	if (record == 0 || result == 0 || recordBytesRemaining < SLIP_TRC_PRIMITIVE_FIRST_INDEX_DWORD_END) {
		return false;
	}
	flags = record[SLIP_TRC_PRIMITIVE_FLAGS_OFFSET];
	*result = (SlipTrackWorldOptionalRecord){.flags = flags,
	                                         .optionalPlaneDisabled = (flags & SLIP_TRC_PRIMITIVE_REPLAY_PLANE) == 0,
	                                         .ret = true,
	                                         .branch = SLIP_TRACK_WORLD_OPTIONAL_RECORD_BRANCH_SKIPPED};
	if ((flags & SLIP_TRC_PRIMITIVE_REPLAY_PLANE) == 0) {
		return true;
	}
	pushedDword = SlipBytes_ReadLE32(record + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET);
	result->materialIndex = SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_MATERIAL_OFFSET);
	result->callGetMaterialFrameAddress = true;
	result->materialFrameAddress = materialFrameAddress;
	result->materialFrameSource = materialFrameAddress;
	result->savedRecordPointer = true;
	result->pushedDword = pushedDword;
	result->planeNormalX = SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_NORMAL_X_OFFSET);
	result->planeNormalY = planeNormalY;
	result->planeNormalZ = planeNormalZ;
	result->viewMatrixAddress = SLIP_TRACK_WORLD_CAMERA_MATRIX_TOKEN;
	result->callTransformPlaneNormal = true;
	result->transformedNormalX = transformedNormalX;
	result->transformedNormalY = transformedNormalY;
	result->transformedNormalZ = transformedNormalZ;
	result->planePointDescriptor = pushedDword;
	result->savedTransformedNormalX = transformedNormalX;
	result->savedTransformedNormalY = transformedNormalY;
	result->savedTransformedNormalZ = transformedNormalZ;
	result->callPointViewPosition = true;
	result->postPlaneNormalZ = transformedNormalZ;
	result->postPlaneNormalY = transformedNormalY;
	result->postPlaneNormalX = transformedNormalX;
	result->callDraw3DCapturePostPlaneRing = true;
	result->draw3DCapturePostPlaneRingCarry = draw3DCapturePostPlaneRingCarry;
	result->restoredRecordPointer = true;
	if (draw3DCapturePostPlaneRingCarry) {
		result->branch = SLIP_TRACK_WORLD_OPTIONAL_RECORD_BRANCH_CARRY_RETURN;
		return true;
	}
	if (!SlipTrackWorld_ReplayList(replayCount, replayList, replayListBytes, replayDrawStateBefore,
	                               replayDrawStateAfter, replayVisits, replayVisitCapacity, &result->replayList)) {
		return false;
	}
	result->callReplayObjects = true;
	result->callDraw3DReleasePostPlaneRing = true;
	result->branch = SLIP_TRACK_WORLD_OPTIONAL_RECORD_BRANCH_REPLAY;
	return true;
}

bool SlipTrackWorld_ReplaySourceDispatch(const uint8_t *initialRecord, size_t recordBytesRemaining,
                                         const uint8_t *chunkBase, size_t chunkBaseBytes,
                                         SlipTrackWorldReplaySourceDispatch *result) {
	uint16_t offset;

	if (initialRecord == 0 || result == 0 || recordBytesRemaining < SLIP_TRC_PRIMITIVE_FIRST_INDEX_END) {
		return false;
	}
	*result = (SlipTrackWorldReplaySourceDispatch){.savedSourcePointer = true,
	                                               .initialRecord = initialRecord,
	                                               .callScanInitialRecord = true,
	                                               .savedRecordForFirstExit = true,
	                                               .restoredRecordAfterFirstExit = true,
	                                               .savedRecordForSecondExit = true,
	                                               .restoredRecordAfterSecondExit = true,
	                                               .savedRecordForThirdExit = true,
	                                               .restoredRecordAfterThirdExit = true,
	                                               .restoredSourcePointer = true,
	                                               .ret = true};
	offset = SlipBytes_ReadLE16(initialRecord + SLIP_TRD_SECTION_FIRST_EXIT_OFFSET);
	if (!SlipTrackWorld_ReplaySourceChild(offset, chunkBase, chunkBaseBytes, &result->firstExit)) {
		return false;
	}
	offset = SlipBytes_ReadLE16(initialRecord + SLIP_TRD_SECTION_SECOND_EXIT_OFFSET);
	if (!SlipTrackWorld_ReplaySourceChild(offset, chunkBase, chunkBaseBytes, &result->secondExit)) {
		return false;
	}
	offset = SlipBytes_ReadLE16(initialRecord + SLIP_TRD_SECTION_THIRD_EXIT_OFFSET);
	if (!SlipTrackWorld_ReplaySourceChild(offset, chunkBase, chunkBaseBytes, &result->thirdExit)) {
		return false;
	}
	return true;
}

bool SlipTrackWorld_ReplaySourceScan(const uint8_t *record, size_t recordBytesRemaining, uint32_t objectBaseAddress,
                                     const uint8_t *objectBase, size_t objectBaseBytes, uint16_t initialCount,
                                     uint16_t replayList[SLIP_TRACK_REPLAY_OBJECT_CAPACITY],
                                     uint32_t *objectDrawCallbacks, size_t objectDrawCallbackCount,
                                     SlipTrackWorldReplaySourceScanVisit *visits, size_t visitCapacity,
                                     SlipTrackWorldReplaySourceScan *result) {
	uint16_t objectListOffset;
	size_t firstNodeOffset;
	size_t nodeOffset;
	uint32_t firstNodeAddress;
	uint32_t nodeAddress;
	uint16_t count;
	size_t visitCount;

	if (record == 0 || result == 0 || recordBytesRemaining < SLIP_TRD_SECTION_DRAW_LIST_END ||
	    initialCount > SLIP_TRACK_REPLAY_OBJECT_CAPACITY) {
		return false;
	}
	objectListOffset = SlipBytes_ReadLE16(record + SLIP_TRD_SECTION_DRAW_LIST_OFFSET);
	*result = (SlipTrackWorldReplaySourceScan){.savedRecordPointer = true,
	                                           .objectListOffset = objectListOffset,
	                                           .finalCount = initialCount,
	                                           .restoredRecordPointer = true,
	                                           .ret = true,
	                                           .branch = objectListOffset == 0
	                                                         ? SLIP_TRACK_WORLD_REPLAY_SOURCE_SCAN_BRANCH_EMPTY
	                                                         : SLIP_TRACK_WORLD_REPLAY_SOURCE_SCAN_BRANCH_VISITED};
	if (objectListOffset == 0) {
		return true;
	}
	if (objectBase == 0 || replayList == 0 || objectDrawCallbacks == 0 || visits == 0 ||
	    (size_t)objectListOffset > objectBaseBytes ||
	    objectBaseBytes - (size_t)objectListOffset < sizeof(SlipTrackDrawRecord)) {
		return false;
	}
	firstNodeOffset = objectListOffset;
	nodeOffset = firstNodeOffset;
	firstNodeAddress = objectBaseAddress + (uint32_t)firstNodeOffset;
	nodeAddress = firstNodeAddress;
	count = initialCount;
	visitCount = 0;
	result->firstNodeOffset = firstNodeOffset;
	do {
		const uint8_t *node;
		SlipTrackWorldReplaySourceScanVisit *visit;
		uint16_t objectOffset;
		bool duplicate;
		size_t i;

		if (visitCount >= visitCapacity || visitCount >= objectDrawCallbackCount) {
			return false;
		}
		if (nodeOffset > objectBaseBytes || objectBaseBytes - nodeOffset < sizeof(SlipTrackDrawRecord)) {
			return false;
		}
		node = objectBase + nodeOffset;
		visit = visits + visitCount;
		objectOffset = (uint16_t)SlipBytes_ReadLE32(node + offsetof(SlipTrackDrawRecord, objectOffset));
		*visit = (SlipTrackWorldReplaySourceScanVisit){.nodeOffset = nodeOffset,
		                                               .nodeAddress = nodeAddress,
		                                               .objectOffset = objectOffset,
		                                               .savedRecordPointer = true,
		                                               .callReadObjectDrawCallback = true,
		                                               .objectDrawCallback = objectDrawCallbacks[visitCount],
		                                               .testedObjectDrawCallback = objectDrawCallbacks[visitCount],
		                                               .restoredRecordPointer = true,
		                                               .countBefore = count};
		if (visit->testedObjectDrawCallback == 0) {
			visit->branch = SLIP_TRACK_WORLD_REPLAY_SOURCE_SCAN_VISIT_BRANCH_ZERO_EAX_AFTER_MOV;
		} else {
			if (count == SLIP_TRACK_REPLAY_OBJECT_CAPACITY) {
				visit->listFull = true;
				visit->branch = SLIP_TRACK_WORLD_REPLAY_SOURCE_SCAN_VISIT_BRANCH_LIST_FULL;
				++visitCount;
				break;
			}
			duplicate = false;
			for (i = 0; i < (size_t)count; ++i) {
				if (replayList[i] == objectOffset) {
					duplicate = true;
					break;
				}
			}
			visit->duplicate = duplicate;
			if (duplicate) {
				visit->branch = SLIP_TRACK_WORLD_REPLAY_SOURCE_SCAN_VISIT_BRANCH_DUPLICATE;
			} else {
				visit->listStoreOffset = (size_t)count * sizeof(replayList[0]);
				replayList[count] = objectOffset;
				count = (uint16_t)(count + 1u);
				visit->countAfter = count;
				visit->storedObjectOffset = true;
				visit->branch = SLIP_TRACK_WORLD_REPLAY_SOURCE_SCAN_VISIT_BRANCH_APPENDED;
			}
		}
		nodeAddress = SlipBytes_ReadLE32(node);
		visit->nextNodeAddress = nodeAddress;
		if (nodeAddress < objectBaseAddress) {
			return false;
		}
		nodeOffset = (size_t)(nodeAddress - objectBaseAddress);
		visit->nextNodeOffset = nodeOffset;
		visit->loop = nodeAddress != firstNodeAddress;
		++visitCount;
	} while (nodeAddress != firstNodeAddress);
	result->visitCount = visitCount;
	result->finalCount = count;
	return true;
}

bool SlipTrackWorld_MaterialStateStore(uint32_t planeOriginX, uint32_t planeOriginY, uint32_t planeOriginZ,
                                       uint32_t planeNormalX, uint32_t planeNormalY, uint32_t planeNormalZ,
                                       SlipTrackWorldMaterialStateStore *result) {
	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldMaterialStateStore){.planeNormalX = planeNormalX,
	                                             .planeNormalY = planeNormalY,
	                                             .planeNormalZ = planeNormalZ,
	                                             .planeOriginX = planeOriginX,
	                                             .planeOriginY = planeOriginY,
	                                             .planeOriginZ = planeOriginZ,
	                                             .ret = true};
	return true;
}

bool SlipTrackWorld_MaterialHandler(uint32_t perspectiveDepth, uint16_t positiveShade, uint16_t negativeShade,
                                    bool carryFrom, const uint8_t *list, size_t listBytes,
                                    SlipTrackWorldMaterialHandlerVisit *visits, size_t visitCapacity,
                                    SlipTrackWorldMaterialHandler *result) {
	uint16_t shadeEntryCount;
	size_t entryOffset;
	size_t visitCount;

	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldMaterialHandler){.perspectiveDepth = perspectiveDepth, .ret = true};
	if ((int32_t)perspectiveDepth > SLIP_TRACK_MATERIAL_HANDLER_MAXIMUM_DEPTH) {
		result->thresholdReturn = true;
		result->branch = SLIP_TRACK_WORLD_MATERIAL_HANDLER_BRANCH_THRESHOLD_RET;
		return true;
	}
	result->positiveShadeOffset = 1u;
	result->callReadPositiveShade = true;
	result->positiveShade = positiveShade;
	result->negativeShadeOffset = UINT32_MAX;
	result->callReadNegativeShade = true;
	result->negativeShade = negativeShade;
	result->polygonIndicesAddress = SLIP_TRACK_WORLD_POLYGON_INDICES_TOKEN;
	result->callBuildMaterialPolygon = true;
	result->materialPolygonCarry = carryFrom;
	if (carryFrom) {
		result->branch = SLIP_TRACK_WORLD_MATERIAL_HANDLER_BRANCH_SETUP_CARRY_RET;
		return true;
	}
	if (list == 0 || visits == 0 || listBytes < 2u) {
		return false;
	}
	shadeEntryCount = SlipBytes_ReadLE16(list);
	if (shadeEntryCount == 0 || visitCapacity < (size_t)shadeEntryCount ||
	    listBytes - 2u < (size_t)shadeEntryCount * SLIP_TRACK_SHADE_ENTRY_BYTES) {
		return false;
	}
	result->shadeEntriesAddress = SLIP_TRACK_WORLD_SHADE_ENTRIES_TOKEN;
	result->shadeEntryCount = shadeEntryCount;
	entryOffset = 2u;
	visitCount = 0;
	while (shadeEntryCount != 0) {
		SlipTrackWorldMaterialHandlerVisit *const visit = visits + visitCount;
		const uint16_t selector = SlipBytes_ReadLE16(list + entryOffset + SLIP_TRACK_SHADE_SELECTOR_OFFSET);

		*visit = (SlipTrackWorldMaterialHandlerVisit){.entryOffset = entryOffset,
		                                              .remainingBefore = shadeEntryCount,
		                                              .defaultShade = result->positiveShade,
		                                              .selector = selector,
		                                              .selectedShade =
		                                                  selector == 0 ? result->positiveShade : result->negativeShade,
		                                              .callShadeEmit = true,
		                                              .nextEntryOffset = entryOffset + SLIP_TRACK_SHADE_ENTRY_BYTES,
		                                              .remainingAfter = (uint16_t)(shadeEntryCount - 1u),
		                                              .loop = (uint16_t)(shadeEntryCount - 1u) != 0};
		entryOffset += SLIP_TRACK_SHADE_ENTRY_BYTES;
		shadeEntryCount = (uint16_t)(shadeEntryCount - 1u);
		++visitCount;
	}
	result->visitCount = visitCount;
	result->callReleaseMaterialPolygon = true;
	result->branch = SLIP_TRACK_WORLD_MATERIAL_HANDLER_BRANCH_LOOP;
	return true;
}

static const uint8_t *track_world_actorTrdBase;
static size_t track_world_actorTrdBytes;
static const uint8_t *track_world_actorSlotDrawBase;
static size_t track_world_actorSlotDrawBytes;
static uint32_t track_world_actorSlotDrawBaseAddress;
static const SlipObject *track_world_actorObjectTable;
static size_t track_world_actorObjectTableBytes;

void SlipTrackWorld_BindActorRingHostMappings(const uint8_t *trdBase, size_t trackDataSize, const uint8_t *slotDrawBase,
                                              size_t slotDrawBytes, uint32_t slotDrawBaseAddress,
                                              const SlipObject *objectTable, size_t objectTableBytes) {
	track_world_actorTrdBase = trdBase;
	track_world_actorTrdBytes = trackDataSize;
	track_world_actorSlotDrawBase = slotDrawBase;
	track_world_actorSlotDrawBytes = slotDrawBytes;
	track_world_actorSlotDrawBaseAddress = slotDrawBaseAddress;
	track_world_actorObjectTable = objectTable;
	track_world_actorObjectTableBytes = objectTableBytes;
}

static void SlipTrackWorld_ReplaySourceScanInternal(const uint8_t *record, SlipTrackWorldComponentSetup *result) {
	uint16_t objectListOffset;
	uint32_t firstNodeAddress;
	uint32_t nodeAddress;

	if (record == NULL || result == NULL || track_world_actorSlotDrawBase == NULL ||
	    track_world_actorObjectTable == NULL) {
		return;
	}
	objectListOffset = SlipBytes_ReadLE16(record + SLIP_TRD_SECTION_DRAW_LIST_OFFSET);
	if (objectListOffset == 0) {
		return;
	}
	firstNodeAddress = track_world_actorSlotDrawBaseAddress + objectListOffset;
	nodeAddress = firstNodeAddress;
	do {
		uint32_t nodeOffset;
		const uint8_t *node;
		uint16_t objectOffset;
		const SlipObject *object;
		uint16_t replayIndex;

		if (nodeAddress < track_world_actorSlotDrawBaseAddress) {
			return;
		}
		nodeOffset = nodeAddress - track_world_actorSlotDrawBaseAddress;
		if ((size_t)nodeOffset + sizeof(SlipTrackDrawRecord) > track_world_actorSlotDrawBytes) {
			return;
		}
		node = track_world_actorSlotDrawBase + nodeOffset;
		objectOffset = (uint16_t)SlipBytes_ReadLE32(node + offsetof(SlipTrackDrawRecord, objectOffset));
		if ((size_t)objectOffset + SLIP_OBJECT_DOS_STRIDE <= track_world_actorObjectTableBytes) {
			object = &track_world_actorObjectTable[objectOffset / SLIP_OBJECT_DOS_STRIDE];
			if (object->drawCallback != NULL) {
				if (result->replayCount == SLIP_TRACK_REPLAY_OBJECT_CAPACITY) {
					return;
				}
				for (replayIndex = 0; replayIndex < result->replayCount; ++replayIndex) {
					if (result->replayList[replayIndex] == objectOffset) {
						break;
					}
				}
				if (replayIndex == result->replayCount) {
					result->replayList[result->replayCount++] = objectOffset;
				}
			}
		}
		nodeAddress = SlipBytes_ReadLE32(node);
	} while (nodeAddress != firstNodeAddress);
}

static void SlipTrackWorld_ReplayListBuild(const uint8_t *record, size_t recordBytesRemaining,
                                           SlipTrackWorldComponentSetup *result) {
	uint32_t linkIndex;

	if (track_world_actorTrdBase == NULL || record == NULL || result == NULL ||
	    recordBytesRemaining < SLIP_TRD_SECTION_DRAW_LIST_END) {
		return;
	}
	SlipTrackWorld_ReplaySourceScanInternal(record, result);
	for (linkIndex = SLIP_TRD_SECTION_FIRST_EXIT_OFFSET; linkIndex <= SLIP_TRD_SECTION_THIRD_EXIT_OFFSET;
	     linkIndex += SLIP_TRD_SECTION_EXIT_BYTES) {
		const uint16_t childOffset = SlipBytes_ReadLE16(record + linkIndex);

		if (childOffset != 0 && (size_t)childOffset + SLIP_TRD_SECTION_DRAW_LIST_END <= track_world_actorTrdBytes) {
			SlipTrackWorld_ReplaySourceScanInternal(track_world_actorTrdBase + childOffset, result);
		}
	}
}

bool SlipTrackWorld_ComponentSetup(const uint8_t *currentRecord, size_t recordBytesRemaining, uint32_t incomingValue,
                                   uint32_t componentViewZ, uint32_t inputDrawFlags, uint32_t textureMode,
                                   uint32_t shading, int32_t componentDistance, uint32_t shadingSecondary,
                                   int32_t componentRadius, const uint8_t *specialRecord, uint32_t globalAfter,
                                   uint16_t randomState, uint32_t shadows, uint32_t processedComponentCount,
                                   uint32_t drawStateIndexBefore, const SlipTrackWorldComponentRefuelCalls *refuel,
                                   SlipTrackWorldComponentSetup *result) {
	uint32_t currentComponentViewZ;
	uint16_t recordShade;
	uint32_t attachmentListOffset;
	uint32_t x;
	uint32_t y;
	SlipTrackWorldDrawFlags drawFlags;

	if (currentRecord == 0 || result == 0 || recordBytesRemaining < SLIP_TRD_SECTION_BYTES) {
		return false;
	}
	currentComponentViewZ = componentViewZ;
	if (!SlipTrackWorld_UpdateDrawFlags(inputDrawFlags, (int32_t)currentComponentViewZ, textureMode, shading,
	                                    componentDistance, shadingSecondary, componentRadius, &drawFlags)) {
		return false;
	}

	if (refuel)
		refuel->build(refuel->context, currentRecord, incomingValue, &drawFlags, &globalAfter);
	recordShade = SlipBytes_ReadLE16(currentRecord + SLIP_TRD_SECTION_SHADE_OFFSET);
	*result = (SlipTrackWorldComponentSetup){currentComponentViewZ,
	                                         true,
	                                         drawFlags,
	                                         true,
	                                         recordShade,
	                                         false,
	                                         false,
	                                         0,
	                                         0,
	                                         0,
	                                         0,
	                                         false,
	                                         {0, 0, 0},
	                                         currentRecord,
	                                         true,
	                                         processedComponentCount + 1u,
	                                         true,
	                                         drawStateIndexBefore,
	                                         drawStateIndexBefore + 1u,
	                                         true};
	if (currentRecord == specialRecord && globalAfter != 0) {
		uint16_t randomShade = refuel ? refuel->random(refuel->context) : SlipTrackWorld_RandomStep(randomState);

		randomShade &= SLIP_REFUEL_SHADE_MASK;
		recordShade = randomShade;
		result->specialRecord = true;
		result->callTrackWorldRandomStep = true;
		result->randomShade = randomShade;
	}
	result->shade = recordShade;
	attachmentListOffset = SlipBytes_ReadLE16(currentRecord + SLIP_TRD_SECTION_DRAW_LIST_OFFSET);
	result->attachmentListOffset = attachmentListOffset;
	result->replayCount = 0;
	if (shadows != 0) {
		result->calledReplayListBuild = true;

		SlipTrackWorld_ReplayListBuild(currentRecord, recordBytesRemaining, result);
	}
	x = SlipBytes_ReadLE32(currentRecord + SLIP_TRD_SECTION_ORIGIN_X_OFFSET);
	y = SlipBytes_ReadLE32(currentRecord + SLIP_TRD_SECTION_ORIGIN_Y_OFFSET);
	currentComponentViewZ = SlipBytes_ReadLE32(currentRecord + SLIP_TRD_SECTION_ORIGIN_Z_OFFSET);
	result->objectWorldPosition = (SlipView3DVec32){(int32_t)x, (int32_t)y, (int32_t)currentComponentViewZ};
	return true;
}

bool SlipTrackWorld_ComponentTail(const uint8_t *component, size_t componentBytesRemaining,
                                  const uint8_t *componentBase, size_t componentBaseBytes, uint16_t shade,
                                  uint32_t renderFlagsValue, uint16_t actorReplayMode, const uint8_t *storedComponent,
                                  uint32_t ambientLightScaleQ14, uint32_t scaledLightX, uint32_t scaledLightY,
                                  uint32_t scaledLightZ, uint32_t directLightScaleQ14, uint32_t renderContextCount,
                                  SlipTrackWorldPrimitiveCallback primitiveCallback, uint32_t drawStateIndexBefore,
                                  SlipTrackWorldComponentTailVisit *visits, size_t visitCapacity, size_t *visitCount,
                                  SlipTrackWorldComponentTail *result) {
	uint16_t childOffset;
	uint16_t nestedOffset;
	uint32_t currentDrawStateIndexBefore;

	if (component == 0 || componentBase == 0 || visitCount == 0 || result == 0 ||
	    componentBytesRemaining < SLIP_TRC_COMPONENT_POINT_LIST_END) {
		return false;
	}
	*visitCount = 0;
	childOffset = SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_POINT_LIST_OFFSET);
	memset(result, 0, sizeof(*result));
	result->childOffset = childOffset;
	result->noChildList = childOffset == 0;
	result->nestedOffset = childOffset;
	result->savedComponentForNestedList = childOffset != 0;
	result->restoredComponentAfterNestedList = true;
	result->drawStateIndexBefore = drawStateIndexBefore;
	result->drawStateIndexAfter = drawStateIndexBefore - 1u;
	result->callLoadDrawState = true;
	result->restoredRecordPointer = true;
	result->returned = true;
	if (childOffset != 0) {
		const uint8_t *childRecord;
		uint16_t vertexCount;

		if ((size_t)childOffset > componentBaseBytes ||
		    componentBaseBytes - (size_t)childOffset < SLIP_TRC_TABLE_COUNT_BYTES) {
			return false;
		}
		childRecord = componentBase + childOffset;
		vertexCount = SlipBytes_ReadLE16(childRecord);
		result->noChildList = false;
		result->childRecord = childRecord;
		result->vertexCount = vertexCount;
		result->savedComponentForVertexBuild = true;
		result->vertexSource = childRecord + SLIP_TRC_TABLE_COUNT_BYTES;
		result->vertexSourceStride = SLIP_TRC_POINT_BYTES;
		result->callBuildVertexRecords = true;
		result->restoredComponentAfterVertexBuild = true;
		result->shade = shade;
		result->calledScaleLight = true;
		result->callTrackWorldPrimitiveWalker = true;
		result->loadedRenderFlags = true;
		result->renderFlagsValue = renderFlagsValue;
		result->savedRenderFlags = true;
		result->calledComponentActorDraw = true;
		result->restoredRenderFlags = renderFlagsValue;
		result->callRendererSetFlags = true;
		result->replayCount = actorReplayMode;
		if (actorReplayMode != 0) {
			result->calledReplayObjects = true;
			result->replayRecord = storedComponent;
		}
		result->ambientLightScaleQ14 = ambientLightScaleQ14;
		result->callDraw3DSetAmbientLight = true;
		result->scaledLightX = scaledLightX;
		result->scaledLightY = scaledLightY;
		result->scaledLightZ = scaledLightZ;
		result->directLightScaleQ14 = directLightScaleQ14;
		result->callDraw3DSetLightVector = true;
		result->renderContextCount = renderContextCount;
		if (renderContextCount != 0) {
			if (componentBytesRemaining < SLIP_TRC_COMPONENT_LINK_HEADER_BYTES) {
				return false;
			}
			nestedOffset = SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_NESTED_LIST_OFFSET);
			result->nestedOffset = nestedOffset;
			if (nestedOffset != 0) {
				const uint8_t *primitiveRecord;
				uint16_t loopCount;

				if ((size_t)nestedOffset > componentBaseBytes ||
				    componentBaseBytes - (size_t)nestedOffset < SLIP_TRC_TABLE_COUNT_BYTES) {
					return false;
				}
				result->savedComponentForNestedList = true;
				result->nestedList = componentBase + nestedOffset;
				loopCount = SlipBytes_ReadLE16(result->nestedList);
				result->loopCount = loopCount;
				primitiveRecord = result->nestedList + SLIP_TRC_TABLE_COUNT_BYTES;
				while (loopCount != 0) {
					SlipTrackWorldComponentTailVisit visit;

					if (*visitCount >= visitCapacity || visits == 0) {
						return false;
					}
					visit = (SlipTrackWorldComponentTailVisit){
					    primitiveRecord, primitiveCallback, loopCount, (uint16_t)(loopCount - 1u), false, 0, false, 0};
					visits[*visitCount] = visit;
					++*visitCount;
					--loopCount;
					if (loopCount == 0) {
						result->restoredComponentAfterNestedList = true;
						break;
					}
					if (primitiveRecord < componentBase ||
					    (size_t)(primitiveRecord - componentBase) > componentBaseBytes ||
					    componentBaseBytes - (size_t)(primitiveRecord - componentBase) < 2u) {
						return false;
					}
					visit.descriptor = SlipBytes_ReadLE16(primitiveRecord);
					visit.readAdvance = true;
					visit.highBit = (visit.descriptor & SLIP_TRC_PRIMITIVE_TEXTURED) != 0;
					if (visit.highBit) {
						visit.advance = ((uint32_t)(visit.descriptor & SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK) *
						                 SLIP_TRC_TEXTURED_VERTEX_BYTES) +
						                SLIP_TRC_PRIMITIVE_HEADER_BYTES;
					} else {
						visit.advance = ((uint32_t)visit.descriptor * SLIP_SERIALIZED_INDEX_BYTES) +
						                SLIP_TRC_PRIMITIVE_HEADER_BYTES;
					}
					visits[*visitCount - 1u] = visit;
					primitiveRecord += visit.advance;
				}
			}
		}
	}
	result->childOffsetForVertexRestore = childOffset;
	result->calledRestoreVertexBufferCursor = childOffset != 0;
	currentDrawStateIndexBefore = SlipDraw3D_CurrentStateRecordIndex(drawStateIndexBefore);
	result->drawStateIndexBefore = currentDrawStateIndexBefore;
	result->drawStateIndexAfter = currentDrawStateIndexBefore - 1u;
	return true;
}

bool SlipTrackWorld_ClearListHeads(uint8_t *objectList, size_t objectListBytes, uint8_t *deferredList,
                                   size_t deferredListBytes, SlipTrackWorldListHeadClear *result) {
	if (objectList == 0 || deferredList == 0 || result == 0 ||
	    objectListBytes < SLIP_TRACK_VISIBILITY_LIST_HEADER_BYTES ||
	    deferredListBytes < SLIP_TRACK_DEFERRED_LIST_HEADER_BYTES) {
		return false;
	}
	SlipTrackWorld_WriteLE32(objectList, 0);
	SlipTrackWorld_WriteLE32(deferredList, 0);
	*result = (SlipTrackWorldListHeadClear){objectList, true, objectList + SLIP_TRACK_VISIBILITY_LIST_HEADER_BYTES,
	                                        deferredList, true};
	return true;
}

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
                              SlipTrackWorldTraversalContext *traversalContext, SlipTrackWorldListSetup *result) {
	uint32_t drawStateIndexAfter;
	SlipTrackWorldListHeadClear heads;
	SlipTrackWorldLoadClipRegisters loadClip;
	SlipObjectPosition objectPosition;
	SlipTrackWorldRecordSearch recordSearch;
	SlipTrackWorldObjectSelect objectSelect;
	SlipTrackWorldObjectDraw objectDraw;
	const uint8_t *selectedRecord;
	size_t selectedRecordBytes;
	uint32_t selectedRecordOffset;
	uint32_t objectListCursorAddress;

	if (result == 0 ||
	    !SlipTrackWorld_ClearListHeads(objectList, objectListBytes, deferredList, deferredListBytes, &heads)) {
		return false;
	}
	*result = (SlipTrackWorldListSetup){heads.objectListHead,
	                                    heads.zeroObjectCount,
	                                    heads.objectListWriteCursor,
	                                    heads.deferredListHead,
	                                    heads.zeroDeferredCount,
	                                    frameRenderFlags,
	                                    false,
	                                    0,
	                                    0,
	                                    false,
	                                    0,
	                                    false,
	                                    false,
	                                    0,
	                                    0,
	                                    0,
	                                    0,
	                                    0,
	                                    0,
	                                    0,
	                                    0,
	                                    0,
	                                    0,
	                                    defaultTraversalGate,
	                                    false,
	                                    {0},
	                                    false,
	                                    0,
	                                    NULL,
	                                    false,
	                                    false,
	                                    false,
	                                    0,
	                                    false,
	                                    false,
	                                    {0},
	                                    SLIP_TRACK_WORLD_LIST_SETUP_BRANCH_INITIALIZED};
	if (frameRenderFlags == UINT32_MAX) {

		result->primaryLeft = viewportMinX;
		result->primaryTop = viewportMinY;
		result->primaryRight = viewportMaxX;
		result->primaryBottom = viewportMaxY;
		result->renderContextCount = 1u;
		if (traversalContext != 0) {
			if (traversalContext->renderContextCount != 0) {
				*traversalContext->renderContextCount = 1u;
			}
			if (traversalContext->primaryLeft != 0) {
				*traversalContext->primaryLeft = viewportMinX;
			}
			if (traversalContext->primaryTop != 0) {
				*traversalContext->primaryTop = viewportMinY;
			}
			if (traversalContext->primaryRight != 0) {
				*traversalContext->primaryRight = viewportMaxX;
			}
			if (traversalContext->primaryBottom != 0) {
				*traversalContext->primaryBottom = viewportMaxY;
			}
		}
		return true;
	}
	drawStateIndexAfter = SlipDraw3D_CurrentStateRecordIndex(drawStateIndexBefore);
	++drawStateIndexAfter;
	result->callGetDrawStateIndex = true;
	result->drawStateIndexBefore = drawStateIndexBefore;
	result->drawStateIndexAfter = drawStateIndexAfter;
	result->callLoadDrawState = true;
	result->objectOffset = 0;
	result->calledObjectPosition = true;
	result->calledMatrixInstall = true;
	result->viewportMinX = viewportMinX;
	result->viewportMinY = viewportMinY;
	result->viewportMaxX = viewportMaxX;
	result->viewportMaxY = viewportMaxY;
	result->primaryLeft = viewportMinX;
	result->primaryTop = viewportMinY;
	result->primaryRight = viewportMaxX;
	result->primaryBottom = viewportMaxY;
	result->renderContextCount = 1u;
	result->componentMask = UINT16_MAX;

	if (traversalContext != NULL) {
		if (traversalContext->primaryLeft != NULL) {
			*traversalContext->primaryLeft = result->primaryLeft;
		}
		if (traversalContext->primaryTop != NULL) {
			*traversalContext->primaryTop = result->primaryTop;
		}
		if (traversalContext->primaryRight != NULL) {
			*traversalContext->primaryRight = result->primaryRight;
		}
		if (traversalContext->primaryBottom != NULL) {
			*traversalContext->primaryBottom = result->primaryBottom;
		}
		if (traversalContext->renderContextCount != NULL) {
			*traversalContext->renderContextCount = result->renderContextCount;
		}
		if (traversalContext->mask != NULL) {
			*traversalContext->mask = result->componentMask;
		}
	}
	result->branch = SLIP_TRACK_WORLD_LIST_SETUP_BRANCH_READY;
	result->calledObjectPositionForSearch = true;
	if (!SlipObject_Position(objectTableBase, objectTableSize, 0, &objectPosition)) {
		return false;
	}
	result->objectPosition = objectPosition;
	result->calledRecordSearch = true;
	if (!SlipTrackWorld_RecordSearch(trdBase, trackDataSize, componentBase, componentBaseBytes, table, tableBytes,
	                                 trdBaseAddress, recordAddress, (int32_t)objectPosition.positionX,
	                                 (int32_t)objectPosition.positionY, (int32_t)objectPosition.positionZ,
	                                 &recordSearch)) {
		return false;
	}
	result->selectedRecordAddressOr = recordSearch.selectedRecordAddress;
	selectedRecord = NULL;
	selectedRecordBytes = 0;
	if (recordSearch.selectedRecordAddress != 0) {
		if (!SlipTrackWorld_DosAddressToOffset(recordSearch.selectedRecordAddress, trdBaseAddress, trackDataSize,
		                                       &selectedRecordOffset)) {
			return false;
		}
		selectedRecord = trdBase + (size_t)selectedRecordOffset;
		selectedRecordBytes = trackDataSize - (size_t)selectedRecordOffset;
	}
	result->selectedRecord = selectedRecord;
	result->callTrackWorldObjectSelect = true;
	if (!SlipTrackWorld_ObjectSelect(selectedRecord, selectedRecordBytes, componentBase, componentBaseBytes,
	                                 &objectSelect)) {
		return false;
	}
	result->noSelectedRecord = objectSelect.noSelectedRecord;
	result->noRelatedRecords = objectSelect.noRelatedRecords;
	result->componentFlags = objectSelect.componentFlags;
	if (heads.objectListWriteCursor < objectList ||
	    (size_t)(heads.objectListWriteCursor - objectList) > objectListBytes) {
		return false;
	}
	objectListCursorAddress = listBaseAddress + (uint32_t)(heads.objectListWriteCursor - objectList);
	if (objectSelect.noSelectedRecord) {
		result->storedDefaultTraversalGate = objectSelect.defaultTraversalGate;

		if (traversalContext != NULL && traversalContext->defaultTraversalGate != NULL) {
			*traversalContext->defaultTraversalGate = result->storedDefaultTraversalGate;
		}
	}
	if (objectSelect.branch == SLIP_TRACK_WORLD_OBJECT_SELECT_BRANCH_SELECTED) {
		result->renderContextCount = objectSelect.renderContextCount;
		result->primaryLeft = objectSelect.primaryLeft;
		result->primaryTop = objectSelect.primaryTop;
		result->primaryRight = objectSelect.primaryRight;
		result->primaryBottom = objectSelect.primaryBottom;
		result->componentMask = objectSelect.componentMask;
		if (traversalContext != 0) {
			if (traversalContext->defaultTraversalGate != 0) {
				*traversalContext->defaultTraversalGate = result->storedDefaultTraversalGate;
			}
			if (traversalContext->mask != 0) {
				*traversalContext->mask = result->componentMask;
			}
			if (traversalContext->renderContextCount != 0) {
				*traversalContext->renderContextCount = result->renderContextCount;
			}
			if (traversalContext->primaryLeft != 0) {
				*traversalContext->primaryLeft = result->primaryLeft;
			}
			if (traversalContext->primaryTop != 0) {
				*traversalContext->primaryTop = result->primaryTop;
			}
			if (traversalContext->primaryRight != 0) {
				*traversalContext->primaryRight = result->primaryRight;
			}
			if (traversalContext->primaryBottom != 0) {
				*traversalContext->primaryBottom = result->primaryBottom;
			}
		}
		if (!SlipTrackWorld_LoadClipRegisters(clipMinX, clipMinY, clipMaxX, clipMaxY, &loadClip)) {
			return false;
		}
		result->calledObjectDraw = true;
		if (!SlipTrackWorld_ObjectDraw(
		        heads.objectListWriteCursor, objectList, objectListBytes, listBaseAddress, objectListCursorAddress,
		        objectSelect.selectedRecord, selectedRecordBytes, recordSearch.selectedRecordAddress, loadClip.clipMinX,
		        loadClip.clipMinY, loadClip.clipMaxX, loadClip.clipMaxY, viewportMinX, viewportMinY, viewportMaxX,
		        viewportMaxY, previousRecordCallback, previousTraversalCallback, result->componentMask,
		        savedMaximumDepth, cameraWorldX, cameraWorldY, cameraWorldZ, viewMatrix, matrixBytes,
		        drawStateIndexBefore, trkBase, trkSize, traversalContext, &objectDraw)) {
			return false;
		}
		if (traversalContext != 0) {
			if (traversalContext->renderContextCount != 0) {
				result->renderContextCount = *traversalContext->renderContextCount;
			}
			if (traversalContext->primaryLeft != 0) {
				result->primaryLeft = *traversalContext->primaryLeft;
			}
			if (traversalContext->primaryTop != 0) {
				result->primaryTop = *traversalContext->primaryTop;
			}
			if (traversalContext->primaryRight != 0) {
				result->primaryRight = *traversalContext->primaryRight;
			}
			if (traversalContext->primaryBottom != 0) {
				result->primaryBottom = *traversalContext->primaryBottom;
			}
		}
		result->calledTraversalEntry = objectDraw.callTrackWorldTraversalEntry;
		result->traversalEntry = objectDraw.traversalEntry;
	}

	return true;
}

bool SlipTrackWorld_ObjectSelect(const uint8_t *searchedRecord, size_t recordBytesRemaining,
                                 const uint8_t *componentBase, size_t componentBaseBytes,
                                 SlipTrackWorldObjectSelect *result) {
	uint16_t firstRelatedOffset;
	uint16_t componentOffset;
	const uint8_t *component;

	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldObjectSelect){0,
	                                       true,
	                                       searchedRecord,
	                                       true,
	                                       searchedRecord,
	                                       searchedRecord == 0,
	                                       0,
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
	                                       0,
	                                       0,
	                                       0,
	                                       0,
	                                       0,
	                                       SLIP_TRACK_WORLD_OBJECT_SELECT_BRANCH_SKIPPED};
	if (searchedRecord == 0) {
		result->defaultTraversalGate = 0;
		return true;
	}
	if (recordBytesRemaining < SLIP_TRC_PRIMITIVE_FIRST_INDEX_END) {
		return false;
	}
	firstRelatedOffset = SlipBytes_ReadLE16(searchedRecord + SLIP_TRD_SECTION_FIRST_EXIT_OFFSET);
	result->firstRelatedOffset = firstRelatedOffset;
	firstRelatedOffset =
	    (uint16_t)(firstRelatedOffset | SlipBytes_ReadLE16(searchedRecord + SLIP_TRD_SECTION_SECOND_EXIT_OFFSET));
	result->firstTwoRelatedOffsets = firstRelatedOffset;
	firstRelatedOffset =
	    (uint16_t)(firstRelatedOffset | SlipBytes_ReadLE16(searchedRecord + SLIP_TRD_SECTION_THIRD_EXIT_OFFSET));
	result->relatedOffsets = firstRelatedOffset;
	if (firstRelatedOffset == 0) {
		result->noRelatedRecords = true;
		return true;
	}
	if (componentBase == 0) {
		return false;
	}
	componentOffset = SlipBytes_ReadLE16(searchedRecord + SLIP_TRD_SECTION_COMPONENT_OFFSET);
	if ((size_t)componentOffset > componentBaseBytes ||
	    componentBaseBytes - (size_t)componentOffset < SLIP_TRC_COMPONENT_FLAGS_END) {
		return false;
	}
	component = componentBase + componentOffset;
	result->renderContextCount = 0;
	result->primaryLeft = INT16_MAX;
	result->primaryTop = INT16_MAX;
	result->primaryRight = SLIP_DRAW3D_ACTIVE_BOUNDS_MAXIMUM_INITIAL;
	result->primaryBottom = SLIP_DRAW3D_ACTIVE_BOUNDS_MAXIMUM_INITIAL;
	result->selectedRecord = searchedRecord;
	result->componentOffset = componentOffset;
	result->componentRecord = component;
	result->componentFlags = SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_FLAGS_OFFSET);
	result->componentMask = (uint16_t)(result->componentFlags & SLIP_TRC_COMPONENT_RANGE_MASK);
	result->rangeMode = 0;
	result->rangeFlag = 0;
	result->processedComponentCount = 0;
	result->branch = SLIP_TRACK_WORLD_OBJECT_SELECT_BRANCH_SELECTED;
	return true;
}

static int32_t SlipTrackWorld_I16Scaled64(const uint8_t *p) {
	return (int32_t)(int16_t)SlipBytes_ReadLE16(p) * (1 << SLIP_TRC_POINT_COORDINATE_SHIFT);
}

bool SlipTrackWorld_ComponentBoundsGate(const uint8_t *componentBase, size_t componentBaseBytes,
                                        uint16_t componentOffset, int32_t localX, int32_t localY, int32_t localZ,
                                        SlipTrackWorldComponentBounds *result) {
	const uint8_t *component;
	uint16_t childListOffset;

	if (componentBase == 0 || result == 0) {
		return false;
	}
	if ((size_t)componentOffset + SLIP_TRC_COMPONENT_BOUNDS_END > componentBaseBytes) {
		return false;
	}
	component = componentBase + componentOffset;
	childListOffset = SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_PRIMITIVE_LIST_OFFSET);
	*result = (SlipTrackWorldComponentBounds){.componentOffsetWord = componentOffset,
	                                          .componentOffset = componentOffset,
	                                          .childListOffset = childListOffset,
	                                          .localX = localX,
	                                          .localY = localY,
	                                          .localZ = localZ,
	                                          .rejected = true,
	                                          .branch = SLIP_TRACK_WORLD_COMPONENT_BOUNDS_BRANCH_NO_CHILD_LIST};
	if (childListOffset == 0) {
		return true;
	}
	result->minX = SlipTrackWorld_I16Scaled64(component + SLIP_TRC_COMPONENT_MINIMUM_X_OFFSET);
	if (localX < result->minX) {
		result->branch = SLIP_TRACK_WORLD_COMPONENT_BOUNDS_BRANCH_X_BELOW;
		return true;
	}
	result->maxX = SlipTrackWorld_I16Scaled64(component + SLIP_TRC_COMPONENT_MAXIMUM_X_OFFSET);
	if (localX > result->maxX) {
		result->branch = SLIP_TRACK_WORLD_COMPONENT_BOUNDS_BRANCH_X_ABOVE;
		return true;
	}
	result->minY = SlipTrackWorld_I16Scaled64(component + SLIP_TRC_COMPONENT_MINIMUM_Y_OFFSET);
	if (localY < result->minY) {
		result->branch = SLIP_TRACK_WORLD_COMPONENT_BOUNDS_BRANCH_Y_BELOW;
		return true;
	}
	result->maxY = SlipTrackWorld_I16Scaled64(component + SLIP_TRC_COMPONENT_MAXIMUM_Y_OFFSET);
	if (localY > result->maxY) {
		result->branch = SLIP_TRACK_WORLD_COMPONENT_BOUNDS_BRANCH_Y_ABOVE;
		return true;
	}
	result->minZ = SlipTrackWorld_I16Scaled64(component + SLIP_TRC_COMPONENT_MINIMUM_Z_OFFSET);
	if (localZ < result->minZ) {
		result->branch = SLIP_TRACK_WORLD_COMPONENT_BOUNDS_BRANCH_Z_BELOW;
		return true;
	}
	result->maxZ = SlipTrackWorld_I16Scaled64(component + SLIP_TRC_COMPONENT_MAXIMUM_Z_OFFSET);
	if (localZ > result->maxZ) {
		result->branch = SLIP_TRACK_WORLD_COMPONENT_BOUNDS_BRANCH_Z_ABOVE;
		return true;
	}
	if ((size_t)childListOffset + SLIP_TRC_TABLE_COUNT_BYTES > componentBaseBytes) {
		return false;
	}
	result->storedLocalX = (uint32_t)localX;
	result->storedLocalY = (uint32_t)localY;
	result->storedLocalZ = (uint32_t)localZ;
	result->childListCount = SlipBytes_ReadLE16(componentBase + childListOffset);
	result->firstChildRecordOffset = (size_t)childListOffset + SLIP_TRC_TABLE_COUNT_BYTES;
	result->rejected = false;
	result->branch = SLIP_TRACK_WORLD_COMPONENT_BOUNDS_BRANCH_CHILD_LIST;
	return true;
}

bool SlipTrackWorld_ComponentChildRangeScan(const uint8_t *componentBase, size_t componentBaseBytes,
                                            uint16_t componentOffset, size_t firstChildRecordOffset,
                                            uint16_t childRecordCount, int32_t storedLocalX, int32_t storedLocalY,
                                            int32_t storedLocalZ, SlipTrackWorldComponentChildRangeVisit *visits,
                                            size_t visitCapacity, SlipTrackWorldComponentChildRangeScan *result) {
	const uint8_t *componentRecord;
	size_t childRecordOffset;
	uint32_t pointXOrRangeDistance;
	uint32_t pointY;
	uint16_t remainingCount;
	uint16_t recordIndex;

	if (componentBase == 0 || result == 0) {
		return false;
	}
	if ((size_t)componentOffset + SLIP_TRC_COMPONENT_POINT_LIST_OFFSET + sizeof(uint16_t) > componentBaseBytes) {
		return false;
	}
	*result = (SlipTrackWorldComponentChildRangeScan){childRecordCount,
	                                                  firstChildRecordOffset,
	                                                  0,
	                                                  false,
	                                                  false,
	                                                  SLIP_TRACK_WORLD_COMPONENT_CHILD_RANGE_BRANCH_ALL_PASSED};
	componentRecord = componentBase + componentOffset;
	childRecordOffset = firstChildRecordOffset;
	pointXOrRangeDistance = (uint32_t)storedLocalX;
	pointY = (uint32_t)storedLocalY;
	remainingCount = childRecordCount;
	recordIndex = 0;
	while (remainingCount != 0) {
		SlipTrackWorldComponentChildRangeVisit visit;
		const uint8_t *childRecord;
		size_t strideBytes;

		if (childRecordOffset + SLIP_TRC_PRIMITIVE_FLAGS_BYTE_END > componentBaseBytes) {
			return false;
		}
		childRecord = componentBase + childRecordOffset;
		memset(&visit, 0, sizeof(visit));
		visit.recordIndex = recordIndex;
		visit.childRecordOffset = childRecordOffset;
		visit.recordFlags = childRecord[SLIP_TRC_PRIMITIVE_FLAGS_OFFSET];
		visit.skipRangeTest = (childRecord[SLIP_TRC_PRIMITIVE_FLAGS_OFFSET] & SLIP_TRC_PRIMITIVE_TRENCH) != 0;
		if (!visit.skipRangeTest) {
			SlipTrackWorldPointLookup pointLookup;
			SlipTrackWorldRangePlane rangePlane;
			uint32_t deltaX;
			uint32_t deltaY;
			uint32_t deltaZ;

			if (childRecordOffset + SLIP_TRC_PRIMITIVE_FIRST_INDEX_END > componentBaseBytes) {
				return false;
			}
			visit.pointIndex = SlipBytes_ReadLE16(childRecord + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET);
			visit.callTrackWorldPointLookup = true;
			if (!SlipTrackWorld_PointLookup(componentRecord, componentBase, componentBaseBytes, visit.pointIndex,
			                                pointXOrRangeDistance, pointY, remainingCount, &pointLookup)) {
				return false;
			}
			visit.pointLookup = pointLookup;
			pointXOrRangeDistance = pointLookup.pointXOrInput;
			pointY = pointLookup.pointYOrInput;
			visit.normalX = SlipBytes_ReadLE16(childRecord + SLIP_TRC_PRIMITIVE_NORMAL_X_OFFSET);
			visit.normalY = SlipBytes_ReadLE16(childRecord + SLIP_TRC_PRIMITIVE_NORMAL_Y_OFFSET);
			visit.normalZ = SlipBytes_ReadLE16(childRecord + SLIP_TRC_PRIMITIVE_NORMAL_Z_OFFSET);
			visit.callTrackWorldStoreRangePlane = true;
			if (!SlipTrackWorld_StoreRangePlane(pointXOrRangeDistance, pointY, pointLookup.pointZOrCountMergedWithInput,
			                                    visit.normalX, visit.normalY, visit.normalZ, &rangePlane)) {
				return false;
			}
			visit.rangePlane = rangePlane;
			deltaX = (uint32_t)storedLocalX - (uint32_t)rangePlane.origin.x;
			deltaY = (uint32_t)storedLocalY - (uint32_t)rangePlane.origin.y;
			deltaZ = (uint32_t)storedLocalZ - (uint32_t)rangePlane.origin.z;
			visit.rangeDelta = (SlipView3DVec32){(int32_t)deltaX, (int32_t)deltaY, (int32_t)deltaZ};
			visit.rangeDotRounded =
			    SlipTrackWorld_RoundedDotProductShift14(deltaX, deltaY, deltaZ, (uint32_t)rangePlane.normal.x,
			                                            (uint32_t)rangePlane.normal.y, (uint32_t)rangePlane.normal.z);

			pointXOrRangeDistance = visit.rangeDotRounded;
			pointY = deltaY;
			visit.depthRejected = (int32_t)visit.rangeDotRounded < SLIP_TRACK_COMPONENT_RANGE_MINIMUM_DEPTH;
		}
		visit.strideWord = SlipBytes_ReadLE16(childRecord);
		if ((visit.strideWord & SLIP_TRC_PRIMITIVE_TEXTURED) == 0) {
			strideBytes = (size_t)visit.strideWord * SLIP_TRC_VERTEX_INDEX_BYTES + SLIP_TRC_PRIMITIVE_HEADER_BYTES;
		} else {
			strideBytes =
			    (size_t)(visit.strideWord & SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK) * SLIP_TRC_TEXTURED_VERTEX_BYTES +
			    SLIP_TRC_PRIMITIVE_HEADER_BYTES;
		}
		if (childRecordOffset > SIZE_MAX - strideBytes) {
			return false;
		}
		visit.nextChildRecordOffset = childRecordOffset + strideBytes;
		if (visits != 0 && result->visitsStored < visitCapacity) {
			visits[result->visitsStored] = visit;
		} else if (visits != 0) {
			result->hitVisitCapacity = true;
		}
		++result->visitsStored;
		if (visit.depthRejected) {
			result->carryOut = true;
			result->branch = SLIP_TRACK_WORLD_COMPONENT_CHILD_RANGE_BRANCH_DEPTH_REJECT;
			return true;
		}
		childRecordOffset = visit.nextChildRecordOffset;
		--remainingCount;
		++recordIndex;
	}
	return true;
}

bool SlipTrackWorld_RecordComponentTest(const uint8_t *trdBase, size_t trdBytes, const uint8_t *componentBase,
                                        size_t componentBaseBytes, uint32_t trdBaseAddress, uint32_t recordAddress,
                                        int32_t objectX, int32_t objectY, int32_t objectZ,
                                        SlipTrackWorldRecordComponentTest *result) {
	size_t recordOffset;
	const uint8_t *record;

	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldRecordComponentTest){.recordAddress = recordAddress, .carryOut = true};
	if (trdBase == 0 || recordAddress < trdBaseAddress) {
		return false;
	}
	recordOffset = (size_t)(recordAddress - trdBaseAddress);
	result->recordOffset = recordOffset;
	if (recordOffset + SLIP_TRD_SECTION_ORIGIN_END > trdBytes) {
		return false;
	}
	record = trdBase + recordOffset;
	result->localX = (int32_t)((uint32_t)objectX - SlipBytes_ReadLE32(record + SLIP_TRD_SECTION_ORIGIN_X_OFFSET));
	result->localY = (int32_t)((uint32_t)objectY - SlipBytes_ReadLE32(record + SLIP_TRD_SECTION_ORIGIN_Y_OFFSET));
	result->localZ = (int32_t)((uint32_t)objectZ - SlipBytes_ReadLE32(record + SLIP_TRD_SECTION_ORIGIN_Z_OFFSET));
	result->componentOffset = SlipBytes_ReadLE16(record + SLIP_TRD_SECTION_COMPONENT_OFFSET);
	result->callTrackWorldComponentBoundsGate = true;
	if (!SlipTrackWorld_ComponentBoundsGate(componentBase, componentBaseBytes, result->componentOffset, result->localX,
	                                        result->localY, result->localZ, &result->bounds)) {
		return false;
	}
	if (result->bounds.branch != SLIP_TRACK_WORLD_COMPONENT_BOUNDS_BRANCH_CHILD_LIST) {
		return true;
	}
	result->callTrackWorldComponentChildRangeScan = true;
	if (!SlipTrackWorld_ComponentChildRangeScan(componentBase, componentBaseBytes, result->componentOffset,
	                                            result->bounds.firstChildRecordOffset, result->bounds.childListCount,
	                                            result->localX, result->localY, result->localZ, 0, 0,
	                                            &result->childRangeScan)) {
		return false;
	}
	result->carryOut = result->childRangeScan.carryOut;
	return true;
}

bool SlipTrackWorld_CellRecordBoundsScan(const uint8_t *trdBase, size_t trdBytes, const uint8_t *componentBase,
                                         size_t componentBaseBytes, uint32_t trdBaseAddress,
                                         uint32_t cellDescriptorAddress, int32_t objectX, int32_t objectY,
                                         int32_t objectZ, SlipTrackWorldCellRecordBoundsVisit *visits,
                                         size_t visitCapacity, SlipTrackWorldCellRecordBoundsScan *result) {
	size_t cellDescriptorOffset;
	uint16_t recordListOffset;
	uint16_t recordCount;
	size_t recordOffset;
	uint16_t i;

	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldCellRecordBoundsScan){.cellDescriptorAddress = cellDescriptorAddress,
	                                               .branch = SLIP_TRACK_WORLD_CELL_RECORD_BOUNDS_SCAN_BRANCH_NO_CELL};
	if (cellDescriptorAddress == 0) {
		return true;
	}
	if (trdBase == 0 || cellDescriptorAddress < trdBaseAddress) {
		return false;
	}
	cellDescriptorOffset = (size_t)(cellDescriptorAddress - trdBaseAddress);
	result->cellDescriptorOffset = cellDescriptorOffset;
	if (cellDescriptorOffset + SLIP_TRACK_LINKED_HEADER_BYTES > trdBytes) {
		return false;
	}
	recordListOffset = SlipBytes_ReadLE16(trdBase + cellDescriptorOffset + SLIP_TRACK_LINKED_OFFSET);
	result->recordListOffset = recordListOffset;
	result->branch = SLIP_TRACK_WORLD_CELL_RECORD_BOUNDS_SCAN_BRANCH_NO_LIST;
	if (recordListOffset == 0) {
		return true;
	}
	if ((size_t)recordListOffset + SLIP_TRD_TABLE_COUNT_BYTES > trdBytes) {
		return false;
	}
	recordCount = SlipBytes_ReadLE16(trdBase + recordListOffset);
	recordOffset = (size_t)recordListOffset + SLIP_TRD_TABLE_COUNT_BYTES;
	result->recordCount = recordCount;
	result->firstRecordOffset = recordOffset;
	result->branch = SLIP_TRACK_WORLD_CELL_RECORD_BOUNDS_SCAN_BRANCH_EXHAUSTED;
	if ((size_t)recordCount > (SIZE_MAX - recordOffset) / SLIP_TRD_SECTION_BYTES ||
	    recordOffset + (size_t)recordCount * SLIP_TRD_SECTION_BYTES > trdBytes) {
		return false;
	}
	for (i = 0; i < recordCount; ++i) {
		SlipTrackWorldCellRecordBoundsVisit visit;
		const uint8_t *const record = trdBase + recordOffset;
		uint16_t orChildOffsets;

		memset(&visit, 0, sizeof(visit));
		visit.recordIndex = i;
		visit.recordOffset = recordOffset;
		visit.recordAddress = trdBaseAddress + (uint32_t)recordOffset;
		visit.localX = (int32_t)((uint32_t)objectX - SlipBytes_ReadLE32(record + SLIP_TRD_SECTION_ORIGIN_X_OFFSET));
		visit.localY = (int32_t)((uint32_t)objectY - SlipBytes_ReadLE32(record + SLIP_TRD_SECTION_ORIGIN_Y_OFFSET));
		visit.localZ = (int32_t)((uint32_t)objectZ - SlipBytes_ReadLE32(record + SLIP_TRD_SECTION_ORIGIN_Z_OFFSET));
		orChildOffsets = SlipBytes_ReadLE16(record + SLIP_TRD_SECTION_FIRST_EXIT_OFFSET);
		orChildOffsets = (uint16_t)(orChildOffsets | SlipBytes_ReadLE16(record + SLIP_TRD_SECTION_SECOND_EXIT_OFFSET));
		orChildOffsets = (uint16_t)(orChildOffsets | SlipBytes_ReadLE16(record + SLIP_TRD_SECTION_THIRD_EXIT_OFFSET));
		visit.orChildOffsets = orChildOffsets;
		if (orChildOffsets != 0) {
			visit.componentOffset = SlipBytes_ReadLE16(record + SLIP_TRD_SECTION_COMPONENT_OFFSET);
			if (!SlipTrackWorld_RecordComponentTest(trdBase, trdBytes, componentBase, componentBaseBytes,
			                                        trdBaseAddress, visit.recordAddress, objectX, objectY, objectZ,
			                                        &visit.componentTest)) {
				return false;
			}
			visit.callTrackWorldComponentBoundsGate = visit.componentTest.callTrackWorldComponentBoundsGate;
			visit.componentPassed = !visit.componentTest.carryOut;
		}
		if (visits != 0 && result->visitsStored < visitCapacity) {
			visits[result->visitsStored] = visit;
		} else if (visits != 0) {
			result->hitVisitCapacity = true;
		}
		++result->visitsStored;
		if (visit.componentPassed) {
			result->selectedRecordOffset = recordOffset;
			result->selectedRecordAddress = trdBaseAddress + (uint32_t)recordOffset;
			result->branch = SLIP_TRACK_WORLD_CELL_RECORD_BOUNDS_SCAN_BRANCH_COMPONENT_PASS;
			return true;
		}
		recordOffset += SLIP_TRD_SECTION_BYTES;
	}
	return true;
}

bool SlipTrackWorld_RecordSearch(const uint8_t *trdBase, size_t trdBytes, const uint8_t *componentBase,
                                 size_t componentBaseBytes, const uint8_t *table, size_t tableBytes,
                                 uint32_t trdBaseAddress, uint32_t initialRecordAddress, int32_t objectX,
                                 int32_t objectY, int32_t objectZ, SlipTrackWorldRecordSearch *result) {
	uint32_t currentRecordAddress;
	size_t currentRecordOffset;
	SlipTrackWorldRecordComponentTest recordTest;

	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldRecordSearch){.searchX = (uint32_t)objectX,
	                                       .searchY = (uint32_t)objectY,
	                                       .searchZ = (uint32_t)objectZ,
	                                       .initialRecordAddress = initialRecordAddress,
	                                       .carryOut = true,
	                                       .branch = SLIP_TRACK_WORLD_RECORD_SEARCH_BRANCH_ZERO};
	if (initialRecordAddress != 0) {
		if (trdBase == 0 || initialRecordAddress < trdBaseAddress) {
			return false;
		}
		currentRecordAddress = initialRecordAddress;
		currentRecordOffset = (size_t)(currentRecordAddress - trdBaseAddress);
		if (currentRecordOffset + SLIP_TRD_SECTION_EXIT_LINKS_END > trdBytes) {
			return false;
		}
		result->storedInitialRecordAddress = currentRecordAddress;
		result->currentRecordAddress = currentRecordAddress;
		if (!SlipTrackWorld_RecordComponentTest(trdBase, trdBytes, componentBase, componentBaseBytes, trdBaseAddress,
		                                        currentRecordAddress, objectX, objectY, objectZ, &recordTest)) {
			return false;
		}
		result->currentRecordTest = recordTest;
		if (!recordTest.carryOut) {
			result->selectedRecordAddress = currentRecordAddress;
			result->carryOut = false;
			result->branch = SLIP_TRACK_WORLD_RECORD_SEARCH_BRANCH_EXISTING_RECORD;
			return true;
		}
		result->link04Offset = SlipBytes_ReadLE16(trdBase + currentRecordOffset + SLIP_TRD_SECTION_FIRST_EXIT_OFFSET);
		if (result->link04Offset != 0) {
			result->link04RecordAddress = trdBaseAddress + result->link04Offset;
			if (!SlipTrackWorld_RecordComponentTest(trdBase, trdBytes, componentBase, componentBaseBytes,
			                                        trdBaseAddress, result->link04RecordAddress, objectX, objectY,
			                                        objectZ, &recordTest)) {
				return false;
			}
			result->link04Test = recordTest;
			if (!recordTest.carryOut) {
				result->selectedRecordAddress = result->link04RecordAddress;
				result->carryOut = false;
				result->branch = SLIP_TRACK_WORLD_RECORD_SEARCH_BRANCH_LINK_04;
				return true;
			}
		}
		result->link08Offset = SlipBytes_ReadLE16(trdBase + currentRecordOffset + SLIP_TRD_SECTION_SECOND_EXIT_OFFSET);
		if (result->link08Offset != 0) {
			result->link08RecordAddress = trdBaseAddress + result->link08Offset;
			if (!SlipTrackWorld_RecordComponentTest(trdBase, trdBytes, componentBase, componentBaseBytes,
			                                        trdBaseAddress, result->link08RecordAddress, objectX, objectY,
			                                        objectZ, &recordTest)) {
				return false;
			}
			result->link08Test = recordTest;
			if (!recordTest.carryOut) {
				result->selectedRecordAddress = result->link08RecordAddress;
				result->carryOut = false;
				result->branch = SLIP_TRACK_WORLD_RECORD_SEARCH_BRANCH_LINK_08;
				return true;
			}
		}
		result->link0cOffset = SlipBytes_ReadLE16(trdBase + currentRecordOffset + SLIP_TRD_SECTION_THIRD_EXIT_OFFSET);
		if (result->link0cOffset != 0) {
			result->link0cRecordAddress = trdBaseAddress + result->link0cOffset;
			if (!SlipTrackWorld_RecordComponentTest(trdBase, trdBytes, componentBase, componentBaseBytes,
			                                        trdBaseAddress, result->link0cRecordAddress, objectX, objectY,
			                                        objectZ, &recordTest)) {
				return false;
			}
			result->link0cTest = recordTest;
			if (!recordTest.carryOut) {
				result->selectedRecordAddress = result->link0cRecordAddress;
				result->carryOut = false;
				result->branch = SLIP_TRACK_WORLD_RECORD_SEARCH_BRANCH_LINK_0C;
				return true;
			}
		}
	}
	result->cellX = (int32_t)SlipTrackWorld_SignedShiftRight20((uint32_t)objectX);
	result->cellY = (int32_t)SlipTrackWorld_SignedShiftRight20((uint32_t)objectY);
	result->cellZ = (int32_t)SlipTrackWorld_SignedShiftRight20((uint32_t)objectZ);
	result->callTrackWorldTableLookup = true;
	if (!SlipTrackWorld_TableLookup(table, tableBytes, (uint16_t)result->cellX, (uint16_t)result->cellY,
	                                (uint16_t)result->cellZ, &result->tableLookup)) {
		return false;
	}
	if (result->tableLookup.recordAddress == 0) {
		return true;
	}
	result->callTrackWorldCellRecordBoundsScan = true;
	if (!SlipTrackWorld_CellRecordBoundsScan(trdBase, trdBytes, componentBase, componentBaseBytes, trdBaseAddress,
	                                         result->tableLookup.recordAddress, objectX, objectY, objectZ, 0, 0,
	                                         &result->cellRecordScan)) {
		return false;
	}
	if (result->cellRecordScan.selectedRecordAddress == 0) {
		return true;
	}
	result->selectedRecordAddress = result->cellRecordScan.selectedRecordAddress;
	result->carryOut = false;
	result->branch = SLIP_TRACK_WORLD_RECORD_SEARCH_BRANCH_CELL_RECORD;
	return true;
}

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
                               SlipTrackWorldTraversalContext *traversalContext, SlipTrackWorldObjectDraw *result) {
	SlipTrackWorldRecordCallback installedRecordCallback;
	uint32_t drawStateIndexBefore;
	SlipTrackWorldTraversalEntry traversalEntry;
	SlipTrackWorldObjectListEntryResult objectListEntry;
	SlipTrackWorldObjectTransform objectTransform;
	SlipView3DMatrix matrix;
	SlipView3DVec32 relativeObject;
	SlipView3DVec32 transformedObject;
	size_t i;

	if (result == 0) {
		return false;
	}

	if (traversalContext != NULL && traversalContext->projectState != NULL) {
		traversalContext->projectState->maxZ = INT32_MAX;
		traversalContext->maxZ = INT32_MAX;
		if (traversalContext->frustum != NULL)
			traversalContext->frustum->maxZ = INT32_MAX;
	}
	SlipDraw3D_SetMaximumDepth(INT32_MAX);
	memset(&traversalEntry, 0, sizeof(traversalEntry));
	memset(&objectListEntry, 0, sizeof(objectListEntry));
	memset(&objectTransform, 0, sizeof(objectTransform));
	if (!SlipTrackWorld_ObjectListEntry(objectList, objectListBytes, objectListBaseAddress, &objectListCursorAddress,
	                                    object, objectBytesRemaining, objectAddress, 0, 0, 0,
	                                    traversalContext != NULL ? traversalContext->useFullObjectViewport : 0,
	                                    savedClipMinX, savedClipMinY, savedClipMaxX, savedClipMaxY, viewportMinX,
	                                    viewportMinY, viewportMaxX, viewportMaxY, &objectListEntry)) {
		return false;
	}
	if (objectListEntry.branch != SLIP_TRACK_WORLD_OBJECT_LIST_ENTRY_BRANCH_CAPACITY_REACHED) {
		if (viewMatrix == 0 || matrixBytes < sizeof(matrix.m)) {
			return false;
		}
		for (i = 0; i < sizeof(matrix.m) / sizeof(matrix.m[0]); ++i) {
			matrix.m[i] = (int16_t)SlipBytes_ReadLE16(viewMatrix + i * sizeof(matrix.m[0]));
		}
		relativeObject = (SlipView3DVec32){
		    (int32_t)(uint32_t)(SlipBytes_ReadLE32(object + SLIP_TRD_SECTION_ORIGIN_X_OFFSET) - cameraWorldX),
		    (int32_t)(uint32_t)(SlipBytes_ReadLE32(object + SLIP_TRD_SECTION_ORIGIN_Y_OFFSET) - cameraWorldY),
		    (int32_t)(uint32_t)(SlipBytes_ReadLE32(object + SLIP_TRD_SECTION_ORIGIN_Z_OFFSET) - cameraWorldZ)};
		transformedObject = SlipView3D_TransformPositionByColumns(&matrix, relativeObject);
		if (!SlipTrackWorld_ObjectTransform(object, objectBytesRemaining, objectListEntry.entry,
		                                    objectListBytes - (size_t)(objectListEntry.entry - objectList),
		                                    cameraWorldX, cameraWorldY, cameraWorldZ, (uint32_t)transformedObject.x,
		                                    (uint32_t)transformedObject.y, (uint32_t)transformedObject.z,
		                                    &objectTransform)) {
			return false;
		}
		if (traversalContext != 0 && traversalContext->chunkProcessExecution != 0 &&
		    traversalContext->chunkProcessExecution->objectContinuation != 0 &&
		    !traversalContext->chunkProcessExecution->objectContinuation(
		        object, objectBytesRemaining, objectAddress, objectListEntry.counter, objectListEntry.entry,
		        objectListBytes - (size_t)(objectListEntry.entry - objectList),
		        (SlipView3DVec32){(int32_t)objectTransform.storedObjectWorldX,
		                          (int32_t)objectTransform.storedObjectWorldY,
		                          (int32_t)objectTransform.storedObjectWorldZ},
		        (SlipView3DVec32){(int32_t)objectTransform.storedViewX, (int32_t)objectTransform.storedViewY,
		                          (int32_t)objectTransform.storedViewZ},
		        traversalContext->chunkProcessExecution->objectContinuationUserData)) {
			return false;
		}
	}

	if (traversalContext != NULL && traversalContext->projectState != NULL) {
		const SlipDraw3DProjectState *const projection = traversalContext->projectState;
		savedClipMinX = (uint32_t)projection->minX;
		savedClipMinY = (uint32_t)projection->minY;
		savedClipMaxX = (uint32_t)projection->maxX;
		savedClipMaxY = (uint32_t)projection->maxY;
		if (traversalContext->storeClipBounds != NULL &&
		    !traversalContext->storeClipBounds(viewportMinX, viewportMinY, viewportMaxX, viewportMaxY,
		                                       traversalContext->storeClipBoundsUserData))
			return false;
	}
	installedRecordCallback = SLIP_TRACK_WORLD_RECORD_CALLBACK_DEFERRED_HEADER;
	if (mask == SLIP_TRACK_SPECIAL_RENDER_MODE) {
		installedRecordCallback = SLIP_TRACK_WORLD_RECORD_CALLBACK_DEFERRED_HEADER_ENTRY;
	}
	drawStateIndexBefore = SlipDraw3D_CurrentStateRecordIndex(drawStateIndex);
	if (traversalContext != 0) {
		if (traversalContext->traversalCallback != 0) {
			*traversalContext->traversalCallback = SLIP_TRACK_WORLD_TRAVERSAL_CALLBACK_DEFERRED_GATE;
		}
		if (traversalContext->recordCallback != 0) {
			*traversalContext->recordCallback = installedRecordCallback;
		}
		if (traversalContext->deferredEntryActive != 0) {
			*traversalContext->deferredEntryActive = 0;
		}
	}
	if (!SlipTrackWorld_TraversalEntry(trkBase, trkSize, traversalContext, &traversalEntry)) {
		return false;
	}
	if (traversalContext != 0) {
		if (traversalContext->recordCallback != 0) {
			*traversalContext->recordCallback = previousTraversalCallback;
		}
		if (traversalContext->traversalCallback != 0) {
			*traversalContext->traversalCallback = previousRecordCallback;
		}
	}

	SlipDraw3D_SetMaximumDepth(savedMaximumDepth);
	if (traversalContext != NULL && traversalContext->projectState != NULL) {
		traversalContext->projectState->maxZ = (int32_t)savedMaximumDepth;
		traversalContext->maxZ = (int32_t)savedMaximumDepth;
		if (traversalContext->frustum != NULL)
			traversalContext->frustum->maxZ = (int32_t)savedMaximumDepth;
		if (traversalContext->storeClipBounds != NULL &&
		    !traversalContext->storeClipBounds(savedClipMinX, savedClipMinY, savedClipMaxX, savedClipMaxY,
		                                       traversalContext->storeClipBoundsUserData))
			return false;
	}
	*result = (SlipTrackWorldObjectDraw){listCursor,
	                                     INT32_MAX,
	                                     true,
	                                     true,
	                                     true,
	                                     savedClipMinX,
	                                     savedClipMinY,
	                                     savedClipMaxX,
	                                     savedClipMaxY,
	                                     viewportMinX,
	                                     viewportMinY,
	                                     viewportMaxX,
	                                     viewportMaxY,
	                                     true,
	                                     previousRecordCallback,
	                                     previousTraversalCallback,
	                                     SLIP_TRACK_WORLD_TRAVERSAL_CALLBACK_DEFERRED_GATE,
	                                     installedRecordCallback,
	                                     mask,
	                                     0,
	                                     true,
	                                     traversalEntry,
	                                     previousTraversalCallback,
	                                     previousRecordCallback,
	                                     savedMaximumDepth,
	                                     true,
	                                     savedClipMaxY,
	                                     savedClipMaxX,
	                                     savedClipMinY,
	                                     savedClipMinX,
	                                     true,
	                                     true,
	                                     drawStateIndexBefore,
	                                     drawStateIndexBefore - 1u,
	                                     true,
	                                     true};
	return true;
}

bool SlipTrackWorld_StateSave(uint32_t clipMinX, uint32_t clipMinY, uint32_t clipMaxX, uint32_t clipMaxY,
                              SlipTrackWorldStateSave *result) {
	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldStateSave){true,     clipMinX, clipMinY, clipMaxX, clipMaxY, clipMinX, clipMinY, clipMaxX,
	                                    clipMaxY, clipMinX, clipMinY, clipMaxX, clipMaxY, 1u,       true};
	return true;
}

bool SlipTrackWorld_ObjectListEntry(uint8_t *objectList, size_t objectListBytes, uint32_t objectListBaseAddress,
                                    uint32_t *objectListCursorAddress, const uint8_t *currentObject,
                                    size_t objectBytesRemaining, uint32_t currentObjectAddress, uint32_t counter,
                                    uint32_t defaultTraversalGate, uint32_t rangeFlag, uint32_t useFullObjectViewport,
                                    uint32_t viewX, uint32_t viewY, uint32_t viewZ, uint32_t objectToken,
                                    uint32_t viewportMinX, uint32_t viewportMinY, uint32_t viewportMaxX,
                                    uint32_t viewportMaxY, SlipTrackWorldObjectListEntryResult *result) {
	uint32_t i;
	uint32_t count;
	uint8_t *entry;
	uint32_t cursorAddress;
	size_t cursorOffset;
	uint32_t minX;
	uint32_t minY;
	uint32_t maxX;
	uint32_t maxY;
	uint32_t useClipBounds;

	(void)objectBytesRemaining;
	if (objectList == 0 || objectListCursorAddress == 0 || result == 0 ||
	    objectListBytes < SLIP_TRACK_VISIBILITY_LIST_HEADER_BYTES) {
		return false;
	}
	uint32_t *const objectCount = (uint32_t *)(void *)objectList;
	count = *objectCount;
	*result = (SlipTrackWorldObjectListEntryResult){counter + 1u,
	                                                true,
	                                                currentObject,
	                                                currentObjectAddress,
	                                                count,
	                                                count == SLIP_TRACK_VISIBILITY_ENTRY_CAPACITY,
	                                                0,
	                                                false,
	                                                count,
	                                                0,
	                                                NULL,
	                                                0,
	                                                *objectListCursorAddress,
	                                                *objectListCursorAddress,
	                                                0,
	                                                0,
	                                                0,
	                                                0,
	                                                0,
	                                                defaultTraversalGate,
	                                                0,
	                                                false,
	                                                0,
	                                                false,
	                                                0,
	                                                0,
	                                                0,
	                                                0,
	                                                0,
	                                                SLIP_TRACK_WORLD_OBJECT_LIST_ENTRY_BRANCH_READY};
	if (count == SLIP_TRACK_VISIBILITY_ENTRY_CAPACITY) {
		result->branch = SLIP_TRACK_WORLD_OBJECT_LIST_ENTRY_BRANCH_CAPACITY_REACHED;
		return true;
	}
	if (count > SLIP_TRACK_VISIBILITY_ENTRY_CAPACITY ||
	    objectListBytes - SLIP_TRACK_VISIBILITY_LIST_HEADER_BYTES < (size_t)count * SLIP_TRACK_VISIBILITY_ENTRY_BYTES) {
		return false;
	}
	for (i = 0; i < count; ++i) {
		const size_t entryOffset =
		    SLIP_TRACK_VISIBILITY_LIST_HEADER_BYTES + (size_t)i * SLIP_TRACK_VISIBILITY_ENTRY_BYTES;
		result->scanIndex = i;
		const SlipTrackVisibilityEntry *const candidate =
		    (const SlipTrackVisibilityEntry *)(const void *)(objectList + entryOffset);
		if (candidate->recordAddress == currentObjectAddress) {
			result->foundExisting = true;
			result->entryIndex = i;
			result->entry = objectList + entryOffset;
			result->entryAddress = objectListBaseAddress + (uint32_t)entryOffset;
			return true;
		}
	}
	cursorAddress = *objectListCursorAddress;
	if (cursorAddress < objectListBaseAddress) {
		return false;
	}
	cursorOffset = (size_t)(cursorAddress - objectListBaseAddress);
	if (cursorOffset > objectListBytes || objectListBytes - cursorOffset < SLIP_TRACK_VISIBILITY_ENTRY_BYTES) {
		return false;
	}
	entry = objectList + cursorOffset;
	SlipTrackVisibilityEntry *const visibility = (SlipTrackVisibilityEntry *)(void *)entry;
	*objectCount = count + 1u;
	*objectListCursorAddress = cursorAddress + SLIP_TRACK_VISIBILITY_ENTRY_BYTES;
	result->countAfter = count + 1u;
	result->entryIndex = count;
	result->entry = entry;
	result->entryAddress = cursorAddress;
	result->listCursorAddressAfter = *objectListCursorAddress;
	result->advance = SLIP_TRACK_VISIBILITY_ENTRY_BYTES;
	visibility->recordAddress = currentObjectAddress;
	visibility->callbackFlag = 0;
	visibility->resetMaximumDepth = UINT32_MAX;
	result->entryObject = currentObject;
	result->entryObjectAddress = currentObjectAddress;
	result->callbackFlag = 0;
	result->resetMaximumDepth = UINT32_MAX;
	result->rangeFlag = rangeFlag;
	result->callTrackWorldLoadClipRegisters = true;
	minX = viewX;
	minY = viewY;
	maxX = viewZ;
	maxY = objectToken;
	useClipBounds = UINT32_MAX;
	if (defaultTraversalGate == 0 && rangeFlag != 0) {
		useClipBounds = 0;
	} else {
		result->useFullObjectViewport = useFullObjectViewport;
		if (useFullObjectViewport != 0) {
			result->useState = true;
			minX = viewportMinX;
			minY = viewportMinY;
			maxX = viewportMaxX;
			maxY = viewportMaxY;
		}
	}
	visibility->minX = minX;
	visibility->minY = minY;
	visibility->maxX = maxX;
	visibility->maxY = maxY;
	visibility->useClipBounds = useClipBounds;
	result->clipMinX = minX;
	result->clipMaxX = maxX;
	result->clipMinY = minY;
	result->clipMaxY = maxY;
	result->useClipBounds = useClipBounds;
	return true;
}

bool SlipTrackWorld_ObjectTransform(const uint8_t *object, size_t objectBytesRemaining, uint8_t *inputEntry,
                                    size_t entryBytesRemaining, uint32_t cameraWorldX, uint32_t cameraWorldY,
                                    uint32_t cameraWorldZ, uint32_t x, uint32_t y, uint32_t z,
                                    SlipTrackWorldObjectTransform *result) {
	uint32_t objectWorldX;
	uint32_t objectWorldY;
	uint32_t objectWorldZ;

	if (object == 0 || inputEntry == 0 || result == 0 || objectBytesRemaining < SLIP_TRD_SECTION_ORIGIN_END ||
	    entryBytesRemaining < SLIP_TRACK_VISIBILITY_POSITION_END) {
		return false;
	}
	objectWorldX = SlipBytes_ReadLE32(object + SLIP_TRD_SECTION_ORIGIN_X_OFFSET);
	objectWorldY = SlipBytes_ReadLE32(object + SLIP_TRD_SECTION_ORIGIN_Y_OFFSET);
	objectWorldZ = SlipBytes_ReadLE32(object + SLIP_TRD_SECTION_ORIGIN_Z_OFFSET);
	SlipTrackVisibilityEntry *const entry = (SlipTrackVisibilityEntry *)(void *)inputEntry;
	entry->viewPosition.x = (int32_t)x;
	entry->viewPosition.y = (int32_t)y;
	entry->viewPosition.z = (int32_t)z;
	*result = (SlipTrackWorldObjectTransform){objectWorldX,
	                                          objectWorldY,
	                                          objectWorldZ,
	                                          objectWorldX,
	                                          objectWorldY,
	                                          objectWorldZ,
	                                          objectWorldX - cameraWorldX,
	                                          objectWorldY - cameraWorldY,
	                                          objectWorldZ - cameraWorldZ,
	                                          true,
	                                          SLIP_TRACK_WORLD_CAMERA_MATRIX_TOKEN,
	                                          true,
	                                          x,
	                                          y,
	                                          z,
	                                          x,
	                                          y,
	                                          z,
	                                          x,
	                                          y,
	                                          z,
	                                          inputEntry};
	return true;
}

bool SlipTrackWorld_ComponentList(const uint8_t *currentObject, size_t objectBytesRemaining,
                                  const uint8_t *componentBase, size_t componentBaseBytes,
                                  SlipTrackWorldComponentList *result) {
	uint16_t componentOffset;
	const uint8_t *component;
	uint16_t childOffset;
	uint16_t listOffset;

	if (currentObject == 0 || componentBase == 0 || result == 0 ||
	    objectBytesRemaining < SLIP_TRD_SECTION_COMPONENT_END) {
		return false;
	}
	componentOffset = SlipBytes_ReadLE16(currentObject + SLIP_TRD_SECTION_COMPONENT_OFFSET);
	if ((size_t)componentOffset > componentBaseBytes ||
	    componentBaseBytes - (size_t)componentOffset < SLIP_TRC_COMPONENT_POINT_LIST_END) {
		return false;
	}
	component = componentBase + componentOffset;
	childOffset = SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_POINT_LIST_OFFSET);
	*result = (SlipTrackWorldComponentList){currentObject,
	                                        componentOffset,
	                                        component,
	                                        component,
	                                        childOffset,
	                                        childOffset == 0,
	                                        0,
	                                        false,
	                                        0,
	                                        0,
	                                        false,
	                                        SLIP_TRACK_WORLD_COMPONENT_LIST_BRANCH_EMPTY};
	if (childOffset == 0) {
		return true;
	}
	if ((size_t)childOffset > componentBaseBytes || componentBaseBytes - (size_t)childOffset < 1u ||
	    componentBaseBytes - (size_t)componentOffset < SLIP_TRC_COMPONENT_PRIMITIVE_LIST_END) {
		return false;
	}
	listOffset = SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_PRIMITIVE_LIST_OFFSET);
	if ((size_t)listOffset > componentBaseBytes) {
		return false;
	}
	result->childList = componentBase + childOffset;
	result->callTrackWorldChildListDispatch = true;
	result->listOffset = listOffset;
	result->primitiveList = componentBase + listOffset;
	result->savedPrimitiveListPointer = true;
	result->branch = SLIP_TRACK_WORLD_COMPONENT_LIST_BRANCH_READY;
	return true;
}

bool SlipTrackWorld_PrimitiveBounds(const uint8_t *list, size_t listBytesRemaining, uint32_t renderContextCount,
                                    uint32_t primaryLeft, uint32_t primaryTop, uint32_t primaryRight,
                                    uint32_t primaryBottom, const SlipTrackWorldPrimitiveBoundsCall *calls,
                                    size_t callCount, SlipTrackWorldPrimitiveBoundsVisit *visits, size_t visitCapacity,
                                    size_t *visitCount, SlipTrackWorldPrimitiveBounds *result) {
	uint16_t count;
	size_t recordOffsetAfterAdvance;
	size_t i;
	uint32_t currentRenderContextCount;
	uint32_t currentPrimaryLeft;
	uint32_t currentPrimaryTop;
	uint32_t currentPrimaryRight;
	uint32_t currentPrimaryBottom;

	if (visitCount != 0) {
		*visitCount = 0;
	}
	if (list == 0 || visits == 0 || result == 0 || listBytesRemaining < 2u) {
		return false;
	}
	count = SlipBytes_ReadLE16(list);
	if (count == 0 || visitCapacity < (size_t)count) {
		return false;
	}
	recordOffsetAfterAdvance = 2u;
	currentRenderContextCount = renderContextCount;
	currentPrimaryLeft = primaryLeft;
	currentPrimaryTop = primaryTop;
	currentPrimaryRight = primaryRight;
	currentPrimaryBottom = primaryBottom;
	for (i = 0; i < (size_t)count; ++i) {
		const uint8_t *record;
		uint16_t remainingCountBefore;
		uint16_t descriptor;
		uint32_t advance;
		SlipTrackWorldPrimitiveBoundsVisit *visit;

		if (recordOffsetAfterAdvance > listBytesRemaining ||
		    listBytesRemaining - recordOffsetAfterAdvance < SLIP_TRC_PRIMITIVE_FLAGS_BYTE_END) {
			return false;
		}
		record = list + recordOffsetAfterAdvance;
		remainingCountBefore = (uint16_t)(count - (uint16_t)i);
		visit = visits + i;
		*visit = (SlipTrackWorldPrimitiveBoundsVisit){0};
		visit->primitiveRecord = record;
		visit->remainingCountBefore = remainingCountBefore;
		visit->recordFlags = record[SLIP_TRC_PRIMITIVE_FLAGS_OFFSET];
		visit->boundsBit = (uint8_t)(visit->recordFlags & SLIP_TRC_PRIMITIVE_CLIP_BOUNDS);
		visit->skippedBounds = visit->boundsBit == 0;
		if (visit->boundsBit != 0) {
			const SlipTrackWorldPrimitiveBoundsCall *call;

			if (listBytesRemaining - recordOffsetAfterAdvance < SLIP_TRC_PRIMITIVE_FIRST_INDEX_END || calls == 0 ||
			    i >= callCount) {
				return false;
			}
			call = calls + i;
			visit->descriptorBeforeMask = SlipBytes_ReadLE16(record);
			visit->vertexCount = (uint16_t)(visit->descriptorBeforeMask & SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK);
			visit->vertexIndexList = record + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET;
			visit->pointIndex = SlipBytes_ReadLE16(visit->vertexIndexList);
			visit->normalX = SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_NORMAL_X_OFFSET);
			visit->normalY = SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_NORMAL_Y_OFFSET);
			visit->normalZ = SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_NORMAL_Z_OFFSET);
			visit->calledPlaneClassify = true;
			visit->planeRejected = call->planeRejected;
			if (!visit->planeRejected) {
				visit->callDraw3DPrimitivePath = true;
				visit->primitiveRejected = call->primitiveRejected;
				if (!visit->primitiveRejected) {
					visit->minX = call->minX;
					visit->minY = call->minY;
					visit->maxX = call->maxX;
					visit->maxY = call->maxY;
					currentRenderContextCount = 1u;
					if ((int32_t)call->minX < (int32_t)currentPrimaryLeft) {
						currentPrimaryLeft = call->minX;
					}
					if ((int32_t)call->maxX > (int32_t)currentPrimaryRight) {
						currentPrimaryRight = call->maxX;
					}
					if ((int32_t)call->minY < (int32_t)currentPrimaryTop) {
						currentPrimaryTop = call->minY;
					}
					if ((int32_t)call->maxY > (int32_t)currentPrimaryBottom) {
						currentPrimaryBottom = call->maxY;
					}
				}
			}
		}
		visit->renderContextCount = currentRenderContextCount;
		visit->primaryLeft = currentPrimaryLeft;
		visit->primaryTop = currentPrimaryTop;
		visit->primaryRight = currentPrimaryRight;
		visit->primaryBottom = currentPrimaryBottom;
		descriptor = SlipBytes_ReadLE16(record);
		visit->descriptor = descriptor;
		visit->highBit = (descriptor & SLIP_TRC_PRIMITIVE_TEXTURED) != 0;
		if (visit->highBit) {
			advance = (uint32_t)(descriptor & SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK) * SLIP_TRC_TEXTURED_VERTEX_BYTES +
			          SLIP_TRC_PRIMITIVE_HEADER_BYTES;
		} else {
			advance = (uint32_t)descriptor * SLIP_TRC_VERTEX_INDEX_BYTES + SLIP_TRC_PRIMITIVE_HEADER_BYTES;
		}
		visit->advance = advance;
		recordOffsetAfterAdvance += (size_t)advance;
		visit->recordOffsetAfterAdvance = recordOffsetAfterAdvance;
		visit->remainingCountAfter = (uint16_t)(remainingCountBefore - 1u);
		if (visitCount != 0) {
			*visitCount = i + 1u;
		}
	}
	*result = (SlipTrackWorldPrimitiveBounds){count,
	                                          list + 2u,
	                                          recordOffsetAfterAdvance,
	                                          currentRenderContextCount,
	                                          currentPrimaryLeft,
	                                          currentPrimaryTop,
	                                          currentPrimaryRight,
	                                          currentPrimaryBottom,
	                                          true};
	return true;
}

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
    const SlipView3DMatrix *matrix, SlipTrackWorldPrimitiveBoundsEvaluated *result) {
	uint16_t count;
	size_t recordOffsetAfterAdvance;
	size_t i;
	SlipTrackWorldPrimitiveBounds bounds;

	if (visitCount != 0) {
		*visitCount = 0;
	}
	if (list == 0 || calls == 0 || evaluatedVisits == 0 || result == 0 || listBytesRemaining < 2u) {
		return false;
	}
	count = SlipBytes_ReadLE16(list);
	if (count == 0 || callCapacity < (size_t)count || evaluatedVisitCapacity < (size_t)count) {
		return false;
	}
	memset(calls, 0, sizeof(calls[0]) * (size_t)count);
	recordOffsetAfterAdvance = 2u;
	for (i = 0; i < (size_t)count; ++i) {
		const uint8_t *record;
		uint16_t descriptor;
		uint32_t advance;
		SlipTrackWorldPrimitiveBoundsEvaluatedVisit *evaluated;

		if (recordOffsetAfterAdvance > listBytesRemaining ||
		    listBytesRemaining - recordOffsetAfterAdvance < SLIP_TRC_PRIMITIVE_FLAGS_BYTE_END) {
			return false;
		}
		record = list + recordOffsetAfterAdvance;
		evaluated = evaluatedVisits + i;
		*evaluated = (SlipTrackWorldPrimitiveBoundsEvaluatedVisit){0};
		evaluated->primitiveRecord = record;
		evaluated->recordFlags = record[SLIP_TRC_PRIMITIVE_FLAGS_OFFSET];
		evaluated->boundsBit = (uint8_t)(evaluated->recordFlags & SLIP_TRC_PRIMITIVE_CLIP_BOUNDS);
		if (evaluated->boundsBit != 0) {
			uint16_t boundsMinX;
			uint16_t boundsMinY;
			uint16_t boundsMaxX;
			uint16_t boundsMaxY;

			if (listBytesRemaining - recordOffsetAfterAdvance < SLIP_TRC_PRIMITIVE_FIRST_INDEX_END) {
				return false;
			}
			boundsMinX = SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_BOUNDS_MINIMUM_X_OFFSET);
			boundsMinY = SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_BOUNDS_MINIMUM_Y_OFFSET);
			boundsMaxX = SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_BOUNDS_MAXIMUM_X_OFFSET);
			boundsMaxY = SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_BOUNDS_MAXIMUM_Y_OFFSET);
			evaluated->calledPlaneClassify = true;
			if (!SlipTrackWorld_ClassifyPlaneFromSource(mode, boundsMinX, boundsMinY, boundsMaxX, boundsMaxY,
			                                            vertexCacheBase, vertexCacheBytes, origin, sourcePoint,
			                                            userData, matrix, &evaluated->plane)) {
				return false;
			}
			calls[i].planeRejected = evaluated->plane.carry;
			if (!calls[i].planeRejected) {
				const uint16_t maskedVertexCount =
				    (uint16_t)(SlipBytes_ReadLE16(record) & SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK);

				evaluated->callDraw3DPrimitivePath = true;
				if (pool == 0 || vertexRecords == 0 || state == 0 || transform == 0 || projectDepth == 0 ||
				    returnVisits == 0 || activeVisits == 0 || boundsVisits == 0) {
					return false;
				}
				if (!SlipDraw3D_PrimitivePath(
				        pool, vertexRecords, vertexRecordCount, record + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET,
				        listBytesRemaining - recordOffsetAfterAdvance - SLIP_TRC_PRIMITIVE_HEADER_BYTES,
				        maskedVertexCount, state, transform, projectDepth, userData, depthClipCarry, screenClipCarry,
				        returnVisits, returnVisitCapacity, activeVisits, activeVisitCapacity, boundsVisits,
				        boundsVisitCapacity, &evaluated->primitive)) {
					return false;
				}
				calls[i].primitiveRejected = evaluated->primitive.carryOut;
				calls[i].minX = (uint32_t)evaluated->primitive.minX;
				calls[i].minY = (uint32_t)evaluated->primitive.minY;
				calls[i].maxX = (uint32_t)evaluated->primitive.maxX;
				calls[i].maxY = (uint32_t)evaluated->primitive.maxY;
			}
		}
		descriptor = SlipBytes_ReadLE16(record);
		evaluated->descriptor = descriptor;
		if ((descriptor & SLIP_TRC_PRIMITIVE_TEXTURED) != 0) {
			advance = (uint32_t)(descriptor & SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK) * SLIP_TRC_TEXTURED_VERTEX_BYTES +
			          SLIP_TRC_PRIMITIVE_HEADER_BYTES;
		} else {
			advance = (uint32_t)descriptor * SLIP_TRC_VERTEX_INDEX_BYTES + SLIP_TRC_PRIMITIVE_HEADER_BYTES;
		}
		recordOffsetAfterAdvance += (size_t)advance;
		evaluated->advance = advance;
		evaluated->recordOffsetAfterAdvance = recordOffsetAfterAdvance;
	}
	if (!SlipTrackWorld_PrimitiveBounds(list, listBytesRemaining, renderContextCount, primaryLeft, primaryTop,
	                                    primaryRight, primaryBottom, calls, (size_t)count, visits, visitCapacity,
	                                    visitCount, &bounds)) {
		return false;
	}
	*result = (SlipTrackWorldPrimitiveBoundsEvaluated){bounds, (size_t)count};
	return true;
}

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
    size_t boundsVisitCapacity, const SlipView3DMatrix *matrix, SlipTrackWorldPrimitiveBoundsEvaluatedExecute *result) {
	uint16_t count;
	size_t recordOffsetAfterAdvance;
	size_t i;
	SlipTrackWorldPrimitiveBounds bounds;

	if (visitCount != 0) {
		*visitCount = 0;
	}
	if (list == 0 || calls == 0 || evaluatedVisits == 0 || result == 0 || listBytesRemaining < 2u) {
		return false;
	}
	count = SlipBytes_ReadLE16(list);
	if (count == 0 || callCapacity < (size_t)count || evaluatedVisitCapacity < (size_t)count) {
		return false;
	}
	memset(calls, 0, sizeof(calls[0]) * (size_t)count);
	recordOffsetAfterAdvance = 2u;
	for (i = 0; i < (size_t)count; ++i) {
		const uint8_t *record;
		uint16_t descriptor;
		uint32_t advance;
		SlipTrackWorldPrimitiveBoundsEvaluatedExecuteVisit *evaluated;

		if (recordOffsetAfterAdvance > listBytesRemaining ||
		    listBytesRemaining - recordOffsetAfterAdvance < SLIP_TRC_PRIMITIVE_FLAGS_BYTE_END) {
			return false;
		}
		record = list + recordOffsetAfterAdvance;
		evaluated = evaluatedVisits + i;
		*evaluated = (SlipTrackWorldPrimitiveBoundsEvaluatedExecuteVisit){0};
		evaluated->primitiveRecord = record;
		evaluated->recordFlags = record[SLIP_TRC_PRIMITIVE_FLAGS_OFFSET];
		evaluated->boundsBit = (uint8_t)(evaluated->recordFlags & SLIP_TRC_PRIMITIVE_CLIP_BOUNDS);
		if (evaluated->boundsBit != 0) {
			uint16_t boundsMinX;
			uint16_t boundsMinY;
			uint16_t boundsMaxX;
			uint16_t boundsMaxY;

			if (listBytesRemaining - recordOffsetAfterAdvance < SLIP_TRC_PRIMITIVE_FIRST_INDEX_END) {
				return false;
			}
			boundsMinX = SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_BOUNDS_MINIMUM_X_OFFSET);
			boundsMinY = SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_BOUNDS_MINIMUM_Y_OFFSET);
			boundsMaxX = SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_BOUNDS_MAXIMUM_X_OFFSET);
			boundsMaxY = SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_BOUNDS_MAXIMUM_Y_OFFSET);
			evaluated->calledPlaneClassify = true;
			if (!SlipTrackWorld_ClassifyPlaneFromSource(mode, boundsMinX, boundsMinY, boundsMaxX, boundsMaxY,
			                                            vertexCacheBase, vertexCacheBytes, origin, sourcePoint,
			                                            userData, matrix, &evaluated->plane)) {
				return false;
			}
			calls[i].planeRejected = evaluated->plane.carry;
			if (!calls[i].planeRejected) {
				const uint16_t maskedVertexCount =
				    (uint16_t)(SlipBytes_ReadLE16(record) & SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK);

				evaluated->callDraw3DPrimitivePath = true;
				if (pool == 0 || vertexRecords == 0 || state == 0 || transform == 0 || projectDepth == 0 ||
				    projectScreen == 0 || returnVisits == 0 || clipFlagVisits == 0 || activeVisits == 0 ||
				    boundsVisits == 0) {
					return false;
				}
				if (!SlipDraw3D_PrimitivePathExecute(
				        pool, vertexRecords, vertexRecordCount, record + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET,
				        listBytesRemaining - recordOffsetAfterAdvance - SLIP_TRC_PRIMITIVE_HEADER_BYTES,
				        maskedVertexCount, state, transform, projectDepth, projectScreen, userData, hasPostPlanes,
				        planeBase, planeBytes, planeHeadOffset, postLimitXMin, postLimitXMax, postLimitYMin,
				        postLimitYMax, maxClipEdgeVisits, returnVisits, returnVisitCapacity, clipFlagVisits,
				        clipFlagVisitCapacity, postBoundsVisits, postBoundsVisitCapacity, postClipRecordVisits,
				        postClipRecordVisitCapacity, postClipPlaneVisits, postClipPlaneVisitCapacity, activeVisits,
				        activeVisitCapacity, boundsVisits, boundsVisitCapacity, &evaluated->primitive)) {
					return false;
				}
				calls[i].primitiveRejected = evaluated->primitive.carryOut;
				calls[i].minX = (uint32_t)evaluated->primitive.minX;
				calls[i].minY = (uint32_t)evaluated->primitive.minY;
				calls[i].maxX = (uint32_t)evaluated->primitive.maxX;
				calls[i].maxY = (uint32_t)evaluated->primitive.maxY;
			}
		}
		descriptor = SlipBytes_ReadLE16(record);
		evaluated->descriptor = descriptor;
		if ((descriptor & SLIP_TRC_PRIMITIVE_TEXTURED) != 0) {
			advance = (uint32_t)(descriptor & SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK) * SLIP_TRC_TEXTURED_VERTEX_BYTES +
			          SLIP_TRC_PRIMITIVE_HEADER_BYTES;
		} else {
			advance = (uint32_t)descriptor * SLIP_TRC_VERTEX_INDEX_BYTES + SLIP_TRC_PRIMITIVE_HEADER_BYTES;
		}
		recordOffsetAfterAdvance += (size_t)advance;
		evaluated->advance = advance;
		evaluated->recordOffsetAfterAdvance = recordOffsetAfterAdvance;
	}
	if (!SlipTrackWorld_PrimitiveBounds(list, listBytesRemaining, renderContextCount, primaryLeft, primaryTop,
	                                    primaryRight, primaryBottom, calls, (size_t)count, visits, visitCapacity,
	                                    visitCount, &bounds)) {
		return false;
	}
	*result = (SlipTrackWorldPrimitiveBoundsEvaluatedExecute){bounds, (size_t)count};
	return true;
}

bool SlipTrackWorld_StoreRangePlane(uint32_t x, uint32_t y, uint32_t z, uint16_t inputX, uint16_t inputY,
                                    uint16_t inputZ, SlipTrackWorldRangePlane *result) {
	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldRangePlane){{(int32_t)x, (int32_t)y, (int32_t)z},
	                                     {(int32_t)(int16_t)inputX, (int32_t)(int16_t)inputY, (int32_t)(int16_t)inputZ},
	                                     true};
	return true;
}

bool SlipTrackWorld_PointLookup(const uint8_t *componentList, const uint8_t *componentBase, size_t componentBaseBytes,
                                uint16_t pointIndex, uint32_t pointXOrInput, uint32_t pointYOrInput,
                                uint32_t pointZOrCountMergedWithInput, SlipTrackWorldPointLookup *result) {
	uint16_t pointListOffset;
	uint16_t pointCount;
	uint16_t pointByteOffset;
	const uint8_t *pointList;
	const uint8_t *point;

	if (componentList == 0 || result == 0) {
		return false;
	}
	*result = (SlipTrackWorldPointLookup){
	    0, 0, pointIndex, 0, true, pointXOrInput, pointYOrInput, pointZOrCountMergedWithInput, true};
	pointListOffset = SlipBytes_ReadLE16(componentList + SLIP_TRC_COMPONENT_POINT_LIST_OFFSET);
	result->pointListOffset = pointListOffset;
	if (pointListOffset == 0) {
		return true;
	}
	if (componentBase == 0 || (size_t)pointListOffset + SLIP_TRC_TABLE_COUNT_BYTES > componentBaseBytes) {
		return false;
	}
	pointList = componentBase + pointListOffset;
	pointCount = SlipBytes_ReadLE16(pointList);
	result->pointCount = pointCount;
	result->pointZOrCountMergedWithInput =
	    (pointZOrCountMergedWithInput & SLIP_TRACK_REGISTER_UPPER_WORD_MASK) | pointCount;
	if (pointCount == 0 || pointIndex >= pointCount) {
		return true;
	}
	pointByteOffset = (uint16_t)(SLIP_TRC_POINT_BYTES * pointIndex);
	result->pointByteOffset = pointByteOffset;
	if ((size_t)pointListOffset + SLIP_TRC_TABLE_COUNT_BYTES + (size_t)pointByteOffset + SLIP_TRC_POINT_BYTES >
	    componentBaseBytes) {
		return false;
	}
	point = pointList + SLIP_TRC_TABLE_COUNT_BYTES + pointByteOffset;
	result->pointXOrInput = (uint32_t)(int32_t)(int16_t)SlipBytes_ReadLE16(point) << SLIP_TRC_POINT_COORDINATE_SHIFT;
	result->pointYOrInput = (uint32_t)(int32_t)(int16_t)SlipBytes_ReadLE16(point + SLIP_TRC_POINT_Y_OFFSET)
	                        << SLIP_TRC_POINT_COORDINATE_SHIFT;
	result->pointZOrCountMergedWithInput =
	    (uint32_t)(int32_t)(int16_t)SlipBytes_ReadLE16(point + SLIP_TRC_POINT_Z_OFFSET)
	    << SLIP_TRC_POINT_COORDINATE_SHIFT;
	result->carry = false;
	return true;
}

bool SlipTrackWorld_PrimitivePreGate(const uint8_t *record, size_t recordBytesRemaining, uint32_t rangeMode,
                                     const uint8_t *componentList, const uint8_t *componentBase,
                                     size_t componentBaseBytes, uint32_t pointInputX, uint32_t pointInputY,
                                     uint32_t pointInputZ, SlipView3DVec32 objectOffset,
                                     SlipView3DVec32 transformedPoint, SlipTrackWorldPrimitivePreGate *result) {
	uint32_t deltaX;
	uint32_t deltaY;
	uint32_t deltaZ;
	uint32_t rangeDot;
	SlipTrackWorldRangePlane rangePlane;
	SlipTrackWorldPointLookup pointLookup;

	if (record == 0 || result == 0 || recordBytesRemaining < SLIP_TRC_PRIMITIVE_FIRST_INDEX_END) {
		return false;
	}
	*result = (SlipTrackWorldPrimitivePreGate){
	    record[SLIP_TRC_PRIMITIVE_FLAGS_OFFSET],
	    (uint8_t)(record[SLIP_TRC_PRIMITIVE_FLAGS_OFFSET] & SLIP_TRC_PRIMITIVE_RANGE_PLANE),
	    rangeMode,
	    false,
	    0,
	    0,
	    0,
	    false,
	    {0},
	    {0},
	    0,
	    0,
	    0,
	    false,
	    false,
	    {{0}, {0}, false},
	    {0},
	    {0},
	    0,
	    false,
	    false,
	    false,
	    SLIP_TRACK_WORLD_PRIMITIVE_PRE_GATE_BRANCH_SKIP};
	if ((result->recordFlags & SLIP_TRC_PRIMITIVE_RANGE_PLANE) == 0) {
		return true;
	}
	if (rangeMode != SLIP_TRACK_OUTER_OBJECT_DEPTH) {
		result->branch = SLIP_TRACK_WORLD_PRIMITIVE_PRE_GATE_BRANCH_DRAW;
		return true;
	}
	result->storeRangeFlagZero = true;
	result->rangeFlag = 0;
	result->componentList = componentList;
	result->pointIndex = SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET);
	result->callTrackWorldPointLookup = true;
	if (!SlipTrackWorld_PointLookup(componentList, componentBase, componentBaseBytes, result->pointIndex, pointInputX,
	                                pointInputY, pointInputZ, &pointLookup)) {
		return false;
	}
	result->pointLookup = pointLookup;
	result->pointAfterObjectOffset =
	    (SlipView3DVec32){(int32_t)(pointLookup.pointXOrInput + (uint32_t)objectOffset.x),
	                      (int32_t)(pointLookup.pointYOrInput + (uint32_t)objectOffset.y),
	                      (int32_t)(pointLookup.pointZOrCountMergedWithInput + (uint32_t)objectOffset.z)};
	result->normalX = SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_NORMAL_X_OFFSET);
	result->normalY = SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_NORMAL_Y_OFFSET);
	result->savedPrimitivePointer = true;
	result->normalZ = SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_NORMAL_Z_OFFSET);
	result->callTrackWorldStoreRangePlane = true;
	if (!SlipTrackWorld_StoreRangePlane((uint32_t)result->pointAfterObjectOffset.x,
	                                    (uint32_t)result->pointAfterObjectOffset.y,
	                                    (uint32_t)result->pointAfterObjectOffset.z, result->normalX, result->normalY,
	                                    result->normalZ, &rangePlane)) {
		return false;
	}
	result->rangePlane = rangePlane;
	result->transformedPoint = transformedPoint;
	deltaX = (uint32_t)transformedPoint.x - (uint32_t)rangePlane.origin.x;
	deltaY = (uint32_t)transformedPoint.y - (uint32_t)rangePlane.origin.y;
	deltaZ = (uint32_t)transformedPoint.z - (uint32_t)rangePlane.origin.z;
	result->rangeDelta = (SlipView3DVec32){(int32_t)deltaX, (int32_t)deltaY, (int32_t)deltaZ};
	rangeDot = SlipTrackWorld_RoundedDotProductShift14(deltaX, deltaY, deltaZ, (uint32_t)rangePlane.normal.x,
	                                                   (uint32_t)rangePlane.normal.y, (uint32_t)rangePlane.normal.z);
	result->rangeDotRounded = rangeDot;
	result->greaterThanUpper = (int32_t)rangeDot > SLIP_TRACK_PRIMITIVE_RANGE_DISTANCE;
	if (result->greaterThanUpper) {
		result->branch = SLIP_TRACK_WORLD_PRIMITIVE_PRE_GATE_BRANCH_DRAW;
		return true;
	}
	result->lessThanLower = (int32_t)rangeDot < -SLIP_TRACK_PRIMITIVE_RANGE_DISTANCE;
	if (result->lessThanLower) {
		result->branch = SLIP_TRACK_WORLD_PRIMITIVE_PRE_GATE_BRANCH_DRAW;
		return true;
	}
	result->storeRangeFlagMinusOne = true;
	result->rangeFlag = UINT32_MAX;
	result->branch = SLIP_TRACK_WORLD_PRIMITIVE_PRE_GATE_BRANCH_RANGE_REJECT;
	return true;
}

bool SlipTrackWorld_PrimitiveDrawGate(const uint8_t *record, size_t recordBytesRemaining, uint32_t countAt,
                                      SlipTrackWorldPrimitiveDrawGateCall rejectionState,
                                      SlipTrackWorldPrimitiveDrawGate *result) {
	if (record == 0 || result == 0 || recordBytesRemaining < SLIP_TRC_PRIMITIVE_FIRST_INDEX_END) {
		return false;
	}
	*result = (SlipTrackWorldPrimitiveDrawGate){record + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET,
	                                            SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET),
	                                            SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_NORMAL_X_OFFSET),
	                                            SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_NORMAL_Y_OFFSET),
	                                            SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_NORMAL_Z_OFFSET),
	                                            true,
	                                            rejectionState.planeRejected,
	                                            0,
	                                            false,
	                                            false,
	                                            SLIP_TRACK_WORLD_PRIMITIVE_DRAW_LIST_TOKEN,
	                                            0,
	                                            false,
	                                            SLIP_TRACK_WORLD_PRIMITIVE_DRAW_GATE_BRANCH_SKIP};
	if (result->planeRejected) {
		return true;
	}
	result->descriptor = SlipBytes_ReadLE16(record);
	result->calledPolygonStatus = true;
	result->polygonRejected = rejectionState.polygonRejected;
	if (result->polygonRejected) {
		return true;
	}
	result->objectListCount = countAt;
	result->full = countAt == SLIP_TRACK_VISIBILITY_ENTRY_CAPACITY;
	if (result->full) {
		return true;
	}
	result->branch = SLIP_TRACK_WORLD_PRIMITIVE_DRAW_GATE_BRANCH_DRAW;
	return true;
}

bool SlipTrackWorld_PrimitiveDrawGateEvaluated(
    const uint8_t *record, size_t recordBytesRemaining, uint32_t countAt, uint32_t mode,
    SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCount, SlipView3DVec32 origin,
    SlipTrackWorldSourcePoint sourcePoint, const SlipDraw3DProjectState *state, SlipDraw3DTransformFn transform,
    SlipDraw3DProjectMaskFn projectMask, void *userData, SlipDraw3DPolygonStatusVisit *visits, size_t visitCapacity,
    const SlipView3DMatrix *matrix, SlipTrackWorldPrimitiveDrawGateEvaluated *result) {
	SlipTrackWorldPrimitiveDrawGateCall call;
	SlipTrackWorldPrimitiveDrawGate gate;
	SlipTrackWorldPlaneClassify plane;
	SlipDraw3DPolygonStatus polygonStatus;
	uint16_t clipMinX;
	uint16_t clipMinY;
	uint16_t clipMaxX;
	uint16_t clipMaxY;
	uint16_t drawStateIndex;

	if (record == 0 || vertexRecords == 0 || state == 0 || sourcePoint == 0 || transform == 0 || projectMask == 0 ||
	    visits == 0 || result == 0 || recordBytesRemaining < SLIP_TRC_PRIMITIVE_FIRST_INDEX_END) {
		return false;
	}
	clipMinX = SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_BOUNDS_MINIMUM_X_OFFSET);
	clipMinY = SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_BOUNDS_MINIMUM_Y_OFFSET);
	clipMaxX = SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_BOUNDS_MAXIMUM_X_OFFSET);
	clipMaxY = SlipBytes_ReadLE16(record + SLIP_TRC_PRIMITIVE_BOUNDS_MAXIMUM_Y_OFFSET);
	drawStateIndex = SlipBytes_ReadLE16(record);
	if (!SlipTrackWorld_ClassifyPlaneFromSource(
	        mode, clipMinX, clipMinY, clipMaxX, clipMaxY, (const uint8_t *)vertexRecords,
	        vertexRecordCount * sizeof(*vertexRecords), origin, sourcePoint, userData, matrix, &plane)) {
		return false;
	}
	call = (SlipTrackWorldPrimitiveDrawGateCall){plane.carry, false};
	polygonStatus = (SlipDraw3DPolygonStatus){0};
	if (!call.planeRejected) {
		if (!SlipDraw3D_PolygonStatus(vertexRecords, vertexRecordCount, record + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET,
		                              recordBytesRemaining - SLIP_TRC_PRIMITIVE_HEADER_BYTES, drawStateIndex, state,
		                              transform, projectMask, userData, visits, visitCapacity, &polygonStatus)) {
			return false;
		}
		call.polygonRejected = polygonStatus.signFlagAfterReturn;
	}
	if (!SlipTrackWorld_PrimitiveDrawGate(record, recordBytesRemaining, countAt, call, &gate)) {
		return false;
	}
	*result = (SlipTrackWorldPrimitiveDrawGateEvaluated){gate, plane, polygonStatus};
	return true;
}

bool SlipTrackWorld_LoadClipRegisters(uint32_t clipMinX, uint32_t clipMinY, uint32_t clipMaxX, uint32_t clipMaxY,
                                      SlipTrackWorldLoadClipRegisters *result) {
	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldLoadClipRegisters){clipMinX, clipMinY, clipMaxX, clipMaxY, true};
	return true;
}

bool SlipTrackWorld_PrimitiveRelatedScan(
    uint32_t primitiveToken, const uint8_t *componentRecord, size_t componentRecordBytes, uint32_t componentBaseAddress,
    uint32_t objectBaseAddress, uint32_t currentObjectAddress, uint8_t *objectList, size_t objectListBytes,
    uint32_t viewportMinX, uint32_t viewportMinY, uint32_t viewportMaxX, uint32_t viewportMaxY, uint32_t rangeMinX,
    uint32_t rangeMinY, uint32_t rangeMaxX, uint32_t rangeMaxY, const uint8_t *dispatchComponent,
    SlipTrackWorldPrimitiveRelatedScanCall clipBounds, SlipTrackWorldPrimitiveRelatedScanVisit *visits,
    size_t visitCapacity, size_t *visitCount, SlipTrackWorldPrimitiveRelatedScan *result) {
	size_t i;

	if (visitCount != 0) {
		*visitCount = 0;
	}
	if (componentRecord == 0 || visits == 0 || result == 0 || componentRecordBytes < SLIP_TRC_RELATED_OBJECTS_END ||
	    visitCapacity < SLIP_TRC_RELATED_OBJECT_COUNT) {
		return false;
	}
	*result = (SlipTrackWorldPrimitiveRelatedScan){true,
	                                               true,
	                                               true,
	                                               clipBounds.clipMinX,
	                                               clipBounds.clipMinY,
	                                               clipBounds.clipMaxX,
	                                               clipBounds.clipMaxY,
	                                               rangeMinX,
	                                               rangeMinY,
	                                               rangeMaxX,
	                                               rangeMaxY,
	                                               componentRecord,
	                                               componentRecord + SLIP_TRC_RELATED_OBJECTS_OFFSET,
	                                               SLIP_TRC_RELATED_OBJECT_COUNT,
	                                               0,
	                                               0,
	                                               false,
	                                               false,
	                                               0,
	                                               0,
	                                               0,
	                                               0,
	                                               false,
	                                               false,
	                                               0,
	                                               false,
	                                               SLIP_TRACK_WORLD_PRIMITIVE_RELATED_SCAN_BRANCH_UNMATCHED};
	for (i = 0; i < SLIP_TRC_RELATED_OBJECT_COUNT; ++i) {
		const uint8_t *relatedRecord;
		uint16_t componentOffset;
		uint16_t objectOffset;
		uint32_t objectAddress;
		SlipTrackWorldPrimitiveRelatedScanVisit *visit;

		relatedRecord = componentRecord + SLIP_TRC_RELATED_OBJECTS_OFFSET + i * SLIP_TRC_RELATED_OBJECT_BYTES;
		visit = visits + i;
		*visit = (SlipTrackWorldPrimitiveRelatedScanVisit){0};
		visit->relatedRecord = relatedRecord;
		componentOffset = SlipBytes_ReadLE16(relatedRecord + SLIP_TRC_RELATED_COMPONENT_OFFSET);
		visit->componentOffset = componentOffset;
		visit->componentAddress = componentBaseAddress + (uint32_t)componentOffset;
		visit->matchesComponent = visit->componentAddress == primitiveToken;
		if (visitCount != 0) {
			*visitCount = i + 1u;
		}
		result->visitCount = (uint32_t)(i + 1u);
		if (!visit->matchesComponent) {
			visit->skippedRelatedRecord = true;
			continue;
		}
		objectOffset = SlipBytes_ReadLE16(relatedRecord);
		visit->objectOffset = objectOffset;
		objectAddress = objectBaseAddress + (uint32_t)objectOffset;
		visit->objectAddress = objectAddress;
		visit->matchesCurrentObject = objectAddress == currentObjectAddress;
		if (visit->matchesCurrentObject) {
			visit->skippedRelatedRecord = true;
			continue;
		}
		if (objectList == 0 || objectListBytes < SLIP_TRACK_VISIBILITY_LIST_HEADER_BYTES) {
			return false;
		}
		visit->enterObjectListScan = true;
		visit->objectListCount = *(const uint32_t *)(const void *)objectList;
		if (visit->objectListCount == 0) {
			return false;
		}
		for (visit->scannedObjectEntries = 0; visit->scannedObjectEntries < visit->objectListCount;
		     ++visit->scannedObjectEntries) {
			const size_t entryOffset = SLIP_TRACK_VISIBILITY_LIST_HEADER_BYTES +
			                           (size_t)visit->scannedObjectEntries * SLIP_TRACK_VISIBILITY_ENTRY_BYTES;

			if (entryOffset > objectListBytes || objectListBytes - entryOffset < SLIP_TRACK_VISIBILITY_BOUNDS_END) {
				return false;
			}
			SlipTrackVisibilityEntry *const entry = (SlipTrackVisibilityEntry *)(void *)(objectList + entryOffset);
			if (entry->recordAddress == objectAddress) {
				visit->foundObjectEntry = true;
				visit->foundObjectEntryIndex = visit->scannedObjectEntries;
				result->matchedEntry = objectList + entryOffset;
				result->savedObjectAddress = true;
				result->savedMatchedPrimitivePointer = true;
				entry->minX = viewportMinX;
				entry->minY = viewportMinY;
				entry->maxX = viewportMaxX;
				entry->maxY = viewportMaxY;
				result->entryMinX = viewportMinX;
				result->entryMinY = viewportMinY;
				result->entryMaxX = viewportMaxX;
				result->entryMaxY = viewportMaxY;
				result->calledRestoreClipBounds = true;
				result->calledRestoreVertexBufferCursor = true;
				result->dispatchComponent = dispatchComponent;
				result->callTrackWorldChildListDispatch = true;
				result->branch = SLIP_TRACK_WORLD_PRIMITIVE_RELATED_SCAN_BRANCH_MATCHED;
				return true;
			}
		}
		result->branch = SLIP_TRACK_WORLD_PRIMITIVE_RELATED_SCAN_BRANCH_MATCHED;
		return true;
	}
	return true;
}

bool SlipTrackWorld_PrimitiveRelatedScanExecute(
    uint32_t primitiveToken, const uint8_t *componentRecord, size_t componentRecordBytes, uint32_t componentBaseAddress,
    uint32_t objectBaseAddress, uint32_t currentObjectAddress, uint8_t *objectList, size_t objectListBytes,
    uint32_t viewportMinX, uint32_t viewportMinY, uint32_t viewportMaxX, uint32_t viewportMaxY, uint32_t rangeMinX,
    uint32_t rangeMinY, uint32_t rangeMaxX, uint32_t rangeMaxY, const uint8_t *component, uint32_t clipMinX,
    uint32_t clipMinY, uint32_t clipMaxX, uint32_t clipMaxY, SlipTrackWorldPrimitiveRelatedScanVisit *visits,
    size_t visitCapacity, size_t *visitCount, SlipTrackWorldPrimitiveRelatedScanExecute *result) {
	SlipTrackWorldPrimitiveRelatedScanCall clipBounds;
	SlipTrackWorldPrimitiveRelatedScan scan;
	SlipTrackWorldLoadClipRegisters load;

	if (result == 0) {
		return false;
	}
	if (!SlipTrackWorld_LoadClipRegisters(clipMinX, clipMinY, clipMaxX, clipMaxY, &load)) {
		return false;
	}
	clipBounds = (SlipTrackWorldPrimitiveRelatedScanCall){load.clipMinX, load.clipMinY, load.clipMaxX, load.clipMaxY};
	if (!SlipTrackWorld_PrimitiveRelatedScan(primitiveToken, componentRecord, componentRecordBytes,
	                                         componentBaseAddress, objectBaseAddress, currentObjectAddress, objectList,
	                                         objectListBytes, viewportMinX, viewportMinY, viewportMaxX, viewportMaxY,
	                                         rangeMinX, rangeMinY, rangeMaxX, rangeMaxY, component, clipBounds, visits,
	                                         visitCapacity, visitCount, &scan)) {
		return false;
	}
	*result = (SlipTrackWorldPrimitiveRelatedScanExecute){load, scan};
	return true;
}

bool SlipTrackWorld_PrimitiveRangeState(const uint8_t *recordFrom, size_t recordBytesRemaining,
                                        uint32_t restoredPrimitiveToken, uint32_t rangeMode, uint32_t rangeFlag,
                                        uint32_t viewportMinX, uint32_t viewportMinY, uint32_t viewportMaxX,
                                        uint32_t viewportMaxY, uint32_t rangeMinX, uint32_t rangeMinY,
                                        uint32_t rangeMaxX, uint32_t rangeMaxY,
                                        SlipTrackWorldPrimitiveRangeStateCall callDraw3DPrimitivePath,
                                        SlipTrackWorldPrimitiveRangeState *result) {
	uint32_t currentRangeMinX;
	uint32_t currentRangeMinY;
	uint32_t currentRangeMaxX;
	uint32_t currentRangeMaxY;

	if (recordFrom == 0 || result == 0) {
		return false;
	}
	*result = (SlipTrackWorldPrimitiveRangeState){recordFrom,
	                                              rangeMode,
	                                              rangeFlag,
	                                              0,
	                                              false,
	                                              false,
	                                              0,
	                                              0,
	                                              0,
	                                              0,
	                                              false,
	                                              false,
	                                              0,
	                                              0,
	                                              0,
	                                              0,
	                                              rangeMinX,
	                                              rangeMinY,
	                                              rangeMaxX,
	                                              rangeMaxY,
	                                              false,
	                                              false,
	                                              SLIP_TRACK_WORLD_PRIMITIVE_RANGE_STATE_BRANCH_CONTINUE};
	if (rangeMode == SLIP_TRACK_OUTER_OBJECT_DEPTH && rangeFlag != 0) {
		result->restoredPrimitiveToken = restoredPrimitiveToken;
		result->rangeMinX = viewportMinX;
		result->rangeMinY = viewportMinY;
		result->rangeMaxX = viewportMaxX;
		result->rangeMaxY = viewportMaxY;
		result->calledRestoreClipBounds = true;
		result->calledRestoreVertexBufferCursor = true;
		result->branch = SLIP_TRACK_WORLD_PRIMITIVE_RANGE_STATE_BRANCH_RESTORED;
		return true;
	}
	if (recordBytesRemaining < SLIP_TRC_PRIMITIVE_FIRST_INDEX_END) {
		return false;
	}
	result->primitiveRecord = recordFrom;
	result->descriptor = SlipBytes_ReadLE16(recordFrom);
	result->vertexCount = (uint16_t)(result->descriptor & SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK);
	result->vertexIndexList = recordFrom + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET;
	result->callDraw3DPrimitivePath = true;
	result->primitiveRejected = callDraw3DPrimitivePath.primitiveRejected;
	if (rangeMode == SLIP_TRACK_OUTER_OBJECT_DEPTH) {
		result->savedPrimitiveInRangeMode = true;
	} else {
		result->savedPrimitiveInOtherMode = true;
	}
	if (result->primitiveRejected) {
		return true;
	}
	result->minX = callDraw3DPrimitivePath.minX;
	result->minY = callDraw3DPrimitivePath.minY;
	result->maxX = callDraw3DPrimitivePath.maxX;
	result->maxY = callDraw3DPrimitivePath.maxY;
	currentRangeMinX = callDraw3DPrimitivePath.minX;
	currentRangeMinY = callDraw3DPrimitivePath.minY;
	currentRangeMaxX = callDraw3DPrimitivePath.maxX;
	currentRangeMaxY = callDraw3DPrimitivePath.maxY;
	if (rangeMode != SLIP_TRACK_OUTER_OBJECT_DEPTH) {
		if ((int32_t)currentRangeMinX < (int32_t)rangeMinX) {
			currentRangeMinX = rangeMinX;
		}
		if ((int32_t)currentRangeMinX > (int32_t)rangeMaxX) {
			currentRangeMinX = rangeMaxX;
		}
		if ((int32_t)currentRangeMinY < (int32_t)rangeMinY) {
			currentRangeMinY = rangeMinY;
		}
		if ((int32_t)currentRangeMinY > (int32_t)rangeMaxY) {
			currentRangeMinY = rangeMaxY;
		}
		if ((int32_t)currentRangeMaxX > (int32_t)rangeMaxX) {
			currentRangeMaxX = rangeMaxX;
		}
		if ((int32_t)currentRangeMaxX < (int32_t)rangeMinX) {
			currentRangeMaxX = rangeMinX;
		}
		if ((int32_t)currentRangeMaxY > (int32_t)rangeMaxY) {
			currentRangeMaxY = rangeMaxY;
		}
		if ((int32_t)currentRangeMaxY < (int32_t)rangeMinY) {
			currentRangeMaxY = rangeMinY;
		}
	}
	result->rangeMinX = currentRangeMinX;
	result->rangeMinY = currentRangeMinY;
	result->rangeMaxX = currentRangeMaxX;
	result->rangeMaxY = currentRangeMaxY;
	result->calledRestoreClipBounds = true;
	result->calledRestoreVertexBufferCursor = true;
	result->branch = SLIP_TRACK_WORLD_PRIMITIVE_RANGE_STATE_BRANCH_RESTORED;
	return true;
}

bool SlipTrackWorld_PrimitiveRangeStateEvaluated(
    const uint8_t *recordFrom, size_t recordBytesRemaining, uint32_t primitiveToken, uint32_t rangeMode,
    uint32_t rangeFlag, uint32_t viewportMinX, uint32_t viewportMinY, uint32_t viewportMaxX, uint32_t viewportMaxY,
    uint32_t rangeMinX, uint32_t rangeMinY, uint32_t rangeMaxX, uint32_t rangeMaxY, SlipDraw3DRecordPool *pool,
    SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCount, const SlipDraw3DProjectState *state,
    SlipDraw3DTransformFn transform, SlipDraw3DProjectFn projectDepth, void *userData, int depthClipCarry,
    int screenClipCarry, SlipDraw3DReturnActiveVisit *returnVisits, size_t returnVisitCapacity,
    SlipDraw3DActiveRingVisit *activeVisits, size_t activeVisitCapacity, SlipDraw3DActiveBoundsVisit *boundsVisits,
    size_t boundsVisitCapacity, SlipTrackWorldPrimitiveRangeStateEvaluated *result) {
	SlipTrackWorldPrimitiveRangeStateCall callDraw3DPrimitivePath;
	SlipDraw3DPrimitivePath primitive;
	SlipTrackWorldPrimitiveRangeState range;
	uint16_t maskedVertexCount;

	if (recordFrom == 0 || result == 0) {
		return false;
	}
	callDraw3DPrimitivePath = (SlipTrackWorldPrimitiveRangeStateCall){0};
	primitive = (SlipDraw3DPrimitivePath){0};
	if (!(rangeMode == SLIP_TRACK_OUTER_OBJECT_DEPTH && rangeFlag != 0)) {
		if (recordBytesRemaining < SLIP_TRC_PRIMITIVE_FIRST_INDEX_END || pool == 0 || vertexRecords == 0 ||
		    state == 0 || transform == 0 || projectDepth == 0 || returnVisits == 0 || activeVisits == 0 ||
		    boundsVisits == 0) {
			return false;
		}
		maskedVertexCount = (uint16_t)(SlipBytes_ReadLE16(recordFrom) & SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK);
		if (!SlipDraw3D_PrimitivePath(
		        pool, vertexRecords, vertexRecordCount, recordFrom + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET,
		        recordBytesRemaining - SLIP_TRC_PRIMITIVE_HEADER_BYTES, maskedVertexCount, state, transform,
		        projectDepth, userData, depthClipCarry, screenClipCarry, returnVisits, returnVisitCapacity,
		        activeVisits, activeVisitCapacity, boundsVisits, boundsVisitCapacity, &primitive)) {
			return false;
		}
		callDraw3DPrimitivePath = (SlipTrackWorldPrimitiveRangeStateCall){
		    primitive.carryOut, (uint32_t)primitive.minX, (uint32_t)primitive.minY, (uint32_t)primitive.maxX,
		    (uint32_t)primitive.maxY};
	}
	if (!SlipTrackWorld_PrimitiveRangeState(recordFrom, recordBytesRemaining, primitiveToken, rangeMode, rangeFlag,
	                                        viewportMinX, viewportMinY, viewportMaxX, viewportMaxY, rangeMinX,
	                                        rangeMinY, rangeMaxX, rangeMaxY, callDraw3DPrimitivePath, &range)) {
		return false;
	}
	*result = (SlipTrackWorldPrimitiveRangeStateEvaluated){range, primitive};
	return true;
}

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
    size_t boundsVisitCapacity, SlipTrackWorldPrimitiveRangeStateEvaluatedExecute *result) {
	SlipTrackWorldPrimitiveRangeStateCall callDraw3DPrimitivePath;
	SlipDraw3DPrimitivePathExecute primitive;
	SlipTrackWorldPrimitiveRangeState range;
	uint16_t maskedVertexCount;

	if (recordFrom == 0 || result == 0) {
		return false;
	}
	callDraw3DPrimitivePath = (SlipTrackWorldPrimitiveRangeStateCall){0};
	primitive = (SlipDraw3DPrimitivePathExecute){0};
	if (!(rangeMode == SLIP_TRACK_OUTER_OBJECT_DEPTH && rangeFlag != 0)) {
		if (recordBytesRemaining < SLIP_TRC_PRIMITIVE_FIRST_INDEX_END || pool == 0 || vertexRecords == 0 ||
		    state == 0 || transform == 0 || projectDepth == 0 || projectScreen == 0 || returnVisits == 0 ||
		    clipFlagVisits == 0 || activeVisits == 0 || boundsVisits == 0) {
			return false;
		}
		maskedVertexCount = (uint16_t)(SlipBytes_ReadLE16(recordFrom) & SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK);
		if (!SlipDraw3D_PrimitivePathExecute(
		        pool, vertexRecords, vertexRecordCount, recordFrom + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET,
		        recordBytesRemaining - SLIP_TRC_PRIMITIVE_HEADER_BYTES, maskedVertexCount, state, transform,
		        projectDepth, projectScreen, userData, hasPostPlanes, planeBase, planeBytes, planeHeadOffset,
		        postLimitXMin, postLimitXMax, postLimitYMin, postLimitYMax, maxClipEdgeVisits, returnVisits,
		        returnVisitCapacity, clipFlagVisits, clipFlagVisitCapacity, postBoundsVisits, postBoundsVisitCapacity,
		        postClipRecordVisits, postClipRecordVisitCapacity, postClipPlaneVisits, postClipPlaneVisitCapacity,
		        activeVisits, activeVisitCapacity, boundsVisits, boundsVisitCapacity, &primitive)) {
			return false;
		}
		callDraw3DPrimitivePath = (SlipTrackWorldPrimitiveRangeStateCall){
		    primitive.carryOut, (uint32_t)primitive.minX, (uint32_t)primitive.minY, (uint32_t)primitive.maxX,
		    (uint32_t)primitive.maxY};
	}
	if (!SlipTrackWorld_PrimitiveRangeState(recordFrom, recordBytesRemaining, primitiveToken, rangeMode, rangeFlag,
	                                        viewportMinX, viewportMinY, viewportMaxX, viewportMaxY, rangeMinX,
	                                        rangeMinY, rangeMaxX, rangeMaxY, callDraw3DPrimitivePath, &range)) {
		return false;
	}
	*result = (SlipTrackWorldPrimitiveRangeStateEvaluatedExecute){range, primitive};
	return true;
}

bool SlipTrackWorld_PrimitiveNestedObjectCall(uint32_t componentViewX, uint32_t componentViewY, uint32_t componentViewZ,
                                              uint32_t savedObjectWorldX, uint32_t savedObjectWorldY,
                                              uint32_t savedObjectWorldZ, uint32_t componentRecord,
                                              uint32_t currentComponentToken, uint32_t savedCurrentObjectAddress,
                                              uint32_t savedRangeFlag, uint32_t renderContextCount,
                                              SlipTrackWorldPrimitiveNestedObjectCall *result) {
	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldPrimitiveNestedObjectCall){componentViewX,
	                                                    componentViewY,
	                                                    componentViewZ,
	                                                    savedObjectWorldX,
	                                                    savedObjectWorldY,
	                                                    savedObjectWorldZ,
	                                                    componentRecord,
	                                                    currentComponentToken,
	                                                    savedCurrentObjectAddress,
	                                                    savedRangeFlag,
	                                                    renderContextCount,
	                                                    renderContextCount,
	                                                    componentRecord,
	                                                    componentRecord,
	                                                    true,
	                                                    savedRangeFlag,
	                                                    savedCurrentObjectAddress,
	                                                    currentComponentToken,
	                                                    componentRecord,
	                                                    savedObjectWorldZ,
	                                                    savedObjectWorldY,
	                                                    savedObjectWorldX,
	                                                    componentViewZ,
	                                                    componentViewY,
	                                                    componentViewX,
	                                                    currentComponentToken,
	                                                    true};
	return true;
}

bool SlipTrackWorld_PrimitiveDrawEpilogue(uint32_t restoredRangeMaxY, uint32_t restoredRangeMaxX,
                                          uint32_t restoredRangeMinY, uint32_t restoredRangeMinX,
                                          uint32_t restoredClipMaxY, uint32_t restoredClipMaxX,
                                          uint32_t restoredClipMinY, uint32_t restoredClipMinX,
                                          uint32_t restoredPrimitiveToken, uint32_t restoredComponentToken,
                                          SlipTrackWorldPrimitiveDrawEpilogue *result) {
	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldPrimitiveDrawEpilogue){restoredRangeMaxY,      restoredRangeMaxX,      restoredRangeMinY,
	                                                restoredRangeMinX,      restoredClipMaxY,       restoredClipMaxX,
	                                                restoredClipMinY,       restoredClipMinX,       true,
	                                                restoredPrimitiveToken, restoredComponentToken, true};
	return true;
}

bool SlipTrackWorld_PrimitiveDrawAdvance(const uint8_t *record, size_t recordBytesRemaining,
                                         size_t recordOffsetAfterAdvance, uint16_t remainingCountBefore,
                                         SlipTrackWorldPrimitiveDrawAdvance *result) {
	uint16_t descriptor;
	uint32_t advance;

	if (record == 0 || result == 0 || recordBytesRemaining < 2u) {
		return false;
	}
	descriptor = SlipBytes_ReadLE16(record);
	*result = (SlipTrackWorldPrimitiveDrawAdvance){remainingCountBefore,
	                                               true,
	                                               descriptor,
	                                               (descriptor & SLIP_TRC_PRIMITIVE_TEXTURED) != 0,
	                                               false,
	                                               0,
	                                               0,
	                                               recordOffsetAfterAdvance,
	                                               false,
	                                               true,
	                                               (uint16_t)(remainingCountBefore - 1u),
	                                               SLIP_TRACK_WORLD_PRIMITIVE_DRAW_ADVANCE_BRANCH_FINISHED};
	if (result->highBit) {
		result->savedStrideAccumulator = true;
		result->vertexSourceStride = SLIP_TRC_TEXTURED_VERTEX_BYTES;
		advance = (uint32_t)(descriptor & SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK) * SLIP_TRC_TEXTURED_VERTEX_BYTES +
		          SLIP_TRC_PRIMITIVE_HEADER_BYTES;
		result->restoredStrideAccumulator = true;
	} else {
		advance = (uint32_t)descriptor * SLIP_TRC_VERTEX_INDEX_BYTES + SLIP_TRC_PRIMITIVE_HEADER_BYTES;
	}
	result->advance = advance;
	result->recordOffsetAfterAdvance = recordOffsetAfterAdvance + (size_t)advance;
	if (result->remainingCountAfter != 0) {
		result->branch = SLIP_TRACK_WORLD_PRIMITIVE_DRAW_ADVANCE_BRANCH_CONTINUE;
	}
	return true;
}

bool SlipTrackWorld_PrimitiveOuterTail(uint32_t restoredPrimitiveToken, uint32_t rangeMode,
                                       SlipTrackWorldPrimitiveOuterTail *result) {
	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldPrimitiveOuterTail){true, restoredPrimitiveToken, rangeMode, rangeMode - 1u, true};
	return true;
}

bool SlipTrackWorld_PrimitiveOuterTailExecute(uint32_t primitiveToken, uint32_t rangeMode, uint32_t vertexBufferCursor,
                                              const SlipDraw3DStateRecord *drawStateRecord,
                                              SlipTrackWorldPrimitiveOuterTailExecute *result) {
	SlipDraw3DRestoreVertexBufferCursor restore;
	SlipTrackWorldPrimitiveOuterTail tail;

	if (result == 0) {
		return false;
	}
	if (!SlipDraw3D_RestoreVertexBufferCursor(vertexBufferCursor, drawStateRecord, &restore)) {
		return false;
	}
	if (!SlipTrackWorld_PrimitiveOuterTail(primitiveToken, rangeMode, &tail)) {
		return false;
	}
	*result = (SlipTrackWorldPrimitiveOuterTailExecute){restore, tail};
	return true;
}

bool SlipTrackWorld_ChildListDispatch(const uint8_t *componentBefore, size_t componentBytesRemaining,
                                      const uint8_t *componentBase, size_t componentBaseBytes,
                                      SlipTrackWorldChildListDispatch *result) {
	uint16_t childOffset;
	const uint8_t *childList;

	if (componentBefore == 0 || componentBase == 0 || result == 0 ||
	    componentBytesRemaining < SLIP_TRC_COMPONENT_POINT_LIST_END) {
		return false;
	}
	childOffset = SlipBytes_ReadLE16(componentBefore + SLIP_TRC_COMPONENT_POINT_LIST_OFFSET);
	if ((size_t)childOffset > componentBaseBytes ||
	    componentBaseBytes - (size_t)childOffset < SLIP_TRC_TABLE_COUNT_BYTES) {
		return false;
	}
	childList = componentBase + childOffset;
	*result = (SlipTrackWorldChildListDispatch){.childOffset = childOffset,
	                                            .childList = childList,
	                                            .vertexCount = SlipBytes_ReadLE16(childList),
	                                            .savedComponentPointer = true,
	                                            .componentBefore = componentBefore,
	                                            .vertexSource = childList + SLIP_TRC_TABLE_COUNT_BYTES,
	                                            .vertexSourceStride = SLIP_TRC_POINT_BYTES,
	                                            .callBuildVertexRecords = true,
	                                            .restoredComponent = componentBefore,
	                                            .returned = true};
	return true;
}

bool SlipTrackWorld_ChildListDispatchExecute(const uint8_t *component, size_t componentBytesRemaining,
                                             const uint8_t *componentBase, size_t componentBaseBytes,
                                             SlipDraw3DVertexRecord *vertexRecords, size_t vertexRecordCapacity,
                                             SlipTrackWorldChildListDispatchExecute *result) {
	SlipTrackWorldChildListDispatch dispatch;
	size_t sourceSize;

	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldChildListDispatchExecute){0};
	if (!SlipTrackWorld_ChildListDispatch(component, componentBytesRemaining, componentBase, componentBaseBytes,
	                                      &dispatch)) {
		return false;
	}
	result->dispatch = dispatch;
	if (vertexRecords == 0 || dispatch.vertexSource < componentBase ||
	    dispatch.vertexSource > componentBase + componentBaseBytes) {
		return false;
	}
	sourceSize = componentBaseBytes - (size_t)(dispatch.vertexSource - componentBase);
	result->callBuildVertexRecords = true;
	if (!SlipDraw3D_BuildVertexRecords(vertexRecords, vertexRecordCapacity, dispatch.vertexSource, sourceSize,
	                                   dispatch.vertexCount, (int16_t)dispatch.vertexSourceStride, NULL, NULL, NULL, 0,
	                                   0, NULL)) {
		return false;
	}
	result->returned = true;
	return true;
}

bool SlipTrackWorld_DeferredListSetup(uint32_t frameRenderFlags, uint32_t viewportMinX, uint32_t viewportMinY,
                                      uint32_t viewportMaxX, uint32_t viewportMaxY, const uint8_t *deferredList,
                                      size_t listBytes, SlipTrackWorldDeferredListSetup *result) {
	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldDeferredListSetup){frameRenderFlags,
	                                            frameRenderFlags == UINT32_MAX,
	                                            0,
	                                            0,
	                                            0,
	                                            0,
	                                            false,
	                                            0,
	                                            0,
	                                            false,
	                                            0,
	                                            false,
	                                            SLIP_TRACK_WORLD_DEFERRED_LIST_SETUP_BRANCH_SKIPPED};
	if (result->disabled) {
		result->returned = true;
		return true;
	}
	if (deferredList == 0 || listBytes < SLIP_TRACK_DEFERRED_LIST_HEADER_BYTES) {
		return false;
	}
	result->clipMinX = viewportMinX;
	result->clipMinY = viewportMinY;
	result->clipMaxX = viewportMaxX;
	result->clipMaxY = viewportMaxY;
	result->callStoreClipBounds = true;
	result->deferredList = deferredList;
	result->entryCount = *(const uint32_t *)(const void *)deferredList;
	result->zeroCount = result->entryCount == 0;
	if (result->zeroCount) {
		result->returned = true;
		return true;
	}
	result->firstCursor = deferredList + SLIP_TRACK_DEFERRED_LIST_HEADER_BYTES;
	result->branch = SLIP_TRACK_WORLD_DEFERRED_LIST_SETUP_BRANCH_READY;
	return true;
}

bool SlipTrackWorld_DeferredItemPrologue(uint32_t savedRemainingCount, const uint8_t *savedCursor,
                                         size_t cursorBytesRemaining, const uint8_t *inputEntry,
                                         size_t entryBytesRemaining, uint32_t viewportMinX, uint32_t viewportMinY,
                                         uint32_t viewportMaxX, uint32_t viewportMaxY,
                                         SlipTrackWorldDeferredItemPrologue *result) {
	if (savedCursor == 0 || inputEntry == 0 || result == 0 ||
	    cursorBytesRemaining < SLIP_TRACK_DEFERRED_REFERENCE_BYTES ||
	    entryBytesRemaining < SLIP_TRACK_VISIBILITY_ENTRY_BYTES) {
		return false;
	}
	const SlipTrackVisibilityEntry *const entry = (const SlipTrackVisibilityEntry *)(const void *)inputEntry;
	*result = (SlipTrackWorldDeferredItemPrologue){savedRemainingCount,
	                                               savedCursor,
	                                               *(const uint32_t *)(const void *)savedCursor,
	                                               inputEntry,
	                                               entry->recordAddress,
	                                               entry->resetMaximumDepth,
	                                               0,
	                                               false,
	                                               entry->useClipBounds,
	                                               false,
	                                               0,
	                                               0,
	                                               0,
	                                               0,
	                                               false,
	                                               0,
	                                               0,
	                                               0,
	                                               0,
	                                               false,
	                                               SLIP_TRACK_WORLD_DEFERRED_ITEM_PROLOGUE_BRANCH_DIRECT};
	if (result->resetMaximumDepth != 0) {
		result->restoredMaximumDepth = INT32_MAX;
		result->callDraw3DSetMaximumDepth = true;
	}
	result->useEntryTransform = result->useClipBounds != 0;
	if (result->useEntryTransform) {
		result->clipMinX = entry->minX;
		result->clipMinY = entry->minY;
		result->clipMaxX = entry->maxX;
		result->clipMaxY = entry->maxY;
	} else {
		result->clipMinX = viewportMinX;
		result->clipMinY = viewportMinY;
		result->clipMaxX = viewportMaxX;
		result->clipMaxY = viewportMaxY;
	}
	result->callStoreClipBounds = true;
	result->viewX = (uint32_t)entry->viewPosition.x;
	result->viewY = (uint32_t)entry->viewPosition.y;
	result->viewZ = (uint32_t)entry->viewPosition.z;
	result->callbackFlag = entry->callbackFlag;
	if (result->callbackFlag != 0) {
		result->callTestRecordVisibility = true;
		result->branch = SLIP_TRACK_WORLD_DEFERRED_ITEM_PROLOGUE_BRANCH_CALLBACK;
	}
	return true;
}

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
    SlipTrackWorldDeferredItemDirect *result) {
	uint16_t componentOffset;
	const uint8_t *component;
	SlipTrackWorldComponentSetup componentSetup;
	SlipTrackWorldComponentTailVisit tailVisits[SLIP_TRACK_DEFERRED_VISIT_CAPACITY];
	SlipTrackWorldComponentTail componentTail;
	SlipTrackWorldPrimitiveWalkerVisit primitiveVisits[SLIP_TRACK_DEFERRED_VISIT_CAPACITY];
	SlipTrackWorldPrimitiveWalker primitiveWalker;
	size_t tailVisitCount = 0;

	if (record == 0 || componentBase == 0 || restoredCursor == 0 || result == 0 ||
	    recordBytesRemaining < SLIP_TRD_SECTION_BYTES) {
		return false;
	}
	componentOffset = SlipBytes_ReadLE16(record + SLIP_TRD_SECTION_COMPONENT_OFFSET);
	if ((size_t)componentOffset > componentBaseBytes ||
	    componentBaseBytes - (size_t)componentOffset < SLIP_TRC_COMPONENT_FLAGS_END) {
		return false;
	}
	component = componentBase + componentOffset;
	memset(&componentSetup, 0, sizeof(componentSetup));
	*result = (SlipTrackWorldDeferredItemDirect){0};
	result->viewX = x;
	result->viewY = y;
	result->viewZ = z;
	result->recordDepthFadeThreshold = SlipBytes_ReadLE16(record + SLIP_TRD_SECTION_SHADE_OFFSET);
	result->componentOffset = componentOffset;
	result->componentAddress = componentBaseAddress + (uint32_t)componentOffset;
	result->componentDrawMask = SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_FLAGS_OFFSET);
	result->restoredMaximumDepth = savedMaximumDepth;
	result->callDraw3DSetMaximumDepth = true;
	result->restoredFadeStart = savedFadeStart;
	result->restoredFadeEnd = savedFadeEnd;
	result->restoredFadeColour = savedFadeColour;
	result->callRestoreDepthFade = true;
	result->restoredCursor = restoredCursor;
	result->restoredRemainingCount = restoredRemainingCount;
	result->nextCursor = restoredCursor + SLIP_TRACK_DEFERRED_REFERENCE_BYTES;
	result->remainingCount = restoredRemainingCount - 1u;
	result->branch = SLIP_TRACK_WORLD_DEFERRED_ITEM_DIRECT_BRANCH_CONTINUE;
	if ((int16_t)result->recordDepthFadeThreshold <= SLIP_TRACK_DEPTH_FADE_THRESHOLD) {
		result->disabledFadeStart = 0;
		result->callDisableDepthFade = true;
	}
	result->enabledComponentDrawMask = (uint16_t)(result->componentDrawMask & renderContextIndex);
	if (result->enabledComponentDrawMask != 0) {

		uint32_t incomingValue = result->callDisableDepthFade ? 0 : x;
		incomingValue = (incomingValue & SLIP_TRACK_REGISTER_UPPER_WORD_MASK) | result->enabledComponentDrawMask;
		if (!SlipTrackWorld_ComponentSetup(record, recordBytesRemaining, incomingValue, z, drawFlags, textureMode,
		                                   shading, componentDistance, shadingSecondary, componentRadius, specialRecord,
		                                   globalAfter, randomState, shadows, processedComponentCount, drawStateIndex,
		                                   refuel, &componentSetup)) {
			return false;
		}
		result->callTrackWorldComponentSetup = true;
		result->componentSetup = componentSetup;
		if (drawStateLoad != 0) {
			if (!drawStateLoad(componentSetup.drawStateIndexAfter, drawStateLoadUserData)) {
				return false;
			}
			result->childDrawStateLoaded = true;
		}
		if (!SlipTrackWorld_ComponentTail(
		        component, componentBaseBytes - (size_t)(component - componentBase), componentBase, componentBaseBytes,
		        componentSetup.shade, componentSetup.drawFlags.rendererFlags, actorReplayMode,
		        componentSetup.currentRecord, ambientLightScaleQ14, scaledLightX, scaledLightY, scaledLightZ,
		        directLightScaleQ14, renderContextCount, primitiveCallback, componentSetup.drawStateIndexAfter,
		        tailVisits, sizeof(tailVisits) / sizeof(tailVisits[0]), &tailVisitCount, &componentTail)) {
			return false;
		}
		result->callTrackWorldComponentTail = true;
		result->componentTail = componentTail;
		result->componentTailVisitCount = tailVisitCount;
		if (componentTail.callBuildVertexRecords) {
			if (componentTail.vertexSource == 0 || componentTail.vertexSource < componentBase ||
			    componentTail.vertexSource > componentBase + componentBaseBytes) {
				return false;
			}
			result->vertexRecordsBuilt = true;
			if (buildVertexRecords != 0) {
				if (!buildVertexRecords(componentTail.vertexSource,
				                        componentBaseBytes - (size_t)(componentTail.vertexSource - componentBase),
				                        componentTail.vertexCount, (int16_t)componentTail.vertexSourceStride,
				                        buildVertexRecordsUserData)) {
					return false;
				}
			} else if (vertexRecords == 0 ||
			           !SlipDraw3D_BuildVertexRecords(
			               vertexRecords, vertexRecordCapacity, componentTail.vertexSource,
			               componentBaseBytes - (size_t)(componentTail.vertexSource - componentBase),
			               componentTail.vertexCount, (int16_t)componentTail.vertexSourceStride, NULL, NULL, NULL, 0, 0,
			               &result->buildVertexRecords)) {
				return false;
			}
		}

		if (componentTail.calledScaleLight && scaledLight != NULL && !scaledLight(lightUserData, &componentTail)) {
			return false;
		}
		if (componentTail.callTrackWorldPrimitiveWalker) {
			if (!SlipTrackWorld_PrimitiveWalker(component, componentBaseBytes - (size_t)(component - componentBase),
			                                    componentBase, componentBaseBytes, componentSetup.replayCount,
			                                    primitiveVisits, sizeof(primitiveVisits) / sizeof(primitiveVisits[0]),
			                                    &primitiveWalker)) {
				return false;
			}
			result->callTrackWorldPrimitiveWalker = true;
			result->primitiveWalkerChildOffset = primitiveWalker.childOffset;
			result->primitiveWalkerDirectCallbackBranch = primitiveWalker.directCallbackBranch;
			if (primitiveWalker.directCallbackBranch) {
				const size_t directListOffset = (size_t)primitiveWalker.childOffset;
				SlipTrackWorldDirectCallbackVisit *directVisits = 0;
				size_t directVisitCapacity = 0;

				if (directListOffset > componentBaseBytes || componentBaseBytes - directListOffset < 2u) {
					return false;
				}
				result->callTrackWorldDirectCallbackLoop = true;
				result->directCallbackChildOffset = primitiveWalker.childOffset;
				result->directCallbackCount = SlipBytes_ReadLE16(componentBase + directListOffset);
				result->directCallbackFirstRecordOffset = directListOffset + SLIP_TRC_TABLE_COUNT_BYTES;
				if (callbackFunction != 0) {
					SlipTrackWorldDirectCallbackLoop directLoop;
					SlipTrackWorldDirectCallbackEnvironment directEnvironment = {
					    &componentSetup, &componentTail, &primitiveWalker, {(int32_t)x, (int32_t)y, (int32_t)z}};

					{
						const size_t loopListOffset =
						    (size_t)SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_PRIMITIVE_LIST_OFFSET);

						directVisitCapacity = 0;
						if (loopListOffset + SLIP_TRC_TABLE_COUNT_BYTES <= componentBaseBytes) {
							directVisitCapacity = (size_t)SlipBytes_ReadLE16(componentBase + loopListOffset);
						}
					}
					if (directVisitCapacity == 0) {
						return false;
					}
					directVisits =
					    (SlipTrackWorldDirectCallbackVisit *)calloc(directVisitCapacity, sizeof(*directVisits));
					if (directVisits == 0) {
						return false;
					}
					if (!SlipTrackWorld_DirectCallbackLoop(
					        component, componentBaseBytes - (size_t)(component - componentBase), componentBase,
					        componentBaseBytes, primitiveCallback, scaledLightX, callbackFunction, callbackUserData,
					        &directEnvironment, 0, 0, directVisits, directVisitCapacity, &directLoop)) {
						free(directVisits);
						return false;
					}
					result->directCallbackLoopExecuted = true;
					result->directCallbackLoopVisitCount = directLoop.visitCount;
					free(directVisits);
				}
			} else if (callbackFunction != 0) {

				SlipTrackWorldDirectCallbackEnvironment inlineEnvironment = {
				    &componentSetup, &componentTail, &primitiveWalker, {(int32_t)x, (int32_t)y, (int32_t)z}, true};
				uint32_t inlineCallbackValue = scaledLightX;
				size_t inlineVisitIndex;

				for (inlineVisitIndex = 0; inlineVisitIndex < primitiveWalker.visitCount &&
				                           inlineVisitIndex < sizeof(primitiveVisits) / sizeof(primitiveVisits[0]);
				     ++inlineVisitIndex) {
					const SlipTrackWorldPrimitiveWalkerVisit *const inlineVisit = &primitiveVisits[inlineVisitIndex];
					bool carryFromInlineVisit = false;

					if (inlineVisit->recordOffset >= componentBaseBytes) {
						return false;
					}
					if (!callbackFunction(componentBase + inlineVisit->recordOffset,
					                      componentBaseBytes - inlineVisit->recordOffset, inlineVisit->recordOffset,
					                      &inlineEnvironment, inlineCallbackValue, callbackUserData,
					                      &inlineCallbackValue, &carryFromInlineVisit)) {
						return false;
					}
				}
			}
			if (componentTail.calledComponentActorDraw && componentActorDraw != 0 &&
			    !componentActorDraw(componentSetup.attachmentListOffset, componentSetup.currentRecord,
			                        componentSetup.drawFlags.rendererFlags,
			                        (SlipView3DVec32){(int32_t)x, (int32_t)y, (int32_t)z},
			                        componentActorDrawUserData)) {
				return false;
			}
		}

		if (componentTail.callDraw3DSetAmbientLight && restoreLight != NULL &&
		    !restoreLight(lightUserData, &componentTail)) {
			return false;
		}

		if (tailVisitCount > 0 && callbackFunction != 0) {
			SlipTrackWorldDirectCallbackEnvironment tailEnvironment = {
			    &componentSetup, &componentTail, 0, {(int32_t)x, (int32_t)y, (int32_t)z}};
			uint32_t tailCallbackValue = scaledLightX;
			size_t tailVisitIndex;

			for (tailVisitIndex = 0; tailVisitIndex < tailVisitCount; ++tailVisitIndex) {
				const SlipTrackWorldComponentTailVisit *const tailVisit = &tailVisits[tailVisitIndex];
				size_t tailVisitOffset;
				bool carryFromTailVisit = false;

				if (tailVisit->primitiveRecord < componentBase ||
				    tailVisit->primitiveRecord >= componentBase + componentBaseBytes) {
					return false;
				}
				tailVisitOffset = (size_t)(tailVisit->primitiveRecord - componentBase);
				if (!callbackFunction(tailVisit->primitiveRecord, componentBaseBytes - tailVisitOffset, tailVisitOffset,
				                      &tailEnvironment, tailCallbackValue, callbackUserData, &tailCallbackValue,
				                      &carryFromTailVisit)) {
					return false;
				}
			}
		}
		if (componentTail.calledRestoreVertexBufferCursor && restoreVertexBuffer != 0) {
			if (!restoreVertexBuffer(restoreVertexBufferUserData)) {
				return false;
			}
			result->vertexBufferRestored = true;
		}
		if (drawStateLoad != 0) {
			if (!drawStateLoad(componentTail.drawStateIndexAfter, drawStateLoadUserData)) {
				return false;
			}
			result->parentDrawStateLoaded = true;
		}
	}
	if (result->remainingCount == 0) {
		result->returned = true;
		result->branch = SLIP_TRACK_WORLD_DEFERRED_ITEM_DIRECT_BRANCH_FINISHED;
	}
	return true;
}

static const SlipTrackWorldDosAddressMap *
SlipTrackWorld_FindHostPointerByDosAddress(const SlipTrackWorldDosAddressMap *map, size_t mapCount,
                                           uint32_t dosAddress) {
	size_t i;

	if (map == 0) {
		return 0;
	}
	for (i = 0; i < mapCount; ++i) {
		if (map[i].dosAddress == dosAddress) {
			return &map[i];
		}
	}
	return 0;
}

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
    const SlipTrackWorldComponentRefuelCalls *refuel, SlipTrackWorldDeferredListDirectExecute *result) {
	SlipTrackWorldDeferredListSetup setup;
	const uint8_t *cursor;
	uint32_t remainingCount;
	uint32_t liveDrawFlags = drawFlagsFrom;
	uint32_t liveVisibleRecordCount = visibleRecordCount;
	uint32_t liveProcessedComponentCount = processedComponentCount;
	size_t visitCount = 0;

	if (result == 0 || !SlipTrackWorld_DeferredListSetup(inputFrameRenderFlags, viewportMinX, viewportMinY,
	                                                     viewportMaxX, viewportMaxY, deferredList, listBytes, &setup)) {
		return false;
	}
	*result = (SlipTrackWorldDeferredListDirectExecute){
	    setup,
	    0,
	    setup.returned,
	    false,
	    false,
	    setup.disabled ? SLIP_TRACK_WORLD_DEFERRED_LIST_DIRECT_EXECUTE_BRANCH_DISABLED
	                   : SLIP_TRACK_WORLD_DEFERRED_LIST_DIRECT_EXECUTE_BRANCH_EMPTY};
	if (setup.branch == SLIP_TRACK_WORLD_DEFERRED_LIST_SETUP_BRANCH_SKIPPED) {
		return true;
	}
	if (setup.callStoreClipBounds && storeClipBoundsFunction != 0 &&
	    !storeClipBoundsFunction(setup.clipMinX, setup.clipMinY, setup.clipMaxX, setup.clipMaxY,
	                             storeClipBoundsUserData)) {
		return false;
	}
	if (visits == 0 || visitCapacity == 0) {
		return false;
	}

	cursor = setup.firstCursor;
	remainingCount = setup.entryCount;
	while (remainingCount != 0) {
		uint32_t entryAddress;
		const SlipTrackWorldDosAddressMap *entryMapped;
		const SlipTrackWorldDosAddressMap *recordMapped;
		SlipTrackWorldDeferredListDirectExecuteVisit *visit;

		if (visitCount >= visitCapacity || cursor < deferredList || (size_t)(cursor - deferredList) > listBytes ||
		    listBytes - (size_t)(cursor - deferredList) < SLIP_TRACK_DEFERRED_REFERENCE_BYTES) {
			return false;
		}
		visit = &visits[visitCount];
		entryAddress = SlipBytes_ReadLE32(cursor);
		entryMapped = SlipTrackWorld_FindHostPointerByDosAddress(entryMap, entryMapCount, entryAddress);
		if (entryMapped == 0 || entryMapped->host == 0 || entryMapped->bytes < SLIP_TRACK_VISIBILITY_ENTRY_BYTES) {
			return false;
		}
		if (!SlipTrackWorld_DeferredItemPrologue(remainingCount, cursor, listBytes - (size_t)(cursor - deferredList),
		                                         entryMapped->host, entryMapped->bytes, viewportMinX, viewportMinY,
		                                         viewportMaxX, viewportMaxY, &visit->prologue)) {
			return false;
		}
		visit->entryAddress = entryAddress;
		visit->recordAddress = visit->prologue.recordAddress;
		if (visit->prologue.callStoreClipBounds && storeClipBoundsFunction != 0 &&
		    !storeClipBoundsFunction(visit->prologue.clipMinX, visit->prologue.clipMinY, visit->prologue.clipMaxX,
		                             visit->prologue.clipMaxY, storeClipBoundsUserData)) {
			return false;
		}
		recordMapped = SlipTrackWorld_FindHostPointerByDosAddress(recordMap, recordMapCount, visit->recordAddress);
		if (recordMapped == 0 || recordMapped->host == 0 || recordMapped->bytes < SLIP_TRD_SECTION_BYTES) {
			return false;
		}
		visit->callTestRecordVisibility = false;
		visit->callPrepareRecordTransform = false;
		visit->callTransformRecordMatrix = false;
		visit->callBuildRecordFacingMatrix = false;
		visit->callTransformRecordCenter = false;
		visit->callCullRecordSphere = false;
		visit->callTrackWorldUpdateDrawFlags = false;
		visit->callDispatchRecordDraw = false;
		visit->callRestoreRecordDrawState = false;
		visit->callTrackWorldDeferredItemDirect = false;
		if (visit->prologue.branch == SLIP_TRACK_WORLD_DEFERRED_ITEM_PROLOGUE_BRANCH_CALLBACK) {
			visit->callTestRecordVisibility = true;
			if (!SlipTrackWorld_RecordVisibility(recordMapped->host, recordMapped->bytes, visit->prologue.viewX,
			                                     visit->prologue.viewY, visit->prologue.viewZ, renderContextIndex, mode,
			                                     minDepth, detailThreshold, &visit->visibility)) {
				return false;
			}
			if (visit->visibility.continues) {
				visit->callPrepareRecordTransform = true;
				if (!SlipTrackWorld_RecordTransformSetup(recordMapped->host, recordMapped->bytes, currentRecordIndex,
				                                         &visit->transformSetup)) {
					return false;
				}
				result->visitCount = visitCount + 1u;
				result->returned = false;
				if (visit->transformSetup.branch == SLIP_TRACK_WORLD_RECORD_MATRIX) {
					visit->callBuildRecordFacingMatrix = true;
					if (!SlipTrackWorld_RecordFacingTransform(recordMapped->host, recordMapped->bytes, cameraWorldX,
					                                          cameraWorldZ, worldMatrix, objectViewMatrix, viewMatrix,
					                                          &visit->facingTransform)) {
						return false;
					}
				} else {
					visit->callTransformRecordMatrix = true;
					if (!SlipTrackWorld_RecordMatrixTransform(recordMapped->host, recordMapped->bytes, worldMatrix,
					                                          objectViewMatrix, viewMatrix, &visit->matrixTransform)) {
						return false;
					}
				}
				visit->callTransformRecordCenter = true;
				if (!SlipTrackWorld_RecordScaledCenter(
				        objectViewMatrix, visit->transformSetup.cachedCenterY, visit->visibility.viewPositionX,
				        visit->visibility.viewPositionY, visit->visibility.viewPositionZ,
				        visit->transformSetup.savedPositionZ, visit->transformSetup.savedPositionYWithCenter,
				        visit->transformSetup.savedPositionX, &visit->scaledCenter)) {
					return false;
				}
				result->stoppedAtSphereCull = true;
				result->branch = SLIP_TRACK_WORLD_DEFERRED_LIST_DIRECT_EXECUTE_BRANCH_SPHERE_CULL_REQUIRED;
				if (sphereCull == 0) {
					return true;
				}
				result->stoppedAtSphereCull = false;
				visit->callCullRecordSphere = true;
				if (!SlipTrackWorld_RecordSphereCull(
				        (SlipView3DVec32){(int32_t)visit->scaledCenter.drawSetup.translationX,
				                          (int32_t)visit->scaledCenter.drawSetup.translationY,
				                          (int32_t)visit->scaledCenter.drawSetup.translationZ},
				        visit->visibility.cachedRadius, sphereCull, sphereCullUserData, liveVisibleRecordCount,
				        visit->scaledCenter.cachedViewCenterZ, &visit->sphereCull)) {
					return false;
				}
				liveVisibleRecordCount = visit->sphereCull.visibleRecordCount;
				if (visit->sphereCull.branch == SLIP_TRACK_WORLD_RECORD_CULLED) {
					visit->callRestoreRecordDrawState = true;
					if (!SlipTrackWorld_RecordDrawRestore(visit->transformSetup.drawStateIndexAfterAdvance,
					                                      &visit->drawRestore)) {
						return false;
					}
					visit->tail =
					    (SlipTrackWorldDeferredItemTail){savedMaximumDepth,
					                                     true,
					                                     savedFadeStart,
					                                     savedFadeEnd,
					                                     savedFadeColour,
					                                     true,
					                                     cursor,
					                                     remainingCount,
					                                     cursor + SLIP_TRACK_DEFERRED_REFERENCE_BYTES,
					                                     remainingCount - 1u,
					                                     false,
					                                     SLIP_TRACK_WORLD_DEFERRED_ITEM_DIRECT_BRANCH_CONTINUE};
					if (visit->tail.remainingCount == 0) {
						visit->tail.returned = true;
						visit->tail.branch = SLIP_TRACK_WORLD_DEFERRED_ITEM_DIRECT_BRANCH_FINISHED;
					}
					++visitCount;
					result->visitCount = visitCount;
					if (visit->tail.returned) {
						result->returned = true;
						result->branch = SLIP_TRACK_WORLD_DEFERRED_LIST_DIRECT_EXECUTE_BRANCH_RETURN;
						return true;
					}
					cursor = visit->tail.nextCursor;
					remainingCount = visit->tail.remainingCount;
					continue;
				}
				visit->callTrackWorldUpdateDrawFlags = true;
				if (!SlipTrackWorld_UpdateDrawFlags(liveDrawFlags, (int32_t)visit->sphereCull.viewDepth, textureMode,
				                                    shading, componentDistance, shadingSecondary, componentRadius,
				                                    &visit->drawFlags)) {
					return false;
				}
				liveDrawFlags = visit->drawFlags.rendererFlags;
				visit->callDispatchRecordDraw = true;
				if (!SlipTrackWorld_RecordDrawDispatch(renderContextIndex, liveDrawFlags,
				                                       visit->transformSetup.branch == SLIP_TRACK_WORLD_RECORD_MATRIX
				                                           ? visit->facingTransform.facingModeFlag
				                                           : visit->matrixTransform.facingModeFlag,
				                                       renderContextIndex == SLIP_TRACK_SPECIAL_RENDER_MODE
				                                           ? (liveDrawFlags & ~SLIP_RENDER_FLAT_SHADING)
				                                           : liveDrawFlags,
				                                       frameRenderFlags, &visit->drawDispatch)) {
					return false;
				}

				if (visit->drawDispatch.callDrawShape) {
					if (shapeDraw == 0) {
						result->stoppedAtShapeDraw = true;
						result->branch = SLIP_TRACK_WORLD_DEFERRED_LIST_DIRECT_EXECUTE_BRANCH_SHAPE_DRAW_REQUIRED;
						return true;
					}
					visit->callDrawShape = true;
					if (!shapeDraw(visit->recordAddress, visit->transformSetup.shapeHandle, recordMapped->host,
					               recordMapped->bytes, worldMatrix, objectViewMatrix,
					               (SlipView3DVec32){(int32_t)visit->scaledCenter.drawSetup.translationX,
					                                 (int32_t)visit->scaledCenter.drawSetup.translationY,
					                                 (int32_t)visit->scaledCenter.drawSetup.translationZ},
					               (SlipView3DVec32){(int32_t)visit->scaledCenter.drawSetup.rotationX,
					                                 (int32_t)visit->scaledCenter.drawSetup.rotationY,
					                                 (int32_t)visit->scaledCenter.drawSetup.rotationZ},
					               renderContextIndex == SLIP_TRACK_SPECIAL_RENDER_MODE
					                   ? (liveDrawFlags & ~SLIP_RENDER_FLAT_SHADING)
					                   : liveDrawFlags,
					               shapeDrawUserData)) {
						return false;
					}
				}
				liveDrawFlags = visit->drawDispatch.restoredFrameRenderFlags;
				visit->callRestoreRecordDrawState = true;
				if (!SlipTrackWorld_RecordDrawRestore(visit->transformSetup.drawStateIndexAfterAdvance,
				                                      &visit->drawRestore)) {
					return false;
				}
				visit->tail = (SlipTrackWorldDeferredItemTail){savedMaximumDepth,
				                                               true,
				                                               savedFadeStart,
				                                               savedFadeEnd,
				                                               savedFadeColour,
				                                               true,
				                                               cursor,
				                                               remainingCount,
				                                               cursor + SLIP_TRACK_DEFERRED_REFERENCE_BYTES,
				                                               remainingCount - 1u,
				                                               false,
				                                               SLIP_TRACK_WORLD_DEFERRED_ITEM_DIRECT_BRANCH_CONTINUE};
				if (visit->tail.remainingCount == 0) {
					visit->tail.returned = true;
					visit->tail.branch = SLIP_TRACK_WORLD_DEFERRED_ITEM_DIRECT_BRANCH_FINISHED;
				}
				++visitCount;
				result->visitCount = visitCount;
				if (visit->tail.returned) {
					result->returned = true;
					result->branch = SLIP_TRACK_WORLD_DEFERRED_LIST_DIRECT_EXECUTE_BRANCH_RETURN;
					return true;
				}
				cursor = visit->tail.nextCursor;
				remainingCount = visit->tail.remainingCount;
				continue;
			}
			visit->tail = (SlipTrackWorldDeferredItemTail){savedMaximumDepth,
			                                               true,
			                                               savedFadeStart,
			                                               savedFadeEnd,
			                                               savedFadeColour,
			                                               true,
			                                               cursor,
			                                               remainingCount,
			                                               cursor + SLIP_TRACK_DEFERRED_REFERENCE_BYTES,
			                                               remainingCount - 1u,
			                                               false,
			                                               SLIP_TRACK_WORLD_DEFERRED_ITEM_DIRECT_BRANCH_CONTINUE};
			if (visit->tail.remainingCount == 0) {
				visit->tail.returned = true;
				visit->tail.branch = SLIP_TRACK_WORLD_DEFERRED_ITEM_DIRECT_BRANCH_FINISHED;
			}
			++visitCount;
			result->visitCount = visitCount;
			if (visit->tail.returned) {
				result->returned = true;
				result->branch = SLIP_TRACK_WORLD_DEFERRED_LIST_DIRECT_EXECUTE_BRANCH_RETURN;
				return true;
			}
			cursor = visit->tail.nextCursor;
			remainingCount = visit->tail.remainingCount;
			continue;
		}

		visit->callTrackWorldDeferredItemDirect = true;
		if (!SlipTrackWorld_DeferredItemDirect(
		        visit->prologue.viewX, visit->prologue.viewY, visit->prologue.viewZ, recordMapped->host,
		        recordMapped->bytes, componentBaseAddress, componentBase, componentBaseBytes, renderContextIndex,
		        liveDrawFlags, textureMode, shading, componentDistance, shadingSecondary, componentRadius,
		        specialRecord, globalAfter, randomState, shadows, liveProcessedComponentCount, currentRecordIndex,
		        actorReplayMode, ambientLightScaleQ14, scaledLightX, scaledLightY, scaledLightZ, directLightScaleQ14,
		        renderContextCount, primitiveCallback, callbackFunction, callbackUserData, componentActorDraw,
		        componentActorDrawUserData, vertexRecords, vertexRecordCapacity, drawStateLoad, drawStateLoadUserData,
		        buildVertexRecords, buildVertexRecordsUserData, restoreVertexBuffer, restoreVertexBufferUserData,
		        savedMaximumDepth, savedFadeStart, savedFadeEnd, savedFadeColour, cursor, remainingCount, scaledLight,
		        restoreLight, lightUserData, refuel, &visit->direct)) {
			++visitCount;
			result->visitCount = visitCount;
			return false;
		}
		if (visit->direct.callTrackWorldComponentSetup) {
			liveDrawFlags = visit->direct.componentSetup.drawFlags.rendererFlags;
			liveProcessedComponentCount = visit->direct.componentSetup.processedComponentCount;
		}
		++visitCount;
		result->visitCount = visitCount;
		if (visit->direct.returned) {
			result->returned = true;
			result->branch = SLIP_TRACK_WORLD_DEFERRED_LIST_DIRECT_EXECUTE_BRANCH_RETURN;
			return true;
		}
		cursor = visit->direct.nextCursor;
		remainingCount = visit->direct.remainingCount;
	}
	result->returned = true;
	result->branch = SLIP_TRACK_WORLD_DEFERRED_LIST_DIRECT_EXECUTE_BRANCH_RETURN;
	return true;
}

bool SlipTrackWorld_DeferredMembership(const uint8_t *deferredList, size_t listBytes, uint32_t recordPointerToFind,
                                       const uint8_t *scanBase, size_t scanBytes,
                                       SlipTrackWorldDeferredMembership *result) {
	uint32_t count;
	uint32_t i;

	if (deferredList == 0 || result == 0 || listBytes < SLIP_TRACK_DEFERRED_LIST_HEADER_BYTES) {
		return false;
	}
	count = *(const uint32_t *)(const void *)deferredList;
	if (count != 0) {
		if (scanBase == 0 || count > (uint32_t)(SIZE_MAX / SLIP_TRACK_DEFERRED_REFERENCE_BYTES) ||
		    scanBytes < (size_t)count * SLIP_TRACK_DEFERRED_REFERENCE_BYTES) {
			return false;
		}
	}
	*result = (SlipTrackWorldDeferredMembership){true, deferredList, count, count == 0, 0,    0,
	                                             0,    false,        false, false,      true, true};
	if (result->zeroCount) {
		result->clearsCarry = true;
		return true;
	}
	result->recordPointerToFind = recordPointerToFind;
	result->scanEntries = scanBase;
	for (i = 0; i < count; ++i) {
		result->scannedDwords = i + 1u;
		if (((const uint32_t *)(const void *)scanBase)[i] == recordPointerToFind) {
			result->recordFound = true;
			result->setsCarry = true;
			return true;
		}
	}
	result->clearsCarry = true;
	return true;
}

bool SlipTrackWorld_DeferredCallbackGate(const uint8_t *record, size_t recordBytesRemaining, uint32_t recordAddress,
                                         SlipView3DVec32 after, const uint8_t *objectList, size_t objectListBytes,
                                         uint32_t deferredEntryActive, uint32_t defaultTraversalGate,
                                         uint16_t renderContextIndex, uint32_t componentBaseAddress,
                                         const uint8_t *componentBase, size_t componentBaseBytes,
                                         uint32_t renderContextCount, SlipTrackWorldDeferredCallbackGate *result) {
	uint32_t count;
	uint32_t i;

	if (record == 0 || objectList == 0 || result == 0 || recordBytesRemaining < SLIP_TRD_SECTION_COMPONENT_END ||
	    objectListBytes < SLIP_TRACK_VISIBILITY_LIST_HEADER_BYTES) {
		return false;
	}
	count = *(const uint32_t *)(const void *)objectList;
	if (count > (uint32_t)((SIZE_MAX - SLIP_TRACK_VISIBILITY_LIST_HEADER_BYTES) / SLIP_TRACK_VISIBILITY_ENTRY_BYTES) ||
	    objectListBytes - SLIP_TRACK_VISIBILITY_LIST_HEADER_BYTES < (size_t)count * SLIP_TRACK_VISIBILITY_ENTRY_BYTES) {
		return false;
	}
	*result =
	    (SlipTrackWorldDeferredCallbackGate){SlipBytes_ReadLE16(record),
	                                         true,
	                                         after,
	                                         objectList,
	                                         count,
	                                         count == 0,
	                                         count == 0 ? 0 : objectList + SLIP_TRACK_VISIBILITY_LIST_HEADER_BYTES,
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
	                                         renderContextCount,
	                                         SLIP_TRACK_WORLD_DEFERRED_CALLBACK_GATE_BRANCH_SKIPPED};
	if (result->zeroCount) {
		return true;
	}
	for (i = 0; i < count; ++i) {
		const uint8_t *const entry =
		    objectList + SLIP_TRACK_VISIBILITY_LIST_HEADER_BYTES + (size_t)i * SLIP_TRACK_VISIBILITY_ENTRY_BYTES;

		result->scannedEntries = i + 1u;
		const SlipTrackVisibilityEntry *const visibility = (const SlipTrackVisibilityEntry *)(const void *)entry;
		if (visibility->recordAddress != recordAddress) {
			continue;
		}
		result->recordFound = true;
		result->matchedEntry = entry;
		result->matchedDeferredEntryActive = deferredEntryActive;
		result->branch = deferredEntryActive == 0
		                     ? SLIP_TRACK_WORLD_DEFERRED_CALLBACK_GATE_BRANCH_EXISTING_DIRECT
		                     : SLIP_TRACK_WORLD_DEFERRED_CALLBACK_GATE_BRANCH_EXISTING_CONTINUATION;
		return true;
	}
	result->unmatchedDeferredEntryActive = deferredEntryActive;
	if (deferredEntryActive == 0) {
		return true;
	}
	result->defaultTraversalGate = defaultTraversalGate;
	if (defaultTraversalGate != 0) {
		return true;
	}
	result->specialModeBits = (uint16_t)(renderContextIndex & SLIP_TRACK_SPECIAL_RENDER_MASK);
	if (result->specialModeBits == 0) {
		uint16_t componentOffset;
		const uint8_t *component;

		if (componentBase == 0) {
			return false;
		}
		componentOffset = SlipBytes_ReadLE16(record + SLIP_TRD_SECTION_COMPONENT_OFFSET);
		if ((size_t)componentOffset > componentBaseBytes ||
		    componentBaseBytes - (size_t)componentOffset < SLIP_TRC_COMPONENT_FLAGS_END) {
			return false;
		}
		component = componentBase + componentOffset;
		result->componentOffset = componentOffset;
		result->componentAddress = componentBaseAddress + (uint32_t)componentOffset;
		result->componentSpecialModeBits = (uint16_t)(SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_FLAGS_OFFSET) &
		                                              SLIP_TRACK_SPECIAL_RENDER_MASK);
		if (result->componentSpecialModeBits != 0) {
			return true;
		}
	}
	if (renderContextCount == 0) {
		return true;
	}
	result->branch = SLIP_TRACK_WORLD_DEFERRED_CALLBACK_GATE_BRANCH_NEW_ENTRY;
	return true;
}

bool SlipTrackWorld_DeferredContinuationGate(uint16_t renderContextIndex, const uint8_t *objectList,
                                             size_t objectListBytes, uint32_t primaryLeft, uint32_t primaryTop,
                                             uint32_t primaryRight, uint32_t primaryBottom,
                                             SlipTrackWorldDeferredContinuationGate *result) {
	if (objectList == 0 || result == 0 || objectListBytes < SLIP_TRACK_VISIBILITY_LIST_HEADER_BYTES) {
		return false;
	}
	*result = (SlipTrackWorldDeferredContinuationGate){renderContextIndex,
	                                                   renderContextIndex == SLIP_TRACK_SPECIAL_RENDER_MODE,
	                                                   0,
	                                                   0,
	                                                   false,
	                                                   0,
	                                                   0,
	                                                   0,
	                                                   0,
	                                                   false,
	                                                   SLIP_TRACK_WORLD_DEFERRED_CONTINUATION_GATE_BRANCH_SKIPPED};
	if (result->isDirectOnlyMode) {
		return true;
	}
	result->objectList = objectList;
	result->objectListCount = *(const uint32_t *)(const void *)objectList;
	result->objectListFull = result->objectListCount == SLIP_TRACK_VISIBILITY_ENTRY_CAPACITY;
	if (result->objectListFull) {
		return true;
	}
	result->clipMinX = primaryLeft;
	result->clipMinY = primaryTop;
	result->clipMaxX = primaryRight;
	result->clipMaxY = primaryBottom;
	result->callStoreClipBounds = true;
	result->branch = SLIP_TRACK_WORLD_DEFERRED_CONTINUATION_GATE_BRANCH_CULL;
	return true;
}

bool SlipTrackWorld_DeferredCullGate(const uint8_t *record, size_t recordBytesRemaining, uint32_t componentBaseAddress,
                                     const uint8_t *componentBase, size_t componentBaseBytes, const uint8_t *savedEntry,
                                     uint32_t savedMaximumDepth, bool trackWorldCullBoundsCarry, uint32_t viewportMinX,
                                     uint32_t viewportMinY, uint32_t viewportMaxX, uint32_t viewportMaxY,
                                     SlipTrackWorldDeferredCullGate *result) {
	uint16_t componentOffset;

	if (record == 0 || componentBase == 0 || savedEntry == 0 || result == 0 ||
	    recordBytesRemaining < SLIP_TRD_SECTION_COMPONENT_END) {
		return false;
	}
	componentOffset = SlipBytes_ReadLE16(record + SLIP_TRD_SECTION_COMPONENT_OFFSET);
	if ((size_t)componentOffset > componentBaseBytes) {
		return false;
	}
	*result = (SlipTrackWorldDeferredCullGate){savedMaximumDepth,
	                                           true,
	                                           savedEntry,
	                                           componentOffset,
	                                           componentBaseAddress + (uint32_t)componentOffset,
	                                           componentBase + componentOffset,
	                                           true,
	                                           trackWorldCullBoundsCarry,
	                                           savedEntry,
	                                           INT32_MAX,
	                                           true,
	                                           true,
	                                           true,
	                                           viewportMinX,
	                                           viewportMinY,
	                                           viewportMaxX,
	                                           viewportMaxY,
	                                           true,
	                                           trackWorldCullBoundsCarry,
	                                           trackWorldCullBoundsCarry
	                                               ? SLIP_TRACK_WORLD_DEFERRED_CULL_GATE_BRANCH_REJECTED
	                                               : SLIP_TRACK_WORLD_DEFERRED_CULL_GATE_BRANCH_VISIBLE};
	return true;
}

bool SlipTrackWorld_DeferredEntryWrite(uint8_t *entryBytes, size_t entryBytesRemaining, uint8_t *objectList,
                                       size_t objectListBytes, uint32_t componentViewX, uint32_t componentViewY,
                                       uint32_t componentViewZ, uint32_t recordAddress, uint32_t primaryLeft,
                                       uint32_t primaryTop, uint32_t primaryRight, uint32_t primaryBottom,
                                       uint32_t objectListCursor, SlipTrackWorldDeferredEntryWrite *result) {
	uint32_t countBefore;

	if (entryBytes == 0 || objectList == 0 || result == 0 || entryBytesRemaining < SLIP_TRACK_VISIBILITY_ENTRY_BYTES ||
	    objectListBytes < SLIP_TRACK_VISIBILITY_LIST_HEADER_BYTES) {
		return false;
	}
	uint32_t *const objectCount = (uint32_t *)(void *)objectList;
	countBefore = *objectCount;
	SlipTrackVisibilityEntry *const entry = (SlipTrackVisibilityEntry *)(void *)entryBytes;
	entry->viewPosition.x = (int32_t)componentViewX;
	entry->viewPosition.y = (int32_t)componentViewY;
	entry->viewPosition.z = (int32_t)componentViewZ;
	entry->recordAddress = recordAddress;
	entry->callbackFlag = 0;
	entry->minX = primaryLeft;
	entry->minY = primaryTop;
	entry->maxX = primaryRight;
	entry->maxY = primaryBottom;
	entry->useClipBounds = UINT32_MAX;
	entry->resetMaximumDepth = 0;
	*objectCount = countBefore + 1u;
	*result = (SlipTrackWorldDeferredEntryWrite){componentViewX,   componentViewY,
	                                             componentViewZ,   componentViewX,
	                                             componentViewY,   componentViewZ,
	                                             recordAddress,    0,
	                                             primaryLeft,      primaryTop,
	                                             primaryRight,     primaryBottom,
	                                             primaryLeft,      primaryTop,
	                                             primaryRight,     primaryBottom,
	                                             UINT32_MAX,       0,
	                                             objectListCursor, objectListCursor + SLIP_TRACK_VISIBILITY_ENTRY_BYTES,
	                                             objectList,       countBefore,
	                                             countBefore + 1u, true};
	return true;
}

bool SlipTrackWorld_DeferredAppendTail(uint8_t *deferredList, size_t listBytes, uint32_t scanBaseAddress,
                                       uint8_t *scanBase, size_t scanBytes, uint32_t storedEntryAddress,
                                       uint32_t storedRecordAddress, SlipTrackWorldDeferredAppendTail *result) {
	uint32_t countBefore;
	uint32_t offset;

	if (deferredList == 0 || scanBase == 0 || result == 0 || listBytes < SLIP_TRACK_DEFERRED_LIST_HEADER_BYTES) {
		return false;
	}
	countBefore = *(const uint32_t *)(const void *)deferredList;
	offset = countBefore << SLIP_TRACK_DEFERRED_REFERENCE_SHIFT;
	if (listBytes - SLIP_TRACK_DEFERRED_LIST_HEADER_BYTES < (size_t)offset + SLIP_TRACK_DEFERRED_REFERENCE_BYTES ||
	    scanBytes < (size_t)offset + SLIP_TRACK_DEFERRED_REFERENCE_BYTES) {
		return false;
	}
	*(uint32_t *)(void *)deferredList = countBefore + 1u;
	((uint32_t *)(void *)deferredList)[1u + offset / SLIP_TRACK_DEFERRED_REFERENCE_BYTES] = storedEntryAddress;
	((uint32_t *)(void *)scanBase)[offset / SLIP_TRACK_DEFERRED_REFERENCE_BYTES] = storedRecordAddress;
	*result = (SlipTrackWorldDeferredAppendTail){
	    deferredList,        countBefore, countBefore + 1u, offset, storedEntryAddress, scanBaseAddress + offset,
	    storedRecordAddress, true};
	return true;
}

bool SlipTrackWorld_DeferredExistingEntry(bool enter, uint8_t *savedEntry, size_t entryBytesRemaining,
                                          const uint8_t *record, size_t recordBytesRemaining,
                                          uint32_t componentBaseAddress, const uint8_t *componentBase,
                                          size_t componentBaseBytes, const uint8_t *objectList, size_t objectListBytes,
                                          uint32_t renderContextCount, uint32_t primaryLeft, uint32_t primaryTop,
                                          uint32_t primaryRight, uint32_t primaryBottom, bool trackWorldCullBoundsCarry,
                                          uint32_t viewportMinX, uint32_t viewportMinY, uint32_t viewportMaxX,
                                          uint32_t viewportMaxY, SlipTrackWorldDeferredExistingEntry *result) {
	uint32_t entryUseClipBounds;

	if (savedEntry == 0 || result == 0 || entryBytesRemaining < SLIP_TRACK_VISIBILITY_CLIP_BOUNDS_END) {
		return false;
	}
	if (enter && (objectList == 0 || objectListBytes < SLIP_TRACK_VISIBILITY_LIST_HEADER_BYTES)) {
		return false;
	}
	SlipTrackVisibilityEntry *const entry = (SlipTrackVisibilityEntry *)(void *)savedEntry;
	entryUseClipBounds = entry->useClipBounds;
	*result = (SlipTrackWorldDeferredExistingEntry){enter,
	                                                enter ? UINT32_MAX : 0,
	                                                enter ? objectList : 0,
	                                                enter ? *(const uint32_t *)(const void *)objectList : 0,
	                                                enter ? *(const uint32_t *)(const void *)objectList : 0,
	                                                entryUseClipBounds,
	                                                0,
	                                                0,
	                                                0,
	                                                0,
	                                                0,
	                                                false,
	                                                0,
	                                                0,
	                                                0,
	                                                0,
	                                                false,
	                                                false,
	                                                0,
	                                                false,
	                                                0,
	                                                0,
	                                                0,
	                                                0,
	                                                false,
	                                                false,
	                                                0,
	                                                SLIP_TRACK_WORLD_DEFERRED_EXISTING_ENTRY_BRANCH_UNCHANGED};
	if (entryUseClipBounds != 0) {
		return true;
	}
	result->renderContextCount = renderContextCount;
	if (renderContextCount == 0) {
		entry->useClipBounds = UINT32_MAX;
		result->storedUseClipBounds = UINT32_MAX;
		result->branch = SLIP_TRACK_WORLD_DEFERRED_EXISTING_ENTRY_BRANCH_UNCLIPPED;
		return true;
	}
	if (record == 0 || componentBase == 0 || recordBytesRemaining < SLIP_TRD_SECTION_COMPONENT_END) {
		return false;
	}
	result->cullClipMinX = primaryLeft;
	result->cullClipMinY = primaryTop;
	result->cullClipMaxX = primaryRight;
	result->cullClipMaxY = primaryBottom;
	result->callStoreCullClipBounds = true;
	result->savedEntry = savedEntry;
	result->componentOffset = SlipBytes_ReadLE16(record + SLIP_TRD_SECTION_COMPONENT_OFFSET);
	if ((size_t)result->componentOffset > componentBaseBytes) {
		return false;
	}
	result->componentAddress = componentBaseAddress + (uint32_t)result->componentOffset;
	result->component = componentBase + result->componentOffset;
	result->callTrackWorldCullBounds = true;
	result->trackWorldCullBoundsCarry = trackWorldCullBoundsCarry;
	result->restoredEntry = savedEntry;
	result->savedCullFlags = true;
	result->restoredClipMinX = viewportMinX;
	result->restoredClipMinY = viewportMinY;
	result->restoredClipMaxX = viewportMaxX;
	result->restoredClipMaxY = viewportMaxY;
	result->callRestoreClipBounds = true;
	result->restoredCullFlags = true;
	if (trackWorldCullBoundsCarry) {
		entry->useClipBounds = UINT32_MAX;
		result->storedUseClipBounds = UINT32_MAX;
		result->branch = SLIP_TRACK_WORLD_DEFERRED_EXISTING_ENTRY_BRANCH_UNCLIPPED;
	}
	return true;
}

bool SlipTrackWorld_DeferredCallbackHeader(uint32_t deferredEntryActive, uint32_t renderContextCount, uint32_t viewX,
                                           uint32_t viewY, uint32_t viewZ, const uint8_t *objectList,
                                           size_t objectListBytes, uint32_t primaryLeft, uint32_t primaryTop,
                                           uint32_t primaryRight, uint32_t primaryBottom,
                                           SlipTrackWorldDeferredCallbackHeader *result) {
	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldDeferredCallbackHeader){deferredEntryActive,
	                                                 0,
	                                                 0,
	                                                 0,
	                                                 0,
	                                                 0,
	                                                 0,
	                                                 false,
	                                                 0,
	                                                 0,
	                                                 0,
	                                                 0,
	                                                 false,
	                                                 SLIP_TRACK_WORLD_DEFERRED_CALLBACK_HEADER_BRANCH_SKIPPED};
	if (deferredEntryActive == 0) {
		return true;
	}
	result->renderContextCount = renderContextCount;
	if (renderContextCount == 0) {
		return true;
	}
	if (objectList == 0 || objectListBytes < SLIP_TRACK_VISIBILITY_LIST_HEADER_BYTES) {
		return false;
	}
	result->viewX = viewX;
	result->viewY = viewY;
	result->viewZ = viewZ;
	result->objectList = objectList;
	result->objectListCount = *(const uint32_t *)(const void *)objectList;
	result->objectListFull = result->objectListCount == SLIP_TRACK_VISIBILITY_ENTRY_CAPACITY;
	if (result->objectListFull) {
		return true;
	}
	result->clipMinX = primaryLeft;
	result->clipMinY = primaryTop;
	result->clipMaxX = primaryRight;
	result->clipMaxY = primaryBottom;
	result->callStoreClipBounds = true;
	result->branch = SLIP_TRACK_WORLD_DEFERRED_CALLBACK_HEADER_BRANCH_CULL;
	return true;
}

bool SlipTrackWorld_DeferredCallbackCull(const uint8_t *record, size_t recordBytesRemaining, uint32_t savedMaximumDepth,
                                         uint32_t viewX, uint32_t viewY, uint32_t viewZ,
                                         bool trackWorldIndirectCullCarry, uint32_t viewportMinX, uint32_t viewportMinY,
                                         uint32_t viewportMaxX, uint32_t viewportMaxY,
                                         SlipTrackWorldDeferredCallbackCull *result) {
	if (record == 0 || result == 0 || recordBytesRemaining < SLIP_TRACK_CALLBACK_CULL_RADIUS_END) {
		return false;
	}
	*result = (SlipTrackWorldDeferredCallbackCull){savedMaximumDepth,
	                                               true,
	                                               viewX,
	                                               viewY,
	                                               viewZ,
	                                               SlipBytes_ReadLE32(record + SLIP_TRACK_CALLBACK_CULL_RADIUS_OFFSET),
	                                               true,
	                                               trackWorldIndirectCullCarry,
	                                               INT32_MAX,
	                                               true,
	                                               true,
	                                               true,
	                                               viewportMinX,
	                                               viewportMinY,
	                                               viewportMaxX,
	                                               viewportMaxY,
	                                               true,
	                                               trackWorldIndirectCullCarry,
	                                               trackWorldIndirectCullCarry
	                                                   ? SLIP_TRACK_WORLD_DEFERRED_CALLBACK_CULL_BRANCH_REJECTED
	                                                   : SLIP_TRACK_WORLD_DEFERRED_CALLBACK_CULL_BRANCH_VISIBLE};
	return true;
}

bool SlipTrackWorld_DeferredCallbackWrite(uint8_t *inputEntry, size_t entryBytesRemaining, uint32_t entryAddress,
                                          uint8_t *objectList, size_t objectListBytes, uint8_t *inputDeferredList,
                                          size_t listBytes, uint32_t scanBaseAddress, uint8_t *scanBase,
                                          size_t scanBytes, uint32_t x, uint32_t y, uint32_t z, uint32_t recordAddress,
                                          uint32_t primaryLeft, uint32_t primaryTop, uint32_t primaryRight,
                                          uint32_t primaryBottom, uint32_t objectListCursor,
                                          SlipTrackWorldDeferredCallbackWrite *result) {
	uint32_t objectCountBefore;
	uint32_t listCountBefore;
	uint32_t listOffset;

	if (inputEntry == 0 || objectList == 0 || inputDeferredList == 0 || scanBase == 0 || result == 0 ||
	    entryBytesRemaining < SLIP_TRACK_VISIBILITY_ENTRY_BYTES ||
	    objectListBytes < SLIP_TRACK_VISIBILITY_LIST_HEADER_BYTES ||
	    listBytes < SLIP_TRACK_DEFERRED_LIST_HEADER_BYTES) {
		return false;
	}
	uint32_t *const objectCount = (uint32_t *)(void *)objectList;
	uint32_t *const deferredList = (uint32_t *)(void *)inputDeferredList;
	uint32_t *const recordReferences = (uint32_t *)(void *)scanBase;
	listCountBefore = deferredList[0];
	listOffset = listCountBefore << SLIP_TRACK_DEFERRED_REFERENCE_SHIFT;
	if (listBytes - SLIP_TRACK_DEFERRED_LIST_HEADER_BYTES < (size_t)listOffset + SLIP_TRACK_DEFERRED_REFERENCE_BYTES ||
	    scanBytes < (size_t)listOffset + SLIP_TRACK_DEFERRED_REFERENCE_BYTES) {
		return false;
	}
	objectCountBefore = *objectCount;
	SlipTrackVisibilityEntry *const entry = (SlipTrackVisibilityEntry *)(void *)inputEntry;
	entry->viewPosition.x = (int32_t)x;
	entry->viewPosition.y = (int32_t)y;
	entry->viewPosition.z = (int32_t)z;
	entry->recordAddress = recordAddress;
	entry->callbackFlag = UINT32_MAX;
	entry->minX = primaryLeft;
	entry->minY = primaryTop;
	entry->maxX = primaryRight;
	entry->maxY = primaryBottom;
	entry->useClipBounds = UINT32_MAX;
	entry->resetMaximumDepth = 0;
	*objectCount = objectCountBefore + 1u;
	deferredList[0] = listCountBefore + 1u;
	deferredList[1u + listOffset / sizeof(*deferredList)] = entryAddress;
	recordReferences[listOffset / sizeof(*recordReferences)] = recordAddress;
	*result = (SlipTrackWorldDeferredCallbackWrite){entryAddress,
	                                                inputEntry,
	                                                x,
	                                                y,
	                                                z,
	                                                x,
	                                                y,
	                                                z,
	                                                recordAddress,
	                                                UINT32_MAX,
	                                                primaryLeft,
	                                                primaryTop,
	                                                primaryRight,
	                                                primaryBottom,
	                                                primaryLeft,
	                                                primaryTop,
	                                                primaryRight,
	                                                primaryBottom,
	                                                UINT32_MAX,
	                                                0,
	                                                objectList,
	                                                objectCountBefore,
	                                                objectCountBefore + 1u,
	                                                objectListCursor,
	                                                objectListCursor + SLIP_TRACK_VISIBILITY_ENTRY_BYTES,
	                                                inputDeferredList,
	                                                listCountBefore,
	                                                listCountBefore + 1u,
	                                                listOffset,
	                                                entryAddress,
	                                                scanBaseAddress + listOffset,
	                                                recordAddress,
	                                                true};
	return true;
}

enum {
	SLIP_TRACK_CELL_COUNT_X = 12,
	SLIP_TRACK_CELL_COUNT_Y = 4,
	SLIP_TRACK_CELL_COUNT_Z = 20,
	SLIP_TRACK_CELL_ROW_STRIDE = SLIP_TRACK_CELL_COUNT_X,
	SLIP_TRACK_CELL_PLANE_STRIDE = SLIP_TRACK_CELL_COUNT_X * SLIP_TRACK_CELL_COUNT_Y,
	SLIP_TRACK_CELL_REFERENCE_BYTES = sizeof(uint32_t),
	SLIP_TRACK_CELL_REFERENCE_SHIFT = 2,
	SLIP_TRACK_CELL_TABLE_CLEAR_WORDS = SLIP_TRACK_WORLD_CELL_TABLE_BYTES / sizeof(uint16_t),
	SLIP_TRACK_CELL_SOURCE_LIST_HEADER_BYTES = sizeof(uint16_t),
	SLIP_TRACK_CELL_SOURCE_RECORD_BYTES = 24,
	SLIP_TRACK_CELL_SOURCE_TABLE_VALUE_OFFSET = 6,
	SLIP_TRACK_CELL_SOURCE_COORDINATE_OFFSET = 8,
	SLIP_TRACK_CELL_COORDINATE_BYTES = 6,
	SLIP_TRACK_CELL_COORDINATE_Y_OFFSET = 2,
	SLIP_TRACK_CELL_COORDINATE_Z_OFFSET = 4,
	SLIP_TRACK_CELL_COORDINATE_DIVISOR = 12,
	SLIP_TRACK_CELL_COORDINATE_Y_BIAS = 12,
	SLIP_TRACK_CELL_COORDINATE_Z_BIAS = 18,
	SLIP_TRACK_CELL_WORLD_SHIFT = 20,
	SLIP_TRACK_CELL_INDEX_UPPER_WORD_MASK = SLIP_TRACK_REGISTER_UPPER_WORD_MASK,
	SLIP_TRACK_SPECIAL_AMBIENT_Q14 = SLIP_Q14_ONE / 4,
	SLIP_TRACK_SPECIAL_TRAVERSAL_DOS_TOKEN = 0x3bf72,
	SLIP_TRACK_SPECIAL_FADE_START = 244000,
	SLIP_TRACK_SPECIAL_FADE_END = 585600,
	SLIP_TRACK_SPECIAL_MAXIMUM_DEPTH = 976000
};

bool SlipTrackWorld_CellTableBuild(uint8_t *table, size_t tableBytes, const uint8_t *trkBase, size_t trkSize,
                                   uint32_t savedVectorParameter, uint32_t tableValueBase,
                                   SlipTrackWorldCellTableVisit *visits, size_t visitCapacity,
                                   SlipTrackWorldCellTableBuild *result) {
	uint16_t listOffset;
	uint16_t recordCount;
	size_t recordsOffset;
	uint16_t i;

	if (table == 0 || tableBytes < SLIP_TRACK_WORLD_CELL_TABLE_BYTES || result == 0) {
		return false;
	}
	for (i = 0; i < SLIP_TRACK_CELL_TABLE_CLEAR_WORDS; ++i) {
		table[(size_t)i * sizeof(uint16_t)] = 0;
		table[(size_t)i * sizeof(uint16_t) + 1u] = 0;
	}
	*result = (SlipTrackWorldCellTableBuild){table,
	                                         SLIP_TRACK_CELL_TABLE_CLEAR_WORDS,
	                                         0,
	                                         trkBase,
	                                         0,
	                                         0,
	                                         0,
	                                         0,
	                                         SLIP_TRACK_WORLD_CELL_TABLE_BUILD_BRANCH_EMPTY,
	                                         true};
	if (trkBase == 0 || trkSize < SLIP_TRK_ROOT_LIST_END) {
		return false;
	}
	listOffset = SlipBytes_ReadLE16(trkBase + SLIP_TRK_ROOT_LIST_OFFSET);
	result->recordListOffset = listOffset;
	if (listOffset == 0) {
		return true;
	}
	if ((size_t)listOffset + SLIP_TRACK_CELL_SOURCE_LIST_HEADER_BYTES > trkSize) {
		return false;
	}
	result->recordList = trkBase + listOffset;
	recordCount = SlipBytes_ReadLE16(trkBase + listOffset);
	result->recordCount = recordCount;
	result->branch = SLIP_TRACK_WORLD_CELL_TABLE_BUILD_BRANCH_LIST;
	if (recordCount == 0 || visits == 0 || visitCapacity < recordCount) {
		return false;
	}
	recordsOffset = (size_t)listOffset + SLIP_TRACK_CELL_SOURCE_LIST_HEADER_BYTES;
	if ((size_t)recordCount > (SIZE_MAX - recordsOffset) / SLIP_TRACK_CELL_SOURCE_RECORD_BYTES ||
	    recordsOffset + (size_t)recordCount * SLIP_TRACK_CELL_SOURCE_RECORD_BYTES > trkSize) {
		return false;
	}
	for (i = 0; i < recordCount; ++i) {
		const uint8_t *const record = trkBase + recordsOffset + (size_t)i * SLIP_TRACK_CELL_SOURCE_RECORD_BYTES;
		const uint16_t tableValueOffset = SlipBytes_ReadLE16(record + SLIP_TRACK_CELL_SOURCE_TABLE_VALUE_OFFSET);
		SlipTrackWorldCellTableVisit *const visit = &visits[i];

		*visit =
		    (SlipTrackWorldCellTableVisit){record,
		                                   (uint32_t)(recordsOffset + (size_t)i * SLIP_TRACK_CELL_SOURCE_RECORD_BYTES),
		                                   (uint16_t)(recordCount - i),
		                                   tableValueOffset,
		                                   tableValueOffset == 0,
		                                   0,
		                                   {0},
		                                   0,
		                                   0,
		                                   0,
		                                   0,
		                                   0,
		                                   {0}};
		if (tableValueOffset != 0) {
			const uint16_t coordinateOffset = SlipBytes_ReadLE16(record + SLIP_TRACK_CELL_SOURCE_COORDINATE_OFFSET);
			uint32_t cellX;
			uint32_t cellY;
			uint32_t cellZ;
			uint32_t tableValueAddress;

			visit->coordinateOffsetField = coordinateOffset;
			if (!SlipTrackWorld_RecordVector(trkBase, trkSize, coordinateOffset, savedVectorParameter,
			                                 &visit->decodedCoordinate)) {
				return false;
			}
			cellX = SlipTrackWorld_SignedShiftRight20(visit->decodedCoordinate.coordinateX);
			cellY = SlipTrackWorld_SignedShiftRight20(visit->decodedCoordinate.coordinateY);
			cellZ = SlipTrackWorld_SignedShiftRight20(visit->decodedCoordinate.coordinateZ);
			visit->cellX = cellX;
			visit->cellY = cellY;
			visit->cellZ = cellZ;
			visit->tableValueOffset = tableValueOffset;
			tableValueAddress = tableValueBase + (uint32_t)tableValueOffset;
			visit->tableValueAddress = tableValueAddress;
			if (!SlipTrackWorld_TableStore(table, tableBytes, cellX, (uint16_t)cellY, (uint16_t)cellZ,
			                               tableValueAddress, &visit->tableStore)) {
				return false;
			}
		}
		result->visitsStored = (uint16_t)(i + 1u);
	}
	return true;
}

bool SlipTrackWorld_FrameDispatch(uint32_t renderContext, uint32_t primaryLeft, uint32_t primaryTop,
                                  uint32_t primaryRight, uint32_t primaryBottom, uint16_t renderMode,
                                  uint32_t underSeaColor, SlipTrackWorldTraversalCallback traversalCallback,
                                  uint32_t savedMaximumDepth, uint32_t savedFadeStart, uint32_t savedFadeEnd,
                                  uint32_t savedFadeColour, SlipRaceTrackFrameCallback frameCallback,
                                  uint32_t defaultTraversalGate, uint8_t *trkBase, size_t trkSize,
                                  SlipTrackWorldTraversalContext *traversalContext,
                                  const SlipRaceTrackFrameCallbackExecuteArgs *callbackArgs,
                                  SlipTrackWorldFrameDispatch *result) {
	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldFrameDispatch){.renderContext = renderContext,
	                                        .skippedByNoContext = renderContext == 0,
	                                        .frameCallback = frameCallback,
	                                        .defaultTraversalGate = defaultTraversalGate,
	                                        .branch = SLIP_TRACK_WORLD_FRAME_DISPATCH_BRANCH_NO_CONTEXT,
	                                        .ret = true};
	if (renderContext == 0) {
		return true;
	}
	result->restoredClipLeft = primaryLeft;
	result->restoredClipTop = primaryTop;
	result->restoredClipRight = primaryRight;
	result->restoredClipBottom = primaryBottom;
	result->callRestoreClipBounds = true;

	if (traversalContext != NULL && traversalContext->storeClipBounds != NULL &&
	    !traversalContext->storeClipBounds(primaryLeft, primaryTop, primaryRight, primaryBottom,
	                                       traversalContext->storeClipBoundsUserData))
		return false;
	result->mode = renderMode;
	if (renderMode == SLIP_TRACK_SPECIAL_RENDER_MODE) {
		bool traversalSucceeded = true;

		result->fillClipLeft = primaryLeft;
		result->fillClipTop = primaryTop;
		result->fillClipRight = primaryRight;
		result->fillClipBottom = primaryBottom;
		result->fillColour = underSeaColor;
		result->callFillClipRect = true;
		Raster_FillRectClipped((uint8_t)underSeaColor, (int16_t)primaryLeft, (int16_t)primaryTop, (int16_t)primaryRight,
		                       (int16_t)primaryBottom);
		result->lightDirectionX = 0;
		result->lightDirectionZ = 0;
		result->lightDirectionY = -SLIP_Q14_ONE;
		result->directLightScaleQ14 = SLIP_Q14_ONE;
		result->callDraw3DSetLightVector = true;
		SlipDraw3D_SetLightVector(0, -SLIP_Q14_ONE, 0, SLIP_Q14_ONE);
		result->ambientLightScaleQ14 = SLIP_TRACK_SPECIAL_AMBIENT_Q14;
		result->callDraw3DSetAmbientLight = true;
		SlipDraw3D_SetAmbientLight(SLIP_TRACK_SPECIAL_AMBIENT_Q14);
		result->savedCallback = traversalCallback;
		result->specialTraversalCallbackAddress = SLIP_TRACK_SPECIAL_TRAVERSAL_DOS_TOKEN;
		result->specialFadeColour = 0;
		result->specialFadeStart = SLIP_TRACK_SPECIAL_FADE_START;
		result->specialFadeEnd = SLIP_TRACK_SPECIAL_FADE_END;
		result->callDisableDepthFade = true;
		SlipDraw3D_SetDepthFade(SLIP_TRACK_SPECIAL_FADE_START, SLIP_TRACK_SPECIAL_FADE_END, 0);
		result->specialMaximumDepth = SLIP_TRACK_SPECIAL_MAXIMUM_DEPTH;
		result->callSetSpecialMaximumDepth = true;
		SlipDraw3D_SetMaximumDepth(SLIP_TRACK_SPECIAL_MAXIMUM_DEPTH);

		if (traversalContext != NULL) {
			traversalContext->maxZ = SLIP_TRACK_SPECIAL_MAXIMUM_DEPTH;
			if (traversalContext->projectState != NULL)
				traversalContext->projectState->maxZ = SLIP_TRACK_SPECIAL_MAXIMUM_DEPTH;
			if (traversalContext->frustum != NULL)
				traversalContext->frustum->maxZ = SLIP_TRACK_SPECIAL_MAXIMUM_DEPTH;
		}
		result->callSpecialTraversal = true;
		if (trkBase != NULL) {
			traversalSucceeded =
			    SlipTrackWorld_TraversalEntry(trkBase, trkSize, traversalContext, &result->traversalEntry);
		}
		result->restoredMaximumDepth = savedMaximumDepth;
		result->callRestoreMaximumDepth = true;
		SlipDraw3D_SetMaximumDepth(savedMaximumDepth);

		if (traversalContext != NULL) {
			traversalContext->maxZ = (int32_t)savedMaximumDepth;
			if (traversalContext->projectState != NULL)
				traversalContext->projectState->maxZ = (int32_t)savedMaximumDepth;
			if (traversalContext->frustum != NULL)
				traversalContext->frustum->maxZ = (int32_t)savedMaximumDepth;
		}
		result->restoredFadeStart = savedFadeStart;
		result->restoredFadeEnd = savedFadeEnd;
		result->restoredFadeColour = savedFadeColour;
		result->callRestoreDepthFade = true;
		SlipDraw3D_SetDepthFade(savedFadeStart, savedFadeEnd, (uint16_t)savedFadeColour);
		result->restoredCallback = traversalCallback;
		result->branch = SLIP_TRACK_WORLD_FRAME_DISPATCH_BRANCH_SPECIAL_MODE_10;
		return traversalSucceeded;
	}
	result->frameCallback = frameCallback;
	result->callCallback = frameCallback != NULL;
	if (frameCallback != NULL) {
		result->callbackSupported = true;
		result->unsupportedCallback = false;
		if (callbackArgs != NULL) {
			result->callbackExecuted = true;

			SlipRaceTrackFrameCallbackExecuteArgs background = *callbackArgs;
			background.minX = (int16_t)primaryLeft;
			background.minY = (uint16_t)primaryTop;
			background.maxX = (uint16_t)primaryRight;
			background.maxY = (uint16_t)primaryBottom;
			if (!frameCallback(&background, &result->callback)) {
				return false;
			}
		}
	}
	result->defaultTraversalGate = defaultTraversalGate;
	result->callFallback = defaultTraversalGate == 0;
	if (result->callFallback && trkBase != NULL &&
	    !SlipTrackWorld_TraversalEntry(trkBase, trkSize, traversalContext, &result->traversalEntry)) {
		return false;
	}
	result->branch = SLIP_TRACK_WORLD_FRAME_DISPATCH_BRANCH_CALLBACK_PATH;
	return true;
}

bool SlipTrackWorld_TableLookup(const uint8_t *table, size_t tableBytes, uint16_t cellX, uint16_t cellY, uint16_t cellZ,
                                SlipTrackWorldTableLookup *result) {
	uint16_t yStride;
	uint16_t xIndex;
	uint32_t cellByteOffset;

	if (result == 0) {
		return false;
	}
	*result = (SlipTrackWorldTableLookup){
	    cellX, cellY, cellZ, cellX >= SLIP_TRACK_CELL_COUNT_X,         false, false, 0, 0, 0, 0, 0, 0, 0, 0,
	    0,     table, 0,     SLIP_TRACK_WORLD_TABLE_LOOKUP_BRANCH_ZERO};
	if (cellX >= SLIP_TRACK_CELL_COUNT_X) {
		return true;
	}
	result->yOutsideTable = cellY >= SLIP_TRACK_CELL_COUNT_Y;
	if (cellY >= SLIP_TRACK_CELL_COUNT_Y) {
		return true;
	}
	result->zOutsideTable = cellZ >= SLIP_TRACK_CELL_COUNT_Z;
	if (cellZ >= SLIP_TRACK_CELL_COUNT_Z) {
		return true;
	}
	if (table == 0) {
		return false;
	}
	xIndex = cellX;
	result->xIndex = xIndex;
	yStride = SLIP_TRACK_CELL_ROW_STRIDE;
	result->yStride = yStride;
	yStride = (uint16_t)(yStride * cellY);
	result->yIndex = yStride;
	xIndex = (uint16_t)(xIndex + yStride);
	result->xyIndex = xIndex;
	yStride = SLIP_TRACK_CELL_PLANE_STRIDE;
	result->zStride = yStride;
	yStride = (uint16_t)(yStride * cellZ);
	result->zIndex = yStride;
	xIndex = (uint16_t)(xIndex + yStride);
	result->cellIndex = xIndex;
	xIndex = (uint16_t)(xIndex << SLIP_TRACK_CELL_REFERENCE_SHIFT);
	result->cellByteOffsetWord = xIndex;
	cellByteOffset = (uint32_t)xIndex;
	result->cellByteOffset = cellByteOffset;
	if (tableBytes < (size_t)cellByteOffset + SLIP_TRACK_CELL_REFERENCE_BYTES) {
		return false;
	}
	result->recordAddress = SlipBytes_ReadLE32(table + cellByteOffset);
	result->branch = SLIP_TRACK_WORLD_TABLE_LOOKUP_BRANCH_VALID;
	return true;
}

bool SlipTrackWorld_RemoveObjectSlot(uint32_t objectToken, uint16_t objectOffset, uint16_t trackHandle,
                                     uint8_t *slotDrawBase, size_t slotDrawBytes, uint32_t slotDrawBaseAddress,
                                     uint32_t slotDrawFreeListAddress, uint8_t *trdBase, size_t trackDataSize,
                                     uint32_t trdBaseAddress, uint8_t *slotListBase, size_t slotListBytes,
                                     uint32_t slotListBaseAddress, uint32_t slotListFreeListAddress,
                                     const SlipObject *objectTable, size_t objectTableBytes) {
	SlipTrackWorldSlotListSelect select;
	uint32_t slotOffset;

	if (!SlipTrackWorld_SelectSlotListEntry(objectToken, slotListBaseAddress, objectTable, objectTableBytes,
	                                        objectOffset, &select)) {
		return false;
	}
	if (select.carry) {
		return true;
	}
	if (!SlipTrackWorld_DosAddressToOffset(select.slotAddress, slotListBaseAddress, slotListBytes, &slotOffset)) {
		return false;
	}
	return SlipTrackWorld_FreeSlotListEntry(trackHandle, slotDrawBase, slotDrawBytes, slotDrawBaseAddress,
	                                        slotDrawFreeListAddress, trdBase, trackDataSize, trdBaseAddress,
	                                        slotListBase, slotListBytes, slotListBaseAddress, slotListFreeListAddress,
	                                        slotListBase + slotOffset);
}

enum {
	SLIP_TRACK_SLOT_RADIUS_PADDING = 200,
	SLIP_TRACK_SLOT_MAXIMUM_RADIUS = 8784,
	SLIP_TRACK_SLOT_ALLOCATION_ERROR = 7
};

bool SlipTrackWorld_FreeSlotListEntry(uint16_t trackHandle, uint8_t *slotDrawBase, size_t slotDrawBytes,
                                      uint32_t slotDrawBaseAddress, uint32_t slotDrawFreeListAddress, uint8_t *trdBase,
                                      size_t trackDataSize, uint32_t trdBaseAddress, uint8_t *slotListBase,
                                      size_t slotListBytes, uint32_t slotListBaseAddress,
                                      uint32_t slotListFreeListAddress, uint8_t *inputSlot) {
	SlipTrackWorldOwnerDrawLinksClear clear;
	uint32_t slotAddress;
	uint32_t nextAddress;
	uint32_t previousSlotAddress;
	uint32_t freeNextAddress;
	uint32_t nextOffset;
	uint32_t previousOffset;
	uint32_t freeListOffset;
	uint32_t freeNextOffset;
	SlipTrackSlotRecord *slot;
	SlipTrackSlotRecord *next;
	SlipTrackSlotRecord *previous;
	SlipTrackSlotRecord *freeHead;
	SlipTrackSlotRecord *freeNext;

	if (slotListBase == NULL || inputSlot == NULL || inputSlot < slotListBase ||
	    (size_t)(inputSlot - slotListBase) + SLIP_TRACK_SLOT_LINKS_END > slotListBytes ||
	    !SlipTrackWorld_ClearOwnerDrawLinks(trackHandle, slotDrawBase, slotDrawBytes, slotDrawBaseAddress,
	                                        slotDrawFreeListAddress, trdBase, trackDataSize, trdBaseAddress, inputSlot,
	                                        slotListBytes - (size_t)(inputSlot - slotListBase), &clear)) {
		return false;
	}
	slotAddress = slotListBaseAddress + (uint32_t)(inputSlot - slotListBase);
	slot = (SlipTrackSlotRecord *)(void *)inputSlot;
	nextAddress = slot->nextSlotAddress;
	previousSlotAddress = slot->previousSlotAddress;
	if (!SlipTrackWorld_DosAddressToOffset(nextAddress, slotListBaseAddress, slotListBytes, &nextOffset) ||
	    !SlipTrackWorld_DosAddressToOffset(previousSlotAddress, slotListBaseAddress, slotListBytes, &previousOffset) ||
	    !SlipTrackWorld_DosAddressToOffset(slotListFreeListAddress, slotListBaseAddress, slotListBytes,
	                                       &freeListOffset)) {
		return false;
	}
	previous = (SlipTrackSlotRecord *)(void *)(slotListBase + previousOffset);
	previous->nextSlotAddress = nextAddress;
	next = (SlipTrackSlotRecord *)(void *)(slotListBase + nextOffset);
	next->previousSlotAddress = previousSlotAddress;
	freeHead = (SlipTrackSlotRecord *)(void *)(slotListBase + freeListOffset);
	freeNextAddress = freeHead->nextSlotAddress;
	if (!SlipTrackWorld_DosAddressToOffset(freeNextAddress, slotListBaseAddress, slotListBytes, &freeNextOffset)) {
		return false;
	}
	freeHead->nextSlotAddress = slotAddress;
	freeNext = (SlipTrackSlotRecord *)(void *)(slotListBase + freeNextOffset);
	freeNext->previousSlotAddress = slotAddress;
	slot->nextSlotAddress = freeNextAddress;
	slot->previousSlotAddress = slotListFreeListAddress;
	return true;
}

static bool SlipTrackWorld_RecordFromDosAddress(const uint8_t *trdBase, size_t trackDataSize, uint32_t trdBaseAddress,
                                                uint32_t recordAddress, const uint8_t **record) {
	uint32_t offset;

	if (recordAddress < trdBaseAddress)
		return false;
	offset = recordAddress - trdBaseAddress;
	if ((size_t)offset + SLIP_TRD_SECTION_DRAW_LIST_END > trackDataSize)
		return false;
	*record = trdBase + offset;
	return true;
}

static bool SlipTrackWorld_AllocRecordLink(uint8_t *slotDrawBase, size_t slotDrawBytes, uint32_t slotDrawBaseAddress,
                                           uint32_t slotDrawFreeListAddress, const uint8_t *trdBase,
                                           size_t trackDataSize, uint32_t trdBaseAddress, uint32_t recordAddress,
                                           uint32_t edgeReference, uint8_t **drawRecord, uint32_t *drawRecordAddress) {
	SlipTrackWorldSlotDrawAlloc allocate;
	const uint8_t *record;
	uint32_t recordOffset;

	if (!SlipTrackWorld_RecordFromDosAddress(trdBase, trackDataSize, trdBaseAddress, recordAddress, &record)) {
		return false;
	}
	recordOffset = recordAddress - trdBaseAddress;
	if (!SlipTrackWorld_AllocSlotDrawRecord(slotDrawBase, slotDrawBytes, slotDrawBaseAddress, slotDrawFreeListAddress,
	                                        (uint8_t *)(uintptr_t)record, trackDataSize - recordOffset, recordAddress,
	                                        &allocate)) {
		return false;
	}
	*drawRecord = slotDrawBase + allocate.allocatedOffset;
	*drawRecordAddress = allocate.allocatedAddress;
	((SlipTrackDrawRecord *)(void *)*drawRecord)->edgeReference = edgeReference;
	return true;
}

bool SlipTrackWorld_RegisterSlot(uint16_t trackHandle, uint8_t *slotDrawBase, size_t slotDrawBytes,
                                 uint32_t slotDrawBaseAddress, uint32_t slotDrawFreeListAddress, uint8_t *slotListBase,
                                 size_t slotListBytes, uint32_t slotListBaseAddress, uint8_t *trdBase,
                                 size_t trackDataSize, uint32_t trdBaseAddress, const SlipObject *objectTable,
                                 size_t objectTableBytes, SlipObjectDrawCallback *slotDrawCallbacks,
                                 size_t slotDrawCallbackCount, uint8_t *inputSlot,
                                 const SlipTrackWorldSlotCollisionState *entryRegisters) {
	SlipTrackSlotRecord *const slot = (SlipTrackSlotRecord *)(void *)inputSlot;
	SlipTrackWorldOwnerDrawLinksClear clear;
	SlipObjectDrawCallbackReadResult callback;
	uint32_t firstRecord = 0;
	uint32_t secondRecord = 0;
	uint32_t firstEdge = 0;
	uint32_t secondEdge = 0;
	uint8_t *firstDraw = NULL;
	uint8_t *secondDraw = NULL;
	uint32_t firstDrawAddress = 0;
	uint32_t secondDrawAddress = 0;
	uint32_t objectOffset;
	uint32_t i;
	bool pairedRecords = false;

	if (inputSlot == NULL || slotListBase == NULL || inputSlot < slotListBase ||
	    (size_t)(inputSlot - slotListBase) + SLIP_TRACK_SLOT_OWNER_END > slotListBytes ||
	    !SlipTrackWorld_ClearOwnerDrawLinks(trackHandle, slotDrawBase, slotDrawBytes, slotDrawBaseAddress,
	                                        slotDrawFreeListAddress, trdBase, trackDataSize, trdBaseAddress, inputSlot,
	                                        slotListBytes - (size_t)(inputSlot - slotListBase), &clear)) {
		return false;
	}
	if (slot->doorAddress != 0) {

		const uint32_t doorOffset = slot->doorAddress - SLIP_TRACK_DOOR_TABLE_DOS_TOKEN;
		if (doorOffset % SLIP_TRACK_DOOR_RECORD_BYTES != 0 ||
		    doorOffset / SLIP_TRACK_DOOR_RECORD_BYTES >= SLIP_TRACK_DOOR_CAPACITY)
			return false;
		const SlipTrackDoorRecord *const door = &SlipTrackWorld_doors[doorOffset / SLIP_TRACK_DOOR_RECORD_BYTES];
		secondRecord = door->secondTrackRecord;
		firstRecord = door->firstTrackRecord;
		if (!SlipTrackWorld_AllocRecordLink(slotDrawBase, slotDrawBytes, slotDrawBaseAddress, slotDrawFreeListAddress,
		                                    trdBase, trackDataSize, trdBaseAddress, firstRecord, 0, &firstDraw,
		                                    &firstDrawAddress) ||
		    !SlipTrackWorld_AllocRecordLink(slotDrawBase, slotDrawBytes, slotDrawBaseAddress, slotDrawFreeListAddress,
		                                    trdBase, trackDataSize, trdBaseAddress, secondRecord, 0, &secondDraw,
		                                    &secondDrawAddress))
			return false;
		SlipTrackDrawRecord *const first = (void *)firstDraw;
		SlipTrackDrawRecord *const second = (void *)secondDraw;
		second->pairedDrawAddress = firstDrawAddress;
		first->pairedDrawAddress = secondDrawAddress;
		slot->secondDrawAddress = secondDrawAddress;
		slot->firstDrawAddress = firstDrawAddress;
		second->objectOffset = slot->ownerObjectOffset;
		first->objectOffset = slot->ownerObjectOffset;
		second->callbackAddress = SLIP_TRACK_DOOR_DRAW_DOS_TOKEN;
		first->callbackAddress = SLIP_TRACK_DOOR_DRAW_REVERSE_DOS_TOKEN;
		const size_t firstIndex = (firstDrawAddress - slotDrawBaseAddress) / sizeof(SlipTrackDrawRecord);
		const size_t secondIndex = (secondDrawAddress - slotDrawBaseAddress) / sizeof(SlipTrackDrawRecord);
		if (slotDrawCallbacks == NULL || firstIndex >= slotDrawCallbackCount || secondIndex >= slotDrawCallbackCount)
			return false;
		slotDrawCallbacks[secondIndex] = TrackView_DrawDoor;
		slotDrawCallbacks[firstIndex] = TrackView_DrawDoorReverse;
		return true;
	}
	do {
		if (slot->flags == SLIP_TRACK_SLOT_POINT_COLLISION) {

			firstRecord = slot->currentTrackRecordAddress;
			break;
		}

		for (i = 0; i < SLIP_TRACK_BOUNDING_CORNER_COUNT; ++i) {
			const uint32_t record = slot->cornerTrackRecords[i];

			if (record == firstRecord)
				continue;
			if (firstRecord == 0) {
				firstRecord = record;
			} else {
				secondRecord = record;
			}
		}
		if (firstRecord == 0) {
			break;
		}
		if (secondRecord != 0) {
			const uint8_t *currentFirstRecord;
			const uint8_t *currentSecondRecord;
			bool adjacent = false;

			if (!SlipTrackWorld_RecordFromDosAddress(trdBase, trackDataSize, trdBaseAddress, firstRecord,
			                                         &currentFirstRecord) ||
			    !SlipTrackWorld_RecordFromDosAddress(trdBase, trackDataSize, trdBaseAddress, secondRecord,
			                                         &currentSecondRecord)) {
				return false;
			}
			for (i = 0; i < SLIP_TRD_SECTION_EXIT_COUNT; ++i) {
				if (trdBaseAddress + SlipBytes_ReadLE16(currentFirstRecord + SLIP_TRD_SECTION_FIRST_EXIT_OFFSET +
				                                        i * SLIP_TRD_SECTION_EXIT_BYTES) ==
				    secondRecord) {
					firstEdge = SlipBytes_ReadLE16(currentFirstRecord + SLIP_TRD_SECTION_FIRST_EXIT_PLANE_OFFSET +
					                               i * SLIP_TRD_SECTION_EXIT_BYTES);
					adjacent = true;
					break;
				}
			}
			if (adjacent) {
				bool reverseAdjacent = false;

				if (!SlipTrackWorld_AllocRecordLink(slotDrawBase, slotDrawBytes, slotDrawBaseAddress,
				                                    slotDrawFreeListAddress, trdBase, trackDataSize, trdBaseAddress,
				                                    firstRecord, firstEdge, &firstDraw, &firstDrawAddress)) {
					return false;
				}
				for (i = 0; i < SLIP_TRD_SECTION_EXIT_COUNT; ++i) {
					if (trdBaseAddress + SlipBytes_ReadLE16(currentSecondRecord + SLIP_TRD_SECTION_FIRST_EXIT_OFFSET +
					                                        i * SLIP_TRD_SECTION_EXIT_BYTES) ==
					    firstRecord) {
						secondEdge = SlipBytes_ReadLE16(currentSecondRecord + SLIP_TRD_SECTION_FIRST_EXIT_PLANE_OFFSET +
						                                i * SLIP_TRD_SECTION_EXIT_BYTES);
						reverseAdjacent = true;
						break;
					}
				}
				if (!reverseAdjacent)
					break;
				if (!SlipTrackWorld_AllocRecordLink(slotDrawBase, slotDrawBytes, slotDrawBaseAddress,
				                                    slotDrawFreeListAddress, trdBase, trackDataSize, trdBaseAddress,
				                                    secondRecord, secondEdge, &secondDraw, &secondDrawAddress)) {
					return false;
				}
				((SlipTrackDrawRecord *)(void *)secondDraw)->pairedDrawAddress = firstDrawAddress;
				((SlipTrackDrawRecord *)(void *)firstDraw)->pairedDrawAddress = secondDrawAddress;
				slot->secondDrawAddress = secondDrawAddress;
				slot->firstDrawAddress = firstDrawAddress;
				pairedRecords = true;
			}
		}
	} while (false);

	if (firstRecord == 0) {

		if (entryRegisters == NULL) {

			return false;
		}
		const uint32_t slotAddress = slotListBaseAddress + (uint32_t)(inputSlot - slotListBase);
		SlipObject_Free((uint16_t)slot->ownerObjectOffset,
		                slot->flags == SLIP_TRACK_SLOT_POINT_COLLISION ? entryRegisters->currentRecordOrFlags : 0,
		                entryRegisters->preservedObjectFreeValue,
		                slot->flags == SLIP_TRACK_SLOT_POINT_COLLISION ? entryRegisters->remainingCornerCount : 0,
		                entryRegisters->secondRecordOrOffset, slotAddress, entryRegisters->firstRecordAddress);
		return true;
	}

	if (!pairedRecords) {
		if (!SlipTrackWorld_AllocRecordLink(slotDrawBase, slotDrawBytes, slotDrawBaseAddress, slotDrawFreeListAddress,
		                                    trdBase, trackDataSize, trdBaseAddress, firstRecord, 0, &firstDraw,
		                                    &firstDrawAddress)) {
			return false;
		}
		slot->firstDrawAddress = firstDrawAddress;
		slot->secondDrawAddress = 0;
	}

	objectOffset = slot->ownerObjectOffset;
	((SlipTrackDrawRecord *)(void *)firstDraw)->objectOffset = objectOffset;
	if (pairedRecords) {
		((SlipTrackDrawRecord *)(void *)secondDraw)->objectOffset = objectOffset;
	}
	if (!SlipObject_GetSlotDrawCallback(objectTable, objectTableBytes, objectOffset, &callback)) {
		return false;
	}
	if (slotDrawCallbacks == NULL || firstDrawAddress < slotDrawBaseAddress ||
	    (firstDrawAddress - slotDrawBaseAddress) % sizeof(SlipTrackDrawRecord) != 0 ||
	    (firstDrawAddress - slotDrawBaseAddress) / sizeof(SlipTrackDrawRecord) >= slotDrawCallbackCount) {
		return false;
	}
	slotDrawCallbacks[(firstDrawAddress - slotDrawBaseAddress) / sizeof(SlipTrackDrawRecord)] =
	    callback.slotDrawCallback;
	if (pairedRecords) {
		if (secondDrawAddress < slotDrawBaseAddress ||
		    (secondDrawAddress - slotDrawBaseAddress) % sizeof(SlipTrackDrawRecord) != 0 ||
		    (secondDrawAddress - slotDrawBaseAddress) / sizeof(SlipTrackDrawRecord) >= slotDrawCallbackCount) {
			return false;
		}
		slotDrawCallbacks[(secondDrawAddress - slotDrawBaseAddress) / sizeof(SlipTrackDrawRecord)] =
		    callback.slotDrawCallback;
	}
	return true;
}

bool SlipTrackWorld_AddSlot(uint16_t objectOffset, uint32_t flags, uint16_t trackHandle, uint8_t *slotDrawBase,
                            size_t slotDrawBytes, SlipObjectDrawCallback *slotDrawCallbacks,
                            size_t slotDrawCallbackCount, uint32_t slotDrawBaseAddress,
                            uint32_t slotDrawFreeListAddress, uint8_t *slotListBase, size_t slotListBytes,
                            uint32_t slotListBaseAddress, uint32_t slotListActiveAddress, uint32_t slotListFreeAddress,
                            SlipObject *objectTable, size_t objectTableBytes, uint8_t *articSlotPool,
                            size_t articSlotPoolBytes, uint32_t articSlotPoolAddress, uint8_t *trdBase,
                            size_t trackDataSize, uint32_t trdBaseAddress, const uint8_t *componentBase,
                            size_t componentBaseBytes, uint32_t componentBaseAddress, const uint8_t *table,
                            size_t tableBytes, SlipTrackWorldAddSlot *result) {
	SlipTrackWorldSlotListAlloc allocate;
	SlipObjectPosition objectPosition;
	uint8_t *currentSlot;
	SlipTrackSlotRecord *slot;
	uint32_t slotAddress;
	uint32_t boundingRadius;

	if (result == NULL || !SlipTrackWorld_AllocSlotListEntry(slotListBase, slotListBytes, slotListBaseAddress,
	                                                         slotListActiveAddress, slotListFreeAddress, &allocate)) {
		return false;
	}
	if (allocate.carry) {
		SlipRuntime_error = SLIP_TRACK_SLOT_ALLOCATION_ERROR;
		*result = (SlipTrackWorldAddSlot){NULL, 0, true};
		return true;
	}
	slotAddress = allocate.allocatedAddress;
	currentSlot = slotListBase + allocate.allocatedOffset;
	slot = (SlipTrackSlotRecord *)(void *)currentSlot;
	slot->doorAddress = 0;
	slot->trackBranch = 0;
	if (!SlipObject_SetTrackSlot(objectTable, objectTableBytes, objectOffset, (uint16_t)(currentSlot - slotListBase))) {
		return false;
	}
	if ((flags & SLIP_TRACK_SLOT_POINT_COLLISION) != 0) {
		slot->flags = flags;
	} else {
		if ((flags & SLIP_TRACK_SLOT_BOX_COLLISION) == 0) {
			SlipRuntime_Fatal("TrackSlotAdd - sphere types not yet supported");
		}
		slot->flags = flags;
		if ((flags & SLIP_TRACK_SLOT_ARTICULATED_BOUNDS) != 0) {
			SlipArticSlotMainBounds mainPartBounds;
			int32_t extent;

			if (!SlipArticSlot_GetMainBounds(objectOffset, objectTable, objectTableBytes, articSlotPool,
			                                 articSlotPoolBytes, articSlotPoolAddress, &mainPartBounds)) {
				return false;
			}
			slot->boundsAndCorners[SLIP_VIEW_BOX_MIN_X] = (int32_t)(0u - (uint32_t)mainPartBounds.maxX);
			slot->boundsAndCorners[SLIP_VIEW_BOX_MIN_Y] = (int32_t)((uint32_t)mainPartBounds.minY);
			slot->boundsAndCorners[SLIP_VIEW_BOX_MIN_Z] = (int32_t)((uint32_t)mainPartBounds.minZ);
			slot->boundsAndCorners[SLIP_VIEW_BOX_MAX_X] = (int32_t)((uint32_t)mainPartBounds.maxX);
			slot->boundsAndCorners[SLIP_VIEW_BOX_MAX_Y] = (int32_t)((uint32_t)mainPartBounds.maxY);
			slot->boundsAndCorners[SLIP_VIEW_BOX_MAX_Z] = (int32_t)((uint32_t)mainPartBounds.maxZ);
			if (!SlipArticSlot_GetExtent(objectOffset, objectTable, objectTableBytes, articSlotPool, articSlotPoolBytes,
			                             articSlotPoolAddress, &extent)) {
				return false;
			}
			boundingRadius = (uint32_t)extent + SLIP_TRACK_SLOT_RADIUS_PADDING;
		} else {
			SlipRaceCollisionBodyBounds objectBounds;

			if (SlipObject_PhysicsOffset(objectTable, objectOffset) == 0) {
				SlipRuntime_Fatal(
				    "TrackSlotAdd - a track collision type slot MUST already be a collision slot (via CollideSlotAdd)");
			}
			objectBounds = SlipRaceCollision_GetBodyBounds(objectOffset);
			slot->boundsAndCorners[SLIP_VIEW_BOX_MIN_X] = (int32_t)(0u - (uint32_t)objectBounds.maxX);
			slot->boundsAndCorners[SLIP_VIEW_BOX_MIN_Y] = (int32_t)((uint32_t)objectBounds.minY);
			slot->boundsAndCorners[SLIP_VIEW_BOX_MIN_Z] = (int32_t)((uint32_t)objectBounds.minZ);
			slot->boundsAndCorners[SLIP_VIEW_BOX_MAX_X] = (int32_t)((uint32_t)objectBounds.maxX);
			slot->boundsAndCorners[SLIP_VIEW_BOX_MAX_Y] = (int32_t)((uint32_t)objectBounds.maxY);
			slot->boundsAndCorners[SLIP_VIEW_BOX_MAX_Z] = (int32_t)((uint32_t)objectBounds.maxZ);
			boundingRadius = SlipView3D_BoxRadius(objectBounds.minX, objectBounds.minY, objectBounds.minZ,
			                                      objectBounds.maxX, objectBounds.maxY, objectBounds.maxZ) +
			                 SLIP_TRACK_SLOT_RADIUS_PADDING;
		}
		if ((int32_t)boundingRadius > SLIP_TRACK_SLOT_MAXIMUM_RADIUS) {
			SlipRuntime_Fatal("TrackSlotAdd - the extent of the collision type slot is too large");
		}
		slot->boundingRadius = boundingRadius;
	}

	if (!SlipObject_Position(objectTable, objectTableBytes, objectOffset, &objectPosition)) {
		return false;
	}
	slot->cachedPosition.x = (int32_t)(objectPosition.positionX - 1u);
	slot->cachedPosition.y = (int32_t)(objectPosition.positionY);
	slot->cachedPosition.z = (int32_t)(objectPosition.positionZ);
	slot->ownerObjectOffset = objectOffset;
	if (!SlipTrackWorld_UpdateSlotRecord(currentSlot, objectTable, objectTableBytes, trdBase, trackDataSize,
	                                     trdBaseAddress, componentBase, componentBaseBytes, componentBaseAddress, table,
	                                     tableBytes)) {
		return false;
	}
	if (SlipTrackWorld_QuerySlotCollision(currentSlot, objectTable, objectTableBytes, trdBase, trackDataSize,
	                                      trdBaseAddress, componentBase, componentBaseBytes, componentBaseAddress,
	                                      table, tableBytes, NULL)) {
		SlipRuntime_error = SLIP_TRACK_SLOT_ALLOCATION_ERROR;
		if (!SlipTrackWorld_FreeSlotListEntry(trackHandle, slotDrawBase, slotDrawBytes, slotDrawBaseAddress,
		                                      slotDrawFreeListAddress, trdBase, trackDataSize, trdBaseAddress,
		                                      slotListBase, slotListBytes, slotListBaseAddress, slotListFreeAddress,
		                                      currentSlot)) {
			return false;
		}
		*result = (SlipTrackWorldAddSlot){currentSlot, slotAddress, true};
		return true;
	}
	if (!SlipTrackWorld_RegisterSlot(trackHandle, slotDrawBase, slotDrawBytes, slotDrawBaseAddress,
	                                 slotDrawFreeListAddress, slotListBase, slotListBytes, slotListBaseAddress, trdBase,
	                                 trackDataSize, trdBaseAddress, objectTable, objectTableBytes, slotDrawCallbacks,
	                                 slotDrawCallbackCount, currentSlot, NULL)) {
		return false;
	}
	*result = (SlipTrackWorldAddSlot){currentSlot, slotAddress, false};
	return true;
}

static uint32_t SlipTrackWorld_orientedFaceAddress;

enum {
	SLIP_TRACK_ORIENTED_COLLISION_TOLERANCE = 512,
	SLIP_TRACK_POSITIVE_COLLISION_SKIP_MASK = SLIP_TRC_PRIMITIVE_TRENCH | SLIP_TRC_PRIMITIVE_RANGE_PLANE
};

bool SlipTrackWorld_OrientedComponentTest(const uint8_t *componentBase, size_t componentBaseBytes,
                                          uint32_t componentBaseAddress, uint16_t componentOffset, int32_t relativeX,
                                          int32_t relativeY, int32_t relativeZ,
                                          SlipTrackWorldOrientedComponentTest *result) {
	const uint8_t *component;
	const uint8_t *face;
	size_t faceOffset;
	uint32_t faceCount;
	int32_t closestDistance = INT32_MAX;
	bool carry = true;
	bool rejectedByFace = false;

	if (componentBase == NULL || result == NULL ||
	    (size_t)componentOffset + SLIP_TRC_COMPONENT_BOUNDS_END > componentBaseBytes) {
		return false;
	}
	component = componentBase + componentOffset;
	if (SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_PRIMITIVE_LIST_OFFSET) == 0) {
		*result = (SlipTrackWorldOrientedComponentTest){closestDistance, SlipTrackWorld_orientedFaceAddress, carry};
		return true;
	}
	if (relativeX <
	        SlipTrackWorld_ScaleSignedWordBy64(SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_MINIMUM_X_OFFSET)) ||
	    relativeX >
	        SlipTrackWorld_ScaleSignedWordBy64(SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_MAXIMUM_X_OFFSET)) ||
	    relativeY <
	        SlipTrackWorld_ScaleSignedWordBy64(SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_MINIMUM_Y_OFFSET)) ||
	    relativeY >
	        SlipTrackWorld_ScaleSignedWordBy64(SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_MAXIMUM_Y_OFFSET)) ||
	    relativeZ <
	        SlipTrackWorld_ScaleSignedWordBy64(SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_MINIMUM_Z_OFFSET)) ||
	    relativeZ >
	        SlipTrackWorld_ScaleSignedWordBy64(SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_MAXIMUM_Z_OFFSET))) {
		*result = (SlipTrackWorldOrientedComponentTest){closestDistance, SlipTrackWorld_orientedFaceAddress, carry};
		return true;
	}
	faceOffset = SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_PRIMITIVE_LIST_OFFSET);
	if (faceOffset + SLIP_TRC_TABLE_COUNT_BYTES > componentBaseBytes) {
		return false;
	}
	face = componentBase + faceOffset;
	faceCount = SlipBytes_ReadLE16(face);
	face += SLIP_TRC_TABLE_COUNT_BYTES;
	faceOffset += SLIP_TRC_TABLE_COUNT_BYTES;
	do {
		uint16_t descriptor;
		size_t faceBytes;

		if (faceOffset + SLIP_TRC_PRIMITIVE_FIRST_INDEX_END > componentBaseBytes) {
			return false;
		}
		if ((face[SLIP_TRC_PRIMITIVE_FLAGS_OFFSET] & SLIP_TRC_PRIMITIVE_TRENCH) == 0) {
			SlipTrackWorldPointLookup point;
			SlipTrackWorldRangePlane plane;
			uint32_t dotRounded;
			int32_t distance;

			if (!SlipTrackWorld_PointLookup(component, componentBase, componentBaseBytes,
			                                SlipBytes_ReadLE16(face + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET),
			                                (uint32_t)relativeX, (uint32_t)relativeY, (uint32_t)relativeZ, &point) ||
			    !SlipTrackWorld_StoreRangePlane(
			        point.pointXOrInput, point.pointYOrInput, point.pointZOrCountMergedWithInput,
			        SlipBytes_ReadLE16(face + SLIP_TRC_PRIMITIVE_NORMAL_X_OFFSET),
			        SlipBytes_ReadLE16(face + SLIP_TRC_PRIMITIVE_NORMAL_Y_OFFSET),
			        SlipBytes_ReadLE16(face + SLIP_TRC_PRIMITIVE_NORMAL_Z_OFFSET), &plane)) {
				return false;
			}
			dotRounded = SlipTrackWorld_RoundedDotProductShift14(
			    (uint32_t)relativeX - (uint32_t)plane.origin.x, (uint32_t)relativeY - (uint32_t)plane.origin.y,
			    (uint32_t)relativeZ - (uint32_t)plane.origin.z, (uint32_t)plane.normal.x, (uint32_t)plane.normal.y,
			    (uint32_t)plane.normal.z);
			distance = (int32_t)dotRounded;
			if (distance < -SLIP_TRACK_ORIENTED_COLLISION_TOLERANCE) {
				rejectedByFace = true;
				break;
			}
			if (distance < closestDistance) {
				closestDistance = distance;
				SlipTrackWorld_orientedFaceAddress = componentBaseAddress + (uint32_t)faceOffset;
			}
		}
		descriptor = SlipBytes_ReadLE16(face);
		if ((descriptor & SLIP_TRC_PRIMITIVE_TEXTURED) == 0) {
			faceBytes = (size_t)descriptor * SLIP_TRC_VERTEX_INDEX_BYTES + SLIP_TRC_PRIMITIVE_HEADER_BYTES;
		} else {
			faceBytes = (size_t)(descriptor & SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK) * SLIP_TRC_TEXTURED_VERTEX_BYTES +
			            SLIP_TRC_PRIMITIVE_HEADER_BYTES;
		}
		if (faceBytes > componentBaseBytes - faceOffset) {
			return false;
		}
		face += faceBytes;
		faceOffset += faceBytes;
	} while (--faceCount != 0);
	if (!rejectedByFace) {
		carry = false;
	}
	*result = (SlipTrackWorldOrientedComponentTest){closestDistance, SlipTrackWorld_orientedFaceAddress, carry};
	return true;
}

bool SlipTrackWorld_OrientedRecordTest(const uint8_t *trdBase, size_t trackDataSize, uint32_t trdBaseAddress,
                                       const uint8_t *componentBase, size_t componentBaseBytes,
                                       uint32_t componentBaseAddress, uint32_t recordAddress, int32_t positionX,
                                       int32_t positionY, int32_t positionZ,
                                       SlipTrackWorldOrientedComponentTest *result) {
	uint32_t recordOffset;
	const uint8_t *record;

	if (trdBase == NULL ||
	    !SlipTrackWorld_DosAddressToOffset(recordAddress, trdBaseAddress, trackDataSize, &recordOffset) ||
	    (size_t)recordOffset + SLIP_TRD_SECTION_ORIGIN_END > trackDataSize) {
		return false;
	}
	record = trdBase + recordOffset;
	return SlipTrackWorld_OrientedComponentTest(
	    componentBase, componentBaseBytes, componentBaseAddress,
	    SlipBytes_ReadLE16(record + SLIP_TRD_SECTION_COMPONENT_OFFSET),
	    (int32_t)((uint32_t)positionX - SlipBytes_ReadLE32(record + SLIP_TRD_SECTION_ORIGIN_X_OFFSET)),
	    (int32_t)((uint32_t)positionY - SlipBytes_ReadLE32(record + SLIP_TRD_SECTION_ORIGIN_Y_OFFSET)),
	    (int32_t)((uint32_t)positionZ - SlipBytes_ReadLE32(record + SLIP_TRD_SECTION_ORIGIN_Z_OFFSET)), result);
}

bool SlipTrackWorld_OrientedRecordSearch(const uint8_t *trdBase, size_t trackDataSize, uint32_t trdBaseAddress,
                                         const uint8_t *componentBase, size_t componentBaseBytes,
                                         uint32_t componentBaseAddress, const uint8_t *table, size_t tableBytes,
                                         int32_t positionX, int32_t positionY, int32_t positionZ,
                                         uint32_t recordAddress, SlipTrackWorldOrientedRecordSearch *result) {
	SlipTrackWorldOrientedComponentTest test;
	int32_t distance = 0;
	uint32_t faceAddress = 0;
	uint32_t currentRecordAddress = recordAddress;
	uint32_t recordOffset;
	const uint8_t *record;

	if (result == NULL) {
		return false;
	}
	if (currentRecordAddress != 0) {
		uint16_t linkOffset;

		if (!SlipTrackWorld_OrientedRecordTest(trdBase, trackDataSize, trdBaseAddress, componentBase,
		                                       componentBaseBytes, componentBaseAddress, currentRecordAddress,
		                                       positionX, positionY, positionZ, &test)) {
			return false;
		}
		distance = test.distance;
		faceAddress = test.faceAddress;
		if (!test.carryOut) {
			*result = (SlipTrackWorldOrientedRecordSearch){distance, faceAddress, currentRecordAddress};
			return true;
		}
		if (!SlipTrackWorld_DosAddressToOffset(currentRecordAddress, trdBaseAddress, trackDataSize, &recordOffset) ||
		    (size_t)recordOffset + SLIP_TRD_SECTION_EXIT_LINKS_END > trackDataSize) {
			return false;
		}
		record = trdBase + recordOffset;
		linkOffset = SlipBytes_ReadLE16(record + SLIP_TRD_SECTION_FIRST_EXIT_OFFSET);
		if (linkOffset != 0) {
			if (!SlipTrackWorld_OrientedRecordTest(
			        trdBase, trackDataSize, trdBaseAddress, componentBase, componentBaseBytes, componentBaseAddress,
			        trdBaseAddress + linkOffset, positionX, positionY, positionZ, &test)) {
				return false;
			}
			distance = test.distance;
			faceAddress = test.faceAddress;
			if (!test.carryOut) {
				currentRecordAddress = trdBaseAddress + linkOffset;
				*result = (SlipTrackWorldOrientedRecordSearch){distance, faceAddress, currentRecordAddress};
				return true;
			}
		}
		linkOffset = SlipBytes_ReadLE16(record + SLIP_TRD_SECTION_SECOND_EXIT_OFFSET);
		if (linkOffset != 0) {
			if (!SlipTrackWorld_OrientedRecordTest(
			        trdBase, trackDataSize, trdBaseAddress, componentBase, componentBaseBytes, componentBaseAddress,
			        trdBaseAddress + linkOffset, positionX, positionY, positionZ, &test)) {
				return false;
			}
			distance = test.distance;
			faceAddress = test.faceAddress;
			if (!test.carryOut) {
				currentRecordAddress = trdBaseAddress + linkOffset;
				*result = (SlipTrackWorldOrientedRecordSearch){distance, faceAddress, currentRecordAddress};
				return true;
			}
		}
		linkOffset = SlipBytes_ReadLE16(record + SLIP_TRD_SECTION_THIRD_EXIT_OFFSET);
		if (linkOffset != 0) {
			if (!SlipTrackWorld_OrientedRecordTest(
			        trdBase, trackDataSize, trdBaseAddress, componentBase, componentBaseBytes, componentBaseAddress,
			        trdBaseAddress + linkOffset, positionX, positionY, positionZ, &test)) {
				return false;
			}
			distance = test.distance;
			faceAddress = test.faceAddress;
			if (!test.carryOut) {
				currentRecordAddress = trdBaseAddress + linkOffset;
				*result = (SlipTrackWorldOrientedRecordSearch){distance, faceAddress, currentRecordAddress};
				return true;
			}
		}
	}

	{
		SlipTrackWorldTableLookup tableLookup;
		uint32_t cellAddress;
		uint32_t cellOffset;
		const uint8_t *cell;
		uint32_t listOffset;
		uint32_t entryCount;
		uint32_t entryOffset;

		if (!SlipTrackWorld_TableLookup(
		        table, tableBytes, (uint16_t)SlipTrackWorld_SignedShiftRight20((uint32_t)positionX),
		        (uint16_t)SlipTrackWorld_SignedShiftRight20((uint32_t)positionY),
		        (uint16_t)SlipTrackWorld_SignedShiftRight20((uint32_t)positionZ), &tableLookup)) {
			return false;
		}
		cellAddress = tableLookup.recordAddress;
		if (cellAddress == 0) {
			*result = (SlipTrackWorldOrientedRecordSearch){distance, faceAddress, 0};
			return true;
		}
		if (!SlipTrackWorld_DosAddressToOffset(cellAddress, trdBaseAddress, trackDataSize, &cellOffset) ||
		    (size_t)cellOffset + SLIP_TRACK_LINKED_HEADER_BYTES > trackDataSize) {
			return false;
		}
		cell = trdBase + cellOffset;
		listOffset = SlipBytes_ReadLE16(cell + SLIP_TRACK_LINKED_OFFSET);
		if (listOffset == 0 || (size_t)listOffset + SLIP_TRD_TABLE_COUNT_BYTES > trackDataSize) {
			if (listOffset != 0)
				return false;
			*result = (SlipTrackWorldOrientedRecordSearch){distance, faceAddress, 0};
			return true;
		}
		entryCount = SlipBytes_ReadLE16(trdBase + listOffset);
		entryOffset = listOffset + SLIP_TRD_TABLE_COUNT_BYTES;
		do {
			const uint8_t *entry;

			if ((size_t)entryOffset + SLIP_TRD_SECTION_ORIGIN_END > trackDataSize) {
				return false;
			}
			entry = trdBase + entryOffset;
			if ((SlipBytes_ReadLE16(entry + SLIP_TRD_SECTION_FIRST_EXIT_OFFSET) |
			     SlipBytes_ReadLE16(entry + SLIP_TRD_SECTION_SECOND_EXIT_OFFSET) |
			     SlipBytes_ReadLE16(entry + SLIP_TRD_SECTION_THIRD_EXIT_OFFSET)) != 0) {
				if (!SlipTrackWorld_OrientedComponentTest(
				        componentBase, componentBaseBytes, componentBaseAddress,
				        SlipBytes_ReadLE16(entry + SLIP_TRD_SECTION_COMPONENT_OFFSET),
				        (int32_t)((uint32_t)positionX - SlipBytes_ReadLE32(entry + SLIP_TRD_SECTION_ORIGIN_X_OFFSET)),
				        (int32_t)((uint32_t)positionY - SlipBytes_ReadLE32(entry + SLIP_TRD_SECTION_ORIGIN_Y_OFFSET)),
				        (int32_t)((uint32_t)positionZ - SlipBytes_ReadLE32(entry + SLIP_TRD_SECTION_ORIGIN_Z_OFFSET)),
				        &test)) {
					return false;
				}
				distance = test.distance;
				faceAddress = test.faceAddress;
				if (!test.carryOut) {
					*result = (SlipTrackWorldOrientedRecordSearch){distance, faceAddress, trdBaseAddress + entryOffset};
					return true;
				}
			}
			entryOffset += SLIP_TRD_SECTION_BYTES;
		} while (--entryCount != 0);
	}

	*result = (SlipTrackWorldOrientedRecordSearch){distance, faceAddress, 0};
	return true;
}

static uint32_t SlipTrackWorld_positiveFaceAddress;

static bool SlipTrackWorld_PositiveComponentTest(const uint8_t *componentBase, size_t componentBaseBytes,
                                                 uint32_t componentBaseAddress, uint16_t componentOffset,
                                                 int32_t relativeX, int32_t relativeY, int32_t relativeZ,
                                                 int32_t *distance, uint32_t *faceAddress, bool *carryOutOrOr) {
	const uint8_t *component;
	const uint8_t *face;
	size_t faceOffset;
	uint32_t faceCount;
	int32_t closestDistance = INT32_MAX;
	bool carry = true;
	bool rejectedByFace = false;

	if (componentBase == NULL || distance == NULL || faceAddress == NULL || carryOutOrOr == NULL ||
	    (size_t)componentOffset + SLIP_TRC_COMPONENT_BOUNDS_END > componentBaseBytes) {
		return false;
	}
	component = componentBase + componentOffset;
	if (SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_PRIMITIVE_LIST_OFFSET) == 0) {
		*distance = closestDistance;
		*faceAddress = SlipTrackWorld_positiveFaceAddress;
		*carryOutOrOr = carry;
		return true;
	}
	if (relativeX <
	        SlipTrackWorld_ScaleSignedWordBy64(SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_MINIMUM_X_OFFSET)) ||
	    relativeX >
	        SlipTrackWorld_ScaleSignedWordBy64(SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_MAXIMUM_X_OFFSET)) ||
	    relativeY <
	        SlipTrackWorld_ScaleSignedWordBy64(SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_MINIMUM_Y_OFFSET)) ||
	    relativeY >
	        SlipTrackWorld_ScaleSignedWordBy64(SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_MAXIMUM_Y_OFFSET)) ||
	    relativeZ <
	        SlipTrackWorld_ScaleSignedWordBy64(SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_MINIMUM_Z_OFFSET)) ||
	    relativeZ >
	        SlipTrackWorld_ScaleSignedWordBy64(SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_MAXIMUM_Z_OFFSET))) {
		*distance = closestDistance;
		*faceAddress = SlipTrackWorld_positiveFaceAddress;
		*carryOutOrOr = carry;
		return true;
	}
	faceOffset = SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_PRIMITIVE_LIST_OFFSET);
	if (faceOffset + SLIP_TRC_TABLE_COUNT_BYTES > componentBaseBytes) {
		return false;
	}
	face = componentBase + faceOffset;
	faceCount = SlipBytes_ReadLE16(face);
	face += SLIP_TRC_TABLE_COUNT_BYTES;
	faceOffset += SLIP_TRC_TABLE_COUNT_BYTES;
	do {
		uint16_t descriptor;
		size_t faceBytes;

		if (faceOffset + SLIP_TRC_PRIMITIVE_FIRST_INDEX_END > componentBaseBytes) {
			return false;
		}
		if ((face[SLIP_TRC_PRIMITIVE_FLAGS_OFFSET] & SLIP_TRACK_POSITIVE_COLLISION_SKIP_MASK) == 0) {
			SlipTrackWorldPointLookup point;
			SlipTrackWorldRangePlane plane;
			uint32_t dotRounded;
			int32_t currentDistance;

			if (!SlipTrackWorld_PointLookup(component, componentBase, componentBaseBytes,
			                                SlipBytes_ReadLE16(face + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET),
			                                (uint32_t)relativeX, (uint32_t)relativeY, (uint32_t)relativeZ, &point) ||
			    !SlipTrackWorld_StoreRangePlane(
			        point.pointXOrInput, point.pointYOrInput, point.pointZOrCountMergedWithInput,
			        SlipBytes_ReadLE16(face + SLIP_TRC_PRIMITIVE_NORMAL_X_OFFSET),
			        SlipBytes_ReadLE16(face + SLIP_TRC_PRIMITIVE_NORMAL_Y_OFFSET),
			        SlipBytes_ReadLE16(face + SLIP_TRC_PRIMITIVE_NORMAL_Z_OFFSET), &plane)) {
				return false;
			}
			dotRounded = SlipTrackWorld_RoundedDotProductShift14(
			    (uint32_t)relativeX - (uint32_t)plane.origin.x, (uint32_t)relativeY - (uint32_t)plane.origin.y,
			    (uint32_t)relativeZ - (uint32_t)plane.origin.z, (uint32_t)plane.normal.x, (uint32_t)plane.normal.y,
			    (uint32_t)plane.normal.z);
			currentDistance = (int32_t)dotRounded;
			if (currentDistance < 0) {
				rejectedByFace = true;
				break;
			}
			if (currentDistance < closestDistance) {
				closestDistance = currentDistance;
				SlipTrackWorld_positiveFaceAddress = componentBaseAddress + (uint32_t)faceOffset;
			}
		}
		descriptor = SlipBytes_ReadLE16(face);
		if ((descriptor & SLIP_TRC_PRIMITIVE_TEXTURED) == 0) {
			faceBytes = (size_t)descriptor * SLIP_TRC_VERTEX_INDEX_BYTES + SLIP_TRC_PRIMITIVE_HEADER_BYTES;
		} else {
			faceBytes = (size_t)(descriptor & SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK) * SLIP_TRC_TEXTURED_VERTEX_BYTES +
			            SLIP_TRC_PRIMITIVE_HEADER_BYTES;
		}
		if (faceBytes > componentBaseBytes - faceOffset) {
			return false;
		}
		face += faceBytes;
		faceOffset += faceBytes;
	} while (--faceCount != 0);
	if (!rejectedByFace) {
		carry = false;
	}
	*distance = closestDistance;
	*faceAddress = SlipTrackWorld_positiveFaceAddress;
	*carryOutOrOr = carry;
	return true;
}

static bool SlipTrackWorld_PositiveRecordTest(const uint8_t *trdBase, size_t trackDataSize, uint32_t trdBaseAddress,
                                              const uint8_t *componentBase, size_t componentBaseBytes,
                                              uint32_t componentBaseAddress, uint32_t recordAddress, int32_t positionX,
                                              int32_t positionY, int32_t positionZ, int32_t *distance,
                                              uint32_t *faceAddress, bool *carryOut) {
	uint32_t recordOffset;
	const uint8_t *record;

	if (trdBase == NULL ||
	    !SlipTrackWorld_DosAddressToOffset(recordAddress, trdBaseAddress, trackDataSize, &recordOffset) ||
	    (size_t)recordOffset + SLIP_TRD_SECTION_ORIGIN_END > trackDataSize) {
		return false;
	}
	record = trdBase + recordOffset;
	return SlipTrackWorld_PositiveComponentTest(
	    componentBase, componentBaseBytes, componentBaseAddress,
	    SlipBytes_ReadLE16(record + SLIP_TRD_SECTION_COMPONENT_OFFSET),
	    (int32_t)((uint32_t)positionX - SlipBytes_ReadLE32(record + SLIP_TRD_SECTION_ORIGIN_X_OFFSET)),
	    (int32_t)((uint32_t)positionY - SlipBytes_ReadLE32(record + SLIP_TRD_SECTION_ORIGIN_Y_OFFSET)),
	    (int32_t)((uint32_t)positionZ - SlipBytes_ReadLE32(record + SLIP_TRD_SECTION_ORIGIN_Z_OFFSET)), distance,
	    faceAddress, carryOut);
}

bool SlipTrackWorld_PositiveRecordSearch(const uint8_t *trdBase, size_t trackDataSize, uint32_t trdBaseAddress,
                                         const uint8_t *componentBase, size_t componentBaseBytes,
                                         uint32_t componentBaseAddress, const uint8_t *table, size_t tableBytes,
                                         int32_t positionX, int32_t positionY, int32_t positionZ,
                                         uint32_t recordAddress, SlipTrackWorldPositiveRecordSearch *result) {
	int32_t distance = 0;
	uint32_t faceAddress = 0;
	uint32_t currentRecordAddress = recordAddress;
	uint32_t recordOffset;
	const uint8_t *record;
	bool carry;

	if (result == NULL) {
		return false;
	}
	if (currentRecordAddress != 0) {
		uint16_t linkOffset;

		if (!SlipTrackWorld_PositiveRecordTest(trdBase, trackDataSize, trdBaseAddress, componentBase,
		                                       componentBaseBytes, componentBaseAddress, currentRecordAddress,
		                                       positionX, positionY, positionZ, &distance, &faceAddress, &carry)) {
			return false;
		}
		if (!carry) {
			*result = (SlipTrackWorldPositiveRecordSearch){distance, faceAddress, currentRecordAddress};
			return true;
		}
		if (!SlipTrackWorld_DosAddressToOffset(currentRecordAddress, trdBaseAddress, trackDataSize, &recordOffset) ||
		    (size_t)recordOffset + SLIP_TRD_SECTION_EXIT_LINKS_END > trackDataSize) {
			return false;
		}
		record = trdBase + recordOffset;
		linkOffset = SlipBytes_ReadLE16(record + SLIP_TRD_SECTION_FIRST_EXIT_OFFSET);
		if (linkOffset != 0) {
			if (!SlipTrackWorld_PositiveRecordTest(
			        trdBase, trackDataSize, trdBaseAddress, componentBase, componentBaseBytes, componentBaseAddress,
			        trdBaseAddress + linkOffset, positionX, positionY, positionZ, &distance, &faceAddress, &carry)) {
				return false;
			}
			if (!carry) {
				currentRecordAddress = trdBaseAddress + linkOffset;
				*result = (SlipTrackWorldPositiveRecordSearch){distance, faceAddress, currentRecordAddress};
				return true;
			}
		}
		linkOffset = SlipBytes_ReadLE16(record + SLIP_TRD_SECTION_SECOND_EXIT_OFFSET);
		if (linkOffset != 0) {
			if (!SlipTrackWorld_PositiveRecordTest(
			        trdBase, trackDataSize, trdBaseAddress, componentBase, componentBaseBytes, componentBaseAddress,
			        trdBaseAddress + linkOffset, positionX, positionY, positionZ, &distance, &faceAddress, &carry)) {
				return false;
			}
			if (!carry) {
				currentRecordAddress = trdBaseAddress + linkOffset;
				*result = (SlipTrackWorldPositiveRecordSearch){distance, faceAddress, currentRecordAddress};
				return true;
			}
		}
		linkOffset = SlipBytes_ReadLE16(record + SLIP_TRD_SECTION_THIRD_EXIT_OFFSET);
		if (linkOffset != 0) {
			if (!SlipTrackWorld_PositiveRecordTest(
			        trdBase, trackDataSize, trdBaseAddress, componentBase, componentBaseBytes, componentBaseAddress,
			        trdBaseAddress + linkOffset, positionX, positionY, positionZ, &distance, &faceAddress, &carry)) {
				return false;
			}
			if (!carry) {
				currentRecordAddress = trdBaseAddress + linkOffset;
				*result = (SlipTrackWorldPositiveRecordSearch){distance, faceAddress, currentRecordAddress};
				return true;
			}
		}
	}

	{
		SlipTrackWorldTableLookup tableLookup;
		uint32_t cellAddress;
		uint32_t cellOffset;
		const uint8_t *cell;
		uint32_t listOffset;
		uint32_t entryCount;
		uint32_t entryOffset;

		if (!SlipTrackWorld_TableLookup(
		        table, tableBytes, (uint16_t)SlipTrackWorld_SignedShiftRight20((uint32_t)positionX),
		        (uint16_t)SlipTrackWorld_SignedShiftRight20((uint32_t)positionY),
		        (uint16_t)SlipTrackWorld_SignedShiftRight20((uint32_t)positionZ), &tableLookup)) {
			return false;
		}
		cellAddress = tableLookup.recordAddress;
		if (cellAddress == 0) {
			*result = (SlipTrackWorldPositiveRecordSearch){distance, faceAddress, 0};
			return true;
		}
		if (!SlipTrackWorld_DosAddressToOffset(cellAddress, trdBaseAddress, trackDataSize, &cellOffset) ||
		    (size_t)cellOffset + SLIP_TRACK_LINKED_HEADER_BYTES > trackDataSize) {
			return false;
		}
		cell = trdBase + cellOffset;
		listOffset = SlipBytes_ReadLE16(cell + SLIP_TRACK_LINKED_OFFSET);
		if (listOffset == 0 || (size_t)listOffset + SLIP_TRD_TABLE_COUNT_BYTES > trackDataSize) {
			if (listOffset != 0)
				return false;
			*result = (SlipTrackWorldPositiveRecordSearch){distance, faceAddress, 0};
			return true;
		}
		entryCount = SlipBytes_ReadLE16(trdBase + listOffset);
		entryOffset = listOffset + SLIP_TRD_TABLE_COUNT_BYTES;
		do {
			const uint8_t *entry;

			if ((size_t)entryOffset + SLIP_TRD_SECTION_ORIGIN_END > trackDataSize) {
				return false;
			}
			entry = trdBase + entryOffset;
			if ((SlipBytes_ReadLE16(entry + SLIP_TRD_SECTION_FIRST_EXIT_OFFSET) |
			     SlipBytes_ReadLE16(entry + SLIP_TRD_SECTION_SECOND_EXIT_OFFSET) |
			     SlipBytes_ReadLE16(entry + SLIP_TRD_SECTION_THIRD_EXIT_OFFSET)) != 0) {
				if (!SlipTrackWorld_PositiveComponentTest(
				        componentBase, componentBaseBytes, componentBaseAddress,
				        SlipBytes_ReadLE16(entry + SLIP_TRD_SECTION_COMPONENT_OFFSET),
				        (int32_t)((uint32_t)positionX - SlipBytes_ReadLE32(entry + SLIP_TRD_SECTION_ORIGIN_X_OFFSET)),
				        (int32_t)((uint32_t)positionY - SlipBytes_ReadLE32(entry + SLIP_TRD_SECTION_ORIGIN_Y_OFFSET)),
				        (int32_t)((uint32_t)positionZ - SlipBytes_ReadLE32(entry + SLIP_TRD_SECTION_ORIGIN_Z_OFFSET)),
				        &distance, &faceAddress, &carry)) {
					return false;
				}
				if (!carry) {
					*result = (SlipTrackWorldPositiveRecordSearch){distance, faceAddress, trdBaseAddress + entryOffset};
					return true;
				}
			}
			entryOffset += SLIP_TRD_SECTION_BYTES;
		} while (--entryCount != 0);
	}

	*result = (SlipTrackWorldPositiveRecordSearch){distance, faceAddress, 0};
	return true;
}

bool SlipTrackWorld_UpdateSlotRecord(uint8_t *inputSlot, const SlipObject *objectTable, size_t objectTableBytes,
                                     const uint8_t *trdBase, size_t trackDataSize, uint32_t trdBaseAddress,
                                     const uint8_t *componentBase, size_t componentBaseBytes,
                                     uint32_t componentBaseAddress, const uint8_t *table, size_t tableBytes) {
	SlipTrackSlotRecord *const slot = (SlipTrackSlotRecord *)(void *)inputSlot;
	const uint32_t object = slot->ownerObjectOffset;

	if ((slot->flags & SLIP_TRACK_SLOT_BOX_COLLISION) == 0) {
		SlipObjectPosition currentObjectPosition;
		SlipTrackWorldRecordSearch recordSearch;

		if (!SlipObject_Position(objectTable, objectTableBytes, object, &currentObjectPosition)) {
			return false;
		}
		if (currentObjectPosition.positionX == slot->cachedPosition.x &&
		    currentObjectPosition.positionY == slot->cachedPosition.y &&
		    currentObjectPosition.positionZ == slot->cachedPosition.z) {
			return true;
		}
		slot->cachedPosition.x = currentObjectPosition.positionX;
		slot->cachedPosition.y = currentObjectPosition.positionY;
		slot->cachedPosition.z = currentObjectPosition.positionZ;
		if (!SlipTrackWorld_RecordSearch(
		        trdBase, trackDataSize, componentBase, componentBaseBytes, table, tableBytes, trdBaseAddress,
		        slot->currentTrackRecordAddress, (int32_t)currentObjectPosition.positionX,
		        (int32_t)currentObjectPosition.positionY, (int32_t)currentObjectPosition.positionZ, &recordSearch)) {
			return false;
		}
		slot->currentTrackRecordAddress = recordSearch.selectedRecordAddress;
		return true;
	} else {
		SlipObjectPosition currentObjectPosition;
		SlipView3DMatrix objectMatrix;
		SlipObjectMatrixCopy matrixCopy;
		SlipTrackWorldOrientedRecordSearch recordSearch;
		SlipView3DVec32 position;
		int32_t *boundsAndCorners;
		uint32_t currentRecord;
		uint32_t i;

		SlipTrackWorld_slotObject = (uint16_t)object;
		if (!SlipObject_Position(objectTable, objectTableBytes, object, &currentObjectPosition) ||
		    !SlipObject_MatrixCopy(objectTable, objectTableBytes, object, &objectMatrix, &matrixCopy)) {
			return false;
		}
		if (currentObjectPosition.positionX == slot->cachedPosition.x &&
		    currentObjectPosition.positionY == slot->cachedPosition.y &&
		    currentObjectPosition.positionZ == slot->cachedPosition.z &&
		    memcmp(&slot->cachedObjectMatrix, &objectMatrix, sizeof(objectMatrix)) == 0) {
			return true;
		}
		slot->cachedPosition.x = currentObjectPosition.positionX;
		slot->cachedPosition.y = currentObjectPosition.positionY;
		slot->cachedPosition.z = currentObjectPosition.positionZ;

		slot->cachedObjectMatrix = objectMatrix;
		currentRecord = slot->currentTrackRecordAddress;
		if (!SlipTrackWorld_OrientedRecordSearch(
		        trdBase, trackDataSize, trdBaseAddress, componentBase, componentBaseBytes, componentBaseAddress, table,
		        tableBytes, (int32_t)currentObjectPosition.positionX, (int32_t)currentObjectPosition.positionY,
		        (int32_t)currentObjectPosition.positionZ, currentRecord, &recordSearch)) {
			return false;
		}
		currentRecord = recordSearch.recordAddress;
		slot->currentTrackRecordAddress = currentRecord;
		position = (SlipView3DVec32){(int32_t)currentObjectPosition.positionX, (int32_t)currentObjectPosition.positionY,
		                             (int32_t)currentObjectPosition.positionZ};
		if (currentRecord != 0 && recordSearch.distance > (int32_t)slot->boundingRadius) {
			boundsAndCorners = slot->boundsAndCorners;
			SlipView3D_BuildBoxCornersThunk(&slot->cachedObjectMatrix, boundsAndCorners, position);
			for (i = 0; i < SLIP_TRACK_BOUNDING_CORNER_COUNT; ++i) {
				slot->cornerTrackRecords[i] = currentRecord;
			}
			return true;
		}
		boundsAndCorners = slot->boundsAndCorners;
		SlipView3D_BuildBoxCornersThunk(&slot->cachedObjectMatrix, boundsAndCorners, position);
		SlipTrackWorld_lastRecord = 0;
		for (i = 0; i < SLIP_TRACK_BOUNDING_CORNER_COUNT; ++i) {
			SlipTrackWorldRecordSearch cornerSearch;
			const int32_t *const corner =
			    &slot->boundsAndCorners[SLIP_TRACK_BOUND_COORDINATE_COUNT + i * SLIP_TRACK_CORNER_COORDINATE_COUNT];

			if (!SlipTrackWorld_RecordSearch(trdBase, trackDataSize, componentBase, componentBaseBytes, table,
			                                 tableBytes, trdBaseAddress, slot->cornerTrackRecords[i], corner[0],
			                                 corner[1], corner[2], &cornerSearch)) {
				return false;
			}
			slot->cornerTrackRecords[i] = cornerSearch.selectedRecordAddress;
			if (cornerSearch.selectedRecordAddress != 0) {
				SlipTrackWorld_lastRecord = cornerSearch.selectedRecordAddress;
			}
		}
		if (slot->currentTrackRecordAddress == 0) {
			slot->currentTrackRecordAddress = SlipTrackWorld_lastRecord;
		}
		return true;
	}
}

void SlipTrackWorld_UpdateSlots(uint32_t trackHandle, uint8_t *slotDrawBase, size_t slotDrawBytes,
                                SlipObjectDrawCallback *slotDrawCallbacks, size_t slotDrawCallbackCount,
                                uint32_t slotDrawBaseAddress, uint32_t slotDrawFreeListAddress, uint8_t *slotListBase,
                                size_t slotListBytes, uint32_t slotListBaseAddress, uint32_t slotListSentinelAddress,
                                uint8_t *trdBase, size_t trackDataSize, uint32_t trdBaseAddress,
                                const uint8_t *componentBase, size_t componentBaseBytes, uint32_t componentBaseAddress,
                                const uint8_t *table, size_t tableBytes, SlipObject *objectTable,
                                size_t objectTableBytes) {
	const SlipTrackSlotRecord *const sentinel =
	    (const SlipTrackSlotRecord *)(const void *)(slotListBase + (slotListSentinelAddress - slotListBaseAddress));
	uint32_t slotAddress = sentinel->nextSlotAddress;

	while (slotAddress != slotListSentinelAddress) {
		SlipTrackSlotRecord *const slot =
		    (SlipTrackSlotRecord *)(void *)(slotListBase + (slotAddress - slotListBaseAddress));
		const uint32_t nextAddress = slot->nextSlotAddress;

		SlipTrackWorld_UpdateSlotRecord((uint8_t *)(void *)slot, objectTable, objectTableBytes, trdBase, trackDataSize,
		                                trdBaseAddress, componentBase, componentBaseBytes, componentBaseAddress, table,
		                                tableBytes);
		SlipTrackWorld_RegisterSlot(trackHandle, slotDrawBase, slotDrawBytes, slotDrawBaseAddress,
		                            slotDrawFreeListAddress, slotListBase, slotListBytes, slotListBaseAddress, trdBase,
		                            trackDataSize, trdBaseAddress, objectTable, objectTableBytes, slotDrawCallbacks,
		                            slotDrawCallbackCount, (uint8_t *)(void *)slot, NULL);
		slotAddress = nextAddress;
	}
}

bool SlipTrackWorld_QuerySlotCollision(uint8_t *inputSlot, const SlipObject *objectTable, size_t objectTableBytes,
                                       const uint8_t *trdBase, size_t trackDataSize, uint32_t trdBaseAddress,
                                       const uint8_t *componentBase, size_t componentBaseBytes,
                                       uint32_t componentBaseAddress, const uint8_t *table, size_t tableBytes,
                                       SlipTrackWorldSlotCollisionState *registers) {
	SlipTrackSlotRecord *const slot = (SlipTrackSlotRecord *)(void *)inputSlot;
	const uint32_t slotFlags = slot->flags;
	uint32_t firstRecordAddress = 0;
	uint32_t secondRecordAddress = 0;
	uint32_t cornerIndex;
	SlipTrackWorldSlotCollisionState discardedRegisters = {0};
	if (registers == NULL)
		registers = &discardedRegisters;
	registers->currentRecordOrFlags = slotFlags;

	SlipTrackWorld_UpdateSlotRecord(inputSlot, objectTable, objectTableBytes, trdBase, trackDataSize, trdBaseAddress,
	                                componentBase, componentBaseBytes, componentBaseAddress, table, tableBytes);
	if ((slotFlags & SLIP_TRACK_SLOT_BOX_COLLISION) == 0) {
		return slot->currentTrackRecordAddress == 0;
	}

	registers->cornerCursorAddress = registers->slotAddress;
	registers->firstRecordAddress = 0;
	registers->secondRecordOrOffset = 0;
	registers->remainingCornerCount = SLIP_TRACK_BOUNDING_CORNER_COUNT;

	for (cornerIndex = 0; cornerIndex < SLIP_TRACK_BOUNDING_CORNER_COUNT; ++cornerIndex) {
		const uint32_t cornerRecordAddress = slot->cornerTrackRecords[cornerIndex];
		registers->currentRecordOrFlags = cornerRecordAddress;

		if (cornerRecordAddress == 0)
			return true;
		if (firstRecordAddress == 0) {
			firstRecordAddress = cornerRecordAddress;
			registers->firstRecordAddress = firstRecordAddress;
		} else if (cornerRecordAddress != firstRecordAddress) {
			if (secondRecordAddress != 0 && cornerRecordAddress != secondRecordAddress)
				return true;
			secondRecordAddress = cornerRecordAddress;
			registers->secondRecordOrOffset = secondRecordAddress;
		}

		registers->cornerCursorAddress += sizeof(slot->cornerTrackRecords[0]);
		--registers->remainingCornerCount;
	}

	if (secondRecordAddress != 0) {
		const uint8_t *const firstRecord = trdBase + (uint32_t)(firstRecordAddress - trdBaseAddress);
		const uint16_t secondOffset = (uint16_t)(secondRecordAddress - trdBaseAddress);
		registers->secondRecordOrOffset = secondRecordAddress - trdBaseAddress;

		if (secondOffset == SlipBytes_ReadLE16(firstRecord + SLIP_TRD_SECTION_FIRST_EXIT_OFFSET) ||
		    secondOffset == SlipBytes_ReadLE16(firstRecord + SLIP_TRD_SECTION_SECOND_EXIT_OFFSET) ||
		    secondOffset == SlipBytes_ReadLE16(firstRecord + SLIP_TRD_SECTION_THIRD_EXIT_OFFSET)) {
			return false;
		}
		return true;
	}

	if (slot->currentTrackRecordAddress == 0) {
		slot->currentTrackRecordAddress = firstRecordAddress;
	}
	return false;
}

bool SlipTrackWorld_QueryObjectCollision(uint32_t collisionValue, uint16_t objectOffset, uint8_t *slotListBase,
                                         uint32_t slotListBaseAddress, const SlipObject *objectTable,
                                         size_t objectTableBytes, const uint8_t *trdBase, size_t trackDataSize,
                                         uint32_t trdBaseAddress, const uint8_t *componentBase,
                                         size_t componentBaseBytes, uint32_t componentBaseAddress, const uint8_t *table,
                                         size_t tableBytes) {
	SlipTrackWorldSlotListSelect select;
	uint32_t slotOffset;
	uint8_t *slot;

	SlipTrackWorld_SelectSlotListEntry(collisionValue, slotListBaseAddress, objectTable, objectTableBytes, objectOffset,
	                                   &select);
	if (select.carry) {
		SlipRuntime_Fatal("TrackSlotCheckStatic - this is NOT a Track slot");
	}
	slotOffset = select.slotAddress - slotListBaseAddress;
	slot = slotListBase + slotOffset;
	return SlipTrackWorld_QuerySlotCollision(slot, objectTable, objectTableBytes, trdBase, trackDataSize,
	                                         trdBaseAddress, componentBase, componentBaseBytes, componentBaseAddress,
	                                         table, tableBytes, NULL);
}

bool SlipTrackWorld_TableStore(uint8_t *table, size_t tableBytes, uint32_t initialCellIndex, uint16_t cellY,
                               uint16_t cellZ, uint32_t tableValue, SlipTrackWorldTableStore *result) {
	uint16_t yIndex;
	uint16_t xyIndex;
	uint32_t cellIndex;

	if (table == 0 || result == 0) {
		return false;
	}
	cellIndex = initialCellIndex;
	xyIndex = (uint16_t)cellIndex;
	yIndex = SLIP_TRACK_CELL_ROW_STRIDE;
	yIndex = (uint16_t)(yIndex * cellY);
	xyIndex = (uint16_t)(xyIndex + yIndex);
	*result = (SlipTrackWorldTableStore){
	    tableValue, initialCellIndex, SLIP_TRACK_CELL_ROW_STRIDE, yIndex, xyIndex, 0, 0, 0, 0, 0, 0, table, 0, false};
	yIndex = SLIP_TRACK_CELL_PLANE_STRIDE;
	result->zStride = yIndex;
	yIndex = (uint16_t)(yIndex * cellZ);
	result->zIndex = yIndex;
	xyIndex = (uint16_t)(xyIndex + yIndex);
	result->cellIndexWord = xyIndex;
	cellIndex = (cellIndex & SLIP_TRACK_CELL_INDEX_UPPER_WORD_MASK) | (uint32_t)xyIndex;
	result->cellIndex = cellIndex;
	cellIndex <<= SLIP_TRACK_CELL_REFERENCE_SHIFT;
	result->cellByteOffsetFull = cellIndex;
	cellIndex = (uint32_t)(uint16_t)cellIndex;
	result->cellByteOffset = cellIndex;
	if (tableBytes < (size_t)cellIndex + SLIP_TRACK_CELL_REFERENCE_BYTES) {
		return false;
	}
	SlipTrackWorld_WriteLE32(table + cellIndex, tableValue);
	result->storedTableValue = tableValue;
	result->returned = true;
	return true;
}

bool SlipTrackWorld_RecordVector(const uint8_t *trkBase, size_t trkSize, uint16_t coordinateOffset,
                                 uint32_t savedParameter, SlipTrackWorldRecordVector *result) {
	const uint8_t *coordinateData;
	uint16_t cellX;
	uint16_t cellY;
	uint16_t cellZ;
	uint32_t scaledX;
	uint32_t scaledY;
	uint32_t scaledZ;

	if (trkBase == 0 || result == 0 || (size_t)coordinateOffset + SLIP_TRACK_CELL_COORDINATE_BYTES > trkSize) {
		return false;
	}
	coordinateData = trkBase + coordinateOffset;
	cellX = SlipBytes_ReadLE16(coordinateData);
	cellX = (uint16_t)(cellX / SLIP_TRACK_CELL_COORDINATE_DIVISOR);
	scaledX = (uint32_t)(int32_t)(int16_t)cellX;
	scaledX <<= SLIP_TRACK_CELL_WORLD_SHIFT;
	cellY = SlipBytes_ReadLE16(coordinateData + SLIP_TRACK_CELL_COORDINATE_Y_OFFSET);
	cellY = (uint16_t)(cellY / SLIP_TRACK_CELL_COORDINATE_DIVISOR);
	cellY = (uint16_t)(cellY - SLIP_TRACK_CELL_COORDINATE_Y_BIAS);
	cellY = (uint16_t)(cellY - 1u);
	scaledY = (uint32_t)(int32_t)(int16_t)cellY;
	scaledY <<= SLIP_TRACK_CELL_WORLD_SHIFT;
	cellZ = SlipBytes_ReadLE16(coordinateData + SLIP_TRACK_CELL_COORDINATE_Z_OFFSET);
	cellZ = (uint16_t)(cellZ / SLIP_TRACK_CELL_COORDINATE_DIVISOR);
	cellZ = (uint16_t)(cellZ - SLIP_TRACK_CELL_COORDINATE_Z_BIAS);
	scaledZ = (uint32_t)(int32_t)(int16_t)cellZ;
	scaledZ <<= SLIP_TRACK_CELL_WORLD_SHIFT;
	*result = (SlipTrackWorldRecordVector){
	    savedParameter,
	    (uint32_t)coordinateOffset,
	    coordinateData,
	    SlipBytes_ReadLE16(coordinateData),
	    0,
	    SLIP_TRACK_CELL_COORDINATE_DIVISOR,
	    (uint16_t)(SlipBytes_ReadLE16(coordinateData) / SLIP_TRACK_CELL_COORDINATE_DIVISOR),
	    (uint32_t)(int32_t)(int16_t)(SlipBytes_ReadLE16(coordinateData) / SLIP_TRACK_CELL_COORDINATE_DIVISOR),
	    scaledX,
	    scaledX,
	    SlipBytes_ReadLE16(coordinateData + SLIP_TRACK_CELL_COORDINATE_Y_OFFSET),
	    0,
	    SLIP_TRACK_CELL_COORDINATE_DIVISOR,
	    (uint16_t)(SlipBytes_ReadLE16(coordinateData + SLIP_TRACK_CELL_COORDINATE_Y_OFFSET) /
	               SLIP_TRACK_CELL_COORDINATE_DIVISOR),
	    (uint16_t)((uint16_t)(SlipBytes_ReadLE16(coordinateData + SLIP_TRACK_CELL_COORDINATE_Y_OFFSET) /
	                          SLIP_TRACK_CELL_COORDINATE_DIVISOR) -
	               SLIP_TRACK_CELL_COORDINATE_Y_BIAS),
	    cellY,
	    (uint32_t)(int32_t)(int16_t)cellY,
	    scaledY,
	    scaledY,
	    SlipBytes_ReadLE16(coordinateData + SLIP_TRACK_CELL_COORDINATE_Z_OFFSET),
	    0,
	    SLIP_TRACK_CELL_COORDINATE_DIVISOR,
	    (uint16_t)(SlipBytes_ReadLE16(coordinateData + SLIP_TRACK_CELL_COORDINATE_Z_OFFSET) /
	               SLIP_TRACK_CELL_COORDINATE_DIVISOR),
	    cellZ,
	    (uint32_t)(int32_t)(int16_t)cellZ,
	    scaledZ,
	    scaledZ,
	    scaledY,
	    scaledX,
	    savedParameter,
	    true};
	return true;
}

enum {
	SLIP_DOOR_VERTICAL_NORMAL_THRESHOLD_Q14 = SLIP_Q14_ONE * 3 / 4,
	SLIP_DOOR_OPEN_GAP_SHIFT = 4,
	SLIP_DOOR_DEFAULT_SPEED = 14300,
	SLIP_DOOR_CONTACT_EVENT_SPEED = 28600,
	SLIP_DOOR_CONTACT_SLOT_MASK = 4
};

bool SlipTrackWorld_InitRefuel(uint32_t *initialized, uint32_t *refuelOffset, uint16_t trackHandle, uint8_t *trd,
                               size_t trdBytes, uint32_t trdAddress, const uint8_t *trc, size_t trcBytes,
                               const uint8_t *materials, size_t materialBytes) {
	if (*initialized != 0)
		return true;
	*initialized = UINT32_MAX;
	*refuelOffset = 0;
	if (trackHandle == 0)
		return true;
	if (trdBytes < SLIP_TRD_ROUTE_HEADER_BYTES)
		return false;
	uint32_t group = SlipBytes_ReadLE16(trd + SLIP_TRD_GROUP_TABLE_OFFSET);
	if (group == 0)
		return true;
	if (group + SLIP_TRD_TABLE_COUNT_BYTES > trdBytes)
		return false;
	const uint16_t groups = SlipBytes_ReadLE16(trd + group);
	group += SLIP_TRD_TABLE_COUNT_BYTES;
	if (groups == 0)
		return true;
	for (uint16_t g = 0; g < groups; ++g) {
		if (group + SLIP_TRACK_LINKED_HEADER_BYTES > trdBytes)
			return false;
		const uint32_t list = SlipBytes_ReadLE16(trd + group + SLIP_TRACK_LINKED_OFFSET);
		if (list != 0) {
			if (list + SLIP_TRD_TABLE_COUNT_BYTES > trdBytes)
				return false;
			const uint16_t records = SlipBytes_ReadLE16(trd + list);
			uint32_t record = list + SLIP_TRD_TABLE_COUNT_BYTES;
			for (uint16_t r = 0; r < records; ++r, record += SLIP_TRD_SECTION_BYTES) {
				if (record + SLIP_TRD_SECTION_BYTES > trdBytes)
					return false;
				const uint32_t component = SlipBytes_ReadLE16(trd + record + SLIP_TRD_SECTION_COMPONENT_OFFSET);
				if (component + SLIP_TRC_COMPONENT_PRIMITIVE_LIST_END > trcBytes)
					return false;
				uint32_t polygon = SlipBytes_ReadLE16(trc + component + SLIP_TRC_COMPONENT_PRIMITIVE_LIST_OFFSET);
				if (polygon == 0)
					continue;
				if (polygon + SLIP_TRC_TABLE_COUNT_BYTES > trcBytes)
					return false;
				const uint16_t polygons = SlipBytes_ReadLE16(trc + polygon);
				polygon += SLIP_TRC_TABLE_COUNT_BYTES;
				for (uint16_t f = 0; f < polygons; ++f) {
					if (polygon + SLIP_TRC_PRIMITIVE_HEADER_BYTES > trcBytes)
						return false;
					const uint8_t *const name = SlipDraw3D_GetMaterialName(
					    materials, materialBytes,
					    SlipBytes_ReadLE16(trc + polygon + SLIP_TRC_PRIMITIVE_MATERIAL_OFFSET));
					if (name == NULL)
						return false;

					if (memcmp(name, "REFUEL 3", 8) == 0)
						*refuelOffset = trdAddress + record;
					const uint16_t count = SlipBytes_ReadLE16(trc + polygon);
					polygon += SLIP_TRC_PRIMITIVE_HEADER_BYTES +
					           (count & SLIP_TRC_PRIMITIVE_TEXTURED
					                ? (count & SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK) * SLIP_TRC_TEXTURED_VERTEX_BYTES
					                : count * SLIP_TRC_VERTEX_INDEX_BYTES);
				}
			}
		}
		group += SlipBytes_ReadLE16(trd + group);
	}
	if (*refuelOffset == 0)
		return true;
	const uint32_t refuel = *refuelOffset - trdAddress;
	const uint32_t target = SlipBytes_ReadLE16(trd + refuel + SLIP_TRD_SECTION_ROUTE_OFFSET);
	uint32_t segment = SlipBytes_ReadLE16(trd + SLIP_TRD_ROUTE_TABLE_OFFSET);
	if (segment == 0)
		return true;
	if (segment + SLIP_TRD_TABLE_COUNT_BYTES > trdBytes)
		return false;
	const uint16_t segments = SlipBytes_ReadLE16(trd + segment);
	segment += SLIP_TRD_TABLE_COUNT_BYTES;
	for (uint16_t s = 0; s < segments; ++s, segment += SLIP_TRD_ROUTE_RECORD_BYTES) {
		if (segment + SLIP_TRD_ROUTE_RECORD_BYTES > trdBytes)
			return false;
		SlipTrackWorld_WriteLE16(trd + segment + SLIP_TRD_ROUTE_REFUEL_REACHABLE_OFFSET, 0);
		if (segment == target)
			continue;
		uint32_t cursor = segment;

		for (;;) {
			if (cursor + SLIP_TRD_ROUTE_REFUEL_BRANCH_END > trdBytes)
				return false;
			const uint32_t branch = SlipBytes_ReadLE16(trd + cursor + SLIP_TRD_ROUTE_SECOND_LINK_OFFSET);
			if (branch != 0) {
				cursor = branch;

				for (;;) {
					if (cursor + SLIP_TRD_ROUTE_REFUEL_BRANCH_END > trdBytes)
						return false;
					if (SlipBytes_ReadLE16(trd + cursor + SLIP_TRD_ROUTE_REFUEL_BOUNDARY_OFFSET) != 0)
						break;
					if (cursor == target) {
						SlipTrackWorld_WriteLE16(trd + segment + SLIP_TRD_ROUTE_REFUEL_REACHABLE_OFFSET, UINT16_MAX);
						break;
					}
					if (cursor == segment)
						break;
					cursor = SlipBytes_ReadLE16(trd + cursor);
				}
				break;
			}
			cursor = SlipBytes_ReadLE16(trd + cursor);
			if (cursor == target || cursor == segment)
				break;
		}
	}
	return true;
}

bool SlipTrackWorld_FindDoors(uint16_t trackHandle, const uint8_t *trd, size_t trdBytes, uint32_t trdAddress,
                              const uint8_t *trc, size_t trcBytes) {
	SlipTrackWorld_doorsInitialized = 0;
	SlipTrackWorld_doorCount = 0;
	if (trackHandle == 0)
		return true;
	if (trdBytes < SLIP_TRD_GROUP_TABLE_END)
		return false;
	uint32_t group = SlipBytes_ReadLE16(trd + SLIP_TRD_GROUP_TABLE_OFFSET);
	if (group == 0)
		return true;
	if (group + SLIP_TRD_TABLE_COUNT_BYTES > trdBytes)
		return false;
	const uint16_t groups = SlipBytes_ReadLE16(trd + group);
	group += SLIP_TRD_TABLE_COUNT_BYTES;
	for (uint16_t g = 0; g < groups; ++g) {
		if (group + SLIP_TRACK_LINKED_HEADER_BYTES > trdBytes)
			return false;
		const uint32_t list = SlipBytes_ReadLE16(trd + group + SLIP_TRACK_LINKED_OFFSET);
		if (list != 0) {
			if (list + SLIP_TRD_TABLE_COUNT_BYTES > trdBytes)
				return false;
			const uint16_t records = SlipBytes_ReadLE16(trd + list);
			uint32_t record = list + SLIP_TRD_TABLE_COUNT_BYTES;
			for (uint16_t r = 0; r < records; ++r, record += SLIP_TRD_SECTION_BYTES) {
				if (record + SLIP_TRD_SECTION_BYTES > trdBytes)
					return false;
				SlipView3DVec32 translation = {
				    (int32_t)SlipBytes_ReadLE32(trd + record + SLIP_TRD_SECTION_ORIGIN_X_OFFSET),
				    (int32_t)SlipBytes_ReadLE32(trd + record + SLIP_TRD_SECTION_ORIGIN_Y_OFFSET),
				    (int32_t)SlipBytes_ReadLE32(trd + record + SLIP_TRD_SECTION_ORIGIN_Z_OFFSET)};
				const uint32_t component = SlipBytes_ReadLE16(trd + record + SLIP_TRD_SECTION_COMPONENT_OFFSET);
				if (component + SLIP_TRC_COMPONENT_PRIMITIVE_LIST_END > trcBytes)
					return false;
				uint32_t polygon = SlipBytes_ReadLE16(trc + component + SLIP_TRC_COMPONENT_PRIMITIVE_LIST_OFFSET);
				if (polygon == 0)
					continue;
				if (polygon + SLIP_TRC_TABLE_COUNT_BYTES > trcBytes)
					return false;
				const uint16_t polygons = SlipBytes_ReadLE16(trc + polygon);
				polygon += SLIP_TRC_TABLE_COUNT_BYTES;
				for (uint16_t f = 0; f < polygons; ++f) {
					if (polygon + SLIP_TRC_PRIMITIVE_HEADER_BYTES > trcBytes)
						return false;
					if ((trc[polygon + SLIP_TRC_PRIMITIVE_FLAGS_OFFSET] & SLIP_TRC_PRIMITIVE_DOOR) != 0) {
						SlipView3DNormalizeLength3D normalized;
						SlipView3DNormalizeVector3D direction;
						SlipView3DMatrix matrix;
						SlipTrackWorldPointLookup first, second;

						if (!SlipView3D_NormalizeLength3D(
						        SlipBytes_ReadLE16(trc + polygon + SLIP_TRC_PRIMITIVE_NORMAL_X_OFFSET), 0,
						        SlipBytes_ReadLE16(trc + polygon + SLIP_TRC_PRIMITIVE_NORMAL_Z_OFFSET), &normalized) ||
						    !SlipView3D_BuildMatrixFromVector(&matrix, (int16_t)normalized.unitXQ14,
						                                      (int16_t)normalized.unitYQ14,
						                                      (int16_t)normalized.unitZQ14))
							return false;
						if (polygon + SLIP_TRC_PRIMITIVE_HEADER_BYTES +
						        SLIP_POLYGON_RECTANGLE_VERTICES * SLIP_TRC_VERTEX_INDEX_BYTES >
						    trcBytes)
							return false;
						uint16_t indices[SLIP_POLYGON_RECTANGLE_VERTICES];
						for (unsigned i = 0; i < SLIP_POLYGON_RECTANGLE_VERTICES; ++i)
							indices[i] = SlipBytes_ReadLE16(trc + polygon + SLIP_TRC_PRIMITIVE_HEADER_BYTES +
							                                i * SLIP_TRC_VERTEX_INDEX_BYTES);

						if (!SlipTrackWorld_PointLookup(trc + component, trc, trcBytes, indices[3], 0, 0, 0, &first) ||
						    !SlipTrackWorld_PointLookup(trc + component, trc, trcBytes, indices[0], 0, 0, 0, &second) ||
						    !SlipView3D_NormalizeVector3D(
						        second.pointXOrInput - first.pointXOrInput, second.pointYOrInput - first.pointYOrInput,
						        second.pointZOrCountMergedWithInput - first.pointZOrCountMergedWithInput, &direction))
							return false;
						int16_t nx = (int16_t)direction.unitXQ14;
						int16_t ny = (int16_t)direction.unitYQ14;
						int16_t nz = (int16_t)direction.unitZQ14;

						if (ny >= SLIP_DOOR_VERTICAL_NORMAL_THRESHOLD_Q14) {
							nx = 0;
							ny = SLIP_Q14_ONE;
							nz = 0;
						} else if (ny <= -SLIP_DOOR_VERTICAL_NORMAL_THRESHOLD_Q14) {
							nx = 0;
							ny = -SLIP_Q14_ONE;
							nz = 0;
						} else {
							if (!SlipView3D_NormalizeLength3D((uint16_t)nx, 0, (uint16_t)nz, &normalized))
								return false;
							nx = (int16_t)normalized.unitXQ14;
							ny = (int16_t)normalized.unitYQ14;
							nz = (int16_t)normalized.unitZQ14;
						}

						if (!SlipTrackWorld_PointLookup(trc + component, trc, trcBytes, indices[0], 0, 0, 0, &first) ||
						    !SlipTrackWorld_PointLookup(trc + component, trc, trcBytes, indices[1], 0, 0, 0, &second))
							return false;
						const int32_t width =
						    (int32_t)SlipView3D_VectorLength(
						        (int32_t)(second.pointXOrInput - first.pointXOrInput),
						        (int32_t)(second.pointYOrInput - first.pointYOrInput),
						        (int32_t)(second.pointZOrCountMergedWithInput - first.pointZOrCountMergedWithInput)) >>
						    1;
						if (!SlipTrackWorld_PointLookup(trc + component, trc, trcBytes, indices[0], 0, 0, 0, &first) ||
						    !SlipTrackWorld_PointLookup(trc + component, trc, trcBytes, indices[3], 0, 0, 0, &second))
							return false;
						const int32_t height =
						    (int32_t)SlipView3D_VectorLength(
						        (int32_t)(second.pointXOrInput - first.pointXOrInput),
						        (int32_t)(second.pointYOrInput - first.pointYOrInput),
						        (int32_t)(second.pointZOrCountMergedWithInput - first.pointZOrCountMergedWithInput)) >>
						    1;
						if (!SlipTrackWorld_PointLookup(trc + component, trc, trcBytes, indices[0], 0, 0, 0, &first) ||
						    !SlipTrackWorld_PointLookup(trc + component, trc, trcBytes, indices[2], 0, 0, 0, &second))
							return false;
						SlipView3DVec32 center = {
						    (int32_t)((uint32_t)((int32_t)(second.pointXOrInput + first.pointXOrInput) >> 1) +
						              (uint32_t)translation.x),
						    (int32_t)((uint32_t)((int32_t)(second.pointYOrInput + first.pointYOrInput) >> 1) +
						              (uint32_t)translation.y),
						    (int32_t)((uint32_t)((int32_t)(second.pointZOrCountMergedWithInput +
						                                   first.pointZOrCountMergedWithInput) >>
						                         1) +
						              (uint32_t)translation.z)};

						if (SlipTrackWorld_doorCount != SLIP_TRACK_DOOR_CAPACITY) {
							SlipTrackDoorRecord *const door = &SlipTrackWorld_doors[SlipTrackWorld_doorCount++];
							door->object = 0;
							door->shapeHandle = 0;
							door->trackSlotAddress = 0;
							door->halfWidth = width;
							door->halfHeight = height;
							door->directionX = (int16_t)(0u - (uint16_t)nx);
							door->directionY = (int16_t)(0u - (uint16_t)ny);
							door->directionZ = (int16_t)(0u - (uint16_t)nz);
							door->matrix = matrix;
							door->closedEndpoint = center;

							SlipView3DVec32 offset =
							    SlipView3D_ScaleVector(door->directionX, door->directionY, door->directionZ, height);
							door->planeOrigin = (SlipView3DVec32){(int32_t)((uint32_t)offset.x + (uint32_t)center.x),
							                                      (int32_t)((uint32_t)offset.y + (uint32_t)center.y),
							                                      (int32_t)((uint32_t)offset.z + (uint32_t)center.z)};
							offset = SlipView3D_ScaleVector(
							    door->directionX, door->directionY, door->directionZ,
							    (int32_t)(((uint32_t)height << 1) - (uint32_t)(height >> SLIP_DOOR_OPEN_GAP_SHIFT)));
							door->openEndpoint = (SlipView3DVec32){(int32_t)((uint32_t)offset.x + (uint32_t)center.x),
							                                       (int32_t)((uint32_t)offset.y + (uint32_t)center.y),
							                                       (int32_t)((uint32_t)offset.z + (uint32_t)center.z)};
							door->closedEndpoint =
							    (SlipView3DVec32){(int32_t)((uint32_t)door->openEndpoint.x + (uint32_t)center.x) >> 1,
							                      (int32_t)((uint32_t)door->openEndpoint.y + (uint32_t)center.y) >> 1,
							                      (int32_t)((uint32_t)door->openEndpoint.z + (uint32_t)center.z) >> 1};
							door->firstTrackRecord = trdAddress + record;

							unsigned edge = 0;
							while (edge < SLIP_TRD_SECTION_EXIT_COUNT &&
							       SlipBytes_ReadLE16(trd + record + SLIP_TRD_SECTION_FIRST_EXIT_PLANE_OFFSET +
							                          edge * SLIP_TRD_SECTION_EXIT_BYTES) != (uint16_t)polygon)
								++edge;
							if (edge == SLIP_TRD_SECTION_EXIT_COUNT)
								SlipRuntime_Fatal("FindDoors - no link!");
							door->secondTrackRecord =
							    trdAddress + SlipBytes_ReadLE16(trd + record + SLIP_TRD_SECTION_FIRST_EXIT_OFFSET +
							                                    edge * SLIP_TRD_SECTION_EXIT_BYTES);
						}
					}
					const uint16_t count = SlipBytes_ReadLE16(trc + polygon);
					polygon += SLIP_TRC_PRIMITIVE_HEADER_BYTES +
					           ((count & SLIP_TRC_PRIMITIVE_TEXTURED) != 0
					                ? (count & SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK) * SLIP_TRC_TEXTURED_VERTEX_BYTES
					                : count * SLIP_TRC_VERTEX_INDEX_BYTES);
				}
			}
		}
		group += SlipBytes_ReadLE16(trd + group);
	}
	return true;
}

void SlipTrackWorld_DoorDirection(const SlipTrackDoorRecord *door) {
	SlipTrackWorld_doorDirection = (SlipView3DVec16){door->directionX, door->directionY, door->directionZ};
	if (door->direction != 0) {
		SlipTrackWorld_doorDirection.x = (int16_t)(0u - (uint16_t)SlipTrackWorld_doorDirection.x);
		SlipTrackWorld_doorDirection.y = (int16_t)(0u - (uint16_t)SlipTrackWorld_doorDirection.y);
		SlipTrackWorld_doorDirection.z = (int16_t)(0u - (uint16_t)SlipTrackWorld_doorDirection.z);
	}
}

bool SlipTrackWorld_MoveDoor(SlipTrackDoorRecord *door, uint16_t objectOffset, SlipObject *objects,
                             size_t objectBytes) {
	SlipObjectPosition oldPosition;
	SlipObjectSetPosition position;
	if (!SlipObject_Position(objects, objectBytes, objectOffset, &oldPosition) ||
	    !SlipObject_SetPosition(objects, objectBytes, objectOffset, (uint32_t)SlipTrackWorld_doorPosition.x,
	                            (uint32_t)SlipTrackWorld_doorPosition.y, (uint32_t)SlipTrackWorld_doorPosition.z,
	                            &position))
		return false;
	if (SlipRaceCollision_Query(objectOffset) && door->direction != 0) {
		if (!SlipObject_SetPosition(objects, objectBytes, objectOffset, oldPosition.positionX, oldPosition.positionY,
		                            oldPosition.positionZ, &position))
			return false;
		door->direction = 0;
		door->speed = SLIP_DOOR_DEFAULT_SPEED;
		door->endpointDelay = 0;
		door->endpointDelayRemaining = 0;
	}
	return true;
}

uint32_t SlipTrackWorld_DoorEvent(uint32_t eventCode, uint16_t objectOffset, uint16_t otherObject,
                                  SlipTrackDoorRecord *door, SlipObject *objects, size_t objectBytes,
                                  const SlipTrackSlotRecord *slots, size_t slotBytes, uint32_t slotAddress) {
	switch ((uint16_t)eventCode) {
	case SLIP_OBJECT_EVENT_FREE:

		for (uint16_t i = 0; i < SlipTrackWorld_doorCount; ++i) {
			if (SlipTrackWorld_doors[i].object == objectOffset)
				SlipTrackWorld_doors[i].object = 0;
		}
		return 0;
	case SLIP_OBJECT_EVENT_COLLISION_BOUNCE:
		return eventCode & SLIP_OBJECT_EVENT_UPPER_WORD_MASK;
	case SLIP_OBJECT_EVENT_COLLISION_STOP: {
		SlipTrackWorldSlotListSelect selected;
		if (!SlipTrackWorld_SelectSlotListEntry(eventCode, slotAddress, objects, objectBytes, otherObject, &selected))
			return UINT32_MAX;
		if (!selected.carry) {
			if (slots == NULL || (size_t)selected.slotOffset + sizeof(*slots) > slotBytes)
				return UINT32_MAX;
			const SlipTrackSlotRecord *const other = &slots[selected.slotOffset / sizeof(*slots)];
			if ((other->flags & SLIP_DOOR_CONTACT_SLOT_MASK) != 0) {
				door->direction = 0;
				door->speed = SLIP_DOOR_CONTACT_EVENT_SPEED;
			}
		}
		return 0;
	}
	case SLIP_OBJECT_EVENT_UPDATE: {

		if (SlipRaceCollision_Query(objectOffset)) {
			door->speed = SLIP_DOOR_DEFAULT_SPEED;
			door->direction = 0;
			door->endpointDelayRemaining = 0;
			door->endpointDelay = 0;
		}
		SlipTrackWorld_DoorDirection(door);

		const uint32_t distance = (uint32_t)(((uint64_t)door->speed * SlipFrameTimer_Step()) >> SLIP_Q14_FRACTION_BITS);
		SlipView3DVec32 displacement =
		    SlipView3D_ScaleVector(SlipTrackWorld_doorDirection.x, SlipTrackWorld_doorDirection.y,
		                           SlipTrackWorld_doorDirection.z, (int32_t)distance);
		SlipObjectPosition current;
		if (!SlipObject_Position(objects, objectBytes, objectOffset, &current))
			return UINT32_MAX;
		SlipTrackWorld_doorPosition = (SlipView3DVec32){(int32_t)(current.positionX + (uint32_t)displacement.x),
		                                                (int32_t)(current.positionY + (uint32_t)displacement.y),
		                                                (int32_t)(current.positionZ + (uint32_t)displacement.z)};

		bool returning = door->direction != 0;
		SlipView3DVec32 endpoint = returning ? door->closedEndpoint : door->openEndpoint;
		SlipView3DNormalizeVector3D direction;
		SlipView3DDotProductQ14 dot;
		if (!SlipView3D_NormalizeVector3D((uint32_t)SlipTrackWorld_doorPosition.x - (uint32_t)endpoint.x,
		                                  (uint32_t)SlipTrackWorld_doorPosition.y - (uint32_t)endpoint.y,
		                                  (uint32_t)SlipTrackWorld_doorPosition.z - (uint32_t)endpoint.z, &direction))
			return UINT32_MAX;
		const int16_t side = (int16_t)SlipView3D_DotProductQ14(
		    (uint16_t)direction.unitXQ14, (uint16_t)direction.unitYQ14, (uint16_t)direction.unitZQ14,
		    (uint16_t)door->directionX, (uint16_t)door->directionY, (uint16_t)door->directionZ, &dot);
		bool reached = returning ? side < 0 : side >= 0;
		if (reached)
			SlipTrackWorld_doorPosition = endpoint;
		if (!SlipTrackWorld_MoveDoor(door, objectOffset, objects, objectBytes))
			return UINT32_MAX;
		if (reached) {

			door->direction ^= UINT32_MAX;
			door->speed = SLIP_DOOR_DEFAULT_SPEED;
			door->endpointDelayRemaining = door->endpointDelay;
		}
		return 0;
	}
	default:
		return UINT32_MAX;
	}
}
