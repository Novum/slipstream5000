#ifndef SLIP_CROSS_EFFECTS_H
#define SLIP_CROSS_EFFECTS_H

#include "view3d.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct SlipObject;

typedef uint32_t (*SlipCrossEffectUpdate)(uint16_t object);

typedef void (*SlipCrossEffectAttach)(uint16_t object);

typedef struct SlipCrossEffectEmission {
	SlipView3DVec16 direction;
	uint32_t speed;
	uint32_t radius;
} SlipCrossEffectEmission;

void SlipCrossEffects_Create(SlipView3DVec32 position, uint32_t count, const SlipCrossEffectEmission *emission,
                             uint16_t material, SlipCrossEffectUpdate update, const SlipView3DMaths *maths,
                             const uint8_t *materialTable, size_t materialBytes);

extern uint32_t SlipCrossEffects_drawMode;
extern SlipCrossEffectAttach SlipCrossEffects_attach;
extern uint32_t SlipCrossEffects_initialized;
extern uint16_t SlipCrossEffects_paletteStart;
extern uint16_t SlipCrossEffects_paletteCount;

void SlipCrossEffects_Initialize(uint32_t drawMode, SlipCrossEffectAttach attach);
void SlipCrossEffects_Cleanup(void);

bool SlipCrossEffects_Color(struct SlipObject *objects, size_t objectBytes, uint16_t object,
                            uint32_t *colorPayloadBits);

typedef struct SlipCrossEffectState {
	SlipCrossEffectUpdate update;
	uint16_t remainingLifetime;
	int16_t updateStepMultiplier;
} SlipCrossEffectState;

uint32_t SlipCrossEffects_Event(uint32_t eventCode, uint32_t eventPayload, uint32_t eventValue, uint32_t eventFlags,
                                uint16_t object, uintptr_t dispatchData, uint32_t dispatchFrame);

/* False denotes carry set. Objects must be the bound native object pool. */
bool SlipCrossEffects_Select(const struct SlipObject *objects, size_t objectBytes, uint16_t *selected);
bool SlipCrossEffects_Reclaim(const struct SlipObject *objects, size_t objectBytes, uint32_t eventCode,
                              uint32_t eventPayload, uint32_t eventValue, uint32_t eventFlags, uintptr_t dispatchData,
                              uint32_t dispatchFrame);

#endif
