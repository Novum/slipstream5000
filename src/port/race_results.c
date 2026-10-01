#include "race_results.h"
#include "game_errors.h"
#include "race_hud.h"
#include "raster.h"
#include "resource_host.h"
#include "runtime.h"
#include "text_layout.h"

#include <stdio.h>

const SlipStringTableResources SlipRaceResults_stringResources = {.load = SlipResourceHost_Load,
                                                                  .lock = SlipResourceHost_Lock,
                                                                  .unlock = SlipResourceHost_Unlock,
                                                                  .release = SlipResourceHost_Release};

void SlipRaceResults_LoadAssets(SlipRaceResultsAssets *assets, const char *const *archives, size_t archiveCount,
                                uint16_t language) {
	(void)archives;
	(void)archiveCount;
	(void)language;
	if (!SlipResourceHost_Load(NULL, "RESULTSA.FNT", &assets->computerFontResource))
		SlipGame_ResourceFailure();
	SlipText_SelectResourceFont(&SlipText_state, assets->computerFontResource, &SlipRaceHud_fontResources);
	if (!SlipResourceHost_Load(NULL, "RESULTSB.FNT", &assets->localFontResource))
		SlipGame_ResourceFailure();
	if (!SlipStringTable_Load(&SlipStringTable_state, "RACERES ", &SlipRaceResults_stringResources,
	                          &assets->stringSlot))
		SlipGame_ResourceFailure();
	if (!SlipResourceHost_Load(NULL, "RACERESD.SPR", &assets->inactiveResource))
		SlipGame_ResourceFailure();
	if (!SlipResourceHost_Load(NULL, "RACERES.SPR", &assets->backgroundResource))
		SlipGame_ResourceFailure();

	const uint8_t *const bytes = SlipResourceHost_Lock(NULL, assets->backgroundResource);
	SlipResourcePayload payload = SlipResourceHost_Payload(assets->backgroundResource);
	payload.data = (uint8_t *)bytes;
	SlipSprite sprite;
	(void)SlipSprite_FromPayload(&payload, &sprite);
	SlipSprite_ApplyPalette(&sprite);
	SlipResourceHost_Unlock(NULL, assets->backgroundResource);
}

void SlipRaceResults_ReleaseSprites(SlipRaceResultsAssets *assets) {
	SlipResourceHost_Release(NULL, assets->backgroundResource);
	SlipResourceHost_Release(NULL, assets->inactiveResource);
	SlipResourceHost_Release(NULL, assets->computerFontResource);
	SlipResourceHost_Release(NULL, assets->localFontResource);
}

const SlipRaceResultsRect SlipRaceResults_buttons[2] = {
    {40, 175, 127, 191},
    {190, 175, 277, 191},
};

uint32_t SlipRaceResults_HitTest(int16_t x, int16_t y) {
	for (uint32_t i = 0; i < 2; ++i) {
		const SlipRaceResultsRect *const rect = &SlipRaceResults_buttons[i];
		if (x >= rect->left && x <= rect->right && y >= rect->top && y <= rect->bottom)
			return i + 1;
	}
	return 0;
}

SlipRaceResultsAction SlipRaceResults_ReadInput(uint32_t hoveredButton, bool pressed[256]) {

	if (pressed[SLIP_INPUT_SCAN_ESCAPE]) {
		pressed[SLIP_INPUT_SCAN_ESCAPE] = false;
		return SLIP_RESULTS_CONTINUE;
	}
	if (hoveredButton == 0)
		return SLIP_RESULTS_WAIT;

	if (pressed[SLIP_INPUT_SCAN_ENTER]) {
		pressed[SLIP_INPUT_SCAN_ENTER] = false;
	} else if (pressed[SLIP_INPUT_MOUSE_LEFT]) {
		pressed[SLIP_INPUT_MOUSE_LEFT] = false;
	} else {
		return SLIP_RESULTS_WAIT;
	}

	return ((hoveredButton - 1u) ^ 1u) != 0 ? SLIP_RESULTS_REPLAY : SLIP_RESULTS_CONTINUE;
}

void SlipRaceResults_DrawRacePanel(const SlipSprite *sprite, const SlipFont *font, const SlipRaceResultsRect *rect,
                                   const char *label, uint8_t *framebuffer, int pitch) {
	Raster_SetClipRect(rect->left, rect->top, rect->right, rect->bottom);

	Raster_DrawLineSolid(0x25, rect->left, rect->top, rect->left, rect->bottom);
	Raster_DrawLineSolid(0x2b, rect->left, rect->top, rect->right, rect->top);
	Raster_DrawLineSolid(0x0a, rect->left, rect->bottom, rect->right, rect->bottom);
	Raster_DrawLineSolid(0x0a, rect->right, rect->top, rect->right, rect->bottom);
	Raster_SetClipRect(rect->left + 1, rect->top + 1, rect->right - 1, rect->bottom - 1);
	SlipSprite_DrawClipped(sprite, framebuffer, pitch, 0, 0);
	Raster_SetClipRect(rect->left, rect->top, rect->right, rect->bottom);

	if (label != NULL)
		SlipFont_DrawCenteredLine(font, framebuffer, pitch, label, -1, rect->left, rect->right, rect->top + 4);
}

void SlipRaceResults_DrawFrame(const SlipRaceRacerTable *racers, const SlipSprite *background,
                               const SlipSprite *inactive, const SlipFont *computerFont, const SlipFont *localFont,
                               const char *const buttonLabels[2], const char *title, uint32_t hoveredButton,
                               uint8_t *framebuffer, int pitch) {

	Raster_SetClipRect(0, 0, 319, 199);
	SlipSprite_DrawClipped(background, framebuffer, pitch, 0, 0);

	for (uint32_t i = 0; i < 2; ++i)
		SlipRaceResults_DrawRacePanel(hoveredButton == i + 1 ? background : inactive, computerFont,
		                              &SlipRaceResults_buttons[i], buttonLabels[i], framebuffer, pitch);

	const SlipRaceResultsRect titleRect = {59, 10, 258, 26};
	SlipRaceResults_DrawRacePanel(inactive, computerFont, &titleRect, title, framebuffer, pitch);
	Raster_SetClipRect(0, 0, 319, 199);
	SlipRaceResults_DrawRows(racers, computerFont, localFont, framebuffer, pitch);
}

static void SlipRaceResults_DrawResourcePanel(uint16_t spriteResource, SlipStringTableSlot *strings,
                                              const SlipRaceResultsRect *rect, uint32_t tag, uint8_t *framebuffer,
                                              int pitch) {
	Raster_SetClipRect(rect->left, rect->top, rect->right, rect->bottom);
	Raster_DrawLineSolid(0x25, rect->left, rect->top, rect->left, rect->bottom);
	Raster_DrawLineSolid(0x2b, rect->left, rect->top, rect->right, rect->top);
	Raster_DrawLineSolid(0x0a, rect->left, rect->bottom, rect->right, rect->bottom);
	Raster_DrawLineSolid(0x0a, rect->right, rect->top, rect->right, rect->bottom);
	Raster_SetClipRect(rect->left + 1, rect->top + 1, rect->right - 1, rect->bottom - 1);
	SlipRaceHud_DrawSpriteResourceClipped(spriteResource, framebuffer, pitch, 0, 0);
	Raster_SetClipRect(rect->left, rect->top, rect->right, rect->bottom);
	if (tag != 0) {
		SlipText_SetStyle(&SlipText_state, 2, UINT16_MAX, rect->left, rect->right);
		const char *const label = SlipStringTable_Get(strings, tag, &SlipRaceResults_stringResources);
		SlipTextPosition position = {0, (int16_t)(rect->top + 4)};
		SlipText_Draw(&SlipText_state, label, NULL, &position);
		SlipStringTable_Unlock(strings, &SlipRaceResults_stringResources);
	}
}

void SlipRaceResults_DrawResources(const SlipRaceRacerTable *racers, const SlipRaceResultsAssets *assets,
                                   uint16_t track, uint32_t hoveredButton, uint8_t *framebuffer, int pitch) {
	Raster_SetClipRect(0, 0, 319, 199);
	SlipRaceHud_DrawSpriteResourceClipped(assets->backgroundResource, framebuffer, pitch, 0, 0);
	SlipText_SetColor(&SlipText_state, UINT16_MAX);
	SlipText_SelectResourceFont(&SlipText_state, assets->computerFontResource, &SlipRaceHud_fontResources);
	for (uint32_t i = 0; i < 2; ++i)
		SlipRaceResults_DrawResourcePanel(
		    hoveredButton == i + 1 ? assets->backgroundResource : assets->inactiveResource, assets->stringSlot,
		    &SlipRaceResults_buttons[i], 0x42555431u + i, framebuffer, pitch);
	const SlipRaceResultsRect titleRect = {59, 10, 258, 26};
	SlipRaceResults_DrawResourcePanel(assets->inactiveResource, assets->stringSlot, &titleRect, 0x5449542fu + track,
	                                  framebuffer, pitch);
	Raster_SetClipRect(0, 0, 319, 199);
	SlipText_SetStyle(&SlipText_state, 0, UINT16_MAX, 20, 300);
	uint16_t y = 40;
	for (uint16_t rank = 1; rank <= racers->racerCount; ++rank) {
		uint16_t index = 0;
		for (; index < racers->racerCount; ++index)
			if (racers->records[index].racePosition == rank)
				break;
		const SlipRaceRacerState *const racer = &racers->records[index];
		SlipText_SelectResourceFont(&SlipText_state,
		                            racer->racerType == 2 ? assets->computerFontResource : assets->localFontResource,
		                            &SlipRaceHud_fontResources);

		SlipTextPosition position = {40, (int16_t)y};
		SlipText_Draw(&SlipText_state, SlipRaceResults_driverNames[racer->tuningIndex], NULL, &position);
		char rankText[8];
		snprintf(rankText, sizeof(rankText), "%d.", (int16_t)racer->racePosition);
		position = (SlipTextPosition){20, (int16_t)y};
		SlipText_Draw(&SlipText_state, rankText, NULL, &position);
		char time[12];
		const char *timeText = "Retired";
		if (racer->finished != 0) {
			SlipRaceHud_FormatTime(racer->totalRaceTime, time);
			time[5] = '\'';
			time[8] = '"';
			timeText = time + 3;
		}
		position = (SlipTextPosition){240, (int16_t)y};
		SlipText_Draw(&SlipText_state, timeText, NULL, &position);
		y = (uint16_t)(y + 13);
	}
}

const char *const SlipRaceResults_driverNames[11] = {
    NULL,
    "Charles Edward-Royce",
    "Rysho",
    "Horst",
    "Victoria Venice",
    "The Shaman",
    "Slayed",
    "Cobra",
    "Isis the Crisis",
    "Kin and Gin Matsu",
    "Ted 'Malibu' Beech",
};

void SlipRaceResults_DrawRow(const SlipRaceRacerState *racer, const SlipFont *font, uint8_t *framebuffer, int pitch,
                             uint16_t y) {
	char position[8];
	char time[12];
	const char *timeText;

	SlipFont_DrawTextClipped(font, framebuffer, pitch, 40, y, SlipRaceResults_driverNames[racer->tuningIndex], -1, 20,
	                         0, 300, 199);

	snprintf(position, sizeof(position), "%d.", (int16_t)racer->racePosition);
	SlipFont_DrawTextClipped(font, framebuffer, pitch, 20, y, position, -1, 20, 0, 300, 199);

	if (racer->finished == 0) {
		timeText = "Retired";
	} else {
		SlipRaceHud_FormatTime(racer->totalRaceTime, time);
		time[5] = '\'';
		time[8] = '"';
		timeText = time + 3;
	}
	SlipFont_DrawTextClipped(font, framebuffer, pitch, 240, y, timeText, -1, 20, 0, 300, 199);
}

void SlipRaceResults_DrawRows(const SlipRaceRacerTable *racers, const SlipFont *computerFont, const SlipFont *localFont,
                              uint8_t *framebuffer, int pitch) {
	uint16_t y = 40;

	for (uint16_t position = 1; position <= racers->racerCount; ++position) {
		uint16_t index = 0;
		for (; index < racers->racerCount; ++index) {
			if (racers->records[index].racePosition == position)
				break;
		}
		const SlipRaceRacerState *const racer = &racers->records[index];
		const SlipFont *const font = racer->racerType == 2 ? computerFont : localFont;
		SlipRaceResults_DrawRow(racer, font, framebuffer, pitch, y);
		y = (uint16_t)(y + 13);
	}
}

void SlipChampionship_DrawRow(const SlipRaceRacerState *racer, const SlipFont *font, uint8_t *framebuffer, int pitch,
                              uint16_t y) {
	char text[8];
	SlipFont_DrawTextClipped(font, framebuffer, pitch, 40, y, SlipRaceResults_driverNames[racer->tuningIndex], -1, 20,
	                         0, 300, 199);
	snprintf(text, sizeof(text), "%d.", (int16_t)racer->championshipPosition);
	SlipFont_DrawTextClipped(font, framebuffer, pitch, 20, y, text, -1, 20, 0, 300, 199);
	snprintf(text, sizeof(text), "%d", (int16_t)racer->championshipPoints);
	SlipFont_DrawTextClipped(font, framebuffer, pitch, 250, y, text, -1, 20, 0, 300, 199);
}

void SlipChampionship_DrawRows(const SlipRaceRacerTable *racers, const SlipFont *computerFont,
                               const SlipFont *localFont, uint8_t *framebuffer, int pitch) {
	uint16_t y = 40;
	for (uint16_t rank = 1; rank <= racers->racerCount; ++rank) {
		uint16_t index = 0;
		for (; index < racers->racerCount; ++index)
			if (racers->records[index].championshipPosition == rank)
				break;
		const SlipRaceRacerState *const racer = &racers->records[index];
		SlipChampionship_DrawRow(racer, racer->racerType == 2 ? computerFont : localFont, framebuffer, pitch, y);
		y = (uint16_t)(y + 13);
	}
}

void SlipChampionship_DrawFrame(const SlipRaceRacerTable *racers, const SlipSprite *background,
                                const SlipSprite *inactive, const SlipFont *computerFont, const SlipFont *localFont,
                                const char *const labels[2], const char *title, uint32_t hoveredButton,
                                uint8_t *framebuffer, int pitch) {
	Raster_SetClipRect(0, 0, 319, 199);
	SlipSprite_DrawClipped(background, framebuffer, pitch, 0, 0);

	for (uint32_t i = 0; i < 2; ++i)
		SlipRaceResults_DrawRacePanel(hoveredButton == i + 1 ? background : inactive, computerFont,
		                              &SlipRaceResults_buttons[i], labels[i], framebuffer, pitch);
	const SlipRaceResultsRect titleRect = {59, 10, 258, 26};
	SlipRaceResults_DrawRacePanel(inactive, computerFont, &titleRect, title, framebuffer, pitch);
	Raster_SetClipRect(0, 0, 319, 199);
	SlipChampionship_DrawRows(racers, computerFont, localFont, framebuffer, pitch);
}

void SlipRaceResults_DrawFinalPanel(const SlipSprite *sprite, const SlipFont *font, const SlipRaceResultsRect *rect,
                                    const char *label, uint8_t *framebuffer, int pitch) {
	Raster_SetClipRect(rect->left, rect->top, rect->right, rect->bottom);
	Raster_DrawLineSolid(0x25, rect->left, rect->top, rect->left, rect->bottom);
	Raster_DrawLineSolid(0x2b, rect->left, rect->top, rect->right, rect->top);
	Raster_DrawLineSolid(0x0a, rect->left, rect->bottom, rect->right, rect->bottom);
	Raster_DrawLineSolid(0x0a, rect->right, rect->top, rect->right, rect->bottom);
	Raster_SetClipRect(rect->left + 1, rect->top + 1, rect->right - 1, rect->bottom - 1);
	SlipSprite_DrawClipped(sprite, framebuffer, pitch, 0, 0);
	Raster_SetClipRect(rect->left, rect->top, rect->right, rect->bottom);
	if (label != NULL)
		SlipFont_DrawCenteredLine(font, framebuffer, pitch, label, -1, rect->left, rect->right, rect->top + 5);
}

void SlipChampionshipFinal_DrawRow(const SlipRaceRacerState *racer, const SlipFont *font, uint8_t *framebuffer,
                                   int pitch, uint16_t y) {
	char text[8];
	SlipFont_DrawTextClipped(font, framebuffer, pitch, 40, y, SlipRaceResults_driverNames[racer->tuningIndex], -1, 20,
	                         0, 300, 199);
	snprintf(text, sizeof(text), "%d.", (int16_t)racer->championshipPosition);
	SlipFont_DrawTextClipped(font, framebuffer, pitch, 20, y, text, -1, 20, 0, 300, 199);
	snprintf(text, sizeof(text), "%d", (int16_t)racer->championshipPoints);
	SlipFont_DrawTextClipped(font, framebuffer, pitch, 250, y, text, -1, 20, 0, 300, 199);
}

void SlipChampionshipFinal_DrawRows(const SlipRaceRacerTable *racers, const SlipFont *computerFont,
                                    const SlipFont *localFont, uint8_t *framebuffer, int pitch) {
	uint16_t y = 40;
	for (uint16_t rank = 1; rank <= racers->racerCount; ++rank) {
		uint16_t index = 0;
		for (; index < racers->racerCount; ++index)
			if (racers->records[index].championshipPosition == rank)
				break;
		const SlipRaceRacerState *const racer = &racers->records[index];
		SlipChampionshipFinal_DrawRow(racer, racer->racerType == 2 ? computerFont : localFont, framebuffer, pitch, y);
		y = (uint16_t)(y + 13);
	}
}

void SlipChampionshipFinal_DrawFrame(const SlipRaceRacerTable *racers, const SlipSprite *background,
                                     const SlipSprite *inactive, const SlipFont *computerFont,
                                     const SlipFont *localFont, const char *title, const char *button,
                                     uint32_t hoveredButton, uint8_t *framebuffer, int pitch) {
	Raster_SetClipRect(0, 0, 319, 199);
	SlipSprite_DrawClipped(background, framebuffer, pitch, 0, 0);
	const SlipRaceResultsRect titleRect = {92, 11, 226, 27};
	SlipRaceResults_DrawFinalPanel(inactive, computerFont, &titleRect, title, framebuffer, pitch);
	const SlipRaceResultsRect buttonRect = {109, 175, 209, 191};
	SlipRaceResults_DrawFinalPanel(hoveredButton != 0 ? background : inactive, computerFont, &buttonRect, button,
	                               framebuffer, pitch);
	Raster_SetClipRect(0, 0, 319, 199);
	SlipChampionshipFinal_DrawRows(racers, computerFont, localFont, framebuffer, pitch);
}

bool SlipChampionshipFinal_ReadInput(uint32_t hoveredButton, bool pressed[256]) {
	if (hoveredButton == 0)
		return false;
	if (pressed[SLIP_INPUT_SCAN_ENTER]) {
		pressed[SLIP_INPUT_SCAN_ENTER] = false;
		return true;
	}
	if (pressed[SLIP_INPUT_MOUSE_LEFT]) {
		pressed[SLIP_INPUT_MOUSE_LEFT] = false;
		return true;
	}
	return false;
}
