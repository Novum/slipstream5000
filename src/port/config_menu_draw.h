#ifndef SLIPSTREAM5000_CONFIG_MENU_DRAW_H
#define SLIPSTREAM5000_CONFIG_MENU_DRAW_H
#include "config_menu.h"
#include "input_navigation.h"
#include "text_layout.h"

typedef SlipInputRectangle SlipConfigMenuRectangle;
extern const SlipConfigMenuRectangle SlipConfigMenu_mainRectangles[SLIP_CONFIG_MAIN_RECTANGLE_COUNT];
extern const SlipConfigMenuRectangle SlipConfigMenu_difficultyRectangles[SLIP_CONFIG_DIFFICULTY_RECTANGLE_COUNT];
extern const uint32_t SlipConfigMenu_mainTags[SLIP_CONFIG_MAIN_RECTANGLE_COUNT];
extern const uint32_t SlipConfigMenu_difficultyTags[SLIP_CONFIG_DIFFICULTY_RECTANGLE_COUNT];
extern const uint32_t *SlipConfigMenu_mainTagCursor;
extern const uint32_t *SlipConfigMenu_difficultyTagCursor;
extern const SlipTextArgument *SlipMenu_panelArguments;

extern const SlipConfigMenuRectangle SlipConfigMenu_generalRectangles[SLIP_CONFIG_GENERAL_RECTANGLE_COUNT];
extern const uint32_t SlipConfigMenu_generalTags[SLIP_CONFIG_GENERAL_RECTANGLE_COUNT];
extern const uint32_t *SlipConfigMenu_generalTagCursor;
extern const SlipConfigMenuRectangle SlipConfigMenu_detailRectangles[SLIP_CONFIG_DETAIL_RECTANGLE_COUNT];
extern const uint32_t SlipConfigMenu_detailTags[SLIP_CONFIG_DETAIL_RECTANGLE_COUNT];
extern const uint32_t *SlipConfigMenu_detailTagCursor;
extern const SlipConfigMenuRectangle SlipConfigMenu_soundRectangles[SLIP_CONFIG_SOUND_RECTANGLE_COUNT];
extern const uint32_t SlipConfigMenu_soundTags[SLIP_CONFIG_SOUND_RECTANGLE_COUNT];
extern const uint32_t *SlipConfigMenu_soundTagCursor;
extern const SlipConfigMenuRectangle SlipConfigMenu_controlsRectangles[SLIP_CONFIG_CONTROLS_RECTANGLE_COUNT];
extern const uint32_t SlipConfigMenu_controlsTags[SLIP_CONFIG_CONTROLS_RECTANGLE_COUNT];
extern const uint32_t *SlipConfigMenu_controlsTagCursor;

typedef struct SlipConfigMenuDrawCalls {
	void *context;
	SlipConfigMenuRectangle (*surface)(void *);
	void (*clip)(void *, SlipConfigMenuRectangle);
	void (*line)(void *, uint16_t color, int16_t x1, int16_t y1, int16_t x2, int16_t y2);
	void (*clippedSprite)(void *, uint16_t, int16_t x, int16_t y);
	void (*unclippedSprite)(void *, uint16_t, int16_t x, int16_t y);
	void (*font)(void *, uint16_t);
	void (*textColor)(void *, uint16_t);
	void (*style)(void *, uint16_t mode, uint16_t spacing, int16_t left, int16_t right);
	const char *(*string)(void *, SlipStringTableSlot *, uint32_t tag);
	void (*unlockStrings)(void *, SlipStringTableSlot *);
	void (*text)(void *, const char *, const SlipTextArgument *, SlipTextPosition);
	uint32_t (*difficulty)(void *);
	uint32_t (*damage)(void *);
	uint32_t (*rearMonitor)(void *);
	uint32_t (*weaponsMonitor)(void *);
	const char *(*language)(void *);
	uint32_t (*trackMap)(void *);
	uint32_t (*speedDisplay)(void *);
	const char *(*environment)(void *);
	uint32_t (*clouds)(void *);
	uint32_t (*shading)(void *);
	uint32_t (*textures)(void *);
	uint32_t (*window)(void *);
	uint16_t (*shadows)(void *);
	uint32_t (*effects)(void *);
	uint32_t (*engines)(void *);
	uint32_t (*speech)(void *);
	uint32_t (*music)(void *);
	uint32_t (*reverseAccelerator)(void *);
} SlipConfigMenuDrawCalls;

void SlipMenu_DrawPanel(SlipConfigMenuRectangle rectangle, uint16_t sprite, SlipStringTableSlot *strings, uint32_t tag,
                        const SlipConfigMenuDrawCalls *calls);
void SlipConfigMenu_DrawMain(const SlipConfigMenuState *, const SlipConfigMenuDrawCalls *);
void SlipConfigMenu_DrawDifficulty(const SlipConfigMenuState *, const SlipConfigMenuDrawCalls *);
void SlipConfigMenu_DrawGeneral(const SlipConfigMenuState *, const SlipConfigMenuDrawCalls *);
void SlipConfigMenu_DrawDetail(const SlipConfigMenuState *, const SlipConfigMenuDrawCalls *);
void SlipConfigMenu_DrawSound(const SlipConfigMenuState *, const SlipConfigMenuDrawCalls *);
void SlipConfigMenu_DrawControls(const SlipConfigMenuState *, const SlipConfigMenuDrawCalls *);
#endif
