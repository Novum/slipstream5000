#ifndef SLIPSTREAM5000_INPUT_NAVIGATION_H
#define SLIPSTREAM5000_INPUT_NAVIGATION_H
#include "input.h"
#include <stdbool.h>

typedef struct SlipInputNavigationTable {
	uint16_t itemCount, currentItem;
	int16_t up[12], down[12], left[12], right[12];
	int16_t centers[12][2];
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
extern SlipInputNavigationTable SlipInput_configurationNavigation[9];
extern SlipInputNavigationTable SlipInput_garageNavigation[5]; /* pods, weapons, turbo, systems, main */
bool SlipInput_TestAndClear(bool pressed[256], SlipInputCode code);
void SlipInput_SetNavigation(SlipInputNavigationTable *);
void SlipInput_ClearNavigation(void);
void SlipInput_UpdateNavigation(bool pressed[256]);

typedef struct SlipInputPointerPosition {
	uint16_t x, y;
} SlipInputPointerPosition;

SlipInputPointerPosition SlipInput_Pointer(void);

typedef struct SlipInputRectangle {
	int16_t left, top, right, bottom;
} SlipInputRectangle;

uint32_t SlipInput_HitTest(const SlipInputRectangle *, uint16_t count, int16_t x, int16_t y);

extern bool SlipInput_pressed[256], SlipInput_held[256];

SlipInputCode SlipInput_PopPressed(bool pressed[256]);
SlipInputCode SlipInput_PopMenuPressed(bool pressed[256]);
#endif
