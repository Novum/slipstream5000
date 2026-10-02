#ifndef SLIPSTREAM5000_RACE_DISPLAY_H
#define SLIPSTREAM5000_RACE_DISPLAY_H
#include "draw3d.h"

/* Host-only race target; menus and HUD retain their original coordinates. */
extern bool SlipRaceDisplay_highRes;
extern bool SlipRaceDisplay_ready;

/* GPU views change projection and clipping, never the indexed drawing surface. */
typedef struct SlipRaceDisplayView {
	SlipDraw3DProjectState *project;
	SlipDraw3DProjectState projection;
	int16_t minX, minY, maxX, maxY;
} SlipRaceDisplayView;

void SlipRaceDisplay_Configure(bool (*size)(int *, int *), void (*save)(void));
void SlipRaceDisplay_Toggle(void *context);
void SlipRaceDisplay_BeginFrame(uint8_t *overlay);
bool SlipRaceDisplay_BeginWorld(SlipDraw3DProjectState *project, SlipRaceDisplayView *saved, uint32_t gameMode);
int32_t SlipRaceDisplay_ScaleWorldOffset(int32_t offset);
void SlipRaceDisplay_EndWorld(const SlipRaceDisplayView *saved);
bool SlipRaceDisplay_BeginMonitor(SlipDraw3DProjectState *project, SlipRaceDisplayView *saved);
void SlipRaceDisplay_EndMonitor(const SlipRaceDisplayView *saved);
bool SlipRaceDisplay_BeginMap(void);
void SlipRaceDisplay_EndMap(void);
void SlipRaceDisplay_EndFrame(void);
void SlipRaceDisplay_DrawOverlay(void);
#endif
