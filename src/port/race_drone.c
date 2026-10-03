#include "race_drone.h"

#include "actor_host.h"
#include "actor_resources.h"
#include "byte_order.h"
#include "draw3d.h"
#include "fixed_point.h"
#include "frame_timer.h"
#include "race.h"
#include "race_bonus.h"
#include "race_collision.h"
#include "race_effects.h"
#include "resource_host.h"
#include "track_format.h"
#include "track_view_render.h"

#include <string.h>

enum {
	SLIP_RACE_DRONE_INITIAL_SPAWN_DELAY_MS = 1000,
	SLIP_RACE_DRONE_SPAWN_INTERVAL_MS = 10000,
	SLIP_RACE_DRONE_MAXIMUM_ACTIVE_COUNT = 6,
	SLIP_RACE_DRONE_ALTERNATING_OBJECT_FLAG = 8u,
	SLIP_RACE_DRONE_DESTRUCTION_DEBRIS_COUNT = 5,
	SLIP_RACE_DRONE_SPAWN_WAYPOINTS_AHEAD = 10,
	SLIP_RACE_DRONE_WAYPOINT_LOOK_AHEAD_DISTANCE = 73200,
	SLIP_RACE_DRONE_INITIAL_MAXIMUM_SPEED = 715000,
	SLIP_RACE_DRONE_CURVE_WAYPOINT_THRESHOLD = 244000,
	SLIP_RACE_DRONE_CURVE_LOOK_AHEAD_DISTANCE = 390400,
	SLIP_RACE_DRONE_UNCHANGED_CURVE_SCALE_Q14 = 3 * SLIP_Q14_ONE / 4,
	SLIP_RACE_DRONE_CURVE_SPEED_MULTIPLIER = 193050,
	SLIP_RACE_DRONE_CURVE_BASE_SPEED = 128700,
	SLIP_RACE_DRONE_MAXIMUM_SPEED = 178750,
	SLIP_RACE_DRONE_CONTROL_COMPONENT_LIMIT_Q14 = 2048,
	SLIP_RACE_DRONE_CONTROL_GAIN_SHIFT = 3,
	SLIP_RACE_DRONE_INTERPOLATION_PRESCALE_BITS = 4,
	SLIP_RACE_DRONE_INTERPOLATION_PRODUCT_SHIFT = SLIP_Q14_FRACTION_BITS - SLIP_RACE_DRONE_INTERPOLATION_PRESCALE_BITS,
	SLIP_RACE_DRONE_HEADING_RESPONSE_PRODUCT_SHIFT = 12,
	SLIP_RACE_DRONE_ROTATION_PRODUCT_SHIFT = 16,
	/* Gain of 1 + 1/2 + 1/4, with each signed contribution rounded separately. */
	SLIP_RACE_DRONE_ROTATION_HALF_GAIN_SHIFT = 1,
	SLIP_RACE_DRONE_ROTATION_QUARTER_GAIN_SHIFT = 2,
	SLIP_RACE_DRONE_HEADING_GAIN_SHIFT = 1,
	SLIP_RACE_DRONE_HEADING_LIMIT = 32512,
	SLIP_RACE_DRONE_BONUS_LIFETIME_MS = 15000,
	SLIP_RACE_DRONE_COLLISION_RADIUS = 9760,
	SLIP_RACE_DRONE_COLLISION_DURATION_MS = 3000
};

typedef struct SlipRaceDronePrivate {
	uint32_t initializeOrientation;
} SlipRaceDronePrivate;

static SlipRaceDroneHostBindings *SlipRaceDrone_hostBindings;
static int32_t SlipRaceDrone_spawnTimer;
static int32_t SlipRaceDrone_activeCount;
static uint32_t SlipRaceDrone_alternateFlags;

static const SlipView3DMatrix SlipRaceDrone_identity = {
    .m = {SLIP_Q14_ONE, 0, 0, 0, SLIP_Q14_ONE, 0, 0, 0, SLIP_Q14_ONE},
};

void SlipRaceDrone_BindHostContext(SlipRaceDroneHostBindings *bindings) { SlipRaceDrone_hostBindings = bindings; }

bool SlipRaceDrone_Initialize(const char *const *archives, size_t archiveCount,
                              TrackViewResourceHandleRegistry *resourceRegistry, SlipResourcePayload *artPayload,
                              uint16_t *artHandle) {
	uint32_t resourceHandle;
	(void)archives;
	(void)archiveCount;

	if (!TrackView_LoadNamedResource(resourceRegistry, "DRONE.ART", &resourceHandle))
		return false;
	*artHandle = (uint16_t)resourceHandle;
	SlipActor_PreloadResources(*artHandle, &SlipActorHost_resourceCalls);
	*artPayload = SlipResourceHost_Payload(*artHandle);
	SlipRaceDrone_spawnTimer = SLIP_RACE_DRONE_INITIAL_SPAWN_DELAY_MS;
	SlipRaceDrone_activeCount = 0;
	SlipRaceDrone_alternateFlags = 0;
	return true;
}

void SlipRaceDrone_Shutdown(uint16_t artHandle, SlipResourcePayload *artPayload,
                            TrackViewResourceHandleRegistry *registry) {
	(void)artPayload;
	(void)registry;
	SlipActor_ReleaseResources(artHandle, &SlipActorHost_resourceCalls);
	SlipResourceHost_Release(NULL, artHandle);
}

static void SlipRaceDrone_TrackPointAhead(uint32_t traversalDirection, uint16_t objectOffset, uint32_t recordCount,
                                          SlipView3DVec32 *point) {
	SlipRaceDroneHostBindings *const bindings = SlipRaceDrone_hostBindings;
	SlipRacePlayerHostBindings *const player = bindings->playerBindings;
	SlipRacePlayerRoadRecord road;
	uint8_t *record;
	const uint8_t *const trackData = player->trdBase;

	(void)SlipRacePlayer_FindRoadRecord(objectOffset, player->objectTable, player->objectTableBytes,
	                                    player->slotListBase, player->slotListBytes, player->slotListBaseOffset,
	                                    player->trdBase, player->trackDataSize, player->trackDataOffset,
	                                    player->componentBase, player->componentBaseBytes, player->componentBaseOffset,
	                                    player->trackTable, player->trackTableBytes, &road);
	record = road.record;
	if (traversalDirection == 0) {
		while (recordCount != 0) {
			uint16_t nextOffset = SlipBytes_ReadLE16(record + SLIP_TRD_WAYPOINT_NEXT_LINK_OFFSET);

			record = (uint8_t *)trackData + nextOffset;
			*point = (SlipView3DVec32){(int32_t)SlipBytes_ReadLE32(record + SLIP_TRD_POSITION_X_OFFSET),
			                           (int32_t)SlipBytes_ReadLE32(record + SLIP_TRD_POSITION_Y_OFFSET),
			                           (int32_t)SlipBytes_ReadLE32(record + SLIP_TRD_POSITION_Z_OFFSET)};
			nextOffset = SlipBytes_ReadLE16(record + SLIP_TRD_WAYPOINT_NEXT_BRANCH_OFFSET);
			if (nextOffset == 0) {
				--recordCount;
				continue;
			}
			record = (uint8_t *)trackData + nextOffset;
			do {
				nextOffset = SlipBytes_ReadLE16(record + SLIP_TRD_WAYPOINT_NEXT_LINK_OFFSET);
				record = (uint8_t *)trackData + nextOffset;
			} while (SlipBytes_ReadLE16(record + SLIP_TRD_WAYPOINT_PREVIOUS_BRANCH_OFFSET) == 0);
		}
	} else {
		while (recordCount != 0) {
			uint16_t nextOffset = SlipBytes_ReadLE16(record + SLIP_TRD_WAYPOINT_PREVIOUS_LINK_OFFSET);

			record = (uint8_t *)trackData + nextOffset;
			*point = (SlipView3DVec32){(int32_t)SlipBytes_ReadLE32(record + SLIP_TRD_POSITION_X_OFFSET),
			                           (int32_t)SlipBytes_ReadLE32(record + SLIP_TRD_POSITION_Y_OFFSET),
			                           (int32_t)SlipBytes_ReadLE32(record + SLIP_TRD_POSITION_Z_OFFSET)};
			nextOffset = SlipBytes_ReadLE16(record + SLIP_TRD_WAYPOINT_PREVIOUS_BRANCH_OFFSET);
			if (nextOffset == 0) {
				--recordCount;
				continue;
			}
			record = (uint8_t *)trackData + nextOffset;
			do {
				nextOffset = SlipBytes_ReadLE16(record + SLIP_TRD_WAYPOINT_PREVIOUS_LINK_OFFSET);
				record = (uint8_t *)trackData + nextOffset;
			} while (SlipBytes_ReadLE16(record + SLIP_TRD_WAYPOINT_NEXT_BRANCH_OFFSET) == 0);
		}
	}
}

void SlipRaceDrone_Move(uint16_t objectOffset, uint32_t initializeOrientation, uint32_t move) {
	SlipRaceDroneHostBindings *const bindings = SlipRaceDrone_hostBindings;
	SlipRacePlayerHostBindings callbackContext = *bindings->playerBindings;
	SlipRacePlayerHostBindings *const player = &callbackContext;
	SlipView3DVec32 waypoints[3];
	SlipObjectPosition objectPosition;
	SlipDraw3DApproxAbsVectorLength approximate;
	SlipView3DNormalizeVector3D normalized;
	SlipRacePlayerTrackDistance distance = {0};
	SlipObjectMatrixCopy matrixCopy;
	SlipObjectMatrixInstall matrixInstall;
	SlipObjectRotate rotate;
	SlipObjectSetDirection direction;
	SlipView3DMatrix matrix;
	SlipView3DMatrix levelMatrix;
	SlipView3DVec32 targetDelta;
	SlipView3DVec32 transformed;
	SlipView3DVec32 velocity;
	uint32_t waypointDistance;
	int32_t interpolationDistance;
	int32_t maximumSpeed = 0;
	int16_t steering;
	int16_t pitch;
	int16_t frameStep;

	callbackContext.objectOffset = objectOffset;
	frameStep = (int16_t)(uint16_t)SlipFrameTimer_Step();
	(void)SlipRacePlayer_LoadWaypoints(player, waypoints);
	(void)SlipObject_Position(player->objectTable, player->objectTableBytes, objectOffset, &objectPosition);
	targetDelta = (SlipView3DVec32){(int32_t)(objectPosition.positionX - (uint32_t)waypoints[1].x),
	                                (int32_t)(objectPosition.positionY - (uint32_t)waypoints[1].y),
	                                (int32_t)(objectPosition.positionZ - (uint32_t)waypoints[1].z)};
	targetDelta.x = -targetDelta.x;
	targetDelta.y = -targetDelta.y;
	targetDelta.z = -targetDelta.z;
	SlipDraw3D_ApproxAbsVectorLength((uint32_t)targetDelta.x, (uint32_t)targetDelta.y, (uint32_t)targetDelta.z,
	                                 &approximate);
	waypointDistance = approximate.approximateLength;
	if ((int32_t)waypointDistance <= SLIP_RACE_DRONE_WAYPOINT_LOOK_AHEAD_DISTANCE) {
		waypoints[0] = waypoints[1];
		waypoints[1] = waypoints[2];
	}
	(void)SlipView3D_NormalizeVector3D((uint32_t)waypoints[0].x - (uint32_t)waypoints[1].x,
	                                   (uint32_t)waypoints[0].y - (uint32_t)waypoints[1].y,
	                                   (uint32_t)waypoints[0].z - (uint32_t)waypoints[1].z, &normalized);
	interpolationDistance = (int32_t)waypointDistance - SLIP_RACE_DRONE_WAYPOINT_LOOK_AHEAD_DISTANCE;
	if (interpolationDistance < 0) {
		SlipDraw3D_ApproxAbsVectorLength((uint32_t)waypoints[0].x - (uint32_t)waypoints[1].x,
		                                 (uint32_t)waypoints[0].y - (uint32_t)waypoints[1].y,
		                                 (uint32_t)waypoints[0].z - (uint32_t)waypoints[1].z, &approximate);
		interpolationDistance += (int32_t)approximate.approximateLength;
	}
	interpolationDistance >>= SLIP_RACE_DRONE_INTERPOLATION_PRESCALE_BITS;
	waypoints[1].x += (int32_t)((int64_t)(int16_t)normalized.unitXQ14 * interpolationDistance >>
	                            SLIP_RACE_DRONE_INTERPOLATION_PRODUCT_SHIFT);
	waypoints[1].y += (int32_t)((int64_t)(int16_t)normalized.unitYQ14 * interpolationDistance >>
	                            SLIP_RACE_DRONE_INTERPOLATION_PRODUCT_SHIFT);
	waypoints[1].z += (int32_t)((int64_t)(int16_t)normalized.unitZQ14 * interpolationDistance >>
	                            SLIP_RACE_DRONE_INTERPOLATION_PRODUCT_SHIFT);
	targetDelta = (SlipView3DVec32){waypoints[1].x - (int32_t)objectPosition.positionX,
	                                waypoints[1].y - (int32_t)objectPosition.positionY,
	                                waypoints[1].z - (int32_t)objectPosition.positionZ};

	if (move != 0) {
		uint32_t curve;

		maximumSpeed = SLIP_RACE_DRONE_INITIAL_MAXIMUM_SPEED;
		if ((int32_t)waypointDistance <= SLIP_RACE_DRONE_CURVE_WAYPOINT_THRESHOLD) {
			(void)SlipRacePlayer_TrackDistance(
			    SLIP_RACE_DRONE_CURVE_LOOK_AHEAD_DISTANCE, objectOffset, player->objectTable, player->objectTableBytes,
			    player->slotListBase, player->slotListBytes, player->slotListBaseOffset, player->trdBase,
			    player->trackDataSize, player->trackDataOffset, player->componentBase, player->componentBaseBytes,
			    player->componentBaseOffset, player->trackTable, player->trackTableBytes, &distance);
			curve = SLIP_Q14_ONE - distance.accumulatedCurve;
			if ((int32_t)curve < 0) {
				curve = 0;
			}
			if (curve != SLIP_RACE_DRONE_UNCHANGED_CURVE_SCALE_Q14) {
				maximumSpeed =
				    (int32_t)(((uint64_t)SLIP_RACE_DRONE_CURVE_SPEED_MULTIPLIER * curve) >> SLIP_Q14_FRACTION_BITS) +
				    SLIP_RACE_DRONE_CURVE_BASE_SPEED;
			}
		}
		if (maximumSpeed > SLIP_RACE_DRONE_MAXIMUM_SPEED) {
			maximumSpeed = SLIP_RACE_DRONE_MAXIMUM_SPEED;
		}
	}

	if (initializeOrientation != 0) {
		(void)SlipView3D_BuildMatrixFromVector32(&matrix, (uint32_t)targetDelta.x, (uint32_t)targetDelta.y,
		                                         (uint32_t)targetDelta.z);
		(void)SlipObject_MatrixInstall(player->objectTable, player->objectTableBytes, objectOffset, &matrix,
		                               &matrixInstall);
	} else {
		(void)SlipObject_MatrixCopy(player->objectTable, player->objectTableBytes, objectOffset, &matrix, &matrixCopy);
		levelMatrix = matrix;
		levelMatrix.m[1] = 0;
		levelMatrix.m[3] = 0;
		SlipView3D_OrthonormalizeForwardBasis(&levelMatrix);
		(void)SlipView3D_NormalizeVector3D((uint32_t)targetDelta.x, (uint32_t)targetDelta.y, (uint32_t)targetDelta.z,
		                                   &normalized);
		transformed =
		    SlipView3D_TransformVector(&levelMatrix, (SlipView3DVec32){(int16_t)(uint16_t)normalized.unitXQ14,
		                                                               (int16_t)(uint16_t)normalized.unitYQ14,
		                                                               (int16_t)(uint16_t)normalized.unitZQ14});
		steering = (int16_t)transformed.x;
		pitch = (int16_t)transformed.y;
		if (steering > SLIP_RACE_DRONE_CONTROL_COMPONENT_LIMIT_Q14) {
			steering = SLIP_RACE_DRONE_CONTROL_COMPONENT_LIMIT_Q14;
		}
		if (steering < -SLIP_RACE_DRONE_CONTROL_COMPONENT_LIMIT_Q14) {
			steering = -SLIP_RACE_DRONE_CONTROL_COMPONENT_LIMIT_Q14;
		}
		steering = (int16_t)(uint16_t)((uint16_t)steering << SLIP_RACE_DRONE_CONTROL_GAIN_SHIFT);
		if (pitch > SLIP_RACE_DRONE_CONTROL_COMPONENT_LIMIT_Q14) {
			pitch = SLIP_RACE_DRONE_CONTROL_COMPONENT_LIMIT_Q14;
		}
		if (pitch < -SLIP_RACE_DRONE_CONTROL_COMPONENT_LIMIT_Q14) {
			pitch = -SLIP_RACE_DRONE_CONTROL_COMPONENT_LIMIT_Q14;
		}
		pitch = (int16_t)(uint16_t)((uint16_t)pitch << SLIP_RACE_DRONE_CONTROL_GAIN_SHIFT);
		{
			const int16_t steeringTarget = steering;
			int16_t headingTarget = (int16_t)(steeringTarget >> SLIP_RACE_DRONE_HEADING_GAIN_SHIFT);
			int16_t rotationFromPitch;
			int16_t rotationFromSteering;

			if (headingTarget < -SLIP_RACE_DRONE_HEADING_LIMIT) {
				headingTarget = -SLIP_RACE_DRONE_HEADING_LIMIT;
			}
			if (headingTarget > SLIP_RACE_DRONE_HEADING_LIMIT) {
				headingTarget = SLIP_RACE_DRONE_HEADING_LIMIT;
			}
			steering = (int16_t)((int32_t)frameStep *
			                         (int16_t)(headingTarget - SlipView3D_HeadingFromMatrix(player->maths, &matrix)) >>
			                     SLIP_RACE_DRONE_HEADING_RESPONSE_PRODUCT_SHIFT);
			rotationFromSteering =
			    (int16_t)((int32_t)frameStep * steeringTarget >> SLIP_RACE_DRONE_ROTATION_PRODUCT_SHIFT);
			rotationFromSteering =
			    (int16_t)(rotationFromSteering + (int16_t)((int32_t)(int16_t)-matrix.m[1] * frameStep >>
			                                               SLIP_RACE_DRONE_ROTATION_PRODUCT_SHIFT));
			rotationFromPitch = (int16_t)((int32_t)frameStep * pitch >> SLIP_RACE_DRONE_ROTATION_PRODUCT_SHIFT);
			rotationFromSteering =
			    (int16_t)(rotationFromSteering + (rotationFromSteering >> SLIP_RACE_DRONE_ROTATION_HALF_GAIN_SHIFT) +
			              (rotationFromSteering >> SLIP_RACE_DRONE_ROTATION_QUARTER_GAIN_SHIFT));
			rotationFromPitch =
			    (int16_t)(rotationFromPitch + (rotationFromPitch >> SLIP_RACE_DRONE_ROTATION_HALF_GAIN_SHIFT) +
			              (rotationFromPitch >> SLIP_RACE_DRONE_ROTATION_QUARTER_GAIN_SHIFT));
			(void)SlipObject_Rotate(player->objectTable, player->objectTableBytes, objectOffset, rotationFromPitch,
			                        steering, 0, rotationFromSteering, player->maths, &rotate);
		}
	}

	if (move != 0) {
		(void)SlipObject_MatrixCopy(player->objectTable, player->objectTableBytes, objectOffset, &matrix, &matrixCopy);
		velocity = SlipView3D_ScaleAxesQ14(matrix.m[6], matrix.m[7], matrix.m[8], maximumSpeed);
		(void)SlipObject_SetDirection(player->objectTable, player->objectTableBytes, objectOffset, velocity.x,
		                              velocity.y, velocity.z, &direction);
	} else {
		SlipObject_CopyMatrixForwardToDirection(player->objectTable, objectOffset);
	}
}

uint32_t SlipRaceDrone_Event(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue, uint32_t eventFlags,
                             uint16_t objectOffset, uintptr_t dispatchData, uint32_t dispatchFrame) {
	SlipRaceDronePrivate *const state = (SlipRaceDronePrivate *)(void *)SlipObject_PrivateState(objectOffset);

	(void)eventPayload;
	(void)eventFlags;
	(void)dispatchData;
	switch ((SlipObjectEvent)(eventCode & UINT16_MAX)) {
	case SLIP_OBJECT_EVENT_FREE:
		--SlipRaceDrone_activeCount;
		return 0;
	case SLIP_OBJECT_EVENT_APPLY_DAMAGE: {
		SlipObjectPosition objectPosition;
		int32_t bonusType = -1;

		if (SlipRace_gameMode != SLIP_RACE_GAME_SINGLE_PLAYER) {
			const SlipRaceRacerState *const attackingRacer = SlipRacePlayer_RacerState((uint16_t)eventValue);

			if (attackingRacer != NULL && attackingRacer->racePosition == 1u) {
				bonusType = SLIP_RACE_BONUS_REVERSE_CONTROLS;
			}
		}

		SlipRaceEffects_Debris(SLIP_RACE_DRONE_DESTRUCTION_DEBRIS_COUNT, objectOffset, dispatchFrame);
		(void)SlipObject_Position(SlipRaceDrone_hostBindings->playerBindings->objectTable,
		                          SlipRaceDrone_hostBindings->playerBindings->objectTableBytes, objectOffset,
		                          &objectPosition);
		(void)SlipRaceCollision_RemoveBody(objectOffset);
		SlipObject_Free(objectOffset, 0, 0, 0, 0, 0, 0);
		SlipRaceBonus_Create((SlipView3DVec32){(int32_t)objectPosition.positionX, (int32_t)objectPosition.positionY,
		                                       (int32_t)objectPosition.positionZ},
		                     SLIP_RACE_DRONE_BONUS_LIFETIME_MS, bonusType);
		return 0;
	}
	case SLIP_OBJECT_EVENT_UPDATE: {
		const uint32_t initializeOrientation = state->initializeOrientation;

		state->initializeOrientation = 0;
		SlipRaceDrone_Move(objectOffset, initializeOrientation, 1);
		return 0;
	}
	case SLIP_OBJECT_EVENT_COLLISION_STOP:
		(void)SlipRaceCollision_RemoveBody(objectOffset);

		SlipRaceEffects_Debris(SLIP_RACE_DRONE_DESTRUCTION_DEBRIS_COUNT, objectOffset, dispatchFrame);
		(void)SlipRaceCollision_RemoveBody(objectOffset);
		SlipObject_Free(objectOffset, 0, 0, 0, 0, 0, 0);
		return 0;
	case SLIP_OBJECT_EVENT_INITIALIZE:
		state->initializeOrientation = 1;
		return 0;
	case SLIP_OBJECT_EVENT_COLLISION_BOUNCE:

		SlipRaceEffects_Collision(SlipRaceCollision_bounceEvent.contactPosition, SLIP_RACE_DRONE_COLLISION_RADIUS,
		                          SLIP_RACE_DRONE_COLLISION_DURATION_MS);
		(void)SlipRaceCollision_RemoveBody(objectOffset);
		SlipObject_Free(objectOffset, 0, 0, 0, 0, 0, 0);
		return 0;
	default:
		return 1;
	}
}

static void SlipRaceDrone_Spawn(SlipView3DVec32 position) {
	SlipRaceDroneHostBindings *const bindings = SlipRaceDrone_hostBindings;
	SlipRacePlayerHostBindings *player;
	SlipObjectSlotFill fill;
	SlipObjectActorHandleWriteResult setSlot;
	SlipArticSlotCreate createArtic;
	SlipObjectSetCallback setCallback;
	SlipArticSlotMainBounds mainPartBounds;
	SlipTrackWorldAddSlot addSlot;
	uint16_t mainShape;
	bool mainShapeCarry;
	uint16_t objectOffset;
	uint32_t actorFlags;

	player = bindings->playerBindings;
	(void)SlipObject_SlotFill(&SlipRaceDrone_identity, (uint32_t)position.x, (uint32_t)position.y, (uint32_t)position.z,
	                          TrackView_ExecuteDroneDrawCallback, 0, SlipRaceDrone_Event, &fill);
	if (fill.carryOut) {
		return;
	}
	objectOffset = (uint16_t)fill.objectOffset;
	SlipRaceDrone_alternateFlags ^= 1u;
	actorFlags = SlipRaceDrone_alternateFlags != 0 ? SLIP_RACE_DRONE_ALTERNATING_OBJECT_FLAG : 0u;
	(void)SlipObject_SetActorHandle(objectOffset, actorFlags, &setSlot);
	(void)SlipArticSlot_Create(objectOffset, bindings->artResourceHandle, bindings->artPayload,
	                           bindings->artPayloadBytes, bindings->articPool, player->objectTable,
	                           player->objectTableBytes, bindings->findResource, bindings->findResourceUser,
	                           &createArtic);
	if (createArtic.creationFailed) {
		SlipObject_Free(objectOffset, 0, 0, 0, 0, 0, 0);
		return;
	}
	(void)SlipObject_SetDrawCallback(objectOffset, TrackView_ExecuteArticDrawCallback, 0, &setCallback);
	if (SlipRaceCollision_CreateBody(objectOffset, SLIP_COLLISION_BODY_RACER)) {
		SlipObject_Free(objectOffset, 0, 0, 0, 0, 0, 0);
		return;
	}
	(void)SlipArticSlot_GetMainBounds(objectOffset, player->objectTable, player->objectTableBytes,
	                                  player->articSlotPool, player->articSlotPoolBytes, player->articSlotPoolOffset,
	                                  &mainPartBounds);
	SlipRaceCollision_SetBodyBounds(objectOffset, mainPartBounds.minX, mainPartBounds.minY, mainPartBounds.minZ,
	                                mainPartBounds.maxX, mainPartBounds.maxY, mainPartBounds.maxZ);
	(void)SlipArticSlot_GetMainShape(objectOffset, 0, player->objectTable, player->objectTableBytes,
	                                 player->articSlotPool, player->articSlotPoolBytes, player->articSlotPoolOffset,
	                                 &mainShape, &mainShapeCarry);
	(void)SlipTrackWorld_AddSlot(
	    objectOffset, (SLIP_TRACK_SLOT_BOX_COLLISION | SLIP_TRACK_SLOT_DRONE), 1u, bindings->slotDrawBase,
	    bindings->slotDrawBytes, player->slotDrawCallbacks, player->slotDrawCallbackCount,
	    bindings->slotDrawBaseAddress, bindings->slotDrawFreeListAddress, player->slotListBase, player->slotListBytes,
	    player->slotListBaseOffset, player->slotListSentinelOffset, bindings->slotListFreeListAddress,
	    player->objectTable, player->objectTableBytes, player->articSlotPool, player->articSlotPoolBytes,
	    player->articSlotPoolOffset, player->trdBase, player->trackDataSize, player->trackDataOffset,
	    player->componentBase, player->componentBaseBytes, player->componentBaseOffset, player->trackTable,
	    player->trackTableBytes, &addSlot);
	if (addSlot.carryOut || SlipRaceCollision_Query(objectOffset)) {
		SlipObject_Free(objectOffset, 0, 0, 0, 0, 0, 0);
		return;
	}
	++SlipRaceDrone_activeCount;
}

void SlipRaceDrone_Update(void) {
	SlipView3DVec32 spawnPoint;
	uint16_t playerObject;

	SlipRaceDrone_spawnTimer -= (int32_t)SlipFrameTimer_Values().deltaMilliseconds;
	if (SlipRaceDrone_spawnTimer >= 0) {
		return;
	}
	if (SlipRaceDrone_activeCount >= SLIP_RACE_DRONE_MAXIMUM_ACTIVE_COUNT) {
		SlipRaceDrone_spawnTimer = SLIP_RACE_DRONE_INITIAL_SPAWN_DELAY_MS;
		return;
	}
	if (SlipRace_gameMode != SLIP_RACE_GAME_SINGLE_PLAYER && SlipRace_gameMode != SLIP_RACE_GAME_SPLIT_SCREEN) {
		playerObject = SlipRacePlayer_playerOneObject;
		if (SlipRace_secondPlayerEnabled == 0) {
			playerObject = SlipRacePlayer_thirdObject;
		}
	} else {
		playerObject = SlipRacePlayer_playerOneObject;
	}
	SlipRaceDrone_TrackPointAhead(0, playerObject, SLIP_RACE_DRONE_SPAWN_WAYPOINTS_AHEAD, &spawnPoint);
	SlipRaceDrone_Spawn(spawnPoint);
	SlipRaceDrone_spawnTimer = SLIP_RACE_DRONE_SPAWN_INTERVAL_MS;
}
