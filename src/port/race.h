#ifndef SLIPSTREAM5000_RACE_H
#define SLIPSTREAM5000_RACE_H

#include "input.h"

#include "artic_slot.h"
#include "race_player.h"

#include <stdbool.h>
#include <stdint.h>

extern uint32_t SlipRace_drawPageToggle;
extern SlipRacePlayerControl SlipRace_controls;
extern uint16_t SlipRace_reverseAccelerator;
extern uint32_t SlipRace_activeReverseAccelerator;
extern uint32_t SlipRace_menuRequest;
extern uint32_t SlipRace_racerCount;

enum { SLIP_RACE_RACER_COUNT = 10 };

typedef struct SlipRaceRacerTable {
	uint16_t racerCount;
	SlipRaceRacerState records[SLIP_RACE_RACER_COUNT];
} SlipRaceRacerTable;

extern SlipRaceRacerTable SlipRace_racerTable;
extern uint32_t SlipRace_gameMode;

typedef enum SlipRaceType {
	SLIP_RACE_TYPE_PRACTICE = 0x11,
	SLIP_RACE_TYPE_SINGLE = 0x12,
	SLIP_RACE_TYPE_CHAMPIONSHIP = 0x13
} SlipRaceType;

extern SlipRaceType SlipRace_type;
extern uint16_t SlipRace_secondPlayerEnabled;
extern uint32_t SlipRace_demoChaseEnabled;
extern uint32_t SlipRace_flybyChaseEnabled;
extern uint32_t SlipRace_allPowerups;
extern SlipRaceRacerTable *SlipRace_activeRacerTable;
extern uint32_t SlipRace_cloudScrollPhase;
extern uint32_t SlipRace_cloudScrollFinePhase;
extern uint16_t SlipRace_cloudScrollStep;
extern uint32_t SlipRace_debrisBudgetClock;
extern uint32_t SlipRace_debrisBudget;
extern uint32_t SlipRace_trackAnimationClock;
extern uint32_t SlipRace_refuelBeamsBuilt;
extern uint16_t SlipRace_playerOneFinished;
extern uint16_t SlipRace_playerTwoFinished;
extern uint16_t SlipRace_playerOneFinishDelay;
extern uint16_t SlipRace_playerTwoFinishDelay;
extern uint32_t SlipRace_alternateStartEnabled;
extern uint32_t SlipRace_playerSlotType;
extern SlipView3DMatrix SlipRace_matrix;
extern uint32_t SlipRace_viewIndex;

typedef enum SlipRaceTrackId {
	SLIP_RACE_TRACK_CHICAGO = 1,
	SLIP_RACE_TRACK_HAWAII,
	SLIP_RACE_TRACK_TOKYO,
	SLIP_RACE_TRACK_NORWAY,
	SLIP_RACE_TRACK_CAVE,
	SLIP_RACE_TRACK_CANADA,
	SLIP_RACE_TRACK_AMAZON,
	SLIP_RACE_TRACK_LONDON,
	SLIP_RACE_TRACK_EGYPT,
	SLIP_RACE_TRACK_NEW_YORK,
	SLIP_RACE_TRACK_COUNT = SLIP_RACE_TRACK_NEW_YORK,
} SlipRaceTrackId;

extern const char *const SlipRace_paletteNames[SLIP_RACE_TRACK_COUNT];
extern const char *const SlipRace_materialNames[SLIP_RACE_TRACK_COUNT];
extern const char *const SlipRace_trackNames[SLIP_RACE_TRACK_COUNT];

void SlipRace_SetDefaultViewDepth(void);
void SlipRace_SetTokyoViewDepth(void);
void SlipRace_ApplyViewDepth(SlipRaceTrackId track);

typedef struct SlipRaceFindRacer {
	SlipRaceRacerState *record;
	bool racerNotFound;
} SlipRaceFindRacer;

SlipRaceFindRacer SlipRace_FindRacer(SlipRaceRacerTable *racerTable, uint16_t driver);

void SlipRace_BuildRacerTable(SlipRaceRacerTable *racerTable, uint16_t playerOne, uint16_t playerTwo);

typedef struct SlipRaceCreatePlayer {
	uint16_t objectOffset;
	bool creationFailed;
} SlipRaceCreatePlayer;

bool SlipRace_CreatePlayer(uint32_t alternateStartEnabled, const SlipRaceRacerState *racerState,
                           uint16_t resourceHandle, SlipRacePlayerHostBindings *context, const uint8_t *artPayload,
                           size_t artPayloadBytes, SlipArticSlotPool *articPool, SlipArticSlotFindResource findResource,
                           void *findResourceUser, uint16_t trackHandle, uint8_t *slotDrawBase, size_t slotDrawBytes,
                           uint32_t slotDrawBaseOffset, uint32_t slotDrawFreeOffset, uint32_t slotListFreeOffset,
                           SlipRaceCreatePlayer *result);

void SlipRace_UpdateGlobals(void);
void SlipRace_UpdateTimedEffects(void);
void SlipRace_UpdateTrackFrameState(const SlipRacePlayerHostBindings *context);
uint32_t SlipRace_Finished(void);

typedef struct SlipRaceControlBinding {
	uint16_t movementControl;
	SlipInputCode left;
	SlipInputCode right;
	SlipInputCode up;
	SlipInputCode down;
	SlipInputCode accelerate;
	SlipInputCode fire;
	SlipInputCode select;
} SlipRaceControlBinding;

extern SlipRaceControlBinding SlipRace_controlBindings[2];

typedef struct SlipRaceControlBindings {
	const SlipRaceControlBinding *playerOne;
	const SlipRaceControlBinding *playerTwo;
} SlipRaceControlBindings;

uint32_t SlipRace_GetReverseAccelerator(void);
void SlipRace_ToggleReverseAccelerator(void);
SlipRaceControlBindings SlipRace_GetControlBindings(void);

typedef struct SlipRaceControlHistory {
	int16_t steering, pitch;
} SlipRaceControlHistory;

void SlipRace_ResetControlHistory(void);
void SlipRace_ReadControls(const SlipRaceControlBinding *binding, const bool inputHeld1[256], bool inputPressed[256],
                           SlipRaceControlHistory *history, SlipRacePlayerControl *controls);

void SlipRace_PreCameraInput(const bool inputHeld1[256], bool inputPressed[256], SlipRacePlayerControl *controls);

#endif
