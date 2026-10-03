#include "input_navigation.h"
#include <stddef.h>

/* Move halfway toward the selected navigation target each update. */
enum { SLIP_INPUT_NAVIGATION_APPROACH_SHIFT = 1 };

SlipInputNavigationState SlipInput_navigation;
int32_t SlipInput_pointerX, SlipInput_pointerY;
SlipInputMotion SlipInput_motion;

SlipInputMotion SlipInput_ReadMotion(void) {
	SlipInputMotion motion = SlipInput_motion;
	SlipInput_motion.x = 0;
	SlipInput_motion.y = 0;
	return motion;
}

bool SlipInput_TestAndClear(bool pressed[SLIP_INPUT_CODE_COUNT], SlipInputCode code) {
	bool result = pressed[code];
	pressed[code] = false;
	return result;
}

void SlipInput_SetNavigation(SlipInputNavigationTable *table) {
	SlipInput_navigation.active = NULL;
	SlipInput_navigation.currentItem = &table->currentItem;
	SlipInput_navigation.up = table->up;
	SlipInput_navigation.down = table->down;
	SlipInput_navigation.left = table->left;
	SlipInput_navigation.right = table->right;
	SlipInput_navigation.centers = table->centers;
	SlipInput_navigation.active = table;
}

void SlipInput_ClearNavigation(void) { SlipInput_navigation.active = NULL; }

void SlipInput_UpdateNavigation(bool pressed[SLIP_INPUT_CODE_COUNT]) {
	if (SlipInput_navigation.active == NULL)
		return;
	if (SlipInput_TestAndClear(pressed, SLIP_INPUT_SCAN_UP)) {
		const int16_t next = SlipInput_navigation.up[*SlipInput_navigation.currentItem];
		if (next >= 0)
			*SlipInput_navigation.currentItem = (uint16_t)next;
	}
	if (SlipInput_TestAndClear(pressed, SLIP_INPUT_SCAN_DOWN)) {
		const int16_t next = SlipInput_navigation.down[*SlipInput_navigation.currentItem];
		if (next >= 0)
			*SlipInput_navigation.currentItem = (uint16_t)next;
	}
	if (SlipInput_TestAndClear(pressed, SLIP_INPUT_SCAN_LEFT)) {
		const int16_t next = SlipInput_navigation.left[*SlipInput_navigation.currentItem];
		if (next >= 0)
			*SlipInput_navigation.currentItem = (uint16_t)next;
	}
	if (SlipInput_TestAndClear(pressed, SLIP_INPUT_SCAN_RIGHT)) {
		const int16_t next = SlipInput_navigation.right[*SlipInput_navigation.currentItem];
		if (next >= 0)
			*SlipInput_navigation.currentItem = (uint16_t)next;
	}
	const uint16_t current = *SlipInput_navigation.currentItem;
	const int32_t distanceX =
	    (int32_t)(((uint32_t)(uint16_t)SlipInput_navigation.centers[current][0] << SLIP_INPUT_POINTER_FRACTION_BITS) -
	              (uint32_t)SlipInput_pointerX);
	const int32_t distanceY =
	    (int32_t)(((uint32_t)(uint16_t)SlipInput_navigation.centers[current][1] << SLIP_INPUT_POINTER_FRACTION_BITS) -
	              (uint32_t)SlipInput_pointerY);
	SlipInput_pointerX =
	    (int32_t)((uint32_t)SlipInput_pointerX + (uint32_t)(distanceX >> SLIP_INPUT_NAVIGATION_APPROACH_SHIFT));
	SlipInput_pointerY =
	    (int32_t)((uint32_t)SlipInput_pointerY + (uint32_t)(distanceY >> SLIP_INPUT_NAVIGATION_APPROACH_SHIFT));
}

/* Original configuration navigation tables, in SlipConfigMenuTable order. */
SlipInputNavigationTable SlipInput_configurationNavigation[SLIP_CONFIG_TABLE_COUNT] = {

    {6,
     0,
     {-1, -1, 0, 5, 1, 2},
     {2, 4, 5, -1, 3, 3},
     {-1, 0, -1, -1, 2, -1},
     {1, -1, 4, -1, -1, -1},
     {{91, 52}, {222, 52}, {91, 74}, {160, 180}, {222, 74}, {91, 96}}},

    {3, 0, {-1, 0, 1}, {1, 2, -1}, {-1, -1, -1}, {-1, -1, -1}, {{153, 51}, {153, 71}, {147, 178}}},

    {7,
     0,
     {-1, 0, 1, 2, 3, 4, 5},
     {1, 2, 3, 4, 5, 6, -1},
     {-1, -1, -1, -1, -1, -1, -1},
     {-1, -1, -1, -1, -1, -1, -1},
     {{153, 51}, {153, 71}, {153, 91}, {153, 111}, {153, 131}, {153, 151}, {147, 178}}},

    {7,
     0,
     {-1, 0, 1, 2, 3, 4, 5},
     {1, 2, 3, 4, 5, 6, -1},
     {-1, -1, -1, -1, -1, -1, -1},
     {-1, -1, -1, -1, -1, -1, -1},
     {{153, 51}, {153, 71}, {153, 91}, {153, 111}, {153, 131}, {153, 151}, {147, 178}}},

    {5,
     0,
     {-1, 0, 1, 2, 3},
     {1, 2, 3, 4, -1},
     {-1, -1, -1, -1, -1},
     {-1, -1, -1, -1, -1},
     {{153, 51}, {153, 71}, {153, 91}, {153, 111}, {147, 178}}},

    {6,
     0,
     {-1, 0, 1, 2, 3, 4},
     {1, 2, 3, 4, 5, -1},
     {-1, -1, -1, -1, -1, -1},
     {-1, -1, -1, -1, -1, -1},
     {{153, 52}, {153, 74}, {153, 96}, {153, 118}, {153, 140}, {147, 178}}},

    {10,
     0,
     {-1, -1, 0, 2, 2, 3, 1, 6, 7, 8},
     {2, 6, 3, 5, 5, 9, 7, 8, 9, -1},
     {-1, 0, -1, -1, 3, -1, 2, 4, 5, -1},
     {1, -1, 6, 4, 7, 8, -1, -1, -1, -1},
     {{121, 73},
      {224, 73},
      {118, 91},
      {86, 103},
      {153, 103},
      {118, 115},
      {224, 91},
      {224, 103},
      {224, 115},
      {153, 135}}},

    {1, 0, {-1}, {-1}, {-1}, {-1}, {{151, 135}}},

    {1, 0, {-1}, {-1}, {-1}, {-1}, {{151, 135}}},
};

SlipInputPointerPosition SlipInput_Pointer(void) {
	return (SlipInputPointerPosition){(uint16_t)((uint32_t)SlipInput_pointerX >> SLIP_INPUT_POINTER_FRACTION_BITS),
	                                  (uint16_t)((uint32_t)SlipInput_pointerY >> SLIP_INPUT_POINTER_FRACTION_BITS)};
}

uint32_t SlipInput_HitTest(const SlipInputRectangle *rectangle, uint16_t count, int16_t x, int16_t y) {
	uint32_t index = 1;
	do {
		if (x >= rectangle->left && x <= rectangle->right && y >= rectangle->top && y <= rectangle->bottom)
			return index;
		++index;
		++rectangle;
		count = (uint16_t)(count - 1u);
	} while (count != 0);
	return 0;
}

bool SlipInput_pressed[SLIP_INPUT_CODE_COUNT], SlipInput_held[SLIP_INPUT_CODE_COUNT];

SlipInputCode SlipInput_PopPressed(bool pressed[SLIP_INPUT_CODE_COUNT]) {
	unsigned code = 0;
	unsigned remaining = SLIP_INPUT_CODE_COUNT;
	do {
		if (pressed[code]) {
			pressed[code] = false;
			return (SlipInputCode)code;
		}
		++code;
	} while (--remaining != 0);
	return SLIP_INPUT_SCAN_NONE;
}

SlipInputCode SlipInput_PopMenuPressed(bool pressed[SLIP_INPUT_CODE_COUNT]) {
	unsigned code = 0;
	unsigned remaining = SLIP_INPUT_CODE_COUNT;
	do {
		bool examine = true;
		if (SlipInput_navigation.active != NULL) {
			if (code == SLIP_INPUT_SCAN_UP || code == SLIP_INPUT_SCAN_DOWN || code == SLIP_INPUT_SCAN_LEFT ||
			    code == SLIP_INPUT_SCAN_RIGHT)
				examine = false;
		}
		if (examine && pressed[code]) {
			pressed[code] = false;
			return (SlipInputCode)code;
		}
		++code;
	} while (--remaining != 0);
	return SLIP_INPUT_SCAN_NONE;
}

SlipInputNavigationTable SlipInput_garageNavigation[SLIP_INPUT_GARAGE_TABLE_COUNT] = {

    {3, 0, {-1, 0, 1}, {1, 2, -1}, {-1, -1, -1}, {-1, -1, -1}, {{63, 122}, {63, 150}, {63, 178}}},

    {12,
     0,
     {-1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 6, 7},
     {4, 5, 6, 7, 8, 9, 10, 11, -1, -1, -1, -1},
     {-1, 0, 1, 2, -1, 4, 5, 6, -1, 8, 9, 10},
     {1, 2, 3, -1, 5, 6, 7, -1, 9, 10, 11, -1},
     {{54, 106},
      {122, 106},
      {190, 106},
      {258, 106},
      {54, 136},
      {122, 136},
      {190, 136},
      {258, 136},
      {54, 166},
      {122, 166},
      {190, 166},
      {258, 166}}},

    {6,
     0,
     {-1, -1, -1, 0, 1, 2},
     {3, 4, 5, -1, -1, -1},
     {-1, 0, 1, -1, 3, 4},
     {1, 2, -1, 4, 5, -1},
     {{64, 119}, {154, 119}, {244, 119}, {64, 162}, {154, 162}, {244, 162}}},

    {4,
     0,
     {-1, -1, -1, -1},
     {-1, -1, -1, -1},
     {-1, 0, 1, 2},
     {1, 2, 3, -1},
     {{54, 138}, {122, 138}, {190, 138}, {258, 138}}},

    {4,
     0,
     {-1, 0, 1, 2},
     {1, 2, 3, -1},
     {-1, -1, -1, -1},
     {-1, -1, -1, -1},
     {{63, 93}, {63, 121}, {63, 149}, {63, 177}}}};
