#include "config_menu_draw.h"
#include "config_settings.h"
#include "menu_palette.h"
#include "string_tags.h"

/* Layout shared by the configuration option pages. */
enum {
	SLIP_CONFIG_PANEL_TEXT_INSET_Y = 5,
	SLIP_CONFIG_OPTION_LABEL_LEFT = 25,
	SLIP_CONFIG_OPTION_LABEL_RIGHT = 200,
	SLIP_CONFIG_OPTION_LABEL_X = 31,
	SLIP_CONFIG_OPTION_VALUE_LEFT = 204,
	SLIP_CONFIG_DETAIL_VALUE_LEFT = 190,
	SLIP_CONFIG_OPTION_VALUE_RIGHT = 281,
	SLIP_CONFIG_OPTION_FIRST_LABEL_Y = 46,
	SLIP_CONFIG_OPTION_FIRST_VALUE_Y = SLIP_CONFIG_OPTION_FIRST_LABEL_Y + 1,
	SLIP_CONFIG_OPTION_ROW_SPACING = 20,
	SLIP_CONFIG_REVERSE_ACCELERATOR_TEXT_Y = 135
};

const SlipConfigMenuRectangle SlipConfigMenu_mainRectangles[SLIP_CONFIG_MAIN_RECTANGLE_COUNT] = {
    {100, 10, 220, 28},   {25, 43, 157, 61},  {163, 43, 281, 61}, {25, 65, 157, 83},
    {113, 171, 207, 189}, {163, 65, 281, 83}, {25, 87, 157, 105}};
const SlipConfigMenuRectangle SlipConfigMenu_difficultyRectangles[SLIP_CONFIG_DIFFICULTY_RECTANGLE_COUNT] = {
    {25, 43, 281, 59}, {25, 63, 281, 79}, {100, 169, 195, 187}, {100, 10, 220, 28}};
const uint32_t SlipConfigMenu_mainTags[SLIP_CONFIG_MAIN_RECTANGLE_COUNT] = {SLIP_STRING_TITLE,
                                                                            SLIP_STRING_FIRST_BUTTON,
                                                                            (SLIP_STRING_FIRST_BUTTON + 1u),
                                                                            (SLIP_STRING_FIRST_BUTTON + 2u),
                                                                            (SLIP_STRING_FIRST_BUTTON + 3u),
                                                                            (SLIP_STRING_FIRST_BUTTON + 4u),
                                                                            (SLIP_STRING_FIRST_BUTTON + 5u)};
const uint32_t SlipConfigMenu_difficultyTags[SLIP_CONFIG_DIFFICULTY_RECTANGLE_COUNT] = {0, 0, SLIP_STRING_FIRST_BUTTON,
                                                                                        SLIP_STRING_TITLE};
const uint32_t *SlipConfigMenu_mainTagCursor;
const uint32_t *SlipConfigMenu_difficultyTagCursor;
const SlipTextArgument *SlipMenu_panelArguments;

void SlipMenu_DrawPanel(SlipConfigMenuRectangle rectangle, uint16_t sprite, SlipStringTableSlot *strings, uint32_t tag,
                        const SlipConfigMenuDrawCalls *calls) {
	void *const context = calls->context;
	calls->clip(context, rectangle);
	calls->line(context, SLIP_MENU_PANEL_LEFT_COLOUR, rectangle.left, rectangle.top, rectangle.left, rectangle.bottom);
	calls->line(context, SLIP_MENU_PANEL_TOP_COLOUR, rectangle.left, rectangle.top, rectangle.right, rectangle.top);
	calls->line(context, SLIP_MENU_PANEL_SHADOW_COLOUR, rectangle.left, rectangle.bottom, rectangle.right,
	            rectangle.bottom);
	calls->line(context, SLIP_MENU_PANEL_SHADOW_COLOUR, rectangle.right, rectangle.top, rectangle.right,
	            rectangle.bottom);
	SlipConfigMenuRectangle inner = {(int16_t)(rectangle.left + 1), (int16_t)(rectangle.top + 1),
	                                 (int16_t)(rectangle.right - 1), (int16_t)(rectangle.bottom - 1)};
	calls->clip(context, inner);
	calls->clippedSprite(context, sprite, 0, 0);
	calls->clip(context, rectangle);
	if (tag != 0) {
		calls->style(context, SLIP_TEXT_CENTERED, UINT16_MAX, rectangle.left, rectangle.right);
		const char *const text = calls->string(context, strings, tag);
		calls->text(context, text, SlipMenu_panelArguments,
		            (SlipTextPosition){0, (int16_t)(rectangle.top + SLIP_CONFIG_PANEL_TEXT_INSET_Y)});
		calls->unlockStrings(context, strings);
	}
}

void SlipConfigMenu_DrawMain(const SlipConfigMenuState *state, const SlipConfigMenuDrawCalls *calls) {
	void *const context = calls->context;
	calls->clip(context, calls->surface(context));
	calls->clippedSprite(context, state->background, 0, 0);
	calls->font(context, state->font);
	calls->textColor(context, UINT16_MAX);
	uint32_t selection = state->mainSelection;
	SlipConfigMenu_mainTagCursor = SlipConfigMenu_mainTags;
	for (unsigned row = 0; row < SLIP_CONFIG_MAIN_RECTANGLE_COUNT; ++row) {
		uint16_t sprite = state->inactiveBackground;
		if (--selection == 0)
			sprite = state->background;
		SlipMenu_DrawPanel(SlipConfigMenu_mainRectangles[row], sprite, state->mainStrings,
		                   *SlipConfigMenu_mainTagCursor, calls);
		++SlipConfigMenu_mainTagCursor;
	}
	if (state->allowDifficulty == 0)
		calls->unclippedSprite(context, state->blocker,
		                       SlipConfigMenu_mainRectangles[SLIP_CONFIG_MAIN_DIFFICULTY - 1].left,
		                       SlipConfigMenu_mainRectangles[SLIP_CONFIG_MAIN_DIFFICULTY - 1].top);
}

void SlipConfigMenu_DrawDifficulty(const SlipConfigMenuState *state, const SlipConfigMenuDrawCalls *calls) {
	void *const context = calls->context;
	calls->clip(context, calls->surface(context));
	calls->clippedSprite(context, state->background, 0, 0);
	uint32_t selection = state->difficultySelection;
	SlipConfigMenu_difficultyTagCursor = SlipConfigMenu_difficultyTags;
	for (unsigned row = 0; row < SLIP_CONFIG_DIFFICULTY_RECTANGLE_COUNT; ++row) {
		uint16_t sprite = state->inactiveBackground;
		if (--selection == 0)
			sprite = state->background;
		SlipMenu_DrawPanel(SlipConfigMenu_difficultyRectangles[row], sprite, state->difficultyStrings,
		                   *SlipConfigMenu_difficultyTagCursor, calls);
		++SlipConfigMenu_difficultyTagCursor;
	}
	calls->clip(context, calls->surface(context));
	calls->style(context, SLIP_TEXT_CENTERED, UINT16_MAX, SLIP_CONFIG_OPTION_VALUE_LEFT,
	             SLIP_CONFIG_OPTION_VALUE_RIGHT);
	calls->textColor(context, UINT16_MAX);
	uint32_t tag = calls->difficulty(context) + SLIP_STRING_DIFFICULTY_LEVEL_ZERO;
	const char *text = calls->string(context, state->difficultyStrings, tag);
	calls->text(context, text, NULL, (SlipTextPosition){0, SLIP_CONFIG_OPTION_FIRST_VALUE_Y});
	calls->unlockStrings(context, state->difficultyStrings);
	tag = calls->damage(context) + SLIP_STRING_SETTING_OFF;
	text = calls->string(context, state->difficultyStrings, tag);
	calls->text(context, text, NULL,
	            (SlipTextPosition){0, SLIP_CONFIG_OPTION_FIRST_VALUE_Y + 1 * SLIP_CONFIG_OPTION_ROW_SPACING});
	calls->unlockStrings(context, state->difficultyStrings);
	calls->style(context, SLIP_TEXT_AT_POSITION, UINT16_MAX, SLIP_CONFIG_OPTION_LABEL_LEFT,
	             SLIP_CONFIG_OPTION_LABEL_RIGHT);
	int16_t y = SLIP_CONFIG_OPTION_FIRST_LABEL_Y;
	tag = SLIP_STRING_FIRST_CATEGORY;
	for (unsigned row = 0; row < SLIP_CONFIG_DIFFICULTY_OPTION_COUNT; ++row) {
		text = calls->string(context, state->difficultyStrings, tag);
		calls->text(context, text, NULL, (SlipTextPosition){SLIP_CONFIG_OPTION_LABEL_X, y});
		calls->unlockStrings(context, state->difficultyStrings);
		y = (int16_t)(y + SLIP_CONFIG_OPTION_ROW_SPACING);
		++tag;
	}
}

const SlipConfigMenuRectangle SlipConfigMenu_generalRectangles[SLIP_CONFIG_GENERAL_RECTANGLE_COUNT] = {
    {25, 63, 281, 79},   {25, 83, 281, 99},    {25, 103, 281, 119}, {25, 123, 281, 139},
    {25, 143, 281, 159}, {100, 169, 195, 187}, {100, 10, 220, 28}};
const uint32_t SlipConfigMenu_generalTags[SLIP_CONFIG_GENERAL_RECTANGLE_COUNT] = {
    0, 0, 0, 0, 0, SLIP_STRING_FIRST_BUTTON, SLIP_STRING_TITLE};
const uint32_t *SlipConfigMenu_generalTagCursor;

void SlipConfigMenu_DrawGeneral(const SlipConfigMenuState *state, const SlipConfigMenuDrawCalls *calls) {
	void *const context = calls->context;
	calls->clip(context, calls->surface(context));
	calls->clippedSprite(context, state->background, 0, 0);
	uint32_t selection = state->generalSelection;
	SlipConfigMenu_generalTagCursor = SlipConfigMenu_generalTags;
	for (unsigned row = 0; row < SLIP_CONFIG_GENERAL_RECTANGLE_COUNT; ++row) {
		uint16_t sprite = state->inactiveBackground;
		if (--selection == 0)
			sprite = state->background;
		SlipMenu_DrawPanel(SlipConfigMenu_generalRectangles[row], sprite, state->generalStrings,
		                   *SlipConfigMenu_generalTagCursor, calls);
		++SlipConfigMenu_generalTagCursor;
	}
	calls->clip(context, calls->surface(context));
	calls->style(context, SLIP_TEXT_CENTERED, UINT16_MAX, SLIP_CONFIG_OPTION_VALUE_LEFT,
	             SLIP_CONFIG_OPTION_VALUE_RIGHT);
	calls->textColor(context, UINT16_MAX);
	uint32_t tag;
	const char *text;
	tag = calls->rearMonitor(context) + SLIP_STRING_SETTING_OFF;
	text = calls->string(context, state->generalStrings, tag);
	calls->text(context, text, NULL,
	            (SlipTextPosition){0, SLIP_CONFIG_OPTION_FIRST_VALUE_Y + 1 * SLIP_CONFIG_OPTION_ROW_SPACING});
	calls->unlockStrings(context, state->generalStrings);
	tag = calls->weaponsMonitor(context) + SLIP_STRING_SETTING_OFF;
	text = calls->string(context, state->generalStrings, tag);
	calls->text(context, text, NULL,
	            (SlipTextPosition){0, SLIP_CONFIG_OPTION_FIRST_VALUE_Y + 2 * SLIP_CONFIG_OPTION_ROW_SPACING});
	calls->unlockStrings(context, state->generalStrings);
	text = calls->language(context);
	calls->text(context, text, NULL,
	            (SlipTextPosition){0, SLIP_CONFIG_OPTION_FIRST_VALUE_Y + 3 * SLIP_CONFIG_OPTION_ROW_SPACING});
	tag = calls->trackMap(context) + SLIP_STRING_SETTING_OFF;
	text = calls->string(context, state->generalStrings, tag);
	calls->text(context, text, NULL,
	            (SlipTextPosition){0, SLIP_CONFIG_OPTION_FIRST_VALUE_Y + 4 * SLIP_CONFIG_OPTION_ROW_SPACING});
	calls->unlockStrings(context, state->generalStrings);
	const uint32_t speed = calls->speedDisplay(context);
	text = "mph";
	if (speed != 0)
		text = "km/h";
	calls->text(context, text, NULL,
	            (SlipTextPosition){0, SLIP_CONFIG_OPTION_FIRST_VALUE_Y + 5 * SLIP_CONFIG_OPTION_ROW_SPACING});
	calls->style(context, SLIP_TEXT_AT_POSITION, UINT16_MAX, SLIP_CONFIG_OPTION_LABEL_LEFT,
	             SLIP_CONFIG_OPTION_LABEL_RIGHT);
	int16_t y = SLIP_CONFIG_OPTION_FIRST_LABEL_Y + SLIP_CONFIG_OPTION_ROW_SPACING;
	tag = SLIP_STRING_FIRST_CATEGORY;
	for (unsigned row = 0; row < SLIP_CONFIG_GENERAL_OPTION_COUNT; ++row) {
		text = calls->string(context, state->generalStrings, tag);
		calls->text(context, text, NULL, (SlipTextPosition){SLIP_CONFIG_OPTION_LABEL_X, y});
		calls->unlockStrings(context, state->generalStrings);
		y = (int16_t)(y + SLIP_CONFIG_OPTION_ROW_SPACING);
		++tag;
	}
}

const SlipConfigMenuRectangle SlipConfigMenu_detailRectangles[SLIP_CONFIG_DETAIL_RECTANGLE_COUNT] = {
    {25, 43, 281, 59},   {25, 63, 281, 79},   {25, 83, 281, 99},    {25, 103, 281, 119},
    {25, 123, 281, 139}, {25, 143, 281, 159}, {100, 169, 195, 187}, {100, 10, 220, 28}};
const uint32_t SlipConfigMenu_detailTags[SLIP_CONFIG_DETAIL_RECTANGLE_COUNT] = {
    0, 0, 0, 0, 0, 0, SLIP_STRING_FIRST_BUTTON, SLIP_STRING_TITLE};
const uint32_t *SlipConfigMenu_detailTagCursor;

void SlipConfigMenu_DrawDetail(const SlipConfigMenuState *state, const SlipConfigMenuDrawCalls *calls) {
	void *const context = calls->context;
	calls->clip(context, calls->surface(context));
	calls->clippedSprite(context, state->background, 0, 0);
	uint32_t selection = state->detailSelection;
	SlipConfigMenu_detailTagCursor = SlipConfigMenu_detailTags;
	for (unsigned row = 0; row < SLIP_CONFIG_DETAIL_RECTANGLE_COUNT; ++row) {
		uint16_t sprite = state->inactiveBackground;
		if (--selection == 0)
			sprite = state->background;
		SlipMenu_DrawPanel(SlipConfigMenu_detailRectangles[row], sprite, state->detailStrings,
		                   *SlipConfigMenu_detailTagCursor, calls);
		++SlipConfigMenu_detailTagCursor;
	}
	calls->clip(context, calls->surface(context));
	calls->style(context, SLIP_TEXT_CENTERED, UINT16_MAX, SLIP_CONFIG_DETAIL_VALUE_LEFT,
	             SLIP_CONFIG_OPTION_VALUE_RIGHT);
	calls->textColor(context, UINT16_MAX);
	uint32_t tag;
	const char *text;
	text = calls->environment(context);
	calls->text(context, text, NULL, (SlipTextPosition){0, SLIP_CONFIG_OPTION_FIRST_VALUE_Y});
	tag = calls->clouds(context) + SLIP_STRING_SETTING_OFF;
	text = calls->string(context, state->detailStrings, tag);
	calls->text(context, text, NULL,
	            (SlipTextPosition){0, SLIP_CONFIG_OPTION_FIRST_VALUE_Y + 1 * SLIP_CONFIG_OPTION_ROW_SPACING});
	calls->unlockStrings(context, state->detailStrings);
	tag = calls->shading(context) + SLIP_STRING_SHADING_ZERO;
	text = calls->string(context, state->detailStrings, tag);
	calls->text(context, text, NULL,
	            (SlipTextPosition){0, SLIP_CONFIG_OPTION_FIRST_VALUE_Y + 2 * SLIP_CONFIG_OPTION_ROW_SPACING});
	calls->unlockStrings(context, state->detailStrings);
	const uint32_t textures = calls->textures(context);
	if (textures == SLIP_CONFIG_TEXTURE_OFF)
		tag = SLIP_STRING_SETTING_OFF;
	else if (textures == SLIP_CONFIG_TEXTURE_COARSE)
		tag = SLIP_STRING_TEXTURE_COARSE;
	else
		tag = SLIP_STRING_TEXTURE_FINE;
	text = calls->string(context, state->detailStrings, tag);
	calls->text(context, text, NULL,
	            (SlipTextPosition){0, SLIP_CONFIG_OPTION_FIRST_VALUE_Y + 3 * SLIP_CONFIG_OPTION_ROW_SPACING});
	calls->unlockStrings(context, state->detailStrings);
	tag = calls->window(context) + SLIP_STRING_WINDOW_ZERO;
	text = calls->string(context, state->detailStrings, tag);
	calls->text(context, text, NULL,
	            (SlipTextPosition){0, SLIP_CONFIG_OPTION_FIRST_VALUE_Y + 4 * SLIP_CONFIG_OPTION_ROW_SPACING});
	calls->unlockStrings(context, state->detailStrings);
	tag = (uint32_t)calls->shadows(context) + SLIP_STRING_SETTING_OFF;
	text = calls->string(context, state->detailStrings, tag);
	calls->text(context, text, NULL,
	            (SlipTextPosition){0, SLIP_CONFIG_OPTION_FIRST_VALUE_Y + 5 * SLIP_CONFIG_OPTION_ROW_SPACING});
	calls->unlockStrings(context, state->detailStrings);
	calls->style(context, SLIP_TEXT_AT_POSITION, UINT16_MAX, SLIP_CONFIG_OPTION_LABEL_LEFT,
	             SLIP_CONFIG_OPTION_LABEL_RIGHT);
	int16_t y = SLIP_CONFIG_OPTION_FIRST_LABEL_Y;
	tag = SLIP_STRING_FIRST_CATEGORY;
	for (unsigned row = 0; row < SLIP_CONFIG_DETAIL_OPTION_COUNT; ++row) {
		text = calls->string(context, state->detailStrings, tag);
		calls->text(context, text, NULL, (SlipTextPosition){SLIP_CONFIG_OPTION_LABEL_X, y});
		calls->unlockStrings(context, state->detailStrings);
		y = (int16_t)(y + SLIP_CONFIG_OPTION_ROW_SPACING);
		++tag;
	}
}

const SlipConfigMenuRectangle SlipConfigMenu_soundRectangles[SLIP_CONFIG_SOUND_RECTANGLE_COUNT] = {
    {25, 43, 281, 59},   {25, 63, 281, 79},    {25, 83, 281, 99},
    {25, 103, 281, 119}, {100, 169, 195, 187}, {100, 10, 220, 28}};
const uint32_t SlipConfigMenu_soundTags[SLIP_CONFIG_SOUND_RECTANGLE_COUNT] = {
    0, 0, 0, 0, SLIP_STRING_FIRST_BUTTON, SLIP_STRING_TITLE};
const uint32_t *SlipConfigMenu_soundTagCursor;

void SlipConfigMenu_DrawSound(const SlipConfigMenuState *state, const SlipConfigMenuDrawCalls *calls) {
	void *const context = calls->context;
	calls->clip(context, calls->surface(context));
	calls->clippedSprite(context, state->background, 0, 0);
	uint32_t selection = state->soundSelection;
	SlipConfigMenu_soundTagCursor = SlipConfigMenu_soundTags;
	for (unsigned row = 0; row < SLIP_CONFIG_SOUND_RECTANGLE_COUNT; ++row) {
		uint16_t sprite = state->inactiveBackground;
		if (--selection == 0)
			sprite = state->background;
		SlipMenu_DrawPanel(SlipConfigMenu_soundRectangles[row], sprite, state->soundStrings,
		                   *SlipConfigMenu_soundTagCursor, calls);
		++SlipConfigMenu_soundTagCursor;
	}
	calls->clip(context, calls->surface(context));
	calls->style(context, SLIP_TEXT_CENTERED, UINT16_MAX, SLIP_CONFIG_OPTION_VALUE_LEFT,
	             SLIP_CONFIG_OPTION_VALUE_RIGHT);
	calls->textColor(context, UINT16_MAX);
	uint32_t tag;
	const char *text;
	tag = calls->effects(context) + SLIP_STRING_SETTING_OFF;
	text = calls->string(context, state->soundStrings, tag);
	calls->text(context, text, NULL, (SlipTextPosition){0, SLIP_CONFIG_OPTION_FIRST_VALUE_Y});
	calls->unlockStrings(context, state->soundStrings);
	tag = calls->engines(context) + SLIP_STRING_SOUND_LEVEL_ZERO;
	text = calls->string(context, state->soundStrings, tag);
	calls->text(context, text, NULL,
	            (SlipTextPosition){0, SLIP_CONFIG_OPTION_FIRST_VALUE_Y + 1 * SLIP_CONFIG_OPTION_ROW_SPACING});
	calls->unlockStrings(context, state->soundStrings);
	tag = calls->speech(context) + SLIP_STRING_SETTING_OFF;
	text = calls->string(context, state->soundStrings, tag);
	calls->text(context, text, NULL,
	            (SlipTextPosition){0, SLIP_CONFIG_OPTION_FIRST_VALUE_Y + 2 * SLIP_CONFIG_OPTION_ROW_SPACING});
	calls->unlockStrings(context, state->soundStrings);
	tag = calls->music(context) + SLIP_STRING_SOUND_LEVEL_ZERO;
	text = calls->string(context, state->soundStrings, tag);
	calls->text(context, text, NULL,
	            (SlipTextPosition){0, SLIP_CONFIG_OPTION_FIRST_VALUE_Y + 3 * SLIP_CONFIG_OPTION_ROW_SPACING});
	calls->unlockStrings(context, state->soundStrings);
	calls->style(context, SLIP_TEXT_AT_POSITION, UINT16_MAX, SLIP_CONFIG_OPTION_LABEL_LEFT,
	             SLIP_CONFIG_OPTION_LABEL_RIGHT);
	int16_t y = SLIP_CONFIG_OPTION_FIRST_LABEL_Y;
	tag = SLIP_STRING_FIRST_CATEGORY;
	for (unsigned row = 0; row < SLIP_CONFIG_SOUND_OPTION_COUNT; ++row) {
		text = calls->string(context, state->soundStrings, tag);
		calls->text(context, text, NULL, (SlipTextPosition){SLIP_CONFIG_OPTION_LABEL_X, y});
		calls->unlockStrings(context, state->soundStrings);
		y = (int16_t)(y + SLIP_CONFIG_OPTION_ROW_SPACING);
		++tag;
	}
}

const SlipConfigMenuRectangle SlipConfigMenu_controlsRectangles[SLIP_CONFIG_CONTROLS_RECTANGLE_COUNT] = {
    {25, 43, 281, 61},   {25, 65, 281, 83},    {25, 87, 281, 105}, {25, 109, 281, 127},
    {25, 131, 281, 149}, {100, 169, 195, 187}, {100, 10, 220, 28}};
const uint32_t SlipConfigMenu_controlsTags[SLIP_CONFIG_CONTROLS_RECTANGLE_COUNT] = {SLIP_STRING_FIRST_PLAYER,
                                                                                    SLIP_STRING_SECOND_PLAYER,
                                                                                    SLIP_STRING_FIRST_CALIBRATION,
                                                                                    SLIP_STRING_SECOND_CALIBRATION,
                                                                                    0,
                                                                                    SLIP_STRING_FIRST_BUTTON,
                                                                                    SLIP_STRING_TITLE};
const uint32_t *SlipConfigMenu_controlsTagCursor;

void SlipConfigMenu_DrawControls(const SlipConfigMenuState *state, const SlipConfigMenuDrawCalls *calls) {
	void *const context = calls->context;
	calls->clip(context, calls->surface(context));
	calls->clippedSprite(context, state->background, 0, 0);
	calls->font(context, state->font);
	calls->textColor(context, UINT16_MAX);
	uint32_t selection = state->controlsSelection;
	SlipConfigMenu_controlsTagCursor = SlipConfigMenu_controlsTags;
	for (unsigned row = 0; row < SLIP_CONFIG_CONTROLS_RECTANGLE_COUNT; ++row) {
		uint16_t sprite = state->inactiveBackground;
		if (--selection == 0)
			sprite = state->background;
		SlipMenu_DrawPanel(SlipConfigMenu_controlsRectangles[row], sprite, state->controlsStrings,
		                   *SlipConfigMenu_controlsTagCursor, calls);
		++SlipConfigMenu_controlsTagCursor;
	}
	calls->clip(context, calls->surface(context));
	calls->style(context, SLIP_TEXT_CENTERED, UINT16_MAX, SLIP_CONFIG_OPTION_VALUE_LEFT,
	             SLIP_CONFIG_OPTION_VALUE_RIGHT);
	calls->textColor(context, UINT16_MAX);
	uint32_t tag;
	const char *text;
	tag = calls->reverseAccelerator(context) + SLIP_STRING_SETTING_OFF;
	text = calls->string(context, state->controlsStrings, tag);
	calls->text(context, text, NULL, (SlipTextPosition){0, SLIP_CONFIG_REVERSE_ACCELERATOR_TEXT_Y});
	calls->unlockStrings(context, state->controlsStrings);
	calls->style(context, SLIP_TEXT_AT_POSITION, UINT16_MAX, SLIP_CONFIG_OPTION_LABEL_LEFT,
	             SLIP_CONFIG_OPTION_LABEL_RIGHT);
	text = calls->string(context, state->controlsStrings, SLIP_STRING_REVERSE_ACCELERATOR);
	calls->text(context, text, NULL,
	            (SlipTextPosition){SLIP_CONFIG_OPTION_LABEL_X, SLIP_CONFIG_REVERSE_ACCELERATOR_TEXT_Y});
	calls->unlockStrings(context, state->controlsStrings);
}
