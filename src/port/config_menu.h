#ifndef SLIPSTREAM5000_CONFIG_MENU_H
#define SLIPSTREAM5000_CONFIG_MENU_H
#include "input.h"
#include "string_table.h"

/* Panel identities select the executable's paired navigation and rectangle tables. */
typedef enum SlipConfigMenuTable {
	SLIP_CONFIG_MAIN_TABLE,
	SLIP_CONFIG_DIFFICULTY_TABLE,
	SLIP_CONFIG_GENERAL_TABLE,
	SLIP_CONFIG_DETAIL_TABLE,
	SLIP_CONFIG_SOUND_TABLE,
	SLIP_CONFIG_CONTROLS_TABLE,
	SLIP_CONFIG_BINDINGS_TABLE,
	SLIP_CONFIG_CONFLICT_TABLE,
	SLIP_CONFIG_CALIBRATION_TABLE,
	SLIP_CONFIG_TABLE_COUNT
} SlipConfigMenuTable;

enum {
	SLIP_CONFIG_MAIN_TITLE = 1,
	SLIP_CONFIG_MAIN_GENERAL = 2,
	SLIP_CONFIG_MAIN_CONTROLS = 3,
	SLIP_CONFIG_MAIN_DETAIL = 4,
	SLIP_CONFIG_MAIN_CLOSE = 5,
	SLIP_CONFIG_MAIN_DIFFICULTY = 6,
	SLIP_CONFIG_MAIN_SOUND = 7
};

enum {
	SLIP_CONFIG_DIFFICULTY_CLOSE = 3,
	SLIP_CONFIG_DIFFICULTY_TITLE = 4,
	SLIP_CONFIG_DIFFICULTY_OPTION_COUNT = SLIP_CONFIG_DIFFICULTY_CLOSE - 1
};

enum {
	SLIP_CONFIG_GENERAL_OPTION_COUNT = 5,
	SLIP_CONFIG_GENERAL_CLOSE = 6,
	SLIP_CONFIG_GENERAL_TITLE = 7,
	SLIP_CONFIG_GENERAL_HIGH_RES = 8
};

enum {
	SLIP_CONFIG_DETAIL_CLOSE = 7,
	SLIP_CONFIG_DETAIL_TITLE = 8,
	SLIP_CONFIG_DETAIL_OPTION_COUNT = SLIP_CONFIG_DETAIL_CLOSE - 1
};

enum {
	SLIP_CONFIG_SOUND_CLOSE = 5,
	SLIP_CONFIG_SOUND_TITLE = 6,
	SLIP_CONFIG_SOUND_OPTION_COUNT = SLIP_CONFIG_SOUND_CLOSE - 1
};

enum {
	SLIP_CONFIG_CONTROLS_PLAYER_ONE = 1,
	SLIP_CONFIG_CONTROLS_PLAYER_TWO = 2,
	SLIP_CONFIG_CONTROLS_CALIBRATE_ONE = 3,
	SLIP_CONFIG_CONTROLS_CALIBRATE_TWO = 4,
	SLIP_CONFIG_CONTROLS_REVERSE_ACCELERATOR = 5,
	SLIP_CONFIG_CONTROLS_CLOSE = 6,
	SLIP_CONFIG_CONTROLS_TITLE = 7
};

enum {
	SLIP_CONFIG_BINDINGS_MOVEMENT_LABEL = 1,
	SLIP_CONFIG_BINDINGS_MOVEMENT_VALUE = 2,
	SLIP_CONFIG_BINDINGS_UP = 3,
	SLIP_CONFIG_BINDINGS_LEFT = 4,
	SLIP_CONFIG_BINDINGS_RIGHT = 5,
	SLIP_CONFIG_BINDINGS_DOWN = 6,
	SLIP_CONFIG_BINDINGS_SELECT = 7,
	SLIP_CONFIG_BINDINGS_FIRE = 8,
	SLIP_CONFIG_BINDINGS_ACCELERATE = 9,
	SLIP_CONFIG_BINDINGS_CLOSE = 10,
};

enum {
	SLIP_CONFIG_MAIN_RECTANGLE_COUNT = SLIP_CONFIG_MAIN_SOUND,
	SLIP_CONFIG_DIFFICULTY_RECTANGLE_COUNT = SLIP_CONFIG_DIFFICULTY_TITLE,
	SLIP_CONFIG_GENERAL_RECTANGLE_COUNT = SLIP_CONFIG_GENERAL_TITLE,
	SLIP_CONFIG_DETAIL_RECTANGLE_COUNT = SLIP_CONFIG_DETAIL_TITLE,
	SLIP_CONFIG_SOUND_RECTANGLE_COUNT = SLIP_CONFIG_SOUND_TITLE,
	SLIP_CONFIG_CONTROLS_RECTANGLE_COUNT = SLIP_CONFIG_CONTROLS_TITLE
};

typedef struct SlipConfigMenuPoint {
	int16_t x, y;
} SlipConfigMenuPoint;

typedef struct SlipConfigMenuState {
	uint16_t blocker, background, inactiveBackground, font;
	uint16_t references;
	uint32_t allowDifficulty;
	SlipStringTableSlot *mainStrings;
	uint32_t mainSelection;
	SlipStringTableSlot *difficultyStrings;
	uint32_t difficultySelection;
	SlipStringTableSlot *generalStrings;
	uint32_t generalSelection;
	SlipStringTableSlot *detailStrings;
	uint32_t detailSelection;
	SlipStringTableSlot *soundStrings;
	uint32_t soundSelection;
	SlipStringTableSlot *controlsStrings;
	uint32_t controlsSelection;
} SlipConfigMenuState;

extern SlipConfigMenuState SlipConfigMenu_state;

struct SlipConfigDialogCalls;

typedef struct SlipConfigMenuCalls {
	void *context;
	bool (*load)(void *, const char *, uint16_t *);
	void (*release)(void *, uint16_t);
	void (*selectFont)(void *, uint16_t);
	uint16_t (*currentFont)(void *);
	void (*textColor)(void *, uint16_t);
	void (*palette)(void *, uint16_t);
	void (*language)(void *);
	bool (*strings)(void *, const char name[SLIP_RESOURCE_BASE_NAME_BYTES], SlipStringTableSlot **);
	void (*releaseStrings)(void *, SlipStringTableSlot *);
	void (*resourceError)(void *);
	void (*navigation)(void *, SlipConfigMenuTable);
	void (*clearNavigation)(void *);
	void (*resetTimer)(void *);
	void (*updateTimer)(void *);
	SlipConfigMenuPoint (*pointer)(void *);
	uint32_t (*hitTest)(void *, SlipConfigMenuTable, SlipConfigMenuPoint);
	bool (*pressed)(void *, SlipInputCode);
	void (*drawMain)(void *, SlipConfigMenuState *);
	void (*drawDifficulty)(void *, SlipConfigMenuState *);
	void (*present)(void *);
	void (*poll)(void *);
	void (*cycleDifficulty)(void *);
	void (*toggleDamage)(void *);
	void (*save)(void *);
	void (*drawGeneral)(void *, SlipConfigMenuState *);
	void (*drawDetail)(void *, SlipConfigMenuState *);
	void (*drawSound)(void *, SlipConfigMenuState *);
	void (*generalOptions[SLIP_CONFIG_GENERAL_OPTION_COUNT])(void *);
	void (*toggleHighRes)(void *);
	void (*detailOptions[SLIP_CONFIG_DETAIL_OPTION_COUNT])(void *);
	void (*soundOptions[SLIP_CONFIG_SOUND_OPTION_COUNT])(void *);
	void (*cycleLanguage)(void *);
	void (*cycleMusic)(void *);
	void (*applyMusic)(void *);
	void (*drawControls)(void *, SlipConfigMenuState *);
	const struct SlipConfigDialogCalls *controlDialogs;
	void (*reverseAccelerator)(void *);
} SlipConfigMenuCalls;

void SlipConfigMenu_Acquire(SlipConfigMenuState *, const SlipConfigMenuCalls *);
void SlipConfigMenu_Release(SlipConfigMenuState *, const SlipConfigMenuCalls *);
void SlipConfigMenu_Run(SlipConfigMenuState *, const SlipConfigMenuCalls *);
void SlipConfigMenu_Difficulty(SlipConfigMenuState *, const SlipConfigMenuCalls *);
void SlipConfigMenu_General(SlipConfigMenuState *, const SlipConfigMenuCalls *);
void SlipConfigMenu_Detail(SlipConfigMenuState *, const SlipConfigMenuCalls *);
void SlipConfigMenu_Sound(SlipConfigMenuState *, const SlipConfigMenuCalls *);
void SlipConfigMenu_CycleLanguage(SlipConfigMenuState *, const SlipConfigMenuCalls *);
void SlipConfigMenu_CycleMusic(const SlipConfigMenuCalls *);
void SlipConfigMenu_Controls(SlipConfigMenuState *, const SlipConfigMenuCalls *);
#endif
