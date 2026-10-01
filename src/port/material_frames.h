#ifndef SLIPSTREAM5000_MATERIAL_FRAMES_H
#define SLIPSTREAM5000_MATERIAL_FRAMES_H
#include "draw3d.h"

typedef struct SlipMaterialFrameCalls {
	void *context;
	bool (*find)(void *, const char *name, uint16_t *resource);
} SlipMaterialFrameCalls;

void SlipMaterial_LoadFrames(SlipDraw3DMaterialTable *, char name[13], const SlipMaterialFrameCalls *);
void SlipMaterial_ResetFrames(SlipDraw3DMaterialTable *, char name[13], const uint32_t *maximumFrame,
                              const SlipMaterialFrameCalls *);

uint32_t SlipMaterial_GetFrame(const SlipDraw3DMaterialTable *table, uint16_t loaded, uint32_t *materialIndex,
                               uint32_t frame);
void SlipMaterial_SetFrame(SlipDraw3DMaterialTable *table, uint16_t loaded, uint32_t *materialIndex, uint32_t frame,
                           uint16_t handle);
#endif
