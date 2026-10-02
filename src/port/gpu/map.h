#ifndef SLIPSTREAM5000_GPU_MAP_H
#define SLIPSTREAM5000_GPU_MAP_H

#include "draw3d.h"
#include "view3d.h"
#include <stddef.h>
#include <stdint.h>

/* Call before releasing or replacing a track resource bundle. */
void SlipRaceGpu_ResetMap(void);
void SlipRaceGpu_DrawMapRoute(const SlipView3DMatrix *matrix, const SlipView3DVec32 *camera,
                              const SlipDraw3DProjectState *project, const uint8_t *data, size_t bytes, size_t base,
                              uint16_t count, uint8_t color);
void SlipRaceGpu_DrawMapFinish(const SlipView3DMatrix *matrix, const SlipView3DVec32 *camera,
                               const SlipDraw3DProjectState *project, SlipView3DVec32 world, uint8_t color);

#endif
