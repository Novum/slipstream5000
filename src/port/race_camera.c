#include "race_camera.h"
#include "actor_tags.h"

#include "byte_order.h"
#include "fixed_point.h"
#include "game_errors.h"
#include "race.h"
#include "race_collision.h"
#include "resource.h"
#include "resource_host.h"
#include "runtime.h"
#include "track_format.h"
#include "track_world.h"

#include <string.h>

enum {
	SLIP_RACE_CAMERA_SIDE_VISIT_CAPACITY = 32,
	/* The original shake spans -3 through +4 pixels. */
	SLIP_RACE_CAMERA_SHAKE_SAMPLE_MASK = 7,
	SLIP_RACE_CAMERA_SHAKE_CENTER_BIAS = 3,
	SLIP_RACE_LAP_TIME_DURATION_MS = 4000,
	SLIP_RACE_LAP_NOTIFICATION_DURATION_MS = 2000,
	SLIP_RACE_FINISH_CAMERA_HORIZONTAL_TILT_SHIFT = 1,
	SLIP_RACE_TV_POSITIONS_DOS_ADDRESS = 0x42e1c,
	SLIP_RACE_CAMERA_MODE_TABLE_DOS_ADDRESS = 0x42cab,
	SLIP_RACE_CAMERA_MODE_DOS_STRIDE = 20,
	SLIP_RACE_TV_ZOOM_START_DISTANCE = 9760,
	SLIP_RACE_TV_ZOOM_DISTANCE_RANGE = 292800,
	SLIP_RACE_TV_ZOOM_SCALE_RANGE_Q16 = 3 * SLIP_DRAW3D_SCALE_ONE_Q16,
	SLIP_RACE_EXTERNAL_DISTANCE_MINIMUM = 8784,
	SLIP_RACE_EXTERNAL_DISTANCE_MAXIMUM = 48800,
	SLIP_RACE_EXTERNAL_DISTANCE_FRACTION_BITS = 13,
	SLIP_RACE_EXTERNAL_ROTATION_SLOW = 1024,
	SLIP_RACE_EXTERNAL_ROTATION_NORMAL = SLIP_ANGLE_QUARTER_TURN,
	SLIP_RACE_EXTERNAL_PITCH_RATE_SHIFT = 1,
	SLIP_RACE_EXTERNAL_PITCH_LIMIT_Q14 = 15360,
	SLIP_RACE_CAMERA_INTRO_INITIAL_DISTANCE = 57344,
	SLIP_RACE_CAMERA_DESTROYED_DISTANCE = 3072,
	SLIP_RACE_CAMERA_INTRO_MINIMUM_DISTANCE = 7372,
	SLIP_RACE_CAMERA_INTRO_ZOOM_STEP = 53248,
	SLIP_RACE_CAMERA_PITCH_OFFSET = -1024,
	SLIP_RACE_CAMERA_CHASE_BASE_DISTANCE = 20480,
	SLIP_RACE_CAMERA_CHASE_SPEED_DISTANCE_SHIFT = 4,
	SLIP_RACE_CAMERA_CHASE_ROTATION_STEP = 8192,
	SLIP_RACE_CAMERA_MODE_NAME_DURATION = 6000,
	SLIP_RACE_CAMERA_FINISH_DELAY = 4000,
	SLIP_RACE_CAMERA_RAY_MINIMUM_FACING_Q14 = 16,
	SLIP_RACE_CAMERA_PLANE_STANDOFF = 2440,
	SLIP_RACE_CAMERA_RAY_NUMERATOR_SHIFT = 30,
	SLIP_RACE_CAMERA_RAY_DIVISOR_SHIFT = 16,
	SLIP_RACE_CAMERA_SELECT_COCKPIT_ONE = 0x3b,
	SLIP_RACE_CAMERA_SELECT_CHASE_ONE = 0x3c,
	SLIP_RACE_CAMERA_SELECT_REAR_ONE = 0x3d,
	SLIP_RACE_CAMERA_SELECT_EXTERNAL_ONE = 0x3f,
	SLIP_RACE_CAMERA_SELECT_COCKPIT_TWO = 0x40,
	SLIP_RACE_CAMERA_SELECT_CHASE_TWO = 0x41,
	SLIP_RACE_CAMERA_SELECT_REAR_TWO = 0x42,
	SLIP_RACE_CAMERA_SELECT_TV_TWO = 0x43,
	SLIP_RACE_CAMERA_SELECT_EXTERNAL_TWO = 0x44,
	SLIP_RACE_CAMERA_KEY_ZOOM_IN = 0x4e,
	SLIP_RACE_CAMERA_KEY_ZOOM_OUT = 0x4a,
	SLIP_RACE_CAMERA_KEY_SLOW_ROTATION = 0x2a,
	SLIP_RACE_CAMERA_KEY_ROTATE_LEFT = 0x52,
	SLIP_RACE_CAMERA_KEY_ROTATE_RIGHT = 0x53,
	SLIP_RACE_CAMERA_KEY_PITCH_UP = 0x49,
	SLIP_RACE_CAMERA_KEY_PITCH_DOWN = 0x51,
	SLIP_RACE_TV_POSITIONS_DOS_END = SLIP_RACE_TV_POSITIONS_DOS_ADDRESS + SLIP_RACE_CAM_FILE_BYTES
};

/* Inclusive viewport coordinates in the original 320x200 display. */
enum {
	SLIP_RACE_VIEWPORT_CENTRE_X = 160,
	SLIP_RACE_VIEWPORT_LEFT = 4,
	SLIP_RACE_VIEWPORT_RIGHT = 315,
	SLIP_RACE_VIEWPORT_SPLIT_BOTTOM_CENTRE_Y = 144,
	SLIP_RACE_VIEWPORT_SPLIT_BOTTOM_TOP = 101,
	SLIP_RACE_VIEWPORT_SPLIT_BOTTOM_BOTTOM = 187,
	SLIP_RACE_VIEWPORT_SPLIT_TOP_CENTRE_Y = 43,
	SLIP_RACE_VIEWPORT_SPLIT_TOP_BOTTOM = 87,
	SLIP_RACE_VIEWPORT_CENTRE_Y = 87,
	SLIP_RACE_VIEWPORT_BOTTOM = 166,
	SLIP_RACE_VIEWPORT_INSET_TOP = 32,
	SLIP_RACE_VIEWPORT_INSET_CENTRE_SHIFT = 12,
	SLIP_RACE_VIEWPORT_TOP = 8,
};

static const char *const cameraFiles[SLIP_RACE_TRACK_COUNT] = {"CHICAGO.CAM", "HAWAII.CAM", "TOKYO.CAM",  "NORWAY.CAM",
                                                               "CAVE.CAM",    "CAN.CAM",    "AMAZON.CAM", "LONDON.CAM",
                                                               "EGYPT.CAM",   "NEWYORK.CAM"};

bool SlipRaceCamera_LoadPositions(SlipRaceCameraState *state, const char *const *archives, size_t archiveCount,
                                  uint16_t track) {
	(void)archives;
	(void)archiveCount;
	if (track == 0 || track > SLIP_RACE_TRACK_COUNT)
		return false;
	uint16_t cameraResource;
	if (!SlipResourceHost_Load(NULL, cameraFiles[track - 1], &cameraResource))
		SlipGame_ResourceFailure();
	const uint8_t *const cameraBytes = SlipResourceHost_Lock(NULL, cameraResource);
	SlipResourcePayload cameraPayload = SlipResourceHost_Payload(cameraResource);
	if (cameraPayload.size < SLIP_RACE_CAM_FILE_BYTES)
		return false;
	for (size_t cameraIndex = 0; cameraIndex < SLIP_RACE_TV_POSITION_COUNT; ++cameraIndex) {
		state->tvPositions[cameraIndex] =
		    (SlipView3DVec32){(int32_t)SlipBytes_ReadLE32(cameraBytes + cameraIndex * SLIP_RACE_CAM_POSITION_BYTES),
		                      (int32_t)SlipBytes_ReadLE32(cameraBytes + cameraIndex * SLIP_RACE_CAM_POSITION_BYTES +
		                                                  SLIP_RACE_CAM_Y_OFFSET),
		                      (int32_t)SlipBytes_ReadLE32(cameraBytes + cameraIndex * SLIP_RACE_CAM_POSITION_BYTES +
		                                                  SLIP_RACE_CAM_Z_OFFSET)};
	}
	SlipResourceHost_Unlock(NULL, cameraResource);
	SlipResourceHost_Release(NULL, cameraResource);
	return true;
}

bool SlipRaceCamera_SelectTv(SlipRaceCameraState *state, SlipView3DVec32 craft, const uint8_t *trd, size_t trdBytes,
                             const uint8_t *components, size_t componentBytes, const uint8_t *cells, size_t cellBytes,
                             uint32_t trackDataAddress, uint32_t *selectedCameraAddressOut, int32_t *distance) {
	uint32_t selectedCameraAddress = 0;
	int32_t nearestCameraDistance = INT32_MAX;
	SlipTrackWorldRecordSearch cameraRecordSearch;
	SlipTrackWorldSegmentCollision lineOfSightCollision;
	const SlipView3DVec32 *cameraPosition;

	for (size_t cameraIndex = 0; cameraIndex < SLIP_RACE_TV_POSITION_COUNT; ++cameraIndex) {
		cameraPosition = &state->tvPositions[cameraIndex];
		if (cameraPosition->x == -1)
			continue;
		const int32_t cameraDistance =
		    (int32_t)SlipView3D_VectorLength((int32_t)((uint32_t)craft.x - (uint32_t)cameraPosition->x),
		                                     (int32_t)((uint32_t)craft.y - (uint32_t)cameraPosition->y),
		                                     (int32_t)((uint32_t)craft.z - (uint32_t)cameraPosition->z));
		if (cameraDistance < nearestCameraDistance) {
			nearestCameraDistance = cameraDistance;
			selectedCameraAddress =
			    SLIP_RACE_TV_POSITIONS_DOS_ADDRESS + (uint32_t)cameraIndex * SLIP_RACE_CAM_POSITION_BYTES;
		}
	}
	if (selectedCameraAddress == 0 || SlipRaceCollision_segmentQuery == NULL)
		return false;
	cameraPosition =
	    &state
	         ->tvPositions[(selectedCameraAddress - SLIP_RACE_TV_POSITIONS_DOS_ADDRESS) / SLIP_RACE_CAM_POSITION_BYTES];

	if (!SlipTrackWorld_RecordSearch(trd, trdBytes, components, componentBytes, cells, cellBytes, trackDataAddress, 0,
	                                 cameraPosition->x, cameraPosition->y, cameraPosition->z, &cameraRecordSearch))
		return false;
	if (cameraRecordSearch.selectedRecordAddress != 0) {
		SlipRaceCollision_segmentQuery(trd, trdBytes, trackDataAddress, components, componentBytes, cells, cellBytes,
		                               craft, *cameraPosition, &lineOfSightCollision);
		if (lineOfSightCollision.transitionBlocked) {
			const uint32_t rejectedCameraAddress = selectedCameraAddress;
			selectedCameraAddress = 0;
			nearestCameraDistance = INT32_MAX;

			for (size_t cameraIndex = 0; cameraIndex < SLIP_RACE_TV_POSITION_COUNT; ++cameraIndex) {
				cameraPosition = &state->tvPositions[cameraIndex];
				const uint32_t cameraAddress =
				    SLIP_RACE_TV_POSITIONS_DOS_ADDRESS + (uint32_t)cameraIndex * SLIP_RACE_CAM_POSITION_BYTES;
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
			cameraPosition = &state->tvPositions[(selectedCameraAddress - SLIP_RACE_TV_POSITIONS_DOS_ADDRESS) /
			                                     SLIP_RACE_CAM_POSITION_BYTES];
			SlipRaceCollision_segmentQuery(trd, trdBytes, trackDataAddress, components, componentBytes, cells,
			                               cellBytes, craft, *cameraPosition, &lineOfSightCollision);
			if (lineOfSightCollision.transitionBlocked)
				selectedCameraAddress =
				    state->tvPreviousCamera != UINT32_MAX ? state->tvPreviousCamera : rejectedCameraAddress;
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
	state->finishPosition = racePosition & SLIP_RACE_POSITION_MASK;
	if (!SlipObject_Position(objects, objectBytes, racerObject, &craft))
		return false;
	for (size_t cameraIndex = 0; cameraIndex < SLIP_RACE_TV_POSITION_COUNT; ++cameraIndex) {
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
	state->tvSoundDistance = INT32_MAX;
	state->tvSoundCamera = 0;
}

uint32_t SlipRaceCamera_TvScale(uint32_t distance) {
	uint32_t zoomDistanceExcess = distance - SLIP_RACE_TV_ZOOM_START_DISTANCE;
	if ((int32_t)zoomDistanceExcess < 0)
		zoomDistanceExcess = 0;
	if ((int32_t)zoomDistanceExcess > SLIP_RACE_TV_ZOOM_DISTANCE_RANGE)
		zoomDistanceExcess = SLIP_RACE_TV_ZOOM_DISTANCE_RANGE;
	return (uint32_t)(((uint64_t)zoomDistanceExcess * SLIP_RACE_TV_ZOOM_SCALE_RANGE_Q16) /
	                  SLIP_RACE_TV_ZOOM_DISTANCE_RANGE) +
	       SLIP_DRAW3D_SCALE_ONE_Q16;
}

bool SlipRaceCamera_TvTransform(const SlipRaceCameraState *state, SlipObject *objects, size_t objectBytes,
                                uint16_t racerObject, uint32_t selectedCameraAddress, uint32_t *distance) {
	SlipObjectSetPosition setPosition;
	SlipObjectPosition craft;
	SlipObjectMatrixInstall install;
	SlipView3DNormalizeVector3D normalized;
	SlipView3DMatrix matrix;
	const SlipView3DVec32 *position;
	if (selectedCameraAddress < SLIP_RACE_TV_POSITIONS_DOS_ADDRESS ||
	    selectedCameraAddress >= SLIP_RACE_TV_POSITIONS_DOS_END ||
	    (selectedCameraAddress - SLIP_RACE_TV_POSITIONS_DOS_ADDRESS) % SLIP_RACE_CAM_POSITION_BYTES != 0)
		return false;
	position =
	    &state
	         ->tvPositions[(selectedCameraAddress - SLIP_RACE_TV_POSITIONS_DOS_ADDRESS) / SLIP_RACE_CAM_POSITION_BYTES];
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
		state->externalDistance =
		    clearance < SLIP_RACE_EXTERNAL_DISTANCE_MINIMUM ? SLIP_RACE_EXTERNAL_DISTANCE_MINIMUM : clearance;
	}
	offset = SlipView3D_TransformPositionByColumns(matrix, (SlipView3DVec32){0, 0, (int32_t)(0u - (uint32_t)distance)});
	return SlipObject_SetPosition(objects, objectBytes, 0, craft.positionX + (uint32_t)offset.x,
	                              craft.positionY + (uint32_t)offset.y, craft.positionZ + (uint32_t)offset.z,
	                              &setPosition);
}

void SlipRaceCamera_ExternalControls(SlipRaceCameraState *state, uint16_t frameStep,
                                     const bool held[SLIP_INPUT_CODE_COUNT], const SlipView3DMaths *maths) {
	const int32_t distanceIncrement =
	    (int32_t)(((int64_t)state->externalDistance * (int16_t)frameStep) >> SLIP_RACE_EXTERNAL_DISTANCE_FRACTION_BITS);
	int32_t distance = state->externalDistance;
	SlipView3DMatrix *const matrix = &state->externalMatrix;
	int16_t rotationAngle;
	if (held[SLIP_RACE_CAMERA_KEY_ZOOM_IN]) {
		distance = (int32_t)((uint32_t)distance - (uint32_t)distanceIncrement);
		if (distance < SLIP_RACE_EXTERNAL_DISTANCE_MINIMUM)
			distance = SLIP_RACE_EXTERNAL_DISTANCE_MINIMUM;
	}
	if (held[SLIP_RACE_CAMERA_KEY_ZOOM_OUT]) {
		distance = (int32_t)((uint32_t)distance + (uint32_t)distanceIncrement);
		if ((uint32_t)distance > SLIP_RACE_EXTERNAL_DISTANCE_MAXIMUM)
			distance = SLIP_RACE_EXTERNAL_DISTANCE_MAXIMUM;
	}
	state->externalDistance = distance;
	rotationAngle =
	    (int16_t)(((int32_t)(held[SLIP_RACE_CAMERA_KEY_SLOW_ROTATION] ? SLIP_RACE_EXTERNAL_ROTATION_SLOW
	                                                                  : SLIP_RACE_EXTERNAL_ROTATION_NORMAL) *
	               (int16_t)frameStep) >>
	              SLIP_Q14_FRACTION_BITS);
	if (held[SLIP_RACE_CAMERA_KEY_ROTATE_LEFT])
		SlipView3D_ApplyColumn0Column2Rotation(maths, rotationAngle, matrix);
	if (held[SLIP_RACE_CAMERA_KEY_ROTATE_RIGHT])
		SlipView3D_ApplyColumn0Column2Rotation(maths, (int16_t)((0u - (uint16_t)rotationAngle) & UINT16_MAX), matrix);
	if (held[SLIP_RACE_CAMERA_KEY_PITCH_UP] && matrix->m[7] > -SLIP_RACE_EXTERNAL_PITCH_LIMIT_Q14)
		SlipView3D_ApplyPitchMatrix(
		    maths,
		    (int16_t)((int16_t)((0u - (uint16_t)rotationAngle) & UINT16_MAX) >> SLIP_RACE_EXTERNAL_PITCH_RATE_SHIFT),
		    matrix);
	if (held[SLIP_RACE_CAMERA_KEY_PITCH_DOWN] && matrix->m[7] < SLIP_RACE_EXTERNAL_PITCH_LIMIT_Q14)
		SlipView3D_ApplyPitchMatrix(maths, (int16_t)(rotationAngle >> SLIP_RACE_EXTERNAL_PITCH_RATE_SHIFT), matrix);
	SlipView3D_OrthonormalizeForwardBasis(matrix);
}

void SlipRaceCamera_ViewportShake(uint16_t shakeTimer, int32_t *centerX, int32_t *centerY) {
	if (shakeTimer == 0 || centerX == NULL || centerY == NULL) {
		return;
	}
	*centerX += (int32_t)(SlipRandom_Next() & SLIP_RACE_CAMERA_SHAKE_SAMPLE_MASK) - SLIP_RACE_CAMERA_SHAKE_CENTER_BIAS;
	*centerY += (int32_t)(SlipRandom_Next() & SLIP_RACE_CAMERA_SHAKE_SAMPLE_MASK) - SLIP_RACE_CAMERA_SHAKE_CENTER_BIAS;
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
	if (gameMode == SLIP_RACE_GAME_SPLIT_SCREEN) {
		centerX = SLIP_RACE_VIEWPORT_CENTRE_X;
		if (viewId == 2u) {
			centerY = SLIP_RACE_VIEWPORT_SPLIT_BOTTOM_CENTRE_Y;
			SlipRaceCamera_ViewportShake(shakeView2, &centerX, &centerY);
			minX = SLIP_RACE_VIEWPORT_LEFT;
			minY = SLIP_RACE_VIEWPORT_SPLIT_BOTTOM_TOP;
			maxX = SLIP_RACE_VIEWPORT_RIGHT;
			maxY = SLIP_RACE_VIEWPORT_SPLIT_BOTTOM_BOTTOM;
		} else {
			centerY = SLIP_RACE_VIEWPORT_SPLIT_TOP_CENTRE_Y;
			SlipRaceCamera_ViewportShake(shakeView1, &centerX, &centerY);
			minX = SLIP_RACE_VIEWPORT_LEFT;
			minY = 0;
			maxX = SLIP_RACE_VIEWPORT_RIGHT;
			maxY = SLIP_RACE_VIEWPORT_SPLIT_TOP_BOTTOM;
		}
	} else {
		centerX = SLIP_RACE_VIEWPORT_CENTRE_X;
		centerY = SLIP_RACE_VIEWPORT_CENTRE_Y;
		SlipRaceCamera_ViewportShake(shakeView1, &centerX, &centerY);
		minX = SLIP_RACE_VIEWPORT_LEFT;
		maxX = SLIP_RACE_VIEWPORT_RIGHT;
		maxY = SLIP_RACE_VIEWPORT_BOTTOM;
		if (splitView != 0) {
			minY = SLIP_RACE_VIEWPORT_INSET_TOP;
			centerY += SLIP_RACE_VIEWPORT_INSET_CENTRE_SHIFT;
		} else {
			minY = SLIP_RACE_VIEWPORT_TOP;
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

static const RaceCameraModeEntry kRaceCameraModeTable[SLIP_RACE_CAMERA_MODE_COUNT] = {
    [SLIP_RACE_CAMERA_MODE_INTRO] = {"", 0x0000u, 0x0000u, false},
    [SLIP_RACE_CAMERA_MODE_DESTROYED] = {"", 0x0000u, 0x0000u, false},
    [SLIP_RACE_CAMERA_MODE_FINISH] = {"", 0x0000u, 0x0000u, false},
    [SLIP_RACE_CAMERA_MODE_COCKPIT] = {"Cockpit View", SLIP_RACE_CAMERA_SELECT_COCKPIT_ONE,
                                       SLIP_RACE_CAMERA_SELECT_COCKPIT_TWO, false},
    [SLIP_RACE_CAMERA_MODE_CHASE] = {"Chase Camera", SLIP_RACE_CAMERA_SELECT_CHASE_ONE,
                                     SLIP_RACE_CAMERA_SELECT_CHASE_TWO, true},
    [SLIP_RACE_CAMERA_MODE_REAR] = {"Rear View", SLIP_RACE_CAMERA_SELECT_REAR_ONE, SLIP_RACE_CAMERA_SELECT_REAR_TWO,
                                    false},
    [SLIP_RACE_CAMERA_MODE_TV] = {"TV Camera", SLIP_RACE_CAMERA_SELECT_TV_ONE, SLIP_RACE_CAMERA_SELECT_TV_TWO, true},
    [SLIP_RACE_CAMERA_MODE_EXTERNAL] = {"External Camera", SLIP_RACE_CAMERA_SELECT_EXTERNAL_ONE,
                                        SLIP_RACE_CAMERA_SELECT_EXTERNAL_TWO, false},
    [SLIP_RACE_CAMERA_MODE_DROPPED] = {"Camera Dropped", SLIP_RACE_CAMERA_SELECT_CHASE_ONE,
                                       SLIP_RACE_CAMERA_SELECT_CHASE_TWO, false},
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

	state->viewOneMode = SLIP_RACE_CAMERA_MODE_INTRO;
	state->viewTwoMode = SLIP_RACE_CAMERA_MODE_INTRO;
	state->lapNotificationTimer[0] = 0;
	state->lapNotificationTimer[1] = 0;
	state->lapTimeTimer[0] = 0;
	state->lapTimeTimer[1] = 0;
	state->shake[0] = 0;
	state->shake[1] = 0;

	state->viewOneZoomDistance = SLIP_RACE_CAMERA_INTRO_INITIAL_DISTANCE;
	state->viewTwoZoomDistance = SLIP_RACE_CAMERA_INTRO_INITIAL_DISTANCE;

	SlipRaceCamera_projectileObject = 0;
	SlipObject_SetServer(SLIP_OBJECT_RELEASE_SERVER_ID, SlipRaceCamera_ProjectileDeleted);

	state->viewOneModeNameTimer = SLIP_RACE_CAMERA_MODE_NAME_DURATION;
	state->viewTwoModeNameTimer = SLIP_RACE_CAMERA_MODE_NAME_DURATION;
}

void SlipRaceCamera_DestroyRacer(SlipRaceCameraState *state, uint16_t racerObject, uint16_t playerOneObject,
                                 uint16_t playerTwoObject, uint16_t thirdObject) {
	if (racerObject == thirdObject) {
		if (SlipRace_racerCount == 2u) {
			SlipRace_playerTwoFinished = 1;
			SlipRace_playerTwoFinishDelay = SLIP_RACE_CAMERA_FINISH_DELAY;
		}
		return;
	}
	if (racerObject == playerOneObject) {
		state->viewOneZoomDistance = SLIP_RACE_CAMERA_DESTROYED_DISTANCE;
		state->viewOneMode = SLIP_RACE_CAMERA_MODE_DESTROYED;
		state->lapNotificationTimer[0] = 0;
		state->viewOneModeNameTimer = 0;
		if (SlipRace_racerCount == 2u || SlipRace_gameMode == SLIP_RACE_GAME_SINGLE_PLAYER) {
			SlipRace_playerOneFinished = 1;
			SlipRace_playerOneFinishDelay = SLIP_RACE_CAMERA_FINISH_DELAY;
		}
		return;
	}
	if (racerObject != playerTwoObject)
		return;

	state->viewTwoZoomDistance = SLIP_RACE_CAMERA_DESTROYED_DISTANCE;
	state->viewTwoMode = SLIP_RACE_CAMERA_MODE_DESTROYED;
	state->lapNotificationTimer[1] = 0;
	state->viewTwoModeNameTimer = 0;
	if (SlipRace_gameMode == SLIP_RACE_GAME_SINGLE_PLAYER) {
		SlipRace_playerTwoFinished = 1;
		SlipRace_playerTwoFinishDelay = SLIP_RACE_CAMERA_FINISH_DELAY;
	}
}

void SlipRaceCamera_FinishRacer(SlipRaceCameraState *state, uint16_t racerObject, uint16_t playerOneObject,
                                uint16_t playerTwoObject) {
	if (racerObject == playerOneObject) {
		state->viewOneMode = SLIP_RACE_CAMERA_MODE_FINISH;
		state->lapNotificationTimer[0] = 0;
		state->viewOneModeNameTimer = 0;
	} else if (racerObject == playerTwoObject) {
		state->viewTwoMode = SLIP_RACE_CAMERA_MODE_FINISH;
		state->lapNotificationTimer[1] = 0;
		state->viewTwoModeNameTimer = 0;
	}
}

void SlipRaceCamera_StoreLapTime(SlipRaceCameraState *state, uint32_t playerId, uint32_t lapTime) {
	uint32_t playerIndex;

	playerIndex = playerId == 1u ? 0u : 1u;
	state->lapTimeTimer[playerIndex] = SLIP_RACE_LAP_TIME_DURATION_MS;
	state->lapTime[playerIndex] = lapTime;
}

void SlipRaceCamera_StoreLapNumber(SlipRaceCameraState *state, uint32_t playerId, uint16_t lapNumber) {
	uint32_t playerIndex;

	playerIndex = playerId - 1u;
	state->lapNotificationTimer[playerIndex] = SLIP_RACE_LAP_NOTIFICATION_DURATION_MS;
	state->lapNotificationValue[playerIndex] = lapNumber;
}

bool SlipRaceCamera_Event(SlipRaceCameraState *state, uint16_t selectionEvent, SlipRaceCameraActivation activate,
                          void *context) {
	uint16_t modeIndex;

	if (state == NULL) {
		return false;
	}
	for (modeIndex = 0; modeIndex < SLIP_RACE_CAMERA_MODE_COUNT; ++modeIndex) {
		const RaceCameraModeEntry *const entry = &kRaceCameraModeTable[modeIndex];

		if (modeIndex != state->viewOneMode && selectionEvent == entry->viewOneSelectionEvent) {
			state->viewOneMode = modeIndex;
			state->viewOneModeNameTimer = SLIP_RACE_CAMERA_MODE_NAME_DURATION;

			if (entry->hasActivationCallback && (activate == NULL || !activate(context, modeIndex, 1u)))
				return false;
			return true;
		}

		if (modeIndex != state->viewTwoMode && selectionEvent == entry->viewTwoSelectionEvent) {
			state->viewTwoMode = modeIndex;
			state->viewTwoModeNameTimer = SLIP_RACE_CAMERA_MODE_NAME_DURATION;

			if (entry->hasActivationCallback && (activate == NULL || !activate(context, modeIndex, 2u)))
				return false;
			return true;
		}
	}

	return true;
}

bool SlipRaceCamera_PollKeys(SlipRaceCameraState *state, uint32_t gameMode, bool pressed[SLIP_INPUT_CODE_COUNT],
                             SlipRaceCameraActivation activate, void *context) {
	for (uint16_t mode = 0; mode < SLIP_RACE_CAMERA_MODE_COUNT; ++mode) {
		const RaceCameraModeEntry *const entry = &kRaceCameraModeTable[mode];
		if (mode != state->viewOneMode && state->viewOneMode != SLIP_RACE_CAMERA_MODE_DESTROYED &&
		    state->viewOneMode != SLIP_RACE_CAMERA_MODE_FINISH && pressed[entry->viewOneSelectionEvent]) {
			pressed[entry->viewOneSelectionEvent] = false;
			state->viewOneMode = mode;
			state->viewOneModeNameTimer = SLIP_RACE_CAMERA_MODE_NAME_DURATION;
			if (entry->hasActivationCallback && (activate == NULL || !activate(context, mode, 1u)))
				return false;
		}
		if (gameMode == SLIP_RACE_GAME_SPLIT_SCREEN && mode != state->viewTwoMode &&
		    state->viewTwoMode != SLIP_RACE_CAMERA_MODE_DESTROYED &&
		    state->viewTwoMode != SLIP_RACE_CAMERA_MODE_FINISH && pressed[entry->viewTwoSelectionEvent]) {
			pressed[entry->viewTwoSelectionEvent] = false;
			state->viewTwoMode = mode;
			state->viewTwoModeNameTimer = SLIP_RACE_CAMERA_MODE_NAME_DURATION;
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

	if (record == NULL || componentBase == NULL || minimumClearance == NULL ||
	    recordBytesRemaining < SLIP_TRD_SECTION_ORIGIN_END) {
		return false;
	}

	queryRelativeToComponent.x = query.x - SlipBytes_ReadLEI32(record + SLIP_TRD_SECTION_ORIGIN_X_OFFSET);
	queryRelativeToComponent.y = query.y - SlipBytes_ReadLEI32(record + SLIP_TRD_SECTION_ORIGIN_Y_OFFSET);
	queryRelativeToComponent.z = query.z - SlipBytes_ReadLEI32(record + SLIP_TRD_SECTION_ORIGIN_Z_OFFSET);

	componentOffset = SlipBytes_ReadLE16(record + SLIP_TRD_SECTION_COMPONENT_OFFSET);
	if (componentOffset == 0 || componentOffset > componentBaseBytes ||
	    componentBaseBytes - componentOffset < SLIP_TRC_COMPONENT_PRIMITIVE_LIST_OFFSET + sizeof(uint16_t)) {
		return false;
	}
	component = componentBase + componentOffset;

	primitiveListOffset = SlipBytes_ReadLE16(component + SLIP_TRC_COMPONENT_PRIMITIVE_LIST_OFFSET);
	if (primitiveListOffset == 0) {
		return true;
	}
	if ((size_t)primitiveListOffset > componentBaseBytes ||
	    componentBaseBytes - primitiveListOffset < SLIP_TRC_TABLE_COUNT_BYTES) {
		return false;
	}
	primitiveCount = (uint16_t)((uint16_t)componentBase[primitiveListOffset] |
	                            ((uint16_t)componentBase[(size_t)primitiveListOffset + 1u] << 8));
	primitiveOffset = (size_t)primitiveListOffset + SLIP_TRC_TABLE_COUNT_BYTES;

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
		SlipTrackWorldSideTestVisit sideVisits[SLIP_RACE_CAMERA_SIDE_VISIT_CAPACITY];
		SlipTrackWorldSideTest sideTest;

		if (primitiveOffset > componentBaseBytes ||
		    componentBaseBytes - primitiveOffset < SLIP_TRC_PRIMITIVE_FIRST_INDEX_END) {
			return false;
		}
		primitive = componentBase + primitiveOffset;
		descriptor = (uint16_t)((uint16_t)primitive[0] | ((uint16_t)primitive[1] << 8));

		if (descriptor & SLIP_TRC_PRIMITIVE_TEXTURED) {
			primitiveByteCount =
			    (uint32_t)(descriptor & SLIP_TRC_PRIMITIVE_VERTEX_COUNT_MASK) * SLIP_TRC_TEXTURED_VERTEX_BYTES +
			    SLIP_TRC_PRIMITIVE_HEADER_BYTES;
		} else {
			primitiveByteCount = (uint32_t)descriptor * SLIP_TRC_VERTEX_INDEX_BYTES + SLIP_TRC_PRIMITIVE_HEADER_BYTES;
		}

		if ((primitive[SLIP_TRC_PRIMITIVE_FLAGS_OFFSET] & SLIP_TRC_PRIMITIVE_CAMERA_RAY_SKIP_MASK) == 0) {
			const int32_t normalDirectionDot =
			    (int32_t)SlipBytes_ReadLEI16(primitive + SLIP_TRC_PRIMITIVE_NORMAL_X_OFFSET) * dirX +
			    (int32_t)SlipBytes_ReadLEI16(primitive + SLIP_TRC_PRIMITIVE_NORMAL_Y_OFFSET) * dirY +
			    (int32_t)SlipBytes_ReadLEI16(primitive + SLIP_TRC_PRIMITIVE_NORMAL_Z_OFFSET) * dirZ;

			if (normalDirectionDot >= 0) {
				primitiveOffset += primitiveByteCount;
				continue;
			}
			facingNormalDot = -(int16_t)((normalDirectionDot >> SLIP_Q14_FRACTION_BITS) +
			                             ((normalDirectionDot >> (SLIP_Q14_FRACTION_BITS - 1)) & 1));

			if (facingNormalDot < SLIP_RACE_CAMERA_RAY_MINIMUM_FACING_Q14) {
				primitiveOffset += primitiveByteCount;
				continue;
			}
			rayPlaneDivisor = (int16_t)facingNormalDot;

			if (!SlipTrackWorld_PointLookup(component, componentBase, componentBaseBytes,
			                                SlipBytes_ReadLE16(primitive + SLIP_TRC_PRIMITIVE_INDEX_STREAM_OFFSET), 0,
			                                0, 0, &pointLookup)) {
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

				const int64_t planeSum =
				    (int64_t)planePointDeltaX * SlipBytes_ReadLEI16(primitive + SLIP_TRC_PRIMITIVE_NORMAL_X_OFFSET) +
				    (int64_t)planePointDeltaY * SlipBytes_ReadLEI16(primitive + SLIP_TRC_PRIMITIVE_NORMAL_Y_OFFSET) +
				    (int64_t)planePointDeltaZ * SlipBytes_ReadLEI16(primitive + SLIP_TRC_PRIMITIVE_NORMAL_Z_OFFSET);

				planeDistance =
				    (int32_t)((planeSum >> SLIP_Q14_FRACTION_BITS) + ((planeSum >> (SLIP_Q14_FRACTION_BITS - 1)) & 1));
			}

			if (planeDistance < 0) {
				primitiveOffset += primitiveByteCount;
				continue;
			}

			nearPlane = planeDistance - SLIP_RACE_CAMERA_PLANE_STANDOFF < 0;
			if (!nearPlane) {
				const int32_t standoffDistance = planeDistance - SLIP_RACE_CAMERA_PLANE_STANDOFF;
				uint64_t standoffDividend;
				uint32_t standoffDivisor;

				if (standoffDistance >= *minimumClearance) {
					primitiveOffset += primitiveByteCount;
					continue;
				}

				standoffDividend = (uint64_t)(uint32_t)standoffDistance << SLIP_RACE_CAMERA_RAY_NUMERATOR_SHIFT;
				standoffDivisor = (uint32_t)(uint16_t)rayPlaneDivisor << SLIP_RACE_CAMERA_RAY_DIVISOR_SHIFT;
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
				clearanceDistance = (uint32_t)(planeDistance - SLIP_RACE_CAMERA_PLANE_STANDOFF);
			}

			{
				const uint64_t planeDistanceDividend = (uint64_t)(uint32_t)planeDistance
				                                       << SLIP_RACE_CAMERA_RAY_NUMERATOR_SHIFT;
				const uint32_t planeDistanceDivisor = (uint32_t)(uint16_t)rayPlaneDivisor
				                                      << SLIP_RACE_CAMERA_RAY_DIVISOR_SHIFT;

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
			    queryRelativeToComponent.x +
			    (int32_t)(((int64_t)dirX * (int32_t)planeIntersectionDistance) >> SLIP_Q14_FRACTION_BITS);
			planeIntersection.y =
			    queryRelativeToComponent.y +
			    (int32_t)(((int64_t)dirY * (int32_t)planeIntersectionDistance) >> SLIP_Q14_FRACTION_BITS);
			planeIntersection.z =
			    queryRelativeToComponent.z +
			    (int32_t)(((int64_t)dirZ * (int32_t)planeIntersectionDistance) >> SLIP_Q14_FRACTION_BITS);

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
				const uint64_t nearPlaneDividend = (uint64_t)(uint32_t)(-(int32_t)clearanceDistance)
				                                   << SLIP_RACE_CAMERA_RAY_NUMERATOR_SHIFT;
				const uint32_t nearPlaneDivisor = (uint32_t)(uint16_t)rayPlaneDivisor
				                                  << SLIP_RACE_CAMERA_RAY_DIVISOR_SHIFT;

				if (nearPlaneDivisor == 0 || (uint32_t)(nearPlaneDividend >> 32) >= nearPlaneDivisor) {
					primitiveOffset += primitiveByteCount;
					continue;
				}
				nearPlaneCorrectionDistance = (uint32_t)(nearPlaneDividend / nearPlaneDivisor);
				if (-(int32_t)nearPlaneCorrectionDistance < 0) {
					*minimumClearance = SLIP_RACE_EXTERNAL_DISTANCE_MINIMUM;
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
		*clearance = SLIP_RACE_EXTERNAL_DISTANCE_MINIMUM;
		return true;
	}
	if (recordAddress < trackDataBaseAddress) {
		return false;
	}
	recordOffset = recordAddress - trackDataBaseAddress;
	if (recordOffset > trdBytes || trdBytes - recordOffset < SLIP_TRD_SECTION_ORIGIN_END) {
		return false;
	}
	record = trdBase + recordOffset;

	minimumClearance = INT32_MAX;

	if (!SlipRaceCamera_AccumulateRecordRayClearance(record, trdBytes - recordOffset, componentBase, componentBaseBytes,
	                                                 query, dirX, dirY, dirZ, &minimumClearance)) {
		return false;
	}

	for (neighborLinkFieldOffset = SLIP_TRD_SECTION_FIRST_EXIT_OFFSET;
	     neighborLinkFieldOffset <= SLIP_TRD_SECTION_THIRD_EXIT_OFFSET;
	     neighborLinkFieldOffset += SLIP_TRD_SECTION_EXIT_BYTES) {
		const uint16_t neighborRecordOffset = (uint16_t)((uint16_t)record[neighborLinkFieldOffset] |
		                                                 ((uint16_t)record[neighborLinkFieldOffset + 1u] << 8));

		if (neighborRecordOffset == 0) {
			continue;
		}
		if ((size_t)neighborRecordOffset > trdBytes || trdBytes - neighborRecordOffset < SLIP_TRD_SECTION_ORIGIN_END) {
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

	cameraMatrix.m[1] = (int16_t)(cameraMatrix.m[1] >> SLIP_RACE_FINISH_CAMERA_HORIZONTAL_TILT_SHIFT);

	SlipView3D_OrthonormalizeForwardBasis(&cameraMatrix);

	if (!SlipObject_MatrixInstall(objectTable, objectTableBytes, 0, &cameraMatrix, &matrixInstall)) {
		return false;
	}

	SlipView3D_ApplyPitchMatrix(maths, SLIP_RACE_CAMERA_PITCH_OFFSET, &cameraMatrix);
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

	zoomDistanceDecrement =
	    (int32_t)(uint16_t)(((uint32_t)SLIP_RACE_CAMERA_INTRO_ZOOM_STEP * frameStep) >> SLIP_Q14_FRACTION_BITS);
	*activeZoomDistance = (int32_t)((uint32_t)*activeZoomDistance - (uint32_t)zoomDistanceDecrement);

	if (*activeZoomDistance < SLIP_RACE_CAMERA_INTRO_MINIMUM_DISTANCE) {
		uint16_t event;

		*activeZoomDistance = SLIP_RACE_CAMERA_INTRO_MINIMUM_DISTANCE;
		event = SLIP_RACE_CAMERA_SELECT_COCKPIT_ONE;
		if (flybyChaseEnabled != 0 || demoChaseEnabled != 0) {
			event = SLIP_RACE_CAMERA_SELECT_TV_ONE;
		}
		if (viewId != 1) {
			event = SLIP_RACE_CAMERA_SELECT_COCKPIT_TWO;
			if (demoChaseEnabled != 0) {
				event = SLIP_RACE_CAMERA_SELECT_TV_TWO;
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
	    (int32_t)((uint32_t)result->carPosition.x +
	              (uint32_t)(((int64_t)appliedDistance * result->direction.x) >> SLIP_Q14_FRACTION_BITS)),
	    (int32_t)((uint32_t)result->carPosition.y +
	              (uint32_t)(((int64_t)appliedDistance * result->direction.y) >> SLIP_Q14_FRACTION_BITS)),
	    (int32_t)((uint32_t)result->carPosition.z +
	              (uint32_t)(((int64_t)appliedDistance * result->direction.z) >> SLIP_Q14_FRACTION_BITS))};

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
	SlipView3D_ApplyPitchMatrix(maths, SLIP_RACE_CAMERA_PITCH_OFFSET, &cameraMatrix);
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
	speedDistance = (int32_t)((uint32_t)(racerSpeed >> SLIP_RACE_CAMERA_CHASE_SPEED_DISTANCE_SHIFT) +
	                          SLIP_RACE_CAMERA_CHASE_BASE_DISTANCE);
	if (distance > speedDistance)
		distance = speedDistance;
	distance >>= 1;
	/* Three signed IMUL/SHRD pairs followed by wrapping 32-bit ADDs. */
	x = position.positionX + (uint32_t)(((int64_t)distance * direction.x) >> SLIP_Q14_FRACTION_BITS);
	y = position.positionY + (uint32_t)(((int64_t)distance * direction.y) >> SLIP_Q14_FRACTION_BITS);
	z = position.positionZ + (uint32_t)(((int64_t)distance * direction.z) >> SLIP_Q14_FRACTION_BITS);
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

	const uint16_t rotationStep =
	    (uint16_t)((SLIP_RACE_CAMERA_CHASE_ROTATION_STEP * frameStep) >> SLIP_Q14_FRACTION_BITS);
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

	SlipArticSlotPosition position = {viewIndex,
	                                  SLIP_RACE_CAMERA_MODE_TABLE_DOS_ADDRESS +
	                                      SLIP_RACE_CAMERA_MODE_REAR * SLIP_RACE_CAMERA_MODE_DOS_STRIDE,
	                                  0, false};
	if (!SlipObject_MatrixCopy(objects, objectBytes, racerObject, &matrix, &copy) ||
	    !SlipArticSlot_WorldPosition(SLIP_ACTOR_PART_MAIN, SLIP_ACTOR_POINT_HEAD, racerObject, objects, objectBytes,
	                                 slots, slotBytes, slotPoolAddress, art, artBytes, artDataAddress, maths,
	                                 &position))
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

	*cameraPosition = (SlipArticSlotPosition){viewIndex,
	                                          SLIP_RACE_CAMERA_MODE_TABLE_DOS_ADDRESS +
	                                              SLIP_RACE_CAMERA_MODE_COCKPIT * SLIP_RACE_CAMERA_MODE_DOS_STRIDE,
	                                          0, false};
	if (!SlipObject_MatrixCopy(objectTable, objectTableBytes, racerObject, &matrix, &matrixCopy) ||
	    !SlipArticSlot_WorldPosition(SLIP_ACTOR_PART_MAIN, SLIP_ACTOR_POINT_HEAD, racerObject, objectTable,
	                                 objectTableBytes, slotPool, slotPoolBytes, slotPoolAddress, artData, artDataBytes,
	                                 artDataAddress, maths, cameraPosition)) {
		return false;
	}

	if (!SlipObject_SetPosition(objectTable, objectTableBytes, 0, cameraPosition->positionX, cameraPosition->positionY,
	                            cameraPosition->positionZ, &setPosition) ||
	    !SlipObject_MatrixInstall(objectTable, objectTableBytes, 0, &matrix, &matrixInstall)) {
		return false;
	}

	return true;
}
