#include "config_menu_draw.h"

const SlipConfigMenuRectangle SlipConfigMenu_mainRectangles[7] = {
    {100, 10, 220, 28},   {25, 43, 157, 61},  {163, 43, 281, 61}, {25, 65, 157, 83},
    {113, 171, 207, 189}, {163, 65, 281, 83}, {25, 87, 157, 105}};
const SlipConfigMenuRectangle SlipConfigMenu_difficultyRectangles[4] = {
    {25, 43, 281, 59}, {25, 63, 281, 79}, {100, 169, 195, 187}, {100, 10, 220, 28}};
const uint32_t SlipConfigMenu_mainTags[7] = {0x5449544c, 0x42555431, 0x42555432, 0x42555433,
                                             0x42555434, 0x42555435, 0x42555436};
const uint32_t SlipConfigMenu_difficultyTags[4] = {0, 0, 0x42555431, 0x5449544c};
const uint32_t *SlipConfigMenu_mainTagCursor;
const uint32_t *SlipConfigMenu_difficultyTagCursor;
const SlipTextArgument *SlipMenu_panelArguments;

void SlipMenu_DrawPanel(SlipConfigMenuRectangle rectangle, uint16_t sprite, SlipStringTableSlot *strings, uint32_t tag,
                        const SlipConfigMenuDrawCalls *calls) {
	void *const context = calls->context;
	calls->clip(context, rectangle);
	calls->line(context, 0x25, rectangle.left, rectangle.top, rectangle.left, rectangle.bottom);
	calls->line(context, 0x2b, rectangle.left, rectangle.top, rectangle.right, rectangle.top);
	calls->line(context, 0x0a, rectangle.left, rectangle.bottom, rectangle.right, rectangle.bottom);
	calls->line(context, 0x0a, rectangle.right, rectangle.top, rectangle.right, rectangle.bottom);
	SlipConfigMenuRectangle inner = {(int16_t)(rectangle.left + 1), (int16_t)(rectangle.top + 1),
	                                 (int16_t)(rectangle.right - 1), (int16_t)(rectangle.bottom - 1)};
	calls->clip(context, inner);
	calls->clippedSprite(context, sprite, 0, 0);
	calls->clip(context, rectangle);
	if (tag != 0) {
		calls->style(context, 2, UINT16_MAX, rectangle.left, rectangle.right);
		const char *const text = calls->string(context, strings, tag);
		calls->text(context, text, SlipMenu_panelArguments, (SlipTextPosition){0, (int16_t)(rectangle.top + 5)});
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
	for (unsigned row = 0; row < 7; ++row) {
		uint16_t sprite = state->inactiveBackground;
		if (--selection == 0)
			sprite = state->background;
		SlipMenu_DrawPanel(SlipConfigMenu_mainRectangles[row], sprite, state->mainStrings,
		                   *SlipConfigMenu_mainTagCursor, calls);
		++SlipConfigMenu_mainTagCursor;
	}
	if (state->allowDifficulty == 0)
		calls->unclippedSprite(context, state->blocker, SlipConfigMenu_mainRectangles[5].left,
		                       SlipConfigMenu_mainRectangles[5].top);
}

void SlipConfigMenu_DrawDifficulty(const SlipConfigMenuState *state, const SlipConfigMenuDrawCalls *calls) {
	void *const context = calls->context;
	calls->clip(context, calls->surface(context));
	calls->clippedSprite(context, state->background, 0, 0);
	uint32_t selection = state->difficultySelection;
	SlipConfigMenu_difficultyTagCursor = SlipConfigMenu_difficultyTags;
	for (unsigned row = 0; row < 4; ++row) {
		uint16_t sprite = state->inactiveBackground;
		if (--selection == 0)
			sprite = state->background;
		SlipMenu_DrawPanel(SlipConfigMenu_difficultyRectangles[row], sprite, state->difficultyStrings,
		                   *SlipConfigMenu_difficultyTagCursor, calls);
		++SlipConfigMenu_difficultyTagCursor;
	}
	calls->clip(context, calls->surface(context));
	calls->style(context, 2, UINT16_MAX, 204, 281);
	calls->textColor(context, UINT16_MAX);
	uint32_t tag = calls->difficulty(context) + 0x4c455630u;
	const char *text = calls->string(context, state->difficultyStrings, tag);
	calls->text(context, text, NULL, (SlipTextPosition){0, 47});
	calls->unlockStrings(context, state->difficultyStrings);
	tag = calls->damage(context) + 0x4f464630u;
	text = calls->string(context, state->difficultyStrings, tag);
	calls->text(context, text, NULL, (SlipTextPosition){0, 67});
	calls->unlockStrings(context, state->difficultyStrings);
	calls->style(context, 0, UINT16_MAX, 25, 200);
	int16_t y = 46;
	tag = 0x43415431u;
	for (unsigned row = 0; row < 2; ++row) {
		text = calls->string(context, state->difficultyStrings, tag);
		calls->text(context, text, NULL, (SlipTextPosition){31, y});
		calls->unlockStrings(context, state->difficultyStrings);
		y = (int16_t)(y + 20);
		++tag;
	}
}

const SlipConfigMenuRectangle SlipConfigMenu_generalRectangles[7] = {
    {25, 63, 281, 79},   {25, 83, 281, 99},    {25, 103, 281, 119}, {25, 123, 281, 139},
    {25, 143, 281, 159}, {100, 169, 195, 187}, {100, 10, 220, 28}};
const uint32_t SlipConfigMenu_generalTags[7] = {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x42555431u, 0x5449544cu};
const uint32_t *SlipConfigMenu_generalTagCursor;

void SlipConfigMenu_DrawGeneral(const SlipConfigMenuState *state, const SlipConfigMenuDrawCalls *calls) {
	void *const context = calls->context;
	calls->clip(context, calls->surface(context));
	calls->clippedSprite(context, state->background, 0, 0);
	uint32_t selection = state->generalSelection;
	SlipConfigMenu_generalTagCursor = SlipConfigMenu_generalTags;
	for (unsigned row = 0; row < 7; ++row) {
		uint16_t sprite = state->inactiveBackground;
		if (--selection == 0)
			sprite = state->background;
		SlipMenu_DrawPanel(SlipConfigMenu_generalRectangles[row], sprite, state->generalStrings,
		                   *SlipConfigMenu_generalTagCursor, calls);
		++SlipConfigMenu_generalTagCursor;
	}
	calls->clip(context, calls->surface(context));
	calls->style(context, 2, UINT16_MAX, 204, 281);
	calls->textColor(context, UINT16_MAX);
	uint32_t tag;
	const char *text;
	tag = calls->rearMonitor(context) + 0x4f464630u;
	text = calls->string(context, state->generalStrings, tag);
	calls->text(context, text, NULL, (SlipTextPosition){0, 67});
	calls->unlockStrings(context, state->generalStrings);
	tag = calls->weaponsMonitor(context) + 0x4f464630u;
	text = calls->string(context, state->generalStrings, tag);
	calls->text(context, text, NULL, (SlipTextPosition){0, 87});
	calls->unlockStrings(context, state->generalStrings);
	text = calls->language(context);
	calls->text(context, text, NULL, (SlipTextPosition){0, 107});
	tag = calls->trackMap(context) + 0x4f464630u;
	text = calls->string(context, state->generalStrings, tag);
	calls->text(context, text, NULL, (SlipTextPosition){0, 127});
	calls->unlockStrings(context, state->generalStrings);
	const uint32_t speed = calls->speedDisplay(context);
	text = "mph";
	if (speed != 0)
		text = "km/h";
	calls->text(context, text, NULL, (SlipTextPosition){0, 147});
	calls->style(context, 0, UINT16_MAX, 25, 200);
	int16_t y = 66;
	tag = 0x43415431u;
	for (unsigned row = 0; row < 5; ++row) {
		text = calls->string(context, state->generalStrings, tag);
		calls->text(context, text, NULL, (SlipTextPosition){31, y});
		calls->unlockStrings(context, state->generalStrings);
		y = (int16_t)(y + 20);
		++tag;
	}
}

const SlipConfigMenuRectangle SlipConfigMenu_detailRectangles[8] = {
    {25, 43, 281, 59},   {25, 63, 281, 79},   {25, 83, 281, 99},    {25, 103, 281, 119},
    {25, 123, 281, 139}, {25, 143, 281, 159}, {100, 169, 195, 187}, {100, 10, 220, 28}};
const uint32_t SlipConfigMenu_detailTags[8] = {0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x0u, 0x42555431u, 0x5449544cu};
const uint32_t *SlipConfigMenu_detailTagCursor;

void SlipConfigMenu_DrawDetail(const SlipConfigMenuState *state, const SlipConfigMenuDrawCalls *calls) {
	void *const context = calls->context;
	calls->clip(context, calls->surface(context));
	calls->clippedSprite(context, state->background, 0, 0);
	uint32_t selection = state->detailSelection;
	SlipConfigMenu_detailTagCursor = SlipConfigMenu_detailTags;
	for (unsigned row = 0; row < 8; ++row) {
		uint16_t sprite = state->inactiveBackground;
		if (--selection == 0)
			sprite = state->background;
		SlipMenu_DrawPanel(SlipConfigMenu_detailRectangles[row], sprite, state->detailStrings,
		                   *SlipConfigMenu_detailTagCursor, calls);
		++SlipConfigMenu_detailTagCursor;
	}
	calls->clip(context, calls->surface(context));
	calls->style(context, 2, UINT16_MAX, 190, 281);
	calls->textColor(context, UINT16_MAX);
	uint32_t tag;
	const char *text;
	text = calls->environment(context);
	calls->text(context, text, NULL, (SlipTextPosition){0, 47});
	tag = calls->clouds(context) + 0x4f464630u;
	text = calls->string(context, state->detailStrings, tag);
	calls->text(context, text, NULL, (SlipTextPosition){0, 67});
	calls->unlockStrings(context, state->detailStrings);
	tag = calls->shading(context) + 0x53484130u;
	text = calls->string(context, state->detailStrings, tag);
	calls->text(context, text, NULL, (SlipTextPosition){0, 87});
	calls->unlockStrings(context, state->detailStrings);
	const uint32_t textures = calls->textures(context);
	if (textures == 0)
		tag = 0x4f464630u;
	else if (textures == 1)
		tag = 0x54455843u;
	else
		tag = 0x54455846u;
	text = calls->string(context, state->detailStrings, tag);
	calls->text(context, text, NULL, (SlipTextPosition){0, 107});
	calls->unlockStrings(context, state->detailStrings);
	tag = calls->window(context) + 0x57494e30u;
	text = calls->string(context, state->detailStrings, tag);
	calls->text(context, text, NULL, (SlipTextPosition){0, 127});
	calls->unlockStrings(context, state->detailStrings);
	tag = (uint32_t)calls->shadows(context) + 0x4f464630u;
	text = calls->string(context, state->detailStrings, tag);
	calls->text(context, text, NULL, (SlipTextPosition){0, 147});
	calls->unlockStrings(context, state->detailStrings);
	calls->style(context, 0, UINT16_MAX, 25, 200);
	int16_t y = 46;
	tag = 0x43415431u;
	for (unsigned row = 0; row < 6; ++row) {
		text = calls->string(context, state->detailStrings, tag);
		calls->text(context, text, NULL, (SlipTextPosition){31, y});
		calls->unlockStrings(context, state->detailStrings);
		y = (int16_t)(y + 20);
		++tag;
	}
}

const SlipConfigMenuRectangle SlipConfigMenu_soundRectangles[6] = {{25, 43, 281, 59},    {25, 63, 281, 79},
                                                                   {25, 83, 281, 99},    {25, 103, 281, 119},
                                                                   {100, 169, 195, 187}, {100, 10, 220, 28}};
const uint32_t SlipConfigMenu_soundTags[6] = {0x0u, 0x0u, 0x0u, 0x0u, 0x42555431u, 0x5449544cu};
const uint32_t *SlipConfigMenu_soundTagCursor;

void SlipConfigMenu_DrawSound(const SlipConfigMenuState *state, const SlipConfigMenuDrawCalls *calls) {
	void *const context = calls->context;
	calls->clip(context, calls->surface(context));
	calls->clippedSprite(context, state->background, 0, 0);
	uint32_t selection = state->soundSelection;
	SlipConfigMenu_soundTagCursor = SlipConfigMenu_soundTags;
	for (unsigned row = 0; row < 6; ++row) {
		uint16_t sprite = state->inactiveBackground;
		if (--selection == 0)
			sprite = state->background;
		SlipMenu_DrawPanel(SlipConfigMenu_soundRectangles[row], sprite, state->soundStrings,
		                   *SlipConfigMenu_soundTagCursor, calls);
		++SlipConfigMenu_soundTagCursor;
	}
	calls->clip(context, calls->surface(context));
	calls->style(context, 2, UINT16_MAX, 204, 281);
	calls->textColor(context, UINT16_MAX);
	uint32_t tag;
	const char *text;
	tag = calls->effects(context) + 0x4f464630u;
	text = calls->string(context, state->soundStrings, tag);
	calls->text(context, text, NULL, (SlipTextPosition){0, 47});
	calls->unlockStrings(context, state->soundStrings);
	tag = calls->engines(context) + 0x454e4730u;
	text = calls->string(context, state->soundStrings, tag);
	calls->text(context, text, NULL, (SlipTextPosition){0, 67});
	calls->unlockStrings(context, state->soundStrings);
	tag = calls->speech(context) + 0x4f464630u;
	text = calls->string(context, state->soundStrings, tag);
	calls->text(context, text, NULL, (SlipTextPosition){0, 87});
	calls->unlockStrings(context, state->soundStrings);
	tag = calls->music(context) + 0x454e4730u;
	text = calls->string(context, state->soundStrings, tag);
	calls->text(context, text, NULL, (SlipTextPosition){0, 107});
	calls->unlockStrings(context, state->soundStrings);
	calls->style(context, 0, UINT16_MAX, 25, 200);
	int16_t y = 46;
	tag = 0x43415431u;
	for (unsigned row = 0; row < 4; ++row) {
		text = calls->string(context, state->soundStrings, tag);
		calls->text(context, text, NULL, (SlipTextPosition){31, y});
		calls->unlockStrings(context, state->soundStrings);
		y = (int16_t)(y + 20);
		++tag;
	}
}

const SlipConfigMenuRectangle SlipConfigMenu_controlsRectangles[7] = {
    {25, 43, 281, 61},   {25, 65, 281, 83},    {25, 87, 281, 105}, {25, 109, 281, 127},
    {25, 131, 281, 149}, {100, 169, 195, 187}, {100, 10, 220, 28}};
const uint32_t SlipConfigMenu_controlsTags[7] = {0x504c5931u, 0x504c5932u, 0x43414c31u, 0x43414c32u,
                                                 0x0u,        0x42555431u, 0x5449544cu};
const uint32_t *SlipConfigMenu_controlsTagCursor;

void SlipConfigMenu_DrawControls(const SlipConfigMenuState *state, const SlipConfigMenuDrawCalls *calls) {
	void *const context = calls->context;
	calls->clip(context, calls->surface(context));
	calls->clippedSprite(context, state->background, 0, 0);
	calls->font(context, state->font);
	calls->textColor(context, UINT16_MAX);
	uint32_t selection = state->controlsSelection;
	SlipConfigMenu_controlsTagCursor = SlipConfigMenu_controlsTags;
	for (unsigned row = 0; row < 7; ++row) {
		uint16_t sprite = state->inactiveBackground;
		if (--selection == 0)
			sprite = state->background;
		SlipMenu_DrawPanel(SlipConfigMenu_controlsRectangles[row], sprite, state->controlsStrings,
		                   *SlipConfigMenu_controlsTagCursor, calls);
		++SlipConfigMenu_controlsTagCursor;
	}
	calls->clip(context, calls->surface(context));
	calls->style(context, 2, UINT16_MAX, 204, 281);
	calls->textColor(context, UINT16_MAX);
	uint32_t tag;
	const char *text;
	tag = calls->reverseAccelerator(context) + 0x4f464630u;
	text = calls->string(context, state->controlsStrings, tag);
	calls->text(context, text, NULL, (SlipTextPosition){0, 135});
	calls->unlockStrings(context, state->controlsStrings);
	calls->style(context, 0, UINT16_MAX, 25, 200);
	text = calls->string(context, state->controlsStrings, 0x52455641u);
	calls->text(context, text, NULL, (SlipTextPosition){31, 135});
	calls->unlockStrings(context, state->controlsStrings);
}
