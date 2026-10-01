#include "race_drone.h"

#include "actor_host.h"
#include "actor_resources.h"
#include "draw3d.h"
#include "frame_timer.h"
#include "race.h"
#include "race_bonus.h"
#include "race_collision.h"
#include "race_effects.h"
#include "resource_host.h"
#include "track_view_render.h"

#include <string.h>

typedef struct SlipRaceDronePrivate {
	uint32_t initializeOrientation;
} SlipRaceDronePrivate;

static SlipRaceDroneHostBindings *SlipRaceDrone_hostBindings;
static int32_t SlipRaceDrone_spawnTimer;
static int32_t SlipRaceDrone_activeCount;
static uint32_t SlipRaceDrone_alternateFlags;

static const SlipView3DMatrix SlipRaceDrone_identity = {
    .m = {0x4000, 0, 0, 0, 0x4000, 0, 0, 0, 0x4000},
};

void SlipRaceDrone_BindHostContext(SlipRaceDroneHostBindings *bindings) { SlipRaceDrone_hostBindings = bindings; }

bool SlipRaceDrone_Initialize(const char *const *archives, size_t archiveCount,
                              TrackViewResourceHandleRegistry *resourceRegistry, SlipResourcePayload *artPayload,
                              uint16_t *artHandle) {
	uint32_t resourceHandle;

	if (!TrackView_LoadNamedResource(resourceRegistry, "DRONE.ART", &resourceHandle))
		return false;
	*artHandle = (uint16_t)resourceHandle;
	if (resourceRegistry->hostResources) {

		SlipActor_PreloadResources(*artHandle, &SlipActorHost_resourceCalls);
		*artPayload = SlipResourceHost_Payload(*artHandle);
	} else {
		if (!SlipResource_LoadByName(archives, archiveCount, "DRONE.ART", artPayload))
			return false;
		SlipArticSlot_PreloadResources(artPayload->data, TrackView_LoadNamedResource, resourceRegistry);
	}
	SlipRaceDrone_spawnTimer = 1000;
	SlipRaceDrone_activeCount = 0;
	SlipRaceDrone_alternateFlags = 0;
	return true;
}

void SlipRaceDrone_Shutdown(uint16_t artHandle, SlipResourcePayload *artPayload,
                            TrackViewResourceHandleRegistry *registry) {
	if (registry->hostResources) {

		SlipActor_ReleaseResources(artHandle, &SlipActorHost_resourceCalls);
		SlipResourceHost_Release(NULL, artHandle);
	} else {
		SlipArticSlot_ReleaseResources(artPayload->data, TrackView_FindNameRecord, TrackView_ReleaseResource, registry);
		SlipResource_ReleaseHandle(artPayload);
	}
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
			uint16_t nextOffset = (uint16_t)((uint16_t)record[0] | ((uint16_t)record[1] << 8));

			record = (uint8_t *)trackData + nextOffset;
			*point = (SlipView3DVec32){(int32_t)((uint32_t)record[0x0cu] | ((uint32_t)record[0x0du] << 8) |
			                                     ((uint32_t)record[0x0eu] << 16) | ((uint32_t)record[0x0fu] << 24)),
			                           (int32_t)((uint32_t)record[0x10u] | ((uint32_t)record[0x11u] << 8) |
			                                     ((uint32_t)record[0x12u] << 16) | ((uint32_t)record[0x13u] << 24)),
			                           (int32_t)((uint32_t)record[0x14u] | ((uint32_t)record[0x15u] << 8) |
			                                     ((uint32_t)record[0x16u] << 16) | ((uint32_t)record[0x17u] << 24))};
			nextOffset = (uint16_t)((uint16_t)record[0x04u] | ((uint16_t)record[0x05u] << 8));
			if (nextOffset == 0) {
				--recordCount;
				continue;
			}
			record = (uint8_t *)trackData + nextOffset;
			do {
				nextOffset = (uint16_t)((uint16_t)record[0] | ((uint16_t)record[1] << 8));
				record = (uint8_t *)trackData + nextOffset;
			} while (((uint16_t)record[0x06u] | ((uint16_t)record[0x07u] << 8)) == 0);
		}
	} else {
		while (recordCount != 0) {
			uint16_t nextOffset = (uint16_t)((uint16_t)record[0x02u] | ((uint16_t)record[0x03u] << 8));

			record = (uint8_t *)trackData + nextOffset;
			*point = (SlipView3DVec32){(int32_t)((uint32_t)record[0x0cu] | ((uint32_t)record[0x0du] << 8) |
			                                     ((uint32_t)record[0x0eu] << 16) | ((uint32_t)record[0x0fu] << 24)),
			                           (int32_t)((uint32_t)record[0x10u] | ((uint32_t)record[0x11u] << 8) |
			                                     ((uint32_t)record[0x12u] << 16) | ((uint32_t)record[0x13u] << 24)),
			                           (int32_t)((uint32_t)record[0x14u] | ((uint32_t)record[0x15u] << 8) |
			                                     ((uint32_t)record[0x16u] << 16) | ((uint32_t)record[0x17u] << 24))};
			nextOffset = (uint16_t)((uint16_t)record[0x06u] | ((uint16_t)record[0x07u] << 8));
			if (nextOffset == 0) {
				--recordCount;
				continue;
			}
			record = (uint8_t *)trackData + nextOffset;
			do {
				nextOffset = (uint16_t)((uint16_t)record[0x02u] | ((uint16_t)record[0x03u] << 8));
				record = (uint8_t *)trackData + nextOffset;
			} while (((uint16_t)record[0x04u] | ((uint16_t)record[0x05u] << 8)) == 0);
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
	if ((int32_t)waypointDistance <= 0x11df0) {
		waypoints[0] = waypoints[1];
		waypoints[1] = waypoints[2];
	}
	(void)SlipView3D_NormalizeVector3D((uint32_t)waypoints[0].x - (uint32_t)waypoints[1].x,
	                                   (uint32_t)waypoints[0].y - (uint32_t)waypoints[1].y,
	                                   (uint32_t)waypoints[0].z - (uint32_t)waypoints[1].z, &normalized);
	interpolationDistance = (int32_t)waypointDistance - 0x11df0;
	if (interpolationDistance < 0) {
		SlipDraw3D_ApproxAbsVectorLength((uint32_t)waypoints[0].x - (uint32_t)waypoints[1].x,
		                                 (uint32_t)waypoints[0].y - (uint32_t)waypoints[1].y,
		                                 (uint32_t)waypoints[0].z - (uint32_t)waypoints[1].z, &approximate);
		interpolationDistance += (int32_t)approximate.approximateLength;
	}
	interpolationDistance >>= 4;
	waypoints[1].x += (int32_t)((int64_t)(int16_t)normalized.unitXQ14 * interpolationDistance >> 10);
	waypoints[1].y += (int32_t)((int64_t)(int16_t)normalized.unitYQ14 * interpolationDistance >> 10);
	waypoints[1].z += (int32_t)((int64_t)(int16_t)normalized.unitZQ14 * interpolationDistance >> 10);
	targetDelta = (SlipView3DVec32){waypoints[1].x - (int32_t)objectPosition.positionX,
	                                waypoints[1].y - (int32_t)objectPosition.positionY,
	                                waypoints[1].z - (int32_t)objectPosition.positionZ};

	if (move != 0) {
		uint32_t curve;

		maximumSpeed = 0x000ae8f8;
		if ((int32_t)waypointDistance <= 0x0003b920) {
			(void)SlipRacePlayer_TrackDistance(
			    0x0005f500, objectOffset, player->objectTable, player->objectTableBytes, player->slotListBase,
			    player->slotListBytes, player->slotListBaseOffset, player->trdBase, player->trackDataSize,
			    player->trackDataOffset, player->componentBase, player->componentBaseBytes, player->componentBaseOffset,
			    player->trackTable, player->trackTableBytes, &distance);
			curve = 0x4000u - distance.accumulatedCurve;
			if ((int32_t)curve < 0) {
				curve = 0;
			}
			if (curve != 0x3000u) {
				maximumSpeed = (int32_t)(((uint64_t)0x0002f21au * curve) >> 14) + 0x0001f6bc;
			}
		}
		if (maximumSpeed > 0x0002ba3e) {
			maximumSpeed = 0x0002ba3e;
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
		if (steering > 0x0800) {
			steering = 0x0800;
		}
		if (steering < -0x0800) {
			steering = -0x0800;
		}
		steering = (int16_t)(uint16_t)((uint16_t)steering << 3);
		if (pitch > 0x0800) {
			pitch = 0x0800;
		}
		if (pitch < -0x0800) {
			pitch = -0x0800;
		}
		pitch = (int16_t)(uint16_t)((uint16_t)pitch << 3);
		{
			const int16_t steeringTarget = steering;
			int16_t headingTarget = (int16_t)(steeringTarget >> 1);
			int16_t rotationFromPitch;
			int16_t rotationFromSteering;

			if (headingTarget < -0x7f00) {
				headingTarget = -0x7f00;
			}
			if (headingTarget > 0x7f00) {
				headingTarget = 0x7f00;
			}
			steering = (int16_t)((int32_t)frameStep *
			                         (int16_t)(headingTarget - SlipView3D_HeadingFromMatrix(player->maths, &matrix)) >>
			                     12);
			rotationFromSteering = (int16_t)((int32_t)frameStep * steeringTarget >> 16);
			rotationFromSteering =
			    (int16_t)(rotationFromSteering + (int16_t)((int32_t)(int16_t)-matrix.m[1] * frameStep >> 16));
			rotationFromPitch = (int16_t)((int32_t)frameStep * pitch >> 16);
			rotationFromSteering =
			    (int16_t)(rotationFromSteering + (rotationFromSteering >> 1) + (rotationFromSteering >> 2));
			rotationFromPitch = (int16_t)(rotationFromPitch + (rotationFromPitch >> 1) + (rotationFromPitch >> 2));
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
	switch ((SlipObjectEvent)(eventCode & 0xffffu)) {
	case SLIP_OBJECT_EVENT_FREE:
		--SlipRaceDrone_activeCount;
		return 0;
	case SLIP_OBJECT_EVENT_APPLY_DAMAGE: {
		SlipObjectPosition objectPosition;
		int32_t bonusType = -1;

		if (SlipRace_gameMode != 0) {
			const SlipRaceRacerState *const attackingRacer = SlipRacePlayer_RacerState((uint16_t)eventValue);

			if (attackingRacer != NULL && attackingRacer->racePosition == 1u) {
				bonusType = 3;
			}
		}

		SlipRaceEffects_Debris(5, objectOffset, dispatchFrame);
		(void)SlipObject_Position(SlipRaceDrone_hostBindings->playerBindings->objectTable,
		                          SlipRaceDrone_hostBindings->playerBindings->objectTableBytes, objectOffset,
		                          &objectPosition);
		(void)SlipRaceCollision_RemoveBody(objectOffset);
		SlipObject_Free(objectOffset, 0, 0, 0, 0, 0, 0);
		SlipRaceBonus_Create((SlipView3DVec32){(int32_t)objectPosition.positionX, (int32_t)objectPosition.positionY,
		                                       (int32_t)objectPosition.positionZ},
		                     0x3a98, bonusType);
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

		SlipRaceEffects_Debris(5, objectOffset, dispatchFrame);
		(void)SlipRaceCollision_RemoveBody(objectOffset);
		SlipObject_Free(objectOffset, 0, 0, 0, 0, 0, 0);
		return 0;
	case SLIP_OBJECT_EVENT_INITIALIZE:
		state->initializeOrientation = 1;
		return 0;
	case SLIP_OBJECT_EVENT_COLLISION_BOUNCE:

		SlipRaceEffects_Collision(SlipRaceCollision_bounceEvent.contactPosition, 0x2620, 0xbb8);
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
	actorFlags = SlipRaceDrone_alternateFlags != 0 ? 8u : 0u;
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
	if (SlipRaceCollision_CreateBody(objectOffset, 7u)) {
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
	    objectOffset, 0x41u, 1u, bindings->slotDrawBase, bindings->slotDrawBytes, player->slotDrawCallbacks,
	    player->slotDrawCallbackCount, bindings->slotDrawBaseAddress, bindings->slotDrawFreeListAddress,
	    player->slotListBase, player->slotListBytes, player->slotListBaseOffset, player->slotListSentinelOffset,
	    bindings->slotListFreeListAddress, player->objectTable, player->objectTableBytes, player->articSlotPool,
	    player->articSlotPoolBytes, player->articSlotPoolOffset, player->trdBase, player->trackDataSize,
	    player->trackDataOffset, player->componentBase, player->componentBaseBytes, player->componentBaseOffset,
	    player->trackTable, player->trackTableBytes, &addSlot);
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
	if (SlipRaceDrone_activeCount >= 6) {
		SlipRaceDrone_spawnTimer = 1000;
		return;
	}
	if (SlipRace_gameMode != 0 && SlipRace_gameMode != 1) {
		playerObject = SlipRacePlayer_playerOneObject;
		if (SlipRace_secondPlayerEnabled == 0) {
			playerObject = SlipRacePlayer_thirdObject;
		}
	} else {
		playerObject = SlipRacePlayer_playerOneObject;
	}
	SlipRaceDrone_TrackPointAhead(0, playerObject, 10u, &spawnPoint);
	SlipRaceDrone_Spawn(spawnPoint);
	SlipRaceDrone_spawnTimer = 10000;
}
