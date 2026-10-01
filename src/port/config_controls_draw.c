#include "config_controls.h"

SlipTextArgument SlipConfigControls_keyName;

SlipConfigControlNames SlipConfigControls_Names(void) {
	static const char *const names[] = {"Keyboard", "Joystick 1", "Joystick 2", "Mouse"};
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
	calls->bindSprite(calls->context, state->workingSprite, 16, size.width, size);
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
	selected.left = (int16_t)(selected.left - 39);
	selected.right = (int16_t)(selected.right - 39);
	selected.top = (int16_t)(selected.top - 53);
	selected.bottom = (int16_t)(selected.bottom - 53);
	calls->fill(calls->context, 0x37, selected);
	draw->font(context, smallFont);
	draw->textColor(context, 0x18);
	const char *text;
	draw->style(context, 2, UINT16_MAX, 48, 111);
	SlipConfigControls_keyName.text = calls->inputName(calls->context, (uint16_t)state->controlBinding->up);
	text = draw->string(context, strings, 0x44454630u);
	draw->text(context, text, &SlipConfigControls_keyName, (SlipTextPosition){0, 34});
	draw->unlockStrings(context, strings);
	draw->style(context, 2, UINT16_MAX, 48, 111);
	SlipConfigControls_keyName.text = calls->inputName(calls->context, (uint16_t)state->controlBinding->down);
	text = draw->string(context, strings, 0x44454631u);
	draw->text(context, text, &SlipConfigControls_keyName, (SlipTextPosition){0, 58});
	draw->unlockStrings(context, strings);
	draw->style(context, 2, UINT16_MAX, 16, 79);
	SlipConfigControls_keyName.text = calls->inputName(calls->context, (uint16_t)state->controlBinding->left);
	text = draw->string(context, strings, 0x44454632u);
	draw->text(context, text, &SlipConfigControls_keyName, (SlipTextPosition){0, 46});
	draw->unlockStrings(context, strings);
	draw->style(context, 2, UINT16_MAX, 83, 146);
	SlipConfigControls_keyName.text = calls->inputName(calls->context, (uint16_t)state->controlBinding->right);
	text = draw->string(context, strings, 0x44454633u);
	draw->text(context, text, &SlipConfigControls_keyName, (SlipTextPosition){0, 46});
	draw->unlockStrings(context, strings);
	draw->style(context, 2, UINT16_MAX, 154, 217);
	SlipConfigControls_keyName.text = calls->inputName(calls->context, (uint16_t)state->controlBinding->select);
	text = draw->string(context, strings, 0x44454634u);
	draw->text(context, text, &SlipConfigControls_keyName, (SlipTextPosition){0, 34});
	draw->unlockStrings(context, strings);
	draw->style(context, 2, UINT16_MAX, 154, 217);
	SlipConfigControls_keyName.text = calls->inputName(calls->context, (uint16_t)state->controlBinding->fire);
	text = draw->string(context, strings, 0x44454635u);
	draw->text(context, text, &SlipConfigControls_keyName, (SlipTextPosition){0, 46});
	draw->unlockStrings(context, strings);
	draw->style(context, 2, UINT16_MAX, 154, 217);
	SlipConfigControls_keyName.text = calls->inputName(calls->context, (uint16_t)state->controlBinding->accelerate);
	text = draw->string(context, strings, 0x44454636u);
	draw->text(context, text, &SlipConfigControls_keyName, (SlipTextPosition){0, 58});
	draw->unlockStrings(context, strings);
	draw->style(context, 2, UINT16_MAX, 83, 146);
	text = draw->string(context, strings, 0x4445464fu);
	draw->text(context, text, &SlipConfigControls_keyName, (SlipTextPosition){0, 78});
	draw->unlockStrings(context, strings);
	draw->style(context, 2, UINT16_MAX, 16, 148);
	text = draw->string(context, strings, 0x4445464du);
	draw->text(context, text, &SlipConfigControls_keyName, (SlipTextPosition){0, 16});
	draw->unlockStrings(context, strings);
	draw->style(context, 2, UINT16_MAX, 154, 217);
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
	draw->text(context, names.displayedPlayer, &SlipConfigControls_keyName, (SlipTextPosition){0, 16});
	if (records.displayedPlayer->movementControl != 0)
		draw->clippedSprite(context, state->movementBlocker, 15, 33);
	calls->restore(calls->context);
}

void SlipConfigControls_DrawButton(SlipConfigMenuRectangle rectangle, uint16_t sprite, SlipStringTableSlot *strings,
                                   uint32_t tag, const SlipConfigMenuDrawCalls *calls) {
	void *const context = calls->context;
	calls->clip(context, rectangle);
	calls->line(context, 0xa4, rectangle.left, rectangle.top, rectangle.left, rectangle.bottom);
	calls->line(context, 0xa5, rectangle.left, rectangle.top, rectangle.right, rectangle.top);
	calls->line(context, 0x8c, rectangle.right, rectangle.top, rectangle.right, rectangle.bottom);
	calls->line(context, 0x8b, rectangle.left, rectangle.bottom, rectangle.right, rectangle.bottom);
	SlipConfigMenuRectangle inner = {(int16_t)(rectangle.left + 1), (int16_t)(rectangle.top + 1),
	                                 (int16_t)(rectangle.right - 1), (int16_t)(rectangle.bottom - 1)};
	calls->clip(context, inner);
	calls->clippedSprite(context, sprite, 0, 0);
	calls->clip(context, rectangle);
	if (tag != 0) {
		calls->style(context, 2, UINT16_MAX, rectangle.left, rectangle.right);
		const char *const text = calls->string(context, strings, tag);
		calls->text(context, text, NULL, (SlipTextPosition){0, (int16_t)(rectangle.top + 3)});
		calls->unlockStrings(context, strings);
	}
}

void SlipConfigControls_DrawWarning(const SlipConfigConflictState *state, uint16_t configurationFont,
                                    uint16_t smallFont, SlipStringTableSlot *strings,
                                    const SlipConfigControlsDrawCalls *calls) {
	const SlipConfigMenuDrawCalls *const draw = calls->draw;
	void *const context = draw->context;
	SlipConfigControlDimensions size = calls->dimensions(calls->context, state->workingSprite);
	calls->bindSprite(calls->context, state->workingSprite, 16, size.width, size);
	draw->clippedSprite(context, state->background, 0, 0);
	draw->font(context, configurationFont);
	draw->textColor(context, UINT16_MAX);
	uint16_t sprite = state->inactiveBackground;
	if (state->selection == 1)
		sprite = state->background;
	SlipConfigControls_DrawButton((SlipConfigMenuRectangle){83, 75, 141, 90}, sprite, strings, 0x4a434231u, draw);
	SlipConfigMenuRectangle bounds = draw->surface(context);
	draw->clip(context, bounds);
	draw->style(context, 2, UINT16_MAX, bounds.left, bounds.right);
	const char *text = draw->string(context, strings, 0x434e4654u);
	draw->text(context, text, NULL, (SlipTextPosition){0, 10});
	draw->unlockStrings(context, strings);
	draw->font(context, smallFont);
	bounds = draw->surface(context);
	draw->style(context, 1, UINT16_MAX, (int16_t)(bounds.left + 15), (int16_t)(bounds.right - 15));
	draw->textColor(context, 0xff);
	text = draw->string(context, strings, 0x434f4e46u);
	draw->text(context, text, NULL, (SlipTextPosition){0, 35});
	draw->unlockStrings(context, strings);
	calls->restore(calls->context);
}

void SlipConfigControls_DrawCalibration(const SlipConfigCalibrationState *state, uint16_t configurationFont,
                                        uint16_t smallFont, SlipStringTableSlot *strings,
                                        const SlipConfigControlsDrawCalls *calls) {
	const SlipConfigMenuDrawCalls *const draw = calls->draw;
	void *const context = draw->context;
	SlipConfigControlDimensions size = calls->dimensions(calls->context, state->workingSprite);
	calls->bindSprite(calls->context, state->workingSprite, 16, size.width, size);
	draw->clippedSprite(context, state->background, 0, 0);
	draw->font(context, configurationFont);
	draw->textColor(context, UINT16_MAX);
	uint16_t sprite = state->inactiveBackground;
	if (state->selection == 1)
		sprite = state->background;
	SlipConfigControls_DrawButton((SlipConfigMenuRectangle){83, 75, 141, 90}, sprite, strings, 0x4a434231u, draw);
	SlipConfigMenuRectangle bounds = draw->surface(context);
	draw->clip(context, bounds);
	draw->style(context, 2, UINT16_MAX, bounds.left, bounds.right);
	const char *text = draw->string(context, strings, 0x43414c31u + state->joystick);
	draw->text(context, text, NULL, (SlipTextPosition){0, 10});
	draw->unlockStrings(context, strings);
	draw->font(context, smallFont);
	bounds = draw->surface(context);
	draw->style(context, 1, UINT16_MAX, (int16_t)(bounds.left + 15), (int16_t)(bounds.right - 15));
	draw->textColor(context, 0xff);
	text = draw->string(context, strings, 0x4a435431u + state->detected);
	draw->text(context, text, NULL, (SlipTextPosition){0, 35});
	draw->unlockStrings(context, strings);
	calls->restore(calls->context);
}
