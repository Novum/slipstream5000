#ifndef SLIPSTREAM5000_RACE_PLAYER_H
#define SLIPSTREAM5000_RACE_PLAYER_H

#include "race_limits.h"
#include "sound_effects.h"
#include "track_world.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { SLIP_RACE_TURBO_UPGRADE_COUNT = 5, SLIP_RACE_BEAM_HIT_FLAG = 0x80000000u };

typedef enum SlipRaceBranchState {
	SLIP_RACE_BRANCH_NO_EXIT = 0,
	SLIP_RACE_BRANCH_LINKED_EXIT = 1,
	SLIP_RACE_BRANCH_ROUTE_CHOICE = 2,
	SLIP_RACE_BRANCH_REFUEL_CHOICE = 3
} SlipRaceBranchState;

enum { SLIP_RACE_POSITION_MASK = 0x7fff, SLIP_RACE_POSITION_FINISHED_FLAG = 0x8000 };

enum {
	SLIP_CONTROL_LEFT = 1u,
	SLIP_CONTROL_RIGHT = 2u,
	SLIP_CONTROL_UP = 4u,
	SLIP_CONTROL_DOWN = 8u,
	SLIP_CONTROL_HORIZONTAL = SLIP_CONTROL_LEFT | SLIP_CONTROL_RIGHT,
	SLIP_CONTROL_VERTICAL = SLIP_CONTROL_UP | SLIP_CONTROL_DOWN,
	SLIP_ACTION_ACCELERATE = 1u,
	SLIP_ACTION_FIRE = 2u,
	SLIP_ACTION_SELECT = 4u,
	SLIP_ACTION_PAUSE = 8u,
	SLIP_CONTROL_AXIS_LIMIT = 0x4000,
	SLIP_CONTROL_STEP_SHIFT = 2
};

/* Shared by Seeker, Super Seeker, Frag, Super Frag and Bomber. */
uint32_t SlipRacePlayer_GuidedProjectileEvent(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue,
                                              uint32_t eventFlags, uint16_t object, uintptr_t dispatchData,
                                              uint32_t dispatchFrame);

typedef struct SlipRacePlayerProjectileState {
	uint32_t weaponIndex;
	int32_t remainingTime;
	uint16_t targetObject;
	uint16_t shooterObject;
	SlipView3DVec32 position;
} SlipRacePlayerProjectileState;

typedef struct SlipRacePlayerMiniMinesEventCalls {
	void *context;
	uint32_t (*timer)(void *);
	SlipRacePlayerProjectileState *(*private)(void *, uint16_t);
	SlipView3DVec32 (*objectPosition)(void *, uint16_t);
	void (*collision)(void *, SlipView3DVec32, uint32_t, uint32_t);
	void (*removeBody)(void *, uint16_t);
	void (*free)(void *, uint16_t);
} SlipRacePlayerMiniMinesEventCalls;

uint32_t SlipRacePlayer_MiniMinesEventWithCalls(uint32_t eventCode, uint16_t object,
                                                const SlipRacePlayerMiniMinesEventCalls *calls);

uint32_t SlipRacePlayer_MiniMinesEvent(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue,
                                       uint32_t eventFlags, uint16_t object, uintptr_t dispatchData,
                                       uint32_t dispatchFrame);

typedef void (*SlipRaceWeaponFireCallback)(uint16_t shooterObject, uint16_t targetObject);

enum { SLIP_OBJECT_FLAG_BONUS = 1u, SLIP_OBJECT_FLAG_PROJECTILE = 2u, SLIP_OBJECT_FLAG_RACER = 4u };

enum { SLIP_RACE_WEAPON_MUZZLE_CENTER = 0, SLIP_RACE_WEAPON_MUZZLE_RIGHT = 1, SLIP_RACE_WEAPON_MUZZLE_LEFT = 2 };

/* Garage system bits follow the Charger, Targetter, Loader item order. */
enum { SLIP_RACE_POWERUP_RAPID_WEAPONS = 1u, SLIP_RACE_POWERUP_TARGETTER = 2u, SLIP_RACE_POWERUP_LOADER = 4u };

/* Weapon IDs index the record table; selected slots index per-player charges. */
enum {
	SLIP_RACE_WEAPON_BLASTER = 0,
	SLIP_RACE_WEAPON_DISRUPTER = 1,
	SLIP_RACE_WEAPON_FRAG = 2,
	SLIP_RACE_WEAPON_SUPER_FRAG = 3,
	SLIP_RACE_WEAPON_SEEKER = 4,
	SLIP_RACE_WEAPON_SUPER_SEEKER = 5,
	SLIP_RACE_WEAPON_AMBLER = 6,
	SLIP_RACE_WEAPON_SCRAMBLER = 7,
	SLIP_RACE_WEAPON_HYPER_NEURO = 8,
	SLIP_RACE_WEAPON_SMOKER = 9,
	SLIP_RACE_WEAPON_MINI_MINES = 10,
	SLIP_RACE_WEAPON_BOMBER = 11,
	SLIP_RACE_WEAPON_COUNT = SLIP_RACE_WEAPON_BOMBER + 1,
	SLIP_RACE_WEAPON_NAME_BYTES = 16,
	SLIP_RACE_WEAPON_LABEL_BYTES = 24,
	SLIP_RACE_WEAPON_PRICE_MODE_COUNT = 3,
	SLIP_RACE_BIASED_WEAPON_COUNT = SLIP_RACE_WEAPON_COUNT + 1
};

typedef struct SlipRacePlayerWeaponRecord {
	char displayName[SLIP_RACE_WEAPON_NAME_BYTES];
	uint32_t priceByMode[SLIP_RACE_WEAPON_PRICE_MODE_COUNT];
	uint32_t initialLoad;
	uint32_t chargeRate;
	uint32_t chargeCost;
	SlipRaceWeaponFireCallback fireCallback;
	uint32_t targetRange;
	uint32_t movementDamageQ16;
	uint32_t handlingDamageQ16;
} SlipRacePlayerWeaponRecord;

extern const SlipRacePlayerWeaponRecord SlipRacePlayer_records[SLIP_RACE_WEAPON_COUNT];
SlipView3DVec32 SlipRacePlayer_WeaponPosition(uint16_t shooter, uint32_t side);

enum { SLIP_RACE_RACER_RECORD_BYTES = 0x4e, SLIP_RACE_TUNING_RECORD_BYTES = 0x18 };

enum {
	SLIP_RACE_WEAPON_SLOT_BLASTER = 0,
	SLIP_RACE_WEAPON_SLOT_PRIMARY = 1,
	SLIP_RACE_WEAPON_SLOT_SECONDARY = 2,
	SLIP_RACE_WEAPON_CHARGE_COUNT = SLIP_RACE_WEAPON_SLOT_SECONDARY + 1,
	SLIP_RACE_WEAPON_SLOT_POWERUP = 3
};

typedef struct SlipRacePlayerPrivateRecord {
	int32_t collisionImpulseX;
	int32_t collisionImpulseY;
	int32_t collisionImpulseZ;
	int32_t speed;
	uint16_t speedFraction;
	uint16_t collisionCooldown;
	uint16_t weaponSelection;
	uint16_t weaponCharge[SLIP_RACE_WEAPON_CHARGE_COUNT];
	uint16_t targetObject;
	uint16_t controller;
	uint32_t racerStateOffset;
	uint16_t damageCooldown;
	uint16_t invertedControlsTimer;
	uint16_t speedLimitTimer;
	uint16_t forcedAccelerateTimer;
	uint16_t amplifiedControlsTimer;
	uint16_t powerupSpeedTimer;
	uint16_t weaponCooldown;
	uint16_t powerupCharge;
	uint16_t powerupActive;
	uint16_t fanPartCount;
	uint16_t jetPartCount;
	uint16_t fanAngle;
	uint8_t actionPressed;
	uint8_t previousActionPressed;
	uint16_t positionBoostTimer;
	uint16_t impactPenaltyTimer;
	int32_t roadCoordinateX;
	int32_t roadCoordinateY;
	uint16_t roadCoordinateTimer;
	uint16_t weaponSelectionTimer;
	uint16_t unusedStorage;
	SlipObjectEventCallback recoveryPreviousCallback;
} SlipRacePlayerPrivateRecord;

typedef enum SlipRacerType {
	SLIP_RACER_PLAYER_ONE = 0,
	SLIP_RACER_PLAYER_TWO = 1,
	SLIP_RACER_COMPUTER = 2,
	SLIP_RACER_LINKED_PLAYER = 3
} SlipRacerType;

typedef struct SlipRaceRacerState {
	uint16_t tuningIndex;
	uint16_t racerType;
	uint32_t bonusScore;
	uint16_t championshipPoints;
	uint16_t championshipPosition;
	uint8_t destroyed;
	uint8_t finished;
	uint32_t currentLapTime;
	uint32_t totalRaceTime;
	uint32_t bestLapTime;
	uint16_t objectOffset;
	uint32_t trackProgress;
	uint16_t lapNumber;
	uint16_t wrongWay;
	uint16_t racePosition;
	uint16_t previousRacePosition;
	uint16_t trackComponent;
	uint32_t movementDamageQ16;
	uint32_t handlingDamageQ16;
	uint32_t primaryWeaponIndex;
	uint32_t secondaryWeaponIndex;
	uint32_t primaryWeaponAmmo;
	uint32_t secondaryWeaponAmmo;
	uint32_t powerupRecord;
	uint32_t powerupFlags;
	uint32_t propulsionProfileIndex;
} SlipRaceRacerState;

typedef struct SlipRacePlayerControl {
	int16_t steering;
	int16_t pitch;
	uint16_t actions;
} SlipRacePlayerControl;

typedef struct SlipRacePowerupScales {
	int32_t speedScaleQ14;
	int32_t chargeDrainScaleQ14;
} SlipRacePowerupScales;

typedef struct SlipRacePlayerRecordValues {
	uint32_t movementDamageQ16;
	uint32_t handlingDamageQ16;
} SlipRacePlayerRecordValues;

typedef struct SlipRacePlayerDamage {
	uint32_t movementDamageQ16;
	uint32_t handlingDamageQ16;
} SlipRacePlayerDamage;

typedef struct SlipRacePlayerRoadRecord {
	uint8_t *record;
	uint32_t recordAddress;
	bool notFound;
} SlipRacePlayerRoadRecord;

typedef struct SlipRacePlayerRoadCoordinates {
	int32_t roadX;
	int32_t roadY;
	int32_t roadZ;
} SlipRacePlayerRoadCoordinates;

typedef struct SlipRacePlayerAvoidanceVector {
	int32_t roadOffsetX;
	int32_t roadOffsetY;
	bool rejected;
} SlipRacePlayerAvoidanceVector;

typedef struct SlipRacePlayerTrackDistance {
	uint32_t accumulatedCurve;
	bool notFound;
} SlipRacePlayerTrackDistance;

typedef struct SlipRacePlayerTrackState {
	SlipTrackDoorRecord *record;
	bool notFound;
} SlipRacePlayerTrackState;

typedef struct SlipRacePlayerRoadValue {
	uint32_t clampedRoadValue;
	bool notFound;
} SlipRacePlayerRoadValue;

typedef struct SlipRacePlayerTrackPoint {
	uint32_t roadPointX;
	uint32_t roadPointY;
	uint32_t roadPointZ;
	bool notFound;
} SlipRacePlayerTrackPoint;

typedef struct SlipRacePlayerNeighbours {
	uint16_t nearObject;
	uint16_t middleObject;
	uint16_t farObject;
	uint32_t nearDistance;
	uint32_t middleDistance;
	uint32_t farDistance;
	bool notFound;
} SlipRacePlayerNeighbours;

extern int32_t SlipConfig_mode;
extern int32_t SlipConfig_fallbackMode;

enum {
	SLIP_RACE_PROPULSION_MODE_COUNT = 3,
	SLIP_RACE_PROPULSION_PROFILE_COUNT = 4,
	SLIP_RACE_PROPULSION_PLAYER_PROFILE = SLIP_RACE_PROPULSION_PROFILE_COUNT - 1
};

extern const int32_t SlipRacePlayer_propulsionTables[SLIP_RACE_PROPULSION_MODE_COUNT][SLIP_RACE_TRACK_COUNT]
                                                    [SLIP_RACE_PROPULSION_PROFILE_COUNT];
extern uint32_t SlipRacePlayer_trackBranch;
extern SlipView3DVec32 SlipRacePlayer_trackPosition;
extern SlipView3DVec32 SlipRacePlayer_roadPosition;
extern SlipView3DMatrix SlipRacePlayer_roadMatrix;
extern int32_t SlipRacePlayer_roadX;
extern int32_t SlipRacePlayer_roadY;
extern uint32_t SlipRacePlayer_distanceLimit;
extern SlipView3DVec32 SlipRacePlayer_distancePosition;
extern uint32_t SlipRacePlayer_distanceAccum;
extern uint32_t SlipRacePlayer_curveAccum;
extern SlipView3DVec32 SlipRacePlayer_waypointPrevious;
extern SlipView3DVec32 SlipRacePlayer_waypointCurrent;
extern SlipView3DVec32 SlipRacePlayer_waypointNext;
extern SlipView3DVec32 SlipRacePlayer_waypointDelta;
extern int32_t SlipRacePlayer_waypointDistance;
extern int32_t SlipRacePlayer_roadDistance;

typedef struct SlipRacePlayerTuningRecord {
	uint32_t acceleration;
	uint32_t minimumAcceleration;
	uint32_t deceleration;
	uint32_t maximumSpeed;
	uint32_t rollScale;
	uint32_t pitchScale;
	uint32_t waypointDistanceThreshold;
} SlipRacePlayerTuningRecord;

extern const SlipRacePlayerTuningRecord *SlipRacePlayer_aiProfile;
extern int32_t SlipRacePlayer_rivalDistance;
extern int32_t SlipRacePlayer_raceDistance;
extern uint16_t SlipRacePlayer_rivalObject;
extern uint16_t SlipRacePlayer_farNeighbourCandidate;
extern uint16_t SlipRacePlayer_middleNeighbourCandidate;
extern int32_t SlipRacePlayer_maximumSpeed;
extern int32_t SlipRacePlayer_lookAhead;

enum {
	SLIP_RACE_AVOIDANCE_X_AXIS_OFFSET = 0,
	SLIP_RACE_AVOIDANCE_Y_AXIS_OFFSET = 3,
	SLIP_RACE_AVOIDANCE_COMPONENT_COUNT = SLIP_RACE_AVOIDANCE_Y_AXIS_OFFSET + 3
};

extern int16_t SlipRacePlayer_avoidanceAxes[SLIP_RACE_AVOIDANCE_COMPONENT_COUNT];
extern SlipView3DMatrix SlipRacePlayer_steeringMatrix;
extern uint16_t SlipRacePlayer_accelerate;
extern uint16_t SlipRacePlayer_targetObject;
extern uint32_t SlipRacePlayer_targetDistance;
extern uint16_t SlipRacePlayer_trackStateEnabled;
extern uint16_t SlipRacePlayer_minimumPosition;
extern uint16_t SlipRacePlayer_previousMinimumPosition;
extern uint32_t SlipRacePlayer_nearNeighbourDistance;
extern uint32_t SlipRacePlayer_middleNeighbourDistance;
extern uint32_t SlipRacePlayer_farNeighbourDistance;
extern uint32_t SlipRacePlayer_neighbourCandidateDistance;
extern uint16_t SlipRacePlayer_nearNeighbourObject;
extern uint16_t SlipRacePlayer_middleNeighbourObject;
extern uint16_t SlipRacePlayer_farNeighbourObject;
extern uint16_t SlipRacePlayer_neighbourDirectionX;
extern uint16_t SlipRacePlayer_neighbourDirectionY;
extern uint16_t SlipRacePlayer_neighbourDirectionZ;
extern uint16_t SlipRacePlayer_neighbourNormalX;
extern uint16_t SlipRacePlayer_neighbourNormalY;
extern uint16_t SlipRacePlayer_neighbourNormalZ;
extern const SlipRacePlayerTuningRecord *const SlipRacePlayer_tuningRecords[SLIP_RACE_DRIVER_TABLE_COUNT];
extern const int32_t SlipRacePlayer_aiBaseSpeed[SLIP_RACE_TRACK_TABLE_COUNT];
extern const int32_t SlipRacePlayer_aiSpeedScale[SLIP_RACE_TRACK_TABLE_COUNT];
extern uint32_t SlipRacePlayer_neighbourMode;
extern uint32_t SlipRacePlayer_neighbourRadius;
extern uint32_t SlipRacePlayer_neighbourNearLimit;
extern uint32_t SlipRacePlayer_neighbourFarLimit;
extern SlipView3DVec32 SlipRacePlayer_neighbourPosition;
extern SlipView3DVec32 SlipRacePlayer_neighbourCandidateRoad;
extern uint32_t SlipRacePlayer_neighbourCurrentSlot;
extern SlipView3DVec32 SlipRacePlayer_neighbourCurrentRoad;

int32_t SlipConfig_CurrentMode(void);
void SlipConfig_CycleMode(void);

void SlipRacePlayer_UpdateMinimumPosition(const SlipRaceRacerState *racerStates, size_t racerStateCount);
void SlipRacePlayer_ResetMinimumPosition(void);

void SlipRacePlayer_BuildWeaponLabel(const SlipRacePlayerWeaponRecord *records, uint32_t weaponIndex,
                                     uint32_t ammunition, char label[SLIP_RACE_WEAPON_LABEL_BYTES]);
uint32_t SlipRacePlayer_WeaponRechargeRate(uint32_t weaponIndex);

uint32_t SlipRacePlayer_WeaponChargeCost(const SlipRacePlayerWeaponRecord *records, uint32_t weaponIndex);

uint32_t SlipRacePlayer_WeaponTargetRange(const SlipRacePlayerWeaponRecord *records, uint32_t weaponIndex);

void SlipRacePlayer_NotifyState(uint16_t objectOffset, uint16_t playerOneObject, uint16_t playerTwoObject,
                                uint32_t *transitionFrames, uint32_t *transitionDuration);

void SlipRacePlayer_NotifyTimer(uint16_t objectOffset, uint16_t playerOneObject, uint16_t playerTwoObject,
                                uint16_t *primaryViewShake, uint16_t *secondaryViewShake);

const SlipRaceRacerState *SlipRacePlayer_RacerState(uint16_t objectOffset);
uint32_t SlipRacePlayer_Speed(uint16_t objectOffset);

SlipRacePlayerRecordValues SlipRacePlayer_ProjectileDamageValues(const SlipRacePlayerWeaponRecord *records,
                                                                 uint16_t objectOffset);

SlipRacePlayerRecordValues SlipRacePlayer_WeaponImpactDamageValues(const SlipRacePlayerWeaponRecord *records,
                                                                   uint32_t weaponIndex);

const char *SlipRacePowerup_GetName(uint32_t index);
int32_t SlipRacePowerup_GetPrice(uint32_t index);
SlipRacePowerupScales SlipRacePowerup_GetScales(uint32_t powerupIndex);

struct SlipRaceCameraState;

typedef struct SlipRacePlayerHostBindings {
	SlipObject *objectTable;
	size_t objectTableBytes;
	uint16_t objectOffset;
	SlipRacePlayerPrivateRecord *playerStates;
	size_t playerStateCount;
	SlipRaceRacerState *racerStates;
	size_t racerStateCount;
	uint32_t racerRecordsOffset;
	const SlipView3DMaths *maths;
	uint8_t *slotListBase;
	size_t slotListBytes;
	uint32_t slotListBaseOffset;
	uint32_t slotListSentinelOffset;
	SlipObjectDrawCallback *slotDrawCallbacks;
	size_t slotDrawCallbackCount;
	uint8_t *trdBase;
	size_t trackDataSize;
	uint32_t trackDataOffset;
	const uint8_t *trkBase;
	size_t trkBytes;
	const uint8_t *componentBase;
	size_t componentBaseBytes;
	uint32_t componentBaseOffset;
	const uint8_t *trackTable;
	size_t trackTableBytes;
	uint8_t *articSlotPool;
	size_t articSlotPoolBytes;
	uint32_t articSlotPoolOffset;
	uint8_t *articData;
	size_t articDataBytes;
	uint32_t articDataOffset;
	const SlipRacePlayerWeaponRecord *weaponRecords;
	const int32_t *aiSpeedScale;
	const int32_t *aiBaseSpeed;
	size_t aiSpeedTableCount;
	SlipTrackDoorRecord *trackStateRecords;
	size_t trackStateRecordCount;
	SlipSoundEffectsState *soundEffects;
	struct SlipRaceCameraState *cameraState;
	const uint8_t *materialTable;
	size_t materialTableBytes;

	uint32_t *transitionFrames;
	uint32_t *transitionDuration;
	uint16_t *primaryViewShake;
	uint16_t *secondaryViewShake;
} SlipRacePlayerHostBindings;

extern int32_t SlipConfig_damageOverride;
extern uint32_t SlipConfig_damageEnabled;
extern uint16_t SlipRacePlayer_damageSourceObject;
extern uint32_t SlipRacePlayer_damageSourceFlags;
extern uint16_t SlipRacePlayer_demoAiEnabled;
extern int32_t SlipRacePlayer_gamePenalty[SLIP_RACE_TRACK_TABLE_COUNT];
extern uint16_t SlipRacePlayer_playerOneObject;
extern uint16_t SlipRacePlayer_track;
extern uint32_t SlipRacePlayer_positionBoostTimer;
extern uint16_t SlipRacePlayer_lapCount;
extern uint32_t SlipRacePlayer_demoMode;
extern uint32_t SlipRacePlayer_flybyMode;
extern SlipRacePlayerControl SlipRacePlayer_playerTwoControls;
extern SlipRacePlayerControl SlipRacePlayer_thirdControls;
extern uint32_t SlipRacePlayer_refuelSection;
extern uint32_t SlipRacePlayer_aiControlsSuppressed;
extern uint16_t SlipRacePlayer_playerTwoObject;
extern uint16_t SlipRacePlayer_thirdObject;
extern uint16_t SlipRacePlayer_startCountdown;

uint32_t SlipRacePlayer_ProjectileWeaponIndex(uint16_t objectOffset);
uint16_t SlipRacePlayer_ProjectileShooter(uint16_t objectOffset);
uint32_t SlipConfig_DamageEnabled(void);
void SlipConfig_ToggleDamage(void);
void SlipRacePlayer_StartWreck(uint16_t object, uint16_t duration, uint16_t debrisCount);
uint32_t SlipRacePlayer_FallingWreckEvent(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue,
                                          uint32_t eventFlags, uint16_t object, uintptr_t dispatchData,
                                          uint32_t dispatchFrame);
SlipRacePlayerDamage SlipRacePlayer_ApplyDamage(SlipRacePlayerHostBindings *context, uint32_t movementDamage,
                                                uint32_t handlingDamage, uint16_t objectOffset);
SlipView3DVec32 SlipRacePlayer_CollisionVector(SlipRacePlayerHostBindings *context, uint16_t normalX, uint16_t normalY,
                                               uint16_t normalZ, uint16_t rotationAngle);

bool SlipRacePlayer_TrackLight(uint16_t objectOffset, uint16_t *light);
void SlipRacePlayer_BindHostContext(SlipRacePlayerHostBindings *context);
uint32_t SlipRacePlayer_Update(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue, uint32_t eventFlags,
                               uint16_t objectOffset, uintptr_t dispatchData, uint32_t dispatchFrame);

uint32_t SlipRacePlayer_CommonEvent(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue, uint32_t eventFlags,
                                    uint16_t objectOffset, uintptr_t dispatchData, uint32_t dispatchFrame);

uint32_t SlipRacePlayer_RivalUpdate(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue, uint32_t eventFlags,
                                    uint16_t objectOffset, uintptr_t dispatchData, uint32_t dispatchFrame);

uint32_t SlipRacePlayer_DisrupterEvent(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue,
                                       uint32_t eventFlags, uint16_t objectOffset, uintptr_t dispatchData,
                                       uint32_t dispatchFrame);
void SlipRacePlayer_FireSmoker(uint16_t shooter, uint16_t target);
void SlipRacePlayer_FireBlaster(uint16_t shooterObject, uint16_t targetObject);

SlipRacePlayerTrackPoint SlipRacePlayer_TrackPoint(SlipRacePlayerHostBindings *context, uint16_t objectOffset);

SlipRacePlayerNeighbours SlipRacePlayer_FindNeighbours(SlipRacePlayerHostBindings *context, uint32_t mode,
                                                       uint16_t objectId);

void SlipRacePlayer_SetController(uint16_t objectOffset, uint16_t controller);

void SlipRacePlayer_AdvanceState(SlipRacePlayerPrivateRecord *privateState, const SlipRaceRacerState *racerState);

bool SlipRacePlayer_CompareSpeeds(const SlipObject *objectTable, uint16_t currentObject, uint16_t otherObject);

SlipTrackDoorRecord *SlipRacePlayer_FindTrackRecord(SlipTrackDoorRecord *trackStateRecords,
                                                    size_t trackStateRecordCount, uint32_t trackRecordAddress,
                                                    bool *notFound);

void SlipRacePlayer_InitializeTrackRecord(SlipTrackDoorRecord *trackStateRecord, uint32_t initialTimer, uint32_t speed);

uint8_t *SlipRacePlayer_SelectLinkedTrackRecord(uint8_t *trackBase, uint8_t *trackRecord, uint32_t trackBranch);

void SlipRacePlayer_SetTrackBranch(uint32_t trackBranch, uint16_t objectOffset, const SlipObject *objectTable,
                                   size_t objectTableBytes, uint8_t *slotListBase, uint32_t slotListBaseAddress);

bool SlipRacePlayer_FindRoadRecord(uint16_t objectOffset, const SlipObject *objectTable, size_t objectTableBytes,
                                   uint8_t *slotListBase, size_t slotListBytes, uint32_t slotListBaseAddress,
                                   const uint8_t *trdBase, size_t trackDataSize, uint32_t trackDataBaseAddress,
                                   const uint8_t *componentBase, size_t componentBaseBytes,
                                   uint32_t componentBaseAddress, const uint8_t *table, size_t tableBytes,
                                   SlipRacePlayerRoadRecord *result);

bool SlipRacePlayer_RoadCoordinates(uint16_t objectOffset, const SlipObject *objectTable, size_t objectTableBytes,
                                    uint8_t *slotListBase, size_t slotListBytes, uint32_t slotListBaseAddress,
                                    const uint8_t *trdBase, size_t trackDataSize, uint32_t trackDataBaseAddress,
                                    const uint8_t *componentBase, size_t componentBaseBytes,
                                    uint32_t componentBaseAddress, const uint8_t *table, size_t tableBytes,
                                    SlipRacePlayerRoadCoordinates *result);

bool SlipRacePlayer_BuildAvoidanceVector(uint16_t currentObject, uint16_t otherObject, int32_t roadDistance,
                                         const SlipObject *objectTable, size_t objectTableBytes, uint8_t *slotListBase,
                                         size_t slotListBytes, uint32_t slotListBaseAddress, const uint8_t *trdBase,
                                         size_t trackDataSize, uint32_t trackDataBaseAddress,
                                         const uint8_t *componentBase, size_t componentBaseBytes,
                                         uint32_t componentBaseAddress, const uint8_t *table, size_t tableBytes,
                                         uint8_t *articSlotPool, size_t articSlotPoolBytes,
                                         uint32_t articSlotPoolAddress, SlipRacePlayerAvoidanceVector *result);

bool SlipRacePlayer_TrackDistance(uint32_t distanceLimit, uint16_t objectOffset, const SlipObject *objectTable,
                                  size_t objectTableBytes, uint8_t *slotListBase, size_t slotListBytes,
                                  uint32_t slotListBaseAddress, uint8_t *trdBase, size_t trackDataSize,
                                  uint32_t trackDataBaseAddress, const uint8_t *componentBase,
                                  size_t componentBaseBytes, uint32_t componentBaseAddress, const uint8_t *table,
                                  size_t tableBytes, SlipRacePlayerTrackDistance *result);

bool SlipRacePlayer_BuildAiControls(SlipRacePlayerHostBindings *context, uint16_t targetObject, uint32_t targetDistance,
                                    SlipRacePlayerControl *result);

bool SlipRacePlayer_LoadWaypoints(SlipRacePlayerHostBindings *context, SlipView3DVec32 waypoints[3]);

bool SlipRacePlayer_RoadValue(SlipRacePlayerHostBindings *context, SlipRacePlayerRoadValue *result);

bool SlipRacePlayer_GetBranchState(SlipRacePlayerHostBindings *context, uint32_t *branchState);

bool SlipRacePlayer_FindTrackState(SlipRacePlayerHostBindings *context, SlipRacePlayerTrackState *result);

SlipRacePlayerControl SlipRacePlayer_LoadThirdControls(void);

bool SlipRacePlayer_AiControls(SlipRacePlayerHostBindings *context, SlipRacePlayerControl *result);

SlipRacePlayerControl SlipRacePlayer_LoadControls(SlipRacePlayerHostBindings *context, uint16_t playerIndex);

void SlipRacePlayer_UpdateDigitalAxes(SlipRacePlayerControl *controls, uint32_t directions);

void SlipRacePlayer_IntegrateDirection(SlipRacePlayerHostBindings *context);

uint32_t SlipRacePlayer_ApplyControls(SlipRacePlayerHostBindings *context, SlipRacePlayerControl controls);

uint32_t SlipRacePlayer_ScramblerEvent(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue,
                                       uint32_t eventFlags, uint16_t object, uintptr_t dispatchData,
                                       uint32_t dispatchFrame);
uint32_t SlipRacePlayer_AmblerHyperNeuroEvent(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue,
                                              uint32_t eventFlags, uint16_t object, uintptr_t dispatchData,
                                              uint32_t dispatchFrame);
#endif
