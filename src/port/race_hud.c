#include "race_hud.h"
#include "raster/raster.h"

#include "byte_order.h"
#include "fixed_point.h"
#include "menu_resources.h"
#include "resource_host.h"
#include "sprite_format.h"
#include "sprite_resource_host.h"
#include "text_layout.h"
#include "track_world.h"

#include <assert.h>
#include <string.h>

enum {
	SLIP_RACE_HUD_TEXT_COLOUR = 254,
	SLIP_RACE_HUD_SPLIT_SCREEN_MODE = 1,
	SLIP_RACE_HUD_BORDER_WIDTH = 4,
	SLIP_RACE_HUD_RIGHT_BORDER_LEFT = SLIPSTREAM_SCREEN_WIDTH - SLIP_RACE_HUD_BORDER_WIDTH,
	SLIP_RACE_HUD_SPLIT_DIVIDER_Y = SLIPSTREAM_SCREEN_HEIGHT / 2,
	SLIP_RACE_HUD_SHORT_TOP_BORDER_HEIGHT = 8,
	SLIP_RACE_HUD_TALL_TOP_BORDER_HEIGHT = 32,
	SLIP_RACE_HUD_BOTTOM_BORDER_TOP = 193,
	SLIP_RACE_HUD_INTRO_CONSOLE_FILL_BOTTOM = 197,
	SLIP_RACE_HUD_CONSOLE_NAME_DRIVER_OFFSET = 3,
	SLIP_RACE_HUD_CONSOLE_NAME_VARIANT_OFFSET = 6,
	SLIP_RACE_HUD_EXTENSION_NAME_VARIANT_OFFSET = 7,
	SLIP_RACE_HUD_SPEED_COLOUR = 252,
	SLIP_RACE_HUD_SPEED_DIVISOR_KMH = 444,
	SLIP_RACE_HUD_SPEED_DIVISOR_MPH = 715,
	SLIP_RACE_HUD_NORMAL_SIGHT_OFFSET_X = 14,
	SLIP_RACE_HUD_NORMAL_SIGHT_OFFSET_Y = 11,
	SLIP_RACE_HUD_TARGET_SIGHT_RADIUS = 6,
	SLIP_RACE_HUD_POSITION_RIGHT_MARGIN = 26,
	SLIP_RACE_HUD_POSITION_TOP_MARGIN = 4,
	SLIP_RACE_HUD_TIME_RIGHT_MARGIN = 60,
	SLIP_RACE_HUD_TIME_TOP_MARGIN = 6,
	SLIP_RACE_HUD_PREVIOUS_TIME_TOP_MARGIN = 24,
	SLIP_RACE_HUD_LAP_LABEL_LEFT = 253,
	SLIP_RACE_HUD_LAP_LABEL_RIGHT = 291,
	SLIP_RACE_HUD_LAP_LABEL_TOP_MARGIN = 15,
	SLIP_RACE_HUD_SPEED_LEFT_MARGIN = 10,
	SLIP_RACE_HUD_SPEED_TOP_MARGIN = 6,
	SLIP_RACE_HUD_MESSAGE_TOP_MARGIN = 5,
	SLIP_RACE_HUD_CONSOLE_LIGHT_THRESHOLD_Q14 = SLIP_Q14_HALF
};

static SlipFont SlipRaceHud_LockFontHost(void *context, uint16_t resource) {
	const uint8_t *const data = SlipResourceHost_Lock(context, resource);
	SlipResourcePayload payload = SlipResourceHost_Payload(resource);
	payload.data = (uint8_t *)data;
	SlipFont font = {0};
	(void)SlipFont_FromPayload(&payload, &font);
	return font;
}

const SlipFontResourceCalls SlipRaceHud_fontResources = {.lock = SlipRaceHud_LockFontHost,
                                                         .unlock = SlipResourceHost_Unlock};

static SlipSprite SlipRaceHud_LockSpriteHost(uint16_t resource) {
	const uint8_t *const data = SlipResourceHost_Lock(NULL, resource);
	SlipResourcePayload payload = SlipResourceHost_Payload(resource);
	payload.data = (uint8_t *)data;
	SlipSprite sprite = {0};
	(void)SlipSprite_FromPayload(&payload, &sprite);
	return sprite;
}

void SlipRaceHud_DrawSpriteResourceClipped(uint16_t resource, uint8_t *framebuffer, int pitch, int x, int y) {
	SlipSprite sprite = SlipRaceHud_LockSpriteHost(resource);
	SlipSprite_DrawClipped(&sprite, framebuffer, pitch, x, y);
	SlipResourceHost_Unlock(NULL, resource);
}

void SlipRaceHud_DrawSpriteResource(uint16_t resource, uint8_t *framebuffer, int pitch, int x, int y) {
	SlipSprite sprite = SlipRaceHud_LockSpriteHost(resource);
	SlipSprite_Draw(&sprite, framebuffer, pitch, x, y);
	SlipResourceHost_Unlock(NULL, resource);
}

static RasterSurfaceBinding SlipRaceHud_BindSurfaceHost(uint8_t *framebuffer, int pitch) {
	RasterSurfaceBinding saved = {g_screenBufferBase, g_screenPitch, g_clipMinX, g_clipMinY, g_clipMaxX, g_clipMaxY};
	Raster_SetScreenBufferRows(framebuffer, pitch);
	Raster_SetClipRect(0, 0, SLIPSTREAM_SCREEN_WIDTH - 1, SLIPSTREAM_SCREEN_HEIGHT - 1);
	return saved;
}

void SlipRaceHud_DrawTextResource(uint16_t resource, uint8_t *framebuffer, int pitch, int x, int y, const char *text,
                                  uint16_t color) {
	RasterSurfaceBinding saved = SlipRaceHud_BindSurfaceHost(framebuffer, pitch);
	int16_t position = (int16_t)x;
	SlipFont_DrawResourceStringColor(resource, text, &position, (int16_t)y, color, &SlipRaceHud_fontResources);
	Raster_RestoreScreen(&saved);
}

void SlipRaceHud_DrawLayoutHost(uint8_t *framebuffer, int pitch, int x, int y, const char *text,
                                const SlipTextArgument *arguments) {
	RasterSurfaceBinding saved = SlipRaceHud_BindSurfaceHost(framebuffer, pitch);
	SlipTextPosition position = {(int16_t)x, (int16_t)y};
	SlipText_Draw(&SlipText_state, text, arguments, &position);
	Raster_RestoreScreen(&saved);
}

void SlipRaceHud_Shutdown(SlipRaceHudAssets *assets, TrackViewTrackLifecycleCallback cleanupCallback,
                          const TrackViewTrackLifecycleArgs *trackLifecycle) {
	if (assets == NULL || !assets->active)
		return;
	assets->active = false;
	if ((assets->flags & SLIP_RACE_HUD_INTRO_PRESENTATION) == 0) {
		SlipResourceHost_ReleaseSequence(NULL, assets->positionSprites, SLIP_RACE_HUD_POSITION_SPRITE_COUNT);
		assets->positionSpritesLoaded = false;
	}
	SlipResourceHost_Release(NULL, assets->timeFont);
	SlipResourceHost_Release(NULL, assets->speedFont);
	SlipResourceHost_ReleaseSequence(NULL, assets->targetSightSprites, SLIP_RACE_HUD_SIGHT_FRAME_COUNT);
	SlipResourceHost_Release(NULL, assets->normalSightSprite);
	SlipResourceHost_ReleaseSequence(NULL, assets->topSprites, SLIP_RACE_HUD_CONSOLE_VARIANT_COUNT);
	SlipResourceHost_ReleaseSequence(NULL, assets->bottomSprites, SLIP_RACE_HUD_CONSOLE_VARIANT_COUNT);
	SlipResourceHost_Release(NULL, assets->consoleExtensionSprite);
	SlipResourceHost_Release(NULL, assets->turboSprite);
	assets->speedFontLoaded = assets->targetSightsLoaded = false;
	assets->normalSightLoaded = assets->consoleSpritesLoaded = false;
	assets->consoleExtensionLoaded = assets->turboLoaded = false;
	(void)cleanupCallback(trackLifecycle);
}

bool SlipRaceHud_LoadTimeFont(SlipRaceHudAssets *assets, const char *const *archives, size_t archiveCount) {
	(void)archives;
	(void)archiveCount;
	return SlipResourceHost_Load(NULL, "TIME.FNT", &assets->timeFont);
}

bool SlipRaceHud_LoadPositionSprites(SlipRaceHudAssets *assets, const char *const *archives, size_t archiveCount) {
	(void)archives;
	(void)archiveCount;
	if (!SlipResourceHost_LoadSequence(NULL, "POS*.SPR", 0, SLIP_RACE_HUD_POSITION_SPRITE_COUNT,
	                                   assets->positionSprites))
		return false;
	assets->positionSpritesLoaded = true;
	return true;
}

bool SlipRaceHud_LoadNormalSight(SlipRaceHudAssets *assets, const char *const *archives, size_t archiveCount) {
	(void)archives;
	(void)archiveCount;
	if (!SlipResourceHost_LoadSequence(NULL, "TSIGHT*.SPR", 0, SLIP_RACE_HUD_SIGHT_FRAME_COUNT,
	                                   assets->targetSightSprites))
		return false;
	assets->targetSightsLoaded = true;
	if (!SlipResourceHost_Load(NULL, "NSIGHT.SPR", &assets->normalSightSprite))
		return false;
	assets->normalSightLoaded = true;
	return true;
}

bool SlipRaceHud_LoadConsoleSprites(SlipRaceHudAssets *assets, const char *const *archives, size_t archiveCount,
                                    int driver, int gameMode) {
	(void)archives;
	(void)archiveCount;
	char topName[] = "CON?_T?*.SPR";
	char bottomName[] = "CON?_B?*.SPR";
	char consoleExtensionName[] = "CONS_EXT.SPR";
	char turboName[] = "TURBO_?.SPR";
	if (!SlipResourceHost_Load(NULL, "SPD.FNT", &assets->speedFont))
		return false;
	assets->speedFontLoaded = true;
	topName[SLIP_RACE_HUD_CONSOLE_NAME_DRIVER_OFFSET] = bottomName[SLIP_RACE_HUD_CONSOLE_NAME_DRIVER_OFFSET] =
	    (char)(driver + '0');
	char consoleVariant = 'N', extensionVariant = 'T';
	if (gameMode == SLIP_RACE_HUD_SPLIT_SCREEN_MODE) {
		topName[SLIP_RACE_HUD_CONSOLE_NAME_DRIVER_OFFSET] = bottomName[SLIP_RACE_HUD_CONSOLE_NAME_DRIVER_OFFSET] = 'S';
		consoleVariant = 'H';
		extensionVariant = '1';
	}
	topName[SLIP_RACE_HUD_CONSOLE_NAME_VARIANT_OFFSET] = bottomName[SLIP_RACE_HUD_CONSOLE_NAME_VARIANT_OFFSET] =
	    turboName[SLIP_RACE_HUD_CONSOLE_NAME_VARIANT_OFFSET] = consoleVariant;
	consoleExtensionName[SLIP_RACE_HUD_EXTENSION_NAME_VARIANT_OFFSET] = extensionVariant;
	if (!SlipResourceHost_LoadSequence(NULL, topName, 1, SLIP_RACE_HUD_CONSOLE_VARIANT_COUNT, assets->topSprites))
		return false;
	if (!SlipResourceHost_LoadSequence(NULL, bottomName, 1, SLIP_RACE_HUD_CONSOLE_VARIANT_COUNT, assets->bottomSprites))
		return false;
	assets->consoleSpritesLoaded = true;
	if (!SlipResourceHost_Load(NULL, consoleExtensionName, &assets->consoleExtensionSprite))
		return false;
	assets->consoleExtensionLoaded = true;
	if (!SlipResourceHost_Load(NULL, turboName, &assets->turboSprite))
		return false;
	assets->turboLoaded = true;
	return true;
}

void SlipRaceHud_DrawNormalSight(const SlipRaceHudAssets *assets, uint8_t *framebuffer, int pitch, int centerX,
                                 int centerY) {
	if (assets == NULL || !assets->normalSightLoaded || framebuffer == NULL) {
		return;
	}
	SlipRaceHud_DrawSpriteResourceClipped(assets->normalSightSprite, framebuffer, pitch,
	                                      centerX - SLIP_RACE_HUD_NORMAL_SIGHT_OFFSET_X,
	                                      centerY - SLIP_RACE_HUD_NORMAL_SIGHT_OFFSET_Y);
}

void SlipRaceHud_DrawTargetSight(const SlipRaceHudAssets *assets, uint8_t *framebuffer, int pitch, int screenX,
                                 int screenY, uint32_t drawPageToggle) {
	if (assets == NULL || !assets->targetSightsLoaded || framebuffer == NULL) {
		return;
	}
	SlipRaceHud_DrawSpriteResourceClipped(
	    assets->targetSightSprites[drawPageToggle & SLIP_RACE_HUD_ALTERNATING_SIGHT_FRAME_MASK], framebuffer, pitch,
	    screenX - SLIP_RACE_HUD_TARGET_SIGHT_RADIUS, screenY - SLIP_RACE_HUD_TARGET_SIGHT_RADIUS);
}

void SlipRaceHud_FormatTime(uint32_t milliseconds, char text[SLIP_RACE_TIME_TEXT_BYTES]) {
	const uint32_t hours = milliseconds / SLIP_RACE_TIME_MILLISECONDS_PER_HOUR;
	uint32_t remainder = milliseconds - hours * SLIP_RACE_TIME_MILLISECONDS_PER_HOUR;
	const uint32_t minutes = remainder / SLIP_RACE_TIME_MILLISECONDS_PER_MINUTE;
	uint32_t seconds;
	uint32_t hundredths;
	uint32_t values[SLIP_RACE_TIME_FIELD_COUNT];
	size_t i;

	remainder -= minutes * SLIP_RACE_TIME_MILLISECONDS_PER_MINUTE;
	seconds = remainder / SLIP_RACE_TIME_MILLISECONDS_PER_SECOND;
	remainder -= seconds * SLIP_RACE_TIME_MILLISECONDS_PER_SECOND;
	hundredths = remainder / SLIP_RACE_TIME_MILLISECONDS_PER_HUNDREDTH;
	values[0] = hours;
	values[1] = minutes;
	values[2] = seconds;
	values[3] = hundredths;
	for (i = 0; i < SLIP_RACE_TIME_FIELD_COUNT; ++i) {
		text[i * SLIP_RACE_TIME_FIELD_STRIDE] = (char)(values[i] / 10u + '0');
		text[i * SLIP_RACE_TIME_FIELD_STRIDE + 1u] = (char)(values[i] % 10u + '0');
		if (i != SLIP_RACE_TIME_FIELD_COUNT - 1u) {
			text[i * SLIP_RACE_TIME_FIELD_STRIDE + SLIP_RACE_TIME_FIELD_DIGITS] = ':';
		}
	}
	text[SLIP_RACE_TIME_TEXT_BYTES - 1] = '\0';
}

void SlipRaceHud_DrawFinishPosition(const SlipRaceHudAssets *assets, uint8_t *framebuffer, int pitch, int minY,
                                    uint16_t position) {
	int16_t rank = (int16_t)position;
	const SlipTextArgument arguments[] = {{.word = &rank}};
	SlipText_SelectResourceFont(&SlipText_state, assets->timeFont, &SlipRaceHud_fontResources);
	SlipText_SetStyle(&SlipText_state, SLIP_TEXT_CENTERED, UINT16_MAX, 0, SLIPSTREAM_SCREEN_WIDTH - 1);
	SlipText_SetColor(&SlipText_state, SLIP_RACE_HUD_TEXT_COLOUR);
	SlipRaceHud_DrawLayoutHost(framebuffer, pitch, 0, minY + SLIP_RACE_HUD_MESSAGE_TOP_MARGIN, "Finished Position %d",
	                           arguments);
}

void SlipRaceHud_DrawRaceStatus(const SlipRaceHudAssets *assets, uint8_t *framebuffer, int pitch, int minY, int maxX,
                                uint16_t racePosition, uint16_t currentLapNumber, uint32_t currentLapTime,
                                uint16_t lapTimeTimer, uint32_t lapTime) {
	char currentTime[SLIP_RACE_TIME_TEXT_BYTES];
	uint32_t lapNumber = currentLapNumber;
	const SlipTextArgument lapArguments[] = {{.dword = &lapNumber}};

	if (assets == NULL || framebuffer == NULL) {
		return;
	}
	if (racePosition >= 1u && racePosition <= SLIP_RACE_HUD_POSITION_SPRITE_COUNT && assets->positionSpritesLoaded) {
		SlipRaceHud_DrawSpriteResourceClipped(assets->positionSprites[racePosition - 1u], framebuffer, pitch,
		                                      maxX - SLIP_RACE_HUD_POSITION_RIGHT_MARGIN,
		                                      minY + SLIP_RACE_HUD_POSITION_TOP_MARGIN);
	}
	SlipRaceHud_FormatTime(currentLapTime, currentTime);
	currentTime[SLIP_RACE_TIME_MINUTES_SEPARATOR] = ':';
	currentTime[SLIP_RACE_TIME_SECONDS_SEPARATOR] = ';';
	SlipRaceHud_DrawTextResource(assets->timeFont, framebuffer, pitch, maxX - SLIP_RACE_HUD_TIME_RIGHT_MARGIN,
	                             minY + SLIP_RACE_HUD_TIME_TOP_MARGIN, currentTime + SLIP_RACE_TIME_MINUTES_OFFSET + 1,
	                             SLIP_RACE_HUD_TEXT_COLOUR);
	if (currentLapNumber != 0) {
		SlipText_SelectResourceFont(&SlipText_state, assets->timeFont, &SlipRaceHud_fontResources);
		SlipText_SetColor(&SlipText_state, SLIP_RACE_HUD_TEXT_COLOUR);
		SlipText_SetStyle(&SlipText_state, SLIP_TEXT_CENTERED, UINT16_MAX, SLIP_RACE_HUD_LAP_LABEL_LEFT,
		                  SLIP_RACE_HUD_LAP_LABEL_RIGHT);
		SlipRaceHud_DrawLayoutHost(framebuffer, pitch, 0, minY + SLIP_RACE_HUD_LAP_LABEL_TOP_MARGIN, "=%2ld",
		                           lapArguments);
	}
	if (lapTimeTimer != 0) {
		SlipRaceHud_FormatTime(lapTime, currentTime);
		currentTime[SLIP_RACE_TIME_MINUTES_SEPARATOR] = ':';
		currentTime[SLIP_RACE_TIME_SECONDS_SEPARATOR] = ';';
		SlipRaceHud_DrawTextResource(assets->timeFont, framebuffer, pitch, maxX - SLIP_RACE_HUD_TIME_RIGHT_MARGIN,
		                             minY + SLIP_RACE_HUD_PREVIOUS_TIME_TOP_MARGIN,
		                             currentTime + SLIP_RACE_TIME_MINUTES_OFFSET + 1, SLIP_RACE_HUD_TEXT_COLOUR);
	}
}

void SlipRaceHud_DrawSpeed(const SlipRaceHudAssets *assets, uint8_t *framebuffer, int pitch, int minX, int minY,
                           uint32_t inputSpeed, int units, uint16_t countdown) {

	const uint32_t quotient =
	    inputSpeed / (units != 0 ? SLIP_RACE_HUD_SPEED_DIVISOR_KMH : SLIP_RACE_HUD_SPEED_DIVISOR_MPH);
	char text[] = "%d:";
	int16_t speed = (int16_t)quotient;
	const SlipTextArgument speedArguments[] = {{.word = &speed}};
	assert(quotient <= UINT16_MAX);
	if (assets == NULL || framebuffer == NULL || !assets->speedFontLoaded)
		return;
	/* ':' and ';' are the original unit glyphs, not punctuation to replace with host text. */
	text[2] = units != 0 ? ';' : ':';
	SlipText_SetColor(&SlipText_state, SLIP_RACE_HUD_SPEED_COLOUR);
	SlipText_SelectResourceFont(&SlipText_state, assets->speedFont, &SlipRaceHud_fontResources);
	SlipText_SetStyle(&SlipText_state, SLIP_TEXT_AT_POSITION, UINT16_MAX, SLIP_RACE_HUD_SPEED_LEFT_MARGIN,
	                  SLIPSTREAM_SCREEN_WIDTH - 1);
	SlipRaceHud_DrawLayoutHost(framebuffer, pitch, minX + SLIP_RACE_HUD_SPEED_LEFT_MARGIN,
	                           minY + SLIP_RACE_HUD_SPEED_TOP_MARGIN, text, speedArguments);
	SlipText_SelectResourceFont(&SlipText_state, SlipMenu_resources.smallFont, &SlipRaceHud_fontResources);

	if (countdown != 0) {
		char prepare[] = "Prepare to Race 1";
		prepare[sizeof(prepare) - 2] = (char)((uint8_t)countdown + '0');
		SlipText_SelectResourceFont(&SlipText_state, assets->timeFont, &SlipRaceHud_fontResources);
		SlipText_SetColor(&SlipText_state, SLIP_RACE_HUD_TEXT_COLOUR);
		SlipText_SetStyle(&SlipText_state, SLIP_TEXT_CENTERED, UINT16_MAX, 0, SLIPSTREAM_SCREEN_WIDTH - 1);
		SlipRaceHud_DrawLayoutHost(framebuffer, pitch, 0, minY + SLIP_RACE_HUD_MESSAGE_TOP_MARGIN, prepare, NULL);
	}
}

void SlipRaceHud_ResetConsole(SlipRaceHudState *state) {
	if (state == NULL) {
		return;
	}
	state->playerOneConsoleSelection = -1;
	state->playerTwoConsoleSelection = -1;
	state->playerOneConsoleRedrawFrames = 0;
	state->playerTwoConsoleRedrawFrames = 0;
	state->borderFrames = SLIP_RACE_HUD_REDRAW_FRAME_COUNT;
}

void SlipRaceHud_DrawBorders(SlipRaceHudState *state, uint32_t gameMode, uint32_t windowSize, uint32_t flags) {
	if (state->borderFrames == 0)
		return;
	--state->borderFrames;
	if (gameMode == SLIP_RACE_HUD_SPLIT_SCREEN_MODE) {

		Raster_FillRectUnchecked(0, 0, 0, SLIP_RACE_HUD_BORDER_WIDTH - 1, SLIPSTREAM_SCREEN_HEIGHT - 1);
		Raster_FillRectUnchecked(0, SLIP_RACE_HUD_RIGHT_BORDER_LEFT, 0, SLIPSTREAM_SCREEN_WIDTH - 1,
		                         SLIPSTREAM_SCREEN_HEIGHT - 1);
		Raster_DrawLineSolid(0, SLIP_RACE_HUD_BORDER_WIDTH, SLIP_RACE_HUD_SPLIT_DIVIDER_Y,
		                     SLIP_RACE_HUD_RIGHT_BORDER_LEFT - 1, SLIP_RACE_HUD_SPLIT_DIVIDER_Y);
	} else {

		Raster_FillRectUnchecked(0, 0, 0, SLIPSTREAM_SCREEN_WIDTH - 1,
		                         windowSize != 0 ? SLIP_RACE_HUD_TALL_TOP_BORDER_HEIGHT - 1
		                                         : SLIP_RACE_HUD_SHORT_TOP_BORDER_HEIGHT - 1);
		if ((flags & SLIP_RACE_HUD_INTRO_PRESENTATION) == 0)
			Raster_FillRectUnchecked(0, 0, SLIP_RACE_HUD_BOTTOM_BORDER_TOP, SLIPSTREAM_SCREEN_WIDTH - 1,
			                         SLIPSTREAM_SCREEN_HEIGHT - 1);
		Raster_FillRectUnchecked(0, 0, SLIP_RACE_HUD_SHORT_TOP_BORDER_HEIGHT, SLIP_RACE_HUD_BORDER_WIDTH - 1,
		                         SLIP_RACE_HUD_BOTTOM_BORDER_TOP - 1);
		Raster_FillRectUnchecked(0, SLIP_RACE_HUD_RIGHT_BORDER_LEFT, SLIP_RACE_HUD_SHORT_TOP_BORDER_HEIGHT,
		                         SLIPSTREAM_SCREEN_WIDTH - 1, SLIP_RACE_HUD_BOTTOM_BORDER_TOP - 1);
	}
}

int SlipRaceHud_DrawUpperConsole(const SlipRaceHudAssets *assets, uint16_t light, uint8_t *framebuffer, int pitch,
                                 int minX, int maxY) {
	const int variant = light >= SLIP_RACE_HUD_CONSOLE_LIGHT_THRESHOLD_Q14 ? 0 : 1;
	uint16_t top;

	if (assets == NULL || !assets->consoleSpritesLoaded || framebuffer == NULL) {
		return SLIP_RACE_HUD_CONSOLE_FULL;
	}
	top = assets->topSprites[variant];
	const uint8_t *const data = SlipResourceHost_Lock(NULL, top);
	const uint16_t height = SlipBytes_ReadLE16(data + SLIP_SPRITE_HEIGHT_OFFSET);
	SlipResourceHost_Unlock(NULL, top);
	SlipRaceHud_DrawSpriteResourceClipped(top, framebuffer, pitch, minX, maxY - (int)height + 1);
	return variant + SLIP_RACE_HUD_CONSOLE_COMPACT_LIGHT;
}

void SlipRaceHud_PublishHandlerResult(SlipRaceHudState *state, int handlerReturn, int maxY, uint16_t view) {
	if (state == NULL) {
		return;
	}

	int32_t *const current = view == 1 ? &state->playerOneConsoleSelection : &state->playerTwoConsoleSelection;
	uint32_t *const transition =
	    view == 1 ? &state->playerOneConsoleRedrawFrames : &state->playerTwoConsoleRedrawFrames;
	uint32_t *const y = view == 1 ? &state->playerOneLowerConsoleY : &state->playerTwoLowerConsoleY;
	*y = (uint32_t)maxY + 1u;
	if (handlerReturn != *current) {
		*current = handlerReturn;
		*transition = SLIP_RACE_HUD_REDRAW_FRAME_COUNT;
	}
}

void SlipRaceHud_DrawLowerConsole(SlipRaceHudState *state, const SlipRaceHudAssets *assets, uint8_t *framebuffer,
                                  int pitch, uint16_t view) {
	int bottomIndex;

	if (state == NULL || assets == NULL || !assets->consoleSpritesLoaded || framebuffer == NULL || pitch <= 0) {
		return;
	}

	uint32_t *const transition =
	    view == 1 ? &state->playerOneConsoleRedrawFrames : &state->playerTwoConsoleRedrawFrames;
	const uint32_t y = view == 1 ? state->playerOneLowerConsoleY : state->playerTwoLowerConsoleY;
	const int32_t current = view == 1 ? state->playerOneConsoleSelection : state->playerTwoConsoleSelection;

	Raster_SetClipRect(0, 0, SLIPSTREAM_SCREEN_WIDTH - 1, SLIPSTREAM_SCREEN_HEIGHT - 1);
	if ((*transition) == 0)
		return;
	--(*transition);
	bottomIndex = current - SLIP_RACE_HUD_CONSOLE_COMPACT_LIGHT;
	if (bottomIndex >= 0 && bottomIndex < SLIP_RACE_HUD_CONSOLE_VARIANT_COUNT) {
		const uint16_t bottom = assets->bottomSprites[bottomIndex];

		SlipRaceHud_DrawSpriteResource(bottom, framebuffer, pitch, SLIP_RACE_HUD_BORDER_WIDTH, (int)y);
		if (view == 1 && (assets->flags & SLIP_RACE_HUD_INTRO_PRESENTATION) != 0) {

			Raster_FillRectClipped(
			    0, 0, (int16_t)(y + SlipSprite_Dimensions(bottom, &SlipSpriteHost_effectResources).height - 1u),
			    SLIPSTREAM_SCREEN_WIDTH - 1, SLIPSTREAM_SCREEN_HEIGHT - 1);
		}
	} else if (bottomIndex < 0) {
		if (view != 1 || (assets->flags & SLIP_RACE_HUD_INTRO_PRESENTATION) == 0) {

			Raster_DrawLineClipped(0, 0, (int16_t)y, SLIPSTREAM_SCREEN_WIDTH - 1, (int16_t)y);
			SlipRaceHud_DrawSpriteResourceClipped(assets->consoleExtensionSprite, framebuffer, pitch,
			                                      SLIP_RACE_HUD_BORDER_WIDTH, (int)y + 1);
		} else {

			const uint8_t *const data = SlipResourceHost_Lock(NULL, assets->bottomSprites[0]);
			const uint16_t width = SlipBytes_ReadLE16(data);
			SlipResourceHost_Unlock(NULL, assets->bottomSprites[0]);
			Raster_FillRectUnchecked(0, SLIP_RACE_HUD_BORDER_WIDTH, (int16_t)y,
			                         (int16_t)(SLIP_RACE_HUD_BORDER_WIDTH + width - 1),
			                         SLIP_RACE_HUD_INTRO_CONSOLE_FILL_BOTTOM);
		}
	}
}
