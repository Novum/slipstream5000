#ifndef SLIPSTREAM5000_RACE_RESULTS_H
#define SLIPSTREAM5000_RACE_RESULTS_H

#include "font.h"
#include "race.h"
#include "race_championship.h"
#include "sprite.h"
#include "string_table.h"

enum {
	SLIP_RACE_RESULTS_DRIVER_NAME_COUNT = SLIP_RACE_RACER_COUNT + 1,
	SLIP_RACE_RESULTS_BUTTON_COUNT = 2,
	SLIP_RACE_RESULTS_PANEL_TEXT_INSET_Y = 4,
	SLIP_CHAMPIONSHIP_FINAL_PANEL_TEXT_INSET_Y = 5,
	SLIP_RACE_RESULTS_TABLE_LEFT = 20,
	SLIP_RACE_RESULTS_TABLE_RIGHT = 300,
	SLIP_RACE_RESULTS_TABLE_TOP = 40,
	SLIP_RACE_RESULTS_ROW_SPACING = 13,
	SLIP_RACE_RESULTS_DRIVER_NAME_X = 40,
	SLIP_RACE_RESULTS_TIME_X = 240,
	SLIP_RACE_RESULTS_POINTS_X = 250
};

extern const char *const SlipRaceResults_driverNames[SLIP_RACE_RESULTS_DRIVER_NAME_COUNT];

typedef struct SlipRaceResultsAssets {
	uint16_t backgroundResource, inactiveResource;
	uint16_t computerFontResource, localFontResource;
	SlipStringTableSlot *stringSlot;
} SlipRaceResultsAssets;

void SlipRaceResults_LoadAssets(SlipRaceResultsAssets *assets, const char *const *archives, size_t archiveCount,
                                uint16_t language);
void SlipRaceResults_ReleaseSprites(SlipRaceResultsAssets *assets);
extern const SlipStringTableResources SlipRaceResults_stringResources;
void SlipRaceResults_DrawResources(const SlipRaceRacerTable *racers, const SlipRaceResultsAssets *assets,
                                   uint16_t track, uint32_t hoveredButton, uint8_t *framebuffer, int pitch);

typedef struct SlipRaceResultsRect {
	int16_t left, top, right, bottom;
} SlipRaceResultsRect;

extern const SlipRaceResultsRect SlipRaceResults_buttons[SLIP_RACE_RESULTS_BUTTON_COUNT];

typedef enum SlipRaceResultsAction {
	SLIP_RESULTS_WAIT,
	SLIP_RESULTS_CONTINUE,
	SLIP_RESULTS_REPLAY
} SlipRaceResultsAction;

uint32_t SlipRaceResults_HitTest(int16_t x, int16_t y);
SlipRaceResultsAction SlipRaceResults_ReadInput(uint32_t hoveredButton, bool pressed[SLIP_INPUT_CODE_COUNT]);
void SlipRaceResults_DrawRacePanel(const SlipSprite *sprite, const SlipFont *font, const SlipRaceResultsRect *rect,
                                   const char *label, uint8_t *framebuffer, int pitch);
void SlipRaceResults_DrawFrame(const SlipRaceRacerTable *racers, const SlipSprite *background,
                               const SlipSprite *inactive, const SlipFont *computerFont, const SlipFont *localFont,
                               const char *const buttonLabels[SLIP_RACE_RESULTS_BUTTON_COUNT], const char *title,
                               uint32_t hoveredButton, uint8_t *framebuffer, int pitch);

void SlipRaceResults_DrawRow(const SlipRaceRacerState *racer, const SlipFont *font, uint8_t *framebuffer, int pitch,
                             uint16_t y);
void SlipRaceResults_DrawRows(const SlipRaceRacerTable *racers, const SlipFont *computerFont, const SlipFont *localFont,
                              uint8_t *framebuffer, int pitch);

void SlipChampionship_DrawRow(const SlipRaceRacerState *racer, const SlipFont *font, uint8_t *framebuffer, int pitch,
                              uint16_t y);
void SlipChampionship_DrawRows(const SlipRaceRacerTable *racers, const SlipFont *computerFont,
                               const SlipFont *localFont, uint8_t *framebuffer, int pitch);
void SlipChampionship_DrawFrame(const SlipRaceRacerTable *racers, const SlipSprite *background,
                                const SlipSprite *inactive, const SlipFont *computerFont, const SlipFont *localFont,
                                const char *const labels[SLIP_RACE_RESULTS_BUTTON_COUNT], const char *title,
                                uint32_t hoveredButton, uint8_t *framebuffer, int pitch);

void SlipRaceResults_DrawFinalPanel(const SlipSprite *sprite, const SlipFont *font, const SlipRaceResultsRect *rect,
                                    const char *label, uint8_t *framebuffer, int pitch);
void SlipChampionshipFinal_DrawRow(const SlipRaceRacerState *racer, const SlipFont *font, uint8_t *framebuffer,
                                   int pitch, uint16_t y);
void SlipChampionshipFinal_DrawRows(const SlipRaceRacerTable *racers, const SlipFont *computerFont,
                                    const SlipFont *localFont, uint8_t *framebuffer, int pitch);
void SlipChampionshipFinal_DrawFrame(const SlipRaceRacerTable *racers, const SlipSprite *background,
                                     const SlipSprite *inactive, const SlipFont *computerFont,
                                     const SlipFont *localFont, const char *title, const char *button,
                                     uint32_t hoveredButton, uint8_t *framebuffer, int pitch);
bool SlipChampionshipFinal_ReadInput(uint32_t hoveredButton, bool pressed[SLIP_INPUT_CODE_COUNT]);

#endif
