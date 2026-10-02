#ifndef SLIPSTREAM5000_RACE_MAP_H
#define SLIPSTREAM5000_RACE_MAP_H

#include "track_world.h"

#include <stddef.h>
#include <stdint.h>

/* Call before releasing or replacing a track resource bundle. */
void SlipRaceMap_Reset(void);

void SlipRaceMap_Draw(int32_t cameraDistance, uint8_t routeColor, uint8_t finishColor, uint16_t objectColor,
                      uint16_t playerObject, uint16_t rivalObject, int16_t centerX, int16_t centerY,
                      uint16_t playerColor, uint16_t rivalColor, const SlipView3DMaths *maths,
                      SlipDraw3DProjectState *projectState, SlipObject *objectTable, size_t objectTableBytes,
                      const uint8_t *trkData, size_t trkBytes, const uint8_t *trdData, size_t trdBytes,
                      const SlipTrackSlotRecord *slots, size_t slotCount, uint32_t slotListBaseAddress,
                      uint32_t activeListAddress);

#endif
