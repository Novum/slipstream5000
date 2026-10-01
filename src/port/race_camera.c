#include "race_camera.h"

#include "byte_order.h"
#include "game_errors.h"
#include "race.h"
#include "race_collision.h"
#include "resource.h"
#include "resource_host.h"
#include "runtime.h"
#include "track_world.h"

#include <string.h>

#define RACE_CAMERA_MODE_COUNT 9u

static const char *const cameraFiles[10] = {"CHICAGO.CAM", "HAWAII.CAM", "TOKYO.CAM",  "NORWAY.CAM", "CAVE.CAM",
                                            "CAN.CAM",     "AMAZON.CAM", "LONDON.CAM", "EGYPT.CAM",  "NEWYORK.CAM"};

bool SlipRaceCamera_LoadPositions(SlipRaceCameraState *state, const char *const *archives, size_t archiveCount,
                                  uint16_t track) {
	(void)archives;
	(void)archiveCount;
	if (track == 0 || track > 10)
		return false;
	uint16_t cameraResource;
	if (!SlipResourceHost_Load(NULL, cameraFiles[track - 1], &cameraResource))
		SlipGame_ResourceFailure();
	const uint8_t *const cameraBytes = SlipResourceHost_Lock(NULL, cameraResource);
	SlipResourcePayload cameraPayload = SlipResourceHost_Payload(cameraResource);
	if (cameraPayload.size < 0x2d0u)
		return false;
	for (size_t cameraIndex = 0; cameraIndex < 60; ++cameraIndex) {
		state->tvPositions[cameraIndex] =
		    (SlipView3DVec32){(int32_t)SlipBytes_ReadLE32(cameraBytes + cameraIndex * 12),
		                      (int32_t)SlipBytes_ReadLE32(cameraBytes + cameraIndex * 12 + 4),
		                      (int32_t)SlipBytes_ReadLE32(cameraBytes + cameraIndex * 12 + 8)};
	}
	SlipResourceHost_Unlock(NULL, cameraResource);
	SlipResourceHost_Release(NULL, cameraResource);
	return true;
}

bool SlipRaceCamera_SelectTv(SlipRaceCameraState *state, SlipView3DVec32 craft, const uint8_t *trd, size_t trdBytes,
                             const uint8_t *components, size_t componentBytes, const uint8_t *cells, size_t cellBytes,
                             uint32_t trackDataAddress, uint32_t *selectedCameraAddressOut, int32_t *distance) {
	uint32_t selectedCameraAddress = 0;
	int32_t nearestCameraDistance = 0x7fffffff;
	SlipTrackWorldRecordSearch cameraRecordSearch;
	SlipTrackWorldSegmentCollision lineOfSightCollision;
	const SlipView3DVec32 *cameraPosition;

	for (size_t cameraIndex = 0; cameraIndex < 60; ++cameraIndex) {
		cameraPosition = &state->tvPositions[cameraIndex];
		if (cameraPosition->x == -1)
			continue;
		const int32_t cameraDistance =
		    (int32_t)SlipView3D_VectorLength((int32_t)((uint32_t)craft.x - (uint32_t)cameraPosition->x),
		                                     (int32_t)((uint32_t)craft.y - (uint32_t)cameraPosition->y),
		                                     (int32_t)((uint32_t)craft.z - (uint32_t)cameraPosition->z));
		if (cameraDistance < nearestCameraDistance) {
			nearestCameraDistance = cameraDistance;
			selectedCameraAddress = 0x42e1cu + (uint32_t)cameraIndex * 12u;
		}
	}
	if (selectedCameraAddress == 0 || SlipRaceCollision_segmentQuery == NULL)
		return false;
	cameraPosition = &state->tvPositions[(selectedCameraAddress - 0x42e1cu) / 12u];

	if (!SlipTrackWorld_RecordSearch(trd, trdBytes, components, componentBytes, cells, cellBytes, trackDataAddress, 0,
	                                 cameraPosition->x, cameraPosition->y, cameraPosition->z, &cameraRecordSearch))
		return false;
	if (cameraRecordSearch.selectedRecordAddress != 0) {
		SlipRaceCollision_segmentQuery(trd, trdBytes, trackDataAddress, components, componentBytes, cells, cellBytes,
		                               craft, *cameraPosition, &lineOfSightCollision);
		if (lineOfSightCollision.transitionBlocked) {
			const uint32_t rejectedCameraAddress = selectedCameraAddress;
			selectedCameraAddress = 0;
			nearestCameraDistance = 0x7fffffff;

			for (size_t cameraIndex = 0; cameraIndex < 60; ++cameraIndex) {
				cameraPosition = &state->tvPositions[cameraIndex];
				const uint32_t cameraAddress = 0x42e1cu + (uint32_t)cameraIndex * 12u;
				if (cameraPosition->x == -1 || cameraAddress == rejectedCameraAddress)
					continue;
				const int32_t cameraDistance =
				    (int32_t)SlipView3D_VectorLength((int32_t)((uint32_t)craft.x - (uint32_t)cameraPosition->x),
				                                     (int32_t)((uint32_t)craft.y - (uint32_t)cameraPosition->y),
				                                     (int32_t)((uint32_t)craft.z - (uint32_t)cameraPosition->z));
				if (cameraDistance < nearestCameraDistance) {
					nearestCameraDistance = cameraDistance;
					selectedCameraAddress = cameraAddress;
				}
			}
			if (selectedCameraAddress == 0)
				return false;
			cameraPosition = &state->tvPositions[(selectedCameraAddress - 0x42e1cu) / 12u];
			SlipRaceCollision_segmentQuery(trd, trdBytes, trackDataAddress, components, componentBytes, cells,
			                               cellBytes, craft, *cameraPosition, &lineOfSightCollision);
			if (lineOfSightCollision.transitionBlocked)
				selectedCameraAddress =
				    state->tvPreviousCamera != 0xffffffffu ? state->tvPreviousCamera : rejectedCameraAddress;
		}
	}
	state->tvPreviousCamera = selectedCameraAddress;
	*selectedCameraAddressOut = selectedCameraAddress;
	*distance = nearestCameraDistance;
	return true;
}

bool SlipRaceCamera_Finish(SlipRaceCameraState *state, SlipObject *objects, size_t objectBytes, uint16_t racerObject,
                           uint16_t racePosition) {
	SlipObjectPosition craft;
	SlipObjectSetPosition setPosition;
	SlipObjectMatrixInstall install;
	SlipView3DScaleVector3D scaled;
	SlipView3DNormalizeLength3D normalized;
	const SlipView3DVec32 *selectedCameraPosition = NULL;
	int32_t nearestCameraDistance = INT32_MAX;
	state->finishPosition = racePosition & 0x7fffu;
	if (!SlipObject_Position(objects, objectBytes, racerObject, &craft))
		return false;
	for (size_t cameraIndex = 0; cameraIndex < 60; ++cameraIndex) {
		const SlipView3DVec32 *const cameraPosition = &state->tvPositions[cameraIndex];
		if (cameraPosition->x == -1)
			continue;
		const int32_t distance =
		    (int32_t)SlipView3D_VectorLength((int32_t)(craft.positionX - (uint32_t)cameraPosition->x),
		                                     (int32_t)(craft.positionY - (uint32_t)cameraPosition->y),
		                                     (int32_t)(craft.positionZ - (uint32_t)cameraPosition->z));
		if (distance < nearestCameraDistance) {
			nearestCameraDistance = distance;
			selectedCameraPosition = cameraPosition;
		}
	}
	if (selectedCameraPosition == NULL ||
	    !SlipObject_SetPosition(objects, objectBytes, 0, (uint32_t)selectedCameraPosition->x,
	                            (uint32_t)selectedCameraPosition->y, (uint32_t)selectedCameraPosition->z, &setPosition))
		return false;

	if (!SlipView3D_ScaleVector3D(craft.positionX - (uint32_t)selectedCameraPosition->x,
	                              craft.positionY - (uint32_t)selectedCameraPosition->y,
	                              craft.positionZ - (uint32_t)selectedCameraPosition->z, &scaled) ||
	    !SlipView3D_NormalizeLength3D(scaled.scaledX, scaled.scaledY, scaled.scaledZ, &normalized) ||
	    !SlipView3D_BuildMatrixFromVector(&state->finishMatrix, (int16_t)normalized.unitXQ14,
	                                      (int16_t)normalized.unitYQ14, (int16_t)normalized.unitZQ14))
		return false;
	return SlipObject_MatrixInstall(objects, objectBytes, 0, &state->finishMatrix, &install);
}

void SlipRaceCamera_ActivateTv(SlipRaceCameraState *state) {
	state->tvSoundDistance = 0x7fffffff;
	state->tvSoundCamera = 0;
}

uint32_t SlipRaceCamera_TvScale(uint32_t distance) {
	uint32_t zoomDistanceExcess = distance - 0x2620u;
	if ((int32_t)zoomDistanceExcess < 0)
		zoomDistanceExcess = 0;
	if ((int32_t)zoomDistanceExcess > 0x477c0)
		zoomDistanceExcess = 0x477c0;
	return (uint32_t)(((uint64_t)zoomDistanceExcess * 0x30000u) / 0x477c0u) + 0x10000u;
}

bool SlipRaceCamera_TvTransform(const SlipRaceCameraState *state, SlipObject *objects, size_t objectBytes,
                                uint16_t racerObject, uint32_t selectedCameraAddress, uint32_t *distance) {
	SlipObjectSetPosition setPosition;
	SlipObjectPosition craft;
	SlipObjectMatrixInstall install;
	SlipView3DNormalizeVector3D normalized;
	SlipView3DMatrix matrix;
	const SlipView3DVec32 *position;
	if (selectedCameraAddress < 0x42e1cu || selectedCameraAddress >= 0x430ecu ||
	    (selectedCameraAddress - 0x42e1cu) % 12u != 0)
		return false;
	position = &state->tvPositions[(selectedCameraAddress - 0x42e1cu) / 12u];
	if (!SlipObject_SetPosition(objects, objectBytes, 0, (uint32_t)position->x, (uint32_t)position->y,
	                            (uint32_t)position->z, &setPosition) ||
	    !SlipObject_Position(objects, objectBytes, racerObject, &craft))
		return false;
	if (!SlipView3D_NormalizeVector3D(craft.positionX - (uint32_t)position->x, craft.positionY - (uint32_t)position->y,
	                                  craft.positionZ - (uint32_t)position->z, &normalized))
		return false;
	*distance = normalized.vectorLength;
	if (!SlipView3D_BuildMatrixFromVector(&matrix, (int16_t)(uint16_t)normalized.unitXQ14,
	                                      (int16_t)(uint16_t)normalized.unitYQ14,
	                                      (int16_t)(uint16_t)normalized.unitZQ14))
		return false;
	return SlipObject_MatrixInstall(objects, objectBytes, 0, &matrix, &install);
}

bool SlipRaceCamera_External(SlipRaceCameraState *state, SlipObject *objects, size_t objectBytes, uint16_t racerObject,
                             const uint8_t *trd, size_t trdBytes, const uint8_t *components, size_t componentBytes,
                             const uint8_t *cells, size_t cellBytes, uint32_t trackDataAddress) {
	SlipObjectPosition craft;
	SlipObjectMatrixInstall install;
	SlipObjectSetPosition setPosition;
	SlipView3DMatrix *const matrix = &state->externalMatrix;
	int32_t clearance, distance;
	SlipView3DVec32 offset;
	if (!SlipObject_Position(objects, objectBytes, racerObject, &craft))
		return false;
	if (!SlipObject_MatrixInstall(objects, objectBytes, 0, matrix, &install) ||
	    !SlipRaceCamera_GroundClearance(
	        trd, trdBytes, components, componentBytes, cells, cellBytes, trackDataAddress,
	        (SlipView3DVec32){(int32_t)craft.positionX, (int32_t)craft.positionY, (int32_t)craft.positionZ},
	        (int16_t)-matrix->m[6], (int16_t)-matrix->m[7], (int16_t)-matrix->m[8], &clearance))
		return false;
	distance = state->externalDistance;
	if (distance > clearance) {
		distance = clearance;
		state->externalDistance = clearance < 0x2250 ? 0x2250 : clearance;
	}
	offset = SlipView3D_TransformPositionByColumns(matrix, (SlipView3DVec32){0, 0, (int32_t)(0u - (uint32_t)distance)});
	return SlipObject_SetPosition(objects, objectBytes, 0, craft.positionX + (uint32_t)offset.x,
	                              craft.positionY + (uint32_t)offset.y, craft.positionZ + (uint32_t)offset.z,
	                              &setPosition);
}

void SlipRaceCamera_ExternalControls(SlipRaceCameraState *state, uint16_t frameStep, const bool held[256],
                                     const SlipView3DMaths *maths) {
	const int32_t distanceIncrement = (int32_t)(((int64_t)state->externalDistance * (int16_t)frameStep) >> 13);
	int32_t distance = state->externalDistance;
	SlipView3DMatrix *const matrix = &state->externalMatrix;
	int16_t rotationAngle;
	if (held[0x4e]) {
		distance = (int32_t)((uint32_t)distance - (uint32_t)distanceIncrement);
		if (distance < 0x2250)
			distance = 0x2250;
	}
	if (held[0x4a]) {
		distance = (int32_t)((uint32_t)distance + (uint32_t)distanceIncrement);
		if ((uint32_t)distance > 0xbea0u)
			distance = 0xbea0;
	}
	state->externalDistance = distance;
	rotationAngle = (int16_t)(((int32_t)(held[0x2a] ? 0x400 : 0x4000) * (int16_t)frameStep) >> 14);
	if (held[0x52])
		SlipView3D_ApplyColumn0Column2Rotation(maths, rotationAngle, matrix);
	if (held[0x53])
		SlipView3D_ApplyColumn0Column2Rotation(maths, (int16_t)((0u - (uint16_t)rotationAngle) & 0xffffu), matrix);
	if (held[0x49] && matrix->m[7] > -0x3c00)
		SlipView3D_ApplyPitchMatrix(maths, (int16_t)((int16_t)((0u - (uint16_t)rotationAngle) & 0xffffu) >> 1), matrix);
	if (held[0x51] && matrix->m[7] < 0x3c00)
		SlipView3D_ApplyPitchMatrix(maths, (int16_t)(rotationAngle >> 1), matrix);
	SlipView3D_OrthonormalizeForwardBasis(matrix);
}

void SlipRaceCamera_ViewportShake(uint16_t shakeTimer, int32_t *centerX, int32_t *centerY) {
	if (shakeTimer == 0 || centerX == NULL || centerY == NULL) {
		return;
	}
	*centerX += (int32_t)(SlipRandom_Next() & 7u) - 3;
	*centerY += (int32_t)(SlipRandom_Next() & 7u) - 3;
}

bool SlipRaceCamera_MainViewport(uint16_t viewId, uint32_t gameMode, uint32_t splitView, uint16_t shakeView1,
                                 uint16_t shakeView2, SlipDraw3DProjectState *projectState) {
	SlipRandomState savedRandomState;
	int32_t centerX;
	int32_t centerY;
	int32_t minX;
	int32_t minY;
	int32_t maxX;
	int32_t maxY;

	if (projectState == NULL) {
		return false;
	}
	savedRandomState = SlipRandom_GetState();
	if (gameMode == 1u) {
		centerX = 0x00a0;
		if (viewId == 2u) {
			centerY = 0x0090;
			SlipRaceCamera_ViewportShake(shakeView2, &centerX, &centerY);
			minX = 0x0004;
			minY = 0x0065;
			maxX = 0x013b;
			maxY = 0x00bb;
		} else {
			centerY = 0x002b;
			SlipRaceCamera_ViewportShake(shakeView1, &centerX, &centerY);
			minX = 0x0004;
			minY = 0;
			maxX = 0x013b;
			maxY = 0x0057;
		}
	} else {
		centerX = 0x00a0;
		centerY = 0x0057;
		SlipRaceCamera_ViewportShake(shakeView1, &centerX, &centerY);
		minX = 0x0004;
		maxX = 0x013b;
		maxY = 0x00a6;
		if (splitView != 0) {
			minY = 0x0020;
			centerY += 0x000c;
		} else {
			minY = 0x0008;
		}
	}
	SlipDraw3D_SetViewport(projectState, minX, minY, maxX, maxY, centerX, centerY);
	SlipRandom_SetState(savedRandomState.stateWords, (uint16_t)savedRandomState.stateTail);
	return true;
}

typedef struct RaceCameraModeEntry {
	const char *modeName;
	uint16_t viewOneSelectionEvent;
	uint16_t viewTwoSelectionEvent;
	bool hasActivationCallback;
} RaceCameraModeEntry;

static const RaceCameraModeEntry kRaceCameraModeTable[RACE_CAMERA_MODE_COUNT] = {
    {"", 0x0000u, 0x0000u, false},
    {"", 0x0000u, 0x0000u, false},
    {"", 0x0000u, 0x0000u, false},
    {"Cockpit View", 0x003bu, 0x0040u, false},
    {"Chase Camera", 0x003cu, 0x0041u, true},
    {"Rear View", 0x003du, 0x0042u, false},
    {"TV Camera", 0x003eu, 0x0043u, true},
    {"External Camera", 0x003fu, 0x0044u, false},
    {"Camera Dropped", 0x003cu, 0x0041u, false},
};

const char *SlipRaceCamera_ModeName(uint16_t mode) { return kRaceCameraModeTable[mode].modeName; }

uint16_t SlipRaceCamera_projectileObject;

static uint32_t SlipRaceCamera_ProjectileDeleted(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue,
                                                 uint32_t eventFlags, uint16_t object, uintptr_t dispatchData,
                                                 uint32_t dispatchFrame) {
	(void)eventPayload;
	(void)eventValue;
	(void)eventFlags;
	(void)dispatchData;
	(void)dispatchFrame;
	if (object == SlipRaceCamera_projectileObject)
		SlipRaceCamera_projectileObject = 0;
	return eventCode;
}

void SlipRaceCamera_TrackProjectile(uint16_t projectile, uint16_t shooter, uint16_t playerOne) {
	if (shooter == playerOne)
		SlipRaceCamera_projectileObject = projectile;
}

void SlipRaceCamera_Reset(SlipRaceCameraState *state) {
	if (state == NULL) {
		return;
	}

	state->viewOneMode = 0;
	state->viewTwoMode = 0;
	state->lapNotificationTimer[0] = 0;
	state->lapNotificationTimer[1] = 0;
	state->lapTimeTimer[0] = 0;
	state->lapTimeTimer[1] = 0;
	state->shake[0] = 0;
	state->shake[1] = 0;

	state->viewOneZoomDistance = 0xe000;
	state->viewTwoZoomDistance = 0xe000;

	SlipRaceCamera_projectileObject = 0;
	SlipObject_SetServer(1, SlipRaceCamera_ProjectileDeleted);

	state->viewOneModeNameTimer = 0x1770;
	state->viewTwoModeNameTimer = 0x1770;
}

void SlipRaceCamera_DestroyRacer(SlipRaceCameraState *state, uint16_t racerObject, uint16_t playerOneObject,
                                 uint16_t playerTwoObject, uint16_t thirdObject) {
	if (racerObject == thirdObject) {
		if (SlipRace_racerCount == 2u) {
			SlipRace_playerTwoFinished = 1;
			SlipRace_playerTwoFinishDelay = 0x0fa0;
		}
		return;
	}
	if (racerObject == playerOneObject) {

		state->viewOneZoomDistance = 0x0c00;
		state->viewOneMode = 1;
		state->lapNotificationTimer[0] = 0;
		state->viewOneModeNameTimer = 0;
		if (SlipRace_racerCount == 2u || SlipRace_gameMode == 0) {
			SlipRace_playerOneFinished = 1;
			SlipRace_playerOneFinishDelay = 0x0fa0;
		}
		return;
	}
	if (racerObject != playerTwoObject)
		return;

	state->viewTwoZoomDistance = 0x0c00;
	state->viewTwoMode = 1;
	state->lapNotificationTimer[1] = 0;
	state->viewTwoModeNameTimer = 0;
	if (SlipRace_gameMode == 0) {
		SlipRace_playerTwoFinished = 1;
		SlipRace_playerTwoFinishDelay = 0x0fa0;
	}
}

void SlipRaceCamera_FinishRacer(SlipRaceCameraState *state, uint16_t racerObject, uint16_t playerOneObject,
                                uint16_t playerTwoObject) {
	if (racerObject == playerOneObject) {
		state->viewOneMode = 2;
		state->lapNotificationTimer[0] = 0;
		state->viewOneModeNameTimer = 0;
	} else if (racerObject == playerTwoObject) {
		state->viewTwoMode = 2;
		state->lapNotificationTimer[1] = 0;
		state->viewTwoModeNameTimer = 0;
	}
}

void SlipRaceCamera_StoreLapTime(SlipRaceCameraState *state, uint32_t playerId, uint32_t lapTime) {
	uint32_t playerIndex;

	playerIndex = playerId == 1u ? 0u : 1u;
	state->lapTimeTimer[playerIndex] = 4000u;
	state->lapTime[playerIndex] = lapTime;
}

void SlipRaceCamera_StoreLapNumber(SlipRaceCameraState *state, uint32_t playerId, uint16_t lapNumber) {
	uint32_t playerIndex;

	playerIndex = playerId - 1u;
	state->lapNotificationTimer[playerIndex] = 2000u;
	state->lapNotificationValue[playerIndex] = lapNumber;
}

bool SlipRaceCamera_Event(SlipRaceCameraState *state, uint16_t selectionEvent, SlipRaceCameraActivation activate,
                          void *context) {
	uint16_t modeIndex;

	if (state == NULL) {
		return false;
	}
	for (modeIndex = 0; modeIndex < RACE_CAMERA_MODE_COUNT; ++modeIndex) {
		const RaceCameraModeEntry *const entry = &kRaceCameraModeTable[modeIndex];

		if (modeIndex != state->viewOneMode && selectionEvent == entry->viewOneSelectionEvent) {
			state->viewOneMode = modeIndex;
			state->viewOneModeNameTimer = 0x1770u;

			if (entry->hasActivationCallback && (activate == NULL || !activate(context, modeIndex, 1u)))
				return false;
			return true;
		}

		if (modeIndex != state->viewTwoMode && selectionEvent == entry->viewTwoSelectionEvent) {
			state->viewTwoMode = modeIndex;
			state->viewTwoModeNameTimer = 0x1770u;

			if (entry->hasActivationCallback && (activate == NULL || !activate(context, modeIndex, 2u)))
				return false;
			return true;
		}
	}

	return true;
}

bool SlipRaceCamera_PollKeys(SlipRaceCameraState *state, uint32_t gameMode, bool pressed[256],
                             SlipRaceCameraActivation activate, void *context) {
	for (uint16_t mode = 0; mode < RACE_CAMERA_MODE_COUNT; ++mode) {
		const RaceCameraModeEntry *const entry = &kRaceCameraModeTable[mode];
		if (mode != state->viewOneMode && state->viewOneMode != 1u && state->viewOneMode != 2u &&
		    pressed[entry->viewOneSelectionEvent]) {
			pressed[entry->viewOneSelectionEvent] = false;
			state->viewOneMode = mode;
			state->viewOneModeNameTimer = 0x1770u;
			if (entry->hasActivationCallback && (activate == NULL || !activate(context, mode, 1u)))
				return false;
		}
		if (gameMode == 1u && mode != state->viewTwoMode && state->viewTwoMode != 1u && state->viewTwoMode != 2u &&
		    pressed[entry->viewTwoSelectionEvent]) {
			pressed[entry->viewTwoSelectionEvent] = false;
			state->viewTwoMode = mode;
			state->viewTwoModeNameTimer = 0x1770u;
			if (entry->hasActivationCallback && (activate == NULL || !activate(context, mode, 2u)))
				return false;
		}
	}
	return true;
}

static bool SlipRaceCamera_AccumulateRecordRayClearance(const uint8_t *record, size_t recordBytesRemaining,
                                                        const uint8_t *componentBase, size_t componentBaseBytes,
                                                        SlipView3DVec32 query, int16_t dirX, int16_t dirY, int16_t dirZ,
                                                        int32_t *minimumClearance) {
	SlipView3DVec32 queryRelativeToComponent;
	const uint8_t *component;
	size_t componentOffset;
	uint16_t primitiveListOffset;
	uint16_t primitiveCount;
	size_t primitiveOffset;
	uint16_t remainingPrimitiveCount;

	if (record == NULL || componentBase == NULL || minimumClearance == NULL || recordBytesRemaining < 0x1eu) {
		return false;
	}

	queryRelativeToComponent.x = query.x - (int32_t)((uint32_t)record[0x12u] | ((uint32_t)record[0x13u] << 8) |
	                                                 ((uint32_t)record[0x14u] << 16) | ((uint32_t)record[0x15u] << 24));
	queryRelativeToComponent.y = query.y - (int32_t)((uint32_t)record[0x16u] | ((uint32_t)record[0x17u] << 8) |
	                                                 ((uint32_t)record[0x18u] << 16) | ((uint32_t)record[0x19u] << 24));
	queryRelativeToComponent.z = query.z - (int32_t)((uint32_t)record[0x1au] | ((uint32_t)record[0x1bu] << 8) |
	                                                 ((uint32_t)record[0x1cu] << 16) | ((uint32_t)record[0x1du] << 24));

	componentOffset = (uint16_t)((uint16_t)record[0x02u] | ((uint16_t)record[0x03u] << 8));
	if (componentOffset == 0 || componentOffset > componentBaseBytes || componentBaseBytes - componentOffset < 0x06u) {
		return false;
	}
	component = componentBase + componentOffset;

	primitiveListOffset = (uint16_t)((uint16_t)component[0x04u] | ((uint16_t)component[0x05u] << 8));
	if (primitiveListOffset == 0) {
		return true;
	}
	if ((size_t)primitiveListOffset > componentBaseBytes || componentBaseBytes - primitiveListOffset < 2u) {
		return false;
	}
	primitiveCount = (uint16_t)((uint16_t)componentBase[primitiveListOffset] |
	                            ((uint16_t)componentBase[(size_t)primitiveListOffset + 1u] << 8));
	primitiveOffset = (size_t)primitiveListOffset + 2u;

	for (remainingPrimitiveCount = primitiveCount; remainingPrimitiveCount != 0; --remainingPrimitiveCount) {
		const uint8_t *primitive;
		uint16_t descriptor;
		uint32_t primitiveByteCount;
		int32_t facingNormalDot;
		int16_t rayPlaneDivisor;
		int32_t planeDistance;
		bool nearPlane;
		uint32_t clearanceDistance = 0;
		uint32_t planeIntersectionDistance;
		SlipView3DVec32 planeIntersection;
		SlipTrackWorldPointLookup pointLookup;
		SlipTrackWorldSideTestVisit sideVisits[32];
		SlipTrackWorldSideTest sideTest;

		if (primitiveOffset > componentBaseBytes || componentBaseBytes - primitiveOffset < 0x0eu) {
			return false;
		}
		primitive = componentBase + primitiveOffset;
		descriptor = (uint16_t)((uint16_t)primitive[0] | ((uint16_t)primitive[1] << 8));

		if (descriptor & 0x8000u) {
			primitiveByteCount = (uint32_t)(descriptor & 0x7fffu) * 6u + 0x0cu;
		} else {
			primitiveByteCount = (uint32_t)descriptor * 2u + 0x0cu;
		}

		if ((primitive[0x08u] & 0x41u) == 0) {

			const int32_t normalDirectionDot =
			    (int32_t)(int16_t)((uint16_t)primitive[0x02u] | ((uint16_t)primitive[0x03u] << 8)) * dirX +
			    (int32_t)(int16_t)((uint16_t)primitive[0x04u] | ((uint16_t)primitive[0x05u] << 8)) * dirY +
			    (int32_t)(int16_t)((uint16_t)primitive[0x06u] | ((uint16_t)primitive[0x07u] << 8)) * dirZ;

			if (normalDirectionDot >= 0) {
				primitiveOffset += primitiveByteCount;
				continue;
			}
			facingNormalDot = -(int16_t)((normalDirectionDot >> 14) + ((normalDirectionDot >> 13) & 1));

			if (facingNormalDot < 0x10) {
				primitiveOffset += primitiveByteCount;
				continue;
			}
			rayPlaneDivisor = (int16_t)facingNormalDot;

			if (!SlipTrackWorld_PointLookup(component, componentBase, componentBaseBytes,
			                                (uint16_t)((uint16_t)primitive[0x0cu] | ((uint16_t)primitive[0x0du] << 8)),
			                                0, 0, 0, &pointLookup)) {
				return false;
			}
			if (pointLookup.carry) {
				primitiveOffset += primitiveByteCount;
				continue;
			}
			{

				const int32_t planePointDeltaX = queryRelativeToComponent.x - (int32_t)pointLookup.pointXOrInput;
				const int32_t planePointDeltaY = queryRelativeToComponent.y - (int32_t)pointLookup.pointYOrInput;
				const int32_t planePointDeltaZ =
				    queryRelativeToComponent.z - (int32_t)pointLookup.pointZOrCountMergedWithInput;

				const int64_t planeSum = (int64_t)planePointDeltaX *
				                             (int16_t)((uint16_t)primitive[0x02u] | ((uint16_t)primitive[0x03u] << 8)) +
				                         (int64_t)planePointDeltaY *
				                             (int16_t)((uint16_t)primitive[0x04u] | ((uint16_t)primitive[0x05u] << 8)) +
				                         (int64_t)planePointDeltaZ *
				                             (int16_t)((uint16_t)primitive[0x06u] | ((uint16_t)primitive[0x07u] << 8));

				planeDistance = (int32_t)((planeSum >> 14) + ((planeSum >> 13) & 1));
			}

			if (planeDistance < 0) {
				primitiveOffset += primitiveByteCount;
				continue;
			}

			nearPlane = planeDistance - 0x988 < 0;
			if (!nearPlane) {
				const int32_t standoffDistance = planeDistance - 0x988;
				uint64_t standoffDividend;
				uint32_t standoffDivisor;

				if (standoffDistance >= *minimumClearance) {
					primitiveOffset += primitiveByteCount;
					continue;
				}

				standoffDividend = (uint64_t)(uint32_t)standoffDistance << 30;
				standoffDivisor = (uint32_t)(uint16_t)rayPlaneDivisor << 16;
				if (standoffDivisor == 0 || (uint32_t)(standoffDividend >> 32) >= standoffDivisor) {
					primitiveOffset += primitiveByteCount;
					continue;
				}
				clearanceDistance = (uint32_t)(standoffDividend / standoffDivisor);
				if ((int32_t)clearanceDistance < 0) {
					primitiveOffset += primitiveByteCount;
					continue;
				}

				if ((int32_t)clearanceDistance >= *minimumClearance) {
					primitiveOffset += primitiveByteCount;
					continue;
				}
			} else {

				clearanceDistance = (uint32_t)(planeDistance - 0x988);
			}

			{
				const uint64_t planeDistanceDividend = (uint64_t)(uint32_t)planeDistance << 30;
				const uint32_t planeDistanceDivisor = (uint32_t)(uint16_t)rayPlaneDivisor << 16;

				if (planeDistanceDivisor == 0 || (uint32_t)(planeDistanceDividend >> 32) >= planeDistanceDivisor) {
					primitiveOffset += primitiveByteCount;
					continue;
				}
				planeIntersectionDistance = (uint32_t)(planeDistanceDividend / planeDistanceDivisor);
				if ((int32_t)planeIntersectionDistance < 0) {
					primitiveOffset += primitiveByteCount;
					continue;
				}
			}

			planeIntersection.x =
			    queryRelativeToComponent.x + (int32_t)(((int64_t)dirX * (int32_t)planeIntersectionDistance) >> 14);
			planeIntersection.y =
			    queryRelativeToComponent.y + (int32_t)(((int64_t)dirY * (int32_t)planeIntersectionDistance) >> 14);
			planeIntersection.z =
			    queryRelativeToComponent.z + (int32_t)(((int64_t)dirZ * (int32_t)planeIntersectionDistance) >> 14);

			if (!SlipTrackWorld_SideTest(component, componentBase, componentBaseBytes, primitive,
			                             componentBaseBytes - primitiveOffset, planeIntersection, sideVisits,
			                             sizeof(sideVisits) / sizeof(sideVisits[0]), &sideTest)) {
				return false;
			}
			if (sideTest.outside) {
				primitiveOffset += primitiveByteCount;
				continue;
			}
			if (!nearPlane) {

				*minimumClearance = (int32_t)clearanceDistance;
			} else {

				uint32_t nearPlaneCorrectionDistance;
				const uint64_t nearPlaneDividend = (uint64_t)(uint32_t)(-(int32_t)clearanceDistance) << 30;
				const uint32_t nearPlaneDivisor = (uint32_t)(uint16_t)rayPlaneDivisor << 16;

				if (nearPlaneDivisor == 0 || (uint32_t)(nearPlaneDividend >> 32) >= nearPlaneDivisor) {
					primitiveOffset += primitiveByteCount;
					continue;
				}
				nearPlaneCorrectionDistance = (uint32_t)(nearPlaneDividend / nearPlaneDivisor);
				if (-(int32_t)nearPlaneCorrectionDistance < 0) {
					*minimumClearance = 0x2250;
				} else {
					*minimumClearance = -(int32_t)nearPlaneCorrectionDistance;
				}
			}
		}
		primitiveOffset += primitiveByteCount;
	}
	return true;
}

bool SlipRaceCamera_GroundClearance(const uint8_t *trdBase, size_t trdBytes, const uint8_t *componentBase,
                                    size_t componentBaseBytes, const uint8_t *cellTable, size_t cellTableBytes,
                                    uint32_t trackDataBaseAddress, SlipView3DVec32 query, int16_t dirX, int16_t dirY,
                                    int16_t dirZ, int32_t *clearance) {
	SlipTrackWorldRecordSearch recordSearch;
	uint32_t recordAddress;
	size_t recordOffset;
	const uint8_t *record;
	int32_t minimumClearance;
	size_t neighborLinkFieldOffset;

	if (trdBase == NULL || componentBase == NULL || clearance == NULL) {
		return false;
	}

	if (!SlipTrackWorld_RecordSearch(trdBase, trdBytes, componentBase, componentBaseBytes, cellTable, cellTableBytes,
	                                 trackDataBaseAddress, 0, query.x, query.y, query.z, &recordSearch)) {
		return false;
	}
	recordAddress = recordSearch.selectedRecordAddress;

	if (recordSearch.carryOut || recordAddress == 0) {
		*clearance = 0x2250;
		return true;
	}
	if (recordAddress < trackDataBaseAddress) {
		return false;
	}
	recordOffset = recordAddress - trackDataBaseAddress;
	if (recordOffset > trdBytes || trdBytes - recordOffset < 0x1eu) {
		return false;
	}
	record = trdBase + recordOffset;

	minimumClearance = 0x7fffffff;

	if (!SlipRaceCamera_AccumulateRecordRayClearance(record, trdBytes - recordOffset, componentBase, componentBaseBytes,
	                                                 query, dirX, dirY, dirZ, &minimumClearance)) {
		return false;
	}

	for (neighborLinkFieldOffset = 0x04u; neighborLinkFieldOffset <= 0x0cu; neighborLinkFieldOffset += 0x04u) {
		const uint16_t neighborRecordOffset = (uint16_t)((uint16_t)record[neighborLinkFieldOffset] |
		                                                 ((uint16_t)record[neighborLinkFieldOffset + 1u] << 8));

		if (neighborRecordOffset == 0) {
			continue;
		}
		if ((size_t)neighborRecordOffset > trdBytes || trdBytes - neighborRecordOffset < 0x1eu) {
			return false;
		}
		if (!SlipRaceCamera_AccumulateRecordRayClearance(
		        trdBase + neighborRecordOffset, trdBytes - neighborRecordOffset, componentBase, componentBaseBytes,
		        query, dirX, dirY, dirZ, &minimumClearance)) {
			return false;
		}
	}
	*clearance = minimumClearance;
	return true;
}

bool SlipRaceCamera_IntroZoom(SlipRaceCameraState *state, SlipObject *objectTable, size_t objectTableBytes,
                              uint16_t racerObject, uint16_t viewId, uint16_t frameStep, uint32_t flybyChaseEnabled,
                              uint32_t demoChaseEnabled, const SlipView3DMaths *maths, const uint8_t *trdBase,
                              size_t trdBytes, const uint8_t *componentBase, size_t componentBaseBytes,
                              const uint8_t *cellTable, size_t cellTableBytes, uint32_t trackDataBaseAddress,
                              SlipRaceCameraActivation activate, void *context, SlipRaceCameraIntroZoom *result) {
	SlipObjectPosition carPositionResult;
	SlipObjectMatrixCopy matrixCopy;
	SlipObjectMatrixInstall matrixInstall;
	SlipObjectSetPosition setPosition;
	SlipView3DMatrix cameraMatrix;
	int32_t *activeZoomDistance;
	int32_t zoomDistanceDecrement;
	int32_t appliedDistance;

	if (state == NULL || objectTable == NULL || maths == NULL || result == NULL) {
		return false;
	}
	memset(result, 0, sizeof(*result));

	if (!SlipObject_Position(objectTable, objectTableBytes, racerObject, &carPositionResult)) {
		return false;
	}
	result->carPosition = (SlipView3DVec32){(int32_t)carPositionResult.positionX, (int32_t)carPositionResult.positionY,
	                                        (int32_t)carPositionResult.positionZ};

	if (!SlipObject_MatrixCopy(objectTable, objectTableBytes, racerObject, &cameraMatrix, &matrixCopy)) {
		return false;
	}

	cameraMatrix.m[1] = (int16_t)(cameraMatrix.m[1] >> 1);

	SlipView3D_OrthonormalizeForwardBasis(&cameraMatrix);

	if (!SlipObject_MatrixInstall(objectTable, objectTableBytes, 0, &cameraMatrix, &matrixInstall)) {
		return false;
	}

	SlipView3D_ApplyPitchMatrix(maths, (int16_t)0xfc00, &cameraMatrix);
	result->pitchedOffsetMatrix = cameraMatrix;

	result->direction =
	    (SlipView3DVec32){-(int32_t)cameraMatrix.m[6], -(int32_t)cameraMatrix.m[7], -(int32_t)cameraMatrix.m[8]};

	if (!SlipRaceCamera_GroundClearance(trdBase, trdBytes, componentBase, componentBaseBytes, cellTable, cellTableBytes,
	                                    trackDataBaseAddress, result->carPosition, (int16_t)result->direction.x,
	                                    (int16_t)result->direction.y, (int16_t)result->direction.z,
	                                    &result->clearance)) {
		return false;
	}

	activeZoomDistance = viewId == 1 ? &state->viewOneZoomDistance : &state->viewTwoZoomDistance;

	zoomDistanceDecrement = (int32_t)(uint16_t)(((uint32_t)0xd000u * frameStep) >> 14);
	*activeZoomDistance = (int32_t)((uint32_t)*activeZoomDistance - (uint32_t)zoomDistanceDecrement);

	if (*activeZoomDistance < 0x1ccc) {
		uint16_t event;

		*activeZoomDistance = 0x1ccc;
		event = 0x3bu;
		if (flybyChaseEnabled != 0 || demoChaseEnabled != 0) {
			event = 0x3eu;
		}
		if (viewId != 1) {
			event = 0x40u;
			if (demoChaseEnabled != 0) {
				event = 0x43u;
			}
		}
		result->clamped = true;
		result->event = event;
		if (!SlipRaceCamera_Event(state, event, activate, context)) {
			return false;
		}
	}
	result->distanceAfter = *activeZoomDistance;

	appliedDistance = result->clearance;
	if (appliedDistance > *activeZoomDistance) {
		appliedDistance = *activeZoomDistance;
	}
	result->appliedDistance = appliedDistance;

	result->cameraPosition = (SlipView3DVec32){
	    (int32_t)((uint32_t)result->carPosition.x + (uint32_t)(((int64_t)appliedDistance * result->direction.x) >> 14)),
	    (int32_t)((uint32_t)result->carPosition.y + (uint32_t)(((int64_t)appliedDistance * result->direction.y) >> 14)),
	    (int32_t)((uint32_t)result->carPosition.z +
	              (uint32_t)(((int64_t)appliedDistance * result->direction.z) >> 14))};

	if (!SlipObject_SetPosition(objectTable, objectTableBytes, 0, (uint32_t)result->cameraPosition.x,
	                            (uint32_t)result->cameraPosition.y, (uint32_t)result->cameraPosition.z, &setPosition)) {
		return false;
	}
	return true;
}

bool SlipRaceCamera_ActivateChase(SlipRaceCameraState *state, const SlipObject *objects, size_t objectBytes,
                                  uint16_t racerObject, uint16_t view) {
	SlipObjectMatrixCopy copy;
	SlipView3DMatrix *const matrix = &state->chaseMatrix[view == 1u ? 0 : 1];
	SlipView3DMatrix source;
	if (!SlipObject_MatrixCopy(objects, objectBytes, racerObject, &source, &copy))
		return false;

	*matrix = source;
	return true;
}

bool SlipRaceCamera_Chase(SlipRaceCameraState *state, SlipObject *objects, size_t objectBytes, uint16_t racerObject,
                          uint16_t view, const SlipView3DMaths *maths, const uint8_t *trd, size_t trdBytes,
                          const uint8_t *components, size_t componentBytes, const uint8_t *cells, size_t cellBytes,
                          uint32_t trackDataAddress) {
	const uint16_t viewIndex = view == 1u ? 0u : 1u;
	const int32_t racerSpeed = SlipObject_Speed(objects, racerObject);
	SlipObjectPosition position;
	SlipObjectMatrixInstall install;
	SlipObjectSetPosition setPosition;
	SlipView3DMatrix cameraMatrix;
	SlipView3DVec32 direction, query;
	int32_t distance, speedDistance;
	uint32_t x, y, z;
	if (!SlipObject_Position(objects, objectBytes, racerObject, &position))
		return false;
	cameraMatrix = state->chaseMatrix[viewIndex];
	SlipView3D_ApplyPitchMatrix(maths, -0x400, &cameraMatrix);
	SlipView3D_OrthonormalizeForwardBasis(&cameraMatrix);
	if (!SlipObject_MatrixInstall(objects, objectBytes, 0, &cameraMatrix, &install))
		return false;
	direction =
	    (SlipView3DVec32){-(int32_t)cameraMatrix.m[6], -(int32_t)cameraMatrix.m[7], -(int32_t)cameraMatrix.m[8]};
	query = (SlipView3DVec32){(int32_t)position.positionX, (int32_t)position.positionY, (int32_t)position.positionZ};
	if (!SlipRaceCamera_GroundClearance(trd, trdBytes, components, componentBytes, cells, cellBytes, trackDataAddress,
	                                    query, (int16_t)direction.x, (int16_t)direction.y, (int16_t)direction.z,
	                                    &distance))
		return false;
	speedDistance = (int32_t)((uint32_t)(racerSpeed >> 4) + 0x5000u);
	if (distance > speedDistance)
		distance = speedDistance;
	distance >>= 1;
	/* Three signed IMUL/SHRD pairs followed by wrapping 32-bit ADDs. */
	x = position.positionX + (uint32_t)(((int64_t)distance * direction.x) >> 14);
	y = position.positionY + (uint32_t)(((int64_t)distance * direction.y) >> 14);
	z = position.positionZ + (uint32_t)(((int64_t)distance * direction.z) >> 14);
	if (!SlipObject_SetPosition(objects, objectBytes, 0, x, y, z, &setPosition))
		return false;
	state->chasePosition[viewIndex] = (SlipView3DVec32){(int32_t)x, (int32_t)y, (int32_t)z};
	return true;
}

bool SlipRaceCamera_UpdateChaseMatrix(SlipRaceCameraState *state, const SlipObject *objects, size_t objectBytes,
                                      uint16_t racerObject, uint16_t view, uint16_t frameStep,
                                      const SlipView3DMaths *maths) {
	SlipObjectMatrixCopy copy;
	SlipView3DMatrix racerMatrix;
	SlipView3DMatrix *const matrix = &state->chaseMatrix[view == 1u ? 0 : 1];

	const uint16_t rotationStep = (uint16_t)(((uint32_t)0x2000u * frameStep) >> 14);
	if (!SlipObject_MatrixCopy(objects, objectBytes, racerObject, &racerMatrix, &copy))
		return false;
	if (!SlipView3D_RotateForwardTowards(maths, matrix, racerMatrix.m[6], racerMatrix.m[7], racerMatrix.m[8],
	                                     rotationStep) ||
	    !SlipView3D_RotateRightTowards(maths, matrix, racerMatrix.m[0], racerMatrix.m[1], racerMatrix.m[2],
	                                   rotationStep))
		return false;
	SlipView3D_OrthonormalizeForwardBasis(matrix);
	return true;
}

bool SlipRaceCamera_Dropped(const SlipRaceCameraState *state, SlipObject *objects, size_t objectBytes,
                            uint16_t racerObject, uint16_t view) {
	const SlipView3DVec32 *const camera = &state->chasePosition[view == 1u ? 0 : 1];
	SlipObjectSetPosition setPosition;
	SlipObjectPosition position;
	SlipObjectMatrixInstall install;
	SlipView3DMatrix matrix;
	if (!SlipObject_SetPosition(objects, objectBytes, 0, (uint32_t)camera->x, (uint32_t)camera->y, (uint32_t)camera->z,
	                            &setPosition) ||
	    !SlipObject_Position(objects, objectBytes, racerObject, &position))
		return false;
	if (!SlipView3D_BuildMatrixFromVector32(&matrix, position.positionX - (uint32_t)camera->x,
	                                        position.positionY - (uint32_t)camera->y,
	                                        position.positionZ - (uint32_t)camera->z))
		return false;
	return SlipObject_MatrixInstall(objects, objectBytes, 0, &matrix, &install);
}

bool SlipRaceCamera_RearMonitorTransform(SlipObject *objects, size_t objectBytes, uint16_t racerObject,
                                         SlipObjectPosition *cameraPosition, SlipView3DMatrix *cameraMatrix) {
	SlipView3DMatrix matrix;
	SlipObjectPosition position;
	SlipObjectMatrixCopy copy;
	SlipObjectMatrixInstall install;
	SlipObjectSetPosition setPosition;
	if (!SlipObject_Position(objects, objectBytes, racerObject, &position) ||
	    !SlipObject_MatrixCopy(objects, objectBytes, racerObject, &matrix, &copy))
		return false;
	matrix.m[0] = (int16_t)(uint16_t)(0u - (uint16_t)matrix.m[0]);
	matrix.m[1] = (int16_t)(uint16_t)(0u - (uint16_t)matrix.m[1]);
	matrix.m[2] = (int16_t)(uint16_t)(0u - (uint16_t)matrix.m[2]);
	matrix.m[6] = (int16_t)(uint16_t)(0u - (uint16_t)matrix.m[6]);
	matrix.m[7] = (int16_t)(uint16_t)(0u - (uint16_t)matrix.m[7]);
	matrix.m[8] = (int16_t)(uint16_t)(0u - (uint16_t)matrix.m[8]);
	*cameraPosition = position;
	*cameraMatrix = matrix;
	if (!SlipObject_SetPosition(objects, objectBytes, 0, position.positionX, position.positionY, position.positionZ,
	                            &setPosition))
		return false;
	return SlipObject_MatrixInstall(objects, objectBytes, 0, &matrix, &install);
}

bool SlipRaceCamera_Rear(SlipObject *objects, size_t objectBytes, uint16_t racerObject, uint32_t viewIndex,
                         uint8_t *slots, size_t slotBytes, uint32_t slotPoolAddress, uint8_t *art, size_t artBytes,
                         uint32_t artDataAddress, const SlipView3DMaths *maths) {
	SlipView3DMatrix matrix;
	SlipObjectMatrixCopy copy;
	SlipObjectMatrixInstall install;
	SlipObjectSetPosition setPosition;

	SlipArticSlotPosition position = {viewIndex, 0x00042cabu + 5u * 0x14u, 0, false};
	if (!SlipObject_MatrixCopy(objects, objectBytes, racerObject, &matrix, &copy) ||
	    !SlipArticSlot_WorldPosition(0x6d61696eu, 0x68656164u, racerObject, objects, objectBytes, slots, slotBytes,
	                                 slotPoolAddress, art, artBytes, artDataAddress, maths, &position))
		return false;
	/* Six 16-bit NEG instructions reverse the right and forward rows, leaving up unchanged. */
	for (size_t matrixElementIndex = 0; matrixElementIndex < 9; ++matrixElementIndex) {
		if (matrixElementIndex >= 3 && matrixElementIndex < 6)
			continue;
		matrix.m[matrixElementIndex] = (int16_t)(uint16_t)(0u - (uint16_t)matrix.m[matrixElementIndex]);
	}
	if (!SlipObject_SetPosition(objects, objectBytes, 0, position.positionX, position.positionY, position.positionZ,
	                            &setPosition))
		return false;
	return SlipObject_MatrixInstall(objects, objectBytes, 0, &matrix, &install);
}

bool SlipRaceCamera_Cockpit(SlipObject *objectTable, size_t objectTableBytes, uint16_t racerObject, uint32_t viewIndex,
                            uint8_t *slotPool, size_t slotPoolBytes, uint32_t slotPoolAddress, uint8_t *artData,
                            size_t artDataBytes, uint32_t artDataAddress, const SlipView3DMaths *maths,
                            SlipArticSlotPosition *cameraPosition) {
	SlipObjectMatrixCopy matrixCopy;
	SlipObjectMatrixInstall matrixInstall;
	SlipObjectSetPosition setPosition;
	SlipView3DMatrix matrix;

	if (cameraPosition == NULL)
		return false;

	*cameraPosition = (SlipArticSlotPosition){viewIndex, 0x00042cabu + 3u * 0x14u, 0, false};
	if (!SlipObject_MatrixCopy(objectTable, objectTableBytes, racerObject, &matrix, &matrixCopy) ||
	    !SlipArticSlot_WorldPosition(0x6d61696eu, 0x68656164u, racerObject, objectTable, objectTableBytes, slotPool,
	                                 slotPoolBytes, slotPoolAddress, artData, artDataBytes, artDataAddress, maths,
	                                 cameraPosition)) {
		return false;
	}

	if (!SlipObject_SetPosition(objectTable, objectTableBytes, 0, cameraPosition->positionX, cameraPosition->positionY,
	                            cameraPosition->positionZ, &setPosition) ||
	    !SlipObject_MatrixInstall(objectTable, objectTableBytes, 0, &matrix, &matrixInstall)) {
		return false;
	}

	return true;
}
