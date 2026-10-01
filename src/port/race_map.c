#include "race_map.h"
#include "renderer_host.h"
#include "renderer_projection.h"

#include "raster.h"

#include <string.h>

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

	transformed.x = (int32_t)((int64_t)transformedX >> 14);
	transformed.y = (int32_t)((int64_t)transformedY >> 14);
	transformed.z = (int32_t)(0u - (uint32_t)deltaY);
	return transformed;
}

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
	x = (int16_t)(x - 2);
	y = (int16_t)(y - 2);
	Raster_DrawLineClipped(0, (int16_t)(x + 1), y, (int16_t)(x + 3), y);
	Raster_DrawLineClipped(0, (int16_t)(x + 1), (int16_t)(y + 4), (int16_t)(x + 3), (int16_t)(y + 4));
	Raster_DrawLineClipped(0, x, (int16_t)(y + 1), x, (int16_t)(y + 3));
	Raster_DrawLineClipped(0, (int16_t)(x + 4), (int16_t)(y + 1), (int16_t)(x + 4), (int16_t)(y + 3));
	Raster_FillRectClipped(color, (int16_t)(x + 1), (int16_t)(y + 1), (int16_t)(x + 3), (int16_t)(y + 3));
	Raster_PutPixelClipped(0, (int16_t)(y + 1), (int16_t)(x + 1));
	Raster_PutPixelClipped(0, (int16_t)(y + 3), (int16_t)(x + 1));
	Raster_PutPixelClipped(0, (int16_t)(y + 1), (int16_t)(x + 3));
	Raster_PutPixelClipped(0, (int16_t)(y + 3), (int16_t)(x + 3));
}

void SlipRaceMap_Draw(int32_t cameraDistance, uint8_t routeColor, uint8_t finishColor, uint16_t objectColor,
                      uint16_t playerObject, uint16_t rivalObject, int16_t centerX, int16_t centerY,
                      uint16_t playerColor, uint16_t rivalColor, const SlipView3DMaths *maths,
                      SlipDraw3DProjectState *projectState, SlipObject *objectTable, size_t objectTableBytes,
                      const uint8_t *trkData, size_t trkBytes, const uint8_t *trdData, size_t trdBytes,
                      const SlipTrackSlotRecord *slots, size_t slotCount, uint32_t slotListBaseAddress,
                      uint32_t activeListAddress) {
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
	uint32_t nextSlotAddress;

	if (cameraDistance == 0 || objectTable == NULL || maths == NULL || projectState == NULL) {
		return;
	}
	if (activeListAddress < slotListBaseAddress || (activeListAddress - slotListBaseAddress) % sizeof(*slots) != 0) {
		return;
	}
	activeIndex = (activeListAddress - slotListBaseAddress) / sizeof(*slots);
	if (activeIndex >= slotCount) {
		return;
	}

	savedMaximumDepth = SlipDraw3D_maximumDepth;
	SlipDraw3D_SetMaximumDepth(0x7fffffffu);
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
	SlipView3D_ApplyPitchMatrix(maths, -0x4000, &projection.matrix);
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
	SlipDraw3D_SetProjectionMode(projectState, 1);
	SlipDraw3D_SetCameraDistance(projectState, (uint32_t)cameraDistance);
	projection.projectState = projectState;

	if (trkData != NULL && trkBytes >= 6u &&
	    (uint16_t)((uint16_t)trkData[4] | (uint16_t)((uint16_t)trkData[5] << 8)) != 0 && trdData != NULL &&
	    trdBytes >= 10u) {
		routeOffset = (uint16_t)((uint16_t)trdData[8] | (uint16_t)((uint16_t)trdData[9] << 8));
		if (routeOffset != 0 && (size_t)routeOffset + 2u <= trdBytes) {
			const uint16_t routeCount = (uint16_t)((uint16_t)trdData[routeOffset] |
			                                       (uint16_t)((uint16_t)trdData[(size_t)routeOffset + 1u] << 8));
			size_t recordOffset = (size_t)routeOffset + 2u;

			for (uint16_t index = 0; index < routeCount && recordOffset + 0x32u <= trdBytes;
			     ++index, recordOffset += 0x32u) {
				SlipView3DVec32 position;
				SlipDraw3DVec32 transformedPosition;
				SlipView3DVec32 linkedPosition;
				SlipDraw3DVec32 transformedLinkedPosition;
				const SlipDraw3DVec32 *points[2];
				uint16_t linkedOffset;

				position.x =
				    (int32_t)((uint32_t)trdData[recordOffset + 0x0cu] | ((uint32_t)trdData[recordOffset + 0x0du] << 8) |
				              ((uint32_t)trdData[recordOffset + 0x0eu] << 16) |
				              ((uint32_t)trdData[recordOffset + 0x0fu] << 24));
				position.y =
				    (int32_t)((uint32_t)trdData[recordOffset + 0x10u] | ((uint32_t)trdData[recordOffset + 0x11u] << 8) |
				              ((uint32_t)trdData[recordOffset + 0x12u] << 16) |
				              ((uint32_t)trdData[recordOffset + 0x13u] << 24));
				position.z =
				    (int32_t)((uint32_t)trdData[recordOffset + 0x14u] | ((uint32_t)trdData[recordOffset + 0x15u] << 8) |
				              ((uint32_t)trdData[recordOffset + 0x16u] << 16) |
				              ((uint32_t)trdData[recordOffset + 0x17u] << 24));
				transformedPosition = SlipRaceMap_Transform(&projection, position);

				linkedOffset =
				    (uint16_t)((uint16_t)trdData[recordOffset] | (uint16_t)((uint16_t)trdData[recordOffset + 1u] << 8));
				if (linkedOffset != 0 && (size_t)linkedOffset + 0x18u <= trdBytes) {
					linkedPosition.x = (int32_t)((uint32_t)trdData[(size_t)linkedOffset + 0x0cu] |
					                             ((uint32_t)trdData[(size_t)linkedOffset + 0x0du] << 8) |
					                             ((uint32_t)trdData[(size_t)linkedOffset + 0x0eu] << 16) |
					                             ((uint32_t)trdData[(size_t)linkedOffset + 0x0fu] << 24));
					linkedPosition.y = (int32_t)((uint32_t)trdData[(size_t)linkedOffset + 0x10u] |
					                             ((uint32_t)trdData[(size_t)linkedOffset + 0x11u] << 8) |
					                             ((uint32_t)trdData[(size_t)linkedOffset + 0x12u] << 16) |
					                             ((uint32_t)trdData[(size_t)linkedOffset + 0x13u] << 24));
					linkedPosition.z = (int32_t)((uint32_t)trdData[(size_t)linkedOffset + 0x14u] |
					                             ((uint32_t)trdData[(size_t)linkedOffset + 0x15u] << 8) |
					                             ((uint32_t)trdData[(size_t)linkedOffset + 0x16u] << 16) |
					                             ((uint32_t)trdData[(size_t)linkedOffset + 0x17u] << 24));
					transformedLinkedPosition = SlipRaceMap_Transform(&projection, linkedPosition);
					points[0] = &transformedPosition;
					points[1] = &transformedLinkedPosition;
					if (SlipDraw3D_ClassifyPoints(points, 2, projection.projectState) >= 0) {

						(void)SlipRenderer_DrawLine(&SlipRendererHost_state, transformedPosition,
						                            transformedLinkedPosition, routeColor, &SlipRendererHost_lineCalls,
						                            &SlipRendererHost_flatRasterCalls);
					}
				}

				linkedOffset = (uint16_t)((uint16_t)trdData[recordOffset + 4u] |
				                          (uint16_t)((uint16_t)trdData[recordOffset + 5u] << 8));
				if (linkedOffset != 0 && (size_t)linkedOffset + 0x18u <= trdBytes) {
					linkedPosition.x = (int32_t)((uint32_t)trdData[(size_t)linkedOffset + 0x0cu] |
					                             ((uint32_t)trdData[(size_t)linkedOffset + 0x0du] << 8) |
					                             ((uint32_t)trdData[(size_t)linkedOffset + 0x0eu] << 16) |
					                             ((uint32_t)trdData[(size_t)linkedOffset + 0x0fu] << 24));
					linkedPosition.y = (int32_t)((uint32_t)trdData[(size_t)linkedOffset + 0x10u] |
					                             ((uint32_t)trdData[(size_t)linkedOffset + 0x11u] << 8) |
					                             ((uint32_t)trdData[(size_t)linkedOffset + 0x12u] << 16) |
					                             ((uint32_t)trdData[(size_t)linkedOffset + 0x13u] << 24));
					linkedPosition.z = (int32_t)((uint32_t)trdData[(size_t)linkedOffset + 0x14u] |
					                             ((uint32_t)trdData[(size_t)linkedOffset + 0x15u] << 8) |
					                             ((uint32_t)trdData[(size_t)linkedOffset + 0x16u] << 16) |
					                             ((uint32_t)trdData[(size_t)linkedOffset + 0x17u] << 24));
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
			const uint16_t startOffset = (uint16_t)((uint16_t)trdData[6] | (uint16_t)((uint16_t)trdData[7] << 8));
			if (startOffset != 0 && (size_t)startOffset + 0x20u <= trdBytes) {
				SlipView3DVec32 finish;
				SlipDraw3DVec32 transformedFinish;
				int32_t projectedX;
				int32_t projectedY;
				const uint16_t finishOffset =
				    (uint16_t)((uint16_t)trdData[(size_t)startOffset + 0x1eu] |
				               (uint16_t)((uint16_t)trdData[(size_t)startOffset + 0x1fu] << 8));

				if (finishOffset != 0 && (size_t)finishOffset + 0x18u <= trdBytes) {
					finish.x = (int32_t)((uint32_t)trdData[(size_t)finishOffset + 0x0cu] |
					                     ((uint32_t)trdData[(size_t)finishOffset + 0x0du] << 8) |
					                     ((uint32_t)trdData[(size_t)finishOffset + 0x0eu] << 16) |
					                     ((uint32_t)trdData[(size_t)finishOffset + 0x0fu] << 24));
					finish.y = (int32_t)((uint32_t)trdData[(size_t)finishOffset + 0x10u] |
					                     ((uint32_t)trdData[(size_t)finishOffset + 0x11u] << 8) |
					                     ((uint32_t)trdData[(size_t)finishOffset + 0x12u] << 16) |
					                     ((uint32_t)trdData[(size_t)finishOffset + 0x13u] << 24));
					finish.z = (int32_t)((uint32_t)trdData[(size_t)finishOffset + 0x14u] |
					                     ((uint32_t)trdData[(size_t)finishOffset + 0x15u] << 8) |
					                     ((uint32_t)trdData[(size_t)finishOffset + 0x16u] << 16) |
					                     ((uint32_t)trdData[(size_t)finishOffset + 0x17u] << 24));
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

	nextSlotAddress = slots[activeIndex].nextSlotAddress;
	while (nextSlotAddress != activeListAddress) {
		size_t slotIndex;
		const SlipTrackSlotRecord *slot;

		if (nextSlotAddress < slotListBaseAddress || (nextSlotAddress - slotListBaseAddress) % sizeof(*slots) != 0) {
			break;
		}
		slotIndex = (nextSlotAddress - slotListBaseAddress) / sizeof(*slots);
		if (slotIndex >= slotCount) {
			break;
		}
		slot = &slots[slotIndex];
		nextSlotAddress = slot->nextSlotAddress;
		if ((slot->flags & 4u) != 0 && slot->ownerObjectOffset != playerObject &&
		    slot->ownerObjectOffset != rivalObject) {
			SlipRaceMap_DrawObject(&projection, objectTable, objectTableBytes, (uint16_t)slot->ownerObjectOffset,
			                       (uint8_t)objectColor);
		}
	}
	if (rivalObject != 0) {
		SlipRaceMap_DrawObject(&projection, objectTable, objectTableBytes, rivalObject, (uint8_t)rivalColor);
	}
	SlipRaceMap_DrawObject(&projection, objectTable, objectTableBytes, playerObject, (uint8_t)playerColor);

	SlipDraw3D_SetProjectionMode(projectState, 0);
	SlipDraw3D_SetViewport(projectState, (int16_t)savedViewport.clipMinX, (uint16_t)savedViewport.clipMinY,
	                       (uint16_t)savedViewport.clipMaxX, (uint16_t)savedViewport.clipMaxY,
	                       (uint32_t)savedViewport.clipCenterX, (uint32_t)savedViewport.clipCenterY);
	Raster_SetClipRect(savedClipMinX, savedClipMinY, savedClipMaxX, savedClipMaxY);
	SlipDraw3D_SetMaximumDepth(savedMaximumDepth);
}
