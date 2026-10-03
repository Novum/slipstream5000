#include "screen_present_host.h"
#include "port_app_bridge.h"
#include "resource_host.h"
#include "screen_lifecycle.h"
#include "screen_present.h"
#include "vga_dac.h"
#include <string.h>

SlipScreenPage g_physicalDisplay;
SlipScreenPage *g_drawPage;
SlipScreenPage *g_displayPage;

enum { SLIP_SCREEN_CURSOR_BACKGROUND_BYTES = 4096 };

static SlipScreenLifecycle screen = {.cursor = {.x = SLIPSTREAM_SCREEN_WIDTH / 2, .y = SLIPSTREAM_SCREEN_HEIGHT / 2}};
static uint8_t biosMode = SLIP_SCREEN_BIOS_MODE_80X25_TEXT;
static uint8_t savedPixels[SLIP_SCREEN_CURSOR_BACKGROUND_BYTES];
static SlipCursorPixels cursorPixels = {.background = {.pixels = savedPixels}};

void SlipScreenHost_SelectCursor(void *context, bool useDefault, const SlipCursorSprite *sprite) {
	(void)context;
	SlipCursor_SelectSprite(&cursorPixels, useDefault, sprite);
}

static void SlipScreenHost_Retrace(void *context) {
	(void)context;
	/* The host presentation callback owns display pacing in place of VGA I/O. */
}

static void SlipScreenHost_Palette(void *context) {
	(void)context;
	SlipVgaDac_Commit();
}

static void SlipScreenHost_PresentChanged(void) {
	uint8_t *draw = *g_drawPage;
	uint8_t *previous = *g_displayPage;
	const SlipScreenPresentCalls calls = {NULL, SlipScreenHost_Retrace, SlipScreenHost_Palette};
	SlipScreen_Present(&draw, &previous, g_physicalDisplay, &screen.cursor, &cursorPixels, &calls);
	/* Array-pointer views preserve the extent used by framebuffer consumers. */
	g_drawPage = (SlipScreenPage *)draw;
	g_displayPage = (SlipScreenPage *)previous;
}

static void SlipScreenHost_PresentSingle(void) {
	const SlipScreenPresentCalls calls = {NULL, SlipScreenHost_Retrace, SlipScreenHost_Palette};
	SlipScreen_PresentSingle(*g_drawPage, g_physicalDisplay, &screen.cursor, &cursorPixels, &calls);
}

static uint8_t SlipScreenHost_GetBiosMode(void *context) {
	(void)context;
	return biosMode;
}

static void SlipScreenHost_SetBiosMode(void *context, uint8_t mode) {
	(void)context;
	biosMode = mode;
	/* SDL owns the physical window. This is the mode-13 VGA state binding. */
	if (mode == SLIP_SCREEN_BIOS_MODE_320X200_256_COLOURS) {
		memset(g_physicalDisplay, 0, sizeof(g_physicalDisplay));
		SlipVgaDac_InitializeHostBiosDefaults();
	}
}

static void SlipScreenHost_ReadDac(void *context,
                                   uint8_t colors[SLIP_VGA_DAC_PALETTE_COUNT][SLIP_VGA_DAC_CHANNEL_COUNT]) {
	(void)context;
	SlipVgaDac_ReadRange(0, SLIP_VGA_DAC_PALETTE_COUNT, colors[0]);
}

static void SlipScreenHost_BindRows(void *context, uint8_t *page, uint32_t offset, uint16_t pitch) {
	(void)context;
	Raster_SetScreenBufferRows(page + offset, pitch);
}

static void SlipScreenHost_SetClip(void *context, SlipScreenClip clip) {
	(void)context;
	Raster_SetClipRect(clip.left, clip.top, clip.right, clip.bottom);
}

static void SlipScreenHost_RegisterCleanup(void *context, SlipRuntimeCleanup cleanup) {
	(void)context;
	SlipRuntime_RegisterExit(cleanup);
}

static void SlipScreenHost_Fatal(void *context, const char *message) {
	(void)context;
	SlipRuntime_Fatal(message);
}

static void SlipScreenHost_Cleanup(void);
static const SlipScreenLifecycleCalls lifecycleCalls = {.allocate = SlipResourceHost_Allocate,
                                                        .lockPixels = SlipResourceHost_LockWritable,
                                                        .lockPerspective = SlipResourceHost_LockPerspectiveTable,
                                                        .unlock = SlipResourceHost_Unlock,
                                                        .release = SlipResourceHost_Release,
                                                        .getBiosMode = SlipScreenHost_GetBiosMode,
                                                        .setBiosMode = SlipScreenHost_SetBiosMode,
                                                        .readDac = SlipScreenHost_ReadDac,
                                                        .bindRows = SlipScreenHost_BindRows,
                                                        .setClip = SlipScreenHost_SetClip,
                                                        .registerExit = SlipScreenHost_RegisterCleanup,
                                                        .fatal = SlipScreenHost_Fatal,
                                                        .cleanup = SlipScreenHost_Cleanup,
                                                        .presentChangedPages = SlipScreenHost_PresentChanged,
                                                        .presentSinglePage = SlipScreenHost_PresentSingle};

static void SlipScreenHost_Cleanup(void) { SlipScreen_Cleanup(&screen, &lifecycleCalls); }

void SlipScreenHost_Initialize(void) {
	SlipScreen_Install(&screen, SLIP_SCREEN_AUTO, &lifecycleCalls);
	g_drawPage = (SlipScreenPage *)screen.drawPage;
	g_displayPage = (SlipScreenPage *)screen.previousPage;
	Raster_perspectiveTable = screen.perspectiveTable;
	cursorPixels.rowOffsets = screen.rowOffsets;
	cursorPixels.clipLeft = screen.cursorClip.left;
	cursorPixels.clipTop = screen.cursorClip.top;
	cursorPixels.clipRight = screen.cursorClip.right;
	cursorPixels.clipBottom = screen.cursorClip.bottom;
}

void SlipScreenHost_Present(void) { screen.present(); }
