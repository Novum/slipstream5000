#ifndef SLIPSTREAM5000_INPUT_NAVIGATION_H
#define SLIPSTREAM5000_INPUT_NAVIGATION_H
#include "config_menu.h"
#include "input.h"
#include <stdbool.h>

enum {
	SLIP_INPUT_POINTER_FRACTION_BITS = 16,
	SLIP_INPUT_POINTER_ONE = 1 << SLIP_INPUT_POINTER_FRACTION_BITS,
	SLIP_INPUT_NAVIGATION_ITEM_CAPACITY = 12,
	/* Garage tables: pods, weapons, turbo, systems, then main. */
	SLIP_INPUT_GARAGE_MAIN_TABLE = 4,
	SLIP_INPUT_GARAGE_TABLE_COUNT = SLIP_INPUT_GARAGE_MAIN_TABLE + 1
};

typedef struct SlipInputNavigationTable {
	uint16_t itemCount, currentItem;
	int16_t up[SLIP_INPUT_NAVIGATION_ITEM_CAPACITY], down[SLIP_INPUT_NAVIGATION_ITEM_CAPACITY],
	    left[SLIP_INPUT_NAVIGATION_ITEM_CAPACITY], right[SLIP_INPUT_NAVIGATION_ITEM_CAPACITY];
	int16_t centers[SLIP_INPUT_NAVIGATION_ITEM_CAPACITY][2];
} SlipInputNavigationTable;

typedef struct SlipInputNavigationState {
	SlipInputNavigationTable *active;
	uint16_t *currentItem;
	const int16_t *up, *down, *left, *right;
	const int16_t (*centers)[2];
} SlipInputNavigationState;

extern SlipInputNavigationState SlipInput_navigation;
extern int32_t SlipInput_pointerX, SlipInput_pointerY;

typedef struct SlipInputMotion {
	int16_t x, y;
} SlipInputMotion;

extern SlipInputMotion SlipInput_motion;
SlipInputMotion SlipInput_ReadMotion(void);
extern SlipInputNavigationTable SlipInput_configurationNavigation[SLIP_CONFIG_TABLE_COUNT];
extern SlipInputNavigationTable
    SlipInput_garageNavigation[SLIP_INPUT_GARAGE_TABLE_COUNT]; /* pods, weapons, turbo, systems, main */
bool SlipInput_TestAndClear(bool pressed[SLIP_INPUT_CODE_COUNT], SlipInputCode code);
void SlipInput_SetNavigation(SlipInputNavigationTable *);
void SlipInput_ClearNavigation(void);
void SlipInput_UpdateNavigation(bool pressed[SLIP_INPUT_CODE_COUNT]);

typedef struct SlipInputPointerPosition {
	uint16_t x, y;
} SlipInputPointerPosition;

SlipInputPointerPosition SlipInput_Pointer(void);

typedef struct SlipInputRectangle {
	int16_t left, top, right, bottom;
} SlipInputRectangle;

uint32_t SlipInput_HitTest(const SlipInputRectangle *, uint16_t count, int16_t x, int16_t y);

extern bool SlipInput_pressed[SLIP_INPUT_CODE_COUNT], SlipInput_held[SLIP_INPUT_CODE_COUNT];

SlipInputCode SlipInput_PopPressed(bool pressed[SLIP_INPUT_CODE_COUNT]);
SlipInputCode SlipInput_PopMenuPressed(bool pressed[SLIP_INPUT_CODE_COUNT]);
#endif
