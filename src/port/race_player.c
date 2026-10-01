#include "race_player.h"
#include "artic_slot.h"
#include "byte_order.h"
#include "frame_timer.h"
#include "guided_projectile_creation.h"
#include "race_bonus.h"
#include "race_camera.h"
#include "race_collision.h"
#include "race_drone.h"
#include "race_effects.h"
#include "race_session.h"
#include "race_voice_host.h"
#include "raster.h"
#include "runtime.h"
#include "track_view_render.h"

#include <string.h>

static SlipRacePlayerHostBindings *SlipRacePlayer_hostContext;

uint32_t SlipRacePlayer_MiniMinesEventWithCalls(uint32_t eventCode, uint16_t object,
                                                const SlipRacePlayerMiniMinesEventCalls *calls) {
	void *const context = calls->context;
	if ((uint16_t)eventCode == SLIP_OBJECT_EVENT_UPDATE) {
		const uint32_t step = calls->timer(context);
		SlipRacePlayerProjectileState *const state = calls->private(context, object);
		state->remainingTime = (int32_t)((uint32_t)state->remainingTime - step);
		if (state->remainingTime < 0)
			calls->free(context, object);
		return 0;
	}
	if ((uint16_t)eventCode == SLIP_OBJECT_EVENT_COLLISION_STOP) {
		SlipView3DVec32 position = calls->objectPosition(context, object);

		enum { COLLISION_RADIUS = 9760, COLLISION_DURATION = 3000 };

		calls->collision(context, position, COLLISION_RADIUS, COLLISION_DURATION);
		calls->removeBody(context, object);
		calls->free(context, object);
		return 0;
	}
	if ((uint16_t)eventCode == SLIP_OBJECT_EVENT_COLLISION_BOUNCE) {
		SlipView3DVec32 position = calls->objectPosition(context, object);

		enum { COLLISION_RADIUS = 9760, COLLISION_DURATION = 3000 };

		calls->collision(context, position, COLLISION_RADIUS, COLLISION_DURATION);
		calls->free(context, object);
		return 0;
	}
	return 1;
}

static uint32_t SlipRacePlayer_MiniMinesTimer(void *context) {
	(void)context;
	return SlipFrameTimer_Values().deltaMilliseconds;
}

static SlipRacePlayerProjectileState *SlipRacePlayer_MiniMinesPrivate(void *context, uint16_t object) {
	(void)context;
	return (SlipRacePlayerProjectileState *)(void *)SlipObject_PrivateState(object);
}

static SlipView3DVec32 SlipRacePlayer_MiniMinesPosition(void *context, uint16_t object) {
	SlipRacePlayerHostBindings *const host = context;
	SlipObjectPosition position;
	(void)SlipObject_Position(host->objectTable, host->objectTableBytes, object, &position);
	return (SlipView3DVec32){(int32_t)position.positionX, (int32_t)position.positionY, (int32_t)position.positionZ};
}

static void SlipRacePlayer_MiniMinesCollision(void *context, SlipView3DVec32 position, uint32_t radius,
                                              uint32_t duration) {
	(void)context;
	SlipRaceEffects_Collision(position, radius, duration);
}

static void SlipRacePlayer_MiniMinesRemoveBody(void *context, uint16_t object) {
	(void)context;
	(void)SlipRaceCollision_RemoveBody(object);
}

static void SlipRacePlayer_MiniMinesFree(void *context, uint16_t object) {
	(void)context;
	SlipObject_Free(object, 0, 0, 0, 0, 0, 0);
}

uint32_t SlipRacePlayer_MiniMinesEvent(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue,
                                       uint32_t eventFlags, uint16_t object, uintptr_t dispatchData,
                                       uint32_t dispatchFrame) {
	(void)eventPayload;
	(void)eventValue;
	(void)eventFlags;
	(void)dispatchData;
	(void)dispatchFrame;
	const SlipRacePlayerMiniMinesEventCalls calls = {.context = SlipRacePlayer_hostContext,
	                                                 .timer = SlipRacePlayer_MiniMinesTimer,
	                                                 .private = SlipRacePlayer_MiniMinesPrivate,
	                                                 .objectPosition = SlipRacePlayer_MiniMinesPosition,
	                                                 .collision = SlipRacePlayer_MiniMinesCollision,
	                                                 .removeBody = SlipRacePlayer_MiniMinesRemoveBody,
	                                                 .free = SlipRacePlayer_MiniMinesFree};
	return SlipRacePlayer_MiniMinesEventWithCalls(eventCode, object, &calls);
}

static uint32_t SlipRacePlayer_ProjectileEvent(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue,
                                               uint32_t eventFlags, uint16_t objectOffset, uintptr_t dispatchData,
                                               uint32_t dispatchFrame);

const SlipRacePlayerWeaponRecord SlipRacePlayer_records[12] = {
    {"Blaster",
     {0x00000000u, 0x00000000u, 0x00000000u},
     0xFFFFFFFFu,
     0x00000800u,
     0x00000C00u,
     SlipRacePlayer_FireBlaster,
     0x00000145u,
     0x00010000u,
     0x00010000u},
    {"Disrupter",
     {0x00000320u, 0x00000352u, 0x00000384u},
     0x00000008u,
     0x00004000u,
     0x00004000u,
     SlipRaceSession_FireDisrupter,
     0x00000145u,
     0x00010000u,
     0x00010000u},
    {"Frag",
     {0x000001F4u, 0x00000226u, 0x00000258u},
     0x00000003u,
     0x00004000u,
     0x00004000u,
     SlipRaceSession_FireFrag,
     0x00000145u,
     0x00020000u,
     0x000F0000u},
    {"Super Frag",
     {0x000002EEu, 0x00000320u, 0x00000352u},
     0x00000003u,
     0x00004000u,
     0x00004000u,
     SlipRaceSession_FireSuperFrag,
     0x00000145u,
     0x00040000u,
     0x00190000u},
    {"Seeker",
     {0x0000028Au, 0x000002BCu, 0x000002EEu},
     0x00000003u,
     0x00004000u,
     0x00004000u,
     SlipRaceSession_FireSeeker,
     0x00000145u,
     0x000F0000u,
     0x00020000u},
    {"Super Seeker",
     {0x000002EEu, 0x00000320u, 0x00000352u},
     0x00000004u,
     0x00004000u,
     0x00004000u,
     SlipRaceSession_FireSuperSeeker,
     0x00000145u,
     0x00190000u,
     0x00040000u},
    {"Ambler",
     {0x000002BCu, 0x000002EEu, 0x00000320u},
     0x00000003u,
     0x00004000u,
     0x00004000u,
     SlipRaceSession_FireAmbler,
     0x00000145u,
     0x00010000u,
     0x00010000u},
    {"Scrambler",
     {0x00000384u, 0x000003B6u, 0x000003E8u},
     0x00000003u,
     0x00004000u,
     0x00004000u,
     SlipRaceSession_FireScrambler,
     0x00000145u,
     0x00190000u,
     0x00190000u},
    {"Hyper Neuro",
     {0x000001F4u, 0x00000226u, 0x00000258u},
     0x00000003u,
     0x00004000u,
     0x00004000u,
     SlipRaceSession_FireHyperNeuro,
     0x00000145u,
     0x00010000u,
     0x00010000u},
    {"Smoker",
     {0x00000258u, 0x0000028Au, 0x000002BCu},
     0x00000003u,
     0x00004000u,
     0x00004000u,
     SlipRacePlayer_FireSmoker,
     0x00000000u,
     0x00010000u,
     0x00010000u},
    {"Bomber",
     {0x0000028Au, 0x000002BCu, 0x000002EEu},
     0x00000003u,
     0x00004000u,
     0x00004000u,
     SlipRaceSession_FireBomber,
     0x00000145u,
     0x00010000u,
     0x00010000u},
    {"Mini Mines",
     {0x000001F4u, 0x00000226u, 0x00000258u},
     0x00000008u,
     0x00004000u,
     0x00004000u,
     SlipRaceSession_FireMiniMines,
     0x00000000u,
     0x000A0000u,
     0x000A0000u},
};

extern uint32_t SlipRace_gameMode;
extern SlipRacePlayerControl SlipRace_controls;

int32_t SlipConfig_mode;
int32_t SlipConfig_fallbackMode = 1;

const int32_t SlipRacePlayer_propulsionTables[3][10][4] = {{{0x3e00, 0x3600, 0x3500, 0x2100},
                                                            {0x4500, 0x4200, 0x3d00, 0x3800},
                                                            {0x5400, 0x4700, 0x4000, 0x3800},
                                                            {0x4400, 0x4000, 0x3a00, 0x3800},
                                                            {0x3800, 0x3500, 0x3000, 0x2d00},
                                                            {0x3000, 0x2a00, 0x2800, 0x2000},
                                                            {0x3c00, 0x3900, 0x3400, 0x2b00},
                                                            {0x4000, 0x3a00, 0x3600, 0x3200},
                                                            {0x3d00, 0x3800, 0x3200, 0x3000},
                                                            {0x3f00, 0x3a00, 0x3700, 0x3100}},
                                                           {{0x49aa, 0x3f80, 0x3cb0, 0x3248},
                                                            {0x4a80, 0x4780, 0x4380, 0x3c00},
                                                            {0x5080, 0x4880, 0x4000, 0x3800},
                                                            {0x4caa, 0x4aaa, 0x47aa, 0x46aa},
                                                            {0x46ff, 0x4828, 0x4100, 0x3e80},
                                                            {0x3948, 0x3600, 0x3400, 0x2800},
                                                            {0x4e00, 0x4c80, 0x4480, 0x3d80},
                                                            {0x4900, 0x4380, 0x3f00, 0x3a00},
                                                            {0x4300, 0x3fa8, 0x3a00, 0x3400},
                                                            {0x4480, 0x4100, 0x3e00, 0x3880}},
                                                           {{0x5fff, 0x5b00, 0x5a60, 0x5690},
                                                            {0x5000, 0x4d00, 0x4a00, 0x4000},
                                                            {0x4d00, 0x4700, 0x4000, 0x3800},
                                                            {0x4600, 0x4400, 0x4200, 0x4000},
                                                            {0x4700, 0x4450, 0x4000, 0x3d00},
                                                            {0x4650, 0x4400, 0x4300, 0x3700},
                                                            {0x5d00, 0x5c00, 0x5800, 0x5300},
                                                            {0x5300, 0x4e00, 0x4900, 0x4300},
                                                            {0x4900, 0x4750, 0x4200, 0x3800},
                                                            {0x4a00, 0x4800, 0x4500, 0x4000}}};
uint32_t SlipRacePlayer_trackBranch;
SlipView3DVec32 SlipRacePlayer_trackPosition;
SlipView3DVec32 SlipRacePlayer_roadPosition;
SlipView3DMatrix SlipRacePlayer_roadMatrix;
int32_t SlipRacePlayer_roadX;
int32_t SlipRacePlayer_roadY;
uint32_t SlipRacePlayer_distanceLimit;
SlipView3DVec32 SlipRacePlayer_distancePosition;
uint32_t SlipRacePlayer_distanceAccum;
uint32_t SlipRacePlayer_curveAccum;
SlipView3DVec32 SlipRacePlayer_waypointPrevious;
SlipView3DVec32 SlipRacePlayer_waypointCurrent;
SlipView3DVec32 SlipRacePlayer_waypointNext;
SlipView3DVec32 SlipRacePlayer_waypointDelta;
int32_t SlipRacePlayer_waypointDistance;
int32_t SlipRacePlayer_roadDistance;
const SlipRacePlayerTuningRecord *SlipRacePlayer_aiProfile;
int32_t SlipRacePlayer_rivalDistance;
int32_t SlipRacePlayer_raceDistance;
uint16_t SlipRacePlayer_rivalObject;
uint16_t SlipRacePlayer_farNeighbourCandidate;
uint16_t SlipRacePlayer_middleNeighbourCandidate;
int32_t SlipRacePlayer_maximumSpeed;
int32_t SlipRacePlayer_lookAhead;
int16_t SlipRacePlayer_avoidanceAxes[6];
uint16_t SlipRacePlayer_accelerate;
SlipView3DMatrix SlipRacePlayer_steeringMatrix;
uint16_t SlipRacePlayer_targetObject;
uint32_t SlipRacePlayer_targetDistance;
uint16_t SlipRacePlayer_trackStateEnabled;
uint16_t SlipRacePlayer_minimumPosition;
uint16_t SlipRacePlayer_previousMinimumPosition;
uint32_t SlipRacePlayer_nearNeighbourDistance;
uint32_t SlipRacePlayer_middleNeighbourDistance;
uint32_t SlipRacePlayer_farNeighbourDistance;
uint32_t SlipRacePlayer_neighbourCandidateDistance;
uint16_t SlipRacePlayer_nearNeighbourObject;
uint16_t SlipRacePlayer_middleNeighbourObject;
uint16_t SlipRacePlayer_farNeighbourObject;
uint16_t SlipRacePlayer_neighbourDirectionX;
uint16_t SlipRacePlayer_neighbourDirectionY;
uint16_t SlipRacePlayer_neighbourDirectionZ;
uint16_t SlipRacePlayer_neighbourNormalX;
uint16_t SlipRacePlayer_neighbourNormalY;
uint16_t SlipRacePlayer_neighbourNormalZ;
uint32_t SlipRacePlayer_neighbourMode;
uint32_t SlipRacePlayer_neighbourRadius;
uint32_t SlipRacePlayer_neighbourNearLimit;
uint32_t SlipRacePlayer_neighbourFarLimit;
SlipView3DVec32 SlipRacePlayer_neighbourPosition;
SlipView3DVec32 SlipRacePlayer_neighbourCandidateRoad;
uint32_t SlipRacePlayer_neighbourCurrentSlot;
SlipView3DVec32 SlipRacePlayer_neighbourCurrentRoad;

static void SlipRacePlayer_InitializeImpactRecovery(SlipRacePlayerHostBindings *context, uint32_t initialSpeed,
                                                    uint16_t baseDuration, uint32_t turnRate, uint16_t objectOffset);
int32_t SlipConfig_damageOverride;
uint32_t SlipConfig_damageEnabled = 1;
uint16_t SlipRacePlayer_damageSourceObject;
uint32_t SlipRacePlayer_damageSourceFlags;
uint16_t SlipRacePlayer_demoAiEnabled;
int32_t SlipRacePlayer_gamePenalty[11] = {0x0003b920, 0, 0, 0, 0x00002000, 0x00002000, 0x00001000, 0x00001800, 0, 0, 0};
uint16_t SlipRacePlayer_playerOneObject;
uint16_t SlipRacePlayer_track;
uint32_t SlipRacePlayer_positionBoostTimer;
uint16_t SlipRacePlayer_lapCount = 6;
uint32_t SlipRacePlayer_demoMode;
uint32_t SlipRacePlayer_flybyMode;
SlipRacePlayerControl SlipRacePlayer_playerTwoControls;
SlipRacePlayerControl SlipRacePlayer_thirdControls;
uint32_t SlipRacePlayer_refuelSection;
uint32_t SlipRacePlayer_aiControlsSuppressed;
uint16_t SlipRacePlayer_playerTwoObject;
uint16_t SlipRacePlayer_thirdObject;
uint16_t SlipRacePlayer_startCountdown;

static const int32_t SlipRacePlayer_positionBoost[11] = {0x0000, 0x3000, 0x2c00, 0x2800, 0x2000, 0x1800,
                                                         0x1000, 0x0800, 0x0400, 0x0200, 0x0100};

const int32_t SlipRacePlayer_aiBaseSpeed[11] = {0x00000100, 0x000345e4, 0x00029e50, 0x00029e50, 0x000345e4, 0x00029e50,
                                                0x000345e4, 0x00037dc0, 0x00029e50, 0x00029e50, 0x000345e4};

const int32_t SlipRacePlayer_aiSpeedScale[11] = {0x000345e4, 0x0002ba3e, 0x000361d2, 0x000361d2, 0x0002ba3e, 0x000361d2,
                                                 0x0002ba3e, 0x00028262, 0x000361d2, 0x000361d2, 0x0002ba3e};

typedef char SlipRacePlayerTuningRecordSize[sizeof(SlipRacePlayerTuningRecord) == 0x1cu ? 1 : -1];

static const SlipRacePlayerTuningRecord SlipRacePlayer_tuningData[10] = {
    {0x1174c, 0x08ba6, 0x1f6bc, 0x46b27, 0x4ccc, 0x4000, 0x3b920},
    {0x0fb5e, 0x0999d, 0x204b3, 0x4791e, 0x4ccc, 0x4000, 0x3b920},
    {0x12ad9, 0x0999d, 0x1f6bc, 0x4579a, 0x4ccc, 0x4000, 0x3b920},
    {0x1174c, 0x08ba6, 0x1f6bc, 0x45ffb, 0x4ccc, 0x4000, 0x3b920},
    {0x1174c, 0x08ba6, 0x1f6bc, 0x462c6, 0x4ccc, 0x4000, 0x3b920},
    {0x12543, 0x08ba6, 0x1f6bc, 0x45a65, 0x4ccc, 0x4000, 0x3b920},
    {0x1333a, 0x07daf, 0x1fc52, 0x46b27, 0x4ccc, 0x4000, 0x3b920},
    {0x1174c, 0x08ba6, 0x1f6bc, 0x4791e, 0x4ccc, 0x4000, 0x3b920},
    {0x14f28, 0x08ba6, 0x1e8c5, 0x44f39, 0x4ccc, 0x4000, 0x3b920},
    {0x16b16, 0x0a794, 0x1dace, 0x4685c, 0x4ccc, 0x4000, 0x3b920}};

const SlipRacePlayerTuningRecord *const SlipRacePlayer_tuningRecords[11] = {NULL,
                                                                            &SlipRacePlayer_tuningData[0],
                                                                            &SlipRacePlayer_tuningData[1],
                                                                            &SlipRacePlayer_tuningData[2],
                                                                            &SlipRacePlayer_tuningData[3],
                                                                            &SlipRacePlayer_tuningData[4],
                                                                            &SlipRacePlayer_tuningData[5],
                                                                            &SlipRacePlayer_tuningData[6],
                                                                            &SlipRacePlayer_tuningData[7],
                                                                            &SlipRacePlayer_tuningData[8],
                                                                            &SlipRacePlayer_tuningData[9]};

typedef struct SlipRacePowerupRecord {
	char powerupName[0x18];
	int32_t price;
	int32_t speedScaleQ14;
	int32_t chargeDrainScaleQ14;
} SlipRacePowerupRecord;

static const SlipRacePowerupRecord SlipRacePowerup_records[5] = {{"Delphine Injection", 480, 0x4666, 0x1000},
                                                                 {"Corolis Dynamic", 720, 0x4999, 0x0c00},
                                                                 {"Dual Derwent", 960, 0x4ccc, 0x0a00},
                                                                 {"Cleric Quinn", 1450, 0x5000, 0x0800},
                                                                 {"Tech Tech 301", 2010, 0x5333, 0x0400}};

typedef char SlipRacePowerupRecordSize[(sizeof(SlipRacePowerupRecord) == 0x24) ? 1 : -1];

static void SlipRacePlayer_QueueLaserLine(SlipView3DVec32 start, SlipView3DVec32 end, uint32_t material) {
	SlipTrackWorld_beams.built = 0;
	if (SlipTrackWorld_beams.queueCount == 0x30 || SlipRacePlayer_hostContext == NULL)
		return;
	SlipRacePlayerHostBindings *const context = SlipRacePlayer_hostContext;
	SlipTrackWorld_beams.queue[SlipTrackWorld_beams.queueCount] = (SlipTrackBeamRequest){start, end, material};
	SlipTrackWorldRecordSearch search;
	if (!SlipTrackWorld_RecordSearch(context->trdBase, context->trackDataSize, context->componentBase,
	                                 context->componentBaseBytes, context->trackTable, context->trackTableBytes,
	                                 context->trackDataOffset, 0, start.x, start.y, start.z, &search))
		return;
	if (search.selectedRecordAddress != 0)
		++SlipTrackWorld_beams.queueCount;
}

SlipView3DVec32 SlipRacePlayer_WeaponPosition(uint16_t shooterObject, uint32_t weaponSide) {
	SlipArticSlotPosition position;
	uint32_t slotTag;
	SlipRacePlayerHostBindings *const context = SlipRacePlayer_hostContext;

	if (weaponSide == 0) {
		slotTag = 0x77656170u; /* "weap" */
	} else if (weaponSide == 1) {
		slotTag = 0x6c617372u; /* "lasr" */
	} else {
		slotTag = 0x6c61736cu; /* "lasl" */
	}
	if (context == NULL ||
	    !SlipArticSlot_WorldPosition(0x6d61696eu, slotTag, shooterObject, context->objectTable,
	                                 context->objectTableBytes, context->articSlotPool, context->articSlotPoolBytes,
	                                 context->articSlotPoolOffset, context->articData, context->articDataBytes,
	                                 context->articDataOffset, context->maths, &position) ||
	    position.lookupFailed) {
		return (SlipView3DVec32){0, 0, 0};
	}
	return (SlipView3DVec32){(int32_t)position.positionX, (int32_t)position.positionY, (int32_t)position.positionZ};
}

static uint16_t SlipRacePlayer_shooter;
static uint16_t SlipRacePlayer_target;

static void SlipRacePlayer_CreateBlasterProjectile(uint16_t shooterObject, uint32_t weaponSide) {
	SlipRacePlayerHostBindings *const context = SlipRacePlayer_hostContext;
	SlipView3DVec32 muzzlePosition;
	SlipView3DMatrix shooterMatrix;
	SlipObjectMatrixCopy matrixCopy;
	SlipObjectSlotFill fill;
	SlipObjectActorHandleWriteResult setSlot;
	SlipRacePlayerProjectileState *projectileState;
	uint16_t projectileObject;

	if (context == NULL) {
		return;
	}
	muzzlePosition = SlipRacePlayer_WeaponPosition(shooterObject, weaponSide);
	if (!SlipObject_MatrixCopy(context->objectTable, context->objectTableBytes, shooterObject, &shooterMatrix,
	                           &matrixCopy) ||
	    !SlipObject_SlotFill(&shooterMatrix, (uint32_t)muzzlePosition.x, (uint32_t)muzzlePosition.y,
	                         (uint32_t)muzzlePosition.z, NULL, 0, SlipRacePlayer_ProjectileEvent, &fill) ||
	    fill.carryOut) {

		return;
	}
	projectileObject = (uint16_t)fill.objectOffset;
	if (!SlipObject_SetActorHandle(projectileObject, 2u, &setSlot)) {
		SlipObject_Free(projectileObject, 0, 0, 0, 0, 0, 0);
		return;
	}
	projectileState = (SlipRacePlayerProjectileState *)(void *)SlipObject_PrivateState(projectileObject);
	if (projectileState == NULL) {
		SlipObject_Free(projectileObject, 0, 0, 0, 0, 0, 0);
		return;
	}
	projectileState->weaponIndex = 0;
	projectileState->remainingTime = 0x1388;
	projectileState->shooterObject = SlipRacePlayer_shooter;
	projectileState->targetObject = SlipRacePlayer_target;
	if (SlipRacePlayer_target != 0) {
		SlipObjectPosition targetPosition;
		SlipObjectPosition projectilePosition;
		SlipView3DNormalizeVector3D direction;

		if (SlipObject_Position(context->objectTable, context->objectTableBytes, SlipRacePlayer_target,
		                        &targetPosition) &&
		    SlipObject_Position(context->objectTable, context->objectTableBytes, projectileObject,
		                        &projectilePosition) &&
		    SlipView3D_NormalizeVector3D(targetPosition.positionX - projectilePosition.positionX,
		                                 targetPosition.positionY - projectilePosition.positionY,
		                                 targetPosition.positionZ - projectilePosition.positionZ, &direction)) {
			SlipObject_SetDirectionQ14(context->objectTable, projectileObject, (uint16_t)direction.unitXQ14,
			                           (uint16_t)direction.unitYQ14, (uint16_t)direction.unitZQ14);
		}
	}
}

void SlipRacePlayer_FireSmoker(uint16_t shooter, uint16_t target) {
	const SlipRacePlayerHostBindings *const context = SlipRacePlayer_hostContext;
	SlipArticSlotPosition smokePosition;
	(void)target;
	if (!SlipArticSlot_Position(0x6d61696eu, 0x736d6f6bu, shooter, context->objectTable, context->objectTableBytes,
	                            context->articSlotPool, context->articSlotPoolBytes, context->articSlotPoolOffset,
	                            context->articData, context->articDataBytes, context->articDataOffset, context->maths,
	                            &smokePosition) ||
	    smokePosition.lookupFailed)
		SlipRuntime_Fatal("Weapon Smoker: Missing reference point.");
	SlipRaceEffects_EmitSmoke(shooter,
	                          (SlipView3DVec32){(int32_t)smokePosition.positionX, (int32_t)smokePosition.positionY,
	                                            (int32_t)smokePosition.positionZ},
	                          4000, &SlipRaceEffects_weaponSmoke);
	SlipSoundEffects_Queue(context->soundEffects, smokePosition.positionX, smokePosition.positionY,
	                       smokePosition.positionZ, 5u, shooter, 1u);
}

void SlipRacePlayer_FireBlaster(uint16_t shooterObject, uint16_t targetObject) {
	SlipRacePlayer_shooter = shooterObject;
	SlipRacePlayer_target = targetObject;
	SlipRacePlayer_CreateBlasterProjectile(shooterObject, 1u);
	SlipRacePlayer_CreateBlasterProjectile(shooterObject, 2u);

	SlipSoundEffects_Queue(SlipRacePlayer_hostContext->soundEffects, 0, 0, 0, 4u, SlipRacePlayer_shooter, 1u);
}

static uint32_t SlipRacePlayer_ProjectileEvent(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue,
                                               uint32_t eventFlags, uint16_t objectOffset, uintptr_t dispatchData,
                                               uint32_t dispatchFrame) {
	SlipRacePlayerHostBindings *const context = SlipRacePlayer_hostContext;
	SlipRacePlayerProjectileState *const projectileState =
	    (SlipRacePlayerProjectileState *)(void *)SlipObject_PrivateState(objectOffset);

	(void)eventPayload;
	(void)eventValue;
	(void)eventFlags;
	(void)dispatchData;
	(void)dispatchFrame;
	if (context == NULL || projectileState == NULL) {
		return 1;
	}

	switch ((SlipObjectEvent)(uint16_t)eventCode) {
	case SLIP_OBJECT_EVENT_INITIALIZE: {
		SlipObjectPosition objectPosition;

		if (SlipObject_Position(context->objectTable, context->objectTableBytes, objectOffset, &objectPosition)) {
			projectileState->position =
			    (SlipView3DVec32){(int32_t)objectPosition.positionX, (int32_t)objectPosition.positionY,
			                      (int32_t)objectPosition.positionZ};
		}
		return 0;
	}
	case SLIP_OBJECT_EVENT_HANDLE_ACTION: {
		SlipObjectPosition objectPosition;
		SlipObjectDirection direction;
		SlipView3DVec32 firstPoint;
		SlipView3DVec32 movement;
		SlipView3DVec32 requestedPoint;
		SlipRaceCollisionSegmentHit collision;
		uint32_t movementDistance;
		bool trackHit;
		uint32_t laserMaterial;

		if (!SlipObject_Position(context->objectTable, context->objectTableBytes, objectOffset, &objectPosition)) {
			return 0;
		}
		firstPoint = (SlipView3DVec32){(int32_t)objectPosition.positionX, (int32_t)objectPosition.positionY,
		                               (int32_t)objectPosition.positionZ};
		movementDistance = (uint32_t)(((uint64_t)0x00077240u * SlipFrameTimer_Step()) >> 14);
		direction = SlipObject_Direction(context->objectTable, objectOffset);
		movement = SlipView3D_ScaleVector((int16_t)direction.directionXQ14, (int16_t)direction.directionYQ14,
		                                  (int16_t)direction.directionZQ14, (int32_t)movementDistance);

		requestedPoint = (SlipView3DVec32){(int32_t)((uint32_t)firstPoint.x + (uint32_t)movement.x),
		                                   (int32_t)((uint32_t)firstPoint.y + (uint32_t)movement.y),
		                                   (int32_t)((uint32_t)firstPoint.z + (uint32_t)movement.z)};
		projectileState->position = requestedPoint;
		collision =
		    SlipRaceCollision_QuerySegment(projectileState->shooterObject, firstPoint, requestedPoint, context->trdBase,
		                                   context->trackDataSize, context->trackDataOffset, context->componentBase,
		                                   context->componentBaseBytes, context->trackTable, context->trackTableBytes);
		trackHit = collision.point.x != requestedPoint.x || collision.point.y != requestedPoint.y ||
		           collision.point.z != requestedPoint.z;
		laserMaterial = 0x00fd00feu;
		if (collision.objectHandle != 0 || trackHit) {
			laserMaterial |= 0x80000000u;
		}
		SlipRacePlayer_QueueLaserLine(firstPoint, collision.point, laserMaterial);
		if (collision.objectHandle != 0) {
			(void)SlipObject_DispatchEvent(collision.objectHandle, SLIP_OBJECT_EVENT_APPLY_DAMAGE, 0,
			                               projectileState->shooterObject, 0, 0, 0);
			SlipObject_Free(objectOffset, 0, 0, 0, 0, 0, 0);
		} else if (trackHit) {
			SlipObject_Free(objectOffset, 0, 0, 0, 0, 0, 0);
		}
		return 0;
	}
	case SLIP_OBJECT_EVENT_UPDATE: {
		SlipObjectSetPosition setPosition;
		SlipFrameTimerValues timer = SlipFrameTimer_Values();

		projectileState->remainingTime = (int32_t)((uint32_t)projectileState->remainingTime - timer.deltaMilliseconds);
		if (projectileState->remainingTime < 0) {
			SlipObject_Free(objectOffset, 0, 0, 0, 0, 0, 0);
			return 0;
		}
		(void)SlipObject_SetPosition(context->objectTable, context->objectTableBytes, objectOffset,
		                             (uint32_t)projectileState->position.x, (uint32_t)projectileState->position.y,
		                             (uint32_t)projectileState->position.z, &setPosition);
		if (projectileState->targetObject != 0) {
			SlipObjectPosition targetPosition;
			SlipView3DNormalizeVector3D desiredDirection;
			SlipObjectDirection currentDirection;
			SlipView3DDotProductQ14 dot;

			if (SlipObject_Position(context->objectTable, context->objectTableBytes, projectileState->targetObject,
			                        &targetPosition) &&
			    SlipView3D_NormalizeVector3D(targetPosition.positionX - (uint32_t)projectileState->position.x,
			                                 targetPosition.positionY - (uint32_t)projectileState->position.y,
			                                 targetPosition.positionZ - (uint32_t)projectileState->position.z,
			                                 &desiredDirection)) {
				currentDirection = SlipObject_Direction(context->objectTable, objectOffset);
				SlipView3D_DotProductQ14((uint16_t)desiredDirection.unitXQ14, (uint16_t)desiredDirection.unitYQ14,
				                         (uint16_t)desiredDirection.unitZQ14, currentDirection.directionXQ14,
				                         currentDirection.directionYQ14, currentDirection.directionZQ14, &dot);
				if ((int16_t)(uint16_t)dot.dotProductQ14 > 0x3400) {
					SlipObject_SetDirectionQ14(context->objectTable, objectOffset, (uint16_t)desiredDirection.unitXQ14,
					                           (uint16_t)desiredDirection.unitYQ14,
					                           (uint16_t)desiredDirection.unitZQ14);
				}
			}
		}
		return 0;
	}
	default:
		return 1;
	}
}

uint32_t SlipRacePlayer_AmblerHyperNeuroEvent(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue,
                                              uint32_t eventFlags, uint16_t object, uintptr_t dispatchData,
                                              uint32_t dispatchFrame) {
	enum {
		PROJECTILE_MAXIMUM_SPEED = 0x9d1ac,
		PROJECTILE_ACCELERATION = 0x22e98,
		PROJECTILE_TURN_RATE = 0x4000,
		FIXED_POINT_FRACTION_BITS = 14,
		IMPACT_SMOKE_LIFETIME = 1000,
		MATRIX_FIRST_ROW_Y = 1
	};

	SlipRacePlayerHostBindings *const context = SlipRacePlayer_hostContext;
	(void)eventPayload;
	(void)eventValue;
	(void)eventFlags;
	(void)dispatchData;
	(void)dispatchFrame;
	switch ((SlipObjectEvent)(uint16_t)eventCode) {
	case SLIP_OBJECT_EVENT_UPDATE: {
		int32_t speed = SlipObject_Speed(context->objectTable, object);
		if (speed != PROJECTILE_MAXIMUM_SPEED) {
			const uint32_t step = SlipFrameTimer_Step();
			const uint32_t increment =
			    (uint32_t)(((uint64_t)PROJECTILE_ACCELERATION * step) >> FIXED_POINT_FRACTION_BITS);
			speed = (int32_t)((uint32_t)speed + increment);
			if (speed > PROJECTILE_MAXIMUM_SPEED)
				speed = PROJECTILE_MAXIMUM_SPEED;
			SlipObject_SetSpeed(context->objectTable, object, speed);
		}
		SlipRacePlayerProjectileState *const state =
		    (SlipRacePlayerProjectileState *)(void *)SlipObject_PrivateState(object);
		if (state->targetObject != 0) {
			SlipObjectPosition targetPosition, position;
			SlipView3DMatrix matrix;
			SlipObjectMatrixCopy copied;
			SlipObjectMatrixInstall installed;
			SlipView3DNormalizeVector3D direction;
			(void)SlipObject_Position(context->objectTable, context->objectTableBytes, state->targetObject,
			                          &targetPosition);
			(void)SlipObject_Position(context->objectTable, context->objectTableBytes, object, &position);
			(void)SlipObject_MatrixCopy(context->objectTable, context->objectTableBytes, object, &matrix, &copied);

			(void)SlipView3D_NormalizeVector3D(targetPosition.positionX - position.positionX,
			                                   targetPosition.positionY - position.positionY,
			                                   targetPosition.positionZ - position.positionZ, &direction);
			const uint32_t step = SlipFrameTimer_Step();
			const uint32_t turn = (uint32_t)(((uint64_t)PROJECTILE_TURN_RATE * step) >> FIXED_POINT_FRACTION_BITS);
			(void)SlipView3D_RotateForwardTowards(context->maths, &matrix, (int16_t)direction.unitXQ14,
			                                      (int16_t)direction.unitYQ14, (int16_t)direction.unitZQ14, turn);
			matrix.m[MATRIX_FIRST_ROW_Y] = 0;
			SlipView3D_OrthonormalizeForwardBasis(&matrix);
			(void)SlipObject_MatrixInstall(context->objectTable, context->objectTableBytes, object, &matrix,
			                               &installed);
			SlipObject_CopyMatrixForwardToDirection(context->objectTable, object);
		}
		return 0;
	}
	case SLIP_OBJECT_EVENT_COLLISION_STOP:
		(void)SlipRaceCollision_RemoveBody(object);
		SlipObject_Free(object, 0, 0, 0, 0, 0, 0);
		return 0;
	case SLIP_OBJECT_EVENT_COLLISION_BOUNCE: {
		SlipObjectPosition position;
		(void)SlipObject_Position(context->objectTable, context->objectTableBytes, object, &position);

		SlipRaceEffects_EmitWorldSmoke(
		    (SlipView3DVec32){(int32_t)position.positionX, (int32_t)position.positionY, (int32_t)position.positionZ},
		    IMPACT_SMOKE_LIFETIME, &SlipRaceEffects_damageSmoke);
		SlipObject_Free(object, 0, 0, 0, 0, 0, 0);
		return 0;
	}
	default:
		return 1;
	}
}

uint32_t SlipRacePlayer_GuidedProjectileEvent(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue,
                                              uint32_t eventFlags, uint16_t object, uintptr_t dispatchData,
                                              uint32_t dispatchFrame) {
	enum {
		PROJECTILE_MAXIMUM_SPEED = 0x9d1ac,
		PROJECTILE_ACCELERATION = 0x22e98,
		PROJECTILE_TURN_RATE = 0x4000,
		FIXED_POINT_FRACTION_BITS = 14,
		IMPACT_SMOKE_LIFETIME = 2000,
		MATRIX_FIRST_ROW_Y = 1
	};

	SlipRacePlayerHostBindings *const context = SlipRacePlayer_hostContext;
	(void)eventPayload;
	(void)eventValue;
	(void)eventFlags;
	(void)dispatchData;
	(void)dispatchFrame;
	switch ((SlipObjectEvent)(uint16_t)eventCode) {
	case SLIP_OBJECT_EVENT_UPDATE: {
		int32_t speed = SlipObject_Speed(context->objectTable, object);
		if (speed != PROJECTILE_MAXIMUM_SPEED) {
			const uint32_t step = SlipFrameTimer_Step();
			const uint32_t increment =
			    (uint32_t)(((uint64_t)PROJECTILE_ACCELERATION * step) >> FIXED_POINT_FRACTION_BITS);
			speed = (int32_t)((uint32_t)speed + increment);
			if (speed > PROJECTILE_MAXIMUM_SPEED)
				speed = PROJECTILE_MAXIMUM_SPEED;
			SlipObject_SetSpeed(context->objectTable, object, speed);
		}
		SlipRacePlayerProjectileState *const state =
		    (SlipRacePlayerProjectileState *)(void *)SlipObject_PrivateState(object);
		if (state->targetObject != 0) {
			SlipObjectPosition targetPosition, position;
			SlipView3DMatrix matrix;
			SlipObjectMatrixCopy copied;
			SlipObjectMatrixInstall installed;
			SlipView3DNormalizeVector3D direction;
			(void)SlipObject_Position(context->objectTable, context->objectTableBytes, state->targetObject,
			                          &targetPosition);
			(void)SlipObject_Position(context->objectTable, context->objectTableBytes, object, &position);
			(void)SlipObject_MatrixCopy(context->objectTable, context->objectTableBytes, object, &matrix, &copied);

			(void)SlipView3D_NormalizeVector3D(targetPosition.positionX - position.positionX,
			                                   targetPosition.positionY - position.positionY,
			                                   targetPosition.positionZ - position.positionZ, &direction);
			const uint32_t step = SlipFrameTimer_Step();
			const uint32_t turn = (uint32_t)(((uint64_t)PROJECTILE_TURN_RATE * step) >> FIXED_POINT_FRACTION_BITS);
			(void)SlipView3D_RotateForwardTowards(context->maths, &matrix, (int16_t)direction.unitXQ14,
			                                      (int16_t)direction.unitYQ14, (int16_t)direction.unitZQ14, turn);
			matrix.m[MATRIX_FIRST_ROW_Y] = 0;
			SlipView3D_OrthonormalizeForwardBasis(&matrix);
			(void)SlipObject_MatrixInstall(context->objectTable, context->objectTableBytes, object, &matrix,
			                               &installed);
			SlipObject_CopyMatrixForwardToDirection(context->objectTable, object);
		}
		return 0;
	}
	case SLIP_OBJECT_EVENT_COLLISION_STOP:
		(void)SlipRaceCollision_RemoveBody(object);
		SlipObject_Free(object, 0, 0, 0, 0, 0, 0);
		return 0;
	case SLIP_OBJECT_EVENT_COLLISION_BOUNCE: {
		SlipObjectPosition position;
		(void)SlipObject_Position(context->objectTable, context->objectTableBytes, object, &position);

		SlipRaceEffects_EmitWorldSmoke(
		    (SlipView3DVec32){(int32_t)position.positionX, (int32_t)position.positionY, (int32_t)position.positionZ},
		    IMPACT_SMOKE_LIFETIME, &SlipRaceEffects_damageSmoke);
		SlipObject_Free(object, 0, 0, 0, 0, 0, 0);
		return 0;
	}
	default:
		return 1;
	}
}

uint32_t SlipRacePlayer_ScramblerEvent(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue,
                                       uint32_t eventFlags, uint16_t object, uintptr_t dispatchData,
                                       uint32_t dispatchFrame) {
	enum {
		PROJECTILE_MAXIMUM_SPEED = 0x9d1ac,
		PROJECTILE_ACCELERATION = 0x22e98,
		PROJECTILE_TURN_RATE = 0x4000,
		FIXED_POINT_FRACTION_BITS = 14,
		IMPACT_SMOKE_LIFETIME = 2000,
		MATRIX_FIRST_ROW_Y = 1,
		ROAD_FOLLOW_DISTANCE = 0x2fa80,
		COLLISION_EFFECT_RADIUS = 0x2620,
		COLLISION_EFFECT_DURATION = 0xbb8
	};

	SlipRacePlayerHostBindings *const context = SlipRacePlayer_hostContext;
	(void)eventPayload;
	(void)eventValue;
	(void)eventFlags;
	(void)dispatchData;
	(void)dispatchFrame;
	switch ((SlipObjectEvent)(uint16_t)eventCode) {
	case SLIP_OBJECT_EVENT_UPDATE: {
		int32_t speed = SlipObject_Speed(context->objectTable, object);
		if (speed != PROJECTILE_MAXIMUM_SPEED) {
			const uint32_t step = SlipFrameTimer_Step();
			const uint32_t increment =
			    (uint32_t)(((uint64_t)PROJECTILE_ACCELERATION * step) >> FIXED_POINT_FRACTION_BITS);
			speed = (int32_t)((uint32_t)speed + increment);
			if (speed > PROJECTILE_MAXIMUM_SPEED)
				speed = PROJECTILE_MAXIMUM_SPEED;
			SlipObject_SetSpeed(context->objectTable, object, speed);
		}
		SlipRacePlayerProjectileState *const state =
		    (SlipRacePlayerProjectileState *)(void *)SlipObject_PrivateState(object);
		if (state->targetObject != 0) {
			SlipObjectPosition targetPosition, position;
			SlipView3DMatrix matrix;
			SlipObjectMatrixCopy copied;
			SlipObjectMatrixInstall installed;
			SlipView3DNormalizeVector3D direction;
			(void)SlipObject_Position(context->objectTable, context->objectTableBytes, state->targetObject,
			                          &targetPosition);
			(void)SlipObject_Position(context->objectTable, context->objectTableBytes, object, &position);
			(void)SlipObject_MatrixCopy(context->objectTable, context->objectTableBytes, object, &matrix, &copied);

			(void)SlipView3D_NormalizeVector3D(targetPosition.positionX - position.positionX,
			                                   targetPosition.positionY - position.positionY,
			                                   targetPosition.positionZ - position.positionZ, &direction);

			if ((int32_t)direction.vectorLength > ROAD_FOLLOW_DISTANCE) {
				SlipRaceDrone_Move(object, 1, 0);
				return 0;
			}
			const uint32_t step = SlipFrameTimer_Step();
			const uint32_t turn = (uint32_t)(((uint64_t)PROJECTILE_TURN_RATE * step) >> FIXED_POINT_FRACTION_BITS);
			(void)SlipView3D_RotateForwardTowards(context->maths, &matrix, (int16_t)direction.unitXQ14,
			                                      (int16_t)direction.unitYQ14, (int16_t)direction.unitZQ14, turn);
			matrix.m[MATRIX_FIRST_ROW_Y] = 0;
			SlipView3D_OrthonormalizeForwardBasis(&matrix);
			(void)SlipObject_MatrixInstall(context->objectTable, context->objectTableBytes, object, &matrix,
			                               &installed);
			SlipObject_CopyMatrixForwardToDirection(context->objectTable, object);
		}
		return 0;
	}
	case SLIP_OBJECT_EVENT_COLLISION_STOP: {
		SlipObjectPosition position;
		(void)SlipObject_Position(context->objectTable, context->objectTableBytes, object, &position);
		SlipRaceEffects_Collision(
		    (SlipView3DVec32){(int32_t)position.positionX, (int32_t)position.positionY, (int32_t)position.positionZ},
		    COLLISION_EFFECT_RADIUS, COLLISION_EFFECT_DURATION);
		(void)SlipRaceCollision_RemoveBody(object);
		SlipObject_Free(object, 0, 0, 0, 0, 0, 0);
		return 0;
	}
	case SLIP_OBJECT_EVENT_COLLISION_BOUNCE: {
		SlipObjectPosition position;
		(void)SlipObject_Position(context->objectTable, context->objectTableBytes, object, &position);

		SlipRaceEffects_EmitWorldSmoke(
		    (SlipView3DVec32){(int32_t)position.positionX, (int32_t)position.positionY, (int32_t)position.positionZ},
		    IMPACT_SMOKE_LIFETIME, &SlipRaceEffects_damageSmoke);
		SlipObject_Free(object, 0, 0, 0, 0, 0, 0);
		return 0;
	}
	default:
		return 1;
	}
}

uint32_t SlipRacePlayer_DisrupterEvent(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue,
                                       uint32_t eventFlags, uint16_t objectOffset, uintptr_t dispatchData,
                                       uint32_t dispatchFrame) {
	SlipRacePlayerHostBindings *const context = SlipRacePlayer_hostContext;
	SlipRacePlayerProjectileState *const projectileState =
	    (SlipRacePlayerProjectileState *)(void *)SlipObject_PrivateState(objectOffset);

	(void)eventPayload;
	(void)eventValue;
	(void)eventFlags;
	(void)dispatchData;
	(void)dispatchFrame;
	if (context == NULL || projectileState == NULL) {
		return 1;
	}

	switch ((SlipObjectEvent)(uint16_t)eventCode) {
	case SLIP_OBJECT_EVENT_INITIALIZE: {
		SlipObjectPosition objectPosition;

		if (SlipObject_Position(context->objectTable, context->objectTableBytes, objectOffset, &objectPosition)) {
			projectileState->position =
			    (SlipView3DVec32){(int32_t)objectPosition.positionX, (int32_t)objectPosition.positionY,
			                      (int32_t)objectPosition.positionZ};
		}
		return 0;
	}
	case SLIP_OBJECT_EVENT_HANDLE_ACTION: {
		SlipObjectPosition objectPosition;
		SlipObjectDirection direction;
		SlipView3DVec32 firstPoint;
		SlipView3DVec32 movement;
		SlipView3DVec32 requestedPoint;
		SlipRaceCollisionSegmentHit collision;
		uint32_t movementDistance;
		bool trackHit;
		uint32_t laserMaterial;

		if (!SlipObject_Position(context->objectTable, context->objectTableBytes, objectOffset, &objectPosition)) {
			return 0;
		}
		firstPoint = (SlipView3DVec32){(int32_t)objectPosition.positionX, (int32_t)objectPosition.positionY,
		                               (int32_t)objectPosition.positionZ};
		movementDistance = (uint32_t)(((uint64_t)0x00077240u * SlipFrameTimer_Step()) >> 14);
		direction = SlipObject_Direction(context->objectTable, objectOffset);
		movement = SlipView3D_ScaleVector((int16_t)direction.directionXQ14, (int16_t)direction.directionYQ14,
		                                  (int16_t)direction.directionZQ14, (int32_t)movementDistance);

		requestedPoint = (SlipView3DVec32){(int32_t)((uint32_t)firstPoint.x + (uint32_t)movement.x),
		                                   (int32_t)((uint32_t)firstPoint.y + (uint32_t)movement.y),
		                                   (int32_t)((uint32_t)firstPoint.z + (uint32_t)movement.z)};
		projectileState->position = requestedPoint;
		collision =
		    SlipRaceCollision_QuerySegment(projectileState->shooterObject, firstPoint, requestedPoint, context->trdBase,
		                                   context->trackDataSize, context->trackDataOffset, context->componentBase,
		                                   context->componentBaseBytes, context->trackTable, context->trackTableBytes);
		trackHit = collision.point.x != requestedPoint.x || collision.point.y != requestedPoint.y ||
		           collision.point.z != requestedPoint.z;
		laserMaterial = 0x0040004fu;
		if (collision.objectHandle != 0 || trackHit) {
			laserMaterial |= 0x80000000u;
		}
		SlipRacePlayer_QueueLaserLine(firstPoint, collision.point, laserMaterial);
		if (collision.objectHandle != 0) {
			(void)SlipObject_DispatchEvent(collision.objectHandle, SLIP_OBJECT_EVENT_APPLY_DAMAGE, 1,
			                               projectileState->shooterObject, 0, 0, 0);
			SlipObject_Free(objectOffset, 0, 0, 0, 0, 0, 0);
		} else if (trackHit) {
			SlipObject_Free(objectOffset, 0, 0, 0, 0, 0, 0);
		}
		return 0;
	}
	case SLIP_OBJECT_EVENT_UPDATE: {
		SlipObjectSetPosition setPosition;
		SlipFrameTimerValues timer = SlipFrameTimer_Values();

		projectileState->remainingTime = (int32_t)((uint32_t)projectileState->remainingTime - timer.deltaMilliseconds);
		if (projectileState->remainingTime < 0) {
			SlipObject_Free(objectOffset, 0, 0, 0, 0, 0, 0);
			return 0;
		}
		(void)SlipObject_SetPosition(context->objectTable, context->objectTableBytes, objectOffset,
		                             (uint32_t)projectileState->position.x, (uint32_t)projectileState->position.y,
		                             (uint32_t)projectileState->position.z, &setPosition);
		if (projectileState->targetObject != 0) {
			SlipObjectPosition targetPosition;
			SlipView3DNormalizeVector3D desiredDirection;
			SlipObjectDirection currentDirection;
			SlipView3DDotProductQ14 dot;

			if (SlipObject_Position(context->objectTable, context->objectTableBytes, projectileState->targetObject,
			                        &targetPosition) &&
			    SlipView3D_NormalizeVector3D(targetPosition.positionX - (uint32_t)projectileState->position.x,
			                                 targetPosition.positionY - (uint32_t)projectileState->position.y,
			                                 targetPosition.positionZ - (uint32_t)projectileState->position.z,
			                                 &desiredDirection)) {
				currentDirection = SlipObject_Direction(context->objectTable, objectOffset);
				SlipView3D_DotProductQ14((uint16_t)desiredDirection.unitXQ14, (uint16_t)desiredDirection.unitYQ14,
				                         (uint16_t)desiredDirection.unitZQ14, currentDirection.directionXQ14,
				                         currentDirection.directionYQ14, currentDirection.directionZQ14, &dot);
				if ((int16_t)(uint16_t)dot.dotProductQ14 > 0x3400) {
					SlipObject_SetDirectionQ14(context->objectTable, objectOffset, (uint16_t)desiredDirection.unitXQ14,
					                           (uint16_t)desiredDirection.unitYQ14,
					                           (uint16_t)desiredDirection.unitZQ14);
				}
			}
		}
		return 0;
	}
	default:
		return 1;
	}
}

static SlipRacePlayerPrivateRecord *SlipRacePlayer_PrivateState(uint16_t objectOffset) {
	enum { objectStride = 0xaeu };

	const size_t objectIndex = (size_t)objectOffset / objectStride;
	const SlipRacePlayerHostBindings *const context = SlipRacePlayer_hostContext;

	if (context == NULL || context->playerStates == NULL || objectOffset % objectStride != 0 ||
	    objectIndex >= context->playerStateCount) {
		return NULL;
	}
	return &context->playerStates[objectIndex];
}

static SlipRaceRacerState *SlipRacePlayer_CurrentRacerFromState(const SlipRacePlayerHostBindings *context,
                                                                const SlipRacePlayerPrivateRecord *privateState) {
	const uint32_t racerOffset = privateState->racerStateOffset - context->racerRecordsOffset;
	const size_t racerIndex = racerOffset / SLIP_RACE_RACER_RECORD_BYTES;

	if (context->racerStates == NULL || racerOffset % SLIP_RACE_RACER_RECORD_BYTES != 0 ||
	    racerIndex >= context->racerStateCount) {
		return NULL;
	}
	return &context->racerStates[racerIndex];
}

typedef struct RacePlayerPositionVoiceContext {
	const SlipRacePlayerHostBindings *player;
	uint16_t objectOffset;
} RacePlayerPositionVoiceContext;

static SlipRandomState SlipRacePlayer_PositionVoiceRandomState(void *context) {
	(void)context;
	return SlipRandom_GetState();
}

static const SlipRaceRacerState *SlipRacePlayer_PositionVoiceRacer(void *context) {
	RacePlayerPositionVoiceContext *const voice = context;
	return SlipRacePlayer_CurrentRacerFromState(voice->player, SlipRacePlayer_PrivateState(voice->objectOffset));
}

static uint32_t SlipRacePlayer_PositionVoiceRandom(void *context) {
	(void)context;
	return SlipRandom_Next();
}

static void SlipRacePlayer_PositionVoicePlay(void *context, uint32_t selection) {
	RacePlayerPositionVoiceContext *const voice = context;
	SlipGameSoundState *const sound = voice->player->soundEffects->gameSound;
	SlipRaceVoiceCalls calls = SlipRaceVoiceHost_Calls(sound);
	SlipRaceVoice_Play(sound->digitalCard, selection, &calls);
}

static void SlipRacePlayer_PositionVoiceRestoreRandom(void *context, SlipRandomState saved) {
	(void)context;
	SlipRandom_SetState(saved.stateWords, (uint16_t)saved.stateTail);
}

static const SlipRaceRacerState *SlipRacePlayer_RacerFromAddress(const SlipRacePlayerHostBindings *context,
                                                                 uint32_t racerAddress) {
	const uint32_t racerOffset = racerAddress - context->racerRecordsOffset;
	const size_t racerIndex = racerOffset / SLIP_RACE_RACER_RECORD_BYTES;

	if (context->racerStates == NULL || racerOffset % SLIP_RACE_RACER_RECORD_BYTES != 0 ||
	    racerIndex >= context->racerStateCount) {
		return NULL;
	}
	return &context->racerStates[racerIndex];
}

static int32_t SlipRacePlayer_MultiplySignedQ14(int32_t lhs, int32_t rhs) {
	const int64_t product = (int64_t)lhs * (int64_t)rhs;
	return (int32_t)((uint64_t)product >> 14);
}

static int32_t SlipRacePlayer_MultiplySignedShifted(int32_t lhs, int32_t rhs, unsigned shift) {
	const int64_t product = (int64_t)lhs * (int64_t)rhs;
	return (int32_t)((uint64_t)product >> shift);
}

static int16_t SlipRacePlayer_MultiplySignedWordsHigh(int16_t lhs, int16_t rhs) {
	return (int16_t)((int32_t)lhs * (int32_t)rhs >> 16);
}

static int16_t SlipRacePlayer_MultiplySignedWordsShifted(int16_t lhs, int16_t rhs, unsigned shift) {
	return (int16_t)((uint32_t)((int32_t)lhs * (int32_t)rhs) >> shift);
}

SlipRacePlayerTrackPoint SlipRacePlayer_TrackPoint(SlipRacePlayerHostBindings *context, uint16_t objectOffset) {
	SlipRacePlayerRoadRecord road;

	if (!SlipRacePlayer_FindRoadRecord(
	        objectOffset, context->objectTable, context->objectTableBytes, context->slotListBase,
	        context->slotListBytes, context->slotListBaseOffset, context->trdBase, context->trackDataSize,
	        context->trackDataOffset, context->componentBase, context->componentBaseBytes, context->componentBaseOffset,
	        context->trackTable, context->trackTableBytes, &road) ||
	    road.notFound) {
		return (SlipRacePlayerTrackPoint){0, 0, 0, true};
	}
	return (SlipRacePlayerTrackPoint){SlipBytes_ReadLE32(road.record + 0x0cu), SlipBytes_ReadLE32(road.record + 0x10u),
	                                  SlipBytes_ReadLE32(road.record + 0x14u), false};
}

SlipRacePlayerNeighbours SlipRacePlayer_FindNeighbours(SlipRacePlayerHostBindings *context, uint32_t mode,
                                                       uint16_t objectId) {
	SlipTrackWorldSlotListSelect selectedSlot;
	SlipObjectDirection objectDirection;
	SlipObjectPosition objectPosition;
	SlipRacePlayerTrackPoint roadPoint;
	SlipView3DNormalizeVector3D roadNormal;
	SlipDraw3DApproxAbsVectorLength vectorLength;
	SlipTrackSlotRecord *currentSlot;
	uint32_t currentSlotAddress;
	uint32_t currentRoadX;
	uint32_t currentRoadY;
	uint32_t currentRoadZ;
	uint32_t objectX;
	uint32_t objectY;
	uint32_t objectZ;
	uint32_t currentRadius;
	uint32_t nearLimit;
	uint32_t farLimit;
	uint16_t nearObject = 0;
	uint16_t middleObject = 0;
	uint16_t farObject = 0;
	uint32_t nearDistance = UINT32_MAX;
	uint32_t middleDistance = UINT32_MAX;
	uint32_t farDistance = UINT32_MAX;
	uint32_t candidateSlotAddress;
	uint16_t bodyPropertyFallback;

	SlipRacePlayer_neighbourMode = mode;
	if (!SlipTrackWorld_SelectSlotListEntry(mode, context->slotListBaseOffset, context->objectTable,
	                                        context->objectTableBytes, objectId, &selectedSlot) ||
	    selectedSlot.carry) {
		SlipRuntime_Fatal("TrackSlotGetNeighbours - not a track slot");
	}
	currentSlotAddress = selectedSlot.slotAddress;
	SlipRacePlayer_neighbourCurrentSlot = currentSlotAddress;
	currentSlot = (SlipTrackSlotRecord *)(context->slotListBase + (currentSlotAddress - context->slotListBaseOffset));
	objectDirection = SlipObject_Direction(context->objectTable, objectId);
	SlipRacePlayer_neighbourDirectionX = objectDirection.directionXQ14;
	SlipRacePlayer_neighbourDirectionY = objectDirection.directionYQ14;
	SlipRacePlayer_neighbourDirectionZ = objectDirection.directionZQ14;
	SlipObject_Position(context->objectTable, context->objectTableBytes, objectId, &objectPosition);
	objectX = objectPosition.positionX;
	objectY = objectPosition.positionY;
	objectZ = objectPosition.positionZ;
	SlipRacePlayer_neighbourPosition = (SlipView3DVec32){(int32_t)objectX, (int32_t)objectY, (int32_t)objectZ};
	currentRadius = currentSlot->boundingRadius;
	SlipRacePlayer_neighbourRadius = currentRadius;
	roadPoint = SlipRacePlayer_TrackPoint(context, objectId);
	currentRoadX = roadPoint.roadPointX;
	currentRoadY = roadPoint.roadPointY;
	currentRoadZ = roadPoint.roadPointZ;
	SlipRacePlayer_neighbourCurrentRoad =
	    (SlipView3DVec32){(int32_t)currentRoadX, (int32_t)currentRoadY, (int32_t)currentRoadZ};
	SlipDraw3D_ApproxAbsVectorLength(currentRoadX - objectX, currentRoadY - objectY, currentRoadZ - objectZ,
	                                 &vectorLength);
	nearLimit = vectorLength.approximateLength - currentRadius;
	farLimit = nearLimit + currentRadius * 2u;
	SlipRacePlayer_neighbourNearLimit = nearLimit;
	SlipRacePlayer_neighbourFarLimit = farLimit;
	SlipView3D_NormalizeVector3D(currentRoadX - objectX, currentRoadY - objectY, currentRoadZ - objectZ, &roadNormal);
	SlipRacePlayer_neighbourNormalX = (uint16_t)roadNormal.unitXQ14;
	SlipRacePlayer_neighbourNormalY = (uint16_t)roadNormal.unitYQ14;
	SlipRacePlayer_neighbourNormalZ = (uint16_t)roadNormal.unitZQ14;
	bodyPropertyFallback = (uint16_t)roadNormal.unitYQ14;
	SlipRacePlayer_nearNeighbourObject = 0;
	SlipRacePlayer_middleNeighbourObject = 0;
	SlipRacePlayer_farNeighbourObject = 0;
	SlipRacePlayer_nearNeighbourDistance = UINT32_MAX;
	SlipRacePlayer_middleNeighbourDistance = UINT32_MAX;
	SlipRacePlayer_farNeighbourDistance = UINT32_MAX;

	candidateSlotAddress = ((SlipTrackSlotRecord *)(context->slotListBase +
	                                                (context->slotListSentinelOffset - context->slotListBaseOffset)))
	                           ->nextSlotAddress;
	while (candidateSlotAddress != context->slotListSentinelOffset) {
		SlipTrackSlotRecord *const candidateSlot =
		    (SlipTrackSlotRecord *)(context->slotListBase + (candidateSlotAddress - context->slotListBaseOffset));
		const uint32_t candidateObjectOffset = candidateSlot->ownerObjectOffset;
		const uint16_t candidateObjectId = (uint16_t)candidateObjectOffset;
		const uint32_t candidateSlotFlags = candidateSlot->flags;
		bool candidateEligible = mode != 0 ? (candidateSlotFlags & 0x40u) != 0 : false;

		if (!candidateEligible)
			candidateEligible = (candidateSlotFlags & 0x04u) != 0;
		if (candidateSlotAddress != currentSlotAddress && candidateEligible &&
		    ((bodyPropertyFallback = SlipRaceCollision_BodyProperty(candidateObjectId, bodyPropertyFallback)) & 2u) !=
		        0) {
			uint32_t relativeX;
			uint32_t relativeY;
			uint32_t relativeZ;
			uint32_t candidateDistance;

			SlipObject_Position(context->objectTable, context->objectTableBytes, candidateObjectId, &objectPosition);
			relativeX = objectPosition.positionX - objectX;
			relativeY = objectPosition.positionY - objectY;
			relativeZ = objectPosition.positionZ - objectZ;

			SlipDraw3D_ApproxAbsVectorLength(relativeX, relativeY, relativeZ, &vectorLength);
			bodyPropertyFallback = (uint16_t)vectorLength.otherQuarter;
			candidateDistance = vectorLength.approximateLength;
			SlipRacePlayer_neighbourCandidateDistance = candidateDistance;
			roadPoint = SlipRacePlayer_TrackPoint(context, candidateObjectId);
			if (!roadPoint.notFound) {
				bodyPropertyFallback = (uint16_t)roadPoint.roadPointY;
			}
			SlipRacePlayer_neighbourCandidateRoad = (SlipView3DVec32){
			    (int32_t)roadPoint.roadPointX, (int32_t)roadPoint.roadPointY, (int32_t)roadPoint.roadPointZ};
			if (roadPoint.notFound || roadPoint.roadPointX != currentRoadX || roadPoint.roadPointY != currentRoadY ||
			    roadPoint.roadPointZ != currentRoadZ) {
				SlipObjectDirection candidateDirection;
				SlipView3DDotProduct32By16 directionDot;

				SlipObject_Position(context->objectTable, context->objectTableBytes, candidateObjectId,
				                    &objectPosition);
				relativeX = objectPosition.positionX - objectX;
				relativeY = objectPosition.positionY - objectY;
				relativeZ = objectPosition.positionZ - objectZ;
				candidateDirection = SlipObject_Direction(context->objectTable, candidateObjectId);
				SlipView3D_DotProduct32By16(relativeX, relativeY, relativeZ, candidateDirection.directionXQ14,
				                            candidateDirection.directionYQ14, candidateDirection.directionZQ14,
				                            &directionDot);

				bodyPropertyFallback = (uint16_t)(directionDot.sumXY >> 32);
				if ((int16_t)(uint16_t)directionDot.dotProductHigh < 0) {
					if (candidateDistance <= farDistance) {
						farObject = candidateObjectId;
						farDistance = candidateDistance;
						SlipRacePlayer_farNeighbourObject = farObject;
						SlipRacePlayer_farNeighbourDistance = farDistance;
					}
				} else if (candidateDistance <= nearDistance) {
					nearObject = candidateObjectId;
					nearDistance = candidateDistance;
					SlipRacePlayer_nearNeighbourObject = nearObject;
					SlipRacePlayer_nearNeighbourDistance = nearDistance;
				}
				candidateSlotAddress = candidateSlot->nextSlotAddress;
				continue;
			}
			SlipObject_Position(context->objectTable, context->objectTableBytes, candidateObjectId, &objectPosition);
			SlipDraw3D_ApproxAbsVectorLength(objectPosition.positionX - roadPoint.roadPointX,
			                                 objectPosition.positionY - roadPoint.roadPointY,
			                                 objectPosition.positionZ - roadPoint.roadPointZ, &vectorLength);
			bodyPropertyFallback = (uint16_t)vectorLength.otherQuarter;
			if (vectorLength.approximateLength + candidateSlot->boundingRadius <= nearLimit) {
				if (candidateDistance <= nearDistance) {
					nearObject = candidateObjectId;
					nearDistance = candidateDistance;
					SlipRacePlayer_nearNeighbourObject = nearObject;
					SlipRacePlayer_nearNeighbourDistance = nearDistance;
				}
			} else if (vectorLength.approximateLength + candidateSlot->boundingRadius >= farLimit) {
				if (candidateDistance <= farDistance) {
					farObject = candidateObjectId;
					farDistance = candidateDistance;
					SlipRacePlayer_farNeighbourObject = farObject;
					SlipRacePlayer_farNeighbourDistance = farDistance;
				}
			} else if (candidateDistance <= middleDistance) {
				middleObject = candidateObjectId;
				middleDistance = candidateDistance;
				SlipRacePlayer_middleNeighbourObject = middleObject;
				SlipRacePlayer_middleNeighbourDistance = middleDistance;
			}
		}
		candidateSlotAddress = candidateSlot->nextSlotAddress;
	}
	return (SlipRacePlayerNeighbours){nearObject,     middleObject, farObject, nearDistance,
	                                  middleDistance, farDistance,  false};
}

void SlipRacePlayer_ResetMinimumPosition(void) { SlipRacePlayer_minimumPosition = 0; }

void SlipRacePlayer_UpdateMinimumPosition(const SlipRaceRacerState *racerStates, size_t racerStateCount) {
	size_t racerIndex = 0;
	int16_t minimumPosition = 10;

	SlipRacePlayer_previousMinimumPosition = SlipRacePlayer_minimumPosition;
	while (racerIndex < racerStateCount) {
		const SlipRaceRacerState *const racerState = &racerStates[racerIndex];

		if (racerState->racerType != 2u && (int16_t)racerState->racePosition < minimumPosition) {
			minimumPosition = (int16_t)racerState->racePosition;
		}
		++racerIndex;
	}
	SlipRacePlayer_minimumPosition = (uint16_t)minimumPosition;
}

uint32_t SlipRacePlayer_Update(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue, uint32_t eventFlags,
                               uint16_t objectOffset, uintptr_t dispatchData, uint32_t dispatchFrame) {
	SlipRacePlayerHostBindings callbackContext = *SlipRacePlayer_hostContext;
	const uint16_t event = (uint16_t)eventCode;
	SlipRacePlayerPrivateRecord *privateState;
	SlipRaceRacerState *racerRecord;
	SlipRacePlayerHostBindings *const context = &callbackContext;

	(void)eventValue;
	(void)eventFlags;
	(void)dispatchData;
	(void)dispatchFrame;
	context->objectOffset = objectOffset;

	switch (event) {
	case SLIP_OBJECT_EVENT_INITIALIZE:
		privateState = SlipRacePlayer_PrivateState(objectOffset);
		privateState->speedFraction = 0;
		privateState->speed = 0;
		privateState->collisionCooldown = 0;
		privateState->collisionImpulseX = 0;
		privateState->collisionImpulseY = 0;
		privateState->collisionImpulseZ = 0;
		privateState->roadCoordinateX = 0;
		privateState->roadCoordinateY = 0;
		privateState->roadCoordinateTimer = 0;
		privateState->damageCooldown = 0;
		privateState->invertedControlsTimer = 0;
		privateState->speedLimitTimer = 0;
		privateState->forcedAccelerateTimer = 0;
		privateState->amplifiedControlsTimer = 0;
		privateState->powerupSpeedTimer = 0;
		privateState->weaponCooldown = 0;
		privateState->positionBoostTimer = 0;
		privateState->impactPenaltyTimer = 0;
		privateState->actionPressed = 0;
		privateState->previousActionPressed = 0;
		privateState->weaponSelection = 0;
		privateState->weaponCharge[0] = 0x4000u;
		privateState->weaponCharge[1] = 0x4000u;
		privateState->weaponCharge[2] = 0x4000u;
		privateState->targetObject = 0;
		privateState->recoveryPreviousCallback = NULL;
		return 0;

	case SLIP_OBJECT_EVENT_BIND_RACER: {
		const uint32_t racerOffset = eventPayload - context->racerRecordsOffset;
		const size_t racerIndex = racerOffset / SLIP_RACE_RACER_RECORD_BYTES;
		uint32_t trackComponent;
		SlipRacePlayerRoadCoordinates road;
		bool partMissing;
		uint16_t foundPartCount;
		uint32_t partTag;

		privateState = SlipRacePlayer_PrivateState(objectOffset);
		privateState->racerStateOffset = eventPayload;
		if (context->racerStates == NULL || racerOffset % SLIP_RACE_RACER_RECORD_BYTES != 0 ||
		    racerIndex >= context->racerStateCount) {
			return 1;
		}
		racerRecord = &context->racerStates[racerIndex];
		racerRecord->objectOffset = objectOffset;
		racerRecord->lapNumber = 0;
		racerRecord->wrongWay = 0;
		racerRecord->destroyed = 0;
		racerRecord->finished = 0;
		racerRecord->bestLapTime = 0;
		racerRecord->currentLapTime = 0;
		racerRecord->totalRaceTime = 0;
		racerRecord->movementDamageQ16 = 0;
		racerRecord->handlingDamageQ16 = 0;
		trackComponent = SlipTrackWorld_CurrentComponent(
		    0, objectOffset, context->slotListBase, context->slotListBytes, context->slotListBaseOffset,
		    context->objectTable, context->objectTableBytes, context->trdBase, context->trackDataSize,
		    context->trackDataOffset, context->componentBase, context->componentBaseBytes, context->componentBaseOffset,
		    context->trackTable, context->trackTableBytes);
		racerRecord->trackComponent = (uint16_t)trackComponent;
		if (SlipRacePlayer_RoadCoordinates(
		        objectOffset, context->objectTable, context->objectTableBytes, context->slotListBase,
		        context->slotListBytes, context->slotListBaseOffset, context->trdBase, context->trackDataSize,
		        context->trackDataOffset, context->componentBase, context->componentBaseBytes,
		        context->componentBaseOffset, context->trackTable, context->trackTableBytes, &road)) {
			privateState = SlipRacePlayer_PrivateState(objectOffset);
			privateState->roadCoordinateX = (uint32_t)road.roadX;
			privateState->roadCoordinateY = (uint32_t)road.roadY;
			privateState->roadCoordinateTimer = 0x0bb8u;
		}
		privateState = SlipRacePlayer_PrivateState(objectOffset);
		privateState->weaponSelectionTimer = 0x3a98u;
		foundPartCount = 0;
		partTag = 0x66616e31u;
		do {
			SlipArticSlot_FindTag(partTag, objectOffset, context->objectTable, context->objectTableBytes,
			                      context->articSlotPool, context->articSlotPoolBytes, context->articSlotPoolOffset,
			                      context->articData, context->articDataBytes, context->articDataOffset, &partMissing);
			if (!partMissing)
				++foundPartCount;
			++partTag;
		} while (partTag != 0x66616e35u);
		privateState = SlipRacePlayer_PrivateState(objectOffset);
		privateState->fanPartCount = foundPartCount;
		privateState->fanAngle = 0;
		foundPartCount = 0;
		partTag = 0x6a657431u;
		do {
			SlipArticSlot_FindTag(partTag, objectOffset, context->objectTable, context->objectTableBytes,
			                      context->articSlotPool, context->articSlotPoolBytes, context->articSlotPoolOffset,
			                      context->articData, context->articDataBytes, context->articDataOffset, &partMissing);
			if (!partMissing)
				++foundPartCount;
			++partTag;
		} while (partTag != 0x6a657435u);
		privateState = SlipRacePlayer_PrivateState(objectOffset);
		privateState->jetPartCount = foundPartCount;
		privateState = SlipRacePlayer_PrivateState(objectOffset);
		privateState->powerupActive = 0;
		racerRecord = SlipRacePlayer_CurrentRacerFromState(context, privateState);
		if ((int32_t)racerRecord->powerupRecord >= 0) {
			privateState->powerupCharge = 0x4000u;
		}
	}
		return 0;

	case SLIP_OBJECT_EVENT_SET_CONTROLLER:
		privateState = SlipRacePlayer_PrivateState(objectOffset);
		privateState->controller = (uint16_t)eventPayload;
		return 0;

	case SLIP_OBJECT_EVENT_RESET_MOTION:
		privateState = SlipRacePlayer_PrivateState(objectOffset);
		privateState->speed = 0;
		privateState->speedFraction = 0;
		privateState->collisionImpulseX = 0;
		privateState->collisionImpulseY = 0;
		privateState->collisionImpulseZ = 0;
		return 0;

	case SLIP_OBJECT_EVENT_APPLY_DAMAGE: {
		SlipRacePlayerRecordValues scaledDamageValues;

		privateState = SlipRacePlayer_PrivateState(objectOffset);
		racerRecord = SlipRacePlayer_CurrentRacerFromState(context, privateState);
		if (racerRecord->racerType == 2u) {
			privateState->positionBoostTimer = 0;
			privateState->impactPenaltyTimer = 0x00fau;
			if ((uint16_t)eventValue == SlipRacePlayer_playerOneObject) {

				static const uint32_t hitByPlayerVoices[] = {14, 15, 16, 17, 18, 19, 20, 21, 22, 23};
				SlipGameSoundState *const sound = context->soundEffects->gameSound;
				SlipRaceVoiceCalls voiceCalls = SlipRaceVoiceHost_Calls(sound);
				SlipRaceVoice_Play(sound->digitalCard, hitByPlayerVoices[racerRecord->tuningIndex - 1], &voiceCalls);
			}
		}
		if ((int32_t)SlipRandom_Next() <= 0x7000) {

			SlipRaceEffects_Debris(3, objectOffset, dispatchFrame);
		}
		if (eventPayload != 1u) {
			scaledDamageValues = SlipRacePlayer_WeaponImpactDamageValues(context->weaponRecords, eventPayload);
			(void)SlipRacePlayer_ApplyDamage(context, scaledDamageValues.movementDamageQ16,
			                                 scaledDamageValues.handlingDamageQ16, objectOffset);

			SlipSoundEffects_Queue(context->soundEffects, 0, 0, 0, 0xau, objectOffset, 1u);
			if (objectOffset == SlipRacePlayer_playerOneObject) {

				enum { SLIP_RACE_VOICE_PLAYER_HIT = 0x3f };

				SlipGameSoundState *const sound = context->soundEffects->gameSound;
				SlipRaceVoiceCalls voiceCalls = SlipRaceVoiceHost_Calls(sound);
				SlipRaceVoice_Play(sound->digitalCard, SLIP_RACE_VOICE_PLAYER_HIT, &voiceCalls);
			}
		} else {
			privateState = SlipRacePlayer_PrivateState(objectOffset);
			privateState->invertedControlsTimer = 0x1388u;
			SlipRacePlayer_NotifyState(objectOffset, SlipRacePlayer_playerOneObject, SlipRacePlayer_playerTwoObject,
			                           context->transitionFrames, context->transitionDuration);

			SlipSoundEffects_Queue(context->soundEffects, 0, 0, 0, 0xbu, objectOffset, 1u);
		}
	}
		return 0;

	case SLIP_OBJECT_EVENT_COLLISION_STOP: {
		enum { OBJECT_FLAG_BONUS = 1u };

		const uint32_t otherObjectFlags = SlipObject_GetActorHandle((uint16_t)eventPayload);
		const SlipRaceCollisionStopEvent *const collisionEvent = &SlipRaceCollision_stopEvent;
		uint32_t projectileWeaponIndex;
		SlipView3DVec32 impulse;
		SlipRacePlayerRecordValues impactDamageValues;
		int32_t impactScale;

		if ((otherObjectFlags & OBJECT_FLAG_BONUS) != 0) {
			uint32_t bonusType;

			privateState = SlipRacePlayer_PrivateState(objectOffset);
			bonusType = SlipRaceBonus_Type((uint16_t)eventPayload);
			switch (bonusType) {
			case SLIP_RACE_BONUS_ENGINE_REPAIR:
				racerRecord = SlipRacePlayer_CurrentRacerFromState(context, privateState);
				racerRecord->movementDamageQ16 = 0;
				SlipRacePlayer_NotifyState(objectOffset, SlipRacePlayer_playerOneObject, SlipRacePlayer_playerTwoObject,
				                           context->transitionFrames, context->transitionDuration);

				SlipSoundEffects_Queue(context->soundEffects, 0, 0, 0, 6u, objectOffset, 1u);
				break;
			case SLIP_RACE_BONUS_CONTROL_REPAIR:
				racerRecord = SlipRacePlayer_CurrentRacerFromState(context, privateState);
				racerRecord->handlingDamageQ16 = 0;
				SlipRacePlayer_NotifyState(objectOffset, SlipRacePlayer_playerOneObject, SlipRacePlayer_playerTwoObject,
				                           context->transitionFrames, context->transitionDuration);

				SlipSoundEffects_Queue(context->soundEffects, 0, 0, 0, 6u, objectOffset, 1u);
				break;
			case SLIP_RACE_BONUS_POWERUP_RECHARGE:
				privateState->powerupCharge = 0x4000u;
				SlipRacePlayer_NotifyState(objectOffset, SlipRacePlayer_playerOneObject, SlipRacePlayer_playerTwoObject,
				                           context->transitionFrames, context->transitionDuration);

				SlipSoundEffects_Queue(context->soundEffects, 0, 0, 0, 6u, objectOffset, 1u);
				break;
			case SLIP_RACE_BONUS_REVERSE_CONTROLS:
				privateState->invertedControlsTimer = 0x1388u;
				SlipRacePlayer_NotifyState(objectOffset, SlipRacePlayer_playerOneObject, SlipRacePlayer_playerTwoObject,
				                           context->transitionFrames, context->transitionDuration);

				SlipSoundEffects_Queue(context->soundEffects, 0, 0, 0, 0xbu, objectOffset, 1u);
				break;
			case SLIP_RACE_BONUS_CREDITS:
				racerRecord = SlipRacePlayer_CurrentRacerFromState(context, privateState);
				racerRecord->bonusScore += 0x32u;

				SlipSoundEffects_Queue(context->soundEffects, 0, 0, 0, 6u, objectOffset, 1u);
				break;
			case SLIP_RACE_BONUS_SPEED_BOOST:
				privateState->powerupSpeedTimer = 0x1388u;
				SlipRacePlayer_NotifyState(objectOffset, SlipRacePlayer_playerOneObject, SlipRacePlayer_playerTwoObject,
				                           context->transitionFrames, context->transitionDuration);

				SlipSoundEffects_Queue(context->soundEffects, 0, 0, 0, 0xcu, objectOffset, 1u);
				break;
			default:
				SlipRuntime_Fatal("RaceSlotControl: Unknown bonus type.");
			}
			return 0;
		}

		SlipRacePlayer_damageSourceFlags = otherObjectFlags;
		SlipRacePlayer_damageSourceObject = (uint16_t)eventPayload;
		if ((otherObjectFlags & SLIP_OBJECT_FLAG_PROJECTILE) != 0) {
			SlipRacePlayer_NotifyTimer(objectOffset, SlipRacePlayer_playerOneObject, SlipRacePlayer_playerTwoObject,
			                           context->primaryViewShake, context->secondaryViewShake);

			SlipRaceEffects_Debris(5, objectOffset, dispatchFrame);

			if (SlipRacePlayer_ProjectileShooter((uint16_t)eventPayload) == SlipRacePlayer_playerOneObject) {
				static const uint32_t hitByPlayerVoices[] = {14, 15, 16, 17, 18, 19, 20, 21, 22, 23};
				privateState = SlipRacePlayer_PrivateState(objectOffset);
				racerRecord = SlipRacePlayer_CurrentRacerFromState(context, privateState);
				SlipGameSoundState *const sound = context->soundEffects->gameSound;
				SlipRaceVoiceCalls voiceCalls = SlipRaceVoiceHost_Calls(sound);
				SlipRaceVoice_Play(sound->digitalCard, hitByPlayerVoices[racerRecord->tuningIndex - 1], &voiceCalls);
			}

			projectileWeaponIndex = SlipRacePlayer_ProjectileWeaponIndex((uint16_t)eventPayload);
			switch (projectileWeaponIndex) {
			case 6:
				privateState = SlipRacePlayer_PrivateState(objectOffset);
				privateState->speedLimitTimer = 0x2710u;

				SlipSoundEffects_Queue(context->soundEffects, 0, 0, 0, 0x10u, objectOffset, 1u);
				break;
			case 10:
				privateState = SlipRacePlayer_PrivateState(objectOffset);
				privateState->forcedAccelerateTimer = 0x0fa0u;

				SlipSoundEffects_Queue(context->soundEffects, 0, 0, 0, 0xdu, objectOffset, 1u);
				break;
			case 8:
				privateState = SlipRacePlayer_PrivateState(objectOffset);
				privateState->amplifiedControlsTimer = 0x2710u;

				SlipSoundEffects_Queue(context->soundEffects, 0, 0, 0, 0xfu, objectOffset, 1u);
				break;
			case 11:

				SlipSoundEffects_Queue(context->soundEffects, 0, 0, 0, 9u, objectOffset, 1u);
				if (objectOffset == SlipRacePlayer_playerOneObject) {
					SlipGameSoundState *const sound = context->soundEffects->gameSound;
					SlipRaceVoiceCalls voiceCalls = SlipRaceVoiceHost_Calls(sound);
					SlipRaceVoice_Play(sound->digitalCard, 0x40, &voiceCalls);
				}
				break;
			case 7:

				SlipSoundEffects_Queue(context->soundEffects, 0, 0, 0, 0xeu, objectOffset, 1u);
				break;
			default:

				SlipSoundEffects_Queue(context->soundEffects, 0, 0, 0, 9u, objectOffset, 1u);
				break;
			}
		}

		if (collisionEvent->impactFlag != 0) {
			uint64_t product;

			privateState = SlipRacePlayer_PrivateState(objectOffset);
			product = (uint64_t)0xa0000000u * privateState->speed;
			privateState->speed = (uint32_t)(product >> 32);

			SlipSoundEffects_Queue(context->soundEffects, collisionEvent->contactPosition.x,
			                       collisionEvent->contactPosition.y, collisionEvent->contactPosition.z, 1u,
			                       objectOffset, 2u);
		}

		privateState = SlipRacePlayer_PrivateState(objectOffset);
		racerRecord = SlipRacePlayer_CurrentRacerFromState(context, privateState);
		if ((uint16_t)((uint32_t)privateState->collisionImpulseX >> 16) == 2u) {

			static const uint32_t impactVoices[] = {4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
			SlipGameSoundState *const sound = context->soundEffects->gameSound;
			SlipRaceVoiceCalls voiceCalls = SlipRaceVoiceHost_Calls(sound);
			SlipRaceVoice_Play(sound->digitalCard, impactVoices[racerRecord->tuningIndex - 1], &voiceCalls);
		}
		impactScale = collisionEvent->impactMagnitude;
		impactScale = (int32_t)((uint32_t)(impactScale >> 1) + (uint32_t)impactScale);
		if (impactScale < 0x37dc) {
			impactScale = 0x37dc;
		}
		impulse = SlipView3D_ScaleVector(collisionEvent->normalX, collisionEvent->normalY, collisionEvent->normalZ,
		                                 impactScale);
		privateState->collisionImpulseX = privateState->collisionImpulseX + (uint32_t)impulse.x;
		privateState->collisionImpulseY = privateState->collisionImpulseY + (uint32_t)impulse.y;
		privateState->collisionImpulseZ = privateState->collisionImpulseZ + (uint32_t)impulse.z;
		SlipRacePlayer_IntegrateDirection(context);

		impactDamageValues = (SlipRacePlayerRecordValues){0, 0x00040000u};
		if ((SlipRacePlayer_damageSourceFlags & SLIP_OBJECT_FLAG_PROJECTILE) != 0) {
			impactDamageValues =
			    SlipRacePlayer_ProjectileDamageValues(context->weaponRecords, SlipRacePlayer_damageSourceObject);
		}
		if (collisionEvent->impactFlag != 0) {
			impactDamageValues.movementDamageQ16 += 0x00020000u;
			impactDamageValues.handlingDamageQ16 += 0x00020000u;
		}
		(void)SlipRacePlayer_ApplyDamage(context, impactDamageValues.movementDamageQ16,
		                                 impactDamageValues.handlingDamageQ16, objectOffset);
	}
		return 0;

	case SLIP_OBJECT_EVENT_COLLISION_BOUNCE: {
		const SlipRaceCollisionBounceEvent *const collisionEvent = &SlipRaceCollision_bounceEvent;

		SlipRaceEffects_SparkSplash(context, 6, objectOffset, collisionEvent);
		SlipRaceEffects_ContactFragments(context, 6, objectOffset, collisionEvent);

		SlipRaceEffects_Debris(6, objectOffset, dispatchFrame);
		const int32_t impactSpeed = SlipObject_Speed(context->objectTable, objectOffset);
		uint32_t dampedSpeed;
		uint32_t soundEffect = 3u;

		if (impactSpeed > 0x00022e98) {
			soundEffect = 2u;
			SlipRacePlayer_NotifyTimer(objectOffset, SlipRacePlayer_playerOneObject, SlipRacePlayer_playerTwoObject,
			                           context->primaryViewShake, context->secondaryViewShake);
		}

		{
			const uint8_t *const name = SlipDraw3D_GetMaterialName(context->materialTable, context->materialTableBytes,
			                                                       (uint16_t)collisionEvent->material);
			if (name != NULL && memcmp(name, "WATE", 4u) == 0)
				soundEffect = 8u;
		}
		SlipSoundEffects_Queue(context->soundEffects, 0, 0, 0, soundEffect, objectOffset, 1u);
		privateState = SlipRacePlayer_PrivateState(objectOffset);
		dampedSpeed = (uint32_t)(((uint64_t)0xc0000000u * privateState->speed) >> 32);
		privateState->speed = dampedSpeed;
		if (privateState->collisionCooldown != 0) {
			SlipObjectEventCallbackWriteResult setCallback;

			privateState->collisionCooldown = 0;
			privateState->invertedControlsTimer = 0;
			privateState->speedLimitTimer = 0;
			privateState->forcedAccelerateTimer = 0;
			privateState->amplifiedControlsTimer = 0;
			privateState->collisionImpulseX = 0;
			privateState->collisionImpulseY = 0;
			privateState->collisionImpulseZ = 0;
			SlipRacePlayer_InitializeImpactRecovery(context, (uint32_t)((int32_t)privateState->speed >> 1), 0x03e8u,
			                                        0x0000e000u, objectOffset);
			(void)SlipObject_SetEventCallback(context->objectTable, context->objectTableBytes, objectOffset,
			                                  SlipRacePlayer_RivalUpdate, &setCallback);
		} else {
			SlipView3DVec32 collisionVector;
			SlipView3DVec32 impulse;

			privateState->collisionCooldown = 0x0190u;
			collisionVector = SlipRacePlayer_CollisionVector(context, (uint16_t)collisionEvent->normalX,
			                                                 (uint16_t)collisionEvent->normalY,
			                                                 (uint16_t)collisionEvent->normalZ, 0x3000u);
			privateState = SlipRacePlayer_PrivateState(objectOffset);
			impulse = SlipView3D_ScaleVector((int16_t)collisionVector.x, (int16_t)collisionVector.y,
			                                 (int16_t)collisionVector.z, (int32_t)privateState->speed);
			privateState->collisionImpulseX = privateState->collisionImpulseX + (uint32_t)impulse.x;
			privateState->collisionImpulseY = privateState->collisionImpulseY + (uint32_t)impulse.y;
			privateState->collisionImpulseZ = privateState->collisionImpulseZ + (uint32_t)impulse.z;
			SlipRacePlayer_IntegrateDirection(context);
			(void)SlipRacePlayer_ApplyDamage(context, 0x00020000u, 0x00010000u, objectOffset);
		}
	}
		return 0;

	case SLIP_OBJECT_EVENT_HANDLE_ACTION: {
		const uint32_t trackFloor = SlipTrackWorld_TrackFloor(context->trkBase);
		SlipObjectPosition objectPosition;

		if (SlipObject_Position(context->objectTable, context->objectTableBytes, objectOffset, &objectPosition) &&
		    (int32_t)objectPosition.positionY > (int32_t)trackFloor) {
			SlipObjectSetPosition setPosition;

			SlipRaceCollision_SaveObjectTransform(objectOffset);
			(void)SlipObject_SetPosition(context->objectTable, context->objectTableBytes, objectOffset,
			                             objectPosition.positionX, trackFloor, objectPosition.positionZ, &setPosition);
			if (SlipRaceCollision_Query(objectOffset)) {
				SlipRaceCollision_RestoreObjectTransform(objectOffset);
			}
		}

		{
			RacePlayerPositionVoiceContext voiceContext = {context, objectOffset};
			const SlipRacePositionVoiceCalls voiceCalls = {&voiceContext,
			                                               SlipRacePlayer_PositionVoiceRandomState,
			                                               SlipRacePlayer_PositionVoiceRacer,
			                                               SlipRacePlayer_PositionVoiceRandom,
			                                               SlipRacePlayer_PositionVoicePlay,
			                                               SlipRacePlayer_PositionVoiceRestoreRandom};
			SlipRaceVoice_PositionChange(SlipRacePlayer_minimumPosition, SlipRacePlayer_previousMinimumPosition,
			                             &voiceCalls);
		}
		privateState = SlipRacePlayer_PrivateState(objectOffset);
		if (privateState->actionPressed != 0) {
			const uint32_t weaponSelection = privateState->weaponSelection;
			uint32_t fireWeaponIndex;
			uint16_t chargeCost;
			uint16_t *energyField;
			uint32_t *ammoField;

			if (weaponSelection == 0) {
				if (privateState->weaponCooldown != 0)
					return 0;
				chargeCost = (uint16_t)SlipRacePlayer_WeaponChargeCost(context->weaponRecords, 0);
				if ((int16_t)privateState->weaponCharge[0] < (int16_t)chargeCost) {
					return 0;
				}
				privateState->weaponCharge[0] = (uint16_t)(privateState->weaponCharge[0] - chargeCost);
				privateState->weaponCooldown = 0x01f4u;
				racerRecord = SlipRacePlayer_CurrentRacerFromState(context, privateState);
				if ((racerRecord->powerupFlags & 1u) != 0) {
					privateState->weaponCooldown = 0x012cu;
				}
				fireWeaponIndex = 0;
			} else if (weaponSelection == 3u) {
				if (privateState->previousActionPressed != 0 || privateState->powerupCharge == 0) {
					return 0;
				}
				privateState->powerupActive ^= 1u;
				if (privateState->powerupActive != 0) {

					SlipSoundEffects_Queue(context->soundEffects, 0, 0, 0, 0xcu, objectOffset, 1u);
				}
				SlipRacePlayer_NotifyState(objectOffset, SlipRacePlayer_playerOneObject, SlipRacePlayer_playerTwoObject,
				                           context->transitionFrames, context->transitionDuration);
				return 0;
			} else {
				racerRecord = SlipRacePlayer_CurrentRacerFromState(context, privateState);
				if (weaponSelection == 1u) {
					fireWeaponIndex = racerRecord->primaryWeaponIndex;
					energyField = &privateState->weaponCharge[1];
					ammoField = &racerRecord->primaryWeaponAmmo;
				} else {
					fireWeaponIndex = racerRecord->secondaryWeaponIndex;
					energyField = &privateState->weaponCharge[2];
					ammoField = &racerRecord->secondaryWeaponAmmo;
				}
				chargeCost = (uint16_t)SlipRacePlayer_WeaponChargeCost(context->weaponRecords, fireWeaponIndex);
				if ((int16_t)*energyField < (int16_t)chargeCost) {
					return 0;
				}
				if ((int32_t)*ammoField >= 0) {
					uint32_t ammo = *ammoField;

					if (ammo == 0)
						return 0;
					--ammo;
					*ammoField = ammo;
					if (ammo == 0) {
						if (weaponSelection == 1u) {
							racerRecord->primaryWeaponIndex = UINT32_MAX;
						} else {
							racerRecord->secondaryWeaponIndex = UINT32_MAX;
						}
						SlipRacePlayer_AdvanceState(privateState, racerRecord);

						if (objectOffset == SlipRacePlayer_playerOneObject) {
							SlipGameSoundState *const sound = context->soundEffects->gameSound;
							SlipRaceVoiceCalls calls = SlipRaceVoiceHost_Calls(sound);
							SlipRaceVoice_Play(sound->digitalCard, 0, &calls);
						}
					}
				}
				*energyField = (uint16_t)(*energyField - chargeCost);
			}

			if (fireWeaponIndex < 12u && context->weaponRecords[fireWeaponIndex].fireCallback != NULL) {
				context->weaponRecords[fireWeaponIndex].fireCallback(objectOffset, privateState->targetObject);
			}
			SlipRacePlayer_NotifyState(objectOffset, SlipRacePlayer_playerOneObject, SlipRacePlayer_playerTwoObject,
			                           context->transitionFrames, context->transitionDuration);
		}
	}
		return 0;

	case SLIP_OBJECT_EVENT_UPDATE: {
		SlipRacePlayerNeighbours neighbours;
		SlipFrameTimerValues frame;
		uint16_t frameMilliseconds;
		int16_t timer;
		SlipTrackWorldCurrentSlot slot;
		uint32_t frameStep;
		uint32_t increment;
		uint32_t recordIndex;
		uint32_t targetObject;
		SlipRacePlayerControl controls;
		uint32_t appliedActions;
		bool angleUpdateCarry;

		neighbours = SlipRacePlayer_FindNeighbours(context, 1u, objectOffset);
		SlipRacePlayer_rivalObject = neighbours.nearObject;
		SlipRacePlayer_rivalDistance = (int32_t)neighbours.nearDistance;
		SlipRacePlayer_farNeighbourCandidate = neighbours.farObject;
		SlipRacePlayer_raceDistance = (int32_t)neighbours.farDistance;
		SlipRacePlayer_middleNeighbourCandidate = neighbours.middleObject;
		privateState = SlipRacePlayer_PrivateState(objectOffset);
		frame = SlipFrameTimer_Values();
		frameMilliseconds = (uint16_t)frame.deltaMilliseconds;

		timer = (int16_t)(privateState->damageCooldown - frameMilliseconds);
		privateState->damageCooldown = timer < 0 ? 0u : (uint16_t)timer;
		timer = (int16_t)(privateState->collisionCooldown - frameMilliseconds);
		privateState->collisionCooldown = timer < 0 ? 0u : (uint16_t)timer;
		timer = (int16_t)(privateState->invertedControlsTimer - frameMilliseconds);
		privateState->invertedControlsTimer = timer < 0 ? 0u : (uint16_t)timer;
		if (timer < 0)
			SlipRacePlayer_NotifyState(objectOffset, SlipRacePlayer_playerOneObject, SlipRacePlayer_playerTwoObject,
			                           context->transitionFrames, context->transitionDuration);
		timer = (int16_t)(privateState->speedLimitTimer - frameMilliseconds);
		privateState->speedLimitTimer = timer < 0 ? 0u : (uint16_t)timer;
		timer = (int16_t)(privateState->forcedAccelerateTimer - frameMilliseconds);
		privateState->forcedAccelerateTimer = timer < 0 ? 0u : (uint16_t)timer;
		timer = (int16_t)(privateState->amplifiedControlsTimer - frameMilliseconds);
		privateState->amplifiedControlsTimer = timer < 0 ? 0u : (uint16_t)timer;
		timer = (int16_t)(privateState->weaponSelectionTimer - frameMilliseconds);
		privateState->weaponSelectionTimer = timer < 0 ? 0u : (uint16_t)timer;
		timer = (int16_t)(privateState->weaponCooldown - frameMilliseconds);
		privateState->weaponCooldown = timer < 0 ? 0u : (uint16_t)timer;
		timer = (int16_t)(privateState->powerupSpeedTimer - frameMilliseconds);
		privateState->powerupSpeedTimer = timer < 0 ? 0u : (uint16_t)timer;
		if (timer < 0)
			SlipRacePlayer_NotifyState(objectOffset, SlipRacePlayer_playerOneObject, SlipRacePlayer_playerTwoObject,
			                           context->transitionFrames, context->transitionDuration);
		timer = (int16_t)(privateState->positionBoostTimer - frameMilliseconds);
		privateState->positionBoostTimer = timer < 0 ? 0u : (uint16_t)timer;
		timer = (int16_t)(privateState->impactPenaltyTimer - frameMilliseconds);
		privateState->impactPenaltyTimer = timer < 0 ? 0u : (uint16_t)timer;

		SlipTrackWorld_CurrentSlot(frame.deltaMilliseconds, objectOffset, context->slotListBase, context->slotListBytes,
		                           context->slotListBaseOffset, context->objectTable, context->objectTableBytes,
		                           context->trdBase, context->trackDataSize, context->trackDataOffset,
		                           context->componentBase, context->componentBaseBytes, context->componentBaseOffset,
		                           context->trackTable, context->trackTableBytes, SlipRacePlayer_refuelSection, &slot);
		if (slot.carryOut) {
			privateState = SlipRacePlayer_PrivateState(objectOffset);
			frameStep = SlipFrameTimer_Step();
			increment = (uint32_t)(((uint64_t)0x00190000u * frameStep) >> 14);
			racerRecord = SlipRacePlayer_CurrentRacerFromState(context, privateState);
			increment = racerRecord->movementDamageQ16 - increment;
			racerRecord->movementDamageQ16 = increment;
			if ((int32_t)increment < 0) {
				racerRecord->movementDamageQ16 = 0;
			}
			increment = (uint32_t)(((uint64_t)0x00190000u * frameStep) >> 14);
			increment = racerRecord->handlingDamageQ16 - increment;
			racerRecord->handlingDamageQ16 = increment;
			if ((int32_t)increment < 0) {
				racerRecord->handlingDamageQ16 = 0;
			}
			increment = (uint16_t)(((uint32_t)0x4000u * (uint16_t)SlipFrameTimer_Step()) >> 14);

			increment = (uint16_t)(increment + privateState->powerupCharge);
			if (increment >= 0x4001u)
				increment = 0x4000u;
			privateState->powerupCharge = (uint16_t)increment;
			SlipRacePlayer_NotifyState(objectOffset, SlipRacePlayer_playerOneObject, SlipRacePlayer_playerTwoObject,
			                           context->transitionFrames, context->transitionDuration);
		}

		privateState = SlipRacePlayer_PrivateState(objectOffset);
		racerRecord = SlipRacePlayer_CurrentRacerFromState(context, privateState);
		if (privateState->weaponCharge[0] != 0x4000u) {
			increment = SlipRacePlayer_WeaponRechargeRate(0);
			if ((racerRecord->powerupFlags & 1u) != 0)
				increment <<= 1;
			increment = (uint16_t)(((uint32_t)(uint16_t)increment * (uint16_t)SlipFrameTimer_Step()) >> 14);
			increment = (uint16_t)(increment + privateState->weaponCharge[0]);
			if ((int16_t)increment > 0x4000)
				increment = 0x4000u;
			privateState->weaponCharge[0] = (uint16_t)increment;
			SlipRacePlayer_NotifyState(objectOffset, SlipRacePlayer_playerOneObject, SlipRacePlayer_playerTwoObject,
			                           context->transitionFrames, context->transitionDuration);
		}
		if (privateState->weaponCharge[1] != 0x4000u) {
			recordIndex = racerRecord->primaryWeaponIndex;
			increment = SlipRacePlayer_WeaponRechargeRate(recordIndex);
			if ((racerRecord->powerupFlags & 1u) != 0)
				increment <<= 1;
			increment = (uint16_t)(((uint32_t)(uint16_t)increment * (uint16_t)SlipFrameTimer_Step()) >> 14);
			increment = (uint16_t)(increment + privateState->weaponCharge[1]);
			if ((int16_t)increment > 0x4000)
				increment = 0x4000u;
			privateState->weaponCharge[1] = (uint16_t)increment;
			SlipRacePlayer_NotifyState(objectOffset, SlipRacePlayer_playerOneObject, SlipRacePlayer_playerTwoObject,
			                           context->transitionFrames, context->transitionDuration);
		}
		if (privateState->weaponCharge[2] != 0x4000u) {
			recordIndex = racerRecord->secondaryWeaponIndex;
			increment = SlipRacePlayer_WeaponRechargeRate(recordIndex);
			if ((racerRecord->powerupFlags & 1u) != 0)
				increment <<= 1;
			increment = (uint16_t)(((uint32_t)(uint16_t)increment * (uint16_t)SlipFrameTimer_Step()) >> 14);
			increment = (uint16_t)(increment + privateState->weaponCharge[2]);
			if ((int16_t)increment > 0x4000)
				increment = 0x4000u;
			privateState->weaponCharge[2] = (uint16_t)increment;
			SlipRacePlayer_NotifyState(objectOffset, SlipRacePlayer_playerOneObject, SlipRacePlayer_playerTwoObject,
			                           context->transitionFrames, context->transitionDuration);
		}

		targetObject = 0;
		recordIndex = 0;
		{
			bool shouldSearchForTarget = true;

			if (racerRecord->racerType == 2u &&
			    (privateState->weaponSelection == 0 || privateState->weaponSelectionTimer == 0)) {
				uint16_t candidateObject = SlipRacePlayer_farNeighbourCandidate;
				bool useGeneralSelection = false;

				if (candidateObject != 0) {
					const SlipRaceRacerState *const candidateRecord = SlipRacePlayer_RacerState(candidateObject);

					if (candidateRecord == NULL || candidateRecord->racerType != 2u) {
						useGeneralSelection = true;
					}
				}
				if (!useGeneralSelection) {
					candidateObject = SlipRacePlayer_rivalObject;
					if (candidateObject != 0) {
						const SlipRaceRacerState *const candidateRecord = SlipRacePlayer_RacerState(candidateObject);

						if (candidateRecord == NULL || candidateRecord->racerType != 2u) {
							useGeneralSelection = true;
						}
					}
				}
				if (!useGeneralSelection)
					shouldSearchForTarget = false;
			} else if (racerRecord->racerType == 2u && privateState->weaponSelection != 0 &&
			           privateState->weaponSelectionTimer != 0) {
				shouldSearchForTarget = false;
			}

			if (shouldSearchForTarget) {
				switch (privateState->weaponSelection) {
				case 0:
					recordIndex = 0;
					break;
				case 1:
					recordIndex = racerRecord->primaryWeaponIndex;
					break;
				case 2:
					recordIndex = racerRecord->secondaryWeaponIndex;
					break;
				default:
					shouldSearchForTarget = false;
					break;
				}
			}
			if (shouldSearchForTarget) {
				uint32_t targetRange = SlipRacePlayer_WeaponTargetRange(context->weaponRecords, recordIndex);
				SlipArticSlotPosition part;
				SlipRaceCollisionNearestBody nearest;

				if (targetRange == 0)
					shouldSearchForTarget = false;
				if ((privateState->roadCoordinateY & 2u) != 0)
					targetRange <<= 1;
				if (shouldSearchForTarget) {
					if (!SlipArticSlot_Position(0x6d61696eu, 0x68656164u, objectOffset, context->objectTable,
					                            context->objectTableBytes, context->articSlotPool,
					                            context->articSlotPoolBytes, context->articSlotPoolOffset,
					                            context->articData, context->articDataBytes, context->articDataOffset,
					                            context->maths, &part) ||
					    part.lookupFailed) {
						shouldSearchForTarget = false;
					} else {
						SlipRaceCollision_FindNearestBody(
						    0x000ee480u, (uint16_t)targetRange, 0x988, (int16_t)part.positionX, (int16_t)part.positionY,
						    (int16_t)part.positionZ, objectOffset, context->maths, &nearest);
						if (nearest.noBodyFound) {
							shouldSearchForTarget = false;
						} else {
							targetObject = nearest.objectHandle;
						}
					}
				}
			}
			if (shouldSearchForTarget) {
				privateState = SlipRacePlayer_PrivateState(objectOffset);
				if (targetObject != 0) {
					racerRecord = SlipRacePlayer_CurrentRacerFromState(context, privateState);
					if (racerRecord->racerType == 2u) {
						const uint32_t targetFlags = SlipObject_GetActorHandle((uint16_t)targetObject);
						if ((targetFlags & 0x0cu) == 0)
							targetObject = 0;
					}
				}
			}
			privateState = SlipRacePlayer_PrivateState(objectOffset);
			privateState->targetObject = shouldSearchForTarget ? (uint16_t)targetObject : 0;
		}

		privateState = SlipRacePlayer_PrivateState(objectOffset);
		if (privateState->powerupActive != 0) {
			SlipRacePowerupScales scales;
			uint16_t chargeDrain;

			SlipRacePlayer_NotifyState(objectOffset, SlipRacePlayer_playerOneObject, SlipRacePlayer_playerTwoObject,
			                           context->transitionFrames, context->transitionDuration);
			racerRecord = SlipRacePlayer_CurrentRacerFromState(context, privateState);
			scales = SlipRacePowerup_GetScales(racerRecord->powerupRecord);
			chargeDrain =
			    (uint16_t)(((uint32_t)(uint16_t)scales.chargeDrainScaleQ14 * (uint16_t)SlipFrameTimer_Step()) >> 14);
			chargeDrain = (uint16_t)(privateState->powerupCharge - chargeDrain);
			privateState->powerupCharge = chargeDrain;
			if (chargeDrain >= 0x4001u) {
				privateState->powerupActive = 0;
				privateState->powerupCharge = 0;
			}
		}

		privateState = SlipRacePlayer_PrivateState(objectOffset);
		racerRecord = SlipRacePlayer_CurrentRacerFromState(context, privateState);
		if (racerRecord->finished == 0 && privateState->controller != 2u) {
			const uint16_t controller = privateState->controller;
			if (controller == 0) {
				controls = SlipRacePlayer_LoadControls(context, 1u);
			} else if (controller == 1u) {
				controls = SlipRacePlayer_LoadControls(context, 2u);
			} else {
				controls = SlipRacePlayer_LoadThirdControls();
			}
		} else {
			SlipRacePlayer_AiControls(context, &controls);
			if (SlipRacePlayer_aiControlsSuppressed != 0) {
				controls.actions &= 0xfffeu;
				controls.steering = 0;
				controls.pitch = 0;
			}
		}
		if (SlipRacePlayer_startCountdown != 0) {
			controls.actions &= 0xfffcu;
			controls.steering = 0;
			controls.pitch = 0;
		}

		if (context->soundEffects != NULL &&
		    (objectOffset == SlipRacePlayer_playerOneObject || objectOffset == SlipRacePlayer_playerTwoObject)) {
			SlipSoundEffects_AddEngine(context->soundEffects, 0, SlipObject_Speed(context->objectTable, objectOffset),
			                           (int32_t)controls.steering, objectOffset);
		}
		appliedActions = SlipRacePlayer_ApplyControls(context, controls);
		SlipArticSlot_SetAngle(0x64727631u, (uint16_t)controls.steering, objectOffset, context->objectTable,
		                       context->objectTableBytes, context->articSlotPool, context->articSlotPoolBytes,
		                       context->articSlotPoolOffset, context->articData, context->articDataBytes,
		                       context->articDataOffset, &angleUpdateCarry);
		privateState = SlipRacePlayer_PrivateState(objectOffset);
		privateState->previousActionPressed = privateState->actionPressed;
		privateState->actionPressed = (appliedActions & 2u) != 0 ? 1u : 0u;
		if ((appliedActions & 4u) != 0) {
			racerRecord = SlipRacePlayer_CurrentRacerFromState(context, privateState);
			SlipRacePlayer_AdvanceState(privateState, racerRecord);
			SlipRacePlayer_NotifyState(objectOffset, SlipRacePlayer_playerOneObject, SlipRacePlayer_playerTwoObject,
			                           context->transitionFrames, context->transitionDuration);
		}
		privateState = SlipRacePlayer_PrivateState(objectOffset);
		{
			uint32_t remainingPartCount = privateState->jetPartCount;
			if (remainingPartCount != 0) {
				int32_t jetSpeed = SlipObject_Speed(context->objectTable, objectOffset);
				uint16_t partAngle;
				uint32_t partTag = 0x6a657431u;
				if (jetSpeed > 0x22e98)
					jetSpeed = 0x22e98;
				partAngle = (uint16_t)(0x4000u - (((uint32_t)0x1d54u * (uint32_t)jetSpeed) >> 16));
				do {
					SlipArticSlot_SetAngle(
					    partTag++, partAngle, objectOffset, context->objectTable, context->objectTableBytes,
					    context->articSlotPool, context->articSlotPoolBytes, context->articSlotPoolOffset,
					    context->articData, context->articDataBytes, context->articDataOffset, &angleUpdateCarry);
				} while (--remainingPartCount != 0);
			}
		}
		privateState = SlipRacePlayer_PrivateState(objectOffset);
		{
			uint32_t remainingPartCount = privateState->fanPartCount;
			if (remainingPartCount != 0) {
				const uint16_t partAngle =
				    (uint16_t)(privateState->fanAngle +
				               (uint16_t)(((uint32_t)0x7fffu * (uint16_t)SlipFrameTimer_Step()) >> 14));
				uint32_t partTag = 0x66616e31u;
				privateState->fanAngle = partAngle;
				do {
					SlipArticSlot_SetAngle(
					    partTag++, partAngle, objectOffset, context->objectTable, context->objectTableBytes,
					    context->articSlotPool, context->articSlotPoolBytes, context->articSlotPoolOffset,
					    context->articData, context->articDataBytes, context->articDataOffset, &angleUpdateCarry);
				} while (--remainingPartCount != 0);
			}
		}
	}
		return 0;
	default:
		return 1;
	}
}

void SlipRacePlayer_BindHostContext(SlipRacePlayerHostBindings *context) {
	SlipRacePlayer_hostContext = context;
	SlipTrackWorld_beams = (SlipTrackBeamState){0};
}

static SlipTrackSlotRecord *SlipRacePlayer_TrackSlot(SlipRacePlayerHostBindings *context, uint16_t objectOffset) {
	SlipTrackWorldSlotListSelect select;
	uint32_t offset;

	if (context == NULL ||
	    !SlipTrackWorld_SelectSlotListEntry(0, context->slotListBaseOffset, context->objectTable,
	                                        context->objectTableBytes, objectOffset, &select) ||
	    select.carry) {
		return NULL;
	}
	offset = select.slotAddress - context->slotListBaseOffset;
	if (offset + 0x118u > context->slotListBytes)
		return NULL;
	return (SlipTrackSlotRecord *)(context->slotListBase + offset);
}

bool SlipRacePlayer_TrackLight(uint16_t objectOffset, uint16_t *light) {
	SlipRacePlayerHostBindings *const context = SlipRacePlayer_hostContext;
	SlipTrackSlotRecord *const slot = SlipRacePlayer_TrackSlot(context, objectOffset);
	if (slot == NULL || light == NULL)
		return false;
	if (!SlipTrackWorld_UpdateSlotRecord((uint8_t *)slot, context->objectTable, context->objectTableBytes,
	                                     context->trdBase, context->trackDataSize, context->trackDataOffset,
	                                     context->componentBase, context->componentBaseBytes,
	                                     context->componentBaseOffset, context->trackTable, context->trackTableBytes))
		return false;
	const uint32_t offset = slot->currentTrackRecordAddress - context->trackDataOffset;
	if (offset > context->trackDataSize || context->trackDataSize - offset < 0x22u)
		return false;
	/* TRD resource bytes; the live slot above is accessed through its typed fields. */
	*light = SlipBytes_ReadLE16(context->trdBase + offset + 0x20u);
	return true;
}

static bool SlipRacePlayer_PreviousTrackPoint(SlipRacePlayerHostBindings *context, uint16_t objectOffset,
                                              SlipView3DVec32 *point) {
	SlipRacePlayerRoadRecord road;
	uint32_t offset;
	const uint8_t *record;

	if (context == NULL || point == NULL ||
	    !SlipRacePlayer_FindRoadRecord(
	        objectOffset, context->objectTable, context->objectTableBytes, context->slotListBase,
	        context->slotListBytes, context->slotListBaseOffset, context->trdBase, context->trackDataSize,
	        context->trackDataOffset, context->componentBase, context->componentBaseBytes, context->componentBaseOffset,
	        context->trackTable, context->trackTableBytes, &road) ||
	    road.notFound) {
		return false;
	}
	offset = SlipBytes_ReadLE16(road.record + 0x02u);
	if ((size_t)offset + 0x18u > context->trackDataSize)
		return false;
	record = context->trdBase + offset;
	*point = (SlipView3DVec32){(int32_t)SlipBytes_ReadLE32(record + 0x0cu), (int32_t)SlipBytes_ReadLE32(record + 0x10u),
	                           (int32_t)SlipBytes_ReadLE32(record + 0x14u)};
	return true;
}

static uint8_t *SlipRacePlayer_TrdRecord(SlipRacePlayerHostBindings *context, uint32_t offset, size_t bytes) {
	if (context == NULL || context->trdBase == NULL || (size_t)offset + bytes > context->trackDataSize) {
		return NULL;
	}
	return context->trdBase + offset;
}

static SlipView3DVec32 SlipRacePlayer_RecordPoint(const uint8_t *record) {
	return (SlipView3DVec32){(int32_t)SlipBytes_ReadLE32(record + 0x0cu), (int32_t)SlipBytes_ReadLE32(record + 0x10u),
	                         (int32_t)SlipBytes_ReadLE32(record + 0x14u)};
}

static bool SlipRacePlayer_ForwardTrackPoint(SlipRacePlayerHostBindings *context, uint16_t objectOffset,
                                             SlipView3DVec32 *point) {
	SlipObjectPosition objectPosition;
	SlipTrackSlotRecord *trackSlot;
	uint8_t *trackRecord;
	uint8_t *linkedRecord;
	uint8_t *firstRecord;
	uint8_t *secondRecord;
	SlipView3DVec32 position;
	SlipView3DVec32 first;
	SlipView3DVec32 second;
	SlipDraw3DApproxAbsVectorLength approximate;
	uint32_t recordAddress;
	uint32_t offset;
	uint32_t firstLength;
	uint32_t secondLength;

	if (context == NULL || point == NULL ||
	    !SlipObject_Position(context->objectTable, context->objectTableBytes, objectOffset, &objectPosition)) {
		return false;
	}
	position = (SlipView3DVec32){(int32_t)objectPosition.positionX, (int32_t)objectPosition.positionY,
	                             (int32_t)objectPosition.positionZ};
	trackSlot = SlipRacePlayer_TrackSlot(context, objectOffset);
	if (trackSlot == NULL ||
	    !SlipTrackWorld_UpdateSlotRecord((uint8_t *)trackSlot, context->objectTable, context->objectTableBytes,
	                                     context->trdBase, context->trackDataSize, context->trackDataOffset,
	                                     context->componentBase, context->componentBaseBytes,
	                                     context->componentBaseOffset, context->trackTable, context->trackTableBytes)) {
		return false;
	}
	recordAddress = trackSlot->currentTrackRecordAddress;
	if (recordAddress < context->trackDataOffset)
		return false;
	offset = recordAddress - context->trackDataOffset;
	trackRecord = SlipRacePlayer_TrdRecord(context, offset, 0x20u);
	if (trackRecord == NULL)
		return false;
	trackRecord = SlipRacePlayer_TrdRecord(context, SlipBytes_ReadLE16(trackRecord + 0x1eu), 0x18u);
	if (trackRecord == NULL)
		return false;
	linkedRecord = SlipRacePlayer_TrdRecord(context, SlipBytes_ReadLE16(trackRecord + 0x02u), 0x06u);
	if (linkedRecord == NULL)
		return false;
	firstRecord = SlipRacePlayer_TrdRecord(context, SlipBytes_ReadLE16(linkedRecord + 0x00u), 0x18u);
	if (firstRecord == NULL)
		return false;
	first = SlipRacePlayer_RecordPoint(firstRecord);
	SlipDraw3D_ApproxAbsVectorLength((uint32_t)(first.x - position.x), (uint32_t)(first.y - position.y),
	                                 (uint32_t)(first.z - position.z), &approximate);
	if ((int32_t)approximate.approximateLength < 0x800) {
		firstRecord = SlipRacePlayer_TrdRecord(context, SlipBytes_ReadLE16(firstRecord + 0x00u), 0x18u);
		if (firstRecord == NULL)
			return false;
		first = SlipRacePlayer_RecordPoint(firstRecord);
	}
	offset = SlipBytes_ReadLE16(linkedRecord + 0x04u);
	if (offset == 0) {
		*point = first;
		return true;
	}
	secondRecord = SlipRacePlayer_TrdRecord(context, offset, 0x18u);
	if (secondRecord == NULL)
		return false;
	second = SlipRacePlayer_RecordPoint(secondRecord);
	SlipDraw3D_ApproxAbsVectorLength((uint32_t)(second.x - position.x), (uint32_t)(second.y - position.y),
	                                 (uint32_t)(second.z - position.z), &approximate);
	if ((int32_t)approximate.approximateLength < 0x800) {
		secondRecord = SlipRacePlayer_TrdRecord(context, SlipBytes_ReadLE16(secondRecord + 0x00u), 0x18u);
		if (secondRecord == NULL)
			return false;
		second = SlipRacePlayer_RecordPoint(secondRecord);
	}
	secondLength = SlipView3D_VectorLength(second.x - position.x, second.y - position.y, second.z - position.z);

	firstLength = SlipView3D_VectorLength(first.x - position.x, first.y - position.y, first.y - position.z);
	*point = firstLength < secondLength ? first : second;
	return true;
}

static bool SlipRacePlayer_ProjectOutFromFace(SlipRacePlayerHostBindings *context, uint32_t faceAddress,
                                              uint32_t recordAddress, uint32_t radius,
                                              SlipView3DVec32 *projectedPosition, SlipView3DVec16 *normal) {
	uint32_t faceOffset;
	uint32_t recordOffset;
	const uint8_t *faceRecord;
	const uint8_t *trackRecord;
	uint16_t componentOffset;
	const uint8_t *componentRecord;
	SlipTrackWorldPointLookup point;
	SlipView3DVec32 planeOrigin;
	SlipView3DVec32 planeNormal;
	SlipView3DVec32 projected;
	SlipView3DVec32 scaled;

	if (context == NULL || projectedPosition == NULL || normal == NULL || faceAddress < context->componentBaseOffset ||
	    recordAddress < context->trackDataOffset) {
		return false;
	}
	faceOffset = faceAddress - context->componentBaseOffset;
	recordOffset = recordAddress - context->trackDataOffset;
	if ((size_t)faceOffset + 0x0eu > context->componentBaseBytes ||
	    (size_t)recordOffset + 0x1eu > context->trackDataSize) {
		return false;
	}
	faceRecord = context->componentBase + faceOffset;
	trackRecord = context->trdBase + recordOffset;
	componentOffset = SlipBytes_ReadLE16(trackRecord + 0x02u);
	if ((size_t)componentOffset + 0x14u > context->componentBaseBytes) {
		return false;
	}
	componentRecord = context->componentBase + componentOffset;
	if (!SlipTrackWorld_PointLookup(componentRecord, context->componentBase, context->componentBaseBytes,
	                                SlipBytes_ReadLE16(faceRecord + 0x0cu), 0, 0, 0, &point) ||
	    point.carry) {
		return false;
	}
	planeOrigin =
	    (SlipView3DVec32){(int32_t)(point.pointXOrInput + SlipBytes_ReadLE32(trackRecord + 0x12u)),
	                      (int32_t)(point.pointYOrInput + SlipBytes_ReadLE32(trackRecord + 0x16u)),
	                      (int32_t)(point.pointZOrCountMergedWithInput + SlipBytes_ReadLE32(trackRecord + 0x1au))};
	*normal = (SlipView3DVec16){(int16_t)SlipBytes_ReadLE16(faceRecord + 0x02u),
	                            (int16_t)SlipBytes_ReadLE16(faceRecord + 0x04u),
	                            (int16_t)SlipBytes_ReadLE16(faceRecord + 0x06u)};
	planeNormal = (SlipView3DVec32){normal->x, normal->y, normal->z};
	projected = SlipView3D_ProjectPointToPlane(*projectedPosition, planeOrigin, planeNormal);
	scaled = SlipView3D_ScaleVector(normal->x, normal->y, normal->z, (int32_t)radius);
	projectedPosition->x = (int32_t)((uint32_t)projected.x + (uint32_t)scaled.x);
	projectedPosition->y = (int32_t)((uint32_t)projected.y + (uint32_t)scaled.y);
	projectedPosition->z = (int32_t)((uint32_t)projected.z + (uint32_t)scaled.z);
	return true;
}

static bool SlipRacePlayer_SeparateFromTrack(SlipRacePlayerHostBindings *context, uint16_t objectOffset) {
	SlipObjectPosition objectPosition;
	SlipObjectSetPosition setPosition;
	SlipTrackWorldPositiveRecordSearch search;
	SlipRaceCollisionQuery collision;
	SlipView3DNormalizeVector3D normalized;
	SlipView3DVec32 initialPosition;
	SlipView3DVec32 projectedPosition;
	SlipView3DVec32 recordOrigin = {0, 0, 0};
	SlipView3DVec16 normal = {0, 0, 0};
	SlipTrackSlotRecord *trackSlot;
	uint32_t radius;
	uint32_t attempts = 8u;

	if (context == NULL ||
	    !SlipObject_Position(context->objectTable, context->objectTableBytes, objectOffset, &objectPosition)) {
		return true;
	}
	initialPosition = (SlipView3DVec32){(int32_t)objectPosition.positionX, (int32_t)objectPosition.positionY,
	                                    (int32_t)objectPosition.positionZ};
	projectedPosition = initialPosition;
	trackSlot = SlipRacePlayer_TrackSlot(context, objectOffset);
	if (trackSlot == NULL || (trackSlot->flags & 1u) == 0) {
		return true;
	}
	radius = trackSlot->boundingRadius + 0x000001e8u;

	do {
		if (!SlipTrackWorld_PositiveRecordSearch(context->trdBase, context->trackDataSize, context->trackDataOffset,
		                                         context->componentBase, context->componentBaseBytes,
		                                         context->componentBaseOffset, context->trackTable,
		                                         context->trackTableBytes, projectedPosition.x, projectedPosition.y,
		                                         projectedPosition.z, trackSlot->currentTrackRecordAddress, &search) ||
		    search.recordAddress == 0) {
			return true;
		}
		if ((uint32_t)search.distance >= radius) {
			return false;
		}
		{
			const uint32_t recordOffset = search.recordAddress - context->trackDataOffset;
			const uint8_t *trackRecord;

			if ((size_t)recordOffset + 0x1eu > context->trackDataSize) {
				return true;
			}
			trackRecord = context->trdBase + recordOffset;
			recordOrigin = (SlipView3DVec32){(int32_t)SlipBytes_ReadLE32(trackRecord + 0x12u),
			                                 (int32_t)SlipBytes_ReadLE32(trackRecord + 0x16u),
			                                 (int32_t)SlipBytes_ReadLE32(trackRecord + 0x1au)};
		}
		if (!SlipRacePlayer_ProjectOutFromFace(context, search.faceAddress, search.recordAddress, radius,
		                                       &projectedPosition, &normal) ||
		    !SlipObject_SetPosition(context->objectTable, context->objectTableBytes, objectOffset,
		                            (uint32_t)projectedPosition.x, (uint32_t)projectedPosition.y,
		                            (uint32_t)projectedPosition.z, &setPosition)) {
			return true;
		}
		SlipRaceCollision_QueryResult(objectOffset, &collision);
		if (!collision.collisionFound) {
			if (!SlipTrackWorld_PositiveRecordSearch(
			        context->trdBase, context->trackDataSize, context->trackDataOffset, context->componentBase,
			        context->componentBaseBytes, context->componentBaseOffset, context->trackTable,
			        context->trackTableBytes, projectedPosition.x, projectedPosition.y, projectedPosition.z,
			        trackSlot->currentTrackRecordAddress, &search)) {
				return true;
			}
			if ((uint32_t)search.distance >= radius) {
				return false;
			}
		} else if ((uint16_t)collision.objectOrFlags != 0) {
			SlipView3DVec32 adjustment = SlipView3D_ScaleVector(normal.x, normal.y, normal.z, -(int32_t)(radius >> 1));

			projectedPosition.x = (int32_t)((uint32_t)projectedPosition.x + (uint32_t)adjustment.x);
			projectedPosition.y = (int32_t)((uint32_t)projectedPosition.y + (uint32_t)adjustment.y);
			projectedPosition.z = (int32_t)((uint32_t)projectedPosition.z + (uint32_t)adjustment.z);
			if (!SlipObject_SetPosition(context->objectTable, context->objectTableBytes, objectOffset,
			                            (uint32_t)projectedPosition.x, (uint32_t)projectedPosition.y,
			                            (uint32_t)projectedPosition.z, &setPosition)) {
				return true;
			}
			SlipRaceCollision_QueryResult(objectOffset, &collision);
			if (!collision.collisionFound) {
				return false;
			}
			(void)SlipObject_SetPosition(context->objectTable, context->objectTableBytes, objectOffset,
			                             (uint32_t)initialPosition.x, (uint32_t)initialPosition.y,
			                             (uint32_t)initialPosition.z, &setPosition);
			return true;
		} else if ((uint16_t)collision.trackHitMask == 0) {
			return false;
		}
	} while (--attempts != 0);

	(void)SlipObject_SetPosition(context->objectTable, context->objectTableBytes, objectOffset,
	                             (uint32_t)initialPosition.x, (uint32_t)initialPosition.y, (uint32_t)initialPosition.z,
	                             &setPosition);
	if (!SlipView3D_NormalizeVector3D((uint32_t)recordOrigin.x - (uint32_t)initialPosition.x,
	                                  (uint32_t)recordOrigin.y - (uint32_t)initialPosition.y,
	                                  (uint32_t)recordOrigin.z - (uint32_t)initialPosition.z, &normalized)) {
		return true;
	}
	{
		SlipView3DVec32 adjustment =
		    SlipView3D_ScaleVector((int16_t)(uint16_t)normalized.unitXQ14, (int16_t)(uint16_t)normalized.unitYQ14,
		                           (int16_t)(uint16_t)normalized.unitZQ14, (int32_t)(radius << 1));

		projectedPosition = (SlipView3DVec32){(int32_t)((uint32_t)initialPosition.x + (uint32_t)adjustment.x),
		                                      (int32_t)((uint32_t)initialPosition.y + (uint32_t)adjustment.y),
		                                      (int32_t)((uint32_t)initialPosition.z + (uint32_t)adjustment.z)};
	}
	if (!SlipObject_SetPosition(context->objectTable, context->objectTableBytes, objectOffset,
	                            (uint32_t)projectedPosition.x, (uint32_t)projectedPosition.y,
	                            (uint32_t)projectedPosition.z, &setPosition)) {
		return true;
	}
	SlipRaceCollision_QueryResult(objectOffset, &collision);
	if (!collision.collisionFound) {
		return false;
	}
	(void)SlipObject_SetPosition(context->objectTable, context->objectTableBytes, objectOffset,
	                             (uint32_t)initialPosition.x, (uint32_t)initialPosition.y, (uint32_t)initialPosition.z,
	                             &setPosition);
	return true;
}

static void SlipRacePlayer_ClampRecoverySpeed(SlipTrackSlotRecord *trackSlot) {
	if (trackSlot->recoverySpeed < 0x0001174c) {
		trackSlot->recoverySpeed = 0x0001174c;
	}
}

static uint32_t SlipRacePlayer_CommonEventInternal(SlipRacePlayerHostBindings *context, uint16_t eventCode,
                                                   uint16_t objectOffset, uint32_t eventPayload, uint32_t eventValue,
                                                   uint32_t eventFlags, uintptr_t dispatchData,
                                                   uint32_t dispatchFrame) {
	SlipTrackSlotRecord *trackSlot;

	if (eventCode == SLIP_OBJECT_EVENT_COLLISION_BOUNCE) {
		SlipObjectDirection direction;
		SlipView3DDotProductQ14 dot;
		SlipView3DVec32 collisionVector;
		const uint16_t collisionNormalX = (uint16_t)SlipRaceCollision_bounceEvent.normalX;
		const uint16_t collisionNormalY = (uint16_t)SlipRaceCollision_bounceEvent.normalY;
		const uint16_t collisionNormalZ = (uint16_t)SlipRaceCollision_bounceEvent.normalZ;

		trackSlot = (SlipTrackSlotRecord *)SlipRacePlayer_TrackSlot(context, objectOffset);
		if (trackSlot == NULL)
			return 0;
		direction = SlipObject_Direction(context->objectTable, objectOffset);
		SlipView3D_DotProductQ14(direction.directionXQ14, direction.directionYQ14, direction.directionZQ14,
		                         collisionNormalX, collisionNormalY, collisionNormalZ, &dot);
		if ((int16_t)(uint16_t)dot.dotProductQ14 > 0x0100) {
			const uint32_t currentSpeed = (uint32_t)SlipObject_Speed(context->objectTable, objectOffset);

			SlipObject_SetSpeed(context->objectTable, objectOffset, (uint32_t)((int32_t)currentSpeed >> 1));
			collisionVector = SlipRacePlayer_CollisionVector(

			    context, (uint16_t)dot.dotProductQ14, (uint16_t)dot.xySumHigh, (uint16_t)dot.inputZ, 0x1000u);
			SlipObject_SetDirectionQ14(context->objectTable, objectOffset, (uint16_t)collisionVector.x,
			                           (uint16_t)collisionVector.y, (uint16_t)collisionVector.z);
			return 0;
		}
		(void)SlipRacePlayer_SeparateFromTrack(context, objectOffset);
		{
			SlipObjectPosition objectPosition;
			SlipView3DVec32 forward;
			SlipView3DVec32 previous;
			SlipView3DVec32 midpoint;
			SlipView3DNormalizeVector3D normalized;

			if (!SlipObject_Position(context->objectTable, context->objectTableBytes, objectOffset, &objectPosition) ||
			    !SlipRacePlayer_ForwardTrackPoint(context, objectOffset, &forward) ||
			    !SlipRacePlayer_PreviousTrackPoint(context, objectOffset, &previous)) {
				return 0;
			}
			midpoint = (SlipView3DVec32){(int32_t)((int32_t)((uint32_t)forward.x + (uint32_t)previous.x) >> 1),
			                             (int32_t)((int32_t)((uint32_t)forward.y + (uint32_t)previous.y) >> 1),
			                             (int32_t)((int32_t)((uint32_t)forward.z + (uint32_t)previous.z) >> 1)};
			trackSlot->recoveryTarget = midpoint;
			if (!SlipView3D_NormalizeVector3D((uint32_t)midpoint.x - objectPosition.positionX,
			                                  (uint32_t)midpoint.y - objectPosition.positionY,
			                                  (uint32_t)midpoint.z - objectPosition.positionZ, &normalized)) {
				return 0;
			}
			SlipObject_SetDirectionQ14(context->objectTable, objectOffset, (uint16_t)normalized.unitXQ14,
			                           (uint16_t)normalized.unitYQ14, (uint16_t)normalized.unitZQ14);
			trackSlot->recoveryFlags &= 0xfffeu;
			SlipRacePlayer_ClampRecoverySpeed(trackSlot);
		}
		return 0;
	}
	if (eventCode == SLIP_OBJECT_EVENT_COLLISION_STOP) {
		SlipObjectEventCallback previousCallback;
		SlipRacePlayerPrivateRecord *privateState;
		SlipObjectEventCallbackWriteResult setCallback;

		trackSlot = (SlipTrackSlotRecord *)SlipRacePlayer_TrackSlot(context, objectOffset);
		if (trackSlot == NULL)
			return 0;
		SlipObject_Stop(objectOffset, eventCode, eventPayload, eventValue, eventFlags, dispatchData, dispatchFrame);
		privateState = SlipRacePlayer_PrivateState(objectOffset);
		previousCallback = privateState->recoveryPreviousCallback;
		SlipObject_SetEventCallback(context->objectTable, context->objectTableBytes, objectOffset, previousCallback,
		                            &setCallback);
		return 0;
	}
	if (eventCode == SLIP_OBJECT_EVENT_UPDATE) {
		SlipRacePlayerPrivateRecord *privateState;
		SlipObjectPosition objectPosition;
		SlipView3DNormalizeVector3D normalized;
		SlipObjectMatrixCopy matrixCopy;
		SlipObjectMatrixInstall matrixInstall;
		SlipObjectRotate rotate;
		SlipObjectEventCallbackWriteResult setCallback;
		SlipRaceCollisionQuery collision;
		SlipView3DMatrix objectTransformMatrix;
		uint16_t flags;

		(void)SlipRacePlayer_SeparateFromTrack(context, objectOffset);
		trackSlot = (SlipTrackSlotRecord *)SlipRacePlayer_TrackSlot(context, objectOffset);
		if (trackSlot == NULL)
			return 0;
		privateState = SlipRacePlayer_PrivateState(objectOffset);
		SlipRacePlayer_ClampRecoverySpeed(trackSlot);
		flags = trackSlot->recoveryFlags;
		if ((flags & 1u) != 0) {
			SlipView3DVec32 forward;
			SlipObjectDirection direction;
			SlipView3DRotateVectorTowards steeringRotation;

			if (!SlipObject_Position(context->objectTable, context->objectTableBytes, objectOffset, &objectPosition) ||
			    !SlipRacePlayer_ForwardTrackPoint(context, objectOffset, &forward) ||
			    !SlipView3D_NormalizeVector3D((uint32_t)forward.x - objectPosition.positionX,
			                                  (uint32_t)forward.y - objectPosition.positionY,
			                                  (uint32_t)forward.z - objectPosition.positionZ, &normalized)) {
				return 0;
			}
			direction = SlipObject_Direction(context->objectTable, objectOffset);
			steeringRotation = SlipView3D_RotateVectorTowards(
			    context->maths, (int16_t)direction.directionXQ14, (int16_t)direction.directionYQ14,
			    (int16_t)direction.directionZQ14, (int16_t)(uint16_t)normalized.unitXQ14,
			    (int16_t)(uint16_t)normalized.unitYQ14, (int16_t)(uint16_t)normalized.unitZQ14,
			    SlipFrameTimer_Step() << 1);
			SlipObject_SetDirectionQ14(context->objectTable, objectOffset, (uint16_t)steeringRotation.vector.x,
			                           (uint16_t)steeringRotation.vector.y, (uint16_t)steeringRotation.vector.z);
		} else {
			if (!SlipObject_Position(context->objectTable, context->objectTableBytes, objectOffset, &objectPosition) ||
			    !SlipView3D_NormalizeVector3D((uint32_t)trackSlot->recoveryTarget.x - objectPosition.positionX,
			                                  (uint32_t)trackSlot->recoveryTarget.y - objectPosition.positionY,
			                                  (uint32_t)trackSlot->recoveryTarget.z - objectPosition.positionZ,
			                                  &normalized)) {
				return 0;
			}
			SlipObject_SetDirectionQ14(context->objectTable, objectOffset, (uint16_t)normalized.unitXQ14,
			                           (uint16_t)normalized.unitYQ14, (uint16_t)normalized.unitZQ14);
		}

		flags = trackSlot->recoveryFlags;
		if ((flags & 2u) == 0) {
			const uint64_t product = (uint64_t)trackSlot->recoveryTurnRate * (uint64_t)SlipFrameTimer_Step();
			uint32_t angle = (uint32_t)(product >> 14);
			int32_t pitch;
			int32_t roll;
			SlipFrameTimerValues timer;

			if ((int32_t)angle >= 0x00010000) {
				angle = 0x0000ffffu;
			}
			SlipRaceCollision_SaveObjectTransform(objectOffset);
			if (SlipObject_MatrixCopy(context->objectTable, context->objectTableBytes, objectOffset,
			                          &objectTransformMatrix, &matrixCopy)) {
				(void)SlipView3D_LevelHeading(context->maths, &objectTransformMatrix, angle);
				(void)SlipObject_MatrixInstall(context->objectTable, context->objectTableBytes, objectOffset,
				                               &objectTransformMatrix, &matrixInstall);
			}
			SlipRaceCollision_QueryResult(objectOffset, &collision);
			if (collision.collisionFound) {
				SlipRaceCollision_RestoreObjectTransform(objectOffset);
			}

			SlipRaceCollision_SaveObjectTransform(objectOffset);
			pitch = (int32_t)angle;
			if ((trackSlot->recoveryPitchSeed & 0x8000u) != 0) {
				pitch = -pitch;
			}
			pitch = (int32_t)((uint32_t)pitch << 1);
			roll = (int32_t)angle;
			if ((trackSlot->recoveryRollSeed & 0x8000u) != 0) {
				roll = -roll;
			}
			(void)SlipObject_Rotate(context->objectTable, context->objectTableBytes, objectOffset, (int16_t)pitch, 0, 0,
			                        (int16_t)roll, context->maths, &rotate);
			SlipRaceCollision_QueryResult(objectOffset, &collision);
			if (collision.collisionFound) {
				SlipRaceCollision_RestoreObjectTransform(objectOffset);
				SlipRaceCollision_SaveObjectTransform(objectOffset);
				(void)SlipObject_Rotate(context->objectTable, context->objectTableBytes, objectOffset, (int16_t)pitch,
				                        0, 0, 0, context->maths, &rotate);
				SlipRaceCollision_QueryResult(objectOffset, &collision);
				if (collision.collisionFound) {
					SlipRaceCollision_RestoreObjectTransform(objectOffset);
				}
				SlipRaceCollision_SaveObjectTransform(objectOffset);
				(void)SlipObject_Rotate(context->objectTable, context->objectTableBytes, objectOffset, 0, 0, 0,
				                        (int16_t)roll, context->maths, &rotate);
				SlipRaceCollision_QueryResult(objectOffset, &collision);
				if (collision.collisionFound) {
					SlipRaceCollision_RestoreObjectTransform(objectOffset);
				}
			}
			timer = SlipFrameTimer_Values();
			{
				const uint16_t oldTimer = trackSlot->recoveryTimer;
				const uint16_t delta = (uint16_t)timer.deltaMilliseconds;

				trackSlot->recoveryTimer = (uint16_t)(oldTimer - delta);
				if (oldTimer < delta) {
					trackSlot->recoveryFlags |= 2u;
				}
			}
			return 0;
		}

		if (!SlipTrackWorld_UpdateSlotRecord(
		        (uint8_t *)trackSlot, context->objectTable, context->objectTableBytes, context->trdBase,
		        context->trackDataSize, context->trackDataOffset, context->componentBase, context->componentBaseBytes,
		        context->componentBaseOffset, context->trackTable, context->trackTableBytes)) {
			return 0;
		}
		if (trackSlot->currentTrackRecordAddress != trackSlot->recoveryInitialTrackRecord) {
			SlipView3DVec32 waypoints[3];
			SlipView3DMatrix targetMatrix;

			SlipRaceCollision_SaveObjectTransform(objectOffset);
			(void)SlipRacePlayer_SeparateFromTrack(context, objectOffset);
			if (SlipRacePlayer_LoadWaypoints(context, waypoints) &&
			    SlipView3D_NormalizeVector3D((uint32_t)waypoints[1].x - (uint32_t)waypoints[0].x,
			                                 (uint32_t)waypoints[1].y - (uint32_t)waypoints[0].y,
			                                 (uint32_t)waypoints[1].z - (uint32_t)waypoints[0].z, &normalized) &&
			    SlipView3D_BuildMatrixFromVector(&targetMatrix, (int16_t)(uint16_t)normalized.unitXQ14,
			                                     (int16_t)(uint16_t)normalized.unitYQ14,
			                                     (int16_t)(uint16_t)normalized.unitZQ14)) {
				(void)SlipObject_MatrixInstall(context->objectTable, context->objectTableBytes, objectOffset,
				                               &targetMatrix, &matrixInstall);
				SlipRaceCollision_QueryResult(objectOffset, &collision);
				if (collision.collisionFound) {
					SlipRaceCollision_RestoreObjectTransform(objectOffset);
				}
			}
		} else {
			SlipView3DMatrix targetMatrix;
			uint32_t rate = trackSlot->recoveryTurnRate;
			uint64_t product;
			uint32_t step;
			int16_t targetRoll;
			int16_t targetPitch;
			int32_t rollStep;
			int32_t pitchStep;
			bool complete;
			bool restorePreviousCallback;

			SlipRaceCollision_SaveObjectTransform(objectOffset);
			if (rate == 0)
				rate = 0x00010000u;
			product = (uint64_t)rate * (uint64_t)SlipFrameTimer_Step();
			step = (uint32_t)(product >> 14);
			if (!SlipView3D_BuildMatrixFromVector(&targetMatrix, trackSlot->recoveryDirectionX,
			                                      trackSlot->recoveryDirectionY, trackSlot->recoveryDirectionZ)) {
				return 0;
			}
			targetRoll = SlipView3D_RollFromMatrix(context->maths, &targetMatrix);
			targetPitch = SlipView3D_PitchFromMatrix(context->maths, &targetMatrix);
			if (targetPitch > 0x3000) {
				targetPitch = 0x3000;
			} else if (targetPitch < (int16_t)0xd000u) {
				targetPitch = (int16_t)0xd000u;
			}
			rollStep = (int32_t)step;
			pitchStep = (int32_t)step;
			if ((trackSlot->recoveryRollSeed & 0x8000u) != 0) {
				rollStep = -rollStep;
			}
			if ((trackSlot->recoveryPitchSeed & 0x8000u) != 0) {
				pitchStep = -pitchStep;
			}
			if (!SlipObject_MatrixCopy(context->objectTable, context->objectTableBytes, objectOffset,
			                           &objectTransformMatrix, &matrixCopy)) {
				return 0;
			}
			complete = SlipView3D_ApproachAngles(context->maths, &objectTransformMatrix, (uint16_t)targetRoll,
			                                     (uint16_t)targetPitch, rollStep, pitchStep);
			(void)SlipObject_MatrixInstall(context->objectTable, context->objectTableBytes, objectOffset,
			                               &objectTransformMatrix, &matrixInstall);
			SlipRaceCollision_QueryResult(objectOffset, &collision);
			restorePreviousCallback = false;
			if (collision.collisionFound) {
				SlipRaceCollision_RestoreObjectTransform(objectOffset);
				if ((uint16_t)collision.objectOrFlags == 0) {
					(void)SlipRacePlayer_SeparateFromTrack(context, objectOffset);
					SlipRaceCollision_QueryResult(objectOffset, &collision);
					if (collision.collisionFound) {
						restorePreviousCallback = true;
					}
				}
			}
			if (!restorePreviousCallback && !complete) {
				return 0;
			}
		}

		(void)SlipObject_SetEventCallback(context->objectTable, context->objectTableBytes, objectOffset,
		                                  privateState->recoveryPreviousCallback, &setCallback);
		return 0;
	}
	return UINT32_MAX;
}

static void SlipRacePlayer_InitializeImpactRecovery(SlipRacePlayerHostBindings *context, uint32_t initialSpeed,
                                                    uint16_t baseDuration, uint32_t turnRate, uint16_t objectOffset) {
	SlipTrackSlotRecord *trackSlot;
	uint32_t clampedSpeed;
	SlipObjectEventCallback oldCallback;
	SlipObjectEventCallbackWriteResult setCallback;
	SlipView3DVec32 previous;
	SlipView3DVec32 forward;
	SlipView3DNormalizeVector3D normalized;
	SlipObjectPosition objectPosition;
	uint32_t distance;
	uint16_t duration;

	if (context == NULL)
		return;
	trackSlot = (SlipTrackSlotRecord *)SlipRacePlayer_TrackSlot(context, objectOffset);
	if (trackSlot == NULL)
		return;
	clampedSpeed = initialSpeed;
	if ((int32_t)clampedSpeed < 0x0001174c)
		clampedSpeed = 0x0001174cu;
	SlipObject_SetSpeed(context->objectTable, objectOffset, clampedSpeed);
	trackSlot->recoverySpeed = (int32_t)initialSpeed;
	trackSlot->recoveryTimer = baseDuration;
	trackSlot->recoveryTurnRate = turnRate;
	trackSlot->recoveryFlags = 1u;
	if (!SlipTrackWorld_UpdateSlotRecord((uint8_t *)trackSlot, context->objectTable, context->objectTableBytes,
	                                     context->trdBase, context->trackDataSize, context->trackDataOffset,
	                                     context->componentBase, context->componentBaseBytes,
	                                     context->componentBaseOffset, context->trackTable, context->trackTableBytes)) {
		return;
	}
	trackSlot->recoveryInitialTrackRecord = trackSlot->currentTrackRecordAddress;
	trackSlot->recoveryRollSeed = (uint16_t)SlipRandom_Next();
	trackSlot->recoveryPitchSeed = (uint16_t)SlipRandom_Next();
	trackSlot->recoveryUnusedSeed = (uint16_t)SlipRandom_Next();
	oldCallback = SlipObject_Callback(objectOffset);
	SlipRacePlayer_PrivateState(objectOffset)->recoveryPreviousCallback = oldCallback;
	(void)SlipObject_SetEventCallback(context->objectTable, context->objectTableBytes, objectOffset,
	                                  SlipRacePlayer_CommonEvent, &setCallback);
	if (!SlipRacePlayer_PreviousTrackPoint(context, objectOffset, &previous) ||
	    !SlipRacePlayer_ForwardTrackPoint(context, objectOffset, &forward) ||
	    !SlipView3D_NormalizeVector3D((uint32_t)forward.x - (uint32_t)previous.x,
	                                  (uint32_t)forward.y - (uint32_t)previous.y,
	                                  (uint32_t)forward.z - (uint32_t)previous.z, &normalized)) {
		return;
	}
	trackSlot->recoveryDirectionX = (int16_t)(uint16_t)normalized.unitXQ14;
	trackSlot->recoveryDirectionY = (int16_t)(uint16_t)normalized.unitYQ14;
	trackSlot->recoveryDirectionZ = (int16_t)(uint16_t)normalized.unitZQ14;
	SlipObject_SetDirectionQ14(context->objectTable, objectOffset, (uint16_t)normalized.unitXQ14,
	                           (uint16_t)normalized.unitYQ14, (uint16_t)normalized.unitZQ14);
	if (!SlipObject_Position(context->objectTable, context->objectTableBytes, objectOffset, &objectPosition) ||
	    !SlipRacePlayer_ForwardTrackPoint(context, objectOffset, &forward)) {
		return;
	}
	distance = SlipView3D_VectorLength((int32_t)((uint32_t)forward.x - objectPosition.positionX),
	                                   (int32_t)((uint32_t)forward.y - objectPosition.positionY),
	                                   (int32_t)((uint32_t)forward.z - objectPosition.positionZ));
	duration = (uint16_t)((distance / 0x00077240u) * 0x000003e8u);
	if ((int16_t)duration > 0x1388)
		duration = 0x1388u;
	trackSlot->recoveryTimer = (uint16_t)(trackSlot->recoveryTimer + duration);
	(void)SlipRacePlayer_CommonEventInternal(context, SLIP_OBJECT_EVENT_UPDATE, objectOffset, 0, 0, 0, 0, 0);
}

uint32_t SlipRacePlayer_CommonEvent(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue, uint32_t eventFlags,
                                    uint16_t objectOffset, uintptr_t dispatchData, uint32_t dispatchFrame) {
	SlipRacePlayerHostBindings callbackContext;

	if (SlipRacePlayer_hostContext == NULL)
		return UINT32_MAX;
	callbackContext = *SlipRacePlayer_hostContext;
	callbackContext.objectOffset = objectOffset;
	return SlipRacePlayer_CommonEventInternal(&callbackContext, (uint16_t)eventCode, objectOffset, eventPayload,
	                                          eventValue, eventFlags, dispatchData, dispatchFrame);
}

uint32_t SlipRacePlayer_RivalUpdate(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue, uint32_t eventFlags,
                                    uint16_t objectOffset, uintptr_t dispatchData, uint32_t dispatchFrame) {
	SlipRacePlayerHostBindings callbackContext;

	if (SlipRacePlayer_hostContext == NULL)
		return UINT32_MAX;
	callbackContext = *SlipRacePlayer_hostContext;
	callbackContext.objectOffset = objectOffset;
	if ((uint16_t)eventCode == SLIP_OBJECT_EVENT_UPDATE &&
	    (objectOffset == SlipRacePlayer_playerOneObject || objectOffset == SlipRacePlayer_playerTwoObject)) {
		SlipSoundEffects_AddEngine(callbackContext.soundEffects, 0,
		                           SlipObject_Speed(callbackContext.objectTable, objectOffset), (int32_t)eventValue,
		                           objectOffset);
	}
	return SlipRacePlayer_CommonEventInternal(&callbackContext, (uint16_t)eventCode, objectOffset, eventPayload,
	                                          eventValue, eventFlags, dispatchData, dispatchFrame);
}

uint32_t SlipRacePlayer_ProjectileWeaponIndex(uint16_t objectOffset) {
	const SlipRacePlayerProjectileState *const state =
	    (const SlipRacePlayerProjectileState *)(const void *)SlipObject_PrivateState(objectOffset);
	return state->weaponIndex;
}

uint16_t SlipRacePlayer_ProjectileShooter(uint16_t objectOffset) {
	const SlipRacePlayerProjectileState *const state =
	    (const SlipRacePlayerProjectileState *)(const void *)SlipObject_PrivateState(objectOffset);
	return state->shooterObject;
}

uint32_t SlipConfig_DamageEnabled(void) {
	if (SlipConfig_damageOverride != -1) {
		return (uint32_t)SlipConfig_damageOverride;
	}
	return SlipConfig_damageEnabled;
}

void SlipRacePlayer_StartWreck(uint16_t object, uint16_t duration, uint16_t debrisCount) {
	SlipRacePlayerPrivateRecord *const player = SlipRacePlayer_PrivateState(object);
	SlipRaceWreckState *const state = &SlipObject_table[object / SLIP_OBJECT_DOS_STRIDE].wreckEffect;
	state->explosionCountdown = player->collisionImpulseY;
	state->explosionRange = (uint32_t)player->collisionImpulseZ;
	state->maximumSpeed = (int16_t)((uint32_t)player->speed >> 16);
	state->nextEvent = NULL;
	state->debrisCount = debrisCount;
	state->remainingLifetime = duration;
	state->remainingBounces = (uint16_t)((SlipRandom_Next() & 1u) + 2u);
	state->spinning = 0;
	SlipRaceCollision_SetBodyFlags(object, 1);
	SlipObjectEventCallbackWriteResult installed;
	(void)SlipObject_SetEventCallback(SlipObject_table, (size_t)SlipObject_count * SLIP_OBJECT_DOS_STRIDE, object,
	                                  SlipRacePlayer_FallingWreckEvent, &installed);
}

uint32_t SlipRacePlayer_FallingWreckEvent(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue,
                                          uint32_t eventFlags, uint16_t object, uintptr_t dispatchData,
                                          uint32_t dispatchFrame) {
	SlipRacePlayerHostBindings context = *SlipRacePlayer_hostContext;
	context.objectOffset = object;
	SlipRaceWreckState *const state = &context.objectTable[object / SLIP_OBJECT_DOS_STRIDE].wreckEffect;
	if ((uint16_t)eventCode == SLIP_OBJECT_EVENT_COLLISION_STOP) {
		(void)SlipObject_Stop(object, eventCode, eventPayload, eventValue, eventFlags, dispatchData, dispatchFrame);
		return 0;
	}
	if ((uint16_t)eventCode == SLIP_OBJECT_EVENT_COLLISION_BOUNCE) {
		const SlipRaceCollisionBounceEvent *const contact = &SlipRaceCollision_bounceEvent;
		SlipView3DVec16 normal = {contact->normalX, contact->normalY, contact->normalZ};
		if (normal.y < -256) {
			SlipView3DVec32 direction = SlipRacePlayer_CollisionVector(&context, (uint16_t)normal.x, (uint16_t)normal.y,
			                                                           (uint16_t)normal.z, 0x100);
			SlipObject_SetDirectionQ14(context.objectTable, object, (uint16_t)direction.x, (uint16_t)direction.y,
			                           (uint16_t)direction.z);
			return 0;
		}
		state->spinning = UINT16_MAX;
		--state->remainingBounces;
		if (state->remainingBounces != 0) {
			SlipView3DVec16 reflected = SlipRaceCollision_ReflectDirection(object, normal);
			SlipView3DNormalizeLength3D direction;
			(void)SlipView3D_NormalizeLength3D((uint16_t)reflected.x, (uint16_t)(int16_t)(reflected.y >> 1),
			                                   (uint16_t)reflected.z, &direction);
			SlipObject_SetDirectionQ14(context.objectTable, object, (uint16_t)direction.unitXQ14,
			                           (uint16_t)direction.unitYQ14, (uint16_t)direction.unitZQ14);
			uint32_t speed = (uint32_t)SlipObject_Speed(context.objectTable, object);
			speed = (uint32_t)(((uint64_t)0x3000 * speed) >> 14);
			SlipObject_SetSpeed(context.objectTable, object, speed);
			return 0;
		}
		SlipObjectDrawCallbackWriteResult draw;
		(void)SlipObject_SetSlotDrawCallback(context.objectTable, context.objectTableBytes, object, NULL, &draw);
		SlipObjectSetCallback alternateDraw;
		const uint32_t drawData = (eventCode & 0xffff0000u) | (uint16_t)normal.x;
		(void)SlipObject_SetDrawCallback(object, NULL, drawData, &alternateDraw);
		(void)SlipObject_Stop(object, eventCode, eventPayload, eventValue, eventFlags, dispatchData, dispatchFrame);

		state->nextEvent = NULL;
		state->remainingLifetime = 2000;
		state->maximumSpeed = 0;
		state->explosionRange = 0xb70;
		SlipObjectEventCallbackWriteResult installed;
		(void)SlipObject_SetEventCallback(context.objectTable, context.objectTableBytes, object,
		                                  SlipRaceEffects_WreckEvent, &installed);
		SlipRaceSession_CreateDebris(state->debrisCount, UINT32_MAX, object, dispatchFrame);
		return 0;
	}
	if ((uint16_t)eventCode == SLIP_OBJECT_EVENT_UPDATE) {
		const uint32_t step = SlipFrameTimer_Step();
		const uint32_t gravity = (uint32_t)(((uint64_t)0x7aa0 * step) >> 14);
		SlipView3DVec32 velocity = SlipObject_Velocity(context.objectTable, object);
		velocity.y = (int32_t)((uint32_t)velocity.y - gravity);
		if (velocity.y < -57200)
			velocity.y = -57200;
		SlipObjectSetDirection directed;
		(void)SlipObject_SetDirection(context.objectTable, context.objectTableBytes, object, velocity.x, velocity.y,
		                              velocity.z, &directed);
		if (state->spinning == 0) {
			uint32_t angle = SlipFrameTimer_Step() << 4;
			if ((int32_t)angle > 65535)
				angle = 65535;
			SlipRaceCollision_SaveObjectTransform(object);
			SlipView3DMatrix matrix;
			SlipObjectMatrixCopy copied;
			(void)SlipObject_MatrixCopy(context.objectTable, context.objectTableBytes, object, &matrix, &copied);
			SlipObjectDirection direction = SlipObject_Direction(context.objectTable, object);
			(void)SlipView3D_RotateForwardTowards(context.maths, &matrix, (int16_t)direction.directionXQ14,
			                                      (int16_t)direction.directionYQ14, (int16_t)direction.directionZQ14,
			                                      angle);
			SlipObjectMatrixInstall installed;
			(void)SlipObject_MatrixInstall(context.objectTable, context.objectTableBytes, object, &matrix, &installed);
			if (SlipRaceCollision_Query(object))
				SlipRaceCollision_RestoreObjectTransform(object);
		} else {
			SlipRaceCollision_SaveObjectTransform(object);
			uint32_t angle = SlipFrameTimer_Step() << 3;
			if ((int32_t)angle > 65535)
				angle = 65535;
			const int16_t rotation = (int16_t)(0u - angle);
			SlipObjectRotate rotated;
			(void)SlipObject_Rotate(context.objectTable, context.objectTableBytes, object, rotation, rotation, rotation,
			                        0, context.maths, &rotated);
			if (SlipRaceCollision_Query(object)) {
				SlipRaceCollision_RestoreObjectTransform(object);
				SlipRaceCollision_SaveObjectTransform(object);
				(void)SlipRacePlayer_SeparateFromTrack(&context, object);
				if (SlipRaceCollision_Query(object))
					SlipRaceCollision_RestoreObjectTransform(object);
			}
		}
		return 0;
	}
	if ((uint16_t)eventCode == SLIP_OBJECT_EVENT_HANDLE_ACTION)
		return 0;
	return eventCode | 0xffffu;
}

SlipRacePlayerDamage SlipRacePlayer_ApplyDamage(SlipRacePlayerHostBindings *context, uint32_t movementDamage,
                                                uint32_t handlingDamage, uint16_t objectOffset) {
	enum { SLIP_WRECK_VOICE_FIRST = 2, SLIP_WRECK_VOICE_SECOND = 3, SLIP_WRECK_VOICE_RANDOM_BIT = 1u };

	SlipRacePlayerPrivateRecord *privateState;
	SlipRaceRacerState *racerRecord;
	SlipObjectEventCallback callback;
	uint32_t totalMovementDamage = movementDamage;
	uint32_t totalHandlingDamage = handlingDamage;

	if (context == NULL) {
		return (SlipRacePlayerDamage){0, 0};
	}
	callback = SlipObject_Callback(objectOffset);
	if (callback != SlipRacePlayer_RivalUpdate && callback != SlipRacePlayer_Update) {
		return (SlipRacePlayerDamage){0, 0};
	}
	privateState = SlipRacePlayer_PrivateState(objectOffset);
	racerRecord = SlipRacePlayer_CurrentRacerFromState(context, privateState);
	if ((racerRecord->racerType != 2u && SlipConfig_DamageEnabled() == 0) || privateState->damageCooldown != 0) {
		totalMovementDamage = 0;
		totalHandlingDamage = 0;
	}
	if ((totalMovementDamage | totalHandlingDamage) != 0) {
		privateState->damageCooldown = 0x0bb8u;
		SlipRacePlayer_NotifyState(objectOffset, SlipRacePlayer_playerOneObject, SlipRacePlayer_playerTwoObject,
		                           context->transitionFrames, context->transitionDuration);
	}

	if ((int32_t)totalMovementDamage >= 0x90000) {
		SlipArticSlotPosition smokePosition;
		if (!SlipArticSlot_Position(0x6d61696eu, 0x736d6f6bu, objectOffset, context->objectTable,
		                            context->objectTableBytes, context->articSlotPool, context->articSlotPoolBytes,
		                            context->articSlotPoolOffset, context->articData, context->articDataBytes,
		                            context->articDataOffset, context->maths, &smokePosition) ||
		    smokePosition.lookupFailed)
			SlipRuntime_Fatal("RaceSlotDamage: Missing reference point.");
		SlipRaceEffects_EmitSmoke(objectOffset,
		                          (SlipView3DVec32){(int32_t)smokePosition.positionX, (int32_t)smokePosition.positionY,
		                                            (int32_t)smokePosition.positionZ},
		                          4000, &SlipRaceEffects_damageSmoke);
	}

	totalMovementDamage += racerRecord->movementDamageQ16;
	if ((int32_t)totalMovementDamage < 0)
		totalMovementDamage = 0;
	totalHandlingDamage += racerRecord->handlingDamageQ16;
	if ((int32_t)totalHandlingDamage < 0)
		totalHandlingDamage = 0;
	if ((int32_t)totalMovementDamage > 0x00640000) {
		totalMovementDamage = 0x00640000u;
		SlipRacePlayer_StartWreck(objectOffset, 5000, 15);
		SlipRaceCamera_DestroyRacer(context->cameraState, objectOffset, SlipRacePlayer_playerOneObject,
		                            SlipRacePlayer_playerTwoObject, SlipRacePlayer_thirdObject);
		const uint32_t randomChoice = SlipRandom_Next();

		if (objectOffset == SlipRacePlayer_playerOneObject) {
			uint32_t phrase = SLIP_WRECK_VOICE_FIRST;
			if ((randomChoice & SLIP_WRECK_VOICE_RANDOM_BIT) != 0)
				phrase = SLIP_WRECK_VOICE_SECOND;
			SlipGameSoundState *const sound = context->soundEffects->gameSound;
			SlipRaceVoiceCalls voiceCalls = SlipRaceVoiceHost_Calls(sound);
			SlipRaceVoice_Play(sound->digitalCard, phrase, &voiceCalls);
		}
		return (SlipRacePlayerDamage){0, 0};
	}
	if ((int32_t)totalHandlingDamage > 0x00640000) {
		totalHandlingDamage = 0x00640000u;
		SlipRacePlayer_StartWreck(objectOffset, 5000, 15);
		SlipRaceCamera_DestroyRacer(context->cameraState, objectOffset, SlipRacePlayer_playerOneObject,
		                            SlipRacePlayer_playerTwoObject, SlipRacePlayer_thirdObject);
		const uint32_t randomChoice = SlipRandom_Next();

		if (objectOffset == SlipRacePlayer_playerOneObject) {
			uint32_t phrase = SLIP_WRECK_VOICE_FIRST;
			if ((randomChoice & SLIP_WRECK_VOICE_RANDOM_BIT) != 0)
				phrase = SLIP_WRECK_VOICE_SECOND;
			SlipGameSoundState *const sound = context->soundEffects->gameSound;
			SlipRaceVoiceCalls voiceCalls = SlipRaceVoiceHost_Calls(sound);
			SlipRaceVoice_Play(sound->digitalCard, phrase, &voiceCalls);
		}
		return (SlipRacePlayerDamage){0, 0};
	}
	racerRecord = SlipRacePlayer_CurrentRacerFromState(context, privateState);
	racerRecord->movementDamageQ16 = totalMovementDamage;
	racerRecord->handlingDamageQ16 = totalHandlingDamage;
	return (SlipRacePlayerDamage){totalMovementDamage, totalHandlingDamage};
}

SlipView3DVec32 SlipRacePlayer_CollisionVector(SlipRacePlayerHostBindings *context, uint16_t normalX, uint16_t normalY,
                                               uint16_t normalZ, uint16_t rotationAngle) {
	SlipObjectDirection direction;
	SlipView3DCrossProduct firstCross;
	SlipView3DCrossProduct secondCross;
	SlipView3DNormalizeVector3D firstNormal;
	SlipView3DNormalizeVector3D secondNormal;
	SlipView3DNormalizeLength3D finalNormal;
	SlipView3DMatrix matrix;
	SlipView3DVec32 transformed;
	uint32_t negativeNormalX;
	uint32_t negativeNormalY;
	uint32_t negativeNormalZ;

	if (context == NULL || context->maths == NULL) {
		return (SlipView3DVec32){(int16_t)normalX, (int16_t)normalY, (int16_t)normalZ};
	}
	direction = SlipObject_Direction(context->objectTable, context->objectOffset);
	SlipView3D_CrossProduct(direction.directionXQ14, direction.directionYQ14, direction.directionZQ14, normalX, normalY,
	                        normalZ, &firstCross);
	if (!SlipView3D_NormalizeVector3D(firstCross.crossX, firstCross.crossY, firstCross.crossZ, &firstNormal) ||
	    ((uint16_t)firstNormal.unitXQ14 | (uint16_t)firstNormal.unitYQ14 | (uint16_t)firstNormal.unitZQ14) == 0) {
		return (SlipView3DVec32){(int16_t)normalX, (int16_t)normalY, (int16_t)normalZ};
	}
	SlipView3D_CrossProduct(firstNormal.unitXQ14, firstNormal.unitYQ14, firstNormal.unitZQ14, normalX, normalY, normalZ,
	                        &secondCross);
	if (!SlipView3D_NormalizeVector3D(secondCross.crossX, secondCross.crossY, secondCross.crossZ, &secondNormal)) {
		return (SlipView3DVec32){(int16_t)normalX, (int16_t)normalY, (int16_t)normalZ};
	}
	negativeNormalX = 0u - secondNormal.unitXQ14;
	negativeNormalY = 0u - secondNormal.unitYQ14;
	negativeNormalZ = 0u - secondNormal.unitZQ14;
	SlipView3D_BuildAxisRotation((uint16_t)SlipView3D_SinQ14(context->maths, (int16_t)rotationAngle),
	                             (uint16_t)SlipView3D_CosQ14(context->maths, (int16_t)rotationAngle),
	                             (uint16_t)firstNormal.unitXQ14, (uint16_t)firstNormal.unitYQ14,
	                             (uint16_t)firstNormal.unitZQ14, &matrix);
	transformed = SlipView3D_TransformPosition16(
	    &matrix, (SlipView3DVec32){(int16_t)negativeNormalX, (int16_t)negativeNormalY, (int16_t)negativeNormalZ});
	if (!SlipView3D_NormalizeLength3D((uint32_t)transformed.x, (uint32_t)transformed.y, (uint32_t)transformed.z,
	                                  &finalNormal)) {
		return transformed;
	}
	return (SlipView3DVec32){(int16_t)finalNormal.unitXQ14, (int16_t)finalNormal.unitYQ14,
	                         (int16_t)finalNormal.unitZQ14};
}

void SlipRacePlayer_BuildWeaponLabel(const SlipRacePlayerWeaponRecord *records, uint32_t weaponIndex,
                                     uint32_t ammunition, char label[24]) {
	strcpy(label, records[weaponIndex].displayName);
	if ((int32_t)ammunition >= 0) {
		char suffix[] = "[?]";
		suffix[1] = (char)(uint8_t)(ammunition + '0');
		strcat(label, suffix);
	}
}

uint32_t SlipRacePlayer_WeaponRechargeRate(uint32_t weaponIndex) {

	static const uint32_t rechargeByBiasedWeaponIndex[13] = {
	    0xfffc9997u, 0x800u,  0x4000u, 0x4000u, 0x4000u, 0x4000u, 0x4000u,
	    0x4000u,     0x4000u, 0x4000u, 0x4000u, 0x4000u, 0x4000u,
	};
	return rechargeByBiasedWeaponIndex[weaponIndex + 1u];
}

uint32_t SlipRacePlayer_WeaponChargeCost(const SlipRacePlayerWeaponRecord *records, uint32_t weaponIndex) {
	return records[weaponIndex].chargeCost;
}

uint32_t SlipRacePlayer_WeaponTargetRange(const SlipRacePlayerWeaponRecord *records, uint32_t weaponIndex) {
	return records[weaponIndex].targetRange;
}

SlipRacePlayerRecordValues SlipRacePlayer_ProjectileDamageValues(const SlipRacePlayerWeaponRecord *records,
                                                                 uint16_t objectOffset) {
	const SlipRacePlayerProjectileState *const state =
	    (const SlipRacePlayerProjectileState *)(const void *)SlipObject_PrivateState(objectOffset);
	const uint32_t weaponIndex = state->weaponIndex;

	return (SlipRacePlayerRecordValues){records[weaponIndex].movementDamageQ16, records[weaponIndex].handlingDamageQ16};
}

SlipRacePlayerRecordValues SlipRacePlayer_WeaponImpactDamageValues(const SlipRacePlayerWeaponRecord *records,
                                                                   uint32_t weaponIndex) {
	uint32_t shift = 1u;

	if (weaponIndex == 0) {
		if (SlipConfig_CurrentMode() == 2) {
			++shift;
		}
	}
	return (SlipRacePlayerRecordValues){records[weaponIndex].movementDamageQ16 << shift,
	                                    records[weaponIndex].handlingDamageQ16 << shift};
}

void SlipRacePlayer_NotifyState(uint16_t objectOffset, uint16_t playerOneObject, uint16_t playerTwoObject,
                                uint32_t *transitionFrames, uint32_t *transitionDuration) {
	if (objectOffset == playerOneObject) {
		*transitionFrames = 2u;
	} else if (objectOffset == playerTwoObject) {
		*transitionDuration = 2u;
	}
}

void SlipRacePlayer_NotifyTimer(uint16_t objectOffset, uint16_t playerOneObject, uint16_t playerTwoObject,
                                uint16_t *primaryViewShake, uint16_t *secondaryViewShake) {
	if (objectOffset == playerOneObject) {
		*primaryViewShake = 0x012cu;
	} else if (objectOffset == playerTwoObject) {
		*secondaryViewShake = 0x012cu;
	}
}

const SlipRaceRacerState *SlipRacePlayer_RacerState(uint16_t objectOffset) {
	const SlipObjectEventCallback callback = SlipObject_Callback(objectOffset);

	if (callback == SlipRacePlayer_RivalUpdate || callback == SlipRacePlayer_Update) {
		const SlipRacePlayerPrivateRecord *const privateState = SlipRacePlayer_PrivateState(objectOffset);

		return privateState != NULL
		           ? SlipRacePlayer_RacerFromAddress(SlipRacePlayer_hostContext, privateState->racerStateOffset)
		           : NULL;
	}
	return NULL;
}

uint32_t SlipRacePlayer_Speed(uint16_t objectOffset) {
	const SlipObjectEventCallback callback = SlipObject_Callback(objectOffset);
	if (callback == SlipRacePlayer_RivalUpdate || callback == SlipRacePlayer_Update)
		return (uint32_t)SlipObject_Speed(SlipRacePlayer_hostContext->objectTable, objectOffset);
	return 0;
}

void SlipConfig_CycleMode(void) {
	SlipConfig_fallbackMode = (int32_t)((uint32_t)SlipConfig_fallbackMode + 1u);
	if (SlipConfig_fallbackMode > 2)
		SlipConfig_fallbackMode = 0;
}

void SlipConfig_ToggleDamage(void) { SlipConfig_damageEnabled ^= 1u; }

int32_t SlipConfig_CurrentMode(void) {
	int32_t mode = SlipConfig_mode;

	if (mode == -1)
		mode = SlipConfig_fallbackMode;
	return mode;
}

const char *SlipRacePowerup_GetName(uint32_t index) { return SlipRacePowerup_records[index].powerupName; }

int32_t SlipRacePowerup_GetPrice(uint32_t index) { return SlipRacePowerup_records[index].price; }

SlipRacePowerupScales SlipRacePowerup_GetScales(uint32_t powerupIndex) {
	const SlipRacePowerupRecord *powerupRecord;

	if (powerupIndex == UINT32_MAX) {
		return (SlipRacePowerupScales){0x4000, 0x4000};
	}
	powerupRecord = &SlipRacePowerup_records[powerupIndex];
	return (SlipRacePowerupScales){powerupRecord->speedScaleQ14, powerupRecord->chargeDrainScaleQ14};
}

void SlipRacePlayer_SetController(uint16_t objectOffset, uint16_t controller) {
	SlipRacePlayer_PrivateState(objectOffset)->controller = controller;
}

void SlipRacePlayer_AdvanceState(SlipRacePlayerPrivateRecord *privateState, const SlipRaceRacerState *racerState) {
	uint32_t selection = privateState->weaponSelection;

	if (selection == 0) {
		if ((int32_t)racerState->primaryWeaponIndex >= 0) {
			selection = 1u;
		} else if ((int32_t)racerState->secondaryWeaponIndex >= 0) {
			selection = 2u;
		} else {
			selection = racerState->powerupRecord == UINT32_MAX ? 0u : 3u;
		}
	} else if (selection == 1u) {
		if ((int32_t)racerState->secondaryWeaponIndex < 0) {
			selection = racerState->powerupRecord == UINT32_MAX ? 0u : 3u;
		} else {
			selection = 2u;
		}
	} else if (selection == 2u) {
		selection = racerState->powerupRecord == UINT32_MAX ? 0u : 3u;
	} else {
		selection = 0;
	}
	privateState->weaponSelection = (uint16_t)selection;
}

bool SlipRacePlayer_CompareSpeeds(const SlipObject *objectTable, uint16_t currentObject, uint16_t otherObject) {
	const int32_t currentSpeed = SlipObject_Speed(objectTable, currentObject);
	const int32_t otherSpeed = SlipObject_Speed(objectTable, otherObject);

	return currentSpeed <= otherSpeed;
}

SlipTrackDoorRecord *SlipRacePlayer_FindTrackRecord(SlipTrackDoorRecord *trackStateRecords,
                                                    size_t trackStateRecordCount, uint32_t trackRecordAddress,
                                                    bool *notFound) {
	size_t recordIndex;

	for (recordIndex = 0; recordIndex < trackStateRecordCount; ++recordIndex) {
		SlipTrackDoorRecord *const record = &trackStateRecords[recordIndex];

		if (trackRecordAddress == record->firstTrackRecord) {
			*notFound = false;
			return record;
		}
	}
	*notFound = true;
	return NULL;
}

void SlipRacePlayer_InitializeTrackRecord(SlipTrackDoorRecord *trackStateRecord, uint32_t initialTimer,
                                          uint32_t speed) {
	trackStateRecord->direction = 0;
	trackStateRecord->speed = speed;
	trackStateRecord->endpointDelay = initialTimer;
}

uint8_t *SlipRacePlayer_SelectLinkedTrackRecord(uint8_t *trackBase, uint8_t *trackRecord, uint32_t trackBranch) {
	uint32_t linkedOffset;

	if (trackBranch != 0 && SlipBytes_ReadLE16(trackRecord + 0x04u) != 0) {
		linkedOffset = SlipBytes_ReadLE16(trackRecord + 0x04u);
	} else {
		linkedOffset = SlipBytes_ReadLE16(trackRecord);
	}
	return trackBase + linkedOffset;
}

void SlipRacePlayer_SetTrackBranch(uint32_t trackBranch, uint16_t objectOffset, const SlipObject *objectTable,
                                   size_t objectTableBytes, uint8_t *slotListBase, uint32_t slotListBaseAddress) {
	SlipTrackWorldSlotListSelect select;

	if (!SlipTrackWorld_SelectSlotListEntry(trackBranch, slotListBaseAddress, objectTable, objectTableBytes,
	                                        objectOffset, &select) ||
	    select.carry) {
		SlipRuntime_Fatal("TrackSlotSetBranch - not a track slot");
	}
	((SlipTrackSlotRecord *)(slotListBase + (select.slotAddress - slotListBaseAddress)))->trackBranch = trackBranch;
}

bool SlipRacePlayer_FindRoadRecord(uint16_t objectOffset, const SlipObject *objectTable, size_t objectTableBytes,
                                   uint8_t *slotListBase, size_t slotListBytes, uint32_t slotListBaseAddress,
                                   const uint8_t *trdBase, size_t trackDataSize, uint32_t trackDataBaseAddress,
                                   const uint8_t *componentBase, size_t componentBaseBytes,
                                   uint32_t componentBaseAddress, const uint8_t *table, size_t tableBytes,
                                   SlipRacePlayerRoadRecord *result) {
	SlipObjectPosition objectPosition;
	SlipTrackWorldSlotListSelect select;
	SlipDraw3DApproxAbsVectorLength approximate;
	uint32_t slotOffset;
	SlipTrackSlotRecord *trackSlot;
	uint32_t recordAddress;
	uint32_t recordOffset;
	uint8_t *roadRecord;
	uint32_t linkedOffset;
	uint32_t roadDeltaX;
	uint32_t roadDeltaY;
	uint32_t roadDeltaZ;

	if (result == NULL || !SlipObject_Position(objectTable, objectTableBytes, objectOffset, &objectPosition)) {
		return false;
	}
	SlipRacePlayer_trackPosition = (SlipView3DVec32){
	    (int32_t)objectPosition.positionX, (int32_t)objectPosition.positionY, (int32_t)objectPosition.positionZ};
	if (!SlipTrackWorld_SelectSlotListEntry(objectPosition.positionX, slotListBaseAddress, objectTable,
	                                        objectTableBytes, objectOffset, &select)) {
		return false;
	}
	if (select.carry) {
		*result = (SlipRacePlayerRoadRecord){NULL, 0, true};
		return true;
	}
	slotOffset = select.slotAddress - slotListBaseAddress;
	if (slotListBase == NULL || (size_t)slotOffset + sizeof(*trackSlot) > slotListBytes) {
		return false;
	}
	trackSlot = (SlipTrackSlotRecord *)(slotListBase + slotOffset);
	SlipRacePlayer_trackBranch = trackSlot->trackBranch;
	if (!SlipTrackWorld_UpdateSlotRecord((uint8_t *)trackSlot, objectTable, objectTableBytes, trdBase, trackDataSize,
	                                     trackDataBaseAddress, componentBase, componentBaseBytes, componentBaseAddress,
	                                     table, tableBytes)) {
		return false;
	}
	recordAddress = trackSlot->currentTrackRecordAddress;
	if (recordAddress < trackDataBaseAddress) {
		return false;
	}
	recordOffset = recordAddress - trackDataBaseAddress;
	if ((size_t)recordOffset + 0x20u > trackDataSize) {
		return false;
	}
	roadRecord = (uint8_t *)trdBase + recordOffset;
	linkedOffset = SlipBytes_ReadLE16(roadRecord + 0x1eu);
	if ((size_t)linkedOffset + 0x18u > trackDataSize) {
		return false;
	}
	roadRecord = (uint8_t *)trdBase + linkedOffset;
	recordAddress = trackDataBaseAddress + linkedOffset;
	if (SlipRacePlayer_trackBranch != 0) {
		const uint32_t childOffset = SlipBytes_ReadLE16(roadRecord + 0x02u);

		if ((size_t)childOffset + 0x06u > trackDataSize) {
			return false;
		}
		linkedOffset = SlipBytes_ReadLE16(trdBase + childOffset + 0x04u);
		if (linkedOffset != 0) {
			if ((size_t)linkedOffset + 0x18u > trackDataSize) {
				return false;
			}
			roadRecord = (uint8_t *)trdBase + linkedOffset;
			recordAddress = trackDataBaseAddress + linkedOffset;
		}
	}
	roadDeltaX = SlipBytes_ReadLE32(roadRecord + 0x0cu) - objectPosition.positionX;
	roadDeltaY = SlipBytes_ReadLE32(roadRecord + 0x10u) - objectPosition.positionY;
	roadDeltaZ = SlipBytes_ReadLE32(roadRecord + 0x14u) - objectPosition.positionZ;
	SlipDraw3D_ApproxAbsVectorLength(roadDeltaX, roadDeltaY, roadDeltaZ, &approximate);
	if ((int32_t)approximate.approximateLength < 0x800) {
		linkedOffset = SlipBytes_ReadLE16(roadRecord);
		if ((size_t)linkedOffset + 0x18u > trackDataSize) {
			return false;
		}
		roadRecord = (uint8_t *)trdBase + linkedOffset;
		recordAddress = trackDataBaseAddress + linkedOffset;
	}
	*result = (SlipRacePlayerRoadRecord){roadRecord, recordAddress, false};
	return true;
}

bool SlipRacePlayer_RoadCoordinates(uint16_t objectOffset, const SlipObject *objectTable, size_t objectTableBytes,
                                    uint8_t *slotListBase, size_t slotListBytes, uint32_t slotListBaseAddress,
                                    const uint8_t *trdBase, size_t trackDataSize, uint32_t trackDataBaseAddress,
                                    const uint8_t *componentBase, size_t componentBaseBytes,
                                    uint32_t componentBaseAddress, const uint8_t *table, size_t tableBytes,
                                    SlipRacePlayerRoadCoordinates *result) {
	SlipRacePlayerRoadRecord road;
	SlipView3DNormalizeVector3D normalized;
	SlipView3DCrossProduct cross;
	SlipObjectPosition objectPosition;
	SlipView3DVec32 transformed;
	uint32_t linkedOffset;
	const uint8_t *linkedRecord;
	uint32_t segmentDeltaX;
	uint32_t segmentDeltaY;
	uint32_t segmentDeltaZ;
	int16_t normalizedX;
	int16_t normalizedY;
	int16_t normalizedZ;

	if (result == NULL ||
	    !SlipRacePlayer_FindRoadRecord(objectOffset, objectTable, objectTableBytes, slotListBase, slotListBytes,
	                                   slotListBaseAddress, trdBase, trackDataSize, trackDataBaseAddress, componentBase,
	                                   componentBaseBytes, componentBaseAddress, table, tableBytes, &road) ||
	    road.notFound) {
		return false;
	}
	SlipRacePlayer_roadPosition = (SlipView3DVec32){(int32_t)SlipBytes_ReadLE32(road.record + 0x0cu),
	                                                (int32_t)SlipBytes_ReadLE32(road.record + 0x10u),
	                                                (int32_t)SlipBytes_ReadLE32(road.record + 0x14u)};
	linkedOffset = SlipBytes_ReadLE16(road.record + 0x02u);
	if ((size_t)linkedOffset + 0x18u > trackDataSize) {
		return false;
	}
	linkedRecord = trdBase + linkedOffset;
	segmentDeltaX = (uint32_t)SlipRacePlayer_roadPosition.x - SlipBytes_ReadLE32(linkedRecord + 0x0cu);
	segmentDeltaY = (uint32_t)SlipRacePlayer_roadPosition.y - SlipBytes_ReadLE32(linkedRecord + 0x10u);
	segmentDeltaZ = (uint32_t)SlipRacePlayer_roadPosition.z - SlipBytes_ReadLE32(linkedRecord + 0x14u);
	SlipView3D_NormalizeVector3D(segmentDeltaX, segmentDeltaY, segmentDeltaZ, &normalized);
	normalizedX = (int16_t)(uint16_t)normalized.unitXQ14;
	normalizedY = (int16_t)(uint16_t)normalized.unitYQ14;
	normalizedZ = (int16_t)(uint16_t)normalized.unitZQ14;
	SlipRacePlayer_roadMatrix.m[6] = normalizedX;
	SlipRacePlayer_roadMatrix.m[7] = normalizedY;
	SlipRacePlayer_roadMatrix.m[8] = normalizedZ;
	SlipRacePlayer_roadMatrix.m[0] = normalizedZ;
	SlipRacePlayer_roadMatrix.m[1] = 0;
	SlipRacePlayer_roadMatrix.m[2] = (int16_t)(uint16_t)(0u - (uint16_t)normalizedX);
	SlipView3D_CrossProduct((uint16_t)normalizedX, (uint16_t)normalizedY, (uint16_t)normalizedZ, (uint16_t)normalizedZ,
	                        0, (uint16_t)SlipRacePlayer_roadMatrix.m[2], &cross);
	SlipRacePlayer_roadMatrix.m[3] = (int16_t)((int32_t)cross.crossX >> 14);
	SlipRacePlayer_roadMatrix.m[4] = (int16_t)((int32_t)cross.crossY >> 14);
	SlipRacePlayer_roadMatrix.m[5] = (int16_t)((int32_t)cross.crossZ >> 14);
	if (!SlipObject_Position(objectTable, objectTableBytes, objectOffset, &objectPosition)) {
		return false;
	}
	transformed = SlipView3D_TransformPositionByRows(
	    &SlipRacePlayer_roadMatrix,
	    (SlipView3DVec32){(int32_t)(objectPosition.positionX - (uint32_t)SlipRacePlayer_roadPosition.x),
	                      (int32_t)(objectPosition.positionY - (uint32_t)SlipRacePlayer_roadPosition.y),
	                      (int32_t)(objectPosition.positionZ - (uint32_t)SlipRacePlayer_roadPosition.z)});
	*result = (SlipRacePlayerRoadCoordinates){transformed.x, transformed.y, transformed.z};
	return true;
}

bool SlipRacePlayer_BuildAvoidanceVector(uint16_t currentObject, uint16_t otherObject, int32_t roadDistance,
                                         const SlipObject *objectTable, size_t objectTableBytes, uint8_t *slotListBase,
                                         size_t slotListBytes, uint32_t slotListBaseAddress, const uint8_t *trdBase,
                                         size_t trackDataSize, uint32_t trackDataBaseAddress,
                                         const uint8_t *componentBase, size_t componentBaseBytes,
                                         uint32_t componentBaseAddress, const uint8_t *table, size_t tableBytes,
                                         uint8_t *articSlotPool, size_t articSlotPoolBytes,
                                         uint32_t articSlotPoolAddress, SlipRacePlayerAvoidanceVector *result) {
	SlipRacePlayerRoadCoordinates road;
	SlipView3DScaledNormalized2D scaled;
	uint32_t squareSum;
	uint32_t roadOffsetLength;
	int32_t targetExtent;
	int32_t currentExtent;
	uint32_t extentMargin;
	uint32_t remainingClearance;
	uint32_t avoidanceScale;
	uint32_t negatedRoadX;
	uint32_t negatedRoadY;

	if (result == NULL || !SlipRacePlayer_RoadCoordinates(otherObject, objectTable, objectTableBytes, slotListBase,
	                                                      slotListBytes, slotListBaseAddress, trdBase, trackDataSize,
	                                                      trackDataBaseAddress, componentBase, componentBaseBytes,
	                                                      componentBaseAddress, table, tableBytes, &road)) {
		return false;
	}
	SlipRacePlayer_roadX = road.roadX;
	SlipRacePlayer_roadY = road.roadY;
	squareSum = (uint32_t)((int32_t)(int16_t)road.roadX * (int32_t)(int16_t)road.roadX) +
	            (uint32_t)((int32_t)(int16_t)road.roadY * (int32_t)(int16_t)road.roadY);
	roadOffsetLength = SlipDraw3D_Root32(squareSum);
	if (!SlipArticSlot_GetExtent(otherObject, objectTable, objectTableBytes, articSlotPool, articSlotPoolBytes,
	                             articSlotPoolAddress, &targetExtent)) {
		return false;
	}
	extentMargin = (uint32_t)targetExtent + 0x1310u;
	avoidanceScale = extentMargin;
	extentMargin -= roadOffsetLength;
	remainingClearance = (uint32_t)roadDistance - extentMargin;
	if ((int32_t)remainingClearance < 0) {
		*result = (SlipRacePlayerAvoidanceVector){0, 0, true};
		return true;
	}
	if (!SlipArticSlot_GetExtent(currentObject, objectTable, objectTableBytes, articSlotPool, articSlotPoolBytes,
	                             articSlotPoolAddress, &currentExtent)) {
		return false;
	}
	extentMargin = (uint32_t)currentExtent + 0x1310u;
	if ((int32_t)extentMargin > (int32_t)remainingClearance) {
		*result = (SlipRacePlayerAvoidanceVector){0, 0, true};
		return true;
	}
	remainingClearance = (uint32_t)((int32_t)remainingClearance >> 1);
	avoidanceScale -= roadOffsetLength;
	avoidanceScale += remainingClearance;
	negatedRoadX = ((uint32_t)SlipRacePlayer_roadX & 0xffff0000u) | (uint16_t)(0u - (uint16_t)SlipRacePlayer_roadX);
	negatedRoadY = ((uint32_t)SlipRacePlayer_roadY & 0xffff0000u) | (uint16_t)(0u - (uint16_t)SlipRacePlayer_roadY);
	scaled = SlipView3D_ScaleNormalizedVector2D(negatedRoadX, negatedRoadY, (int32_t)avoidanceScale);
	*result = (SlipRacePlayerAvoidanceVector){scaled.scaledNormalizedX, scaled.scaledNormalizedY, false};
	return true;
}

bool SlipRacePlayer_TrackDistance(uint32_t distanceLimit, uint16_t objectOffset, const SlipObject *objectTable,
                                  size_t objectTableBytes, uint8_t *slotListBase, size_t slotListBytes,
                                  uint32_t slotListBaseAddress, uint8_t *trdBase, size_t trackDataSize,
                                  uint32_t trackDataBaseAddress, const uint8_t *componentBase,
                                  size_t componentBaseBytes, uint32_t componentBaseAddress, const uint8_t *table,
                                  size_t tableBytes, SlipRacePlayerTrackDistance *result) {
	SlipObjectPosition objectPosition;
	SlipRacePlayerRoadRecord road;
	SlipDraw3DApproxAbsVectorLength approximate;
	uint8_t *roadRecord;

	if (result == NULL)
		return false;
	SlipRacePlayer_distanceLimit = distanceLimit;
	SlipRacePlayer_distanceAccum = 0;
	SlipRacePlayer_curveAccum = 0;
	if (!SlipObject_Position(objectTable, objectTableBytes, objectOffset, &objectPosition)) {
		return false;
	}
	SlipRacePlayer_distancePosition = (SlipView3DVec32){
	    (int32_t)objectPosition.positionX, (int32_t)objectPosition.positionY, (int32_t)objectPosition.positionZ};
	if (!SlipRacePlayer_FindRoadRecord(objectOffset, objectTable, objectTableBytes, slotListBase, slotListBytes,
	                                   slotListBaseAddress, trdBase, trackDataSize, trackDataBaseAddress, componentBase,
	                                   componentBaseBytes, componentBaseAddress, table, tableBytes, &road)) {
		return false;
	}
	if (road.notFound) {
		result->notFound = true;
		return true;
	}
	roadRecord = road.record;
	for (;;) {
		uint32_t segmentDeltaX;
		uint32_t segmentDeltaY;
		uint32_t segmentDeltaZ;

		segmentDeltaX = SlipBytes_ReadLE32(roadRecord + 0x0cu) - (uint32_t)SlipRacePlayer_distancePosition.x;
		segmentDeltaY = SlipBytes_ReadLE32(roadRecord + 0x10u) - (uint32_t)SlipRacePlayer_distancePosition.y;
		segmentDeltaZ = SlipBytes_ReadLE32(roadRecord + 0x14u) - (uint32_t)SlipRacePlayer_distancePosition.z;
		SlipDraw3D_ApproxAbsVectorLength(segmentDeltaX, segmentDeltaY, segmentDeltaZ, &approximate);
		SlipRacePlayer_distanceAccum += approximate.approximateLength;
		SlipRacePlayer_distancePosition = (SlipView3DVec32){(int32_t)SlipBytes_ReadLE32(roadRecord + 0x0cu),
		                                                    (int32_t)SlipBytes_ReadLE32(roadRecord + 0x10u),
		                                                    (int32_t)SlipBytes_ReadLE32(roadRecord + 0x14u)};
		SlipRacePlayer_curveAccum += 0x8000u - SlipBytes_ReadLE16(roadRecord + 0x08u);
		if ((int32_t)SlipRacePlayer_distanceAccum >= (int32_t)SlipRacePlayer_distanceLimit) {
			result->accumulatedCurve = SlipRacePlayer_curveAccum;
			result->notFound = false;
			return true;
		}
		roadRecord = SlipRacePlayer_SelectLinkedTrackRecord(trdBase, roadRecord, SlipRacePlayer_trackBranch);
		if (roadRecord < trdBase || (size_t)(roadRecord - trdBase) + 0x18u > trackDataSize) {
			return false;
		}
	}
}

bool SlipRacePlayer_BuildAiControls(SlipRacePlayerHostBindings *context, uint16_t targetObject, uint32_t targetDistance,
                                    SlipRacePlayerControl *result) {
	SlipRacePlayerPrivateRecord *privateState;
	const SlipRaceRacerState *racerState;
	SlipView3DNormalizeVector3D normalized;
	SlipView3DCrossProduct cross;
	SlipDraw3DApproxAbsVectorLength approximate;
	SlipObjectPosition objectPosition;
	SlipObjectMatrixCopy matrixCopy;
	SlipRacePlayerTrackDistance trackDistance;
	SlipView3DVec32 transformed;
	uint32_t avoidanceX;
	uint32_t avoidanceY;
	uint64_t squareSum;
	uint32_t root;
	uint32_t threshold;
	uint32_t scale;
	uint32_t currentSpeed;
	uint32_t lookAheadOrSegmentXOrSpeedAdjustment;
	uint32_t segmentYOrCurveScale;
	uint32_t segmentDeltaZ;
	int32_t waypointOffset;
	int32_t objectExtent;
	int16_t normalizedX;
	int16_t normalizedY;
	int16_t normalizedZ;
	int16_t steering;
	int16_t pitch;
	uint16_t trackIndex;

	if (context == NULL || result == NULL || SlipRacePlayer_aiProfile == NULL) {
		return false;
	}
	SlipRacePlayer_targetObject = targetObject;
	SlipRacePlayer_targetDistance = targetDistance;
	privateState = SlipRacePlayer_PrivateState(context->objectOffset);
	racerState = SlipRacePlayer_CurrentRacerFromState(context, privateState);
	avoidanceX = (uint32_t)privateState->roadCoordinateX;
	avoidanceY = (uint32_t)privateState->roadCoordinateY;
	if ((avoidanceX | avoidanceY) != 0) {
		if (!SlipArticSlot_GetExtent(context->objectOffset, context->objectTable, context->objectTableBytes,
		                             context->articSlotPool, context->articSlotPoolBytes, context->articSlotPoolOffset,
		                             &objectExtent)) {
			return false;
		}
		threshold = (uint32_t)SlipRacePlayer_roadDistance - (uint32_t)objectExtent - 0x988u;
		squareSum = (uint64_t)((int64_t)(int32_t)avoidanceX * (int32_t)avoidanceX) +
		            (uint64_t)((int64_t)(int32_t)avoidanceY * (int32_t)avoidanceY);
		root = (uint16_t)SlipDraw3D_Root64((uint32_t)squareSum, (uint32_t)(squareSum >> 32));
		if ((int32_t)root > (int32_t)threshold) {
			scale = (uint32_t)(threshold << 16) / root;
			avoidanceX = (uint32_t)SlipRacePlayer_MultiplySignedShifted((int32_t)avoidanceX, (int32_t)scale, 16);
			avoidanceY = (uint32_t)SlipRacePlayer_MultiplySignedShifted((int32_t)avoidanceY, (int32_t)scale, 16);
			privateState->roadCoordinateX = (int32_t)avoidanceX;
			privateState->roadCoordinateY = (int32_t)avoidanceY;
		}
	}
	currentSpeed = (uint32_t)SlipObject_Speed(context->objectTable, context->objectOffset);
	lookAheadOrSegmentXOrSpeedAdjustment = (uint32_t)(((uint64_t)0x1e8u * currentSpeed) / 0x2cbu);
	lookAheadOrSegmentXOrSpeedAdjustment = (uint32_t)((int32_t)lookAheadOrSegmentXOrSpeedAdjustment >> 2);
	lookAheadOrSegmentXOrSpeedAdjustment += (uint32_t)SlipRacePlayer_roadDistance << 1;
	lookAheadOrSegmentXOrSpeedAdjustment += (uint32_t)((int32_t)SlipRacePlayer_roadDistance >> 1);
	lookAheadOrSegmentXOrSpeedAdjustment += 0x5f50u;
	SlipRacePlayer_lookAhead = (int32_t)lookAheadOrSegmentXOrSpeedAdjustment;
	if (SlipRacePlayer_waypointDistance <= SlipRacePlayer_lookAhead) {
		SlipRacePlayer_waypointPrevious = SlipRacePlayer_waypointCurrent;
		SlipRacePlayer_waypointCurrent = SlipRacePlayer_waypointNext;
	}
	lookAheadOrSegmentXOrSpeedAdjustment =
	    (uint32_t)SlipRacePlayer_waypointPrevious.x - (uint32_t)SlipRacePlayer_waypointCurrent.x;
	segmentYOrCurveScale = (uint32_t)SlipRacePlayer_waypointPrevious.y - (uint32_t)SlipRacePlayer_waypointCurrent.y;
	segmentDeltaZ = (uint32_t)SlipRacePlayer_waypointPrevious.z - (uint32_t)SlipRacePlayer_waypointCurrent.z;
	SlipView3D_NormalizeVector3D(lookAheadOrSegmentXOrSpeedAdjustment, segmentYOrCurveScale, segmentDeltaZ,
	                             &normalized);
	normalizedX = (int16_t)(uint16_t)normalized.unitXQ14;
	normalizedY = (int16_t)(uint16_t)normalized.unitYQ14;
	normalizedZ = (int16_t)(uint16_t)normalized.unitZQ14;
	waypointOffset = SlipRacePlayer_waypointDistance - SlipRacePlayer_lookAhead;
	if (waypointOffset < 0) {
		SlipDraw3D_ApproxAbsVectorLength(
		    (uint32_t)SlipRacePlayer_waypointPrevious.x - (uint32_t)SlipRacePlayer_waypointCurrent.x,
		    (uint32_t)SlipRacePlayer_waypointPrevious.y - (uint32_t)SlipRacePlayer_waypointCurrent.y,
		    (uint32_t)SlipRacePlayer_waypointPrevious.z - (uint32_t)SlipRacePlayer_waypointCurrent.z, &approximate);
		waypointOffset = (int32_t)((uint32_t)waypointOffset + approximate.approximateLength);
	}
	waypointOffset >>= 4;
	SlipRacePlayer_waypointCurrent.x =
	    (int32_t)((uint32_t)SlipRacePlayer_waypointCurrent.x +
	              (uint32_t)SlipRacePlayer_MultiplySignedShifted(normalizedX, waypointOffset, 10));
	SlipRacePlayer_waypointCurrent.y =
	    (int32_t)((uint32_t)SlipRacePlayer_waypointCurrent.y +
	              (uint32_t)SlipRacePlayer_MultiplySignedShifted(normalizedY, waypointOffset, 10));
	SlipRacePlayer_waypointCurrent.z =
	    (int32_t)((uint32_t)SlipRacePlayer_waypointCurrent.z +
	              (uint32_t)SlipRacePlayer_MultiplySignedShifted(normalizedZ, waypointOffset, 10));

	SlipRacePlayer_avoidanceAxes[0] = (int16_t)(uint16_t)(0u - (uint16_t)normalizedZ);
	SlipRacePlayer_avoidanceAxes[1] = 0;
	SlipRacePlayer_avoidanceAxes[2] = normalizedX;
	SlipView3D_CrossProduct((uint16_t)(0u - (uint16_t)normalizedX), (uint16_t)(0u - (uint16_t)normalizedY),
	                        (uint16_t)(0u - (uint16_t)normalizedZ), (uint16_t)SlipRacePlayer_avoidanceAxes[0], 0,
	                        (uint16_t)SlipRacePlayer_avoidanceAxes[2], &cross);
	SlipRacePlayer_avoidanceAxes[3] = (int16_t)((int32_t)cross.crossX >> 14);
	SlipRacePlayer_avoidanceAxes[4] = (int16_t)((int32_t)cross.crossY >> 14);
	SlipRacePlayer_avoidanceAxes[5] = (int16_t)((int32_t)cross.crossZ >> 14);
	avoidanceX = (uint32_t)privateState->roadCoordinateX;
	avoidanceY = (uint32_t)privateState->roadCoordinateY;
	SlipRacePlayer_waypointCurrent.x =
	    (int32_t)((uint32_t)SlipRacePlayer_waypointCurrent.x +
	              (uint32_t)SlipRacePlayer_MultiplySignedQ14(SlipRacePlayer_avoidanceAxes[0], (int32_t)avoidanceX) +
	              (uint32_t)SlipRacePlayer_MultiplySignedQ14(SlipRacePlayer_avoidanceAxes[3], (int32_t)avoidanceY));
	SlipRacePlayer_waypointCurrent.y =
	    (int32_t)((uint32_t)SlipRacePlayer_waypointCurrent.y +
	              (uint32_t)SlipRacePlayer_MultiplySignedQ14(SlipRacePlayer_avoidanceAxes[1], (int32_t)avoidanceX) +
	              (uint32_t)SlipRacePlayer_MultiplySignedQ14(SlipRacePlayer_avoidanceAxes[4], (int32_t)avoidanceY));
	SlipRacePlayer_waypointCurrent.z =
	    (int32_t)((uint32_t)SlipRacePlayer_waypointCurrent.z +
	              (uint32_t)SlipRacePlayer_MultiplySignedQ14(SlipRacePlayer_avoidanceAxes[2], (int32_t)avoidanceX) +
	              (uint32_t)SlipRacePlayer_MultiplySignedQ14(SlipRacePlayer_avoidanceAxes[5], (int32_t)avoidanceY));
	if (!SlipObject_Position(context->objectTable, context->objectTableBytes, context->objectOffset, &objectPosition)) {
		return false;
	}
	SlipRacePlayer_waypointDelta =
	    (SlipView3DVec32){(int32_t)((uint32_t)SlipRacePlayer_waypointCurrent.x - objectPosition.positionX),
	                      (int32_t)((uint32_t)SlipRacePlayer_waypointCurrent.y - objectPosition.positionY),
	                      (int32_t)((uint32_t)SlipRacePlayer_waypointCurrent.z - objectPosition.positionZ)};
	SlipRacePlayer_maximumSpeed = 0xae8f8;
	if (SlipRacePlayer_waypointDistance <= (int32_t)SlipRacePlayer_aiProfile->waypointDistanceThreshold) {
		if (!SlipRacePlayer_TrackDistance(
		        0x5f500, context->objectOffset, context->objectTable, context->objectTableBytes, context->slotListBase,
		        context->slotListBytes, context->slotListBaseOffset, context->trdBase, context->trackDataSize,
		        context->trackDataOffset, context->componentBase, context->componentBaseBytes,
		        context->componentBaseOffset, context->trackTable, context->trackTableBytes, &trackDistance)) {
			return false;
		}
		segmentYOrCurveScale = 0x4000u - trackDistance.accumulatedCurve;
		if ((int32_t)segmentYOrCurveScale < 0)
			segmentYOrCurveScale = 0;
		if (segmentYOrCurveScale != 0x3000u) {
			trackIndex = SlipRacePlayer_track;
			if (context->aiSpeedScale == NULL || context->aiBaseSpeed == NULL ||
			    trackIndex >= context->aiSpeedTableCount) {
				return false;
			}
			lookAheadOrSegmentXOrSpeedAdjustment =
			    (uint32_t)(((uint64_t)(uint32_t)context->aiSpeedScale[trackIndex] * segmentYOrCurveScale) >> 14);
			SlipRacePlayer_maximumSpeed =
			    (int32_t)(lookAheadOrSegmentXOrSpeedAdjustment + (uint32_t)context->aiBaseSpeed[trackIndex]);
		}
	}
	if (SlipRacePlayer_targetObject != 0 && (int32_t)SlipRacePlayer_targetDistance < 0xbea0) {
		int32_t targetSpeed = SlipObject_Speed(context->objectTable, SlipRacePlayer_targetObject);

		if ((int32_t)SlipRacePlayer_targetDistance < 0x5f50) {
			targetSpeed = (int32_t)((uint32_t)targetSpeed - 0x1beeu);
		} else if ((int32_t)SlipRacePlayer_targetDistance >= 0x9880) {
			targetSpeed = (int32_t)((uint32_t)targetSpeed + 0x138du);
		}
		if (targetSpeed < SlipRacePlayer_maximumSpeed) {
			SlipRacePlayer_maximumSpeed = targetSpeed;
		}
	}
	if ((int16_t)racerState->racePosition < (int16_t)SlipRacePlayer_minimumPosition &&
	    SlipRacePlayer_raceDistance >= 0xee480 && SlipRacePlayer_maximumSpeed > 0x345e4) {
		SlipRacePlayer_maximumSpeed = 0x345e4;
	}
	if (racerState->finished != 0 && SlipRacePlayer_maximumSpeed > 0x345e4) {
		SlipRacePlayer_maximumSpeed = 0x345e4;
	}
	currentSpeed = (uint32_t)SlipObject_Speed(context->objectTable, context->objectOffset);
	SlipRacePlayer_accelerate = SlipRacePlayer_maximumSpeed >= (int32_t)currentSpeed ? 1u : 0u;
	if (!SlipObject_MatrixCopy(context->objectTable, context->objectTableBytes, context->objectOffset,
	                           &SlipRacePlayer_steeringMatrix, &matrixCopy)) {
		return false;
	}
	SlipRacePlayer_steeringMatrix.m[1] = 0;
	SlipRacePlayer_steeringMatrix.m[3] = 0;
	SlipView3D_OrthonormalizeForwardBasis(&SlipRacePlayer_steeringMatrix);
	SlipView3D_NormalizeVector3D((uint32_t)SlipRacePlayer_waypointDelta.x, (uint32_t)SlipRacePlayer_waypointDelta.y,
	                             (uint32_t)SlipRacePlayer_waypointDelta.z, &normalized);
	transformed = SlipView3D_TransformVector(&SlipRacePlayer_steeringMatrix,
	                                         (SlipView3DVec32){(int16_t)(uint16_t)normalized.unitXQ14,
	                                                           (int16_t)(uint16_t)normalized.unitYQ14,
	                                                           (int16_t)(uint16_t)normalized.unitZQ14});
	steering = (int16_t)(uint16_t)transformed.x;
	pitch = (int16_t)(uint16_t)transformed.y;
	if (steering > 0x800)
		steering = 0x800;
	if (steering < -0x800)
		steering = -0x800;
	steering = (int16_t)(uint16_t)((uint16_t)steering << 3);
	if (pitch > 0x800)
		pitch = 0x800;
	if (pitch < -0x800)
		pitch = -0x800;
	pitch = (int16_t)(uint16_t)((uint16_t)pitch << 3);
	*result = (SlipRacePlayerControl){steering, pitch, SlipRacePlayer_accelerate != 0 ? 1u : 0u};
	return true;
}

bool SlipRacePlayer_LoadWaypoints(SlipRacePlayerHostBindings *context, SlipView3DVec32 waypoints[3]) {
	SlipRacePlayerRoadRecord road;
	uint8_t *roadRecord;
	uint8_t *linkedRecord;
	uint32_t linkedOffset;

	if (context == NULL || waypoints == NULL ||
	    !SlipRacePlayer_FindRoadRecord(
	        context->objectOffset, context->objectTable, context->objectTableBytes, context->slotListBase,
	        context->slotListBytes, context->slotListBaseOffset, context->trdBase, context->trackDataSize,
	        context->trackDataOffset, context->componentBase, context->componentBaseBytes, context->componentBaseOffset,
	        context->trackTable, context->trackTableBytes, &road) ||
	    road.notFound) {
		return false;
	}
	roadRecord = road.record;
	waypoints[1] = (SlipView3DVec32){(int32_t)SlipBytes_ReadLE32(roadRecord + 0x0cu),
	                                 (int32_t)SlipBytes_ReadLE32(roadRecord + 0x10u),
	                                 (int32_t)SlipBytes_ReadLE32(roadRecord + 0x14u)};
	linkedOffset = SlipBytes_ReadLE16(roadRecord + 0x02u);
	if ((size_t)linkedOffset + 0x18u > context->trackDataSize) {
		return false;
	}
	linkedRecord = context->trdBase + linkedOffset;
	waypoints[0] = (SlipView3DVec32){(int32_t)SlipBytes_ReadLE32(linkedRecord + 0x0cu),
	                                 (int32_t)SlipBytes_ReadLE32(linkedRecord + 0x10u),
	                                 (int32_t)SlipBytes_ReadLE32(linkedRecord + 0x14u)};
	roadRecord = SlipRacePlayer_SelectLinkedTrackRecord(context->trdBase, roadRecord, SlipRacePlayer_trackBranch);
	if (roadRecord < context->trdBase || (size_t)(roadRecord - context->trdBase) + 0x18u > context->trackDataSize) {
		return false;
	}
	waypoints[2] = (SlipView3DVec32){(int32_t)SlipBytes_ReadLE32(roadRecord + 0x0cu),
	                                 (int32_t)SlipBytes_ReadLE32(roadRecord + 0x10u),
	                                 (int32_t)SlipBytes_ReadLE32(roadRecord + 0x14u)};
	return true;
}

bool SlipRacePlayer_RoadValue(SlipRacePlayerHostBindings *context, SlipRacePlayerRoadValue *result) {
	SlipRacePlayerRoadRecord road;
	uint32_t roadValue;

	if (context == NULL || result == NULL ||
	    !SlipRacePlayer_FindRoadRecord(
	        context->objectOffset, context->objectTable, context->objectTableBytes, context->slotListBase,
	        context->slotListBytes, context->slotListBaseOffset, context->trdBase, context->trackDataSize,
	        context->trackDataOffset, context->componentBase, context->componentBaseBytes, context->componentBaseOffset,
	        context->trackTable, context->trackTableBytes, &road)) {
		return false;
	}
	if (road.notFound) {
		result->notFound = true;
		return true;
	}
	roadValue = SlipBytes_ReadLE32(road.record + 0x18u);
	if (roadValue < 0x3190u)
		roadValue = 0x3190u;
	*result = (SlipRacePlayerRoadValue){roadValue, false};
	return true;
}

bool SlipRacePlayer_GetBranchState(SlipRacePlayerHostBindings *context, uint32_t *branchState) {
	SlipTrackWorldSlotListSelect select;
	uint32_t slotOffset;
	SlipTrackSlotRecord *trackSlot;
	uint32_t recordAddress;
	uint32_t recordOffset;
	uint8_t *trackRecord;
	uint32_t linkedOffset;

	if (context == NULL || branchState == NULL ||
	    !SlipTrackWorld_SelectSlotListEntry(0, context->slotListBaseOffset, context->objectTable,
	                                        context->objectTableBytes, context->objectOffset, &select)) {
		return false;
	}
	if (select.carry) {
		SlipRuntime_Fatal("TrackSlotCheckBranch - not a track slot");
	}
	slotOffset = select.slotAddress - context->slotListBaseOffset;
	if (context->slotListBase == NULL || (size_t)slotOffset + 0xd4u > context->slotListBytes) {
		return false;
	}
	trackSlot = (SlipTrackSlotRecord *)(void *)(context->slotListBase + slotOffset);
	SlipRacePlayer_trackBranch = trackSlot->trackBranch;
	if (!SlipTrackWorld_UpdateSlotRecord((uint8_t *)(void *)trackSlot, context->objectTable, context->objectTableBytes,
	                                     context->trdBase, context->trackDataSize, context->trackDataOffset,
	                                     context->componentBase, context->componentBaseBytes,
	                                     context->componentBaseOffset, context->trackTable, context->trackTableBytes)) {
		return false;
	}
	recordAddress = trackSlot->currentTrackRecordAddress;
	if (recordAddress < context->trackDataOffset)
		return false;
	recordOffset = recordAddress - context->trackDataOffset;
	if ((size_t)recordOffset + 0x20u > context->trackDataSize)
		return false;
	trackRecord = context->trdBase + recordOffset;
	linkedOffset = SlipBytes_ReadLE16(trackRecord + 0x1eu);
	if ((size_t)linkedOffset + 0x18u > context->trackDataSize)
		return false;
	trackRecord = context->trdBase + linkedOffset;
	linkedOffset = SlipBytes_ReadLE16(trackRecord + 0x02u);
	if (linkedOffset != 0) {
		if ((size_t)linkedOffset + 0x06u > context->trackDataSize)
			return false;
		trackRecord = context->trdBase + linkedOffset;
		if (SlipBytes_ReadLE16(trackRecord + 0x04u) != 0) {
			*branchState = 1;
			return true;
		}
	} else {
		trackRecord = context->trdBase;
	}
	trackRecord = SlipRacePlayer_SelectLinkedTrackRecord(context->trdBase, trackRecord, SlipRacePlayer_trackBranch);
	if (trackRecord < context->trdBase || (size_t)(trackRecord - context->trdBase) + 0x2au > context->trackDataSize) {
		return false;
	}
	if (SlipBytes_ReadLE16(trackRecord + 0x04u) == 0) {
		*branchState = 0;
	} else if (SlipBytes_ReadLE16(trackRecord + 0x28u) == 0) {
		*branchState = 2;
	} else {
		*branchState = 3;
	}
	return true;
}

bool SlipRacePlayer_FindTrackState(SlipRacePlayerHostBindings *context, SlipRacePlayerTrackState *result) {
	SlipTrackWorldSlotListSelect select;
	bool notFound;
	uint32_t slotOffset;
	SlipTrackSlotRecord *trackSlot;
	uint32_t recordAddress;
	SlipTrackDoorRecord *trackStateRecord;

	if (context == NULL || result == NULL)
		return false;
	if (SlipRacePlayer_trackStateEnabled == 0) {
		*result = (SlipRacePlayerTrackState){NULL, true};
		return true;
	}
	if (!SlipTrackWorld_SelectSlotListEntry(0, context->slotListBaseOffset, context->objectTable,
	                                        context->objectTableBytes, context->objectOffset, &select)) {
		return false;
	}
	if (select.carry) {
		SlipRuntime_Fatal("TrackSlotFindDoor - not a track slot");
	}
	slotOffset = select.slotAddress - context->slotListBaseOffset;
	if (context->slotListBase == NULL || (size_t)slotOffset + 0xd4u > context->slotListBytes) {
		return false;
	}
	trackSlot = (SlipTrackSlotRecord *)(void *)(context->slotListBase + slotOffset);
	if (!SlipTrackWorld_UpdateSlotRecord((uint8_t *)(void *)trackSlot, context->objectTable, context->objectTableBytes,
	                                     context->trdBase, context->trackDataSize, context->trackDataOffset,
	                                     context->componentBase, context->componentBaseBytes,
	                                     context->componentBaseOffset, context->trackTable, context->trackTableBytes)) {
		return false;
	}
	recordAddress = trackSlot->currentTrackRecordAddress;
	if (context->trackStateRecords == NULL) {
		return false;
	}
	trackStateRecord = SlipRacePlayer_FindTrackRecord(context->trackStateRecords, context->trackStateRecordCount,
	                                                  recordAddress, &notFound);
	*result = (SlipRacePlayerTrackState){trackStateRecord, notFound};
	return true;
}

bool SlipRacePlayer_AiControls(SlipRacePlayerHostBindings *context, SlipRacePlayerControl *result) {
	SlipRacePlayerTrackState trackState;
	SlipRacePlayerRoadValue roadValue;
	SlipRacePlayerAvoidanceVector avoidance;
	SlipDraw3DApproxAbsVectorLength distance;
	SlipObjectPosition objectPosition;
	SlipFrameTimerValues timer;
	SlipRacePlayerControl controls;
	SlipView3DVec32 waypoints[3];
	SlipRacePlayerPrivateRecord *privateState;
	SlipRaceRacerState *racerState;
	uint32_t branchState;
	uint32_t selectedBranch;
	uint32_t randomChoice;
	uint16_t racerType;
	uint16_t targetObject;
	uint32_t targetDistance;
	bool controlsBuilt;

	if (context == NULL || result == NULL) {
		return false;
	}
	if (!SlipRacePlayer_FindTrackState(context, &trackState)) {
		return false;
	}
	if (!trackState.notFound) {
		SlipRacePlayer_InitializeTrackRecord(trackState.record, 0x3e8u, 0x53cau);
	}
	privateState = SlipRacePlayer_PrivateState(context->objectOffset);
	racerState = SlipRacePlayer_CurrentRacerFromState(context, privateState);
	racerType = racerState->tuningIndex;
	SlipRacePlayer_aiProfile = SlipRacePlayer_tuningRecords[racerType];
	if (!SlipRacePlayer_GetBranchState(context, &branchState)) {
		return false;
	}
	if (branchState == 3u) {
		selectedBranch = 1u;
		if ((int32_t)racerState->movementDamageQ16 <= 0x320000 && (int32_t)racerState->handlingDamageQ16 <= 0x320000) {
			selectedBranch = 0;
		}
		SlipRacePlayer_SetTrackBranch(selectedBranch, context->objectOffset, context->objectTable,
		                              context->objectTableBytes, context->slotListBase, context->slotListBaseOffset);
	} else if (branchState == 2u) {
		selectedBranch = 0;
		if (context->objectOffset != SlipRacePlayer_playerOneObject &&
		    context->objectOffset != SlipRacePlayer_playerTwoObject &&
		    context->objectOffset != SlipRacePlayer_thirdObject && SlipRacePlayer_middleNeighbourCandidate == 0 &&
		    racerState->racePosition != 1u &&
		    (int16_t)racerState->racePosition < (int16_t)SlipRacePlayer_minimumPosition) {
			uint16_t branchProbabilityThreshold = 0x0a00u;

			if (SlipRacePlayer_farNeighbourCandidate != 0 && SlipRacePlayer_raceDistance >= 0x77240) {
				branchProbabilityThreshold = 0x6000u;
			}
			randomChoice = SlipRandom_Next();
			if ((uint16_t)randomChoice < branchProbabilityThreshold)
				selectedBranch = 1u;
		}
		SlipRacePlayer_SetTrackBranch(selectedBranch, context->objectOffset, context->objectTable,
		                              context->objectTableBytes, context->slotListBase, context->slotListBaseOffset);
	}
	if (!SlipRacePlayer_LoadWaypoints(context, waypoints) ||
	    !SlipObject_Position(context->objectTable, context->objectTableBytes, context->objectOffset, &objectPosition)) {
		return false;
	}
	SlipRacePlayer_waypointPrevious = waypoints[0];
	SlipRacePlayer_waypointCurrent = waypoints[1];
	SlipRacePlayer_waypointNext = waypoints[2];
	SlipRacePlayer_waypointDelta =
	    (SlipView3DVec32){(int32_t)((uint32_t)SlipRacePlayer_waypointCurrent.x - objectPosition.positionX),
	                      (int32_t)((uint32_t)SlipRacePlayer_waypointCurrent.y - objectPosition.positionY),
	                      (int32_t)((uint32_t)SlipRacePlayer_waypointCurrent.z - objectPosition.positionZ)};
	SlipDraw3D_ApproxAbsVectorLength((uint32_t)SlipRacePlayer_waypointDelta.x, (uint32_t)SlipRacePlayer_waypointDelta.y,
	                                 (uint32_t)SlipRacePlayer_waypointDelta.z, &distance);
	SlipRacePlayer_waypointDistance = (int32_t)distance.approximateLength;
	if (!SlipRacePlayer_RoadValue(context, &roadValue) || roadValue.notFound) {
		return false;
	}
	SlipRacePlayer_roadDistance = (int32_t)roadValue.clampedRoadValue;
	controlsBuilt = false;

	if (privateState->roadCoordinateTimer != 0) {
		timer = SlipFrameTimer_Values();
		privateState->roadCoordinateTimer =
		    (uint16_t)(privateState->roadCoordinateTimer - (uint16_t)timer.deltaMilliseconds);
		if ((int16_t)privateState->roadCoordinateTimer >= 0) {
			if (!SlipRacePlayer_BuildAiControls(context, 0, 0, &controls)) {
				return false;
			}
			controlsBuilt = true;
		} else {
			privateState->roadCoordinateTimer = 0;
		}
	}

	targetObject = SlipRacePlayer_rivalObject;
	targetDistance = (uint32_t)SlipRacePlayer_rivalDistance;
	if (!controlsBuilt && targetObject != 0 && (int32_t)targetDistance <= 0x17d40 &&
	    !SlipRacePlayer_CompareSpeeds(context->objectTable, context->objectOffset, targetObject)) {
		if (!SlipRacePlayer_BuildAvoidanceVector(
		        context->objectOffset, targetObject, SlipRacePlayer_roadDistance, context->objectTable,
		        context->objectTableBytes, context->slotListBase, context->slotListBytes, context->slotListBaseOffset,
		        context->trdBase, context->trackDataSize, context->trackDataOffset, context->componentBase,
		        context->componentBaseBytes, context->componentBaseOffset, context->trackTable,
		        context->trackTableBytes, context->articSlotPool, context->articSlotPoolBytes,
		        context->articSlotPoolOffset, &avoidance)) {
			return false;
		}
		if (!avoidance.rejected) {
			privateState->roadCoordinateX = avoidance.roadOffsetX;
			privateState->roadCoordinateY = avoidance.roadOffsetY;
			if (!SlipRacePlayer_BuildAiControls(context, 0, targetDistance, &controls)) {
				return false;
			}
			controlsBuilt = true;
		}
	}
	if (!controlsBuilt) {
		if (SlipRacePlayer_middleNeighbourCandidate == 0) {
			privateState->roadCoordinateX = 0;
			privateState->roadCoordinateY = 0;
		}
		if (!SlipRacePlayer_BuildAiControls(context, targetObject, targetDistance, &controls)) {
			return false;
		}
	}

	if (racerState->finished == 0) {
		bool turboSelected = false;

		if (SlipRacePlayer_demoMode == 0) {
			randomChoice = SlipRandom_Next();
			if ((int16_t)(uint16_t)randomChoice <= 0x2000) {
				controls.actions |= 4u;
				turboSelected = true;
			}
		}
		if (!turboSelected && privateState->weaponSelection != 3u && privateState->targetObject != 0) {
			controls.actions |= 2u;
			privateState->weaponSelectionTimer = 0x1770u;
		}
	}

	if (privateState->targetObject == SlipRacePlayer_playerOneObject) {
		static const uint32_t targetPlayerVoices[] = {44, 45, 46, 47, 48, 49, 50, 51, 52, 53};
		SlipGameSoundState *const sound = context->soundEffects->gameSound;
		SlipRaceVoiceCalls voiceCalls = SlipRaceVoiceHost_Calls(sound);
		racerState = SlipRacePlayer_CurrentRacerFromState(context, privateState);
		SlipRaceVoice_Play(sound->digitalCard, targetPlayerVoices[racerState->tuningIndex - 1], &voiceCalls);
	}
	*result = controls;
	return true;
}

SlipRacePlayerControl SlipRacePlayer_LoadThirdControls(void) { return SlipRacePlayer_thirdControls; }

SlipRacePlayerControl SlipRacePlayer_LoadControls(SlipRacePlayerHostBindings *context, uint16_t playerIndex) {
	if (SlipRace_gameMode == 0 && SlipRacePlayer_demoAiEnabled != 0) {
		SlipRacePlayerControl controls = {0, 0, 0};

		SlipRacePlayer_AiControls(context, &controls);
		return controls;
	}
	if (playerIndex == 1u) {
		return SlipRace_controls;
	}
	return SlipRacePlayer_playerTwoControls;
}

void SlipRacePlayer_UpdateDigitalAxes(SlipRacePlayerControl *controls, uint32_t directions) {
	const uint32_t frameStep = SlipFrameTimer_Step();
	int32_t step = (int32_t)(frameStep << SLIP_CONTROL_STEP_SHIFT);
	int16_t steering;
	int16_t pitch;

	if (step > SLIP_CONTROL_AXIS_LIMIT) {
		step = SLIP_CONTROL_AXIS_LIMIT;
	}
	steering = controls->steering;
	if ((directions & SLIP_CONTROL_LEFT) != 0) {
		if (steering >= 0)
			steering = 0;
		steering = (int16_t)(uint16_t)(steering - (int16_t)step);
		if (steering < -SLIP_CONTROL_AXIS_LIMIT)
			steering = -SLIP_CONTROL_AXIS_LIMIT;
	}
	if ((directions & SLIP_CONTROL_RIGHT) != 0) {
		if (steering < 0)
			steering = 0;
		steering = (int16_t)(uint16_t)(steering + (int16_t)step);
		if (steering > SLIP_CONTROL_AXIS_LIMIT)
			steering = SLIP_CONTROL_AXIS_LIMIT;
	}
	if ((directions & SLIP_CONTROL_HORIZONTAL) == 0) {
		if (steering >= 0) {
			steering = (int16_t)(uint16_t)(steering - (int16_t)step);
			if (steering < 0)
				steering = 0;
		} else {
			steering = (int16_t)(uint16_t)(steering + (int16_t)step);
			if (steering >= 0)
				steering = 0;
		}
	}
	controls->steering = steering;

	pitch = controls->pitch;
	if ((directions & SLIP_CONTROL_UP) != 0) {
		if (pitch >= 0)
			pitch = 0;
		pitch = (int16_t)(uint16_t)(pitch - (int16_t)step);
		if (pitch < -SLIP_CONTROL_AXIS_LIMIT)
			pitch = -SLIP_CONTROL_AXIS_LIMIT;
	}
	if ((directions & SLIP_CONTROL_DOWN) != 0) {
		if (pitch < 0)
			pitch = 0;
		pitch = (int16_t)(uint16_t)(pitch + (int16_t)step);
		if (pitch > SLIP_CONTROL_AXIS_LIMIT)
			pitch = SLIP_CONTROL_AXIS_LIMIT;
	}
	if ((directions & SLIP_CONTROL_VERTICAL) == 0) {
		if (pitch >= 0) {
			pitch = (int16_t)(uint16_t)(pitch - (int16_t)step);
			if (pitch < 0)
				pitch = 0;
		} else {
			pitch = (int16_t)(uint16_t)(pitch + (int16_t)step);
			if (pitch >= 0)
				pitch = 0;
		}
	}
	controls->pitch = pitch;
}

void SlipRacePlayer_IntegrateDirection(SlipRacePlayerHostBindings *context) {
	SlipRacePlayerPrivateRecord *privateState;
	const SlipRaceRacerState *racerState;
	SlipObjectMatrixCopy matrixCopy;
	SlipObjectSetDirection directionResult;
	SlipView3DMatrix matrix;
	SlipView3DVec32 scaled;
	int32_t speed;
	int32_t factor;
	int32_t term;
	uint32_t product;

	privateState = SlipRacePlayer_PrivateState(context->objectOffset);
	racerState = SlipRacePlayer_CurrentRacerFromState(context, privateState);
	speed = privateState->speed;
	SlipObject_MatrixCopy(context->objectTable, context->objectTableBytes, context->objectOffset, &matrix, &matrixCopy);
	term = matrix.m[7];
	if (term > 0x1000)
		term = 0x1000;
	if (term < -0x1000)
		term = -0x1000;
	factor = (-term) >> 3;
	term = matrix.m[1];
	if (term < 0)
		term = -term;
	factor += 0x200 - (term >> 5);
	product = 0x28u * racerState->movementDamageQ16;
	factor += 0x1000 - (int32_t)(product >> 16);
	factor += 0x2c00;
	speed = (int32_t)((uint32_t)(((uint64_t)(uint32_t)speed * (uint32_t)factor) >> 14));
	scaled = SlipView3D_ScaleAxesQ14(matrix.m[6], matrix.m[7], matrix.m[8], speed);
	scaled.x = (int32_t)((uint32_t)scaled.x + (uint32_t)privateState->collisionImpulseX);
	scaled.y = (int32_t)((uint32_t)scaled.y + (uint32_t)privateState->collisionImpulseY);
	scaled.z = (int32_t)((uint32_t)scaled.z + (uint32_t)privateState->collisionImpulseZ);
	SlipObject_SetDirection(context->objectTable, context->objectTableBytes, context->objectOffset, scaled.x, scaled.y,
	                        scaled.z, &directionResult);
}

uint32_t SlipRacePlayer_ApplyControls(SlipRacePlayerHostBindings *context, SlipRacePlayerControl controls) {
	SlipRacePlayerPrivateRecord *privateState;
	const SlipRaceRacerState *racerState;
	const SlipRacePlayerTuningRecord *tuning;
	SlipObjectMatrixCopy matrixCopy;
	SlipObjectRotate rotateResult;
	SlipView3DMatrix matrix;
	uint16_t racerType;
	uint16_t actions;
	int16_t steering;
	int16_t effectiveSteering;
	int16_t pitch;
	int32_t speed;
	uint16_t speedFraction;
	int32_t target;
	int32_t multiplier;
	int64_t accelerationProduct;
	uint32_t accelerationLow;
	uint32_t accelerationHigh;
	uint32_t integratedSpeed;
	uint32_t fractionSum;
	int32_t maximumSpeed;
	int16_t yawAngle;
	int16_t pitchAngle;
	int16_t rollAngle;
	int16_t heading;
	int32_t modifier;
	uint32_t steeringFrameStep;
	uint32_t accelerationFrameStep;
	uint32_t impulseFrameStep;
	bool collisionCarry;

	privateState = SlipRacePlayer_PrivateState(context->objectOffset);
	racerState = SlipRacePlayer_CurrentRacerFromState(context, privateState);
	actions = controls.actions;
	steering = controls.steering;
	pitch = controls.pitch;
	if (privateState->invertedControlsTimer != 0) {
		steering = (int16_t)(uint16_t)(0u - (uint16_t)steering);
		pitch = (int16_t)(uint16_t)(0u - (uint16_t)pitch);
	}
	if (privateState->forcedAccelerateTimer != 0) {
		actions |= 1u;
	}
	if (privateState->amplifiedControlsTimer != 0 || privateState->impactPenaltyTimer != 0) {

		int32_t expandedSteering = (int32_t)steering * 16;
		int32_t expandedPitch = (int32_t)pitch * 16;
		if (expandedSteering > 0x4000)
			expandedSteering = 0x4000;
		if (expandedSteering < -0x4000)
			expandedSteering = -0x4000;
		if (expandedPitch > 0x4000)
			expandedPitch = 0x4000;
		if (expandedPitch < -0x4000)
			expandedPitch = -0x4000;
		steering = (int16_t)expandedSteering;
		pitch = (int16_t)expandedPitch;
	}
	SlipRaceCollision_SaveObjectTransform(context->objectOffset);
	steeringFrameStep = SlipFrameTimer_Step();
	racerType = racerState->tuningIndex;
	tuning = SlipRacePlayer_tuningRecords[racerType];
	speed = (int32_t)privateState->speed;
	speedFraction = privateState->speedFraction;
	if ((actions & 1u) != 0) {
		const int32_t divisor = (int32_t)tuning->maximumSpeed;
		const int64_t speed48 = (int64_t)speed * 0x10000 + speedFraction;
		int32_t quotient;
		quotient = (int32_t)(speed48 / divisor);
		target =
		    (int32_t)tuning->acceleration -
		    (int32_t)(((int64_t)quotient * ((int32_t)tuning->acceleration - (int32_t)tuning->minimumAcceleration)) >>
		              16);
	} else {
		target = -(int32_t)tuning->deceleration;
	}
	multiplier = 0x4000;
	if ((int16_t)privateState->controller == 2) {
		if (SlipRacePlayer_flybyMode == 0) {
			const int32_t mode = SlipConfig_CurrentMode();
			multiplier =
			    SlipRacePlayer_propulsionTables[mode][SlipRacePlayer_track - 1u][racerState->propulsionProfileIndex];
		}
		if (SlipRacePlayer_demoMode != 0 && context->objectOffset == SlipRacePlayer_playerOneObject &&
		    SlipRacePlayer_demoAiEnabled != 0) {
			multiplier += 0x2000;
		}
		if (SlipRacePlayer_flybyMode != 0) {
			multiplier -= SlipRacePlayer_gamePenalty[SlipRacePlayer_track];
		}
		if ((int16_t)racerState->racerType == 2 &&
		    (uint16_t)(SlipRacePlayer_minimumPosition + 1u) == racerState->racePosition &&
		    SlipRacePlayer_rivalDistance > 0x595b0) {
			const uint16_t lapNumber = racerState->lapNumber;

			const uint32_t targetController =
			    (uint32_t)privateState->targetObject | ((uint32_t)privateState->controller << 16);

			if (lapNumber != SlipRacePlayer_lapCount || (int32_t)targetController >= 0x1dc90) {
				privateState->positionBoostTimer = 0x1770u;
			}
		}
		if (privateState->impactPenaltyTimer != 0) {
			multiplier -= 0x1000;
		} else if (privateState->positionBoostTimer != 0) {
			multiplier += 0x2000;
		}
		if (SlipRacePlayer_positionBoostTimer != 0) {
			multiplier += SlipRacePlayer_positionBoost[racerState->racePosition];
		}
	}
	if (privateState->powerupSpeedTimer != 0 || privateState->powerupActive != 0) {
		SlipRacePowerupScales scales = SlipRacePowerup_GetScales(racerState->powerupRecord);

		multiplier += scales.speedScaleQ14 - 0x4000;
	}
	if (multiplier != 0x4000) {
		target = SlipRacePlayer_MultiplySignedQ14(target, multiplier);
	}
	accelerationFrameStep = SlipFrameTimer_Step();
	accelerationProduct = (int64_t)target * (int32_t)accelerationFrameStep;
	accelerationLow = (uint32_t)(((uint64_t)accelerationProduct >> 6));
	accelerationHigh = (accelerationLow >> 8) | ((uint32_t)((uint64_t)accelerationProduct >> 32) << 24);
	fractionSum = (uint32_t)speedFraction + (uint16_t)accelerationLow;
	speedFraction = (uint16_t)fractionSum;
	integratedSpeed = (uint32_t)speed + accelerationHigh + (fractionSum >> 16);
	speed = (int32_t)integratedSpeed;
	if (speed < 0) {
		speedFraction = 0;
		speed = 0;
	}
	maximumSpeed = (int32_t)tuning->maximumSpeed;
	if (multiplier != 0x4000) {
		maximumSpeed = (int32_t)(((uint64_t)(uint32_t)maximumSpeed * (uint32_t)multiplier) >> 14);
	}
	if (privateState->speedLimitTimer != 0) {
		maximumSpeed >>= 1;
	}
	if (speed > maximumSpeed) {
		speed = maximumSpeed;
		speedFraction = 0;
	}
	privateState->speed = (uint32_t)speed;
	privateState->speedFraction = speedFraction;
	SlipObject_MatrixCopy(context->objectTable, context->objectTableBytes, context->objectOffset, &matrix, &matrixCopy);
	effectiveSteering = steering;
	steering = (int16_t)(steering >> 1);
	if (steering < (int16_t)0x8100)
		steering = (int16_t)0x8100;
	if (steering > 0x7f00)
		steering = 0x7f00;
	heading = SlipView3D_HeadingFromMatrix(context->maths, &matrix);
	yawAngle = SlipRacePlayer_MultiplySignedWordsShifted((int16_t)steeringFrameStep,
	                                                     (int16_t)(uint16_t)(steering - heading), 12u);
	pitchAngle = SlipRacePlayer_MultiplySignedWordsHigh((int16_t)steeringFrameStep, effectiveSteering);

	pitchAngle = (int16_t)(uint16_t)(pitchAngle +
	                                 SlipRacePlayer_MultiplySignedWordsHigh(
	                                     (int16_t)(uint16_t)(0u - (uint16_t)matrix.m[1]), (int16_t)steeringFrameStep));
	rollAngle = SlipRacePlayer_MultiplySignedWordsHigh((int16_t)steeringFrameStep, pitch);
	modifier = -(int32_t)((uint32_t)(0x51u * racerState->handlingDamageQ16) >> 16);
	rollAngle =
	    SlipRacePlayer_MultiplySignedWordsShifted(rollAngle, (int16_t)((int32_t)tuning->rollScale + modifier), 14u);
	pitchAngle =
	    SlipRacePlayer_MultiplySignedWordsShifted(pitchAngle, (int16_t)((int32_t)tuning->pitchScale + modifier), 14u);
	if (matrix.m[7] > 0x3400 && rollAngle >= 0)
		rollAngle = 0;
	if (matrix.m[7] < (int16_t)0xcc00 && rollAngle < 0)
		rollAngle = 0;
	SlipObject_Rotate(context->objectTable, context->objectTableBytes, context->objectOffset, rollAngle, yawAngle, 0,
	                  pitchAngle, context->maths, &rotateResult);
	impulseFrameStep = SlipFrameTimer_Step();
	{
		const int32_t collisionImpulseX = (int32_t)privateState->collisionImpulseX;
		const int32_t collisionImpulseY = (int32_t)privateState->collisionImpulseY;
		const int32_t collisionImpulseZ = (int32_t)privateState->collisionImpulseZ;
		const int32_t scaledImpulseX = (int32_t)((uint32_t)collisionImpulseX << 2);
		const int32_t scaledImpulseY = (int32_t)((uint32_t)collisionImpulseY << 2);
		const int32_t scaledImpulseZ = (int32_t)((uint32_t)collisionImpulseZ << 2);

		if (scaledImpulseX != 0) {
			const int32_t collisionDragX = SlipRacePlayer_MultiplySignedQ14(scaledImpulseX, (int32_t)impulseFrameStep);
			privateState->collisionImpulseX = (uint32_t)collisionImpulseX - (uint32_t)collisionDragX;
		}
		if (scaledImpulseY != 0) {
			const int32_t collisionDragY = SlipRacePlayer_MultiplySignedQ14(scaledImpulseY, (int32_t)impulseFrameStep);
			privateState->collisionImpulseY = (uint32_t)collisionImpulseY - (uint32_t)collisionDragY;
		}
		if (scaledImpulseZ != 0) {
			const int32_t collisionDragZ = SlipRacePlayer_MultiplySignedQ14(scaledImpulseZ, (int32_t)impulseFrameStep);
			privateState->collisionImpulseZ = (uint32_t)collisionImpulseZ - (uint32_t)collisionDragZ;
		}
	}
	SlipRacePlayer_IntegrateDirection(context);
	collisionCarry = SlipRaceCollision_Query(context->objectOffset);
	if (collisionCarry) {
		SlipRaceCollision_RestoreObjectTransform(context->objectOffset);
	}
	return controls.actions;
}
