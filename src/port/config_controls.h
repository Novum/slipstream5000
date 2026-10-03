#ifndef SLIPSTREAM5000_CONFIG_CONTROLS_H
#define SLIPSTREAM5000_CONFIG_CONTROLS_H
#include "config_menu_draw.h"
#include "race.h"

/* The confirmation button is shared by conflict and calibration screens. */
enum {
	SLIP_CONTROL_CONFIRM_BUTTON_LEFT = 83,
	SLIP_CONTROL_CONFIRM_BUTTON_TOP = 75,
	SLIP_CONTROL_CONFIRM_BUTTON_RIGHT = 141,
	SLIP_CONTROL_CONFIRM_BUTTON_BOTTOM = 90
};

typedef struct SlipConfigControlsState {
	uint32_t player;
	SlipRaceControlBinding *controlBinding;

	SlipInputCode *pendingBinding;
	uint32_t selection;
	uint16_t background, workingSprite, movementBlocker;
} SlipConfigControlsState;

extern SlipConfigControlsState SlipConfigControls_state;
extern uint8_t SlipConfigControls_usedInputs[SLIP_INPUT_CODE_COUNT];

typedef struct SlipConfigControlsCalls {
	const SlipConfigMenuCalls *menu;
	const SlipConfigMenuDrawCalls *draw;
	void *context;
	bool (*copy)(void *, uint16_t *handle);
	int32_t (*nextInput)(void *);
	const char *(*inputName)(void *, uint32_t code);
	void (*showPointer)(void *);
	void (*hidePointer)(void *);
	void (*cycleMovement)(void *, uint32_t player);
	void (*drawBindings)(void *, const SlipConfigControlsState *);
} SlipConfigControlsCalls;

void SlipConfigControls_Run(SlipConfigControlsState *, uint32_t player, uint16_t configurationBackground,
                            const SlipConfigControlsCalls *);
bool SlipConfigControls_Conflict(void);

typedef struct SlipConfigControlNames {
	const char *displayedPlayer, *otherPlayer;
} SlipConfigControlNames;

typedef struct SlipConfigControlRecords {
	SlipRaceControlBinding *displayedPlayer, *otherPlayer;
} SlipConfigControlRecords;

typedef struct SlipConfigControlDimensions {
	uint16_t width, height;
} SlipConfigControlDimensions;

typedef struct SlipConfigControlsDrawCalls {
	const SlipConfigMenuDrawCalls *draw;
	void *context;
	SlipConfigControlDimensions (*dimensions)(void *, uint16_t);
	void (*bindSprite)(void *, uint16_t resource, uint16_t offset, uint16_t pitch, SlipConfigControlDimensions);
	void (*fill)(void *, uint16_t color, SlipConfigMenuRectangle);
	const char *(*inputName)(void *, uint32_t);
	void (*restore)(void *);
} SlipConfigControlsDrawCalls;

extern SlipTextArgument SlipConfigControls_keyName;
SlipConfigControlNames SlipConfigControls_Names(void);
SlipConfigControlRecords SlipConfigControls_Records(void);
void SlipConfigControls_Draw(const SlipConfigControlsState *, uint16_t smallFont, SlipStringTableSlot *,
                             const SlipConfigControlsDrawCalls *);

typedef struct SlipConfigConflictState {
	uint32_t selection;
	uint16_t background, workingSprite, inactiveBackground;
} SlipConfigConflictState;

typedef enum SlipJoystickCalibrationOperation {
	SLIP_JOYSTICK_CALIBRATION_INITIALIZE = 0,
	SLIP_JOYSTICK_CALIBRATION_RESET_RANGE = 1,
	SLIP_JOYSTICK_CALIBRATION_SAMPLE_RANGE = 2,
	SLIP_JOYSTICK_CALIBRATION_FINISH = 3
} SlipJoystickCalibrationOperation;

typedef struct SlipJoystickCalibration {
	uint16_t centerX, centerY;
	int16_t minimumX, maximumX, minimumY, maximumY;
} SlipJoystickCalibration;

typedef struct SlipConfigCalibrationState {
	uint32_t detected, joystick, selection;
	uint16_t background, workingSprite, inactiveBackground;
} SlipConfigCalibrationState;

extern SlipConfigConflictState SlipConfigConflict_state;
extern SlipConfigCalibrationState SlipConfigCalibration_state;
extern uint8_t SlipConfigControls_calibrated[2];
extern SlipJoystickCalibration SlipConfigControls_calibration[2];

typedef struct SlipConfigDialogCalls {
	const SlipConfigControlsCalls *controls;
	void *context;

	bool (*calibration)(void *, uint32_t operation, uint32_t joystick);
	void (*readCalibration)(void *, uint32_t joystick, SlipJoystickCalibration *);
	void (*drawConflict)(void *, const SlipConfigConflictState *);
	void (*drawCalibration)(void *, const SlipConfigCalibrationState *);
} SlipConfigDialogCalls;

void SlipConfigControls_Warn(SlipConfigConflictState *, uint16_t configurationBackground,
                             const SlipConfigDialogCalls *);
void SlipConfigControls_Calibrate(SlipConfigCalibrationState *, uint32_t joystick, uint16_t configurationBackground,
                                  const SlipConfigDialogCalls *);

void SlipConfigControls_DrawButton(SlipConfigMenuRectangle, uint16_t sprite, SlipStringTableSlot *, uint32_t tag,
                                   const SlipConfigMenuDrawCalls *);
void SlipConfigControls_DrawWarning(const SlipConfigConflictState *, uint16_t configurationFont, uint16_t smallFont,
                                    SlipStringTableSlot *, const SlipConfigControlsDrawCalls *);
void SlipConfigControls_DrawCalibration(const SlipConfigCalibrationState *, uint16_t configurationFont,
                                        uint16_t smallFont, SlipStringTableSlot *, const SlipConfigControlsDrawCalls *);

typedef uint32_t (*SlipConfigMousePresent)(void *);
void SlipConfigControls_CycleMovement(uint32_t player, SlipConfigMousePresent, void *context);
#endif
