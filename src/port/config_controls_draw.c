#include "config_controls.h"
#include "menu_palette.h"
#include "sprite_format.h"

enum {
	SLIP_CONTROL_WORKING_SPRITE_LEFT = 39,
	SLIP_CONTROL_WORKING_SPRITE_TOP = 53,
	SLIP_CONTROL_VERTICAL_MOVEMENT_LEFT = 48,
	SLIP_CONTROL_VERTICAL_MOVEMENT_RIGHT = 111,
	SLIP_CONTROL_LEFT_MOVEMENT_LEFT = 16,
	SLIP_CONTROL_LEFT_MOVEMENT_RIGHT = 79,
	SLIP_CONTROL_RIGHT_MOVEMENT_LEFT = 83,
	SLIP_CONTROL_RIGHT_MOVEMENT_RIGHT = 146,
	SLIP_CONTROL_ACTION_LEFT = 154,
	SLIP_CONTROL_ACTION_RIGHT = 217,
	SLIP_CONTROL_HEADER_LEFT = 16,
	SLIP_CONTROL_HEADER_RIGHT = 148,
	SLIP_CONTROL_HEADER_Y = 16,
	SLIP_CONTROL_FIRST_BINDING_Y = 34,
	SLIP_CONTROL_BINDING_ROW_SPACING = 12,
	SLIP_CONTROL_FOOTER_Y = 78,
	SLIP_CONTROL_MOVEMENT_BLOCKER_LEFT = 15,
	SLIP_CONTROL_MOVEMENT_BLOCKER_TOP = 33,
	SLIP_CONTROL_BUTTON_TEXT_INSET_Y = 3,
	SLIP_CONTROL_MESSAGE_TITLE_Y = 10,
	SLIP_CONTROL_MESSAGE_BODY_Y = 35,
	SLIP_CONTROL_MESSAGE_MARGIN_X = 15
};

#include "string_tags.h"

SlipTextArgument SlipConfigControls_keyName;

SlipConfigControlNames SlipConfigControls_Names(void) {
	static const char *const names[SLIP_MOVEMENT_COUNT] = {
	    [SLIP_MOVEMENT_KEYBOARD] = "Keyboard",
	    [SLIP_MOVEMENT_JOYSTICK_ONE] = "Joystick 1",
	    [SLIP_MOVEMENT_JOYSTICK_TWO] = "Joystick 2",
	    [SLIP_MOVEMENT_MOUSE] = "Mouse",
	};
	return (SlipConfigControlNames){names[SlipRace_controlBindings[0].movementControl],
	                                names[SlipRace_controlBindings[1].movementControl]};
}

SlipConfigControlRecords SlipConfigControls_Records(void) {
	return (SlipConfigControlRecords){&SlipRace_controlBindings[0], &SlipRace_controlBindings[1]};
}

void SlipConfigControls_Draw(const SlipConfigControlsState *state, uint16_t smallFont, SlipStringTableSlot *strings,
                             const SlipConfigControlsDrawCalls *calls) {
	const SlipConfigMenuDrawCalls *const draw = calls->draw;
	void *const context = draw->context;
	SlipConfigControlDimensions size = calls->dimensions(calls->context, state->workingSprite);
	calls->bindSprite(calls->context, state->workingSprite, SLIP_SPRITE_HEADER_BYTES, size.width, size);
	draw->clippedSprite(context, state->background, 0, 0);

	const SlipConfigMenuRectangle rectangles[] = {
	    {(int16_t)state->background, (int16_t)state->workingSprite, (int16_t)state->movementBlocker, 10},
	    {55, 69, 187, 77},
	    {193, 69, 256, 77},
	    {87, 87, 150, 95},
	    {55, 99, 118, 107},
	    {122, 99, 185, 107},
	    {87, 111, 150, 119},
	    {193, 87, 256, 95},
	    {193, 99, 256, 107},
	    {193, 111, 256, 119},
	    {122, 131, 185, 139}};
	SlipConfigMenuRectangle selected = rectangles[state->selection];
	selected.left = (int16_t)(selected.left - SLIP_CONTROL_WORKING_SPRITE_LEFT);
	selected.right = (int16_t)(selected.right - SLIP_CONTROL_WORKING_SPRITE_LEFT);
	selected.top = (int16_t)(selected.top - SLIP_CONTROL_WORKING_SPRITE_TOP);
	selected.bottom = (int16_t)(selected.bottom - SLIP_CONTROL_WORKING_SPRITE_TOP);
	calls->fill(calls->context, SLIP_MENU_CONTROL_SELECTION_COLOUR, selected);
	draw->font(context, smallFont);
	draw->textColor(context, SLIP_MENU_CONTROL_LABEL_COLOUR);
	const char *text;
	draw->style(context, SLIP_TEXT_CENTERED, UINT16_MAX, SLIP_CONTROL_VERTICAL_MOVEMENT_LEFT,
	            SLIP_CONTROL_VERTICAL_MOVEMENT_RIGHT);
	SlipConfigControls_keyName.text = calls->inputName(calls->context, (uint16_t)state->controlBinding->up);
	text = draw->string(context, strings, SLIP_STRING_CONTROL_UP);
	draw->text(context, text, &SlipConfigControls_keyName, (SlipTextPosition){0, SLIP_CONTROL_FIRST_BINDING_Y});
	draw->unlockStrings(context, strings);
	draw->style(context, SLIP_TEXT_CENTERED, UINT16_MAX, SLIP_CONTROL_VERTICAL_MOVEMENT_LEFT,
	            SLIP_CONTROL_VERTICAL_MOVEMENT_RIGHT);
	SlipConfigControls_keyName.text = calls->inputName(calls->context, (uint16_t)state->controlBinding->down);
	text = draw->string(context, strings, SLIP_STRING_CONTROL_DOWN);
	draw->text(context, text, &SlipConfigControls_keyName,
	           (SlipTextPosition){0, SLIP_CONTROL_FIRST_BINDING_Y + 2 * SLIP_CONTROL_BINDING_ROW_SPACING});
	draw->unlockStrings(context, strings);
	draw->style(context, SLIP_TEXT_CENTERED, UINT16_MAX, SLIP_CONTROL_LEFT_MOVEMENT_LEFT,
	            SLIP_CONTROL_LEFT_MOVEMENT_RIGHT);
	SlipConfigControls_keyName.text = calls->inputName(calls->context, (uint16_t)state->controlBinding->left);
	text = draw->string(context, strings, SLIP_STRING_CONTROL_LEFT);
	draw->text(context, text, &SlipConfigControls_keyName,
	           (SlipTextPosition){0, SLIP_CONTROL_FIRST_BINDING_Y + 1 * SLIP_CONTROL_BINDING_ROW_SPACING});
	draw->unlockStrings(context, strings);
	draw->style(context, SLIP_TEXT_CENTERED, UINT16_MAX, SLIP_CONTROL_RIGHT_MOVEMENT_LEFT,
	            SLIP_CONTROL_RIGHT_MOVEMENT_RIGHT);
	SlipConfigControls_keyName.text = calls->inputName(calls->context, (uint16_t)state->controlBinding->right);
	text = draw->string(context, strings, SLIP_STRING_CONTROL_RIGHT);
	draw->text(context, text, &SlipConfigControls_keyName,
	           (SlipTextPosition){0, SLIP_CONTROL_FIRST_BINDING_Y + 1 * SLIP_CONTROL_BINDING_ROW_SPACING});
	draw->unlockStrings(context, strings);
	draw->style(context, SLIP_TEXT_CENTERED, UINT16_MAX, SLIP_CONTROL_ACTION_LEFT, SLIP_CONTROL_ACTION_RIGHT);
	SlipConfigControls_keyName.text = calls->inputName(calls->context, (uint16_t)state->controlBinding->select);
	text = draw->string(context, strings, SLIP_STRING_CONTROL_SELECT);
	draw->text(context, text, &SlipConfigControls_keyName, (SlipTextPosition){0, SLIP_CONTROL_FIRST_BINDING_Y});
	draw->unlockStrings(context, strings);
	draw->style(context, SLIP_TEXT_CENTERED, UINT16_MAX, SLIP_CONTROL_ACTION_LEFT, SLIP_CONTROL_ACTION_RIGHT);
	SlipConfigControls_keyName.text = calls->inputName(calls->context, (uint16_t)state->controlBinding->fire);
	text = draw->string(context, strings, SLIP_STRING_CONTROL_FIRE);
	draw->text(context, text, &SlipConfigControls_keyName,
	           (SlipTextPosition){0, SLIP_CONTROL_FIRST_BINDING_Y + 1 * SLIP_CONTROL_BINDING_ROW_SPACING});
	draw->unlockStrings(context, strings);
	draw->style(context, SLIP_TEXT_CENTERED, UINT16_MAX, SLIP_CONTROL_ACTION_LEFT, SLIP_CONTROL_ACTION_RIGHT);
	SlipConfigControls_keyName.text = calls->inputName(calls->context, (uint16_t)state->controlBinding->accelerate);
	text = draw->string(context, strings, SLIP_STRING_CONTROL_ACCELERATE);
	draw->text(context, text, &SlipConfigControls_keyName,
	           (SlipTextPosition){0, SLIP_CONTROL_FIRST_BINDING_Y + 2 * SLIP_CONTROL_BINDING_ROW_SPACING});
	draw->unlockStrings(context, strings);
	draw->style(context, SLIP_TEXT_CENTERED, UINT16_MAX, SLIP_CONTROL_RIGHT_MOVEMENT_LEFT,
	            SLIP_CONTROL_RIGHT_MOVEMENT_RIGHT);
	text = draw->string(context, strings, SLIP_STRING_CONTROL_FOOTER);
	draw->text(context, text, &SlipConfigControls_keyName, (SlipTextPosition){0, SLIP_CONTROL_FOOTER_Y});
	draw->unlockStrings(context, strings);
	draw->style(context, SLIP_TEXT_CENTERED, UINT16_MAX, SLIP_CONTROL_HEADER_LEFT, SLIP_CONTROL_HEADER_RIGHT);
	text = draw->string(context, strings, SLIP_STRING_CONTROL_HEADER);
	draw->text(context, text, &SlipConfigControls_keyName, (SlipTextPosition){0, SLIP_CONTROL_HEADER_Y});
	draw->unlockStrings(context, strings);
	draw->style(context, SLIP_TEXT_CENTERED, UINT16_MAX, SLIP_CONTROL_ACTION_LEFT, SLIP_CONTROL_ACTION_RIGHT);
	SlipConfigControlNames names = SlipConfigControls_Names();
	SlipConfigControlRecords records = SlipConfigControls_Records();
	if (state->player != 1) {
		const char *const name = names.displayedPlayer;
		names.displayedPlayer = names.otherPlayer;
		names.otherPlayer = name;
		SlipRaceControlBinding *const record = records.displayedPlayer;
		records.displayedPlayer = records.otherPlayer;
		records.otherPlayer = record;
	}
	draw->text(context, names.displayedPlayer, &SlipConfigControls_keyName,
	           (SlipTextPosition){0, SLIP_CONTROL_HEADER_Y});
	if (records.displayedPlayer->movementControl != SLIP_MOVEMENT_KEYBOARD)
		draw->clippedSprite(context, state->movementBlocker, SLIP_CONTROL_MOVEMENT_BLOCKER_LEFT,
		                    SLIP_CONTROL_MOVEMENT_BLOCKER_TOP);
	calls->restore(calls->context);
}

void SlipConfigControls_DrawButton(SlipConfigMenuRectangle rectangle, uint16_t sprite, SlipStringTableSlot *strings,
                                   uint32_t tag, const SlipConfigMenuDrawCalls *calls) {
	void *const context = calls->context;
	calls->clip(context, rectangle);
	calls->line(context, SLIP_MENU_CONTROL_BUTTON_LEFT_COLOUR, rectangle.left, rectangle.top, rectangle.left,
	            rectangle.bottom);
	calls->line(context, SLIP_MENU_CONTROL_BUTTON_TOP_COLOUR, rectangle.left, rectangle.top, rectangle.right,
	            rectangle.top);
	calls->line(context, SLIP_MENU_CONTROL_BUTTON_RIGHT_COLOUR, rectangle.right, rectangle.top, rectangle.right,
	            rectangle.bottom);
	calls->line(context, SLIP_MENU_CONTROL_BUTTON_BOTTOM_COLOUR, rectangle.left, rectangle.bottom, rectangle.right,
	            rectangle.bottom);
	SlipConfigMenuRectangle inner = {(int16_t)(rectangle.left + 1), (int16_t)(rectangle.top + 1),
	                                 (int16_t)(rectangle.right - 1), (int16_t)(rectangle.bottom - 1)};
	calls->clip(context, inner);
	calls->clippedSprite(context, sprite, 0, 0);
	calls->clip(context, rectangle);
	if (tag != 0) {
		calls->style(context, SLIP_TEXT_CENTERED, UINT16_MAX, rectangle.left, rectangle.right);
		const char *const text = calls->string(context, strings, tag);
		calls->text(context, text, NULL,
		            (SlipTextPosition){0, (int16_t)(rectangle.top + SLIP_CONTROL_BUTTON_TEXT_INSET_Y)});
		calls->unlockStrings(context, strings);
	}
}

void SlipConfigControls_DrawWarning(const SlipConfigConflictState *state, uint16_t configurationFont,
                                    uint16_t smallFont, SlipStringTableSlot *strings,
                                    const SlipConfigControlsDrawCalls *calls) {
	const SlipConfigMenuDrawCalls *const draw = calls->draw;
	void *const context = draw->context;
	SlipConfigControlDimensions size = calls->dimensions(calls->context, state->workingSprite);
	calls->bindSprite(calls->context, state->workingSprite, SLIP_SPRITE_HEADER_BYTES, size.width, size);
	draw->clippedSprite(context, state->background, 0, 0);
	draw->font(context, configurationFont);
	draw->textColor(context, UINT16_MAX);
	uint16_t sprite = state->inactiveBackground;
	if (state->selection == 1)
		sprite = state->background;
	SlipConfigControls_DrawButton(
	    (SlipConfigMenuRectangle){SLIP_CONTROL_CONFIRM_BUTTON_LEFT, SLIP_CONTROL_CONFIRM_BUTTON_TOP,
	                              SLIP_CONTROL_CONFIRM_BUTTON_RIGHT, SLIP_CONTROL_CONFIRM_BUTTON_BOTTOM},
	    sprite, strings, SLIP_STRING_CALIBRATION_BUTTON, draw);
	SlipConfigMenuRectangle bounds = draw->surface(context);
	draw->clip(context, bounds);
	draw->style(context, SLIP_TEXT_CENTERED, UINT16_MAX, bounds.left, bounds.right);
	const char *text = draw->string(context, strings, SLIP_STRING_CONTROL_CONFLICT_TITLE);
	draw->text(context, text, NULL, (SlipTextPosition){0, SLIP_CONTROL_MESSAGE_TITLE_Y});
	draw->unlockStrings(context, strings);
	draw->font(context, smallFont);
	bounds = draw->surface(context);
	draw->style(context, SLIP_TEXT_JUSTIFIED, UINT16_MAX, (int16_t)(bounds.left + SLIP_CONTROL_MESSAGE_MARGIN_X),
	            (int16_t)(bounds.right - SLIP_CONTROL_MESSAGE_MARGIN_X));
	draw->textColor(context, SLIP_MENU_CONTROL_MESSAGE_COLOUR);
	text = draw->string(context, strings, SLIP_STRING_CONTROL_CONFLICT);
	draw->text(context, text, NULL, (SlipTextPosition){0, SLIP_CONTROL_MESSAGE_BODY_Y});
	draw->unlockStrings(context, strings);
	calls->restore(calls->context);
}

void SlipConfigControls_DrawCalibration(const SlipConfigCalibrationState *state, uint16_t configurationFont,
                                        uint16_t smallFont, SlipStringTableSlot *strings,
                                        const SlipConfigControlsDrawCalls *calls) {
	const SlipConfigMenuDrawCalls *const draw = calls->draw;
	void *const context = draw->context;
	SlipConfigControlDimensions size = calls->dimensions(calls->context, state->workingSprite);
	calls->bindSprite(calls->context, state->workingSprite, SLIP_SPRITE_HEADER_BYTES, size.width, size);
	draw->clippedSprite(context, state->background, 0, 0);
	draw->font(context, configurationFont);
	draw->textColor(context, UINT16_MAX);
	uint16_t sprite = state->inactiveBackground;
	if (state->selection == 1)
		sprite = state->background;
	SlipConfigControls_DrawButton(
	    (SlipConfigMenuRectangle){SLIP_CONTROL_CONFIRM_BUTTON_LEFT, SLIP_CONTROL_CONFIRM_BUTTON_TOP,
	                              SLIP_CONTROL_CONFIRM_BUTTON_RIGHT, SLIP_CONTROL_CONFIRM_BUTTON_BOTTOM},
	    sprite, strings, SLIP_STRING_CALIBRATION_BUTTON, draw);
	SlipConfigMenuRectangle bounds = draw->surface(context);
	draw->clip(context, bounds);
	draw->style(context, SLIP_TEXT_CENTERED, UINT16_MAX, bounds.left, bounds.right);
	const char *text = draw->string(context, strings, SLIP_STRING_FIRST_CALIBRATION + state->joystick);
	draw->text(context, text, NULL, (SlipTextPosition){0, SLIP_CONTROL_MESSAGE_TITLE_Y});
	draw->unlockStrings(context, strings);
	draw->font(context, smallFont);
	bounds = draw->surface(context);
	draw->style(context, SLIP_TEXT_JUSTIFIED, UINT16_MAX, (int16_t)(bounds.left + SLIP_CONTROL_MESSAGE_MARGIN_X),
	            (int16_t)(bounds.right - SLIP_CONTROL_MESSAGE_MARGIN_X));
	draw->textColor(context, SLIP_MENU_CONTROL_MESSAGE_COLOUR);
	text = draw->string(context, strings, SLIP_STRING_JOYSTICK_DETECTION_ZERO + state->detected);
	draw->text(context, text, NULL, (SlipTextPosition){0, SLIP_CONTROL_MESSAGE_BODY_Y});
	draw->unlockStrings(context, strings);
	calls->restore(calls->context);
}
