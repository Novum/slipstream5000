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
	SLIP_CONFIG_CALIBRATION_TABLE
} SlipConfigMenuTable;

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
	bool (*strings)(void *, const char name[8], SlipStringTableSlot **);
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
	void (*generalOptions[5])(void *);
	void (*toggleHighRes)(void *);
	void (*detailOptions[6])(void *);
	void (*soundOptions[4])(void *);
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
