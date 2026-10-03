#ifndef SLIPSTREAM5000_RACE_HUD_H
#define SLIPSTREAM5000_RACE_HUD_H

#include "font.h"
#include "race_time_format.h"
#include "resource.h"
#include "sprite.h"
#include "text_layout.h"
#include "track_view_render.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void SlipRaceHud_FormatTime(uint32_t milliseconds, char text[SLIP_RACE_TIME_TEXT_BYTES]);

typedef struct SlipObject SlipObject;

enum {
	SLIP_RACE_HUD_POSITION_SPRITE_COUNT = 10,
	SLIP_RACE_HUD_SIGHT_FRAME_COUNT = 2,
	SLIP_RACE_HUD_CONSOLE_VARIANT_COUNT = 2,
	SLIP_RACE_HUD_REDRAW_FRAME_COUNT = 2
};

/* Zero uses the full indicator layout without console sprites. */
typedef enum SlipRaceHudConsoleSelection {
	SLIP_RACE_HUD_CONSOLE_FULL = 0,
	SLIP_RACE_HUD_CONSOLE_COMPACT_LIGHT = 1,
	SLIP_RACE_HUD_CONSOLE_COMPACT_DARK = 2
} SlipRaceHudConsoleSelection;

typedef struct SlipRaceHudAssets {
	uint16_t timeFont, speedFont;
	uint16_t positionSprites[SLIP_RACE_HUD_POSITION_SPRITE_COUNT], targetSightSprites[SLIP_RACE_HUD_SIGHT_FRAME_COUNT];
	uint16_t normalSightSprite;
	uint16_t topSprites[SLIP_RACE_HUD_CONSOLE_VARIANT_COUNT], bottomSprites[SLIP_RACE_HUD_CONSOLE_VARIANT_COUNT];
	uint16_t consoleExtensionSprite, turboSprite;
	uint32_t flags;
	bool active;
	bool speedFontLoaded;
	bool consoleExtensionLoaded;
	bool turboLoaded;
	bool normalSightLoaded;
	bool targetSightsLoaded;
	bool positionSpritesLoaded;
	bool consoleSpritesLoaded;
} SlipRaceHudAssets;

enum { SLIP_RACE_HUD_INTRO_PRESENTATION = 1u, SLIP_RACE_HUD_ALTERNATING_SIGHT_FRAME_MASK = 1u };

extern const SlipFontResourceCalls SlipRaceHud_fontResources;
/* Host surface bindings for the original resource-backed blitters. */
void SlipRaceHud_DrawSpriteResourceClipped(uint16_t resource, uint8_t *framebuffer, int pitch, int x, int y);
void SlipRaceHud_DrawSpriteResource(uint16_t resource, uint8_t *framebuffer, int pitch, int x, int y);
void SlipRaceHud_DrawTextResource(uint16_t resource, uint8_t *framebuffer, int pitch, int x, int y, const char *text,
                                  uint16_t color);

void SlipRaceHud_DrawLayoutHost(uint8_t *framebuffer, int pitch, int x, int y, const char *text,
                                const SlipTextArgument *arguments);

bool SlipRaceHud_LoadTimeFont(SlipRaceHudAssets *assets, const char *const *archives, size_t archiveCount);

bool SlipRaceHud_LoadPositionSprites(SlipRaceHudAssets *assets, const char *const *archives, size_t archiveCount);

typedef struct SlipRaceHudState {
	int32_t playerOneConsoleSelection;
	uint32_t playerOneConsoleRedrawFrames;
	uint32_t playerOneLowerConsoleY;
	int32_t playerTwoConsoleSelection;
	uint32_t playerTwoConsoleRedrawFrames;
	uint32_t playerTwoLowerConsoleY;
	uint32_t borderFrames;
} SlipRaceHudState;

bool SlipRaceHud_LoadNormalSight(SlipRaceHudAssets *assets, const char *const *archives, size_t archiveCount);

bool SlipRaceHud_LoadConsoleSprites(SlipRaceHudAssets *assets, const char *const *archives, size_t archiveCount,
                                    int driver, int gameMode);

void SlipRaceHud_Shutdown(SlipRaceHudAssets *assets, TrackViewTrackLifecycleCallback cleanupCallback,
                          const TrackViewTrackLifecycleArgs *trackLifecycle);

void SlipRaceHud_DrawNormalSight(const SlipRaceHudAssets *assets, uint8_t *framebuffer, int pitch, int centerX,
                                 int centerY);

void SlipRaceHud_DrawTargetSight(const SlipRaceHudAssets *assets, uint8_t *framebuffer, int pitch, int screenX,
                                 int screenY, uint32_t drawPageToggle);

void SlipRaceHud_DrawFinishPosition(const SlipRaceHudAssets *assets, uint8_t *framebuffer, int pitch, int minY,
                                    uint16_t position);

void SlipRaceHud_DrawRaceStatus(const SlipRaceHudAssets *assets, uint8_t *framebuffer, int pitch, int minY, int maxX,
                                uint16_t racePosition, uint16_t currentLapNumber, uint32_t currentLapTime,
                                uint16_t lapTimeTimer, uint32_t lapTime);

void SlipRaceHud_DrawSpeed(const SlipRaceHudAssets *assets, uint8_t *framebuffer, int pitch, int minX, int minY,
                           uint32_t inputSpeed, int units, uint16_t countdown);

void SlipRaceHud_ResetConsole(SlipRaceHudState *state);

int SlipRaceHud_DrawUpperConsole(const SlipRaceHudAssets *assets, uint16_t light, uint8_t *framebuffer, int pitch,
                                 int minX, int maxY);

void SlipRaceHud_PublishHandlerResult(SlipRaceHudState *state, int handlerReturn, int maxY, uint16_t view);

void SlipRaceHud_DrawLowerConsole(SlipRaceHudState *state, const SlipRaceHudAssets *assets, uint8_t *framebuffer,
                                  int pitch, uint16_t view);

void SlipRaceHud_DrawBorders(SlipRaceHudState *state, uint32_t gameMode, uint32_t windowSize, uint32_t flags);

#endif
