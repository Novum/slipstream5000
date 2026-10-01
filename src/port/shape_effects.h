#ifndef SLIP_SHAPE_EFFECTS_H
#define SLIP_SHAPE_EFFECTS_H

#include "view3d.h"
#include <stdbool.h>
#include <stdint.h>

extern uint32_t SlipShapeEffects_initialized;
void SlipShapeEffects_Initialize(void);
void SlipShapeEffects_Cleanup(void);
uint32_t SlipShapeEffects_Notify(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue, uint32_t eventFlags,
                                 uint16_t object, uintptr_t dispatchData, uint32_t dispatchFrame);

bool SlipShapeEffects_Create(SlipView3DVec32 position, const SlipView3DMatrix *matrix, uint16_t shape,
                             uint32_t drawDataPrefix, uint16_t *createdObject);

struct SlipObject;
bool SlipShapeEffects_SelectOldest(const struct SlipObject *objects, size_t objectBytes, uint16_t *selected);
bool SlipShapeEffects_Reclaim(const struct SlipObject *objects, size_t objectBytes, uint32_t eventCode,
                              uint32_t eventPayload, uint32_t eventValue, uint32_t eventFlags, uintptr_t dispatchData,
                              uint32_t dispatchFrame);

#endif
