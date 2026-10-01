#include "race.h"
#include "game_errors.h"
#include "joystick_calibration.h"

#include "draw3d.h"
#include "frame_timer.h"
#include "race_collision.h"
#include "race_effects.h"
#include "runtime.h"
#include "track_view_render.h"

uint32_t SlipRace_drawPageToggle;
SlipRacePlayerControl SlipRace_controls;
uint16_t SlipRace_reverseAccelerator;
uint32_t SlipRace_activeReverseAccelerator;
uint32_t SlipRace_menuRequest;
uint32_t SlipRace_racerCount = SLIP_RACE_RACER_COUNT;
SlipRaceRacerTable SlipRace_racerTable;
uint32_t SlipRace_gameMode;
SlipRaceType SlipRace_type = SLIP_RACE_TYPE_PRACTICE;
uint16_t SlipRace_secondPlayerEnabled;
uint32_t SlipRace_demoChaseEnabled;
uint32_t SlipRace_flybyChaseEnabled;
uint32_t SlipRace_allPowerups;
SlipRaceRacerTable *SlipRace_activeRacerTable;
uint32_t SlipRace_cloudScrollPhase;
uint32_t SlipRace_cloudScrollFinePhase;
uint16_t SlipRace_cloudScrollStep;
uint32_t SlipRace_debrisBudgetClock;
uint32_t SlipRace_debrisBudget;
uint32_t SlipRace_trackAnimationClock;
uint32_t SlipRace_refuelBeamsBuilt;
uint16_t SlipRace_playerOneFinished;
uint16_t SlipRace_playerTwoFinished;
uint16_t SlipRace_playerOneFinishDelay;
uint16_t SlipRace_playerTwoFinishDelay;
uint32_t SlipRace_alternateStartEnabled;
uint32_t SlipRace_playerSlotType;
SlipView3DMatrix SlipRace_matrix;
uint32_t SlipRace_viewIndex;

const char *const SlipRace_paletteNames[SLIP_RACE_TRACK_COUNT] = {
    "CHICAGO.PAL", "HAWAII.PAL", "TOKYO.PAL",  "NORWAY.PAL", "CAVE.PAL",
    "CAN.PAL",     "AMAZON.PAL", "LONDON.PAL", "EGYPT.PAL",  "NEWYORK.PAL"};

const char *const SlipRace_materialNames[SLIP_RACE_TRACK_COUNT] = {
    "CHICAGO.MAT", "HAWAII.MAT", "TOKYO.MAT",  "NORWAY.MAT", "CAVE.MAT",
    "CAN.MAT",     "AMAZON.MAT", "LONDON.MAT", "EGYPT.MAT",  "NEWYORK.MAT"};

const char *const SlipRace_trackNames[SLIP_RACE_TRACK_COUNT] = {"CHICAGO.TRK", "HAWAII.TRK", "TOKYO.TRK",  "NORWAY.TRK",
                                                                "CAVE.TRK",    "CAN.TRK",    "AMAZON.TRK", "LONDON.TRK",
                                                                "EGYPT.TRK",   "NEWYORK.TRK"};

void SlipRace_SetDefaultViewDepth(void) {
	SlipDraw3D_SetMaximumDepth(0x7fffffffu);
	SlipDraw3D_SetDepthFade(0, 0, 0);
}

void SlipRace_SetTokyoViewDepth(void) {
	SlipDraw3D_SetMaximumDepth(0x00260000u);
	SlipDraw3D_SetDepthFade(0, 0, 0);
}

void SlipRace_ApplyViewDepth(SlipRaceTrackId track) {
	if (track == SLIP_RACE_TRACK_TOKYO) {
		SlipRace_SetTokyoViewDepth();
	} else {
		SlipRace_SetDefaultViewDepth();
	}
}

static const uint16_t SlipRace_playerStartIndex[SLIP_RACE_TRACK_COUNT] = {0, 5, 1, 6, 2, 7, 3, 8, 4, 9};

static const uint32_t SlipRace_initialPowerup[11] = {0, 4, 4, 7, 5, 2, 2, 3, 1, 8, 2};

static const uint32_t SlipRace_initialPowerupCount[11] = {0, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6};

static const uint16_t SlipRace_aiClass[11] = {0, 0, 0, 1, 1, 1, 2, 2, 3, 3, 3};

SlipRaceFindRacer SlipRace_FindRacer(SlipRaceRacerTable *racerTable, uint16_t driver) {
	uint32_t remaining = racerTable->racerCount;
	SlipRaceRacerState *record = racerTable->records;

	do {
		if (record->tuningIndex == driver) {
			return (SlipRaceFindRacer){record, false};
		}
		++record;
		--remaining;
	} while (remaining != 0);
	return (SlipRaceFindRacer){NULL, true};
}

static void SlipRace_ClearRacerMotion(SlipRaceRacerState *racerState) {
	racerState->movementDamageQ16 = 0;
	racerState->handlingDamageQ16 = 0;
}

static void SlipRace_InitializeRacerState(SlipRaceRacerState *racerState) {
	racerState->primaryWeaponIndex = UINT32_MAX;
	racerState->secondaryWeaponIndex = UINT32_MAX;
	racerState->primaryWeaponAmmo = 0;
	racerState->secondaryWeaponAmmo = 0;
	racerState->powerupRecord = 0;
	racerState->powerupFlags = 0;
	racerState->bonusScore = SlipRace_gameMode == 0 ? 0x02eeu : 0x0bb8u;
	if (SlipRace_allPowerups != 0) {
		racerState->primaryWeaponIndex = 4;
		racerState->primaryWeaponAmmo = 9;
		racerState->secondaryWeaponIndex = 7;
		racerState->secondaryWeaponAmmo = 9;
		racerState->bonusScore = 9999;
	}
}

static void SlipRace_InitializePlayerOne(SlipRaceRacerState *racerState, uint16_t driver, uint16_t position) {
	racerState->tuningIndex = driver;
	racerState->championshipPoints = 0;
	racerState->racerType = 0;
	racerState->racePosition = position;
	racerState->previousRacePosition = position;
	racerState->championshipPosition = position;
	racerState->propulsionProfileIndex = 3;
	SlipRace_ClearRacerMotion(racerState);
	SlipRace_InitializeRacerState(racerState);
}

static void SlipRace_InitializePlayerTwo(SlipRaceRacerState *racerState, uint16_t driver, uint16_t position) {
	racerState->tuningIndex = driver;
	racerState->championshipPoints = 0;
	racerState->propulsionProfileIndex = 3;
	racerState->racerType = SlipRace_gameMode == 1 ? 1u : 3u;
	racerState->racePosition = position;
	racerState->previousRacePosition = position;
	racerState->championshipPosition = position;
	SlipRace_ClearRacerMotion(racerState);
	SlipRace_InitializeRacerState(racerState);
}

static void SlipRace_InitializeAiPowerups(SlipRaceRacerState *racerState) {
	const uint16_t driver = racerState->tuningIndex;

	racerState->secondaryWeaponIndex = UINT32_MAX;
	racerState->secondaryWeaponAmmo = 0;
	racerState->powerupRecord = 0;
	racerState->powerupFlags = 0;
	racerState->primaryWeaponIndex = SlipRace_initialPowerup[driver];
	racerState->primaryWeaponAmmo = SlipRace_initialPowerupCount[driver];
}

void SlipRace_BuildRacerTable(SlipRaceRacerTable *racerTable, uint16_t playerOne, uint16_t playerTwo) {
	uint16_t shuffledDrivers[SLIP_RACE_RACER_COUNT];
	uint32_t shuffleCount;
	uint16_t remainingRacers = racerTable->racerCount;
	SlipRaceRacerState *racerState = racerTable->records;
	uint32_t driverIndex;
	uint32_t nextShuffledDriver = 0;

	for (driverIndex = 0; driverIndex < SLIP_RACE_RACER_COUNT; ++driverIndex) {
		shuffledDrivers[driverIndex] = (uint16_t)(driverIndex + 1u);
	}
	shuffleCount = SlipRandom_Range(50u) + 20u;
	do {
		uint32_t firstIndex;
		uint32_t secondIndex;
		uint16_t swappedDriver;

		do {
			firstIndex = SlipRandom_Range(9u);
			secondIndex = SlipRandom_Range(9u);
		} while (secondIndex == firstIndex);
		swappedDriver = shuffledDrivers[secondIndex];
		shuffledDrivers[secondIndex] = shuffledDrivers[firstIndex];
		shuffledDrivers[firstIndex] = swappedDriver;
		--shuffleCount;
	} while (shuffleCount != 0);

	if ((SlipRace_gameMode == 0 || SlipRace_gameMode == 1) || SlipRace_secondPlayerEnabled != 0) {
		SlipRace_InitializePlayerOne(racerState, playerOne, remainingRacers);
		++racerState;
		if (--remainingRacers == 0)
			return;
		if (playerTwo != 0) {
			SlipRace_InitializePlayerTwo(racerState, playerTwo, remainingRacers);
			++racerState;
			if (--remainingRacers == 0)
				return;
		}
	} else {
		SlipRace_InitializePlayerTwo(racerState, playerTwo, remainingRacers);
		++racerState;
		if (--remainingRacers == 0)
			return;
		SlipRace_InitializePlayerOne(racerState, playerOne, remainingRacers);
		++racerState;
		if (--remainingRacers == 0)
			return;
	}

	do {
		uint16_t driver;

		racerState->championshipPoints = 0;
		racerState->racerType = 2;
		racerState->bonusScore = 0x02eeu;
		racerState->racePosition = remainingRacers;
		racerState->previousRacePosition = remainingRacers;
		racerState->championshipPosition = remainingRacers;
		racerState->propulsionProfileIndex = SlipRace_aiClass[remainingRacers];
		SlipRace_ClearRacerMotion(racerState);
		do {
			driver = shuffledDrivers[nextShuffledDriver++];
		} while (driver == playerOne || driver == playerTwo);
		racerState->tuningIndex = driver;
		SlipRace_InitializeAiPowerups(racerState);
		++racerState;
		--remainingRacers;
	} while (remainingRacers != 0);
}

bool SlipRace_CreatePlayer(uint32_t alternateStartEnabled, const SlipRaceRacerState *racerState,
                           uint16_t resourceHandle, SlipRacePlayerHostBindings *context, const uint8_t *artPayload,
                           size_t artPayloadBytes, SlipArticSlotPool *articPool, SlipArticSlotFindResource findResource,
                           void *findResourceUser, uint16_t trackHandle, uint8_t *slotDrawBase, size_t slotDrawBytes,
                           uint32_t slotDrawBaseOffset, uint32_t slotDrawFreeOffset, uint32_t slotListFreeOffset,
                           SlipRaceCreatePlayer *result) {
	uint32_t racePositionIndex;
	SlipTrackStartRecord start;
	SlipTrackStartHeading heading;
	SlipObjectSlotFill fill;
	SlipObjectActorHandleWriteResult setSlot;
	SlipArticSlotCreate createArtic;
	SlipObjectSetCallback setCallback;
	SlipArticSlotMainBounds mainPartBounds;
	SlipTrackWorldAddSlot addSlot;
	uint16_t mainShapeHandle;
	bool mainShapeCarry;
	uint16_t objectOffset;

	if (result == NULL || racerState == NULL || context == NULL || context->trkBase == NULL ||
	    context->objectTable == NULL || artPayload == NULL || articPool == NULL) {
		return false;
	}
	*result = (SlipRaceCreatePlayer){0};

	SlipRace_alternateStartEnabled = alternateStartEnabled;
	SlipRace_playerSlotType = 0x0du;
	if (racerState->racerType == 2u) {
		SlipRace_playerSlotType = 0x15u;
	}

	racePositionIndex = racerState->racePosition;
	--racePositionIndex;
	if (SlipRace_alternateStartEnabled != 0) {
		if (!SlipTrack_StartRecordAlternate(context->trkBase, context->trkBytes, (uint16_t)racePositionIndex, &start)) {
			return false;
		}
	} else {
		if (racePositionIndex >= 10u || !SlipTrack_StartRecord(context->trkBase, context->trkBytes,
		                                                       SlipRace_playerStartIndex[racePositionIndex], &start)) {
			return false;
		}
	}

	if (!SlipTrack_StartHeading(context->trkBase, context->trkBytes, &heading) ||
	    !SlipView3D_BuildMatrixFromVector(&SlipRace_matrix, (int16_t)heading.headingXQ14, (int16_t)heading.headingYQ14,
	                                      (int16_t)heading.headingZQ14)) {
		return false;
	}

	if (!SlipObject_SlotFill(&SlipRace_matrix, start.startPositionX, start.startPositionY, start.startPositionZ,
	                         TrackView_ExecuteSlotDrawCallback, resourceHandle, SlipRacePlayer_Update, &fill)) {
		return false;
	}
	if (fill.carryOut) {
		SlipRuntime_error = 7;
		result->creationFailed = true;
		return true;
	}
	objectOffset = (uint16_t)fill.objectOffset;
	result->objectOffset = objectOffset;

	if (!SlipObject_SetActorHandle(objectOffset, 4u, &setSlot) ||
	    !SlipArticSlot_Create(objectOffset, resourceHandle, artPayload, artPayloadBytes, articPool,
	                          context->objectTable, context->objectTableBytes, findResource, findResourceUser,
	                          &createArtic)) {
		return false;
	}
	if (createArtic.creationFailed) {
		SlipRuntime_error = 7;
		result->creationFailed = true;
		return true;
	}

	if (!SlipObject_SetDrawCallback(objectOffset, TrackView_ExecuteArticDrawCallback, 0, &setCallback)) {
		return false;
	}
	if (SlipRaceCollision_CreateBody(objectOffset, 7u)) {
		SlipRuntime_error = 7;
		result->creationFailed = true;
		return true;
	}

	if (!SlipArticSlot_GetMainBounds(objectOffset, context->objectTable, context->objectTableBytes,
	                                 context->articSlotPool, context->articSlotPoolBytes, context->articSlotPoolOffset,
	                                 &mainPartBounds)) {
		return false;
	}
	SlipRaceCollision_SetBodyBounds(objectOffset, mainPartBounds.minX, mainPartBounds.minY, mainPartBounds.minZ,
	                                mainPartBounds.maxX, mainPartBounds.maxY, mainPartBounds.maxZ);
	if (!SlipArticSlot_GetMainShape(objectOffset, 0, context->objectTable, context->objectTableBytes,
	                                context->articSlotPool, context->articSlotPoolBytes, context->articSlotPoolOffset,
	                                &mainShapeHandle, &mainShapeCarry)) {
		return false;
	}

	if (!SlipTrackWorld_AddSlot(
	        objectOffset, SlipRace_playerSlotType, trackHandle, slotDrawBase, slotDrawBytes, context->slotDrawCallbacks,
	        context->slotDrawCallbackCount, slotDrawBaseOffset, slotDrawFreeOffset, context->slotListBase,
	        context->slotListBytes, context->slotListBaseOffset, context->slotListSentinelOffset, slotListFreeOffset,
	        context->objectTable, context->objectTableBytes, context->articSlotPool, context->articSlotPoolBytes,
	        context->articSlotPoolOffset, context->trdBase, context->trackDataSize, context->trackDataOffset,
	        context->componentBase, context->componentBaseBytes, context->componentBaseOffset, context->trackTable,
	        context->trackTableBytes, &addSlot)) {
		return false;
	}
	if (addSlot.carryOut) {
		SlipRuntime_error = 7;
		result->creationFailed = true;
		return true;
	}
	return true;
}

SlipRaceControlBinding SlipRace_controlBindings[2] = {
    {0, SLIP_INPUT_SCAN_LEFT, SLIP_INPUT_SCAN_RIGHT, SLIP_INPUT_SCAN_UP, SLIP_INPUT_SCAN_DOWN, SLIP_INPUT_SCAN_SPACE,
     SLIP_INPUT_SCAN_ALT, SLIP_INPUT_SCAN_CONTROL},
    {0, SLIP_INPUT_SCAN_O, SLIP_INPUT_SCAN_P, SLIP_INPUT_SCAN_Q, SLIP_INPUT_SCAN_A, SLIP_INPUT_SCAN_M,
     SLIP_INPUT_SCAN_X, SLIP_INPUT_SCAN_Z}};

uint32_t SlipRace_GetReverseAccelerator(void) { return SlipRace_reverseAccelerator; }

void SlipRace_ToggleReverseAccelerator(void) { SlipRace_reverseAccelerator ^= 1u; }

SlipRaceControlBindings SlipRace_GetControlBindings(void) {
	return (SlipRaceControlBindings){&SlipRace_controlBindings[0], &SlipRace_controlBindings[1]};
}

void SlipRace_UpdateGlobals(void) {
	const uint16_t frameStepQ14 = (uint16_t)SlipFrameTimer_Step();
	const int32_t cloudScrollProduct = (int32_t)(int16_t)SlipRace_cloudScrollStep * (int32_t)(int16_t)frameStepQ14;
	const uint32_t cloudScrollIncrement = (uint32_t)cloudScrollProduct << 2;

	SlipRace_cloudScrollPhase += cloudScrollIncrement;
	SlipRace_cloudScrollFinePhase += (uint32_t)((int32_t)cloudScrollIncrement >> 3);
}

void SlipRace_UpdateTimedEffects(void) {
	uint32_t debrisBudgetElapsedMilliseconds = SlipFrameTimer_delta + SlipRace_debrisBudgetClock;

	if ((int32_t)debrisBudgetElapsedMilliseconds > 1000) {
		debrisBudgetElapsedMilliseconds = 0;
		SlipRace_debrisBudget = 4;
	}
	SlipRace_debrisBudgetClock = debrisBudgetElapsedMilliseconds;

	SlipTimedEffects_Tick(&SlipRaceEffects_creationTemplate, 0x08000000u);
}

void SlipRace_UpdateTrackFrameState(const SlipRacePlayerHostBindings *context) {
	TrackView_MaterialAnimationTick();
	if (!SlipTrackWorld_InitRefuel(&SlipTrackWorld_refuelInitialized, &SlipRacePlayer_refuelSection, 1u,
	                               context->trdBase, context->trackDataSize, context->trackDataOffset,
	                               context->componentBase, context->componentBaseBytes, context->materialTable,
	                               context->materialTableBytes)) {
		SlipRuntime_Fatal("Invalid track data in refuel initialization (0003d568)");
	}
	SlipRace_trackAnimationClock += (uint16_t)SlipFrameTimer_Step();
	SlipTrackWorld_beams.recordCount = 0;
	SlipRace_refuelBeamsBuilt = 0;
}

uint32_t SlipRace_Finished(void) {
	if (SlipRace_playerOneFinished == 0 || SlipRace_playerOneFinishDelay != 0) {
		return 0;
	}
	if (SlipRace_playerTwoFinished == 0 || SlipRace_playerTwoFinishDelay != 0) {
		return 0;
	}
	return 1;
}

static SlipRaceControlHistory controlHistory[2];

void SlipRace_ResetControlHistory(void) {
	controlHistory[0].steering = 0;
	controlHistory[0].pitch = 0;
	controlHistory[1].steering = 0;
	controlHistory[1].pitch = 0;
	(void)SlipInput_ReadMotion();
}

void SlipRace_PreCameraInput(const bool inputHeld1[256], bool inputPressed[256], SlipRacePlayerControl *controls) {
	SlipRaceControlBindings bindings;

	SlipRace_activeReverseAccelerator = SlipRace_GetReverseAccelerator();
	bindings = SlipRace_GetControlBindings();
	SlipRace_ReadControls(bindings.playerOne, inputHeld1, inputPressed, &controlHistory[0], controls);

	if (SlipRace_activeReverseAccelerator != 0) {
		controls->actions ^= 1u;
	}

	if (inputPressed[SLIP_INPUT_SCAN_ESCAPE]) {
		inputPressed[SLIP_INPUT_SCAN_ESCAPE] = false;
		SlipRace_menuRequest = 0;
		controls->actions |= SLIP_ACTION_PAUSE;
	} else if (SlipRace_menuRequest != 0) {
		SlipRace_menuRequest = 0;
		controls->actions |= SLIP_ACTION_PAUSE;
	}

	if (SlipRacePlayer_playerTwoObject != 0) {
		SlipRace_ReadControls(bindings.playerTwo, inputHeld1, inputPressed, &controlHistory[1],
		                      &SlipRacePlayer_playerTwoControls);
		if (SlipRace_activeReverseAccelerator != 0) {
			SlipRacePlayer_playerTwoControls.actions ^= 1u;
		}
	}
}

void SlipRace_ReadControls(const SlipRaceControlBinding *binding, const bool inputHeld1[256], bool inputPressed[256],
                           SlipRaceControlHistory *history, SlipRacePlayerControl *controls) {
	uint32_t actions = 0;
	uint32_t directions = 0;
	SlipInputCode inputCode;

	enum { JOYSTICK_DEAD_ZONE = 0x400, MOUSE_STEERING_SHIFT = 5, MOUSE_PITCH_SHIFT = 6 };

	inputCode = binding->accelerate;
	if (inputHeld1[inputCode]) {
		actions |= SLIP_ACTION_ACCELERATE;
	}
	inputCode = binding->fire;
	if (inputHeld1[inputCode]) {
		actions |= SLIP_ACTION_FIRE;
	}
	inputCode = binding->select;
	if (inputPressed[inputCode]) {
		actions |= SLIP_ACTION_SELECT;
		inputPressed[inputCode] = false;
	}

	if (binding->movementControl == 0) {
		inputCode = binding->left;
		if (inputHeld1[inputCode]) {
			directions |= SLIP_CONTROL_LEFT;
		}
		inputCode = binding->right;
		if (inputHeld1[inputCode]) {
			directions |= SLIP_CONTROL_RIGHT;
		}
		inputCode = binding->up;
		if (inputHeld1[inputCode]) {
			directions |= SLIP_CONTROL_UP;
		}
		inputCode = binding->down;
		if (inputHeld1[inputCode]) {
			directions |= SLIP_CONTROL_DOWN;
		}
		SlipRacePlayerControl axes = {history->steering, history->pitch, 0};
		SlipRacePlayer_UpdateDigitalAxes(&axes, directions);
		history->steering = axes.steering;
		history->pitch = axes.pitch;
		controls->steering = axes.steering;
		controls->pitch = axes.pitch;
	} else if (binding->movementControl == 1 || binding->movementControl == 2) {

		const SlipJoystickAxesExit exit = SlipJoystick_ReadAxes(binding->movementControl - 1u, JOYSTICK_DEAD_ZONE,
		                                                        &controls->steering, &controls->pitch);
		if (exit != SLIP_JOYSTICK_AXES_RETURN)
			SlipJoystick_ReportUnsupportedDivide(exit);
	} else if (binding->movementControl == 3) {
		SlipInputMotion motion = SlipInput_ReadMotion();
		int16_t steering =
		    (int16_t)(uint16_t)((uint16_t)history->steering + ((uint32_t)(uint16_t)motion.x << MOUSE_STEERING_SHIFT));
		int16_t pitch =
		    (int16_t)(uint16_t)((uint16_t)history->pitch + ((uint32_t)(uint16_t)motion.y << MOUSE_PITCH_SHIFT));
		if (steering > SLIP_CONTROL_AXIS_LIMIT)
			steering = SLIP_CONTROL_AXIS_LIMIT;
		if (steering < -SLIP_CONTROL_AXIS_LIMIT)
			steering = -SLIP_CONTROL_AXIS_LIMIT;
		if (pitch > SLIP_CONTROL_AXIS_LIMIT)
			pitch = SLIP_CONTROL_AXIS_LIMIT;
		if (pitch < -SLIP_CONTROL_AXIS_LIMIT)
			pitch = -SLIP_CONTROL_AXIS_LIMIT;
		history->steering = controls->steering = steering;
		history->pitch = controls->pitch = pitch;
	} else {
		SlipGame_UnexpectedFailure();
	}
	controls->actions = (uint16_t)actions;
}
