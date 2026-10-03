#include "vehicle_selector_host.h"
#include "game_errors.h"
#include "input_zone.h"
#include "menu.h"
#include "menu_music.h"
#include "port_app_bridge.h"
#include "resource_host.h"
#include "sprite_resource_host.h"
#include "text_layout.h"
#include "vga_dac.h"
#include <stdlib.h>

/* These adapters bind the named original callees to native resource views.
 * They do not supply the selector's event loop or a substitute viewer. */
static const SlipStringTableResources strings = {.load = SlipResourceHost_Load,
                                                 .lock = SlipResourceHost_Lock,
                                                 .unlock = SlipResourceHost_Unlock,
                                                 .release = SlipResourceHost_Release};

static void SlipVehicleSelector_ResourceError(void *context) {
	(void)context;
	SlipGame_ResourceFailure();
}

static void SlipVehicleSelector_FileError(void *context) {
	(void)context;
	SlipGame_FileFailure();
}

static SlipSprite SlipVehicleSelector_SpriteView(uint16_t resource) {
	SlipResourcePayload payload = SlipResourceHost_Payload(resource);
	SlipSprite sprite;
	if (!SlipSprite_FromPayload(&payload, &sprite))
		SlipGame_ResourceFailure();
	return sprite;
}

static uint32_t SlipVehicleSelector_Size(void *context, uint16_t resource) {
	uint32_t bytes;
	SlipResourceHost_Size(context, resource, &bytes);
	return bytes;
}

static SlipSelectorRectangle SlipVehicleSelector_Bounds(void *context, uint16_t resource) {
	(void)context;
	SlipSpriteBounds rectangle = SlipSprite_Bounds(resource, &SlipSpriteHost_effectResources);
	return (SlipSelectorRectangle){rectangle.left, rectangle.top, rectangle.right, rectangle.bottom};
}

static SlipSelectorDimensions SlipVehicleSelector_Dimensions(void *context, uint16_t resource) {
	(void)context;
	SlipSpriteDimensions value = SlipSprite_Dimensions(resource, &SlipSpriteHost_effectResources);
	return (SlipSelectorDimensions){value.width, value.height};
}

static void SlipVehicleSelector_Grayscale(void *context, uint16_t resource) {
	(void)context;
	SlipSprite_Grayscale(resource, NULL, &SlipSpriteHost_effectResources);
}

static void SlipVehicleSelector_Sprite(void *context, uint16_t resource, int16_t x, int16_t y) {
	SlipResourceHost_Lock(context, resource);
	SlipSprite view = SlipVehicleSelector_SpriteView(resource);
	SlipSprite_DrawClipped(&view, g_screenBufferBase, g_screenPitch, x, y);
	SlipResourceHost_Unlock(context, resource);
}

static void SlipVehicleSelector_SpritePalette(void *context, uint16_t resource) {
	SlipResourceHost_Lock(context, resource);
	SlipSprite view = SlipVehicleSelector_SpriteView(resource);
	SlipSprite_ApplyPalette(&view);
	SlipResourceHost_Unlock(context, resource);
}

static void SlipVehicleSelector_Palette(void *context, const uint8_t *packet) {
	(void)context;

	const uint16_t first = (uint16_t)(packet[0] | (uint16_t)packet[1] << 8);
	const uint16_t count =
	    (uint16_t)(packet[SLIP_PALETTE_COUNT_OFFSET] | (uint16_t)packet[SLIP_PALETTE_COUNT_OFFSET + 1] << 8);
	SlipVgaDac_WriteRange(first, count, packet + SLIP_PALETTE_HEADER_BYTES);
}

static uint16_t SlipVehicleSelector_BlendPalette(void *context, const uint8_t *first, const uint8_t *second,
                                                 int16_t amount) {
	(void)context;
	return SlipPalette_Blend(first, second, amount, &SlipSpriteHost_effectResources);
}

static SlipFont SlipVehicleSelector_LockFont(void *context, uint16_t resource) {
	SlipResourceHost_Lock(context, resource);
	SlipResourcePayload payload = SlipResourceHost_Payload(resource);
	SlipFont view;
	if (!SlipFont_FromPayload(&payload, &view))
		SlipGame_ResourceFailure();
	return view;
}

static const SlipFontResourceCalls fontCalls = {.lock = SlipVehicleSelector_LockFont,
                                                .unlock = SlipResourceHost_Unlock};

static void SlipVehicleSelector_Font(void *context, uint16_t resource) {
	(void)context;
	SlipText_SelectResourceFont(&SlipText_state, resource, &fontCalls);
}

static void SlipVehicleSelector_Navigation(void *context, SlipInputNavigationTable *table) {
	(void)context;
	SlipInput_SetNavigation(table);
}

static void SlipVehicleSelector_ClearNavigation(void *context) {
	(void)context;
	SlipInput_ClearNavigation();
}

static void SlipVehicleSelector_Poll(void *context) {
	(void)context;
	if (!SlipMenu_PollInput()) {
		SlipRuntime_Shutdown();
		SDL_Quit();
		exit(0);
	}
}

static bool SlipVehicleSelector_Pressed(void *context, SlipInputCode code) {
	(void)context;
	return SlipInput_TestAndClear(SlipInput_pressed, code);
}

static uint32_t SlipVehicleSelector_ClockMilliseconds(void *context) {
	(void)context;
	return (uint32_t)SlipSdl_TicksMs();
}

static SlipInputPointerPosition SlipVehicleSelector_ZonePointer(void *context) {
	(void)context;
	return SlipInput_Pointer();
}

static uint16_t SlipVehicleSelector_Zone(void *context, uint16_t resource) {
	const SlipInputZoneCalls calls = {context, SlipResourceHost_Lock, SlipVehicleSelector_ZonePointer,
	                                  SlipResourceHost_Unlock};
	return SlipInput_Zone(resource, &calls);
}

static SlipSelectorPoint SlipVehicleSelector_Pointer(void *context) {
	SlipInputPointerPosition position = SlipVehicleSelector_ZonePointer(context);
	return (SlipSelectorPoint){(int16_t)position.x, (int16_t)position.y};
}

static uint32_t SlipVehicleSelector_HitTest(void *context, const SlipSelectorRectangle *rectangles, uint16_t count,
                                            SlipSelectorPoint position) {
	(void)context;
	return SlipInput_HitTest(rectangles, count, position.x, position.y);
}

static void SlipVehicleSelector_Present(void *context) {
	(void)context;
	SlipMenu_PresentFrame();
}

static void SlipVehicleSelector_MusicBranch(void *context, uint32_t selection) {
	(void)context;
	SlipMenuMusic_Branch(selection);
}

static void SlipVehicleSelector_BindSprite(void *context, uint16_t resource, SlipSelectorDimensions size) {
	SlipVehicleSelectorHost *const host = context;
	host->boundSpriteHandle = resource;
	uint8_t *const payload = SlipResourceHost_LockWritable(context, resource);
	Raster_BindSprite(payload + SLIP_SPRITE_HEADER_BYTES, size.width, size.height, &host->previousScreenSurface);
}

static void SlipVehicleSelector_RestoreScreen(void *context) {
	SlipVehicleSelectorHost *const host = context;
	if (host->boundSpriteHandle != 0)
		SlipResourceHost_Unlock(context, host->boundSpriteHandle);
	Raster_RestoreScreen(&host->previousScreenSurface);
}

static void SlipVehicleSelector_BakeText(void *context, uint16_t resource, const char *text, uint16_t color,
                                         int16_t y) {
	SlipText_SetColor(&SlipText_state, color);
	SlipSelectorDimensions size = SlipVehicleSelector_Dimensions(context, resource);
	SlipVehicleSelector_BindSprite(context, resource, size);
	SlipText_SetStyle(&SlipText_state, SLIP_TEXT_CENTERED, UINT16_MAX, 0, (int16_t)(size.width - 1));
	SlipTextPosition position = {0, y};
	SlipText_Draw(&SlipText_state, text, NULL, &position);
	SlipVehicleSelector_RestoreScreen(context);
}

static void SlipVehicleSelector_Zoom(void *context, uint16_t resource, int16_t scale, SlipSelectorPoint center,
                                     SlipSelectorPoint target) {
	(void)context;
	SlipSprite_Zoom(resource, scale, center.x, center.y, target.x, target.y, &SlipSpriteHost_effectResources);
}

static void SlipVehicleSelector_Background(void *context, SlipVehicleSelector *selector) {
	SlipVehicleSelectorHost *const host = context;
	RasterSurfaceBounds surface = Raster_GetSurfaceBounds();
	Raster_SetClipRect((int16_t)surface.left, (int16_t)surface.top, (int16_t)surface.right, (int16_t)surface.bottom);
	SlipVehicleSelector_Sprite(context, selector->assets.background, 0, 0);
	SlipVehicleSelector_Font(context, selector->assets.titleFont);
	SlipText_SetStyle(&SlipText_state, SLIP_TEXT_CENTERED, UINT16_MAX, 0, SLIPSTREAM_SCREEN_WIDTH - 1);
	SlipText_SetColor(&SlipText_state, SLIP_VEHICLE_SELECTION_TITLE_COLOUR);
	const char *const title = SlipStringTable_Get(selector->assets.strings, selector->animation.titleTag, &strings);
	SlipTextPosition position = {0, SLIP_VEHICLE_SELECTION_TITLE_TOP};
	SlipText_Draw(&SlipText_state, title, NULL, &position);
	SlipStringTable_Unlock(selector->assets.strings, &strings);
	SlipVehicleSelector_Font(context, host->smallFontHandle);
	const int32_t markerIndex = (int32_t)selector->excludedVehicle - 1;
	if (markerIndex >= 0) {
		static const int16_t positions[SLIP_RACE_RACER_COUNT][2] = {
		    {109, 116}, {13, 107}, {206, 115}, {250, 76}, {200, 45}, {93, 77}, {2, 66}, {167, 73}, {123, 43}, {74, 50}};
		SlipVehicleSelector_Sprite(context, selector->playerMarker, positions[markerIndex][0],
		                           positions[markerIndex][1]);
	}
	for (unsigned i = 0; i < SLIP_RACE_RACER_COUNT; ++i) {
		if (selector->animation.doors[i].frame != 0)
			selector->animation.doors[i].redrawPasses = SLIP_VEHICLE_SELECTION_REDRAW_PASSES;
	}
}

void SlipVehicleSelector_BindNativeCalls(SlipVehicleSelectorCalls *calls) {
	calls->resources = strings;
	calls->errors =
	    (SlipVehicleSelectionErrors){NULL, SlipVehicleSelector_ResourceError, SlipVehicleSelector_FileError};
	calls->lock = SlipResourceHost_LockWritable;
	calls->resourceSize = SlipVehicleSelector_Size;
	calls->allocate = SlipResourceHost_Allocate;
	calls->spriteBounds = SlipVehicleSelector_Bounds;
	calls->grayscale = SlipVehicleSelector_Grayscale;
	calls->spritePalette = SlipVehicleSelector_SpritePalette;
	calls->selectFont = SlipVehicleSelector_Font;
	calls->setNavigation = SlipVehicleSelector_Navigation;
	calls->clearNavigation = SlipVehicleSelector_ClearNavigation;
	calls->poll = SlipVehicleSelector_Poll;
	calls->pressed = SlipVehicleSelector_Pressed;
	calls->frameClockMilliseconds = SlipVehicleSelector_ClockMilliseconds;
	calls->zone = SlipVehicleSelector_Zone;
	calls->background = SlipVehicleSelector_Background;
	calls->drawSprite = SlipVehicleSelector_Sprite;
	calls->blendPalette = SlipVehicleSelector_BlendPalette;
	calls->palette = SlipVehicleSelector_Palette;
	calls->present = SlipVehicleSelector_Present;
	calls->musicBranch = SlipVehicleSelector_MusicBranch;
	calls->bakeText = SlipVehicleSelector_BakeText;
	calls->spriteDimensions = SlipVehicleSelector_Dimensions;
	calls->bindSprite = SlipVehicleSelector_BindSprite;
	calls->restoreScreen = SlipVehicleSelector_RestoreScreen;
	calls->zoom = SlipVehicleSelector_Zoom;
	calls->pointer = SlipVehicleSelector_Pointer;
	calls->hitTest = SlipVehicleSelector_HitTest;
}
