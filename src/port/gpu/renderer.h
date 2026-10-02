#ifndef SLIPSTREAM5000_RACE_GPU_H
#define SLIPSTREAM5000_RACE_GPU_H
#include "draw3d.h"
#include "raster/raster.h"
#include <SDL3/SDL.h>
bool SlipRaceGpu_Initialize(SDL_Renderer *renderer);
void SlipRaceGpu_Shutdown(void);
bool SlipRaceGpu_Available(void);
bool SlipRaceGpu_Active(void);
void SlipRaceGpu_BeginFrame(int width, int height);
void SlipRaceGpu_BeginWorld(int lineWidth);
void SlipRaceGpu_EndWorld(void);
void SlipRaceGpu_BeginMap(void);
void SlipRaceGpu_EndMap(void);
bool SlipRaceGpu_Present(void);
void SlipRaceGpu_Texture(const uint8_t *payload, size_t bytes, uint32_t scroll, const RasterTexturedPoint *points,
                         uint32_t count);

/* Source coordinates determine topology; view coordinates determine clip position. */
typedef struct {
	SlipDraw3DVec32 source, view;
	uint16_t u, v;
} SlipRaceGpuWorldPoint;

void SlipRaceGpu_WorldTexture(const uint8_t *payload, size_t bytes, uint32_t scroll,
                              const SlipRaceGpuWorldPoint *points, uint32_t count,
                              const SlipDraw3DProjectState *projection);
void SlipRaceGpu_Flat(const RasterPoint *points, uint32_t count, uint8_t color, uint8_t dither);
void SlipRaceGpu_Shaded(const RasterShadedPoint *points, uint32_t count);
void SlipRaceGpu_MapRibbon(const SDL_FPoint *points, uint32_t count, bool closed, uint8_t color);
void SlipRaceGpu_MapMarker(SDL_FPoint point, uint8_t color);
void SlipRaceGpu_Line(uint8_t color, int x0, int y0, int x1, int y1);
void SlipRaceGpu_Sprite(const uint8_t *payload, size_t bytes, int left, int top, int right, int bottom);

void SlipRaceGpu_Overlay(const uint8_t *pixels, const uint8_t *mask, int left, int top, int right, int bottom,
                         SDL_FRect destination);
#endif
