#include "race_display.h"
#include "gpu/renderer.h"
#include "port_app_bridge.h"
#include "race.h"
#include "race_hud.h"
#include "raster/overlay.h"
#include "vga_dac.h"
#include <string.h>

enum {
	SLIP_RACE_DISPLAY_MAXIMUM_OUTPUT_DIMENSION = 8192,
	SLIP_RACE_DISPLAY_SPLIT_VIEW_HEIGHT = SLIPSTREAM_SCREEN_HEIGHT / 2,
	SLIP_RACE_DISPLAY_HUD_CENTER_TOP = 0,
	SLIP_RACE_DISPLAY_HUD_CENTER_MIDDLE = 1,
	SLIP_RACE_DISPLAY_HUD_RIGHT_EDGE = 2,
	SLIP_RACE_DISPLAY_HUD_LEFT_EDGE = 3,
	SLIP_RACE_DISPLAY_HUD_BOTTOM_EDGE = 4,
	SLIP_RACE_DISPLAY_HUD_REGION_COUNT = 5,
	SLIP_RACE_DISPLAY_LEFT_HUD_MARGIN = 8,
	SLIP_RACE_DISPLAY_LEFT_HUD_ART_OFFSET = 14
};

bool SlipRaceDisplay_highRes;
bool SlipRaceDisplay_ready;
static bool alignHudEdges;
static bool bottomAlignCockpit;
static int width, height;
static uint8_t overlayCoverage[SLIPSTREAM_SCREEN_WIDTH * SLIPSTREAM_SCREEN_HEIGHT];
static bool (*outputSize)(int *, int *);
static void (*saveSettings)(void);
static uint8_t *overlayPixels;
static bool frameActive;
static int worldHeight, originalHeight;
static SDL_Rect monitor;

void SlipRaceDisplay_Configure(bool (*size)(int *, int *), void (*save)(void)) {
	outputSize = size;
	saveSettings = save;
}

void SlipRaceDisplay_Toggle(void *context) {
	(void)context;
	SlipRaceDisplay_highRes = !SlipRaceDisplay_highRes;
	if (saveSettings != NULL)
		saveSettings();
}

void SlipRaceDisplay_BeginFrame(uint8_t *overlay, bool alignCockpit) {
	SlipRaceDisplay_EndFrame();
	monitor = (SDL_Rect){0};
	bottomAlignCockpit = alignCockpit;
	if (!SlipRaceDisplay_highRes || outputSize == NULL || !outputSize(&width, &height) || width < 2 || height < 2 ||
	    width > SLIP_RACE_DISPLAY_MAXIMUM_OUTPUT_DIMENSION || height > SLIP_RACE_DISPLAY_MAXIMUM_OUTPUT_DIMENSION)
		return;
	if (!SlipRaceGpu_Available())
		return;
	SlipVgaDac_RefreshArgbPalette(g_palette);
	SlipRaceGpu_BeginFrame(width, height);
	memset(overlayCoverage, 0, sizeof(overlayCoverage));
	overlayPixels = overlay;
	frameActive = true;
}

static void SlipRaceDisplay_SaveView(SlipDraw3DProjectState *project, SlipRaceDisplayView *saved) {
	saved->project = project;
	saved->projection = *project;
	Raster_GetClipRect(&saved->minX, &saved->minY, &saved->maxX, &saved->maxY);
	RasterOverlay_End();
}

bool SlipRaceDisplay_BeginWorld(SlipDraw3DProjectState *project, SlipRaceDisplayView *saved, uint32_t gameMode) {
	if (!frameActive)
		return false;
	SlipRaceDisplay_SaveView(project, saved);
	const int top = gameMode == SLIP_RACE_GAME_SPLIT_SCREEN && project->minY >= SLIP_RACE_DISPLAY_SPLIT_VIEW_HEIGHT
	                    ? height / 2
	                    : 0;
	const int bottom = gameMode == SLIP_RACE_GAME_SPLIT_SCREEN && top == 0 ? height / 2 : height;
	const int viewHeight = bottom - top;
	worldHeight = viewHeight;
	alignHudEdges = gameMode != SLIP_RACE_GAME_SPLIT_SCREEN;
	originalHeight =
	    gameMode == SLIP_RACE_GAME_SPLIT_SCREEN ? SLIP_RACE_DISPLAY_SPLIT_VIEW_HEIGHT : SLIPSTREAM_SCREEN_HEIGHT;
	int lineWidth = (viewHeight + originalHeight / 2) / originalHeight;
	SlipRaceGpu_BeginWorld(lineWidth < 1 ? 1 : lineWidth);
	const int originalTop =
	    gameMode == SLIP_RACE_GAME_SPLIT_SCREEN && top != 0 ? SLIP_RACE_DISPLAY_SPLIT_VIEW_HEIGHT : 0;
	const int centerY = top + (project->centerY - originalTop) * viewHeight / originalHeight;
	/* Match the original vertical field of view; extra horizontal pixels reveal more of the track. */
	const uint32_t scale = (uint32_t)((uint64_t)project->perspectiveScale * viewHeight / originalHeight);
	/* 320x200 DOS pixels were displayed at 4:3 (a 5:6 pixel aspect). */
	project->squarePixels = true;
	SlipDraw3D_SetProjectionScale(project, scale);
	SlipDraw3D_SetViewport(project, 0, top, width - 1, bottom - 1, width / 2, centerY);
	Raster_SetClipRect(0, (int16_t)top, (int16_t)(width - 1), (int16_t)(bottom - 1));
	SlipRaceDisplay_ready = true;
	return true;
}

int32_t SlipRaceDisplay_ScaleWorldOffset(int32_t offset) {
	if (!SlipRaceDisplay_ready || !SlipRaceGpu_Active())
		return offset;
	return (int32_t)((int64_t)offset * worldHeight / originalHeight);
}

void SlipRaceDisplay_EndWorld(const SlipRaceDisplayView *saved) {
	SlipRaceGpu_EndWorld();
	*saved->project = saved->projection;
	Raster_SetClipRect(saved->minX, saved->minY, saved->maxX, saved->maxY);
	RasterOverlay_Begin(overlayPixels, overlayCoverage);
}

bool SlipRaceDisplay_BeginMonitor(SlipDraw3DProjectState *project, SlipRaceDisplayView *saved) {
	if (!SlipRaceDisplay_ready)
		return false;
	SlipRaceDisplay_SaveView(project, saved);
	monitor =
	    (SDL_Rect){project->minX, project->minY, project->maxX - project->minX + 1, project->maxY - project->minY + 1};
	const float sy = height / (float)SLIPSTREAM_SCREEN_HEIGHT,
	            sx = sy * SLIPSTREAM_PIXEL_ASPECT_WIDTH / SLIPSTREAM_PIXEL_ASPECT_HEIGHT;
	const float left = (width - SLIPSTREAM_SCREEN_WIDTH * sx) / 2;
	const int minX = (int)(left + project->minX * sx), minY = (int)(project->minY * sy);
	const int maxX = (int)(left + (project->maxX + 1) * sx) - 1;
	const int maxY = (int)((project->maxY + 1) * sy) - 1;
	const int centerX = (int)(left + project->centerX * sx), centerY = (int)(project->centerY * sy);
	worldHeight = height;
	originalHeight = SLIPSTREAM_SCREEN_HEIGHT;
	SlipRaceGpu_BeginWorld((int)(sy + .5f) > 0 ? (int)(sy + .5f) : 1);
	project->squarePixels = true;
	SlipDraw3D_SetProjectionScale(project, (uint32_t)(project->perspectiveScale * sy));
	SlipDraw3D_SetViewport(project, minX, minY, maxX, maxY, centerX, centerY);
	Raster_SetClipRect((int16_t)minX, (int16_t)minY, (int16_t)maxX, (int16_t)maxY);
	RasterPoint background[] = {{minX, minY}, {maxX + 1, minY}, {maxX + 1, maxY + 1}, {minX, maxY + 1}};
	SlipRaceGpu_Flat(background, 4, 0, 0);
	return true;
}

void SlipRaceDisplay_EndMonitor(const SlipRaceDisplayView *saved) {
	SlipRaceDisplay_EndWorld(saved);
	/* The native scene replaces the old indexed monitor pixels; labels remain an overlay. */
	for (int y = monitor.y; y < monitor.y + monitor.h; y++)
		memset(overlayCoverage + y * SLIPSTREAM_SCREEN_WIDTH + monitor.x, 0, (size_t)monitor.w);
}

bool SlipRaceDisplay_BeginMap(void) {
	if (!SlipRaceDisplay_ready)
		return false;
	RasterOverlay_End();
	SlipRaceGpu_BeginMap();
	return true;
}

void SlipRaceDisplay_EndMap(void) {
	SlipRaceGpu_EndMap();
	RasterOverlay_Begin(overlayPixels, overlayCoverage);
}

void SlipRaceDisplay_EndFrame(void) {
	SlipRaceGpu_EndWorld();
	RasterOverlay_End();
	frameActive = false;
	SlipRaceDisplay_ready = false;
}

static void SlipRaceDisplay_DrawHudRegion(const int *r, SDL_FRect destination) {
	SDL_Rect region = {r[0], r[1], r[2] - r[0], r[3] - r[1]}, intersection;
	SDL_Rect outer = {monitor.x - 1, monitor.y - 1, monitor.w + 2, monitor.h + 2};
	if (!monitor.w || !SDL_GetRectIntersection(&region, &outer, &intersection)) {
		SlipRaceGpu_Overlay(overlayPixels, overlayCoverage, r[0], r[1], r[2], r[3], destination);
		return;
	}
	/* Keep the monitor and its label together instead of splitting it at the right HUD anchor. */
	SDL_Rect pieces[] = {
	    {region.x, region.y, region.w, intersection.y - region.y},
	    {region.x, intersection.y + intersection.h, region.w, region.y + region.h - intersection.y - intersection.h},
	    {region.x, intersection.y, intersection.x - region.x, intersection.h},
	    {intersection.x + intersection.w, intersection.y, region.x + region.w - intersection.x - intersection.w,
	     intersection.h}};
	for (unsigned i = 0; i < 4; i++) {
		SDL_Rect p = pieces[i];
		if (p.w <= 0 || p.h <= 0)
			continue;
		float sx = destination.w / region.w, sy = destination.h / region.h;
		SlipRaceGpu_Overlay(overlayPixels, overlayCoverage, p.x, p.y, p.x + p.w, p.y + p.h,
		                    (SDL_FRect){destination.x + (p.x - region.x) * sx, destination.y + (p.y - region.y) * sy,
		                                p.w * sx, p.h * sy});
	}
}

void SlipRaceDisplay_DrawOverlay(void) {
	const float h = (float)height, w = (float)width;
	const float hudWidth = h * SLIPSTREAM_DISPLAY_ASPECT_WIDTH / SLIPSTREAM_DISPLAY_ASPECT_HEIGHT,
	            left = (w - hudWidth) / 2, sx = hudWidth / SLIPSTREAM_SCREEN_WIDTH, sy = h / SLIPSTREAM_SCREEN_HEIGHT;
	if (!alignHudEdges) {
		SlipRaceGpu_Overlay(overlayPixels, overlayCoverage, 0, 0, SLIPSTREAM_SCREEN_WIDTH, SLIPSTREAM_SCREEN_HEIGHT,
		                    (SDL_FRect){left, 0, hudWidth, h});
	} else {
		/* Cockpit art excludes the original bottom border; intro captions use all 200 source rows. */
		const int rects[SLIP_RACE_DISPLAY_HUD_REGION_COUNT][4] = {
		    [SLIP_RACE_DISPLAY_HUD_CENTER_TOP] = {110, 0, 250, 38},
		    [SLIP_RACE_DISPLAY_HUD_CENTER_MIDDLE] = {0, 38, 250, 150},
		    [SLIP_RACE_DISPLAY_HUD_RIGHT_EDGE] = {250, 0, SLIPSTREAM_SCREEN_WIDTH, 150},
		    [SLIP_RACE_DISPLAY_HUD_LEFT_EDGE] = {0, 0, 110, 38},
		    [SLIP_RACE_DISPLAY_HUD_BOTTOM_EDGE] = {0, 150, SLIPSTREAM_SCREEN_WIDTH,
		                                           bottomAlignCockpit ? SLIP_RACE_HUD_BOTTOM_BORDER_TOP
		                                                              : SLIPSTREAM_SCREEN_HEIGHT}};
		for (int i = 0; i < SLIP_RACE_DISPLAY_HUD_REGION_COUNT; i++) {
			const int *r = rects[i];
			float x = left + r[0] * sx, y = r[1] * sy;
			if (i == SLIP_RACE_DISPLAY_HUD_RIGHT_EDGE)
				x = w - hudWidth + r[0] * sx;
			if (i == SLIP_RACE_DISPLAY_HUD_LEFT_EDGE)
				x = h * SLIP_RACE_DISPLAY_LEFT_HUD_MARGIN / SLIPSTREAM_SCREEN_HEIGHT -
				    SLIP_RACE_DISPLAY_LEFT_HUD_ART_OFFSET * sx;
			if (i == SLIP_RACE_DISPLAY_HUD_BOTTOM_EDGE && bottomAlignCockpit)
				y += (SLIPSTREAM_SCREEN_HEIGHT - SLIP_RACE_HUD_BOTTOM_BORDER_TOP) * sy;
			SlipRaceDisplay_DrawHudRegion(r, (SDL_FRect){x, y, (r[2] - r[0]) * sx, (r[3] - r[1]) * sy});
		}
		if (monitor.w)
			SlipRaceGpu_Overlay(overlayPixels, overlayCoverage, monitor.x - 1, monitor.y - 1, monitor.x + monitor.w + 1,
			                    monitor.y + monitor.h + 1,
			                    (SDL_FRect){left + (monitor.x - 1) * sx, (monitor.y - 1) * sy, (monitor.w + 2) * sx,
			                                (monitor.h + 2) * sy});
	}
}
