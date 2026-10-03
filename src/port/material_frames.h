#ifndef SLIPSTREAM5000_MATERIAL_FRAMES_H
#define SLIPSTREAM5000_MATERIAL_FRAMES_H
#include "draw3d.h"

enum { SLIP_MATERIAL_FRAME_NAME_BYTES = SLIP_MAT_TEXTURE_NAME_BYTES + 1 };

typedef struct SlipMaterialFrameCalls {
	void *context;
	bool (*find)(void *, const char *name, uint16_t *resource);
} SlipMaterialFrameCalls;

void SlipMaterial_LoadFrames(SlipDraw3DMaterialTable *, char name[SLIP_MATERIAL_FRAME_NAME_BYTES],
                             const SlipMaterialFrameCalls *);
void SlipMaterial_ResetFrames(SlipDraw3DMaterialTable *, char name[SLIP_MATERIAL_FRAME_NAME_BYTES],
                              const uint32_t *maximumFrame, const SlipMaterialFrameCalls *);

uint32_t SlipMaterial_GetFrame(const SlipDraw3DMaterialTable *table, uint16_t loaded, uint32_t *materialIndex,
                               uint32_t frame);
void SlipMaterial_SetFrame(SlipDraw3DMaterialTable *table, uint16_t loaded, uint32_t *materialIndex, uint32_t frame,
                           uint16_t handle);
#endif
