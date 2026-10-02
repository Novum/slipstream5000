#include "race_hud.h"
#include "raster/raster.h"

#include "byte_order.h"
#include "menu_resources.h"
#include "resource_host.h"
#include "sprite_resource_host.h"
#include "text_layout.h"
#include "track_world.h"

#include <assert.h>
#include <string.h>

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
		SlipResourceHost_ReleaseSequence(NULL, assets->positionSprites, 10);
		assets->positionSpritesLoaded = false;
	}
	SlipResourceHost_Release(NULL, assets->timeFont);
	SlipResourceHost_Release(NULL, assets->speedFont);
	SlipResourceHost_ReleaseSequence(NULL, assets->targetSightSprites, 2);
	SlipResourceHost_Release(NULL, assets->normalSightSprite);
	SlipResourceHost_ReleaseSequence(NULL, assets->topSprites, 2);
	SlipResourceHost_ReleaseSequence(NULL, assets->bottomSprites, 2);
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
	if (!SlipResourceHost_LoadSequence(NULL, "POS*.SPR", 0, 10, assets->positionSprites))
		return false;
	assets->positionSpritesLoaded = true;
	return true;
}

bool SlipRaceHud_LoadNormalSight(SlipRaceHudAssets *assets, const char *const *archives, size_t archiveCount) {
	(void)archives;
	(void)archiveCount;
	if (!SlipResourceHost_LoadSequence(NULL, "TSIGHT*.SPR", 0, 2, assets->targetSightSprites))
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
	topName[3] = bottomName[3] = (char)(driver + '0');
	char consoleVariant = 'N', extensionVariant = 'T';
	if (gameMode == 1) {
		topName[3] = bottomName[3] = 'S';
		consoleVariant = 'H';
		extensionVariant = '1';
	}
	topName[6] = bottomName[6] = turboName[6] = consoleVariant;
	consoleExtensionName[7] = extensionVariant;
	if (!SlipResourceHost_LoadSequence(NULL, topName, 1, 2, assets->topSprites))
		return false;
	if (!SlipResourceHost_LoadSequence(NULL, bottomName, 1, 2, assets->bottomSprites))
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
	SlipRaceHud_DrawSpriteResourceClipped(assets->normalSightSprite, framebuffer, pitch, centerX - 14, centerY - 11);
}

void SlipRaceHud_DrawTargetSight(const SlipRaceHudAssets *assets, uint8_t *framebuffer, int pitch, int screenX,
                                 int screenY, uint32_t drawPageToggle) {
	if (assets == NULL || !assets->targetSightsLoaded || framebuffer == NULL) {
		return;
	}
	SlipRaceHud_DrawSpriteResourceClipped(
	    assets->targetSightSprites[drawPageToggle & SLIP_RACE_HUD_ALTERNATING_SIGHT_FRAME_MASK], framebuffer, pitch,
	    screenX - 6, screenY - 6);
}

void SlipRaceHud_FormatTime(uint32_t milliseconds, char text[12]) {
	const uint32_t hours = milliseconds / 3600000u;
	uint32_t remainder = milliseconds - hours * 3600000u;
	const uint32_t minutes = remainder / 60000u;
	uint32_t seconds;
	uint32_t hundredths;
	uint32_t values[4];
	size_t i;

	remainder -= minutes * 60000u;
	seconds = remainder / 1000u;
	remainder -= seconds * 1000u;
	hundredths = remainder / 10u;
	values[0] = hours;
	values[1] = minutes;
	values[2] = seconds;
	values[3] = hundredths;
	for (i = 0; i < 4u; ++i) {
		text[i * 3u] = (char)(values[i] / 10u + '0');
		text[i * 3u + 1u] = (char)(values[i] % 10u + '0');
		if (i != 3u) {
			text[i * 3u + 2u] = ':';
		}
	}
	text[11] = '\0';
}

void SlipRaceHud_DrawFinishPosition(const SlipRaceHudAssets *assets, uint8_t *framebuffer, int pitch, int minY,
                                    uint16_t position) {
	int16_t rank = (int16_t)position;
	const SlipTextArgument arguments[] = {{.word = &rank}};
	SlipText_SelectResourceFont(&SlipText_state, assets->timeFont, &SlipRaceHud_fontResources);
	SlipText_SetStyle(&SlipText_state, 2, UINT16_MAX, 0, 319);
	SlipText_SetColor(&SlipText_state, 0xfe);
	SlipRaceHud_DrawLayoutHost(framebuffer, pitch, 0, minY + 5, "Finished Position %d", arguments);
}

void SlipRaceHud_DrawRaceStatus(const SlipRaceHudAssets *assets, uint8_t *framebuffer, int pitch, int minY, int maxX,
                                uint16_t racePosition, uint16_t currentLapNumber, uint32_t currentLapTime,
                                uint16_t lapTimeTimer, uint32_t lapTime) {
	char currentTime[12];
	uint32_t lapNumber = currentLapNumber;
	const SlipTextArgument lapArguments[] = {{.dword = &lapNumber}};

	if (assets == NULL || framebuffer == NULL) {
		return;
	}
	if (racePosition >= 1u && racePosition <= 10u && assets->positionSpritesLoaded) {
		SlipRaceHud_DrawSpriteResourceClipped(assets->positionSprites[racePosition - 1u], framebuffer, pitch,
		                                      maxX - 0x1a, minY + 4);
	}
	SlipRaceHud_FormatTime(currentLapTime, currentTime);
	currentTime[5] = ':';
	currentTime[8] = ';';
	SlipRaceHud_DrawTextResource(assets->timeFont, framebuffer, pitch, maxX - 0x3c, minY + 6, currentTime + 4, 0xfe);
	if (currentLapNumber != 0) {
		SlipText_SelectResourceFont(&SlipText_state, assets->timeFont, &SlipRaceHud_fontResources);
		SlipText_SetColor(&SlipText_state, 0xfe);
		SlipText_SetStyle(&SlipText_state, 2, UINT16_MAX, 0xfd, 0x123);
		SlipRaceHud_DrawLayoutHost(framebuffer, pitch, 0, minY + 0x0f, "=%2ld", lapArguments);
	}
	if (lapTimeTimer != 0) {
		SlipRaceHud_FormatTime(lapTime, currentTime);
		currentTime[5] = ':';
		currentTime[8] = ';';
		SlipRaceHud_DrawTextResource(assets->timeFont, framebuffer, pitch, maxX - 0x3c, minY + 0x18, currentTime + 4,
		                             0xfe);
	}
}

void SlipRaceHud_DrawSpeed(const SlipRaceHudAssets *assets, uint8_t *framebuffer, int pitch, int minX, int minY,
                           uint32_t inputSpeed, int units, uint16_t countdown) {

	const uint32_t quotient = inputSpeed / (units != 0 ? 0x1bcu : 0x2cbu);
	char text[] = "%d:";
	int16_t speed = (int16_t)quotient;
	const SlipTextArgument speedArguments[] = {{.word = &speed}};
	assert(quotient <= 0xffffu);
	if (assets == NULL || framebuffer == NULL || !assets->speedFontLoaded)
		return;
	/* ':' and ';' are the original unit glyphs, not punctuation to replace with host text. */
	text[2] = units != 0 ? ';' : ':';
	SlipText_SetColor(&SlipText_state, 0xfc);
	SlipText_SelectResourceFont(&SlipText_state, assets->speedFont, &SlipRaceHud_fontResources);
	SlipText_SetStyle(&SlipText_state, 0, UINT16_MAX, 10, 319);
	SlipRaceHud_DrawLayoutHost(framebuffer, pitch, minX + 10, minY + 6, text, speedArguments);
	SlipText_SelectResourceFont(&SlipText_state, SlipMenu_resources.smallFont, &SlipRaceHud_fontResources);

	if (countdown != 0) {
		char prepare[] = "Prepare to Race 1";
		prepare[16] = (char)((uint8_t)countdown + 0x30u);
		SlipText_SelectResourceFont(&SlipText_state, assets->timeFont, &SlipRaceHud_fontResources);
		SlipText_SetColor(&SlipText_state, 0xfe);
		SlipText_SetStyle(&SlipText_state, 2, UINT16_MAX, 0, 319);
		SlipRaceHud_DrawLayoutHost(framebuffer, pitch, 0, minY + 5, prepare, NULL);
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
	state->borderFrames = 2;
}

void SlipRaceHud_DrawBorders(SlipRaceHudState *state, uint32_t gameMode, uint32_t windowSize, uint32_t flags) {
	if (state->borderFrames == 0)
		return;
	--state->borderFrames;
	if (gameMode == 1) {

		Raster_FillRectUnchecked(0, 0, 0, 3, 199);
		Raster_FillRectUnchecked(0, 316, 0, 319, 199);
		Raster_DrawLineSolid(0, 4, 100, 315, 100);
	} else {

		Raster_FillRectUnchecked(0, 0, 0, 319, windowSize != 0 ? 31 : 7);
		if ((flags & SLIP_RACE_HUD_INTRO_PRESENTATION) == 0)
			Raster_FillRectUnchecked(0, 0, 193, 319, 199);
		Raster_FillRectUnchecked(0, 0, 8, 3, 192);
		Raster_FillRectUnchecked(0, 316, 8, 319, 192);
	}
}

int SlipRaceHud_DrawUpperConsole(const SlipRaceHudAssets *assets, uint16_t light, uint8_t *framebuffer, int pitch,
                                 int minX, int maxY) {
	const int variant = light >= 0x2000u ? 0 : 1;
	uint16_t top;

	if (assets == NULL || !assets->consoleSpritesLoaded || framebuffer == NULL) {
		return 0;
	}
	top = assets->topSprites[variant];
	const uint8_t *const data = SlipResourceHost_Lock(NULL, top);
	const uint16_t height = SlipBytes_ReadLE16(data + 2);
	SlipResourceHost_Unlock(NULL, top);
	SlipRaceHud_DrawSpriteResourceClipped(top, framebuffer, pitch, minX, maxY - (int)height + 1);
	return variant + 1;
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
		*transition = 2;
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
	bottomIndex = current - 1;
	if (bottomIndex >= 0 && bottomIndex < 2) {
		const uint16_t bottom = assets->bottomSprites[bottomIndex];

		SlipRaceHud_DrawSpriteResource(bottom, framebuffer, pitch, 4, (int)y);
		if (view == 1 && (assets->flags & SLIP_RACE_HUD_INTRO_PRESENTATION) != 0) {

			Raster_FillRectClipped(
			    0, 0, (int16_t)(y + SlipSprite_Dimensions(bottom, &SlipSpriteHost_effectResources).height - 1u), 319,
			    199);
		}
	} else if (bottomIndex < 0) {
		if (view != 1 || (assets->flags & SLIP_RACE_HUD_INTRO_PRESENTATION) == 0) {

			Raster_DrawLineClipped(0, 0, (int16_t)y, 319, (int16_t)y);
			SlipRaceHud_DrawSpriteResourceClipped(assets->consoleExtensionSprite, framebuffer, pitch, 4, (int)y + 1);
		} else {

			const uint8_t *const data = SlipResourceHost_Lock(NULL, assets->bottomSprites[0]);
			const uint16_t width = SlipBytes_ReadLE16(data);
			SlipResourceHost_Unlock(NULL, assets->bottomSprites[0]);
			Raster_FillRectUnchecked(0, 4, (int16_t)y, (int16_t)(4 + width - 1), 197);
		}
	}
}
