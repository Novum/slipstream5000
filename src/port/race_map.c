#include "race_map.h"
#include "byte_order.h"
#include "fixed_point.h"
#include "gpu/map.h"
#include "gpu/renderer.h"
#include "race_display.h"
#include "raster/raster.h"
#include "renderer_host.h"
#include "renderer_projection.h"
#include "track_format.h"

enum {
	SLIP_RACE_MAP_MARKER_RADIUS = 2,
	SLIP_RACE_MAP_MARKER_OUTER_EDGE = 2 * SLIP_RACE_MAP_MARKER_RADIUS,
	SLIP_RACE_MAP_MARKER_INNER_EDGE = SLIP_RACE_MAP_MARKER_OUTER_EDGE - 1,
	SLIP_RACE_MAP_MARKER_BORDER_COLOUR = 0
};

typedef struct SlipRaceMapProjection {
	SlipView3DMatrix matrix;
	SlipView3DVec32 camera;
	const SlipDraw3DProjectState *projectState;
} SlipRaceMapProjection;

static SlipDraw3DVec32 SlipRaceMap_Transform(const SlipRaceMapProjection *projection, SlipView3DVec32 world) {
	const int32_t deltaX = (int32_t)((uint32_t)world.x - (uint32_t)projection->camera.x);
	const int32_t deltaY = (int32_t)((uint32_t)world.y - (uint32_t)projection->camera.y);
	const int32_t deltaZ = (int32_t)((uint32_t)world.z - (uint32_t)projection->camera.z);
	const uint64_t transformedX =
	    (uint64_t)((int64_t)projection->matrix.m[0] * deltaX) + (uint64_t)((int64_t)projection->matrix.m[2] * deltaZ);
	const uint64_t transformedY =
	    (uint64_t)((int64_t)projection->matrix.m[3] * deltaX) + (uint64_t)((int64_t)projection->matrix.m[5] * deltaZ);
	SlipDraw3DVec32 transformed;

	transformed.x = (int32_t)((int64_t)transformedX >> SLIP_Q14_FRACTION_BITS);
	transformed.y = (int32_t)((int64_t)transformedY >> SLIP_Q14_FRACTION_BITS);
	transformed.z = (int32_t)(0u - (uint32_t)deltaY);
	return transformed;
}

void SlipRaceMap_Reset(void) { SlipRaceGpu_ResetMap(); }

static void SlipRaceMap_DrawObject(const SlipRaceMapProjection *projection, const SlipObject *objectTable,
                                   size_t objectTableBytes, uint16_t objectOffset, uint8_t color) {
	SlipObjectPosition objectPosition;
	SlipView3DVec32 position;
	SlipDraw3DVec32 transformed;
	int32_t projectedX;
	int32_t projectedY;
	int16_t x;
	int16_t y;

	if (!SlipObject_Position(objectTable, objectTableBytes, objectOffset, &objectPosition)) {
		return;
	}
	position.x = (int32_t)objectPosition.positionX;
	position.y = (int32_t)objectPosition.positionY;
	position.z = (int32_t)objectPosition.positionZ;
	transformed = SlipRaceMap_Transform(projection, position);
	if (!SlipDraw3D_ProjectVisiblePoint(transformed, projection->projectState, &projectedX, &projectedY)) {
		return;
	}
	x = (int16_t)projectedX;
	y = (int16_t)projectedY;
	x = (int16_t)(x - SLIP_RACE_MAP_MARKER_RADIUS);
	y = (int16_t)(y - SLIP_RACE_MAP_MARKER_RADIUS);
	Raster_DrawLineClipped(SLIP_RACE_MAP_MARKER_BORDER_COLOUR, (int16_t)(x + 1), y,
	                       (int16_t)(x + SLIP_RACE_MAP_MARKER_INNER_EDGE), y);
	Raster_DrawLineClipped(
	    SLIP_RACE_MAP_MARKER_BORDER_COLOUR, (int16_t)(x + 1), (int16_t)(y + SLIP_RACE_MAP_MARKER_OUTER_EDGE),
	    (int16_t)(x + SLIP_RACE_MAP_MARKER_INNER_EDGE), (int16_t)(y + SLIP_RACE_MAP_MARKER_OUTER_EDGE));
	Raster_DrawLineClipped(SLIP_RACE_MAP_MARKER_BORDER_COLOUR, x, (int16_t)(y + 1), x,
	                       (int16_t)(y + SLIP_RACE_MAP_MARKER_INNER_EDGE));
	Raster_DrawLineClipped(SLIP_RACE_MAP_MARKER_BORDER_COLOUR, (int16_t)(x + SLIP_RACE_MAP_MARKER_OUTER_EDGE),
	                       (int16_t)(y + 1), (int16_t)(x + SLIP_RACE_MAP_MARKER_OUTER_EDGE),
	                       (int16_t)(y + SLIP_RACE_MAP_MARKER_INNER_EDGE));
	Raster_FillRectClipped(color, (int16_t)(x + 1), (int16_t)(y + 1), (int16_t)(x + SLIP_RACE_MAP_MARKER_INNER_EDGE),
	                       (int16_t)(y + SLIP_RACE_MAP_MARKER_INNER_EDGE));
	if (SlipRaceGpu_Active()) {
		Raster_FillRectClipped(SLIP_RACE_MAP_MARKER_BORDER_COLOUR, x + 1, y + 1, x + 1, y + 1);
		Raster_FillRectClipped(SLIP_RACE_MAP_MARKER_BORDER_COLOUR, x + 1, y + SLIP_RACE_MAP_MARKER_INNER_EDGE, x + 1,
		                       y + SLIP_RACE_MAP_MARKER_INNER_EDGE);
		Raster_FillRectClipped(SLIP_RACE_MAP_MARKER_BORDER_COLOUR, x + SLIP_RACE_MAP_MARKER_INNER_EDGE, y + 1,
		                       x + SLIP_RACE_MAP_MARKER_INNER_EDGE, y + 1);
		Raster_FillRectClipped(SLIP_RACE_MAP_MARKER_BORDER_COLOUR, x + SLIP_RACE_MAP_MARKER_INNER_EDGE,
		                       y + SLIP_RACE_MAP_MARKER_INNER_EDGE, x + SLIP_RACE_MAP_MARKER_INNER_EDGE,
		                       y + SLIP_RACE_MAP_MARKER_INNER_EDGE);
	} else {
		Raster_PutPixelClipped(SLIP_RACE_MAP_MARKER_BORDER_COLOUR, (int16_t)(y + 1), (int16_t)(x + 1));
		Raster_PutPixelClipped(SLIP_RACE_MAP_MARKER_BORDER_COLOUR, (int16_t)(y + SLIP_RACE_MAP_MARKER_INNER_EDGE),
		                       (int16_t)(x + 1));
		Raster_PutPixelClipped(SLIP_RACE_MAP_MARKER_BORDER_COLOUR, (int16_t)(y + 1),
		                       (int16_t)(x + SLIP_RACE_MAP_MARKER_INNER_EDGE));
		Raster_PutPixelClipped(SLIP_RACE_MAP_MARKER_BORDER_COLOUR, (int16_t)(y + SLIP_RACE_MAP_MARKER_INNER_EDGE),
		                       (int16_t)(x + SLIP_RACE_MAP_MARKER_INNER_EDGE));
	}
}

void SlipRaceMap_Draw(int32_t cameraDistance, uint8_t routeColor, uint8_t finishColor, uint16_t objectColor,
                      uint16_t playerObject, uint16_t rivalObject, int16_t centerX, int16_t centerY,
                      uint16_t playerColor, uint16_t rivalColor, const SlipView3DMaths *maths,
                      SlipDraw3DProjectState *projectState, SlipObject *objectTable, size_t objectTableBytes,
                      const uint8_t *trkData, size_t trkBytes, const uint8_t *trdData, size_t trdBytes,
                      const SlipTrackSlotRecord *slots, size_t slotCount, uint32_t slotListBaseToken,
                      uint32_t activeListToken) {
	SlipRaceMapProjection projection;
	SlipObjectMatrixCopy matrixCopy;
	SlipObjectMatrixInstall matrixInstall;
	SlipObjectSetPosition cameraPosition;
	SlipObjectPosition playerPosition;
	SlipView3DVec32 cameraOffset;
	SlipDraw3DClipAndCenter savedViewport;
	uint32_t savedMaximumDepth;
	int16_t savedClipMinX;
	int16_t savedClipMinY;
	int16_t savedClipMaxX;
	int16_t savedClipMaxY;
	SlipView3DMatrix playerMatrix;
	uint16_t routeOffset;
	size_t activeIndex;
	uint32_t nextSlotToken;

	if (cameraDistance == 0 || objectTable == NULL || maths == NULL || projectState == NULL) {
		return;
	}
	if (activeListToken < slotListBaseToken || (activeListToken - slotListBaseToken) % sizeof(*slots) != 0) {
		return;
	}
	activeIndex = (activeListToken - slotListBaseToken) / sizeof(*slots);
	if (activeIndex >= slotCount) {
		return;
	}

	savedMaximumDepth = SlipDraw3D_GetMaximumDepth();
	SlipDraw3D_SetMaximumDepth(INT32_MAX);
	Raster_GetClipRect(&savedClipMinX, &savedClipMinY, &savedClipMaxX, &savedClipMaxY);
	if (!SlipDraw3D_LoadClipAndCenter(projectState, &savedViewport)) {
		return;
	}
	SlipDraw3D_SetViewport(projectState, (int16_t)savedViewport.clipMinX, (uint16_t)savedViewport.clipMinY,
	                       (uint16_t)savedViewport.clipMaxX, (uint16_t)savedViewport.clipMaxY, centerX, centerY);
	Raster_SetClipRect((int16_t)savedViewport.clipMinX, (uint16_t)savedViewport.clipMinY,
	                   (uint16_t)savedViewport.clipMaxX, (uint16_t)savedViewport.clipMaxY);
	SlipRenderer_SetShapeFlags(&SlipRendererHost_state, 0);

	if (!SlipObject_MatrixCopy(objectTable, objectTableBytes, playerObject, &playerMatrix, &matrixCopy))
		return;
	projection.matrix = playerMatrix;
	SlipView3D_BuildYawMatrix(maths, SlipView3D_RollFromMatrix(maths, &projection.matrix), &projection.matrix);
	SlipView3D_ApplyPitchMatrix(maths, -SLIP_ANGLE_QUARTER_TURN, &projection.matrix);
	SlipView3D_OrthonormalizeForwardBasis(&projection.matrix);

	if (!SlipObject_MatrixInstall(objectTable, objectTableBytes, 0, &projection.matrix, &matrixInstall))
		return;
	cameraOffset = SlipView3D_ScaleVector(projection.matrix.m[6], projection.matrix.m[7], projection.matrix.m[8],
	                                      (int32_t)(0u - (uint32_t)cameraDistance));

	if (!SlipObject_Position(objectTable, objectTableBytes, playerObject, &playerPosition))
		return;
	projection.camera.x = (int32_t)((uint32_t)playerPosition.positionX + (uint32_t)cameraOffset.x);
	projection.camera.y = (int32_t)((uint32_t)playerPosition.positionY + (uint32_t)cameraOffset.y);
	projection.camera.z = (int32_t)((uint32_t)playerPosition.positionZ + (uint32_t)cameraOffset.z);

	if (!SlipObject_SetPosition(objectTable, objectTableBytes, 0, (uint32_t)projection.camera.x,
	                            (uint32_t)projection.camera.y, (uint32_t)projection.camera.z, &cameraPosition))
		return;
	SlipDraw3D_SetProjectionMode(projectState, SLIP_DRAW3D_PROJECTION_ORTHOGRAPHIC);
	SlipDraw3D_SetCameraDistance(projectState, (uint32_t)cameraDistance);
	projection.projectState = projectState;

	const bool nativeMap = SlipRaceDisplay_BeginMap();

	if (trkData != NULL && trkBytes >= SLIP_TRACK_LINKED_HEADER_BYTES &&
	    SlipBytes_ReadLE16(trkData + SLIP_TRACK_LINKED_OFFSET) != 0 && trdData != NULL &&
	    trdBytes >= SLIP_TRD_ROUTE_HEADER_BYTES) {
		routeOffset = SlipBytes_ReadLE16(trdData + SLIP_TRD_ROUTE_TABLE_OFFSET);
		if (routeOffset != 0 && (size_t)routeOffset + SLIP_TRD_TABLE_COUNT_BYTES <= trdBytes) {
			const uint16_t routeCount = SlipBytes_ReadLE16(trdData + routeOffset);
			size_t recordOffset = (size_t)routeOffset + SLIP_TRD_TABLE_COUNT_BYTES;

			if (nativeMap)
				SlipRaceGpu_DrawMapRoute(&projection.matrix, &projection.camera, projection.projectState, trdData,
				                         trdBytes, recordOffset, routeCount, routeColor);
			for (uint16_t index = 0;
			     !nativeMap && index < routeCount && recordOffset + SLIP_TRD_ROUTE_RECORD_BYTES <= trdBytes;
			     ++index, recordOffset += SLIP_TRD_ROUTE_RECORD_BYTES) {
				SlipView3DVec32 position;
				SlipDraw3DVec32 transformedPosition;
				SlipView3DVec32 linkedPosition;
				SlipDraw3DVec32 transformedLinkedPosition;
				const SlipDraw3DVec32 *points[2];
				uint16_t linkedOffset;

				position.x = SlipBytes_ReadLEI32(trdData + recordOffset + SLIP_TRD_POSITION_X_OFFSET);
				position.y = SlipBytes_ReadLEI32(trdData + recordOffset + SLIP_TRD_POSITION_Y_OFFSET);
				position.z = SlipBytes_ReadLEI32(trdData + recordOffset + SLIP_TRD_POSITION_Z_OFFSET);
				transformedPosition = SlipRaceMap_Transform(&projection, position);

				linkedOffset = SlipBytes_ReadLE16(trdData + recordOffset + SLIP_TRD_ROUTE_FIRST_LINK_OFFSET);
				if (linkedOffset != 0 && (size_t)linkedOffset + SLIP_TRD_POSITION_RECORD_BYTES <= trdBytes) {
					linkedPosition.x = SlipBytes_ReadLEI32(trdData + (size_t)linkedOffset + SLIP_TRD_POSITION_X_OFFSET);
					linkedPosition.y = SlipBytes_ReadLEI32(trdData + (size_t)linkedOffset + SLIP_TRD_POSITION_Y_OFFSET);
					linkedPosition.z = SlipBytes_ReadLEI32(trdData + (size_t)linkedOffset + SLIP_TRD_POSITION_Z_OFFSET);
					transformedLinkedPosition = SlipRaceMap_Transform(&projection, linkedPosition);
					points[0] = &transformedPosition;
					points[1] = &transformedLinkedPosition;
					if (SlipDraw3D_ClassifyPoints(points, 2, projection.projectState) >= 0) {
						(void)SlipRenderer_DrawLine(&SlipRendererHost_state, transformedPosition,
						                            transformedLinkedPosition, routeColor, &SlipRendererHost_lineCalls,
						                            &SlipRendererHost_flatRasterCalls);
					}
				}

				linkedOffset = SlipBytes_ReadLE16(trdData + recordOffset + SLIP_TRD_ROUTE_SECOND_LINK_OFFSET);
				if (linkedOffset != 0 && (size_t)linkedOffset + SLIP_TRD_POSITION_RECORD_BYTES <= trdBytes) {
					linkedPosition.x = SlipBytes_ReadLEI32(trdData + (size_t)linkedOffset + SLIP_TRD_POSITION_X_OFFSET);
					linkedPosition.y = SlipBytes_ReadLEI32(trdData + (size_t)linkedOffset + SLIP_TRD_POSITION_Y_OFFSET);
					linkedPosition.z = SlipBytes_ReadLEI32(trdData + (size_t)linkedOffset + SLIP_TRD_POSITION_Z_OFFSET);
					transformedLinkedPosition = SlipRaceMap_Transform(&projection, linkedPosition);
					points[0] = &transformedPosition;
					points[1] = &transformedLinkedPosition;
					if (SlipDraw3D_ClassifyPoints(points, 2, projection.projectState) >= 0) {
						(void)SlipRenderer_DrawLine(&SlipRendererHost_state, transformedPosition,
						                            transformedLinkedPosition, routeColor, &SlipRendererHost_lineCalls,
						                            &SlipRendererHost_flatRasterCalls);
					}
				}
			}
		}
		{
			const uint16_t startOffset = SlipBytes_ReadLE16(trdData + SLIP_TRD_START_OFFSET);
			if (startOffset != 0 && (size_t)startOffset + SLIP_TRD_START_RECORD_BYTES <= trdBytes) {
				SlipView3DVec32 finish;
				SlipDraw3DVec32 transformedFinish;
				int32_t projectedX;
				int32_t projectedY;
				const uint16_t finishOffset =
				    SlipBytes_ReadLE16(trdData + startOffset + SLIP_TRD_START_FINISH_LINK_OFFSET);

				if (finishOffset != 0 && (size_t)finishOffset + SLIP_TRD_POSITION_RECORD_BYTES <= trdBytes) {
					finish.x = SlipBytes_ReadLEI32(trdData + (size_t)finishOffset + SLIP_TRD_POSITION_X_OFFSET);
					finish.y = SlipBytes_ReadLEI32(trdData + (size_t)finishOffset + SLIP_TRD_POSITION_Y_OFFSET);
					finish.z = SlipBytes_ReadLEI32(trdData + (size_t)finishOffset + SLIP_TRD_POSITION_Z_OFFSET);
					if (nativeMap) {
						SlipRaceGpu_DrawMapFinish(&projection.matrix, &projection.camera, projection.projectState,
						                          finish, finishColor);
					} else {
						transformedFinish = SlipRaceMap_Transform(&projection, finish);
						if (SlipDraw3D_ProjectVisiblePoint(transformedFinish, projection.projectState, &projectedX,
						                                   &projectedY)) {
							Raster_FillRectClipped(finishColor, (int16_t)(projectedX - 1), (int16_t)(projectedY - 1),
							                       (int16_t)(projectedX + 1), (int16_t)(projectedY + 1));
						}
					}
				}
			}
		}
	}

	nextSlotToken = slots[activeIndex].nextSlotAddress;
	while (nextSlotToken != activeListToken) {
		size_t slotIndex;
		const SlipTrackSlotRecord *slot;

		if (nextSlotToken < slotListBaseToken || (nextSlotToken - slotListBaseToken) % sizeof(*slots) != 0) {
			break;
		}
		slotIndex = (nextSlotToken - slotListBaseToken) / sizeof(*slots);
		if (slotIndex >= slotCount) {
			break;
		}
		slot = &slots[slotIndex];
		nextSlotToken = slot->nextSlotAddress;
		if ((slot->flags & SLIP_TRACK_SLOT_ARTICULATED_BOUNDS) != 0 && slot->ownerObjectOffset != playerObject &&
		    slot->ownerObjectOffset != rivalObject) {
			SlipRaceMap_DrawObject(&projection, objectTable, objectTableBytes, (uint16_t)slot->ownerObjectOffset,
			                       (uint8_t)objectColor);
		}
	}
	if (rivalObject != 0) {
		SlipRaceMap_DrawObject(&projection, objectTable, objectTableBytes, rivalObject, (uint8_t)rivalColor);
	}
	SlipRaceMap_DrawObject(&projection, objectTable, objectTableBytes, playerObject, (uint8_t)playerColor);

	if (nativeMap)
		SlipRaceDisplay_EndMap();

	SlipDraw3D_SetProjectionMode(projectState, SLIP_DRAW3D_PROJECTION_PERSPECTIVE);
	SlipDraw3D_SetViewport(projectState, (int16_t)savedViewport.clipMinX, (uint16_t)savedViewport.clipMinY,
	                       (uint16_t)savedViewport.clipMaxX, (uint16_t)savedViewport.clipMaxY,
	                       (uint32_t)savedViewport.clipCenterX, (uint32_t)savedViewport.clipCenterY);
	Raster_SetClipRect(savedClipMinX, savedClipMinY, savedClipMaxX, savedClipMaxY);
	SlipDraw3D_SetMaximumDepth(savedMaximumDepth);
}
