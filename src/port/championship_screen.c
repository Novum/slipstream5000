#include "championship_screen.h"
#include "config_menu_draw.h"
#include "config_menu_host.h"
#include "game_errors.h"
#include "menu_music.h"
#include "race_results.h"
#include "resource_host.h"
#include "sprite.h"
#include "text_layout.h"

static SlipInputNavigationTable navigation = {2, 0, {-1, -1}, {-1, -1}, {-1, 0}, {1, -1}, {{83, 183}, {233, 183}}};
static const SlipInputRectangle buttons[2] = {{40, 175, 127, 191}, {190, 175, 277, 191}};
static const SlipStringTableResources stringResources = {.load = SlipResourceHost_Load,
                                                         .lock = SlipResourceHost_Lock,
                                                         .unlock = SlipResourceHost_Unlock,
                                                         .release = SlipResourceHost_Release};

static SlipFont SlipChampionship_LockFont(void *context, uint16_t resource) {
	SlipResourceHost_Lock(context, resource);
	SlipResourcePayload payload = SlipResourceHost_Payload(resource);
	SlipFont font;
	if (!SlipFont_FromPayload(&payload, &font))
		SlipGame_ResourceFailure();
	return font;
}

static const SlipFontResourceCalls fontResources = {.lock = SlipChampionship_LockFont,
                                                    .unlock = SlipResourceHost_Unlock};

static void SlipChampionship_SelectFont(uint16_t resource) {
	SlipText_SelectResourceFont(&SlipText_state, resource, &fontResources);
}

static uint16_t SlipChampionship_Load(const char *name) {
	uint16_t resource;
	if (!SlipResourceHost_Load(NULL, name, &resource))
		SlipGame_ResourceFailure();
	return resource;
}

static void SlipChampionship_Sprite(uint16_t resource) {
	SlipResourceHost_Lock(NULL, resource);
	SlipResourcePayload payload = SlipResourceHost_Payload(resource);
	SlipSprite view;
	if (!SlipSprite_FromPayload(&payload, &view))
		SlipGame_ResourceFailure();
	SlipSprite_DrawClipped(&view, g_screenBufferBase, g_screenPitch, 0, 0);
	SlipResourceHost_Unlock(NULL, resource);
}

static void SlipChampionship_FullClip(void) {
	RasterSurfaceBounds bounds = Raster_GetSurfaceBounds();
	Raster_SetClipRect((int16_t)bounds.left, (int16_t)bounds.top, (int16_t)bounds.right, (int16_t)bounds.bottom);
}

static void SlipChampionship_Panel(SlipInputRectangle rectangle, uint16_t resource, SlipStringTableSlot *strings,
                                   uint32_t tag, int16_t textInset) {
	Raster_SetClipRect(rectangle.left, rectangle.top, rectangle.right, rectangle.bottom);
	Raster_DrawLineSolid(0x25, rectangle.left, rectangle.top, rectangle.left, rectangle.bottom);
	Raster_DrawLineSolid(0x2b, rectangle.left, rectangle.top, rectangle.right, rectangle.top);
	Raster_DrawLineSolid(0x0a, rectangle.left, rectangle.bottom, rectangle.right, rectangle.bottom);
	Raster_DrawLineSolid(0x0a, rectangle.right, rectangle.top, rectangle.right, rectangle.bottom);
	Raster_SetClipRect(rectangle.left + 1, rectangle.top + 1, rectangle.right - 1, rectangle.bottom - 1);
	SlipChampionship_Sprite(resource);
	Raster_SetClipRect(rectangle.left, rectangle.top, rectangle.right, rectangle.bottom);
	if (tag != 0) {
		SlipText_SetStyle(&SlipText_state, 2, UINT16_MAX, rectangle.left, rectangle.right);
		const char *const text = SlipStringTable_Get(strings, tag, &stringResources);
		SlipTextPosition position = {0, (int16_t)(rectangle.top + textInset)};
		SlipText_Draw(&SlipText_state, text, SlipMenu_panelArguments, &position);
		SlipStringTable_Unlock(strings, &stringResources);
	}
}

static void SlipChampionship_Row(const SlipRaceRacerState *racer, int16_t y) {
	SlipTextPosition position = {40, y};
	SlipText_Draw(&SlipText_state, SlipRaceResults_driverNames[racer->tuningIndex], NULL, &position);
	int16_t rank = (int16_t)racer->championshipPosition;
	SlipTextArgument argument = {.word = &rank};
	position = (SlipTextPosition){20, position.y};
	SlipText_Draw(&SlipText_state, "%d.", &argument, &position);
	int16_t points = (int16_t)racer->championshipPoints;
	argument.word = &points;
	position = (SlipTextPosition){250, position.y};
	SlipText_Draw(&SlipText_state, "%d", &argument, &position);
}

static uint32_t championshipRounds;

SlipChampionshipAction SlipChampionship_Screen(uint16_t rounds, const SlipRaceRacerTable *racers) {
	championshipRounds = rounds;
	if (!SlipMenuMusic_StandingsStart())
		SlipGame_ResourceFailure();
	const uint16_t computerFont = SlipChampionship_Load("RESULTSA.FNT");
	SlipChampionship_SelectFont(computerFont);
	const uint16_t localFont = SlipChampionship_Load("RESULTSB.FNT");
	SlipStringTableSlot *strings;
	if (!SlipStringTable_Load(&SlipStringTable_state, "CHAMPPOS", &stringResources, &strings))
		SlipGame_ResourceFailure();
	const uint16_t inactive = SlipChampionship_Load("RACERESD.SPR");
	const uint16_t background = SlipChampionship_Load("RACERES.SPR");
	SlipConfigHost_calls.palette(SlipConfigHost_calls.context, background);
	SlipInput_SetNavigation(&navigation);
	SlipConfigHost_calls.resetTimer(SlipConfigHost_calls.context);
	SlipChampionshipAction action;
	do {
		SlipConfigHost_calls.updateTimer(SlipConfigHost_calls.context);
		SlipInputPointerPosition pointer = SlipInput_Pointer();
		const uint32_t hovered = SlipInput_HitTest(buttons, 2, pointer.x, pointer.y);
		SlipChampionship_FullClip();
		SlipChampionship_Sprite(background);
		SlipText_SetColor(&SlipText_state, UINT16_MAX);
		SlipChampionship_SelectFont(computerFont);
		for (unsigned button = 0; button < 2; ++button)
			SlipChampionship_Panel(buttons[button], hovered == button + 1 ? background : inactive, strings,
			                       0x42555431u + button, 4);
		SlipChampionship_Panel((SlipInputRectangle){59, 10, 258, 26}, inactive, strings, 0x5449544cu, 4);
		SlipChampionship_FullClip();
		SlipText_SetStyle(&SlipText_state, 0, UINT16_MAX, 20, 300);
		int16_t y = 40;
		for (uint16_t rank = 1; rank <= racers->racerCount; ++rank) {
			uint16_t index = 0;
			while (index < racers->racerCount && racers->records[index].championshipPosition != rank)
				++index;
			const SlipRaceRacerState *const racer = &racers->records[index];
			SlipChampionship_SelectFont(racer->racerType == 2 ? computerFont : localFont);
			SlipChampionship_Row(racer, y);
			y = (int16_t)(y + 13);
		}
		SlipConfigHost_calls.present(SlipConfigHost_calls.context);
		SlipConfigHost_calls.poll(SlipConfigHost_calls.context);
		action = SlipChampionship_ReadInput(hovered, SlipInput_pressed);
	} while (action == SLIP_CHAMPIONSHIP_WAIT);
	SlipInput_ClearNavigation();
	SlipStringTable_Release(strings, &stringResources);
	SlipMenuMusic_StandingsStop();
	SlipResourceHost_Release(NULL, background);
	SlipResourceHost_Release(NULL, inactive);
	SlipResourceHost_Release(NULL, computerFont);
	SlipResourceHost_Release(NULL, localFont);
	return action;
}

static SlipInputNavigationTable finalNavigation = {1, 0, {-1}, {-1}, {-1}, {-1}, {{159, 183}}};
static const SlipInputRectangle finalButton = {109, 175, 209, 191};

void SlipChampionship_FinalScreen(const SlipRaceRacerTable *racers) {
	SlipStringTableSlot *strings;
	if (!SlipStringTable_Load(&SlipStringTable_state, "FINALPOS", &stringResources, &strings))
		SlipGame_ResourceFailure();
	const uint16_t computerFont = SlipChampionship_Load("RESULTSC.FNT");
	SlipChampionship_SelectFont(computerFont);
	const uint16_t localFont = SlipChampionship_Load("RESULTSD.FNT");
	const uint16_t inactive = SlipChampionship_Load("FINPOSD.SPR");
	const uint16_t background = SlipChampionship_Load("FINALPOS.SPR");
	SlipConfigHost_calls.palette(SlipConfigHost_calls.context, background);
	SlipInput_SetNavigation(&finalNavigation);
	SlipConfigHost_calls.resetTimer(SlipConfigHost_calls.context);
	bool accepted;
	do {
		SlipConfigHost_calls.updateTimer(SlipConfigHost_calls.context);
		SlipInputPointerPosition pointer = SlipInput_Pointer();
		const uint32_t hovered = SlipInput_HitTest(&finalButton, 1, pointer.x, pointer.y);
		SlipChampionship_FullClip();
		SlipChampionship_Sprite(background);
		SlipText_SetColor(&SlipText_state, UINT16_MAX);
		SlipChampionship_SelectFont(computerFont);
		SlipChampionship_Panel((SlipInputRectangle){92, 11, 226, 27}, inactive, strings, 0x5449544cu, 5);
		SlipChampionship_Panel(finalButton, hovered != 0 ? background : inactive, strings, 0x42555431u, 5);
		SlipChampionship_FullClip();
		SlipText_SetStyle(&SlipText_state, 0, UINT16_MAX, 20, 300);
		int16_t y = 40;
		for (uint16_t rank = 1; rank <= racers->racerCount; ++rank) {
			uint16_t index = 0;
			while (index < racers->racerCount && racers->records[index].championshipPosition != rank)
				++index;
			const SlipRaceRacerState *const racer = &racers->records[index];
			SlipChampionship_SelectFont(racer->racerType == 2 ? computerFont : localFont);

			SlipChampionship_Row(racer, y);
			y = (int16_t)(y + 13);
		}
		SlipConfigHost_calls.present(SlipConfigHost_calls.context);
		SlipConfigHost_calls.poll(SlipConfigHost_calls.context);
		accepted = SlipChampionshipFinal_ReadInput(hovered, SlipInput_pressed);
	} while (!accepted);
	SlipResourceHost_Release(NULL, background);
	SlipResourceHost_Release(NULL, inactive);
	SlipResourceHost_Release(NULL, computerFont);
	SlipResourceHost_Release(NULL, localFont);
	SlipStringTable_Release(strings, &stringResources);
	SlipInput_ClearNavigation();
}
