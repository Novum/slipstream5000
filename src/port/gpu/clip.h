#ifndef SLIPSTREAM5000_RACE_GPU_CLIP_H
#define SLIPSTREAM5000_RACE_GPU_CLIP_H
#include "draw3d.h"

void SlipRaceGpu_ClipAttributes(SlipDraw3DDrawRecord *target, const SlipDraw3DDrawRecord *other, uint32_t mode,
                                double screenRatio);
int SlipRaceGpu_SplitScreenXRecord(uint8_t *recordBase, size_t recordBytes, uint32_t targetOffset, uint32_t otherOffset,
                                   uint32_t projectionMode, int32_t clipPlaneX, int32_t limitYMin, int32_t limitYMax,
                                   SlipDraw3DSplitScreenX *result);
int SlipRaceGpu_SplitScreenYRecord(uint8_t *recordBase, size_t recordBytes, uint32_t targetOffset, uint32_t otherOffset,
                                   uint32_t projectionMode, int32_t clipPlaneY, SlipDraw3DSplitScreenY *result);
#endif
