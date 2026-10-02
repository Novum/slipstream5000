#ifndef SLIPSTREAM5000_PORT_APP_BRIDGE_H
#define SLIPSTREAM5000_PORT_APP_BRIDGE_H

#include "font.h"
#include "raster/raster.h"
#include "sprite.h"
#include "view3d.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SLIPSTREAM_DRIVER_COUNT 10

typedef struct VehicleViewParams {
	int16_t distanceLowWord;
	int16_t distanceHighWord;
	int16_t pitchAngle;
	int16_t centerYOffset;
} VehicleViewParams;

/* Present scale: 320x200 into the (320*N)x(320*N*3/4) 4:3 CRT space. */
#define SLIP_PRESENT_SCALE 6
#define SLIP_OUT_WIDTH (SLIPSTREAM_SCREEN_WIDTH * SLIP_PRESENT_SCALE)
#define SLIP_OUT_HEIGHT (SLIP_OUT_WIDTH * 3 / 4)

typedef uint8_t SlipScreenPage[SLIPSTREAM_SCREEN_WIDTH * SLIPSTREAM_SCREEN_HEIGHT];
extern SlipScreenPage *g_drawPage;
extern SlipScreenPage *g_displayPage;
extern SlipScreenPage g_physicalDisplay;
/* Preserve the array extent for framebuffer consumers, including sizeof. */
#define g_framebuffer (*g_drawPage)
#define g_displayFramebuffer g_physicalDisplay
extern uint32_t g_palette[256];
extern uint32_t g_presentPixels[SLIPSTREAM_SCREEN_WIDTH * SLIPSTREAM_SCREEN_HEIGHT];
extern bool g_presentPixelsReady;
extern const VehicleViewParams g_vehicleViewParams[SLIPSTREAM_DRIVER_COUNT];
extern const char *g_trackResourceNames[SLIPSTREAM_DRIVER_COUNT];
extern const uint8_t g_trackMenuToResourceIndex[SLIPSTREAM_DRIVER_COUNT];
extern const SlipView3DVec32 g_trackFirstSpawnRecords[SLIPSTREAM_DRIVER_COUNT];
extern const uint16_t g_trackPlayerStartRecordIndices[SLIPSTREAM_DRIVER_COUNT];
extern const int16_t g_trackGlobeFlagCoords[SLIPSTREAM_DRIVER_COUNT][2];
extern const int16_t g_trackGlobeOrientation[SLIPSTREAM_DRIVER_COUNT][6];
extern char g_driverDescriptions[SLIPSTREAM_DRIVER_COUNT][512];

uint64_t SlipSdl_TicksMs(void);
union SDL_Event;
bool SlipSdl_PollEvent(union SDL_Event *event);
bool SlipSdl_RunStartupIntro(const char *resourcePath);
uint8_t SlipDebug_BiosTickLow(void);
void SlipSdl_DelayMs(uint32_t ms);
size_t SlipMenu_BuildArchiveList(const char *resPath, char *secondaryPath, const char *archives[2]);
void SlipMenu_MakeDriverSpriteName(char *dst, size_t dstSize, const char *prefix, int driver, const char *suffix);
bool SlipMenu_DrawSpriteFromRes(const char *resPath, const char *name, bool applySpritePalette);
bool SlipMenu_DrawVehiclePanel(const char *resPath, int driver);
bool SlipMenu_ApplyPaletteResource(const char *const *archives, size_t archiveCount, const char *paletteName);

#endif
